/* Battery BMS over BLE.
 *
 * Two protocols are supported, both used by ECO-WORTHY batteries:
 *  - ECO-WORTHY / BWOB: service 0x0001, write 0x0002, notify 0x0003.
 *    Commands AA 00/20/21/22; reply 0x21 has voltage, SOC and capacity,
 *    reply 0x22 has the cell voltages.
 *  - JBD (Jiabaida): service 0xFF00, write 0xFF02, request DD A5 <cmd> ...
 *    If no JBD characteristic is found, every writable one is tried in turn.
 *
 * All GATT services are listed and every notification is logged to the Serial
 * Monitor, so an unknown battery can still be identified.
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

/* Every writable characteristic is a candidate for the JBD request; the one that
   produces a valid answer wins. ECO-WORTHY batteries use the JBD (Jiabaida) protocol,
   but not all of them expose it on the documented 0xFF00/0xFF02 IDs. */
#define MAX_WRITE_CHRS 8
static NimBLERemoteCharacteristic *write_chr[MAX_WRITE_CHRS];
static int write_chr_count = 0;
static int write_chr_idx = 0;
static volatile bool answered = false;  // a valid frame of either protocol arrived

enum BmsProto { PROTO_PROBE_JBD,
                PROTO_EW };
static BmsProto proto = PROTO_PROBE_JBD;
static NimBLERemoteCharacteristic *ew_write = nullptr;

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

/* ---------- ECO-WORTHY / BWOB protocol ----------
   Frame: AA <cmd> <len> <payload len bytes> <2 bytes checksum>
   The commands are fixed byte sequences, so no checksum has to be calculated. */
static const uint8_t EW_INIT[5] = { 0xAA, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t EW_CMD20[5] = { 0xAA, 0x20, 0x00, 0x20, 0x00 };
static const uint8_t EW_CMD21[5] = { 0xAA, 0x21, 0x00, 0x21, 0x00 };
static const uint8_t EW_CMD22[5] = { 0xAA, 0x22, 0x00, 0x22, 0x00 };

static uint8_t ew_buf[128];
static size_t ew_len = 0;

static uint32_t le16(const uint8_t *p) {
  return p[0] | (p[1] << 8);
}
static uint32_t le32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void ew_decode(uint8_t cmd, const uint8_t *p, size_t n) {
  USBSerial.printf("BMS EW frame cmd 0x%02X, %u bytes payload\n", cmd, (unsigned)n);
  if (cmd == 0x21 && n >= 18) {
    xSemaphoreTake(bms_mtx, portMAX_DELAY);
    bms.volt = le16(p) / 1000.0f;
    bms.soc = p[8];
    bms.remain_ah = le32(p + 10) / 1000.0f;
    bms.nominal_ah = le32(p + 14) / 1000.0f;
    bms.valid = true;
    bms.updated = millis();
    xSemaphoreGive(bms_mtx);
    answered = true;
  } else if (cmd == 0x22 && n >= 17) {
    float cells[8];
    for (int i = 0; i < 8; i++) cells[i] = ((p[1 + i * 2] << 8) | p[2 + i * 2]) / 1000.0f;  // big endian
    float mn = cells[0], mx = cells[0];
    for (int i = 1; i < 8; i++) {
      if (cells[i] < mn) mn = cells[i];
      if (cells[i] > mx) mx = cells[i];
    }
    if (mn < 2.5f || mx > 3.8f || mx - mn > 0.2f) return;  // implausible: ignore this frame
    xSemaphoreTake(bms_mtx, portMAX_DELAY);
    for (int i = 0; i < 8; i++) bms.cell[i] = cells[i];
    bms.ncell = 8;
    bms.cells_valid = true;
    xSemaphoreGive(bms_mtx);
    answered = true;
  }
}

static void ew_feed(const uint8_t *d, size_t n) {
  if (ew_len + n > sizeof(ew_buf)) ew_len = 0;
  memcpy(ew_buf + ew_len, d, n);
  ew_len += n;

  size_t pos = 0;
  while (ew_len - pos >= 3) {
    if (ew_buf[pos] != 0xAA) {  // resynchronise
      pos++;
      continue;
    }
    size_t frame_len = 3 + ew_buf[pos + 2] + 2;
    if (ew_len - pos < frame_len) break;  // wait for the rest
    ew_decode(ew_buf[pos + 1], ew_buf + pos + 3, frame_len - 5);
    pos += frame_len;
  }
  if (pos) {
    memmove(ew_buf, ew_buf + pos, ew_len - pos);
    ew_len -= pos;
  }
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
  answered = true;
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
    bms.has_fets = true;
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
  if (write_chr_idx >= write_chr_count) return;
  NimBLERemoteCharacteristic *chr = write_chr[write_chr_idx];
  uint16_t chk = (uint16_t)(0x10000 - cmd);  // sum over cmd and length (0)
  uint8_t req[7] = { 0xDD, 0xA5, cmd, 0x00, (uint8_t)(chk >> 8), (uint8_t)chk, 0x77 };
  chr->writeValue(req, sizeof(req), !chr->canWriteNoResponse());
}

/* ---------- notifications (NimBLE host task) ---------- */
static void notify_cb(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool is_notify) {
  char hex[100];
  hex_line(data, len, hex, sizeof(hex));
  USBSerial.printf("BMS notify %s (%u bytes): %s\n", chr->getUUID().toString().c_str(), (unsigned)len, hex);

  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  strlcpy(bms.last_frame, hex, sizeof(bms.last_frame));
  xSemaphoreGive(bms_mtx);

  /* each parser checks its own start byte, so only real frames are decoded */
  if (proto == PROTO_EW) ew_feed(data, len);
  else jbd_feed(data, len);
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
  write_chr_count = write_chr_idx = 0;
  answered = false;
  ew_write = nullptr;
  ew_len = 0;
  proto = PROTO_PROBE_JBD;

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
      if ((chr->canWrite() || chr->canWriteNoResponse()) && write_chr_count < MAX_WRITE_CHRS) {
        /* the documented JBD characteristic goes first, the rest are tried in turn */
        if (chr->getUUID() == NimBLEUUID((uint16_t)0xFF02) && write_chr_count) {
          write_chr[write_chr_count++] = write_chr[0];
          write_chr[0] = chr;
        } else {
          write_chr[write_chr_count++] = chr;
        }
      }
    }
  }
  /* ECO-WORTHY / BWOB: service 0x0001 with write 0x0002 and notify 0x0003 */
  NimBLERemoteService *ew = client->getService(NimBLEUUID((uint16_t)0x0001));
  if (ew) ew_write = ew->getCharacteristic(NimBLEUUID((uint16_t)0x0002));
  if (ew_write) {
    proto = PROTO_EW;
    USBSerial.println("BMS: ECO-WORTHY service found (0x0001/0x0002)");
  } else {
    USBSerial.printf("BMS: %d writable characteristics to try (JBD)\n", write_chr_count);
  }

  xSemaphoreTake(bms_mtx, portMAX_DELAY);
  bms.connected = true;
  strlcpy(bms.services, services, sizeof(bms.services));
  xSemaphoreGive(bms_mtx);

  if (ew_write || write_chr_count) set_status("Connected to %s - asking for data...", cfg.name);
  else set_status("Connected to %s, but it accepts no requests - see Serial log", cfg.name);
  return true;
}

