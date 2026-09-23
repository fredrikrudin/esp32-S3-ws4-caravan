/* Shelly devices over Bluetooth (BLE RPC) - framework, off by default.
 *
 * Shelly Gen2/Gen3 devices expose a JSON-RPC channel over GATT:
 *   service  5f6d4f53-5f52-5043-5f53-56435f49445f
 *   data     5f6d4f53-5f52-5043-5f64-6174615f5f5f   read/write, the JSON itself
 *   tx_ctl   5f6d4f53-5f52-5043-5f72-785f63746c5f   write: length of the request
 *   rx_ctl   5f6d4f53-5f52-5043-5f74-785f63746c5f   notify: length of the answer
 * A request is: write the 4-byte big-endian length to tx_ctl, then the JSON to
 * data in MTU-sized chunks. The answer arrives the same way: rx_ctl notifies the
 * length, then data is read until that many bytes have arrived.
 *
 * Only two methods are used: Switch.GetStatus (on/off + power) and Switch.Set.
 *
 * Note: Shelly requires pairing (bonding) for BLE RPC, so the first connection
 * to a device bonds with it. Devices are polled one at a time, connecting and
 * disconnecting again, so only one BLE connection is open at any moment.
 * See https://shelly-api-docs.shelly.cloud/gen2/ for the RPC methods.
 */
#include "app.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>

#define SHELLY_POLL_MS 15000  // how often each device is read
#define SHELLY_TIMEOUT 6000   // RPC answer timeout

static const char *SHELLY_SVC = "5f6d4f53-5f52-5043-5f53-56435f49445f";
static const char *SHELLY_DATA = "5f6d4f53-5f52-5043-5f64-6174615f5f5f";
static const char *SHELLY_TX = "5f6d4f53-5f52-5043-5f72-785f63746c5f";
static const char *SHELLY_RX = "5f6d4f53-5f52-5043-5f74-785f63746c5f";

static SemaphoreHandle_t sh_mtx;
static ShellyData sh_data[MAX_SHELLY];
static volatile int8_t set_req_idx = -1;  // device the UI wants to switch
static volatile bool set_req_on = false;

static NimBLEClient *cl = nullptr;
static NimBLERemoteCharacteristic *ch_data, *ch_tx;
static volatile uint32_t rx_len = 0;

/* ---------- shared state ---------- */
static void sh_status(int i, const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  strlcpy(sh_data[i].status, buf, sizeof(sh_data[i].status));
  xSemaphoreGive(sh_mtx);
  USBSerial.printf("Shelly %d: %s\n", i + 1, buf);
}

void shelly_get(int idx, ShellyData *out) {
  if (idx < 0 || idx >= MAX_SHELLY || !sh_mtx) {
    memset(out, 0, sizeof(*out));
    return;
  }
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  *out = sh_data[idx];
  xSemaphoreGive(sh_mtx);
}

void shelly_set(int idx, bool on) {
  set_req_on = on;
  set_req_idx = idx;  // picked up by the task
}

void shelly_add(const char *mac, uint8_t addr_type, const char *name) {
  LOCK();
  for (int i = 0; i < MAX_SHELLY; i++) {
    if (shelly_cfg[i].used) continue;
    shelly_cfg[i].used = true;
    strlcpy(shelly_cfg[i].mac, mac, sizeof(shelly_cfg[i].mac));
    shelly_cfg[i].addr_type = addr_type;
    strlcpy(shelly_cfg[i].name, name, sizeof(shelly_cfg[i].name));
    break;
  }
  UNLOCK();
  cmd_save_shelly = true;
}

void shelly_remove(int idx) {
  if (idx < 0 || idx >= MAX_SHELLY) return;
  LOCK();
  memset(&shelly_cfg[idx], 0, sizeof(ShellyCfg));
  UNLOCK();
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  memset(&sh_data[idx], 0, sizeof(ShellyData));
  xSemaphoreGive(sh_mtx);
  cmd_save_shelly = true;
}

/* ---------- RPC over GATT ---------- */
static void rx_notify(NimBLERemoteCharacteristic *chr, uint8_t *d, size_t len, bool is_notify) {
  if (len >= 4) rx_len = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | (d[2] << 8) | d[3];
}

