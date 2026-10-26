// esp32-S3-ws4-caravan v1.0
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
#include <WiFi.h>
#include <HTTPClient.h>

#define SHELLY_POLL_MS 15000   // how often each device is read
#define SHELLY_TIMEOUT 10000   // BLE answer timeout (10 s, as in esp32-shelly-ble-rpc)
#define SHELLY_HTTP_MS 4000    // WiFi request timeout
#define SHELLY_MAX_BACKOFF 8   // failures beyond which the interval stops growing

static const char *SHELLY_SVC = "5f6d4f53-5f52-5043-5f53-56435f49445f";
static const char *SHELLY_DATA = "5f6d4f53-5f52-5043-5f64-6174615f5f5f";
/* The mongoose-os RPC profile. TX control ("_tx_ctl_", 0x74 0x78) is where the
   request length goes, RX control ("_rx_ctl_", 0x72 0x78) gives the answer's
   length. These two were swapped before, so the Shelly never got a request. */
static const char *SHELLY_TX = "5f6d4f53-5f52-5043-5f74-785f63746c5f";
static const char *SHELLY_RX = "5f6d4f53-5f52-5043-5f72-785f63746c5f";

static SemaphoreHandle_t sh_mtx;
static ShellyData sh_data[MAX_SHELLY];
static volatile int8_t set_req_idx = -1;  // device the UI wants to switch
static volatile bool set_req_on = false;

static NimBLEClient *cl = nullptr;
static NimBLERemoteCharacteristic *ch_data, *ch_tx, *ch_rx;
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
  serf("Shelly %d: %s\n", i + 1, buf);
}

