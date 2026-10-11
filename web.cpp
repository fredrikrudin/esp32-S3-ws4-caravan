// esp32-S3-ws4-caravan v1.0
/* Web server (port 80).
 *   /        The page: a fixed HTML file (web_page.h) that loads /json every 5 s and
 *            draws itself in the browser, adapting to phone, tablet or PC.
 *   /lang    The page's texts in the display's language (see handle_lang)
 *   /json    All the data as JSON: for the page, scripts or Home Assistant
 *            (?key=<password> if a password is set).
 *   /login   Password-only login, when a password is set under Settings -> Web page.
 *   /logout
 *   /switch  POST, only with "Remote admin" enabled in Settings: switches a relay or Shelly
 *   /log     the most recent log lines as plain text (same log as the Serial Monitor)
 *   /files   list of files on the TF card; /files?name=... downloads one
 * Reachable at http://<page name>.local/ (mDNS; caravan.local until renamed) or the board's IP address.
 * Starts as soon as WiFi is connected. Runs from loop(), so it never races the UI,
 * and never makes it wait: the server is web_http.cpp, which handles several
 * connections at once and only ever sends what the network takes at once.
 *
 * Note: plain HTTP, so the password travels unencrypted on the local network.
 * It keeps casual visitors on the same WiFi out; it is not strong security. */
#include "app.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "web_http.h"
#include "web_page.h"

#ifndef WEB_PORT
#define WEB_PORT 80  // tools/webpreview uses 8080
#endif

static MiniHttp server;
static bool started = false;

/* ---------- text helpers (used everywhere below) ---------- */
static void add(String &s, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  s += buf;
}

/* names are typed by the user: escape them for HTML / JSON */
static String esc_html(const char *in) {
  String o;
  for (; *in; in++) {
    if (*in == '<') o += "&lt;";
    else if (*in == '>') o += "&gt;";
    else if (*in == '&') o += "&amp;";
    else if (*in == '"') o += "&quot;";
    else o += *in;
  }
  return o;
}

static String esc_json(const char *in) {
  String o;
  for (; *in; in++) {
    if (*in == '"' || *in == '\\') o += '\\';
    if ((unsigned char)*in >= 0x20) o += *in;
  }
  return o;
}

static void num_json(String &s, const char *key, float v, int decimals) {
  if (isnan(v)) add(s, "\"%s\":null", key);
  else add(s, "\"%s\":%.*f", key, decimals, v);
}

/* ---------- login ---------- */
static char token[33] = "";     // session token in the login cookie; new after boot or password change
static uint32_t last_fail = 0;  // wrong password: next attempt allowed after 2 s

static void new_token() {
  for (int i = 0; i < 16; i++) sprintf(token + i * 2, "%02x", (unsigned)(esp_random() & 0xFF));
}

void web_password_changed() {
  new_token();  // everyone logged in has to log in again
}

/* The page's name, made into a host name for http://<name>.local/:
   lower case, a-z 0-9 and '-', with å ä ö (and Å Ä Ö) as a a o */
void web_host_name(char *out, size_t n) {
  char name[24];
  LOCK();
  strlcpy(name, g.web_name, sizeof(name));
  UNLOCK();
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)name; *p && o + 1 < n; p++) {
    char c = 0;
    if (*p >= 'a' && *p <= 'z') c = *p;
    else if (*p >= 'A' && *p <= 'Z') c = *p - 'A' + 'a';
    else if (*p >= '0' && *p <= '9') c = *p;
    else if (*p == 0xC3 && p[1]) {  // UTF-8 for a-ring, a/o-umlaut and their capitals
      unsigned char d = *++p;
      c = (d == 0xA5 || d == 0xA4 || d == 0x85 || d == 0x84) ? 'a' : (d == 0xB6 || d == 0x96) ? 'o' : '-';
    } else c = '-';
    if (c == '-' && (o == 0 || out[o - 1] == '-')) continue;  // no leading or double dashes
    out[o++] = c;
  }
  while (o && out[o - 1] == '-') o--;
  out[o] = 0;
  if (!o) strlcpy(out, MDNS_NAME, n);
}

