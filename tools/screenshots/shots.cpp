// esp32-S3-ws4-caravan v1.0
/* Screenshot harness: the real UI code with example data, rendered by LVGL on a PC */
#include "app.h"
#include <sys/time.h>
#include <WiFi.h>
#include "mbedtls/aes.h"
/* ---------------- platform stubs ---------------- */
EspClass ESP;
void EspClass::restart() {}
WiFiClass WiFi;
wl_status_t WiFiClass::status() { return WL_CONNECTED; }
static uint32_t ms = 200000;
unsigned long millis() { return ms; }
void delay(unsigned long d) { ms += d; }
uint32_t esp_random() { return 4; }
void esp_deep_sleep_start() {}
void *heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
void *heap_caps_calloc(size_t n, size_t s, uint32_t) { return calloc(n, s); }
SemaphoreHandle_t xSemaphoreCreateMutex() { static int x; return &x; }
int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return 1; }
int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
size_t strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
size_t strlcat(char *d, const char *s, size_t n) { size_t l = strlen(d); return l + strlcpy(d + l, s, n > l ? n - l : 0); }
bool Preferences::begin(const char *, bool) { return true; }
bool Preferences::clear() { return true; }
bool Preferences::getBool(const char *, bool d) { return d; }
size_t Preferences::getBytes(const char *, void *, size_t) { return 0; }
size_t Preferences::getBytesLength(const char *) { return 0; }
float Preferences::getFloat(const char *, float d) { return d; }
int32_t Preferences::getInt(const char *, int32_t d) { return d; }
size_t Preferences::getString(const char *, char *b, size_t n) { if (n) b[0] = 0; return 0; }
uint8_t Preferences::getUChar(const char *, uint8_t d) { return d; }
bool Preferences::isKey(const char *) { return false; }
size_t Print::write(const uint8_t *, size_t n) { return n; }
String::String(const char *) {}
String &String::operator+=(char) { return *this; }
fs::File fs::FS::open(const char *, const char *, bool) { return fs::File(); }
void fs::File::close() {}
fs::File::operator bool() const { return false; }
size_t fs::File::read(uint8_t *, size_t) { return 0; }
size_t fs::File::size() { return 0; }
int mbedtls_aes_setkey_enc(mbedtls_aes_context *, const unsigned char *, unsigned) { return 0; }
int mbedtls_aes_crypt_ecb(mbedtls_aes_context *, int, const unsigned char[16], unsigned char[16]) { return 0; }
void mbedtls_aes_init(mbedtls_aes_context *) {}
void mbedtls_aes_free(mbedtls_aes_context *) {}
/* ---------------- firmware stubs ---------------- */
void logf(const char *, ...) {}
void log_fault(const char *, ...) {}
void serf(const char *, ...) {}
void serln(const char *) {}
void serial_apply() {}
void bms_connect_to(const char *, uint8_t, const char *) {}
void bms_get(BmsData *d) { memset(d, 0, sizeof(*d)); strcpy(d->status, "No battery chosen"); }
int bms_get_seen(BmsSeen *, int) { return 0; }
const char *bms_protection_text(uint16_t) { return NULL; }
bool board_battery(float *v, int *p, bool *c) { *v = 4.12f; *p = 92; *c = false; return true; }
const char *csv_status() { return "Not started"; }
int i2c_scan(char *out, size_t n) { strlcpy(out, "0x20  PCF8574\n0x24  CH32 IO chip (on board)\n0x51  RTC PCF85063 (on board)\n0x5D  Touch GT911 (on board)", n); return 4; }
bool power_on_battery() { return false; }
void power_set_saving(bool) {}
bool relay_apply() { return true; }
uint8_t relay_mask() { return 0xFF; }
void relay_read_back() {}
bool relays_present() { return true; }
void sd_card_info(char *o, size_t n) { strlcpy(o, "SDHC card, 212 MB used of 3781 MB", n); }
static fs::FS fake_fs;
fs::FS &sd_fs() { return fake_fs; }
int sd_log_delete_old() { return 0; }
bool sd_log_mount() { return true; }
const char *sd_log_name() { return "/caravan.log"; }
bool sd_log_new_file() { return true; }
bool sd_log_ok() { return true; }
bool sd_log_probe() { return true; }
size_t sd_log_size() { return 182 * 1024; }
const char *sd_log_status() { return "Logging to /caravan.log (card 3781 MB, SPI)"; }
void sd_log_unmount() {}
void set_backlight(uint8_t) {}
bool settings_backup() { return true; }
bool settings_restore() { return true; }
static ShellyData sh[MAX_SHELLY];
void shelly_add(const char *, uint8_t, const char *) {}
void shelly_add_wifi(const char *, const char *) {}
void shelly_get(int i, ShellyData *o) { *o = sh[i]; }
bool shelly_present() { return true; }
void shelly_remove(int) {}
void shelly_retry(int) {}
void shelly_set(int, bool) {}
bool sw6106_present() { return false; }
void web_password_changed() {}
/* ---------------- display ---------------- */
static lv_color_t fb[480 * 480];
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c) {
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) fb[y * 480 + x] = *c++;
  lv_disp_flush_ready(d);
}
static void save(const char *path) {
  lv_refr_now(NULL);
  FILE *f = fopen(path, "wb");
  fwrite(fb, sizeof(fb), 1, f);
  fclose(f);
}
static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
    lv_obj_t *c = lv_obj_get_child(o, i);
    if (lv_obj_check_type(c, cls)) return c;
    if (lv_obj_t *r = find_class(c, cls)) return r;
  }
  return NULL;
}
static void run_timers() {
  for (int i = 0; i < 4; i++) {
    ms += 1000;
    lv_tick_inc(1000);
    lv_disp_trig_activity(NULL);  // no screen saver
    lv_timer_handler();
  }
}
/* ---------------- example data ---------------- */
static void example_data(bool alarm) {
  time_t now = time(nullptr);
  /* WiFi, place, weather */
  strcpy(g.ssid, "Caravan");
  strcpy(g.wifi_status, "Connected to Caravan\nIP 192.168.4.37");
  g.wifi_up = true; g.wifi_rssi = -58;
  strcpy(g.city, "Stockholm, Sweden"); strcpy(g.place, "Stockholm, Sweden");
  g.has_loc = true; g.utc_offset = 7200; g.offset_valid = true;
  strcpy(g.loc_status, "Location: Stockholm, Sweden");
  Weather &w = g.weather;
  w.valid = true; w.temp = 12.6f; w.feels = 10.9f; w.wind = 4.2f; w.humidity = 71; w.code = 2;
  w.today_max = 15; w.today_min = 7; w.fetched = now - 600;
  const char *days[] = { "Fri", "Sat", "Sun" };
  int codes[] = { 61, 3, 0 };
  for (int i = 0; i < 3; i++) { strcpy(w.fc_day[i], days[i]); w.fc_code[i] = codes[i]; w.fc_max[i] = 14 - i; w.fc_min[i] = 6 - i; }
  g.weather_changed = true;
  /* Victron */
  struct { uint8_t type; const char *name; } dev[] = { { VIC_SOLAR, "SmartSolar 100/30" }, { VIC_BATTMON, "SmartShunt" },
                                                         { VIC_DCDC, "Orion XS" }, { VIC_ACCHG, "Blue Smart IP22" } };
  for (int i = 0; i < 4; i++) {
    vic_cfg[i].used = true; vic_cfg[i].type = dev[i].type; strcpy(vic_cfg[i].name, dev[i].name);
    snprintf(vic_cfg[i].mac, 18, "C0:11:22:33:44:%02X", i);
    VicData &d = vic_data[i];
    memset(&d, 0, sizeof(d));
    d.type = dev[i].type; d.last_seen = ms - 3000; d.key_ok = true; d.rssi = -60 - i * 4; d.remaining_min = -1;
    d.temp_c = NAN; d.aux_v = NAN; d.consumed_ah = NAN;
    d.temp_c = NAN; d.aux_v = NAN; d.consumed_ah = NAN;
    d.batt_v = 13.32f; d.pv_w = NAN; d.yield_kwh = NAN; d.soc = NAN; d.in_v = NAN; d.out_v = NAN; d.ac_va = NAN;
  }
  vic_data[0].state = 3; vic_data[0].pv_w = 245; vic_data[0].yield_kwh = 1.23f; vic_data[0].batt_i = 17.6f;
  vic_data[1].soc = alarm ? 18 : 87; vic_data[1].batt_i = alarm ? -6.2f : 12.4f; vic_data[1].remaining_min = alarm ? 214 : 1120;
  vic_data[1].batt_v = alarm ? 12.41f : 13.32f;
  vic_data[2].state = 3; vic_data[2].in_v = 14.1f; vic_data[2].out_v = 13.5f; vic_data[2].batt_i = 4.1f;
  vic_data[3].state = 0; vic_data[3].batt_i = 0;
  /* RuuviTags */
  const char *names[] = { "Inside", "Outside", "Fridge" };
  float now_t[] = { 21.4f, 6.3f, 4.1f };
  int lo[3][7] = { { 182, 179, 185, 190, 176, 181, 188 }, { -32, -18, 4, 12, -6, -41, 21 }, { 31, 29, 33, 30, 35, 32, 34 } };
  int hi[3][7] = { { 228, 231, 219, 224, 236, 229, 214 }, { 61, 85, 92, 110, 74, 38, 63 }, { 58, 61, 55, 64, 59, 66, 41 } };
  for (int i = 0; i < 3; i++) {
    RuuviCfg &c = ruuvi_cfg[i];
    c.used = true; snprintf(c.mac, 18, "E1:22:33:44:55:%02X", i); strcpy(c.name, names[i]);
    RuuviTag &t = tags[i];
    t.used = true; strcpy(t.addr, c.mac); t.temp = now_t[i]; t.has_temp = t.has_hum = t.has_pres = true;
    t.hum = 48 + i * 10; t.pres = 1012; t.batt_mv = 2950; t.rssi = -68 - i * 5; t.last_seen = ms - 4000;
    RuuviHist &h = ruuvi_hist[i];
    strcpy(h.mac, c.mac); h.day = (int32_t)((now + g.utc_offset) / 86400);
    for (int d = 0; d < 7; d++) { h.d[d].lo = lo[i][d]; h.d[d].hi = hi[i][d]; }
  }
  /* relays */
  relay_cfg.addr = 0x20; relay_cfg.active_low = true; relay_cfg.count = 6;
  const char *rn[] = { "Water pump", "Fridge fan", "Outside light", "Floor heating", "USB sockets", "Awning LED" };
  for (int i = 0; i < 6; i++) strcpy(relay_cfg.names[i], rn[i]);
  relay_state = 0x01 | 0x04 | 0x10;
  pcf_ok = true;
  /* Shelly */
  shelly_cfg[0] = {}; shelly_cfg[0].used = true; shelly_cfg[0].transport = SHELLY_WIFI; strcpy(shelly_cfg[0].name, "Coffee machine"); strcpy(shelly_cfg[0].host, "192.168.4.52");
  shelly_cfg[1] = {}; shelly_cfg[1].used = true; shelly_cfg[1].transport = SHELLY_BLE; strcpy(shelly_cfg[1].name, "Heater"); strcpy(shelly_cfg[1].mac, "A4:CF:12:34:56:78");
  sh[0] = { true, true, true, 1245, 229.8f, 5.42f, ms, 0, "OK over WiFi" };
  sh[1] = { true, true, false, 0, 230.4f, 0, ms, 0, "OK over Bluetooth" };
  /* history */
  HistBucket *hours, *daysb;
  history_get(&hours, &daysb);
  for (int i = 0; i < HIST_HOURS; i++) {
    int hr = (i + 20) % 24;  // ends at the current hour, roughly
    float sun = hr >= 6 && hr <= 20 ? 380 * sinf((hr - 6) / 14.0f * 3.14159f) : 0;
    hours[i] = { sun, 55 + 30 * (hr >= 7 && hr <= 22) + 40 * (hr == 8 || hr == 19), 70 + i * 0.7f };
  }
  float dsol[7] = { 1.9f, 2.4f, 0.8f, 1.2f, 2.8f, 2.1f, 1.23f }, dload[7] = { 1.4f, 1.6f, 1.3f, 1.5f, 1.7f, 1.2f, 0.9f };
  for (int i = 0; i < HIST_DAYS; i++) daysb[i] = { dsol[i] * 1000, dload[i] * 1000, 80 };
  HistSample *rec = (HistSample *)history_recent();
  for (int i = 0; i < HIST_RECENT; i++) rec[i] = { 200 + 45 * sinf(i / 6.0f) + (i % 7) * 4, 60 + (i % 9) * 3, 86 + i / 60.0f, 140 + 20 * sinf(i / 5.0f) };
  feat_ruuvi = feat_relays = feat_shelly = true;
  feat_web = true;
  feat_sdlog = true;
  feat_csv = true;
  feat_battmode = true;
}
/* ---------------- main ---------------- */
int main(int argc, char **argv) {
  const char *lang = argc > 1 ? argv[1] : "en";
  const char *outdir = argc > 2 ? argv[2] : ".";
  strlcpy(ui_lang, lang, sizeof(ui_lang));
  lv_init();
  static lv_disp_draw_buf_t db;
  static lv_color_t buf[480 * 480];
  lv_disp_draw_buf_init(&db, buf, NULL, 480 * 480);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = 480; drv.ver_res = 480; drv.flush_cb = flush; drv.draw_buf = &db; drv.full_refresh = 1;
  lv_disp_drv_register(&drv);

  state_init();
  lang_begin();
  history_begin();
  alarms_begin();
  schedule_begin();
  bool alarm = argc > 3 && !strcmp(argv[3], "alarm");
  example_data(alarm);
  build_ui();
  lv_obj_t *tv = lv_obj_get_parent(lv_obj_get_parent(tab_home));
  char p[256];
  auto shot = [&](lv_obj_t *tab, const char *name) {
    lv_tabview_set_act(tv, lv_obj_get_index(tab), LV_ANIM_OFF);
    run_timers();
    snprintf(p, sizeof(p), "%s/%s.raw", outdir, name);
    save(p);
  };
  if (alarm) {
    alarms_check();
    shot(tab_home, "alarm");
    return 0;
  }
  shot(tab_home, "home");
  shot(tab_power, "power");
  shot(tab_temp, "ruuvi");
  shot(tab_weather, "weather");
  shot(tab_relays, "relays");
  shot(tab_shelly, "shelly");
  /* Settings pages: the inner tabview */
  lv_tabview_set_act(tv, lv_obj_get_index(tab_settings), LV_ANIM_OFF);
  lv_obj_t *inner = find_class(tab_settings, &lv_tabview_class);
  const char *pages[] = { "settings", "settings-sensors", "settings-control", "settings-device" };
  for (int i = 0; i < 4 && inner; i++) {
    lv_tabview_set_act(inner, i, LV_ANIM_OFF);
    run_timers();
    snprintf(p, sizeof(p), "%s/%s.raw", outdir, pages[i]);
    save(p);
  }
  /* Device page scrolled to the end: Debug, About, System */
  if (inner) {
    lv_tabview_set_act(inner, 3, LV_ANIM_OFF);
    lv_obj_t *page = lv_tabview_get_content(inner);
    page = lv_obj_get_child(page, 3);
    lv_obj_update_layout(page);
    lv_obj_scroll_to_y(page, LV_COORD_MAX, LV_ANIM_OFF);
    lv_obj_scroll_by(page, 0, 215, LV_ANIM_OFF);  // back up to the Debug card
    run_timers();
    snprintf(p, sizeof(p), "%s/settings-debug.raw", outdir);
    save(p);
  }
  /* History screen */
  show_history_screen();
  for (int i = 0; i < 3; i++) run_timers();
  snprintf(p, sizeof(p), "%s/history.raw", outdir);
  save(p);
  return 0;
}
