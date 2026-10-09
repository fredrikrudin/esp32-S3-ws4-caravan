// esp32-S3-ws4-caravan v1.0
/* Shared state, settings loading and small helpers */
#include "app.h"

Shared g;
SemaphoreHandle_t g_mtx, ruuvi_mtx, vic_mtx;
SemaphoreHandle_t ble_conn_mtx;
Preferences prefs;

RuuviTag tags[MAX_TAGS];
RuuviCfg ruuvi_cfg[MAX_RUUVI];
VicCfg vic_cfg[MAX_VIC];
VicData vic_data[MAX_VIC];
VicSeen vic_seen[MAX_VIC_SEEN];

RelayCfg relay_cfg = { 0, true, 8, {} };
BmsCfg bms_cfg = { "", 0, "" };
ShellyCfg shelly_cfg[MAX_SHELLY];
SplashCfg splash_cfg = { "CABBY", 48, 0xD6, 0x00, 0x1C, 5 };  // Cabby red
uint8_t relay_state = 0;
bool pcf_ok = false;

volatile bool cmd_scan = false, cmd_connect = false, cmd_geocode = false, cmd_weather = false;
volatile bool cmd_save_ruuvi = false, cmd_save_vic = false, cmd_save_scan = false;
volatile bool cmd_save_lang = false;
volatile bool cmd_save_rhist = false;
volatile bool cmd_save_bl = false, cmd_save_relay = false, cmd_save_bms = false, cmd_save_web = false, cmd_save_feat = false, cmd_save_shelly = false, cmd_save_alarm = false, cmd_save_sched = false, cmd_save_splash = false;
volatile bool scan_restart = false;

volatile bool feat_ruuvi = true, feat_relays = true, feat_bms = ENABLE_BMS;
volatile bool feat_shelly = false;  // Shelly over Bluetooth is off until switched on
volatile bool feat_web = true;      // the web server runs unless switched off
volatile bool feat_remote = false;  // switching from the web page is off until switched on
volatile bool feat_sdlog = false;   // logging to the TF card is off until switched on
volatile bool feat_powersave = true;   // on by default: it only slows the CPU while asleep
volatile bool feat_perflog = false;
volatile bool feat_serial = false;
volatile bool feat_battmode = false;  // off until switched on
volatile uint8_t batt_shutdown_pct = 20;
volatile bool feat_csv = false;     // measurement logging is off until switched on
volatile uint8_t csv_interval_min = 5;
volatile uint8_t scan_interval_s = 1;
volatile uint8_t bl_normal = 100;
volatile uint8_t bl_saver = 10;

void state_init() {
  g_mtx = xSemaphoreCreateMutex();
  ruuvi_mtx = xSemaphoreCreateMutex();
  vic_mtx = xSemaphoreCreateMutex();
  ble_conn_mtx = xSemaphoreCreateMutex();
  for (int i = 0; i < MAX_VIC; i++) vic_clear_data(vic_data[i]);
}