static volatile bool host_changed = false;
void web_name_changed() {
  host_changed = true;  // web_service() announces the new name
}

static void get_page_name(char *out, size_t n) {
  LOCK();
  strlcpy(out, g.web_name, n);
  UNLOCK();
  if (!out[0]) strlcpy(out, DEFAULT_WEB_NAME, n);
}

static void get_password(char *out) {
  LOCK();
  strlcpy(out, g.web_pass, 33);
  UNLOCK();
}

static bool authorized() {
  char pass[33];
  get_password(pass);
  if (!pass[0]) return true;  // no password set: open page
  if (server.hasHeader("Cookie") && server.header("Cookie").indexOf(String("caravan=") + token) >= 0) return true;
  if (server.hasArg("key") && server.arg("key") == pass) return true;  // for scripts: /json?key=...
  return false;
}

static const char PAGE_HEAD[] PROGMEM =
  "<!DOCTYPE html><html><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<style>"
  "body{font-family:system-ui,sans-serif;background:#111418;color:#e6e8eb;margin:0 auto;padding:16px;max-width:760px}"
  "h1{font-size:20px;margin:0 0 12px}h2{font-size:15px;color:#2196f3;margin:20px 0 6px}"
  ".soc{font-size:56px;font-weight:bold}.sub{color:#9e9e9e;font-size:13px}"
  ".row{display:flex;justify-content:space-between;padding:8px 10px;background:#282b30;border-radius:6px;margin:4px 0}"
  ".on{color:#4caf50;font-weight:bold}.off{color:#9e9e9e}.chg{color:#f39c12;font-weight:bold}"
  ".bar{height:10px;background:#1f5f8b;border-radius:5px;overflow:hidden;margin:6px 0}"
  ".bar div{height:100%;background:#3498db}a{color:#2196f3}"
  ".hero{display:flex;gap:14px;align-items:stretch;margin:10px 0 4px}"
  ".g{width:16px;border-radius:8px;background:#10202c;position:relative}"
  ".g i{position:absolute;bottom:0;left:0;width:100%;border-radius:8px}"
  ".gb i{background:linear-gradient(to top,#0b4a7a,#38a9f5)}.gs i{background:linear-gradient(to top,#8a4d06,#f5b32b)}"
  ".gl i{background:linear-gradient(to top,#6e1e16,#e74c3c)}"
  ".mid{flex:1;text-align:center}.clock{font-size:64px;font-weight:bold;line-height:1.1}"
  ".vals{display:flex;justify-content:space-between;font-size:14px;margin-top:6px}"
  ".strip{display:flex;justify-content:space-evenly;background:#1b1f24;border-radius:12px;padding:10px;margin:10px 0}"
  ".fc{display:flex;gap:8px}.fc>div{flex:1;background:#23272e;border-radius:10px;padding:8px;text-align:center;font-size:13px}"
  ".big{font-size:26px}"
  ".alarm{padding:10px 12px;border-radius:8px;margin:8px 0;font-weight:bold}"
  ".lvl2{background:#c0392b}.lvl1{background:#b9770e}"
  ".chart{width:100%;height:150px;background:#1b1f24;border-radius:10px;margin:6px 0}"
  ".sparks{display:flex;gap:10px;flex-wrap:wrap}"
  ".spark{flex:1;min-width:200px;background:#1b1f24;border-radius:10px;padding:6px}"
  ".spark svg{width:100%;height:60px}"
  "form.sw{margin:0}.seg{display:flex;border-radius:8px;overflow:hidden}"
  ".seg button,.seg span{font-size:14px;padding:8px 16px;border:1px solid #2f6fb5;background:#0e2233;color:#e0e0e0;width:auto}"
  ".seg button:first-child,.seg span:first-child{border-radius:8px 0 0 8px}"
  ".seg button:last-child,.seg span:last-child{border-radius:0 8px 8px 0}"
  ".seg .act{background:#2f6fb5;color:#fff}"
  "input,button{font-size:16px;padding:10px;border-radius:6px;border:1px solid #3a3f44}"
  "input{background:#282b30;color:#e0e0e0;width:100%;box-sizing:border-box;margin:8px 0}"
  "button{background:#2196f3;color:#fff;border:0;width:100%}.err{color:#f39c12}"
  "</style>";

