/* Minimal web server (port 80), read-only.
 *   /        HTML page: battery SOC, Victron devices (what is connected and charging), relays.
 *            Refreshes itself every 5 seconds.
 *   /json    The same data as JSON, for scripts or Home Assistant (?key=<password> if a password is set).
 *   /login   Password-only login, when a password is set under Settings -> Web page.
 *   /logout
 *   /switch  POST, only with "Remote admin" enabled in Settings: switches a relay or Shelly
 *   /log     the most recent log lines as plain text (same log as the Serial Monitor)
 *   /files   list of files on the TF card; /files?name=... downloads one
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

static void get_page_name(char *out, size_t n) {
  LOCK();
  strlcpy(out, g.web_name, n);
  UNLOCK();
  if (!out[0]) strlcpy(out, "Waveshare", n);
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
  "body{font-family:sans-serif;background:#15171a;color:#e0e0e0;margin:0;padding:16px;max-width:520px}"
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
  add(s, "<title>%s</title></head><body><h1>%s</h1>", esc_html(name).c_str(), esc_html(name).c_str());
  s += F("<form method='post' action='/login'>"
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



/* ---------- HTML page ---------- */
static void handle_root() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  take_snapshot();
  uint32_t now = millis();
  String s;
  s.reserve(8192);  // the page has grown: clock, weather, Victron, relays, Shelly

  char page_name[24];
  get_page_name(page_name, sizeof(page_name));
  s += FPSTR(PAGE_HEAD);
  add(s, "<meta http-equiv='refresh' content='5'><title>%s</title></head><body><h1>%s</h1>",
      esc_html(page_name).c_str(), esc_html(page_name).c_str());

  /* ================= start page: clock with gauges ================= */
  int bm = battery_monitor(now);
  bool bms_fresh = feat_bms && bms.valid && now - bms.updated < 30000;
  float soc = NAN, batt_v = NAN, batt_w = NAN;
  int ttg = -1;
  if (bm >= 0 && !isnan(dat[bm].soc)) {
    soc = dat[bm].soc;
    batt_v = dat[bm].batt_v;
    if (!isnan(dat[bm].batt_v) && !isnan(dat[bm].batt_i)) batt_w = dat[bm].batt_v * dat[bm].batt_i;
    ttg = dat[bm].remaining_min;
  } else if (bms_fresh) {
    soc = bms.soc;
    batt_v = bms.volt;
    if (!isnan(bms.curr)) batt_w = bms.volt * bms.curr;
  }

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
  static float pv_scale = 400;
  if (pv_sum > pv_scale) pv_scale = pv_sum;

  char clock_txt[16], date_txt[64];
  format_local_time(clock_txt, date_txt);

  s += F("<div class='hero'>");
  add(s, "<div class='g %s'><i style='height:%.0f%%'></i></div>", (!isnan(soc) && soc < 20) ? "gl" : "gb", isnan(soc) ? 0.0f : soc);
  s += F("<div class='mid'>");
  if (!isnan(batt_w) && batt_w > 5) s += F("<div style='color:#2ecc71;font-size:14px'>&#9889; Charging</div>");
  else s += F("<div style='font-size:14px'>&nbsp;</div>");
  add(s, "<div class='clock'>%s</div><div class='sub'>%s</div>", clock_txt, date_txt);
  s += F("</div>");
  add(s, "<div class='g gs'><i style='height:%.0f%%'></i></div>", pv_any ? pv_sum / pv_scale * 100 : 0.0f);
  s += F("</div><div class='vals'><div>");

  if (isnan(soc)) s += F("<b>--</b><br><span class='sub'>No battery data</span>");
  else {
    add(s, "<b class='big'>%.0f%%</b>", soc);
    if (!isnan(batt_v)) {
      if (!isnan(batt_w)) add(s, "<br><span class='sub'>%.2f V &middot; %+.0f W</span>", batt_v, batt_w);
      else add(s, "<br><span class='sub'>%.2f V</span>", batt_v);
    }
    if (ttg >= 0 && !isnan(batt_w) && batt_w < 0) add(s, "<br><span class='sub'>%dh %02dm left</span>", ttg / 60, ttg % 60);
  }
  s += F("</div><div style='text-align:right'>");
  if (!pv_any) s += F("<b>--</b><br><span class='sub'>No solar data</span>");
  else add(s, "<b class='big'>%.0f W</b><br><span class='sub'>Solar &middot; today %.2f kWh</span>", pv_sum, yield_sum);
  s += F("</div></div>");

  /* temperatures and relays in one strip */
  s += F("<div class='strip'>");
  for (int i = 0; i < MAX_RUUVI; i++) {
    float t = ruuvi_temp(i, now);
    if (!ruuvi_cfg[i].used) continue;
    if (isnan(t)) add(s, "<div><span class='sub'>%s</span><br>--</div>", esc_html(ruuvi_cfg[i].name).c_str());
    else add(s, "<div><span class='sub'>%s</span><br><b>%.1f&deg;C</b></div>", esc_html(ruuvi_cfg[i].name).c_str(), t);
  }
  if (wx.valid) add(s, "<div><span class='sub'>Outside</span><br><b>%.1f&deg;C</b></div>", wx.temp);
  if (feat_relays && relay_cfg.addr) {
    int on = 0;
    for (int i = 0; i < relay_cfg.count; i++) on += (relay_state >> i) & 1;
    add(s, "<div><span class='sub'>Relays</span><br><b>%d/%d</b></div>", on, relay_cfg.count);
  }
  s += F("</div>");

  /* ================= weather ================= */
  s += F("<h2>Weather</h2>");
  if (!wx.valid) {
    s += wx_has_loc ? F("<div class='sub'>Loading weather...</div>") : F("<div class='sub'>No location set</div>");
  } else {
    add(s, "<div class='row'><span><b class='big'>%.1f&deg;C</b><br><span class='sub'>%s</span></span>"
           "<span style='text-align:right'>H %.0f&deg; &middot; L %.0f&deg;<br><span class='sub'>Feels %.1f&deg; &middot; %d%% &middot; %.1f m/s</span></span></div>",
        wx.temp, wmo_text(wx.code), wx.today_max, wx.today_min, wx.feels, wx.humidity, wx.wind);
    s += F("<div class='fc'>");
    for (int i = 0; i < 3; i++)
      add(s, "<div><b>%s</b><br><span class='sub'>%s</span><br>%.0f&deg; / %.0f&deg;</div>",
          wx.fc_day[i], wmo_text(wx.fc_code[i]), wx.fc_max[i], wx.fc_min[i]);
    s += F("</div>");
  }

  /* ================= Victron ================= */
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

  /* ================= relays ================= */
  s += F("<h2>Relays</h2>");
  if (!feat_relays) {
    s += F("<div class='sub'>Switched off</div>");
  } else if (!relay_cfg.addr) {
    s += F("<div class='sub'>No relay board set up</div>");
  } else if (!pcf_ok) {
    add(s, "<div class='sub'>No answer from PCF8574 at 0x%02X</div>", relay_cfg.addr);
  } else {
    for (int i = 0; i < relay_cfg.count; i++) {
      bool on = (relay_state >> i) & 1;
      add(s, "<div class='row'><span>%s</span><span>", esc_html(relay_cfg.names[i]).c_str());
      if (feat_remote)
        add(s, "<form class='sw' method='post' action='/switch'><input type='hidden' name='relay' value='%d'>"
               "<div class='seg'><button name='on' value='0' class='%s'>Off</button>"
               "<button name='on' value='1' class='%s'>On</button></div></form>",
            i, on ? "" : "act", on ? "act" : "");
      else
        add(s, "<div class='seg'><span class='%s'>Off</span><span class='%s'>On</span></div>",
            on ? "" : "act", on ? "act" : "");
      s += F("</span></div>");
    }
  }

  /* ================= Shelly ================= */
  s += F("<h2>Shelly</h2>");
  if (!feat_shelly) {
    s += F("<div class='sub'>Switched off</div>");
  } else {
    bool any_sh = false;
    for (int i = 0; i < MAX_SHELLY; i++) {
      if (!shelly_cfg[i].used) continue;
      any_sh = true;
      add(s, "<div class='row'><span>%s<br><span class='sub'>%s</span></span><span style='text-align:right'>",
          esc_html(shelly_cfg[i].name).c_str(), sh[i].status);
      if (!isnan(sh[i].power)) add(s, "%.1f W<br>", sh[i].power);
      if (feat_remote && sh[i].valid)
        add(s, "<form class='sw' method='post' action='/switch'><input type='hidden' name='shelly' value='%d'>"
               "<div class='seg'><button name='on' value='0' class='%s'>Off</button>"
               "<button name='on' value='1' class='%s'>On</button></div></form>",
            i, sh[i].on ? "" : "act", sh[i].on ? "act" : "");
      else if (sh[i].valid)
        add(s, "<div class='seg'><span class='%s'>Off</span><span class='%s'>On</span></div>",
            sh[i].on ? "" : "act", sh[i].on ? "act" : "");
      else
        s += F("<span class='off'>--</span>");
      s += F("</span></div>");
    }
    if (!any_sh) s += F("<div class='sub'>No Shelly devices paired</div>");
  }

  char pass[33];
  get_password(pass);
  add(s, "<p class='sub'>Updated every 5 s &middot; up %lu min &middot; <a href='/log'>log</a> &middot; <a href='/files'>files</a>%s</p>"
         "<p class='sub'>&copy; %s %s Fredrik Rudin &middot; "
         "<a href='https://github.com/fredrikrudin/esp32-S3-ws4-caravan'>github.com/fredrikrudin/esp32-S3-ws4-caravan</a><br>"
         "med hj&auml;lp av claude.ai Opus 5</p></body></html>",
      (unsigned long)(now / 60000), pass[0] ? " &middot; <a href='/logout'>Log out</a>" : "", __DATE__, __TIME__);
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
  s.reserve(3072);

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

  if (feat_relays && relay_cfg.addr && pcf_ok) {
    for (int i = 0; i < relay_cfg.count; i++) {
      if (i) s += ',';
      add(s, "{\"name\":\"%s\",\"on\":%s}", esc_json(relay_cfg.names[i]).c_str(), ((relay_state >> i) & 1) ? "true" : "false");
    }
  }
  s += "],\"shelly\":[";

  if (feat_shelly) {
    bool first_sh = true;
    for (int i = 0; i < MAX_SHELLY; i++) {
      if (!shelly_cfg[i].used) continue;
      if (!first_sh) s += ',';
      first_sh = false;
      add(s, "{\"name\":\"%s\",\"on\":%s,", esc_json(shelly_cfg[i].name).c_str(), sh[i].on ? "true" : "false");
      num_json(s, "power", sh[i].valid ? sh[i].power : NAN, 1);
      add(s, ",\"status\":\"%s\"}", esc_json(sh[i].status).c_str());
    }
  }
  s += "],\"temperatures\":[";

  bool first_t = true;
  for (int i = 0; i < MAX_RUUVI; i++) {
    if (!ruuvi_cfg[i].used) continue;
    if (!first_t) s += ',';
    first_t = false;
    add(s, "{\"name\":\"%s\",", esc_json(ruuvi_cfg[i].name).c_str());
    num_json(s, "temperature", ruuvi_temp(i, now), 1);
    s += '}';
  }
  s += "],\"weather\":{";
  if (wx.valid) {
    num_json(s, "temperature", wx.temp, 1);
    s += ',';
    num_json(s, "feels_like", wx.feels, 1);
    s += ',';
    add(s, "\"humidity\":%d,", wx.humidity);
    num_json(s, "wind", wx.wind, 1);
    add(s, ",\"condition\":\"%s\",", wmo_text(wx.code));
    num_json(s, "today_max", wx.today_max, 0);
    s += ',';
    num_json(s, "today_min", wx.today_min, 0);
    s += ",\"forecast\":[";
    for (int i = 0; i < 3; i++) {
      if (i) s += ',';
      add(s, "{\"day\":\"%s\",\"condition\":\"%s\",", wx.fc_day[i], wmo_text(wx.fc_code[i]));
      num_json(s, "max", wx.fc_max[i], 0);
      s += ',';
      num_json(s, "min", wx.fc_min[i], 0);
      s += '}';
    }
    s += ']';
  }
  s += "}}";
  server.send(200, "application/json", s);
}