static void bms_task(void *arg) {
  uint32_t next_try = 0, next_poll = 0;
  int tries = 0;
  bool had_target = false, reported = false;
  for (;;) {
    if (!feat_bms) {  // switched off in Settings
      if (client && client->isConnected()) {
        client->disconnect();
        set_status("Battery reading is switched off in Settings");
      }
      had_target = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

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
      tries = 0;
      reported = false;
    }

    if (connected && (ew_write || write_chr_count) && (int32_t)(millis() - next_poll) >= 0) {
      if (proto == PROTO_EW) {
        bool resp = !ew_write->canWriteNoResponse();
        if (!answered) {  // wake-up sequence, as the phone app sends it
          ew_write->writeValue(EW_INIT, sizeof(EW_INIT), resp);
          vTaskDelay(pdMS_TO_TICKS(300));
          ew_write->writeValue(EW_CMD20, sizeof(EW_CMD20), resp);
          vTaskDelay(pdMS_TO_TICKS(300));
        }
        ew_write->writeValue(EW_CMD21, sizeof(EW_CMD21), resp);  // voltage, SOC, capacity
        vTaskDelay(pdMS_TO_TICKS(500));
        ew_write->writeValue(EW_CMD22, sizeof(EW_CMD22), resp);  // cell voltages
      } else {
        jbd_request(0x03);  // basic info
        vTaskDelay(pdMS_TO_TICKS(400));
        jbd_request(0x04);  // cell voltages
      }
      next_poll = millis() + (answered ? BMS_POLL_MS : 1500);

      if (answered && !reported) {  // first valid answer: now we know the protocol
        reported = true;
        xSemaphoreTake(bms_mtx, portMAX_DELAY);
        bms.is_jbd = true;
        xSemaphoreGive(bms_mtx);
        BmsCfg c;
        LOCK();
        c = bms_cfg;
        UNLOCK();
        if (proto == PROTO_EW) set_status("Connected to %s (ECO-WORTHY protocol)", c.name);
        else set_status("Connected to %s (JBD protocol, %s)", c.name,
                        write_chr[write_chr_idx]->getUUID().toString().c_str());
      } else if (!answered && proto == PROTO_PROBE_JBD && ++tries >= 3) {  // try the next characteristic
        tries = 0;
        write_chr_idx++;
        if (write_chr_idx >= write_chr_count) {
          write_chr_idx = 0;
          BmsCfg c;
          LOCK();
          c = bms_cfg;
          UNLOCK();
          set_status("%s answers nothing we understand - see Serial log", c.name);
        } else {
          USBSerial.printf("BMS: trying characteristic %d of %d\n", write_chr_idx + 1, write_chr_count);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void bms_start() {
  bms_mtx = xSemaphoreCreateMutex();
  memset(&bms, 0, sizeof(bms));
#if ENABLE_BMS
  strlcpy(bms.status, "No battery chosen", sizeof(bms.status));
  xTaskCreatePinnedToCore(bms_task, "bms", 4096, NULL, 1, NULL, 0);
#else
  strlcpy(bms.status, "Battery connection is switched off (ENABLE_BMS 0 in app.h)", sizeof(bms.status));
#endif
}