static void send_login(const char *msg) {
  String s;
  s.reserve(1800);
  char name[24];
  get_page_name(name, sizeof(name));
  s += FPSTR(PAGE_HEAD);
  add(s, "<title>%s</title></head><body><h1>%s</h1>", esc_html(name).c_str(), esc_html(name).c_str());  // lang: set in PAGE_HEAD
  add(s, "<form method='post' action='/login'><input type='password' name='p' placeholder='%s' autofocus>"
         "<button>%s</button></form>", TR("Password"), TR("Log in"));
  if (msg && msg[0]) {
    s += F("<p class='err'>");
    s += msg;
    s += F("</p>");
  }
  s += F("</body></html>");
  server.send(200, "text/html; charset=utf-8", s);
}

static void redirect(const char *to) {
  server.sendHeader("Location", to);
  server.send(303, "text/plain", "");
}

static void handle_login() {
  if (server.method() != MH_POST) {
    send_login("");
    return;
  }
  if (last_fail && millis() - last_fail < 2000) {
    send_login(TR("Too many attempts - wait a moment"));
    return;
  }
  char pass[33];
  get_password(pass);
  if (!pass[0] || server.arg("p") == pass) {
    server.sendHeader("Set-Cookie", String("caravan=") + token + "; Path=/; HttpOnly; Max-Age=2592000");
    redirect("/");
  } else {
    last_fail = millis() | 1;
    send_login(TR("Wrong password"));
  }
}

static void handle_logout() {
  server.sendHeader("Set-Cookie", "caravan=; Path=/; Max-Age=0");
  redirect("/login");
}

/* snapshot of everything a page needs */
static VicCfg cfg[MAX_VIC];
static VicData dat[MAX_VIC];
static BmsData bms;
static RuuviTag rtags[MAX_TAGS];
static ShellyData sh[MAX_SHELLY];
static Weather wx;
static bool wx_has_loc;

static void take_snapshot() {
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  memcpy(cfg, vic_cfg, sizeof(cfg));
  memcpy(dat, vic_data, sizeof(dat));
  xSemaphoreGive(vic_mtx);
  bms_get(&bms);

  xSemaphoreTake(ruuvi_mtx, portMAX_DELAY);
  memcpy(rtags, tags, sizeof(rtags));
  xSemaphoreGive(ruuvi_mtx);

  for (int i = 0; i < MAX_SHELLY; i++) shelly_get(i, &sh[i]);

  LOCK();
  wx = g.weather;
  wx_has_loc = g.has_loc;
  UNLOCK();
}

/* temperature of an added RuuviTag, NAN if not heard lately */
static float ruuvi_temp(int slot, uint32_t now) {
  if (!feat_ruuvi || !ruuvi_cfg[slot].used) return NAN;
  for (int i = 0; i < MAX_TAGS; i++)
    if (rtags[i].used && !strcmp(rtags[i].addr, ruuvi_cfg[slot].mac) && rtags[i].has_temp && now - rtags[i].last_seen < 600000UL)
      return rtags[i].temp;
  return NAN;
}

static bool vic_ok(int i, uint32_t now) {
  return cfg[i].used && vic_fresh(dat[i], now) && dat[i].key_ok;
}

/* Is this device putting energy into the battery right now? */
static bool vic_charging(int i, uint32_t now) {
  if (!vic_ok(i, now)) return false;
  const VicData &d = dat[i];
  switch (cfg[i].type) {
    case VIC_SOLAR: return !isnan(d.pv_w) && d.pv_w > 2;
    case VIC_ACCHG: return !isnan(d.batt_i) && d.batt_i > 0.2f;
    case VIC_DCDC: return vic_active_state(d.state);
    default: return false;
  }
}

