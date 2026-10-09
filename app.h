// esp32-S3-ws4-caravan v1.0
#pragma once
/*
 * esp32-S3-ws4-caravan - shared declarations
 *
 * Every .cpp file includes this header. It holds the configuration,
 * the data types, the state shared between tasks, and the functions
 * each module offers to the others.
 *
 * Copyright (c) 2026 Fredrik Rudin - CC BY-NC 4.0, see LICENSE.md
 * Written with the help of Claude (Anthropic's AI).
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <Preferences.h>
#include <string>
#include <math.h>
#include <time.h>
#include <FS.h>
#include "HWCDC.h"

/* ================================================================== */
/* Configuration                                                      */
/* ================================================================== */

/* ================================================================== */
/* Language (lang.cpp, lang/*.po)                                     */
/* ================================================================== */
/* All on-screen text is written in English in the code. tr() returns the
   translation in the chosen language, or the text unchanged. The LVGL calls
   that put text on screen are wrapped further down, so most strings need no
   TR() of their own; snprintf() formats and button maps do. Text that is
   translated later (tables of names) is marked N_() so tools/i18n.py finds it.
   Translations live in lang/<code>.po (gettext, edit with Poedit) and are
   compiled in: tools/i18n.py turns them into lang_tables.h. */
#define LANG_CODE_LEN 8
extern char ui_lang[LANG_CODE_LEN];  // "en", "sv", ...; changing it restarts
extern bool ui_translated;           // false while the language is English
void lang_begin();                   // after load_cfg(): loads the chosen language
int lang_list(char *codes, int max, char *names, size_t names_len);  // English + built-in languages, names "\n"-separated
const char *tr(const char *en);  // safe from any task
#define TR(s) tr(s)
#define N_(s) s  // marks text for translation without translating it here
/* Most languages need letters (a-ring, umlauts, accents) the built-in Montserrat
   fonts lack: when translated, each built-in font is swapped for one that adds
   the Latin-1 letters (font_latin1.c) and falls back to the built-in one. */
const lv_font_t *ui_font(const lv_font_t *builtin);
#define FONT_UI ui_font(LV_FONT_DEFAULT)
/* Gives a screen or layer the translated font; everything on it inherits it.
   (lv_theme_default_init() keeps the old font when only the font changes.) */
void ui_font_apply(lv_obj_t *scr);
const char *tr_weekday(int wday);  // 0 = Sunday, as in struct tm
const char *tr_month(int mon);     // 0 = January
const char *tr_day_short(int i);   // 0 = Monday, two or three letters

/* Fonts: fall back gracefully if the big fonts are not enabled in lv_conf.h */
#if LV_FONT_MONTSERRAT_48
#define FONT_CLOCK ui_font(&lv_font_montserrat_48)
#elif LV_FONT_MONTSERRAT_40
#define FONT_CLOCK ui_font(&lv_font_montserrat_40)
#elif LV_FONT_MONTSERRAT_32
#define FONT_CLOCK ui_font(&lv_font_montserrat_32)
#else
#define FONT_CLOCK ui_font(LV_FONT_DEFAULT)
#endif

#if LV_FONT_MONTSERRAT_32
#define FONT_BIG ui_font(&lv_font_montserrat_32)
#elif LV_FONT_MONTSERRAT_28
#define FONT_BIG ui_font(&lv_font_montserrat_28)
#elif LV_FONT_MONTSERRAT_24
#define FONT_BIG ui_font(&lv_font_montserrat_24)
#else
#define FONT_BIG ui_font(LV_FONT_DEFAULT)
#endif

#ifndef LV_KEYBOARD_CTRL_BTN_FLAGS
#define LV_KEYBOARD_CTRL_BTN_FLAGS (LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_CHECKED)
#endif

/* Big clock font for the screen saver: DejaVu Sans 96 px, digits, ':' and '-' only
   (font_clock_96.c, generated - the largest built-in LVGL font is 48 px) */
LV_FONT_DECLARE(font_clock_96)
#define FONT_SAVER (&font_clock_96)