/* The log, as plain text: handy when no computer is attached to the USB port */
static void handle_log() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  String s;
  log_dump(s);
  if (!s.length()) s = "(log is empty)";
  server.send(200, "text/plain; charset=utf-8", s);
}

/* Files on the TF card: a list, or one file as a download */
static void handle_files() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  if (!sd_log_ok()) {
    server.send(200, "text/plain", "No card mounted (Settings -> SD card)");
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
    server.streamFile(f, "text/plain");
    f.close();
    return;
  }

  String list;
  sd_list_files(list);
  String s;
  s.reserve(1024);
  s += FPSTR(PAGE_HEAD);
  char name[24];
  get_page_name(name, sizeof(name));
  add(s, "<title>%s</title></head><body><h1>Files on the card</h1>", esc_html(name).c_str());
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
  s += F("<p class='sub'><a href='/'>back</a></p></body></html>");
  server.send(200, "text/html; charset=utf-8", s);
}

/* Switching from the web page: only with "Remote admin" enabled, and logged in */
static void handle_switch() {
  if (!authorized()) {
    redirect("/login");
    return;
  }
  if (!feat_remote) {
    server.send(403, "text/plain", "Remote admin is switched off");
    return;
  }
  bool on = server.arg("on") == "1";

  if (server.hasArg("relay")) {
    int i = server.arg("relay").toInt();
    if (feat_relays && relay_cfg.addr && i >= 0 && i < relay_cfg.count) {
      uint8_t old = relay_state;
      if (on) relay_state |= (1 << i);
      else relay_state &= ~(1 << i);
      if (!relay_apply()) relay_state = old;  // write failed: keep the real state
      USBSerial.printf("Web: relay %d -> %s\n", i + 1, on ? "ON" : "OFF");
    }
  } else if (server.hasArg("shelly")) {
    int i = server.arg("shelly").toInt();
    if (feat_shelly && i >= 0 && i < MAX_SHELLY && shelly_cfg[i].used) {
      shelly_set(i, on);
      USBSerial.printf("Web: Shelly %d -> %s\n", i + 1, on ? "ON" : "OFF");
    }
  }
  redirect("/");
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
    server.on("/switch", handle_switch);
    server.on("/log", handle_log);
    server.on("/files", handle_files);
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