/* Power in watts for the device list, NAN if not known */
static float vic_power(int i) {
  const VicData &d = dat[i];
  switch (cfg[i].type) {
    case VIC_SOLAR: return d.pv_w;
    case VIC_ACCHG:
    case VIC_BATTMON: return (isnan(d.batt_v) || isnan(d.batt_i)) ? NAN : d.batt_v * d.batt_i;
    case VIC_INVERTER: return d.ac_va;  // VA
    default: return NAN;
  }
}

/* First battery monitor with fresh data, -1 if none */
static int battery_monitor(uint32_t now) {
  for (int i = 0; i < MAX_VIC; i++)
    if (cfg[i].type == VIC_BATTMON && vic_ok(i, now)) return i;
  return -1;
}


/* ---------- the page ---------- */
/* The page's texts in the display's language, as {"English": "translation", ...}.
   They are the T("...") calls in web_page.h: found there at run time, so the list
   can't go stale, and tools/i18n.py puts the same texts in lang/<code>.po.
   In English (or for a text not translated yet) the page keeps its own text. */
static void handle_lang() {
  String s;
  s.reserve(4096);  // PSRAM
  s += '{';
  if (ui_translated) {
    char en[96];
    for (const char *p = WEB_PAGE; (p = strstr(p, "T(\"")) != nullptr;) {
      bool call = p == WEB_PAGE || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
      p += 3;
      const char *e = strchr(p, '"');
      if (!e) break;
      size_t n = e - p;
      if (!call || n == 0 || n >= sizeof(en)) continue;
      memcpy(en, p, n);
      en[n] = 0;
      const char *t = tr(en);
      if (t == en || !strcmp(t, en)) continue;  // not translated
      String key = "\"" + esc_json(en) + "\":";
      if (s.indexOf(key) >= 0) continue;  // used more than once in the page
      if (s.length() > 1) s += ',';
      s += key;
      s += '"';
      s += esc_json(t);
      s += '"';
    }
  }
  s += '}';
  server.send(200, "application/json; charset=utf-8", s);
}

/* One fixed file: the browser keeps it (ETag) and only fetches /json after that */
static void handle_root() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  static const char etag[] = "\"" FW_VERSION "-" __DATE__ "-" __TIME__ "\"";
  if (server.header("If-None-Match") == etag) {
    server.send(304, "text/plain", "");
    return;
  }
  server.sendHeader("ETag", etag);
  server.sendHeader("Cache-Control", "no-cache");  // the browser checks the ETag each time
  server.send_P(200, "text/html; charset=utf-8", WEB_PAGE, strlen_P(WEB_PAGE));
}

/* ---------- JSON ---------- */
/* n values, stride floats apart (fields of a struct array), NAN as null */
static void json_series(String &s, const char *key, const float *v, int n, int stride, int decimals) {
  add(s, "\"%s\":[", key);
  for (int i = 0; i < n; i++) {
    float x = v[i * stride];
    if (i) s += ',';
    if (isnan(x)) s += "null";
    else add(s, "%.*f", decimals, x);
  }
  s += ']';
}

/* The history behind the page's charts: 24 hours of solar and consumption (Wh)
   with the battery %, and the last 10 minutes */
static void json_history(String &s) {
  HistBucket *hours, *days;
  history_get(&hours, &days);
  const HistSample *rec = history_recent();
  const int hs = sizeof(HistBucket) / sizeof(float), rs = sizeof(HistSample) / sizeof(float);
  s += "\"history\":{";
  if (hours) {
    s += "\"hours\":{";
    json_series(s, "solar", &hours[0].solar_wh, HIST_HOURS, hs, 0);
    s += ',';
    json_series(s, "load", &hours[0].load_wh, HIST_HOURS, hs, 0);
    s += ',';
    json_series(s, "soc", &hours[0].soc, HIST_HOURS, hs, 0);
    s += '}';
  }
  if (rec) {
    s += hours ? ",\"recent\":{" : "\"recent\":{";
    json_series(s, "solar", &rec[0].pv_w, HIST_RECENT, rs, 0);
    s += ',';
    json_series(s, "load", &rec[0].load_w, HIST_RECENT, rs, 0);
    s += ',';
    json_series(s, "soc", &rec[0].soc, HIST_RECENT, rs, 1);
    s += '}';
  }
  s += '}';
}