#define DEG "\xC2\xB0"  // degree sign (UTF-8)
#define NO_NET_TEXT N_("Press Scan")
#define NO_NET_FOUND N_("No networks found")
#define KB_H 220  // on-screen keyboard height

/* Firmware version. Bump the minor number when something user-visible changes,
   the major when the settings format does. Shown in Settings -> About, on the
   web page and in the first log line. */
#define FW_VERSION "1.0"

#define MDNS_NAME "caravan"         // web page at http://caravan.local/
#define DEFAULT_WEB_NAME "Caravan"  // title of the web page until renamed in Settings
/* Battery (BMS) support. Set to 1 to bring the tab and the ECO-WORTHY/JBD
   decoding back; with 0 the code is still built but never runs, the tab is
   hidden and the switch in Settings has no effect. */
#define ENABLE_BMS 0

/* 1 = log every BLE frame and other chatty diagnostics. Off in normal use:
   the prints cost time inside the Bluetooth task. */
#define DEBUG_LOG 0  // (they also need the serial monitor on to reach USB serial)
#define dlogf(...) \
  do { \
    if (DEBUG_LOG) logf(__VA_ARGS__); \
  } while (0)
#define SAVER_TIMEOUT_MS 30000  // screen saver after this long without touch
#define SAVER_COLOR 0x505050    // screen saver clock color (dim grey)

#define MAX_TAGS 8            // RuuviTags in range remembered at the same time
#define MAX_RUUVI 3           // RuuviTags that can be added and named
#define MAX_VIC 6             // Victron devices that can be added
#define MAX_VIC_SEEN 12       // Victron devices remembered for the "Add" list
#define WEATHER_EVERY_MS (60UL * 60 * 1000)  // weather refresh while the screen is awake
#define VIC_STALE_MS 60000UL  // no Victron data for this long = "No signal"
#define MAX_RELAYS 8          // PCF8574 has 8 outputs
#define MAX_ALARMS 8          // messages kept at once
#define HIST_RECENT 60        // recent samples (10 s apart) for the sparklines
#define HIST_HOURS 24         // hourly energy buckets kept
#define HIST_DAYS 7           // daily energy buckets kept
#define MAX_BMS_SEEN 10       // named BLE devices listed on the Battery and Shelly pages
#define MAX_SHELLY 4          // Shelly devices that can be paired
#define BMS_MAX_CELLS 16
#define BMS_MAX_TEMPS 6

/* Victron Instant Readout record types */
#define VIC_SOLAR 0x01
#define VIC_BATTMON 0x02
#define VIC_INVERTER 0x03
#define VIC_DCDC 0x04
#define VIC_ACCHG 0x08

/* ================================================================== */
/* Data types                                                         */
/* ================================================================== */
struct Weather {
  bool valid = false;
  float temp = 0, feels = 0, wind = 0;
  int humidity = 0, code = 0;
  float today_max = 0, today_min = 0;
  char fc_day[3][8] = {};
  int fc_code[3] = {};
  float fc_max[3] = {}, fc_min[3] = {};
  time_t fetched = 0;
};

/* Settings and network results, protected by g_mtx */
struct Shared {
  // settings (stored in NVS)
  char ssid[33] = "";
  char pass[65] = "";
  char city[64] = "";
  char place[96] = "";
  float lat = 0, lon = 0;
  bool has_loc = false;
  int32_t utc_offset = 0;  // from Open-Meteo, follows DST
  bool offset_valid = false;
  char web_pass[33] = "";          // web page password, "" = no login
  char web_name[24] = DEFAULT_WEB_NAME;  // title of the web page
  // results for the UI
  char wifi_status[96] = "Not connected";
  bool wifi_up = false;    // connected to the access point
  int wifi_rssi = 0;       // signal strength in dBm while connected
  bool status_changed = true;
  char scan_list[1024] = "";
  int scan_sel = 0;
  bool scan_ready = false;
  char loc_status[128] = "";
  bool loc_changed = true;
  Weather weather;
  bool weather_changed = false;
};

struct RuuviTag {
  bool used = false;
  char addr[18] = "";  // "AA:BB:CC:DD:EE:FF"
  float temp = 0, hum = 0, pres = 0;
  bool has_temp = false, has_hum = false, has_pres = false;
  int batt_mv = 0;
  int rssi = 0;
  uint32_t last_seen = 0;
};