static bool rpc_call(const char *json, String &answer) {
  if (!ch_data || !ch_tx) return false;
  uint32_t n = strlen(json);
  uint8_t hdr[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
  rx_len = 0;
  if (!ch_tx->writeValue(hdr, 4, true)) return false;

  uint16_t chunk = cl->getMTU() > 23 ? cl->getMTU() - 3 : 20;
  for (uint32_t off = 0; off < n; off += chunk) {
    uint32_t len = n - off < chunk ? n - off : chunk;
    if (!ch_data->writeValue((const uint8_t *)json + off, len, true)) return false;
  }

  uint32_t t0 = millis();
  while (!rx_len && millis() - t0 < SHELLY_TIMEOUT) vTaskDelay(pdMS_TO_TICKS(20));
  if (!rx_len) return false;

  answer = "";
  while (answer.length() < rx_len && millis() - t0 < SHELLY_TIMEOUT) {
    NimBLEAttValue v = ch_data->readValue();
    if (!v.length()) break;
    answer += v.c_str();
  }
  return answer.length() >= rx_len;
}

/* ---------- one device ---------- */
static bool shelly_connect(const ShellyCfg &c) {
  if (!cl) {
    cl = NimBLEDevice::createClient();
    cl->setConnectTimeout(8000);
  }
  ch_data = ch_tx = nullptr;

  ble_pause_scan(true);
  vTaskDelay(pdMS_TO_TICKS(150));
  bool ok = cl->connect(NimBLEAddress(std::string(c.mac), c.addr_type));
  ble_pause_scan(false);
  if (!ok) return false;

  cl->exchangeMTU();
  NimBLERemoteService *svc = cl->getService(NimBLEUUID(SHELLY_SVC));
  if (!svc) {
    cl->disconnect();
    return false;
  }
  ch_data = svc->getCharacteristic(NimBLEUUID(SHELLY_DATA));
  ch_tx = svc->getCharacteristic(NimBLEUUID(SHELLY_TX));
  NimBLERemoteCharacteristic *ch_rx = svc->getCharacteristic(NimBLEUUID(SHELLY_RX));
  if (!ch_data || !ch_tx || !ch_rx) {
    cl->disconnect();
    return false;
  }
  ch_rx->subscribe(true, rx_notify);
  return true;
}

/* Reads on/off and power; optionally switches first */
static bool shelly_poll(int i, bool do_set, bool on) {
  ShellyCfg c;
  LOCK();
  c = shelly_cfg[i];
  UNLOCK();
  if (!c.used) return false;

  sh_status(i, do_set ? "Switching..." : "Reading...");
  if (!shelly_connect(c)) {
    sh_status(i, "No connection (paired?)");
    xSemaphoreTake(sh_mtx, portMAX_DELAY);
    sh_data[i].connected = false;
    xSemaphoreGive(sh_mtx);
    return false;
  }

  String answer;
  bool ok = true;
  if (do_set) {
    char req[96];
    snprintf(req, sizeof(req), "{\"id\":1,\"method\":\"Switch.Set\",\"params\":{\"id\":0,\"on\":%s}}", on ? "true" : "false");
    ok = rpc_call(req, answer);
    if (!ok) sh_status(i, "Switching failed");
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (ok) {
    ok = rpc_call("{\"id\":2,\"method\":\"Switch.GetStatus\",\"params\":{\"id\":0}}", answer);
    if (!ok) sh_status(i, "No answer");
  }

  if (ok) {
    JsonDocument doc;
    if (deserializeJson(doc, answer)) {
      sh_status(i, "Bad answer");
      ok = false;
    } else {
      JsonObject r = doc["result"];
      if (r.isNull()) {
        sh_status(i, "%s", (const char *)(doc["error"]["message"] | "RPC error"));
        ok = false;
      } else {
        xSemaphoreTake(sh_mtx, portMAX_DELAY);
        sh_data[i].on = r["output"] | false;
        sh_data[i].power = r["apower"] | NAN;
        sh_data[i].voltage = r["voltage"] | NAN;
        sh_data[i].current = r["current"] | NAN;
        sh_data[i].valid = true;
        sh_data[i].connected = true;
        sh_data[i].updated = millis();
        xSemaphoreGive(sh_mtx);
        sh_status(i, "OK");
      }
    }
  }

  cl->disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  sh_data[i].connected = false;
  xSemaphoreGive(sh_mtx);
  return ok;
}

static void shelly_task(void *arg) {
  static uint32_t next_poll[MAX_SHELLY] = { 0 };
  for (;;) {
    if (!feat_shelly) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    /* the UI asked for a switch: do that first */
    int req = set_req_idx;
    if (req >= 0) {
      set_req_idx = -1;
      shelly_poll(req, true, set_req_on);
      next_poll[req] = millis() + SHELLY_POLL_MS;
    }

    /* otherwise read one device whose turn it is */
    for (int i = 0; i < MAX_SHELLY; i++) {
      LOCK();
      bool used = shelly_cfg[i].used;
      UNLOCK();
      if (!used || (int32_t)(millis() - next_poll[i]) < 0) continue;
      shelly_poll(i, false, false);
      next_poll[i] = millis() + SHELLY_POLL_MS;
      break;  // one device per round, so switching stays responsive
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void shelly_start() {
  sh_mtx = xSemaphoreCreateMutex();
  memset(sh_data, 0, sizeof(sh_data));
  for (int i = 0; i < MAX_SHELLY; i++) strlcpy(sh_data[i].status, "Not read yet", sizeof(sh_data[i].status));
  /* Shelly requires bonding for RPC; "just works" pairing, no PIN */
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  xTaskCreatePinnedToCore(shelly_task, "shelly", 6144, NULL, 1, NULL, 0);
}