static void json_weather(String &s) {
  s += "\"weather\":{";
  if (wx.valid) {
    num_json(s, "temperature", wx.temp, 1);
    s += ',';
    num_json(s, "feels_like", wx.feels, 1);
    s += ',';
    add(s, "\"humidity\":%d,", wx.humidity);
    num_json(s, "wind", wx.wind, 1);
    add(s, ",\"condition\":\"%s\",", tr(wmo_text(wx.code)));
    num_json(s, "today_max", wx.today_max, 0);
    s += ',';
    num_json(s, "today_min", wx.today_min, 0);
    s += ",\"forecast\":[";
    for (int i = 0; i < 3; i++) {
      if (i) s += ',';
      add(s, "{\"day\":\"%s\",\"condition\":\"%s\",", tr(wx.fc_day[i]), tr(wmo_text(wx.fc_code[i])));
      num_json(s, "max", wx.fc_max[i], 0);
      s += ',';
      num_json(s, "min", wx.fc_min[i], 0);
      s += '}';
    }
    s += ']';
  }
  s += '}';
}

/* Everything the page shows, built in one piece (a few kB, in PSRAM) and sent
   with its length: no half-sent replies holding the board up */
static void handle_json() {
  uint32_t t0 = millis();
  if (!authorized()) {
    server.send(401, "application/json", "{\"error\":\"login required\"}");
    return;
  }
  take_snapshot();
  uint32_t now = millis();

  String s;
  s.reserve(8192);  // over 4 kB: comes from PSRAM, not the scarce internal RAM

  /* ---- page header: name, clock, version ---- */
  char name[24], tb[16], db[64], pass[33];
  get_page_name(name, sizeof(name));
  format_local_time(tb, db);  // in the display's language, like the rest of the page
  get_password(pass);
  add(s, "{\"name\":\"%s\",\"project\":\"esp32-S3-ws4-caravan\",\"version\":\"" FW_VERSION "\",\"time\":\"%s\",\"date\":\"%s\",",
      esc_json(name).c_str(), tb, esc_json(db).c_str());
  add(s, "\"lang\":\"%s\",", ui_lang);  // the page loads its texts from /lang when this changes
  add(s, "\"uptime_min\":%lu,\"logout\":%s,\"remote\":%s,\"has_location\":%s,", (unsigned long)(now / 60000),
      pass[0] ? "true" : "false", feat_remote ? "true" : "false", wx_has_loc ? "true" : "false");

  /* ---- battery: Victron monitor first, otherwise the BMS ---- */
  int bm = battery_monitor(now);
  bool bms_fresh = feat_bms && bms.valid && now - bms.updated < 30000;
  s += "\"battery\":{";
  if (bm >= 0) {
    const VicData &d = dat[bm];
    float p = (isnan(d.batt_v) || isnan(d.batt_i)) ? NAN : d.batt_v * d.batt_i;
    num_json(s, "soc", d.soc, 1);
    s += ',';
    num_json(s, "voltage", d.batt_v, 2);
    s += ',';
    num_json(s, "current", d.batt_i, 2);
    s += ',';
    num_json(s, "power", p, 0);
    add(s, ",\"ttg_min\":%d,\"charging\":%s,\"source\":\"victron\"", d.remaining_min,
        (!isnan(p) && p > 5) ? "true" : "false");
  } else if (bms_fresh) {
    add(s, "\"soc\":%d,\"voltage\":%.2f,", bms.soc, bms.volt);
    num_json(s, "current", bms.curr, 2);
    s += ',';
    num_json(s, "power", isnan(bms.curr) ? NAN : bms.volt * bms.curr, 0);
    add(s, ",\"charging\":%s,\"source\":\"bms\"", (!isnan(bms.curr) && bms.curr > 0.2f) ? "true" : "false");
  } else {
    s += "\"soc\":null";
  }
  s += "},";

  /* ---- solar ---- */
  float pv_sum = 0, yield_sum = 0;
  bool pv_any = false;
  for (int i = 0; i < MAX_VIC; i++) {
    if (cfg[i].type != VIC_SOLAR || !vic_ok(i, now)) continue;
    if (!isnan(dat[i].pv_w)) {
      pv_sum += dat[i].pv_w;
      pv_any = true;
    }
    if (!isnan(dat[i].yield_kwh)) yield_sum += dat[i].yield_kwh;
  }
  if (pv_any) add(s, "\"solar\":{\"power\":%.0f,\"yield_kwh\":%.2f},", pv_sum, yield_sum);
  else s += "\"solar\":null,";

  /* ---- Victron devices ---- */
  s += "\"victron\":[";
  bool first = true;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used) continue;
    if (!first) s += ',';
    first = false;
    bool ok = vic_ok(i, now);
    add(s, "{\"name\":\"%s\",\"type\":\"%s\",\"status\":\"%s\",", esc_json(cfg[i].name).c_str(),
        tr(vic_type_name(cfg[i].type)), tr(vic_status(dat[i], cfg[i].type, now)));
    num_json(s, "power", ok ? vic_power(i) : NAN, 0);
    add(s, ",\"charging\":%s", vic_charging(i, now) ? "true" : "false");
    if (ok && cfg[i].type == VIC_INVERTER) {
      const char *a = vic_alarm_text(dat[i].alarm);
      add(s, ",\"state\":\"%s\"", tr(a ? a : vic_state_name(dat[i].state)));
    } else if (ok && cfg[i].type != VIC_BATTMON) {
      add(s, ",\"state\":\"%s\"", tr(vic_state_name(dat[i].state)));
    }
    s += '}';
  }
  s += "],";

  /* ---- relays ---- */
  s += "\"relays\":[";
  if (feat_relays && relay_cfg.addr && pcf_ok) {
    for (int i = 0; i < relay_cfg.count; i++) {
      if (i) s += ',';
      add(s, "{\"name\":\"%s\",\"on\":%s}", esc_json(relay_cfg.names[i]).c_str(), ((relay_state >> i) & 1) ? "true" : "false");
    }
    s += "],";
  } else if (feat_relays && relay_cfg.addr) {
    char note[64];
    snprintf(note, sizeof(note), TR("No answer from PCF8574 at 0x%02X"), relay_cfg.addr);
    add(s, "],\"relays_note\":\"%s\",", esc_json(note).c_str());
  } else {
    s += "],";
  }

  /* ---- Shelly ---- */
  s += "\"shelly\":[";
  if (feat_shelly) {
    bool first_sh = true;
    for (int i = 0; i < MAX_SHELLY; i++) {
      if (!shelly_cfg[i].used) continue;
      if (!first_sh) s += ',';
      first_sh = false;
      add(s, "{\"idx\":%d,\"name\":\"%s\",\"on\":%s,\"valid\":%s,", i, esc_json(shelly_cfg[i].name).c_str(),
          sh[i].on ? "true" : "false", sh[i].valid ? "true" : "false");
      num_json(s, "power", sh[i].valid ? sh[i].power : NAN, 1);
      add(s, ",\"status\":\"%s\"}", esc_json(sh[i].status).c_str());
    }
  }
  s += "],";

  /* ---- schedules ---- */
  s += "\"schedules\":[";
  first = true;
  for (int i = 0; i < SCHED_COUNT; i++) {
    if (!schedule_target_exists(i) || !sched[i].enabled) continue;
    char st[64], days[32] = "";
    schedule_status(i, st, sizeof(st));
    if (sched[i].days == 0x7F) {
      strlcpy(days, TR("every day"), sizeof(days));
    } else {
      for (int d = 0; d < 7; d++) {
        if (!(sched[i].days & (1 << d))) continue;
        if (days[0]) strlcat(days, " ", sizeof(days));
        strlcat(days, tr_day_short(d), sizeof(days));
      }
    }
    if (!first) s += ',';
    first = false;
    add(s, "{\"idx\":%d,\"name\":\"%s\",\"status\":\"%s\",\"days\":\"%s\",\"on\":%s,\"overridden\":%s}", i,
        esc_json(schedule_target_name(i)).c_str(), esc_json(st).c_str(), esc_json(days).c_str(),
        schedule_target_on(i) ? "true" : "false", schedule_overridden(i) ? "true" : "false");
  }
  s += "],";

  /* ---- warnings and alarms ---- */
  s += "\"alarms\":[";
  {
    Alarm al[MAX_ALARMS];
    int aln = alarms_get(al, MAX_ALARMS);
    for (int i = 0; i < aln; i++) {
      if (i) s += ',';
      add(s, "{\"level\":%d,\"text\":\"%s\"}", al[i].level, esc_json(al[i].text).c_str());
    }
  }
  s += "],";

  /* ---- temperatures ---- */
  s += "\"temperatures\":[";
  first = true;
  for (int i = 0; i < MAX_RUUVI; i++) {
    if (!ruuvi_cfg[i].used) continue;
    if (!first) s += ',';
    first = false;
    add(s, "{\"name\":\"%s\",", esc_json(ruuvi_cfg[i].name).c_str());
    num_json(s, "temperature", ruuvi_temp(i, now), 1);
    s += '}';
  }
  s += "],";

  /* ---- the board's own battery ---- */
  {
    float lv;
    int lp;
    bool lc;
    if (board_battery(&lv, &lp, &lc)) {
      add(s, "\"board_battery\":{\"percent\":%d,", lp);
      num_json(s, "voltage", lv, 2);
      add(s, ",\"charging\":%s,\"on_battery\":%s},", lc ? "true" : "false", power_on_battery() ? "true" : "false");
    } else {
      s += "\"board_battery\":null,";
    }
  }

  json_weather(s);
  s += ',';
  json_history(s);
  s += '}';

  server.send(200, "application/json", s);
  uint32_t took = millis() - t0;
  if (took > 300) logf("Web: /json took %lu ms (%u bytes)", (unsigned long)took, (unsigned)s.length());
}