/* RuuviTag: one entry per added tag, saved to flash as one block */
struct RuuviCfg {
  bool used;
  char mac[18];
  char name[20];
};

/* Victron: one entry per added device, saved to flash as one block */
struct VicCfg {
  bool used;
  uint8_t type;
  char mac[18];
  char name[24];
  uint8_t key[16];
};

/* Victron: every device heard on BLE (for the "Add" list) */
struct VicSeen {
  bool used;
  uint8_t type;
  char mac[18];
  int rssi;
  uint32_t last_seen;
};

/* Victron: decoded values, same index as VicCfg. NAN = not available */
struct VicData {
  uint32_t last_seen;  // 0 = never heard
  bool key_ok, key_bad;
  uint8_t type, state, error, aux_mode;
  int rssi;
  float batt_v, batt_i;   // battery side (solar, battery monitor, AC charger, inverter)
  float pv_w, yield_kwh;  // solar charger
  float soc, consumed_ah, aux_v, temp_c;
  int remaining_min;        // -1 = not available
  float in_v, out_v;        // DC-DC converter
  float ac_va, ac_v, ac_i;  // inverter AC output
  uint16_t alarm;           // inverter alarm bits
};

/* A Shelly device, over Bluetooth or over WiFi. Saved to flash as one block. */
enum { SHELLY_BLE = 0,
       SHELLY_WIFI = 1 };

struct ShellyCfg {
  bool used;
  uint8_t addr_type;
  uint8_t transport;  // SHELLY_BLE or SHELLY_WIFI
  char mac[18];       // Bluetooth address
  char name[24];
  char host[32];  // WiFi: IP address or host name
};

/* Live values of one Shelly device */
struct ShellyData {
  bool connected, valid, on;
  float power, voltage, current;  // NAN if the device doesn't measure
  uint32_t updated;
  uint8_t fails;  // consecutive failures; the poll interval backs off
  char status[64];
};

/* Warnings and alarms */
enum { ALARM_LEVEL_NONE = 0,
       ALARM_LEVEL_WARN = 1,
       ALARM_LEVEL_ALARM = 2 };

struct Alarm {
  uint8_t level;
  char text[48];
};

/* Thresholds, saved to flash as one block */
struct AlarmCfg {
  bool enabled;
  uint8_t soc_warn;   // % - warning below this
  uint8_t soc_alarm;  // % - alarm below this
  int8_t temp_low;    // degC - frost alarm at or below this
  uint8_t wind_warn;  // m/s - 0 = off
  bool wake_saver;    // a new alarm wakes the screen saver
  bool warn_sd;       // warn when the TF card is missing or unreadable
};

/* One recent sample, taken every 10 s, for the small graphs in the tiles */
struct HistSample {
  float pv_w;    // solar power
  float load_w;  // estimated consumption
  float soc;     // state of charge
  float batt_w;  // battery power, + charging
};

/* One period of energy history (an hour or a day) */
struct HistBucket {
  float solar_wh;
  float load_wh;
  float soc;  // state of charge at the end of the period, NAN if unknown
};

/* The starting screen, saved to flash as one block */
struct SplashCfg {
  char text[20];
  uint8_t size;     // 20, 28, 32 or 48: the built-in Montserrat sizes
  uint8_t r, g, b;  // colour of the text
  uint8_t seconds;  // how long it stays, 0 = skip it
};

/* Ruuvi tab history: daily low and high per added tag (ruuvi_hist.cpp) */
#define RUUVI_DAYS 7
#define RUUVI_NONE INT16_MIN  // no reading that day
struct RuuviDay {
  int16_t lo, hi;  // tenths of a degree C
};
struct RuuviHist {
  char mac[18];   // the tag these days belong to
  int32_t day;    // local day number (days since 1970) of d[RUUVI_DAYS - 1]
  RuuviDay d[RUUVI_DAYS];  // oldest first
};

/* One timed schedule per output: relays first, then Shelly devices */
#define SCHED_COUNT (MAX_RELAYS + MAX_SHELLY)

