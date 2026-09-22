/* Battery BMS over BLE - EXPERIMENTAL.
 *
 * Connects to the battery chosen on the Battery tab and:
 *  - lists all GATT services and subscribes to every notification (diagnostics,
 *    logged to the Serial Monitor), so unknown protocols can be identified
 *  - if the JBD service (0xFF00) is present, polls it with the JBD protocol
 *    (used by many ECO-WORTHY batteries) and decodes the answers.
 *
 * Only one BLE connection per battery is possible: close the ECO-WORTHY app first.
 * Read-only: nothing is ever written to the BMS except the two JBD read requests.
 */
#include "app.h"
#include <NimBLEDevice.h>

static SemaphoreHandle_t bms_mtx;
static BmsData bms;  // protected by bms_mtx
static BmsSeen seen[MAX_BMS_SEEN];

static volatile bool connect_req = false;
static NimBLEClient *client = nullptr;
static NimBLERemoteCharacteristic *jbd_write = nullptr;

#define BMS_POLL_MS 5000     // JBD request interval
#define BMS_RETRY_MS 30000   // reconnect interval after a failure

/* ---------- helpers ---------- */
static void set_status(const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  strlcpy(bms.status, buf, sizeof(bms.status));
  xSemaphoreGive(bms_mtx);
  USBSerial.printf("BMS: %s\n", buf);
}

static void hex_line(const uint8_t *d, size_t n, char *out, size_t out_len) {
  out[0] = 0;
  for (size_t i = 0; i < n && strlen(out) + 4 < out_len; i++) {
    char h[4];
    snprintf(h, sizeof(h), "%02X ", d[i]);
    strlcat(out, h, out_len);
  }
}

const char *bms_protection_text(uint16_t b) {
  if (b & 0x0400) return "Short circuit";
  if (b & 0x0001) return "Cell overvoltage";
  if (b & 0x0002) return "Cell undervoltage";
  if (b & 0x0004) return "Pack overvoltage";
  if (b & 0x0008) return "Pack undervoltage";
  if (b & 0x0010) return "Charge overtemperature";
  if (b & 0x0020) return "Charge undertemperature";
  if (b & 0x0040) return "Discharge overtemperature";
  if (b & 0x0080) return "Discharge undertemperature";
  if (b & 0x0100) return "Charge overcurrent";
  if (b & 0x0200) return "Discharge overcurrent";
  if (b & 0x0800) return "BMS IC error";
  if (b & 0x1000) return "MOSFET locked";
  if (b) return "Protection active";
  return NULL;
}

/* ---------- device list (from the BLE scan) ---------- */
void bms_note_adv(const char *name, const char *mac, uint8_t addr_type, int rssi) {
  if (!name || !name[0] || !bms_mtx) return;
  char m[18];
  strlcpy(m, mac, sizeof(m));
  for (char *c = m; *c; c++) *c = toupper(*c);

  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  int slot = -1, oldest = 0;
  for (int i = 0; i < MAX_BMS_SEEN; i++) {
    if (seen[i].used && !strcmp(seen[i].mac, m)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_BMS_SEEN; i++) {
      if (!seen[i].used) {
        slot = i;
        break;
      }
      if (seen[i].last_seen < seen[oldest].last_seen) oldest = i;
    }
    if (slot < 0) slot = oldest;
  }
  seen[slot].used = true;
  strlcpy(seen[slot].name, name, sizeof(seen[slot].name));
  strlcpy(seen[slot].mac, m, sizeof(seen[slot].mac));
  seen[slot].addr_type = addr_type;
  seen[slot].rssi = rssi;
  seen[slot].last_seen = millis();
  xSemaphoreGive(bms_mtx);
}

int bms_get_seen(BmsSeen *out, int max) {
  int n = 0;
  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  for (int i = 0; i < MAX_BMS_SEEN && n < max; i++)
    if (seen[i].used) out[n++] = seen[i];
  xSemaphoreGive(bms_mtx);
  return n;
}

void bms_get(BmsData *out) {
  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  *out = bms;
  xSemaphoreGive(bms_mtx);
}

void bms_connect_to(const char *mac, uint8_t addr_type, const char *name) {
  LOCK();
  strlcpy(bms_cfg.mac, mac, sizeof(bms_cfg.mac));
  bms_cfg.addr_type = addr_type;
  strlcpy(bms_cfg.name, name, sizeof(bms_cfg.name));
  UNLOCK();
  cmd_save_bms = true;
  connect_req = true;
}