/* The log, as plain text: handy when no computer is attached to the USB port */
static void handle_log() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  String s;
  log_dump(s);
  if (!s.length()) s = TR("(log is empty)");
  server.send(200, "text/plain; charset=utf-8", s);
}

/* Files on the TF card: a list, or one file as a download */
static void handle_files() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  if (!sd_log_ok()) {
    server.send(200, "text/plain; charset=utf-8", TR("No card mounted (Settings -> SD card)"));
    return;
  }
  if (server.hasArg("name")) {
    String name = server.arg("name");
    if (name.indexOf("..") >= 0) {  // no climbing out of the root
      server.send(400, "text/plain", "Bad name");
      return;
    }
    if (!name.startsWith("/")) name = "/" + name;
    File f = sd_fs().open(name);
    if (!f || f.isDirectory()) {
      server.send(404, "text/plain", "Not found");
      return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name.substring(1) + "\"");
    server.streamFile(f, "text/plain");  // sent in pieces by the server, which also closes the file
    return;
  }

  String list;
  sd_list_files(list);
  String s;
  s.reserve(2048);
  s += FPSTR(PAGE_HEAD);
  char name[24];
  get_page_name(name, sizeof(name));
  add(s, "<title>%s</title></head><body><h1>%s</h1>", esc_html(name).c_str(), TR("Files on the card"));
  int start = 0;
  while (start < (int)list.length()) {
    int nl = list.indexOf('\n', start);
    if (nl < 0) nl = list.length();
    String line = list.substring(start, nl);
    start = nl + 1;
    int tab = line.indexOf('\t');
    if (tab < 0) continue;
    String fname = line.substring(0, tab);
    long size = line.substring(tab + 1).toInt();
    add(s, "<div class='row'><span><a href='/files?name=%s'>%s</a></span><span class='sub'>%ld kB</span></div>",
        esc_html(fname.c_str()).c_str(), esc_html(fname.c_str()).c_str(), size / 1024);
  }
  add(s, "<p class='sub'><a href='/'>%s</a></p></body></html>", TR("back"));
  server.send(200, "text/html; charset=utf-8", s);
}

