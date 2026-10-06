/* Warnings and alarms.
 * Every check runs once a second and produces at most one message. The most
 * important one is shown as a banner on the Home page and on the web page, and
 * a new alarm wakes the screen saver.
 * Thresholds live in Settings -> Alarms and are saved to flash. */
#include "app.h"
#include <WiFi.h>

AlarmCfg alarm_cfg = { true, 40, 20, 0, 12, true, false };  // temp_low unused; card warning off by default

static Alarm list[MAX_ALARMS];
static int list_n = 0;
static SemaphoreHandle_t alarm_mtx;
static char last_top[48] = "";
static bool new_alarm_flag = false;

static void add_alarm(uint8_t level, const char *fmt, ...) {
  if (list_n >= MAX_ALARMS) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(list[list_n].text, sizeof(list[list_n].text), fmt, ap);
  va_end(ap);
  list[list_n].level = level;
  list_n++;
}

void alarms_begin() {
  alarm_mtx = xSemaphoreCreateMutex();
  if (prefs.getBytesLength("alarm") == sizeof(alarm_cfg)) prefs.getBytes("alarm", &alarm_cfg, sizeof(alarm_cfg));
}

/* Copy of the current list, most important first */
int alarms_get(Alarm *out, int max) {
  xSemaphoreTake(alarm_mtx, portMAX_DELAY);
  int n = 0;
  for (int lvl = ALARM_LEVEL_ALARM; lvl >= ALARM_LEVEL_WARN && n < max; lvl--)
    for (int i = 0; i < list_n && n < max; i++)
      if (list[i].level == lvl) out[n++] = list[i];
  xSemaphoreGive(alarm_mtx);
  return n;
}

bool alarms_top(char *text, size_t len, uint8_t *level) {
  Alarm top[1];
  if (!alarms_get(top, 1)) {
    if (text && len) text[0] = 0;
    if (level) *level = ALARM_LEVEL_NONE;
    return false;
  }
  if (text) strlcpy(text, top[0].text, len);
  if (level) *level = top[0].level;
  return true;
}

/* True once after a new alarm appears, so the screen saver can wake */
bool alarms_take_new() {
  bool f = new_alarm_flag;
  new_alarm_flag = false;
  return f;
}