struct Schedule {
  bool enabled;
  uint16_t on_min;   // minutes since midnight
  uint16_t off_min;  // may be before on_min: the window then crosses midnight
  uint8_t days;      // bit 0 = Monday ... bit 6 = Sunday
};

/* Relays on an external PCF8574, saved to flash as one block */
struct RelayCfg {
  uint8_t addr;     // 0 = not used, else 0x20-0x27 / 0x38-0x3F
  bool active_low;  // most relay boards switch on when the pin is LOW
  uint8_t count;    // relays in use, 1-8
  char names[MAX_RELAYS][20];
};

/* Battery BMS over BLE (experimental), saved to flash as one block */
struct BmsCfg {
  char mac[18];  // "" = none chosen
  uint8_t addr_type;
  char name[32];
};

/* Named BLE devices heard, for the Battery tab's device list */
struct BmsSeen {
  bool used;
  char name[32];
  char mac[18];
  uint8_t addr_type;
  int rssi;
  uint32_t last_seen;
};

/* Decoded BMS values (JBD protocol) and connection info */
struct BmsData {
  bool connected, is_jbd, valid, cells_valid;
  bool has_fets;  // JBD reports the charge/discharge switches; ECO-WORTHY does not
  uint32_t updated;  // millis() of last basic-info frame
  float volt, curr, remain_ah, nominal_ah;
  int cycles, soc;
  bool chg_fet, dsg_fet;
  uint16_t protection;
  int ncell, ntemp;
  float cell[BMS_MAX_CELLS];
  float temp[BMS_MAX_TEMPS];
  char status[96];       // connection status for the UI
  char services[200];    // GATT services found (diagnostics)
  char last_frame[100];  // last notification in hex (diagnostics)
};

/* ================================================================== */
/* Shared state (state.cpp)                                           */
/* ================================================================== */
extern HWCDC USBSerial;  // defined in board.cpp

extern Shared g;
extern SemaphoreHandle_t g_mtx, ruuvi_mtx, vic_mtx;
extern SemaphoreHandle_t ble_conn_mtx;  // only one task may connect over BLE at a time
extern Preferences prefs;

extern RuuviTag tags[MAX_TAGS];
extern RuuviCfg ruuvi_cfg[MAX_RUUVI];  // changed only by the UI (under g_mtx)
extern VicCfg vic_cfg[MAX_VIC];
extern VicData vic_data[MAX_VIC];
extern VicSeen vic_seen[MAX_VIC_SEEN];

extern SplashCfg splash_cfg;
extern AlarmCfg alarm_cfg;
extern BmsCfg bms_cfg;
extern ShellyCfg shelly_cfg[MAX_SHELLY];
extern RelayCfg relay_cfg;
extern uint8_t relay_state;  // bit set = relay ON
extern bool pcf_ok;

/* Requests from the UI to the network task (which also does all flash writes) */
extern volatile bool cmd_scan, cmd_connect, cmd_geocode, cmd_weather;
extern volatile bool cmd_save_rhist;  // Ruuvi history changed: save it
extern RuuviHist ruuvi_hist[MAX_RUUVI];
extern volatile bool cmd_save_lang;  // a new language was chosen: save lang_pending (the UI then restarts)
extern char lang_pending[LANG_CODE_LEN];
extern volatile bool cmd_save_ruuvi, cmd_save_vic, cmd_save_scan, cmd_save_bl, cmd_save_relay, cmd_save_bms, cmd_save_web, cmd_save_feat, cmd_save_shelly, cmd_save_alarm, cmd_save_sched, cmd_save_splash;
extern volatile bool scan_restart;