/* Switching from the web page: only with "Remote admin" enabled, and logged in.
   The page sends ajax=1 and gets a short answer; a plain form is sent back to /. */
static void switch_done() {
  if (server.hasArg("ajax")) server.send(204, "text/plain", "");
  else redirect("/");
}

static void handle_switch() {
  if (!authorized()) {
    if (server.hasArg("ajax")) server.send(401, "text/plain", "login required");
    else redirect("/login");
    return;
  }
  if (!feat_remote) {
    server.send(403, "text/plain", "Remote admin is switched off");
    return;
  }
  bool on = server.arg("on") == "1";

  if (server.hasArg("sched")) {  // an override, or back on schedule
    int i = server.arg("sched").toInt();
    if (i >= 0 && i < SCHED_COUNT) {
      if (server.hasArg("resume")) schedule_resume(i);
      else schedule_override(i, on);
    }
    switch_done();
    return;
  }

  if (server.hasArg("relay")) {
    int i = server.arg("relay").toInt();
    if (feat_relays && relay_cfg.addr && i >= 0 && i < relay_cfg.count) {
      schedule_override(i, on);  // holds until the next scheduled change
      logf("Web: relay %d -> %s", i + 1, on ? "ON" : "OFF");
    }
  } else if (server.hasArg("shelly")) {
    int i = server.arg("shelly").toInt();
    if (feat_shelly && i >= 0 && i < MAX_SHELLY && shelly_cfg[i].used) {
      schedule_override(MAX_RELAYS + i, on);
      logf("Web: Shelly %d -> %s", i + 1, on ? "ON" : "OFF");
    }
  }
  switch_done();
}