/* Called once a second from the UI */
void alarms_check() {
  if (!alarm_mtx) return;
  uint32_t now = millis();

  xSemaphoreTake(alarm_mtx, portMAX_DELAY);
  list_n = 0;
  if (!alarm_cfg.enabled) {
    xSemaphoreGive(alarm_mtx);
    return;
  }

  /* ---- battery: Victron monitor first, otherwise the BMS ---- */
  static VicCfg cfg[MAX_VIC];
  static VicData dat[MAX_VIC];
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  memcpy(cfg, vic_cfg, sizeof(cfg));
  memcpy(dat, vic_data, sizeof(dat));
  xSemaphoreGive(vic_mtx);

  float soc = NAN, volt = NAN, curr = NAN;
  int ttg = -1;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used || cfg[i].type != VIC_BATTMON || !vic_fresh(dat[i], now) || !dat[i].key_ok) continue;
    soc = dat[i].soc;
    volt = dat[i].batt_v;
    curr = dat[i].batt_i;
    ttg = dat[i].remaining_min;
    break;
  }
  if (isnan(soc)) {
    BmsData b;
    bms_get(&b);
    if (feat_bms && b.valid && now - b.updated < 30000) {
      soc = b.soc;
      volt = b.volt;
      curr = b.curr;
    }
  }

  if (!isnan(soc)) {
    if (soc <= alarm_cfg.soc_alarm) add_alarm(ALARM_LEVEL_ALARM, "Battery %.0f%% - charge now", soc);
    else if (soc <= alarm_cfg.soc_warn) add_alarm(ALARM_LEVEL_WARN, "Battery low: %.0f%%", soc);
  }
  if (!isnan(volt)) {
    if (volt < 11.8f) add_alarm(ALARM_LEVEL_ALARM, "Battery voltage low: %.2f V", volt);
    else if (volt > 15.0f) add_alarm(ALARM_LEVEL_ALARM, "Battery voltage high: %.2f V", volt);
  }
  if (ttg >= 0 && ttg < 120 && !isnan(curr) && curr < 0)
    add_alarm(ALARM_LEVEL_WARN, "Battery empty in %dh %02dm", ttg / 60, ttg % 60);

  /* ---- Victron devices ---- */
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used) continue;
    if (dat[i].key_bad) {
      add_alarm(ALARM_LEVEL_WARN, "%s: wrong key", cfg[i].name);
    } else if (dat[i].last_seen && now - dat[i].last_seen > 600000UL) {
      add_alarm(ALARM_LEVEL_WARN, "%s: no signal", cfg[i].name);
    }
    if (cfg[i].type == VIC_INVERTER && vic_fresh(dat[i], now) && dat[i].key_ok) {
      const char *a = vic_alarm_text(dat[i].alarm);
      if (a) add_alarm(ALARM_LEVEL_ALARM, "Inverter: %s", a);
    }
  }

  /* No temperature or tag alarms: a caravan may well be left unheated, and a tag
     can sit in a fridge or outside, so a low reading is normal. The Temp tab
     shows how long ago each tag was heard. */

  /* ---- weather: wind, for the awning ---- */
  Weather w;
  LOCK();
  w = g.weather;
  bool have_ssid = g.ssid[0] != 0;
  UNLOCK();
  if (w.valid && alarm_cfg.wind_warn && w.wind >= alarm_cfg.wind_warn)
    add_alarm(ALARM_LEVEL_WARN, "Wind %.0f m/s - take the awning in", w.wind);

  /* ---- WiFi (not while we have switched it off ourselves) ---- */
  if (power_on_battery()) have_ssid = false;

  static uint32_t wifi_lost_since = 0;
  if (have_ssid && WiFi.status() != WL_CONNECTED) {
    if (!wifi_lost_since) wifi_lost_since = now;
    if (now - wifi_lost_since > 120000UL) add_alarm(ALARM_LEVEL_WARN, "WiFi lost");
  } else {
    wifi_lost_since = 0;
  }

  /* ---- the board's own LiPo, if one is fitted ---- */
  static uint32_t lipo_next = 0;  // once a minute is plenty, and it shares I2C with touch
  static bool lipo_low = false, lipo_warn = false;
  static float lipo_v = 0;
  if ((int32_t)(now - lipo_next) >= 0) {
    lipo_next = now + 60000;
    int pct;
    bool chg;
    float v;
    lipo_low = lipo_warn = false;
    if (board_battery(&v, &pct, &chg) && !chg) {
      lipo_v = v;
      lipo_low = v < 3.45f;
      lipo_warn = pct <= 20;
    }
  }
  if (lipo_low || lipo_warn) {
    if (lipo_low) add_alarm(ALARM_LEVEL_ALARM, "Board battery %.2f V - shutting down soon", lipo_v);
    else add_alarm(ALARM_LEVEL_WARN, "Board battery low (%.2f V)", lipo_v);
  }

  /* ---- relays and card ---- */
  if (feat_relays && relay_cfg.addr && !pcf_ok) add_alarm(ALARM_LEVEL_WARN, "Relay board not answering");
  if (alarm_cfg.warn_sd && feat_sdlog && !sd_log_ok()) add_alarm(ALARM_LEVEL_WARN, "SD card not available");

  /* a new top alarm wakes the screen saver */
  char top[48] = "";
  for (int i = 0; i < list_n; i++)
    if (list[i].level == ALARM_LEVEL_ALARM) {
      strlcpy(top, list[i].text, sizeof(top));
      break;
    }
  if (top[0] && strcmp(top, last_top)) {
    new_alarm_flag = true;
    logf("ALARM: %s", top);
  }
  strlcpy(last_top, top, sizeof(last_top));
  xSemaphoreGive(alarm_mtx);
}
