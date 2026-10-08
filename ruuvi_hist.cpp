// esp32-S3-ws4-caravan v1.0
/* RuuviTag history for the Ruuvi tab: the lowest and highest temperature of
 * each of the last RUUVI_DAYS days, per added tag. About 100 bytes in all, kept
 * in flash (saved by the network task at most every 30 minutes and when a new
 * day starts), so it survives a restart without an SD card. Days follow the
 * local clock; nothing is recorded until the time is set over NTP.
 * Updated from the UI task only. */
#include "app.h"

RuuviHist ruuvi_hist[MAX_RUUVI];
static uint32_t last_save = 0;
static bool changed = false;

static int32_t local_day() {
  time_t now = time(nullptr);
  if (now < 1700000000) return -1;  // no time yet
  return (int32_t)((now + g.utc_offset) / 86400);
}

/* Moves the days along so that d[RUUVI_DAYS - 1] is today. True if it moved. */
static bool roll(RuuviHist &h, int32_t today) {
  if (h.day == today) return false;
  int32_t gap = today - h.day;
  if (h.day <= 0 || gap < 0 || gap >= RUUVI_DAYS) {
    for (auto &d : h.d) d.lo = d.hi = RUUVI_NONE;
  } else {
    memmove(h.d, h.d + gap, sizeof(RuuviDay) * (RUUVI_DAYS - gap));
    for (int i = RUUVI_DAYS - gap; i < RUUVI_DAYS; i++) h.d[i].lo = h.d[i].hi = RUUVI_NONE;
  }
  h.day = today;
  return true;
}

void ruuvi_hist_note(int slot, const char *mac, float temp) {
  int32_t today = local_day();
  if (today < 0 || slot < 0 || slot >= MAX_RUUVI || isnan(temp)) return;
  RuuviHist &h = ruuvi_hist[slot];
  LOCK();
  if (strcmp(h.mac, mac)) {  // another tag in this slot: start again
    memset(&h, 0, sizeof(h));
    strlcpy(h.mac, mac, sizeof(h.mac));
    for (auto &d : h.d) d.lo = d.hi = RUUVI_NONE;
    h.day = today;
    changed = true;
  }
  bool new_day = roll(h, today);
  if (new_day) changed = true;
  int16_t t = (int16_t)lroundf(temp * 10);
  RuuviDay &d = h.d[RUUVI_DAYS - 1];
  if (d.lo == RUUVI_NONE || t < d.lo) { d.lo = t; changed = true; }
  if (d.hi == RUUVI_NONE || t > d.hi) { d.hi = t; changed = true; }
  UNLOCK();
  if (changed && (new_day || !last_save || millis() - last_save > 30UL * 60 * 1000)) {
    cmd_save_rhist = true;
    changed = false;
    last_save = millis() | 1;
  }
}

/* A copy with today as the last day, also when the tag has been silent */
bool ruuvi_hist_get(int slot, const char *mac, RuuviHist *out) {
  if (slot < 0 || slot >= MAX_RUUVI) return false;
  LOCK();
  *out = ruuvi_hist[slot];
  UNLOCK();
  if (strcmp(out->mac, mac)) return false;
  int32_t today = local_day();
  if (today >= 0) roll(*out, today);
  return true;
}