/* ---------- JBD protocol ----------
   Request:  DD A5 <cmd> 00 <chk_hi> <chk_lo> 77
   Response: DD <cmd> <status> <len> <data...> <chk_hi> <chk_lo> 77
   The response may arrive split over several notifications. */
static uint8_t jbd_buf[160];
static size_t jbd_len = 0;

static uint16_t be16(const uint8_t *p) {
  return (p[0] << 8) | p[1];
}

static void jbd_parse(uint8_t cmd, const uint8_t *p, size_t n) {
  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  if (cmd == 0x03 && n >= 23) {  // basic info
    bms.volt = be16(p) / 100.0f;
    bms.curr = (int16_t)be16(p + 2) / 100.0f;
    bms.remain_ah = be16(p + 4) / 100.0f;
    bms.nominal_ah = be16(p + 6) / 100.0f;
    bms.cycles = be16(p + 8);
    bms.protection = be16(p + 16);
    bms.soc = p[19];
    bms.chg_fet = p[20] & 0x01;
    bms.dsg_fet = p[20] & 0x02;
    bms.ntemp = 0;
    for (int i = 0; i < p[22] && i < BMS_MAX_TEMPS && 24 + 2 * i < (int)n; i++)
      bms.temp[bms.ntemp++] = ((int)be16(p + 23 + 2 * i) - 2731) / 10.0f;
    bms.valid = true;
    bms.updated = millis();
  } else if (cmd == 0x04) {  // cell voltages
    bms.ncell = 0;
    for (size_t i = 0; i + 1 < n && bms.ncell < BMS_MAX_CELLS; i += 2)
      bms.cell[bms.ncell++] = be16(p + i) / 1000.0f;
    bms.cells_valid = true;
  }
  xSemaphoreGive(bms_mtx);
}

static void jbd_feed(const uint8_t *d, size_t n) {
  if (n && d[0] == 0xDD) jbd_len = 0;  // start of a new frame
  if (jbd_len + n > sizeof(jbd_buf)) {
    jbd_len = 0;
    return;
  }
  memcpy(jbd_buf + jbd_len, d, n);
  jbd_len += n;
  if (jbd_len < 7 || jbd_buf[0] != 0xDD) return;

  size_t len = jbd_buf[3];
  size_t total = 4 + len + 3;
  if (jbd_len < total) return;  // wait for the rest
  if (jbd_buf[total - 1] != 0x77) {
    USBSerial.println("BMS: JBD frame without end byte");
    jbd_len = 0;
    return;
  }
  uint16_t sum = 0;
  for (size_t i = 2; i < 4 + len; i++) sum += jbd_buf[i];
  uint16_t chk = (uint16_t)(0x10000 - sum);
  uint16_t got = (jbd_buf[4 + len] << 8) | jbd_buf[5 + len];
  if (chk != got) {
    USBSerial.printf("BMS: JBD checksum mismatch (%04X != %04X)\n", chk, got);
  } else if (jbd_buf[2] != 0) {
    USBSerial.printf("BMS: JBD error status 0x%02X for cmd 0x%02X\n", jbd_buf[2], jbd_buf[1]);
  } else {
    jbd_parse(jbd_buf[1], jbd_buf + 4, len);
  }
  jbd_len = 0;
}

static void jbd_request(uint8_t cmd) {
  if (!jbd_write) return;
  uint16_t chk = (uint16_t)(0x10000 - cmd);  // sum over cmd and length (0)
  uint8_t req[7] = { 0xDD, 0xA5, cmd, 0x00, (uint8_t)(chk >> 8), (uint8_t)chk, 0x77 };
  jbd_write->writeValue(req, sizeof(req), !jbd_write->canWriteNoResponse());
}

/* ---------- notifications (NimBLE host task) ---------- */
static void notify_cb(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool is_notify) {
  char hex[100];
  hex_line(data, len, hex, sizeof(hex));
  USBSerial.printf("BMS notify %s (%u bytes): %s\n", chr->getUUID().toString().c_str(), (unsigned)len, hex);

  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  strlcpy(bms.last_frame, hex, sizeof(bms.last_frame));
  xSemaphoreGive(bms_mtx);

  if (chr->getUUID() == NimBLEUUID((uint16_t)0xFF01)) jbd_feed(data, len);
}