/* ---------- start, stop, serve ---------- */
static void add_routes() {
  server.on("/", handle_root);
  server.on("/json", handle_json);
  server.on("/lang", handle_lang);
  server.on("/login", handle_login);
  server.on("/logout", handle_logout);
  server.on("/switch", handle_switch);
  server.on("/log", handle_log);
  server.on("/files", handle_files);
}

void web_service() {
  if (!feat_web) {  // switched off in Settings
    if (started) {
      server.stop();
      MDNS.end();
      started = false;
      logf("Web server stopped");
    }
    return;
  }
  if (!started) {
    if (WiFi.status() != WL_CONNECTED) return;  // start once WiFi is up
    static bool routes = false;
    if (!routes) {
      routes = true;
      new_token();
      add_routes();
    }
    if (!server.begin(WEB_PORT)) {
      static uint32_t next_msg = 0;
      if ((int32_t)(millis() - next_msg) >= 0) {
        log_fault("Web server: could not open port 80 - retrying");
        next_msg = millis() + 60000;
      }
      return;
    }
    started = true;
    char host[32];
    web_host_name(host, sizeof(host));
    if (MDNS.begin(host)) MDNS.addService("http", "tcp", 80);
    logf("Web server: http://%s.local/ or http://%s/", host, WiFi.localIP().toString().c_str());
    host_changed = false;
    return;
  }
  if (host_changed) {  // the page was renamed: answer to the new .local name
    host_changed = false;
    char host[32];
    web_host_name(host, sizeof(host));
    MDNS.end();
    if (MDNS.begin(host)) MDNS.addService("http", "tcp", 80);
    logf("Web server: renamed, now http://%s.local/", host);
  }
  server.handleClient();
}
