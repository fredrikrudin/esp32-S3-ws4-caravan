/* RuuviTag BLE advertisement decoding (data formats 5 and 3) */
#include "app.h"

static void ruuvi_store(const RuuviTag &r) {
  xSemaphoreTake(ruuvi_mtx, portMAX_DELAY);
  int slot = -1, oldest = 0;
  for (int i = 0; i < MAX_TAGS; i++) {
    if (tags[i].used && !strcmp(tags[i].addr, r.addr)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_TAGS; i++) {
      if (!tags[i].used) {
        slot = i;
        break;
      }
      if (tags[i].last_seen < tags[oldest].last_seen) oldest = i;
    }
  }
  if (slot < 0) slot = oldest;  // table full: replace the one not seen for longest
  tags[slot] = r;
  xSemaphoreGive(ruuvi_mtx);
}

void ruuvi_parse(const std::string &md, const char *addr, int rssi) {
  if (!feat_ruuvi) return;  // switched off in Settings
  const uint8_t *d = (const uint8_t *)md.data();
  size_t n = md.size();
  if (n < 3 || d[0] != 0x99 || d[1] != 0x04) return;  // Ruuvi company ID 0x0499
  const uint8_t *p = d + 2;
  size_t len = n - 2;

  RuuviTag r;
  if (p[0] == 5 && len >= 24) {  // data format 5 (RAWv2)
    int16_t t = (int16_t)((p[1] << 8) | p[2]);
    uint16_t h = (p[3] << 8) | p[4];
    uint16_t pr = (p[5] << 8) | p[6];
    uint16_t pw = (p[13] << 8) | p[14];
    r.has_temp = (t != (int16_t)0x8000);
    r.temp = t * 0.005f;
    r.has_hum = (h != 0xFFFF);
    r.hum = h * 0.0025f;
    r.has_pres = (pr != 0xFFFF);
    r.pres = (pr + 50000) / 100.0f;  // hPa
    uint16_t mv = pw >> 5;
    r.batt_mv = (mv != 2047) ? mv + 1600 : 0;
  } else if (p[0] == 3 && len >= 14) {  // data format 3 (RAWv1, older firmware)
    r.has_hum = true;
    r.hum = p[1] * 0.5f;
    r.has_temp = true;
    r.temp = (p[2] & 0x7F) + p[3] / 100.0f;
    if (p[2] & 0x80) r.temp = -r.temp;
    r.has_pres = true;
    r.pres = (((p[4] << 8) | p[5]) + 50000) / 100.0f;
    r.batt_mv = (p[12] << 8) | p[13];
  } else {
    return;  // other formats not supported
  }

  r.used = true;
  strlcpy(r.addr, addr, sizeof(r.addr));
  for (char *c = r.addr; *c; c++) *c = toupper(*c);
  r.rssi = rssi;
  r.last_seen = millis();
  ruuvi_store(r);
}
