/* Energy history for the Power page.
 * Samples solar power and estimated consumption every 10 s, adds them up into
 * watt-hours, and keeps 24 hourly buckets and 30 daily ones - like the bar
 * charts in Victron VRM. Buckets are kept in PSRAM and, if a card is mounted,
 * written to /history.bin so they survive a restart.
 * Nothing is recorded until the clock is set over NTP. */
#include "app.h"
#include <SD_MMC.h>

#define HIST_FILE "/history.bin"
#define SAMPLE_MS 10000

static HistBucket *hours = nullptr;  // 24 entries, oldest first
static HistBucket *days = nullptr;   // 30 entries, oldest first
static uint32_t cur_hour = 0, cur_day = 0;
static float acc_solar_wh = 0, acc_load_wh = 0, last_soc = NAN;
static bool dirty = false;

static void shift_in(HistBucket *arr, int n, const HistBucket &b) {
  memmove(arr, arr + 1, sizeof(HistBucket) * (n - 1));
  arr[n - 1] = b;
}

void history_get(HistBucket **hour_arr, HistBucket **day_arr) {
  *hour_arr = hours;
  *day_arr = days;
}

static void history_save() {
  if (!sd_log_ok() || !hours) return;
  File f = SD_MMC.open(HIST_FILE, FILE_WRITE);
  if (!f) return;
  f.write((uint8_t *)&cur_hour, sizeof(cur_hour));
  f.write((uint8_t *)&cur_day, sizeof(cur_day));
  f.write((uint8_t *)hours, sizeof(HistBucket) * HIST_HOURS);
  f.write((uint8_t *)days, sizeof(HistBucket) * HIST_DAYS);
  f.close();
}

static void history_load() {
  if (!sd_log_ok() || !hours) return;
  File f = SD_MMC.open(HIST_FILE);
  if (!f) return;
  if (f.size() == sizeof(cur_hour) + sizeof(cur_day) + sizeof(HistBucket) * (HIST_HOURS + HIST_DAYS)) {
    f.read((uint8_t *)&cur_hour, sizeof(cur_hour));
    f.read((uint8_t *)&cur_day, sizeof(cur_day));
    f.read((uint8_t *)hours, sizeof(HistBucket) * HIST_HOURS);
    f.read((uint8_t *)days, sizeof(HistBucket) * HIST_DAYS);
    logf("History loaded from the card");
  }
  f.close();
}

void history_begin() {
  hours = (HistBucket *)heap_caps_calloc(HIST_HOURS, sizeof(HistBucket), MALLOC_CAP_SPIRAM);
  days = (HistBucket *)heap_caps_calloc(HIST_DAYS, sizeof(HistBucket), MALLOC_CAP_SPIRAM);
  history_load();
}

/* Current solar power and estimated consumption, as the Power tab computes them */
static void read_power(float *solar_w, float *load_w, float *soc) {
  static VicCfg cfg[MAX_VIC];
  static VicData dat[MAX_VIC];
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  memcpy(cfg, vic_cfg, sizeof(cfg));
  memcpy(dat, vic_data, sizeof(dat));
  xSemaphoreGive(vic_mtx);

  uint32_t now = millis();
  float pv = 0, charger_out = 0, batt_w = NAN;
  *soc = NAN;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used || !vic_fresh(dat[i], now) || !dat[i].key_ok) continue;
    const VicData &d = dat[i];
    if (cfg[i].type == VIC_SOLAR) {
      if (!isnan(d.pv_w)) pv += d.pv_w;
      if (!isnan(d.batt_v) && !isnan(d.batt_i)) charger_out += d.batt_v * d.batt_i;
    } else if (cfg[i].type == VIC_ACCHG) {
      if (!isnan(d.batt_v) && !isnan(d.batt_i)) charger_out += d.batt_v * d.batt_i;
    } else if (cfg[i].type == VIC_BATTMON && isnan(*soc)) {
      *soc = d.soc;
      if (!isnan(d.batt_v) && !isnan(d.batt_i)) batt_w = d.batt_v * d.batt_i;
    }
  }
  if (isnan(*soc)) {  // battery's own BMS as a fallback
    BmsData b;
    bms_get(&b);
    if (feat_bms && b.valid && now - b.updated < 30000) {
      *soc = b.soc;
      if (!isnan(b.curr)) batt_w = b.volt * b.curr;
    }
  }
  *solar_w = pv;
  *load_w = isnan(batt_w) ? NAN : charger_out - batt_w;  // what the loads take
  if (!isnan(*load_w) && *load_w < 0) *load_w = 0;
}

/* Called from loop() */
void history_service() {
  static uint32_t next = 0;
  if (!hours) return;
  if (next && (int32_t)(millis() - next) < 0) return;
  next = millis() + SAMPLE_MS;

  time_t t = time(nullptr);
  if (t < 1700000000) return;  // no clock yet
  t += g.utc_offset;
  uint32_t hour_id = t / 3600, day_id = t / 86400;

  if (!cur_hour) {  // first sample after a start
    cur_hour = hour_id;
    cur_day = day_id;
  }

  float solar_w, load_w, soc;
  read_power(&solar_w, &load_w, &soc);
  float hours_elapsed = SAMPLE_MS / 3600000.0f;
  acc_solar_wh += solar_w * hours_elapsed;
  if (!isnan(load_w)) acc_load_wh += load_w * hours_elapsed;
  if (!isnan(soc)) last_soc = soc;

  while (hour_id > cur_hour) {  // an hour finished (or several, if we were off)
    HistBucket b = { acc_solar_wh, acc_load_wh, last_soc };
    shift_in(hours, HIST_HOURS, b);
    acc_solar_wh = acc_load_wh = 0;
    cur_hour++;
    dirty = true;

    if (day_id > cur_day) {
      HistBucket d = { 0, 0, last_soc };
      for (int i = HIST_HOURS - 24; i < HIST_HOURS; i++) {  // the day just gone
        d.solar_wh += hours[i].solar_wh;
        d.load_wh += hours[i].load_wh;
      }
      shift_in(days, HIST_DAYS, d);
      cur_day++;
    }
  }

  if (dirty) {
    history_save();
    dirty = false;
  }
}

/* Totals for the summary boxes */
void history_totals(bool daily, float *solar_kwh, float *load_kwh) {
  const HistBucket *a = daily ? days : hours;
  int n = daily ? HIST_DAYS : HIST_HOURS;
  *solar_kwh = *load_kwh = 0;
  for (int i = 0; i < n; i++) {
    *solar_kwh += a[i].solar_wh;
    *load_kwh += a[i].load_wh;
  }
  *solar_kwh /= 1000;
  *load_kwh /= 1000;
}