/* Features that can be switched off in Settings (services stop too) */
extern volatile bool feat_ruuvi;   // Temp tab: RuuviTag reading
extern volatile bool feat_relays;  // Relays tab: PCF8574
extern volatile bool feat_bms;     // Battery tab: BMS connection
extern volatile bool feat_shelly;  // Shelly tab: Shelly devices over Bluetooth (default off)
extern volatile bool feat_web;     // run the web server at all (default on)
extern volatile bool feat_remote;  // web page may switch relays and Shelly (default off)
extern volatile bool feat_sdlog;   // write the log to the TF card (default off)
extern volatile bool feat_powersave;  // slow the CPU while the screen sleeps
extern volatile bool feat_perflog;
extern volatile bool feat_serial;  // serial monitor: everything to USB serial (default off: faults only)
extern volatile bool feat_battmode;        // run on the LiPo when external power goes
extern volatile uint8_t batt_shutdown_pct;  // shut down at this charge
extern volatile bool feat_csv;     // write measurements to /data.csv (default off)
extern volatile uint8_t csv_interval_min;  // minutes between CSV lines

extern volatile uint8_t scan_interval_s;  // BLE scan interval, 1-10 s (1 = continuous)
extern volatile uint8_t bl_normal;        // backlight %, normal use
extern volatile uint8_t bl_saver;         // backlight %, screen saver

#define LOCK() xSemaphoreTake(g_mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(g_mtx)

void state_init();  // mutexes etc., call before anything else uses the state
void load_cfg();    // settings from flash
void set_wifi_status(const char *fmt, ...);
void set_loc_status(const char *fmt, ...);
void mac_short(const char *addr, char *out);  // "AA:BB:CC:DD:EE:FF" -> "EEFF"
const char *wmo_text(int code);               // weather code -> text
String url_encode(const char *s);

/* ================================================================== */
/* Hardware, network, sensors                                         */
/* ================================================================== */
/* board.cpp */
void board_init();  // IO expander, touch, display, LVGL
void set_backlight(uint8_t pct);
/* Onboard LiPo, measured by the CH32 chip. false = no battery connected. */
bool board_battery(float *volts, int *percent, bool *charging);

/* net.cpp */
void net_start();                     // starts the network task (WiFi, NTP, weather, flash writes)
bool net_wait_wifi_init(uint32_t ms);  // waits until the WiFi driver is initialised

/* web.cpp */
void web_service();           // call from loop(): starts the web server once WiFi is up, then serves requests
void web_password_changed();  // call after g.web_pass changed: logs everyone out

/* ruuvi.cpp */
void ruuvi_parse(const std::string &md, const char *addr, int rssi);

/* victron.cpp */
const char *vic_type_name(uint8_t t);
const char *vic_state_name(uint8_t s);
const char *vic_alarm_text(uint16_t a);  // NULL = no alarm
bool vic_supported(uint8_t t);
bool vic_active_state(uint8_t s);  // charger/converter state that means energy flows
void vic_clear_data(VicData &v);
void vic_handle(const std::string &md, const char *addr, int rssi);
bool vic_fresh(const VicData &d, uint32_t now);
const char *vic_status(const VicData &d, uint8_t type, uint32_t now);

/* ble.cpp */
void ble_start();
void ble_scan_service();          // call from loop()
void ble_pause_scan(bool pause);  // stop scanning while connecting to a device

/* bms.cpp (experimental: battery BMS over BLE, JBD protocol + diagnostics) */
void bms_start();  // starts the BMS task
void bms_note_adv(const char *name, const char *mac, uint8_t addr_type, int rssi);  // from BLE scan
void bms_connect_to(const char *mac, uint8_t addr_type, const char *name);          // from the UI
void bms_get(BmsData *out);                      // copy of the current data
int bms_get_seen(BmsSeen *out, int max);         // copy of the device list
const char *bms_protection_text(uint16_t bits);  // NULL = no protection active

/* sdlog.cpp (log to Serial, to a ring buffer readable at /log, and to the TF card) */
void log_begin();
void log_boot_banner();  // version, build date and the environment check
void logf(const char *fmt, ...);       // the log; to USB serial only with the serial monitor on
void log_fault(const char *fmt, ...);  // an error or warning: to USB serial always
void serial_apply();                   // after feat_serial changed: sets the ESP-IDF log level
void serf(const char *fmt, ...);       // USB serial only, and only with the serial monitor on
void serln(const char *s = "");
void log_dump(String &out);
bool sd_log_mount();
bool sd_log_probe();  // works out which CH32 bit releases the card
void sd_log_unmount();
bool sd_log_ok();
const char *sd_log_status();
size_t sd_log_size();
const char *sd_log_name();
void sd_card_info(char *out, size_t n);
bool sd_log_new_file();
int sd_log_delete_old();
void sd_list_files(String &out);
fs::FS &sd_fs();  // the mounted card (SD mode or SPI mode)

