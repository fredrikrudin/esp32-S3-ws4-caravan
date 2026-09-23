/* Minimal web server (port 80), read-only.
 *   /        HTML page: battery SOC, Victron devices (what is connected and charging), relays.
 *            Refreshes itself every 5 seconds.
 *   /json    The same data as JSON, for scripts or Home Assistant (?key=<password> if a password is set).
 *   /login   Password-only login, when a password is set under Settings -> Web page.
 *   /logout
 * Reachable at http://waveshare.local/ (mDNS) or the board's IP address.
 * Starts as soon as WiFi is connected. Runs from loop(), so it never races the UI.
 *
 * Note: plain HTTP, so the password travels unencrypted on the local network.
 * It keeps casual visitors on the same WiFi out; it is not strong security. */
#include "app.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

static WebServer server(80);
static bool started = false;

/* ---------- login ---------- */
static char token[33] = "";     // session token in the login cookie; new after boot or password change
static uint32_t last_fail = 0;  // wrong password: next attempt allowed after 2 s

static void new_token() {
  for (int i = 0; i < 16; i++) sprintf(token + i * 2, "%02x", (unsigned)(esp_random() & 0xFF));
}

void web_password_changed() {
  new_token();  // everyone logged in has to log in again
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
  "<title>Caravan</title><style>"
  "body{font-family:sans-serif;background:#15171a;color:#e0e0e0;margin:0;padding:16px;max-width:520px}"
  "h1{font-size:20px;margin:0 0 12px}h2{font-size:15px;color:#2196f3;margin:20px 0 6px}"
  ".soc{font-size:56px;font-weight:bold}.sub{color:#9e9e9e;font-size:13px}"
  ".row{display:flex;justify-content:space-between;padding:8px 10px;background:#282b30;border-radius:6px;margin:4px 0}"
  ".on{color:#4caf50;font-weight:bold}.off{color:#9e9e9e}.chg{color:#f39c12;font-weight:bold}"
  ".bar{height:10px;background:#1f5f8b;border-radius:5px;overflow:hidden;margin:6px 0}"
  ".bar div{height:100%;background:#3498db}a{color:#2196f3}"
  "input,button{font-size:16px;padding:10px;border-radius:6px;border:1px solid #3a3f44}"
  "input{background:#282b30;color:#e0e0e0;width:100%;box-sizing:border-box;margin:8px 0}"
  "button{background:#2196f3;color:#fff;border:0;width:100%}.err{color:#f39c12}"
  "</style>";

static void send_login(const char *msg) {
  String s;
  s.reserve(1800);
  s += FPSTR(PAGE_HEAD);
  s += F("</head><body><h1>Caravan</h1><form method='post' action='/login'>"
         "<input type='password' name='p' placeholder='Password' autofocus>"
         "<button>Log in</button></form>");
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
  if (server.method() != HTTP_POST) {
    send_login("");
    return;
  }
  if (last_fail && millis() - last_fail < 2000) {
    send_login("Too many attempts - wait a moment");
    return;
  }
  char pass[33];
  get_password(pass);
  if (!pass[0] || server.arg("p") == pass) {
    server.sendHeader("Set-Cookie", String("caravan=") + token + "; Path=/; HttpOnly; Max-Age=2592000");
    redirect("/");
  } else {
    last_fail = millis() | 1;
    send_login("Wrong password");
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

static void take_snapshot() {
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  memcpy(cfg, vic_cfg, sizeof(cfg));
  memcpy(dat, vic_data, sizeof(dat));
  xSemaphoreGive(vic_mtx);
  bms_get(&bms);
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

/* ---------- HTML page ---------- */
static void handle_root() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  take_snapshot();
  uint32_t now = millis();
  String s;
  s.reserve(4096);

  s += FPSTR(PAGE_HEAD);
  s += F("<meta http-equiv='refresh' content='5'></head><body><h1>Caravan</h1>");

  /* ---- battery ---- */
  s += F("<h2>Battery</h2>");
  int bm = battery_monitor(now);
  bool bms_fresh = bms.valid && now - bms.updated < 30000;
  if (bm >= 0 && !isnan(dat[bm].soc)) {
    const VicData &d = dat[bm];
    add(s, "<div class='soc'>%.0f%%</div><div class='bar'><div style='width:%.0f%%'></div></div>", d.soc, d.soc);
    add(s, "<div>%.2f V &middot; %+.1f A &middot; %+.0f W", d.batt_v, d.batt_i, d.batt_v * d.batt_i);
    if (!isnan(d.batt_i) && d.batt_i > 0.05f) s += F(" &middot; <span class='chg'>Charging</span>");
    else if (d.remaining_min >= 0) add(s, " &middot; %dh %02dm left", d.remaining_min / 60, d.remaining_min % 60);
    add(s, "</div><div class='sub'>%s</div>", esc_html(cfg[bm].name).c_str());
  } else if (bms_fresh) {
    add(s, "<div class='soc'>%d%%</div><div class='bar'><div style='width:%d%%'></div></div>", bms.soc, bms.soc);
    if (!isnan(bms.curr)) add(s, "<div>%.2f V &middot; %+.1f A &middot; %+.0f W</div>", bms.volt, bms.curr, bms.volt * bms.curr);
    else add(s, "<div>%.2f V &middot; %.1f / %.0f Ah</div>", bms.volt, bms.remain_ah, bms.nominal_ah);
    s += F("<div class='sub'>Battery BMS</div>");
  } else {
    s += F("<div class='sub'>No battery data</div>");
  }

  /* ---- Victron devices ---- */
  s += F("<h2>Victron devices</h2>");
  bool any = false;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used) continue;
    any = true;
    const char *st = vic_status(dat[i], cfg[i].type, now);
    bool ok = vic_ok(i, now);
    add(s, "<div class='row'><span>%s<br><span class='sub'>%s</span></span><span style='text-align:right'>",
        esc_html(cfg[i].name).c_str(), vic_type_name(cfg[i].type));
    if (!ok) {
      add(s, "<span class='off'>%s</span>", st);
    } else {
      float p = vic_power(i);
      if (!isnan(p)) add(s, "%.0f %s<br>", p, cfg[i].type == VIC_INVERTER ? "VA" : "W");
      if (vic_charging(i, now)) s += F("<span class='chg'>&#9889; Charging</span>");
      else if (cfg[i].type == VIC_INVERTER) {
        const char *alarm = vic_alarm_text(dat[i].alarm);
        add(s, "<span class='sub'>%s</span>", alarm ? alarm : vic_state_name(dat[i].state));
      } else if (cfg[i].type != VIC_BATTMON) {
        add(s, "<span class='sub'>%s</span>", vic_state_name(dat[i].state));
      }
    }
    s += F("</span></div>");
  }
  if (!any) s += F("<div class='sub'>No Victron devices added</div>");

  /* ---- relays ---- */
  s += F("<h2>Relays</h2>");
  if (!relay_cfg.addr) {
    s += F("<div class='sub'>No relay board set up</div>");
  } else if (!pcf_ok) {
    add(s, "<div class='sub'>No answer from PCF8574 at 0x%02X</div>", relay_cfg.addr);
  } else {
    for (int i = 0; i < relay_cfg.count; i++) {
      bool on = (relay_state >> i) & 1;
      add(s, "<div class='row'><span>%s</span><span class='%s'>%s</span></div>",
          esc_html(relay_cfg.names[i]).c_str(), on ? "on" : "off", on ? "ON" : "OFF");
    }
  }

  char pass[33];
  get_password(pass);
  add(s, "<p class='sub'>Updated every 5 s &middot; up %lu min%s</p></body></html>", (unsigned long)(now / 60000),
      pass[0] ? " &middot; <a href='/logout'>Log out</a>" : "");
  server.send(200, "text/html; charset=utf-8", s);
}

/* ---------- JSON ---------- */
static void handle_json() {
  if (!authorized()) {
    server.send(401, "application/json", "{\"error\":\"login required\"}");
    return;
  }
  take_snapshot();
  uint32_t now = millis();
  String s;
  s.reserve(2048);

  int bm = battery_monitor(now);
  bool bms_fresh = bms.valid && now - bms.updated < 30000;
  s += "{\"battery\":{";
  if (bm >= 0) {
    const VicData &d = dat[bm];
    num_json(s, "soc", d.soc, 1);
    s += ',';
    num_json(s, "voltage", d.batt_v, 2);
    s += ',';
    num_json(s, "current", d.batt_i, 2);
    s += ',';
    num_json(s, "power", (isnan(d.batt_v) || isnan(d.batt_i)) ? NAN : d.batt_v * d.batt_i, 0);
    add(s, ",\"source\":\"victron\"");
  } else if (bms_fresh) {
    add(s, "\"soc\":%d,\"voltage\":%.2f,", bms.soc, bms.volt);
    num_json(s, "current", bms.curr, 2);
    s += ',';
    num_json(s, "power", isnan(bms.curr) ? NAN : bms.volt * bms.curr, 0);
    add(s, ",\"source\":\"bms\"");
  } else {
    s += "\"soc\":null";
  }
  s += "},\"victron\":[";

  bool first = true;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used) continue;
    if (!first) s += ',';
    first = false;
    bool ok = vic_ok(i, now);
    add(s, "{\"name\":\"%s\",\"type\":\"%s\",\"status\":\"%s\",", esc_json(cfg[i].name).c_str(),
        vic_type_name(cfg[i].type), vic_status(dat[i], cfg[i].type, now));
    num_json(s, "power", ok ? vic_power(i) : NAN, 0);
    add(s, ",\"charging\":%s", vic_charging(i, now) ? "true" : "false");
    if (ok && cfg[i].type != VIC_BATTMON) add(s, ",\"state\":\"%s\"", vic_state_name(dat[i].state));
    s += '}';
  }
  s += "],\"relays\":[";

  if (relay_cfg.addr && pcf_ok) {
    for (int i = 0; i < relay_cfg.count; i++) {
      if (i) s += ',';
      add(s, "{\"name\":\"%s\",\"on\":%s}", esc_json(relay_cfg.names[i]).c_str(), ((relay_state >> i) & 1) ? "true" : "false");
    }
  }
  s += "]}";
  server.send(200, "application/json", s);
}

void web_service() {
  if (!started) {
    if (WiFi.status() != WL_CONNECTED) return;  // start once WiFi is up
    new_token();
    const char *headers[] = { "Cookie" };
    server.collectHeaders(headers, 1);
    server.on("/", handle_root);
    server.on("/json", handle_json);
    server.on("/login", handle_login);
    server.on("/logout", handle_logout);
    server.onNotFound([]() {
      server.send(404, "text/plain", "Not found");
    });
    server.begin();
    started = true;
    if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
    USBSerial.printf("Web server: http://%s.local/ or http://%s/\n", MDNS_NAME, WiFi.localIP().toString().c_str());
    return;
  }
  server.handleClient();
}