/* ---------- connection ---------- */
static bool do_connect() {
  BmsCfg cfg;
  LOCK();
  cfg = bms_cfg;
  UNLOCK();
  if (!cfg.mac[0]) return false;

  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  bms.connected = bms.is_jbd = bms.valid = bms.cells_valid = false;
  bms.services[0] = bms.last_frame[0] = 0;
  xSemaphoreGive(bms_mtx);
  jbd_write = nullptr;

  if (!client) {
    client = NimBLEDevice::createClient();
    client->setConnectTimeout(8000);
  }
  if (client->isConnected()) client->disconnect();

  set_status("Connecting to %s...", cfg.name);
  ble_pause_scan(true);  // scanning and connecting at the same time is unreliable
  vTaskDelay(pdMS_TO_TICKS(200));
  bool ok = client->connect(NimBLEAddress(std::string(cfg.mac), cfg.addr_type));
  ble_pause_scan(false);
  if (!ok) {
    set_status("Could not connect to %s. Is the phone app closed?", cfg.name);
    return false;
  }

  /* Diagnostics: list everything and subscribe to all notifications */
  char services[200] = "";
  USBSerial.printf("BMS: connected to %s (%s). GATT services:\n", cfg.name, cfg.mac);
  for (NimBLERemoteService *svc : client->getServices(true)) {
    std::string su = svc->getUUID().toString();
    USBSerial.printf("  service %s\n", su.c_str());
    if (strlen(services) + su.length() + 2 < sizeof(services)) {
      if (services[0]) strlcat(services, " ", sizeof(services));
      strlcat(services, su.c_str(), sizeof(services));
    }
    for (NimBLERemoteCharacteristic *chr : svc->getCharacteristics(true)) {
      USBSerial.printf("    char %s%s%s%s%s\n", chr->getUUID().toString().c_str(),
                       chr->canRead() ? " read" : "", chr->canWrite() ? " write" : "",
                       chr->canWriteNoResponse() ? " write-no-resp" : "",
                       (chr->canNotify() || chr->canIndicate()) ? " notify" : "");
      if (chr->canNotify()) chr->subscribe(true, notify_cb);
      else if (chr->canIndicate()) chr->subscribe(false, notify_cb);
    }
  }

  /* JBD? */
  NimBLERemoteService *jbd = client->getService(NimBLEUUID((uint16_t)0xFF00));
  if (jbd) jbd_write = jbd->getCharacteristic(NimBLEUUID((uint16_t)0xFF02));

  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  bms.connected = true;
  bms.is_jbd = (jbd_write != nullptr);
  strlcpy(bms.services, services, sizeof(bms.services));
  xSemaphoreGive(bms_mtx);

  if (jbd_write) set_status("Connected to %s (JBD protocol)", cfg.name);
  else set_status("Connected to %s. Unknown protocol - see Serial log", cfg.name);
  return true;
}

static void bms_task(void *arg) {
  uint32_t next_try = 0, next_poll = 0;
  bool had_target = false;
  for (;;) {
    LOCK();
    bool have_target = bms_cfg.mac[0] != 0;
    UNLOCK();
    if (have_target && !had_target) connect_req = true;  // saved battery at boot
    had_target = have_target;

    bool connected = client && client->isConnected();
    if (!connected) {
      xSemaphoreTake(bms_mtx, portMAX_DELAY);
      bool was = bms.connected;
      bms.connected = false;
      xSemaphoreGive(bms_mtx);
      if (was) {
        set_status("Connection lost - retrying...");
        next_try = millis() + 3000;
      }
    }

    if (have_target && (connect_req || (!connected && (int32_t)(millis() - next_try) >= 0))) {
      connect_req = false;
      if (!do_connect()) next_try = millis() + BMS_RETRY_MS;
      next_poll = millis() + 500;
    }

    if (connected && jbd_write && (int32_t)(millis() - next_poll) >= 0) {
      jbd_request(0x03);  // basic info
      vTaskDelay(pdMS_TO_TICKS(400));
      jbd_request(0x04);  // cell voltages
      next_poll = millis() + BMS_POLL_MS;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void bms_start() {
  bms_mtx = xSemaphoreCreateMutex();
  memset(&bms, 0, sizeof(bms));
  strlcpy(bms.status, "No battery chosen", sizeof(bms.status));
  xTaskCreatePinnedToCore(bms_task, "bms", 4096, NULL, 1, NULL, 0);
}