/* sw6106.cpp (power bank controller, where fitted) */
void sw6106_begin();
void sw6106_service();  // call from loop(): keep-alive against light-load shutdown
bool sw6106_present();

/* power.cpp */
void power_set_saving(bool screen_asleep);
extern volatile bool screen_asleep;  // the screen saver is on (set whether or not the CPU is slowed)
bool power_saving_active();
void power_web_activity();      // a web request came in: full speed for a while, even under the screen saver
void power_wifi_sleep_apply();  // modem sleep only while the web server is off
void perf_service();            // call from loop()
void power_battery_service();   // call from loop(): watches the onboard cell
bool power_on_battery();        // true while running on the LiPo
void battery_monitor();         // call from loop(): one status line every 5 s

/* alarms.cpp */
void alarms_begin();
void alarms_check();  // once a second from the UI
int alarms_get(Alarm *out, int max);
bool alarms_top(char *text, size_t len, uint8_t *level);
bool alarms_take_new();  // true once after a new alarm appears

/* history.cpp (energy history behind the Power tab's History screen) */
void history_begin();
void history_service();  // call from loop()
void history_get(HistBucket **hour_arr, HistBucket **day_arr);
void history_totals(bool daily, float *solar_kwh, float *load_kwh);
const HistSample *history_recent();  // HIST_RECENT entries, oldest first

/* ui_history.cpp */
void build_history_screen();
void show_history_screen();
void history_timer_cb(lv_timer_t *t);

/* datalog.cpp (measurements as CSV, settings backup/restore) */
void csv_service();  // call from loop()
const char *csv_status();
bool settings_backup();
bool settings_restore();

/* shelly.cpp (Shelly devices over BLE RPC; off until switched on in Settings) */
void shelly_start();
bool shelly_present();  // switched on and at least one device added
void shelly_get(int idx, ShellyData *out);
void shelly_set(int idx, bool on);  // switch a device on or off
void shelly_add(const char *mac, uint8_t addr_type, const char *name);        // Bluetooth
void shelly_add_wifi(const char *host, const char *name);                     // WiFi
void shelly_retry(int idx);  // poll this device again at once
void shelly_remove(int idx);

/* schedule.cpp */
extern Schedule sched[SCHED_COUNT];
void schedule_begin();
void schedule_service();  // call from loop()
const char *schedule_target_name(int i);
bool schedule_target_exists(int i);
bool schedule_target_on(int i);
void schedule_override(int i, bool on);  // switch by hand until the next change
void schedule_resume(int i);
bool schedule_overridden(int i);
void schedule_status(int i, char *out, size_t len);
void settings_schedule(lv_obj_t *parent);  // ui_schedule.cpp
void schedule_settings_refresh();

/* relays.cpp */
bool relays_present();  // a board is set up and answering
uint8_t relay_mask();
bool relay_apply();      // write relay_state to the PCF8574
void relay_read_back();  // read the relays' current state from the PCF8574
const char *i2c_guess(uint8_t addr);
int i2c_scan(char *out, size_t len);  // returns number of devices found