/* True when Shelly is on and at least one device has been added */
bool shelly_present() {
  if (!feat_shelly) return false;
  for (int i = 0; i < MAX_SHELLY; i++)
    if (shelly_cfg[i].used) return true;
  return false;
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

static int free_slot() {
  for (int i = 0; i < MAX_SHELLY; i++)
    if (!shelly_cfg[i].used) return i;
  return -1;
}

void shelly_add(const char *mac, uint8_t addr_type, const char *name) {
  LOCK();
  int i = free_slot();
  if (i >= 0) {
    shelly_cfg[i] = {};
    shelly_cfg[i].used = true;
    shelly_cfg[i].transport = SHELLY_BLE;
    shelly_cfg[i].addr_type = addr_type;
    strlcpy(shelly_cfg[i].mac, mac, sizeof(shelly_cfg[i].mac));
    strlcpy(shelly_cfg[i].name, name, sizeof(shelly_cfg[i].name));
  }
  UNLOCK();
  cmd_save_shelly = true;
}

void shelly_add_wifi(const char *host, const char *name) {
  LOCK();
  int i = free_slot();
  if (i >= 0) {
    shelly_cfg[i] = {};
    shelly_cfg[i].used = true;
    shelly_cfg[i].transport = SHELLY_WIFI;
    strlcpy(shelly_cfg[i].host, host, sizeof(shelly_cfg[i].host));
    strlcpy(shelly_cfg[i].name, name[0] ? name : host, sizeof(shelly_cfg[i].name));
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
  if (!ch_data || !ch_tx || !ch_rx) return false;
  uint32_t n = strlen(json);
  uint8_t hdr[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
  rx_len = 0;
  if (!ch_tx->writeValue(hdr, 4, !ch_tx->canWriteNoResponse())) return false;

  uint16_t chunk = cl->getMTU() > 23 ? cl->getMTU() - 3 : 20;
  for (uint32_t off = 0; off < n; off += chunk) {
    uint32_t len = n - off < chunk ? n - off : chunk;
    if (!ch_data->writeValue((const uint8_t *)json + off, len, !ch_data->canWriteNoResponse())) return false;
    if (off + len < n) vTaskDelay(pdMS_TO_TICKS(10));
  }

  /* the answer's length: notified, or read from RX control until it is there */
  uint32_t t0 = millis(), next_read = t0 + 300;
  while (!rx_len && millis() - t0 < SHELLY_TIMEOUT) {
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!rx_len && (int32_t)(millis() - next_read) >= 0 && ch_rx->canRead()) {
      next_read = millis() + 250;
      NimBLEAttValue v = ch_rx->readValue();
      if (v.length() >= 4) {
        const uint8_t *d = v.data();
        rx_len = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | (d[2] << 8) | d[3];
      }
    }
  }
  if (!rx_len) return false;

  answer = "";
  while (answer.length() < rx_len && millis() - t0 < SHELLY_TIMEOUT) {
    NimBLEAttValue v = ch_data->readValue();
    if (!v.length()) {  // not ready yet: try again shortly
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    answer.concat((const char *)v.data(), v.length());
  }
  return answer.length() >= rx_len;
}

/* ---------- WiFi: the same RPC methods over HTTP ---------- */
static bool http_rpc(const ShellyCfg &c, const char *path, String &answer) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(SHELLY_HTTP_MS);
  http.setConnectTimeout(SHELLY_HTTP_MS);
  String url = String("http://") + c.host + path;
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = (code == HTTP_CODE_OK);
  if (ok) answer = http.getString();
  else dlogf("Shelly HTTP %d for %s", code, url.c_str());
  http.end();
  return ok;
}

/* ---------- one device ---------- */
/* Shelly firmware 2.0 and later only answers RPC over Bluetooth on an encrypted
   connection with a device it has paired with ("Just Works": no code to type).
   Pairing happens on the first connection while the Shelly is in pairing mode
   (Shelly app or web page: Bluetooth -> Pair new device, 3 minutes); the keys
   are kept in flash on both sides, so later connections just encrypt. */
static bool paired_failed = false;  // the last attempt connected but could not pair or encrypt

/* A Bluetooth read that never ends (the Shelly stops answering halfway, without
   the connection dropping) would stop all Shelly control. shelly_watch(), called
   from loop(), notices it and drops the connection, which ends every wait. */
static volatile uint32_t bt_busy_since = 0;
static const char *volatile bt_stage = "";
static volatile int bt_dev = -1;

void shelly_watch() {
  static uint32_t warned_for = 0;
  uint32_t since = bt_busy_since;
  /* signed: the task may have just set "since" (millis() | 1) a moment after this
     millis(), and unsigned that looked like 49 days and raised a false alarm */
  if (!since || (int32_t)(millis() - since) < 40000 || warned_for == since) return;
  warned_for = since;
  log_fault("Shelly %d: no progress for 40 s (at: %s) - dropping the Bluetooth connection", bt_dev + 1, bt_stage);
  if (cl && cl->isConnected()) cl->disconnect();
}

static bool shelly_connect(const ShellyCfg &c) {
  if (!cl) {
    /* as esp32-shelly-ble-rpc does it: bond, no passkey, and legacy pairing
       (with "secure connections" the Shelly encrypted the link but kept no bond) */
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setMTU(517);  // fewer pieces per RPC message
    cl = NimBLEDevice::createClient();
    cl->setConnectTimeout(8000);
  }
  paired_failed = false;
  ch_data = ch_tx = ch_rx = nullptr;

  ble_pause_scan(true);
  vTaskDelay(pdMS_TO_TICKS(150));
  bool ok = cl->connect(NimBLEAddress(std::string(c.mac), c.addr_type));
  ble_pause_scan(false);
  if (!ok) return false;

  bt_stage = "pairing / encrypting";
  bool secured = cl->secureConnection();  // pairs the first time, encrypts after that
  if (!secured) {
    /* The board still has a pairing the Shelly no longer has (the Shelly was reset,
       or its list of paired devices was cleared). Encrypting with the old keys can
       never work, and NimBLE won't pair afresh while it keeps them: forget them and
       pair again at once, which works while the Shelly is in pairing mode. */
    NimBLEAddress addr(std::string(c.mac), c.addr_type);
    if (NimBLEDevice::isBonded(addr)) {
      NimBLEDevice::deleteBond(addr);
      logf("Shelly: the Shelly no longer knows this board; old pairing removed, pairing again");
      if (cl->isConnected()) secured = cl->secureConnection();  // else on the next try
    }
  }
  static bool logged_ok = false;  // a working connection is logged once, a failing one each time
  if (!secured || !logged_ok) {
    logged_ok = secured;
    /* what the pairing gave: in the log, to see why a Shelly may still refuse */
    NimBLEConnInfo ci = cl->getConnInfo();
    logf("Shelly: %s, encrypted %s, bonded %s, authenticated %s; the board keeps %d pairing(s)",
         secured ? "secured" : "pairing failed", ci.isEncrypted() ? "yes" : "no", ci.isBonded() ? "yes" : "no",
         ci.isAuthenticated() ? "yes" : "no", NimBLEDevice::getNumBonds());
  }
  if (!secured) {
    paired_failed = true;
    cl->disconnect();
    return false;
  }
  bt_stage = "finding the RPC service";
  cl->exchangeMTU();
  NimBLERemoteService *svc = cl->getService(NimBLEUUID(SHELLY_SVC));
  if (!svc) {
    cl->disconnect();
    return false;
  }
  ch_data = svc->getCharacteristic(NimBLEUUID(SHELLY_DATA));
  ch_tx = svc->getCharacteristic(NimBLEUUID(SHELLY_TX));
  ch_rx = svc->getCharacteristic(NimBLEUUID(SHELLY_RX));
  if (!ch_data || !ch_tx || !ch_rx) {
    cl->disconnect();
    return false;
  }
  if (ch_rx->canNotify()) ch_rx->subscribe(true, rx_notify);  // or it is read (rpc_call)
  return true;
}

/* Turns an RPC answer into values. Returns false if it wasn't understood. */
static bool apply_status(int i, const String &answer) {
  JsonDocument doc;
  if (deserializeJson(doc, answer)) {
    sh_status(i, TR("Bad answer"));
    return false;
  }
  JsonObject r = doc["result"].isNull() ? doc.as<JsonObject>() : doc["result"].as<JsonObject>();
  if (r.isNull() || r["output"].isNull()) {
    const char *err = doc["error"]["message"] | "No switch in the answer";
    sh_status(i, "%s", err);
    return false;
  }
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  sh_data[i].on = r["output"] | false;
  sh_data[i].power = r["apower"] | NAN;
  sh_data[i].voltage = r["voltage"] | NAN;
  sh_data[i].current = r["current"] | NAN;
  sh_data[i].valid = true;
  sh_data[i].updated = millis();
  sh_data[i].fails = 0;
  xSemaphoreGive(sh_mtx);
  return true;
}

/* A failure: count it, say why, and let the values go stale after a few tries */
static void note_failure(int i, const char *why) {
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  if (sh_data[i].fails < 255) sh_data[i].fails++;
  uint8_t n = sh_data[i].fails;
  if (n >= 3) sh_data[i].valid = false;  // stop showing values we can no longer trust
  sh_data[i].connected = false;
  snprintf(sh_data[i].status, sizeof(sh_data[i].status), n > 1 ? TR("%s (%u tries)") : "%s", why, n);
  xSemaphoreGive(sh_mtx);
  log_fault("Shelly %d: %s", i + 1, why);
}

/* Reads on/off and power; optionally switches first */
static bool shelly_poll(int i, bool do_set, bool on) {
  ShellyCfg c;
  LOCK();
  c = shelly_cfg[i];
  UNLOCK();
  if (!c.used) return false;

  sh_status(i, do_set ? TR("Switching...") : TR("Reading..."));

  /* ---------------- WiFi ---------------- */
  if (c.transport == SHELLY_WIFI) {
    if (WiFi.status() != WL_CONNECTED) {
      note_failure(i, TR("No WiFi"));
      return false;
    }
    String answer;
    if (do_set) {
      char path[64];
      snprintf(path, sizeof(path), "/rpc/Switch.Set?id=0&on=%s", on ? "true" : "false");
      if (!http_rpc(c, path, answer)) {
        note_failure(i, TR("Switching failed"));
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!http_rpc(c, "/rpc/Switch.GetStatus?id=0", answer)) {
      note_failure(i, TR("No answer over WiFi"));
      return false;
    }
    if (!apply_status(i, answer)) return false;
    sh_status(i, TR("OK over WiFi"));
    return true;
  }

  /* ---------------- Bluetooth ---------------- */
  xSemaphoreTake(ble_conn_mtx, portMAX_DELAY);  // never connect while another task is connecting
  bt_dev = i;
  bt_stage = "connecting";
  bt_busy_since = millis() | 1;
  uint32_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);  // for the "Memory:" log line
  if (!shelly_connect(c)) {
    cl->deleteServices();
    ch_data = ch_tx = ch_rx = nullptr;
    bt_busy_since = 0;
    xSemaphoreGive(ble_conn_mtx);
    mem_note(MEM_SHELLY_BT, (int32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) - (int32_t)free_before);
    note_failure(i, paired_failed ? TR("Not paired - pair on the Shelly, then tap") : TR("No Bluetooth connection"));
    return false;
  }

  String answer;
  bool ok = true;
  if (do_set) {
    bt_stage = "switching";
    char req[96];
    snprintf(req, sizeof(req), "{\"id\":1,\"method\":\"Switch.Set\",\"params\":{\"id\":0,\"on\":%s}}", on ? "true" : "false");
    ok = rpc_call(req, answer);
    if (!ok) note_failure(i, TR("Switching failed"));
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (ok) {
    bt_stage = "reading";
    ok = rpc_call("{\"id\":2,\"method\":\"Switch.GetStatus\",\"params\":{\"id\":0}}", answer);
    if (!ok) note_failure(i, TR("No answer over Bluetooth"));
  }
  if (ok) ok = apply_status(i, answer);
  if (ok) sh_status(i, TR("OK over Bluetooth"));

  bt_stage = "disconnecting";
  cl->disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));
  /* The services and characteristics found on the device stay in internal RAM
     until deleted: let them go, the next read finds them again */
  cl->deleteServices();
  ch_data = ch_tx = ch_rx = nullptr;
  bt_busy_since = 0;
  xSemaphoreGive(ble_conn_mtx);
  mem_note(MEM_SHELLY_BT, (int32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) - (int32_t)free_before);
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  sh_data[i].connected = false;
  xSemaphoreGive(sh_mtx);
  return ok;
}

static uint32_t next_poll[MAX_SHELLY] = { 0 };

void shelly_retry(int idx) {
  if (idx >= 0 && idx < MAX_SHELLY) next_poll[idx] = 0;  // the task picks it up within 200 ms
}

/* After a failure the interval doubles, up to about four minutes, so an absent
   device doesn't keep the radio (or the WiFi) busy. */
static uint32_t poll_delay(int i) {
  uint8_t f;
  xSemaphoreTake(sh_mtx, portMAX_DELAY);
  f = sh_data[i].fails;
  xSemaphoreGive(sh_mtx);
  if (!f) return SHELLY_POLL_MS;
  if (f > SHELLY_MAX_BACKOFF) f = SHELLY_MAX_BACKOFF;
  uint32_t ms = SHELLY_POLL_MS * (1u << (f - 1));
  return ms > 240000 ? 240000 : ms;  // at most 4 minutes (was up to 32): a fixed Shelly is found again soon
}

static void shelly_task(void *arg) {
  /* Bluetooth is started after this task (setup(): WiFi first). Touching NimBLE
     before NimBLEDevice::init() crashed the board at every start. */
  while (!NimBLEDevice::isInitialized()) vTaskDelay(pdMS_TO_TICKS(200));
  vTaskDelay(pdMS_TO_TICKS(2000));  // and let WiFi and the first scan settle
  for (;;) {
    if (!feat_shelly || power_on_battery()) {  // on the LiPo: no connections, only scans
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    /* the UI asked for a switch: do that first */
    int req = set_req_idx;
    if (req >= 0) {
      set_req_idx = -1;
      shelly_poll(req, true, set_req_on);
      next_poll[req] = millis() + poll_delay(req);
    }

    /* otherwise read one device whose turn it is */
    for (int i = 0; i < MAX_SHELLY; i++) {
      LOCK();
      bool used = shelly_cfg[i].used;
      UNLOCK();
      if (!used || (int32_t)(millis() - next_poll[i]) < 0) continue;
      shelly_poll(i, false, false);
      next_poll[i] = millis() + poll_delay(i);
      break;  // one device per round, so switching stays responsive
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void shelly_start() {
  sh_mtx = xSemaphoreCreateMutex();
  memset(sh_data, 0, sizeof(sh_data));
  for (int i = 0; i < MAX_SHELLY; i++) strlcpy(sh_data[i].status, TR("Not read yet"), sizeof(sh_data[i].status));
  /* Shelly requires bonding for RPC; "just works" pairing, no PIN */
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  /* measured: about 700 bytes used of 8 kB, but HTTPClient and JSON can spike,
     so 5 kB keeps a wide margin and gives 3 kB back to the heap */
  xTaskCreatePinnedToCore(shelly_task, "shelly", 5120, NULL, 1, NULL, 0);
}