void load_cfg() {
  prefs.begin("wx", false);
  prefs.getString("lang", ui_lang, sizeof(ui_lang));
  if (!ui_lang[0]) strlcpy(ui_lang, "en", sizeof(ui_lang));
  lang_begin();  // before anything below builds text
  prefs.getString("ssid", g.ssid, sizeof(g.ssid));
  prefs.getString("pass", g.pass, sizeof(g.pass));
  prefs.getString("city", g.city, sizeof(g.city));
  prefs.getString("place", g.place, sizeof(g.place));
  prefs.getString("webpass", g.web_pass, sizeof(g.web_pass));
  prefs.getString("webname", g.web_name, sizeof(g.web_name));
  if (!g.web_name[0]) strlcpy(g.web_name, DEFAULT_WEB_NAME, sizeof(g.web_name));
  g.has_loc = prefs.getBool("hasloc", false);
  g.lat = prefs.getFloat("lat", 0);
  g.lon = prefs.getFloat("lon", 0);
  g.offset_valid = prefs.isKey("utcoff");
  g.utc_offset = prefs.getInt("utcoff", 0);
  if (g.has_loc) snprintf(g.loc_status, sizeof(g.loc_status), TR("Location: %s"), g.place);
  else strlcpy(g.loc_status, TR("No location set"), sizeof(g.loc_status));

  uint8_t feat = prefs.getUChar("feat", 0x01 | 0x02 | (ENABLE_BMS ? 0x04 : 0));
  feat_ruuvi = feat & 0x01;
  feat_relays = feat & 0x02;
  feat_bms = (feat & 0x04) && ENABLE_BMS;  // ENABLE_BMS 0 overrides whatever was saved
  feat_shelly = feat & 0x08;
  feat_remote = feat & 0x10;
  feat_web = !prefs.getUChar("noweb", 0);  // stored inverted, so old settings keep the server on
  feat_sdlog = feat & 0x20;
  feat_csv = feat & 0x40;
  feat_powersave = !(feat & 0x80);  // stored inverted, so old settings default to on
  csv_interval_min = constrain(prefs.getUChar("csvmin", 5), 1, 60);
  if (prefs.getBytesLength("rhist") == sizeof(ruuvi_hist)) prefs.getBytes("rhist", ruuvi_hist, sizeof(ruuvi_hist));
  if (prefs.getBytesLength("splash") == sizeof(splash_cfg)) prefs.getBytes("splash", &splash_cfg, sizeof(splash_cfg));
  if (!splash_cfg.text[0]) strlcpy(splash_cfg.text, "CABBY", sizeof(splash_cfg.text));

  feat_battmode = prefs.getUChar("battmode", 0);
  batt_shutdown_pct = constrain(prefs.getUChar("battpct", 20), 5, 60);

  memset(shelly_cfg, 0, sizeof(shelly_cfg));
  if (prefs.getBytesLength("shelly") == sizeof(shelly_cfg)) prefs.getBytes("shelly", shelly_cfg, sizeof(shelly_cfg));

  scan_interval_s = constrain(prefs.getUChar("scanint", 1), 1, 10);
  bl_normal = constrain(prefs.getUChar("bl", 100), 5, 100);
  bl_saver = constrain(prefs.getUChar("blsaver", 10), 0, 100);

  if (prefs.getBytesLength("relay") == sizeof(relay_cfg)) prefs.getBytes("relay", &relay_cfg, sizeof(relay_cfg));
  relay_cfg.count = constrain(relay_cfg.count, 1, MAX_RELAYS);
  for (int i = 0; i < MAX_RELAYS; i++)
    if (!relay_cfg.names[i][0]) snprintf(relay_cfg.names[i], sizeof(relay_cfg.names[i]), "Relay %d", i + 1);

  /* RuuviTags; the old single-tag setting becomes the first tag */
  memset(ruuvi_cfg, 0, sizeof(ruuvi_cfg));
  if (prefs.getBytesLength("ruuvis") == sizeof(ruuvi_cfg)) {
    prefs.getBytes("ruuvis", ruuvi_cfg, sizeof(ruuvi_cfg));
  } else {
    char old[18] = "";
    prefs.getString("ruuvi", old, sizeof(old));
    if (old[0]) {
      char s[8];
      mac_short(old, s);
      ruuvi_cfg[0].used = true;
      strlcpy(ruuvi_cfg[0].mac, old, sizeof(ruuvi_cfg[0].mac));
      snprintf(ruuvi_cfg[0].name, sizeof(ruuvi_cfg[0].name), "Ruuvi %s", s);
      cmd_save_ruuvi = true;  // store in the new format
    }
  }
  int nruuvi = 0;
  for (int i = 0; i < MAX_RUUVI; i++) nruuvi += ruuvi_cfg[i].used;

  if (prefs.getBytesLength("bms") == sizeof(bms_cfg)) prefs.getBytes("bms", &bms_cfg, sizeof(bms_cfg));

  memset(vic_cfg, 0, sizeof(vic_cfg));
  if (prefs.getBytesLength("victron") == sizeof(vic_cfg)) prefs.getBytes("victron", vic_cfg, sizeof(vic_cfg));
  int nvic = 0;
  for (int i = 0; i < MAX_VIC; i++) nvic += vic_cfg[i].used;

  serf("Loaded: ssid='%s' city='%s' place='%s' lat=%.4f lon=%.4f ruuvi=%d victron=%d relay=0x%02X bms='%s'\n",
                   g.ssid, g.city, g.place, g.lat, g.lon, nruuvi, nvic, relay_cfg.addr, bms_cfg.name);
}

void set_wifi_status(const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  LOCK();
  strlcpy(g.wifi_status, buf, sizeof(g.wifi_status));
  g.status_changed = true;
  UNLOCK();
}

void set_loc_status(const char *fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  LOCK();
  strlcpy(g.loc_status, buf, sizeof(g.loc_status));
  g.loc_changed = true;
  UNLOCK();
}

void mac_short(const char *addr, char *out) {
  if (strlen(addr) >= 17) {
    out[0] = addr[12];
    out[1] = addr[13];
    out[2] = addr[15];
    out[3] = addr[16];
    out[4] = 0;
  } else {
    strcpy(out, "????");
  }
}

const char *wmo_text(int c) {
  switch (c) {
    case 0: return N_("Clear sky");
    case 1: return N_("Mainly clear");
    case 2: return N_("Partly cloudy");
    case 3: return N_("Overcast");
    case 45: case 48: return N_("Fog");
    case 51: case 53: case 55: return N_("Drizzle");
    case 56: case 57: return N_("Freezing drizzle");
    case 61: return N_("Light rain");
    case 63: return N_("Rain");
    case 65: return N_("Heavy rain");
    case 66: case 67: return N_("Freezing rain");
    case 71: return N_("Light snow");
    case 73: return N_("Snow");
    case 75: return N_("Heavy snow");
    case 77: return N_("Snow grains");
    case 80: case 81: case 82: return N_("Showers");
    case 85: case 86: return N_("Snow showers");
    case 95: return N_("Thunderstorm");
    case 96: case 99: return N_("Thunder + hail");
    default: return N_("Unknown");
  }
}

String url_encode(const char *s) {
  const char *hex = "0123456789ABCDEF";
  String o;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}