/* ================================================================== */
/* UI                                                                 */
/* ================================================================== */
/* ui_common.cpp */
extern lv_obj_t *tab_home, *tab_power, *tab_battery, *tab_relays, *tab_shelly, *tab_temp, *tab_weather, *tab_settings, *kb;
const lv_font_t *splash_font(uint8_t size);
void splash_show();    // the starting screen, called as soon as the settings are loaded
void settings_splash(lv_obj_t *parent);  // ui_settings.cpp
void splash_finish();  // waits until 5 s after power-on, then removes it
void build_ui();
void ui_update_tabs();  // hides the tabs of switched-off features
void kb_show(lv_obj_t *ta);
void kb_hide();
lv_obj_t *make_row(lv_obj_t *parent, lv_flex_align_t main_align);
lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb);
lv_obj_t *make_ta(lv_obj_t *parent, const char *placeholder, void (*on_ready)() = nullptr, bool hex = false);
lv_obj_t *make_heading(lv_obj_t *parent, const char *txt);
lv_obj_t *make_section(lv_obj_t *parent, const char *title);  // card with a heading, for Settings
lv_obj_t *make_switch_row(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb);
lv_obj_t *make_grey_label(lv_obj_t *parent);
lv_obj_t *make_slider(lv_obj_t *parent, int min, int max, int val, lv_event_cb_t cb);
bool tab_visible(lv_obj_t *tab);  // false when hidden: timers can skip their work
void set_label(lv_obj_t *l, const char *txt);  // only redraws when the text changes
lv_obj_t *make_sparkline(lv_obj_t *parent, uint32_t color, int w, int h, int points);
void set_sparkline(lv_obj_t *chart, const float *values, int n, float floor_max);
lv_obj_t *make_segment(lv_obj_t *parent, lv_event_cb_t cb, void *user_data);  // Off | On control
void set_segment(lv_obj_t *seg, bool on);

/* ui_home.cpp */
void build_home_tab();
void home_timer_cb(lv_timer_t *t);

/* ui_power.cpp */
void build_power_tab();
void power_timer_cb(lv_timer_t *t);

/* ui_battery.cpp */
void build_battery_tab();
void battery_timer_cb(lv_timer_t *t);

/* ui_relays.cpp */
void build_relays_tab();
void settings_relays(lv_obj_t *parent);
void relay_tab_refresh();
void settings_i2c(lv_obj_t *parent);

/* ui_shelly.cpp */
void build_shelly_tab();
void shelly_timer_cb(lv_timer_t *t);
void settings_shelly(lv_obj_t *parent);
void shelly_settings_refresh(const BmsSeen *seen, int n, uint32_t now);

/* ruuvi_hist.cpp */
void ruuvi_hist_note(int slot, const char *mac, float temp);     // a reading from an added tag
bool ruuvi_hist_get(int slot, const char *mac, RuuviHist *out);  // false: nothing for this tag yet

/* ui_temp.cpp (the Ruuvi tab) */
void build_temp_tab();
void ruuvi_timer_cb(lv_timer_t *t);

/* ui_weather.cpp */
void build_weather_tab();
void format_local_time(char *tb, char *db);  // tb >= 16, db >= 64 chars
void clock_timer_cb(lv_timer_t *t);
void net_poll_cb(lv_timer_t *t);

/* ui_settings.cpp */
extern lv_obj_t *lbl_wifi_status, *dd_ssid, *lbl_loc;
void build_settings_tab();
void bms_settings_refresh(const BmsSeen *seen, int n, uint32_t now);
void ruuvi_settings_refresh(const RuuviTag *copy, uint32_t now);

/* ui_victron_settings.cpp */
void settings_victron(lv_obj_t *parent);
void victron_settings_refresh(const VicCfg *cfg, const VicData *dat, const VicSeen *seen, uint32_t now);

/* ui_saver.cpp */
void build_saver();
void saver_timer_cb(lv_timer_t *t);

/* ================================================================== */
/* Translation of text given to LVGL                                  */
/* ================================================================== */
/* These wrap LVGL's own functions (the name in brackets is not expanded again)
   so that every label, placeholder, tab name and option list is translated. */
#define lv_label_set_text(obj, txt) (lv_label_set_text)(obj, tr(txt))
#define lv_label_set_text_fmt(obj, fmt, ...) (lv_label_set_text_fmt)(obj, tr(fmt), ##__VA_ARGS__)
#define lv_textarea_set_placeholder_text(obj, txt) (lv_textarea_set_placeholder_text)(obj, tr(txt))
#define lv_dropdown_set_options(obj, opts) (lv_dropdown_set_options)(obj, tr(opts))
#define lv_dropdown_set_options_static(obj, opts) (lv_dropdown_set_options_static)(obj, tr(opts))
#define lv_tabview_add_tab(tv, name) (lv_tabview_add_tab)(tv, tr(name))

