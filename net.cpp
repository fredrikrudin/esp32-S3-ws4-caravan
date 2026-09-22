/* Network task (core 0, never touches LVGL): WiFi, NTP, Open-Meteo weather.
   It also does all flash writes the UI asks for, so they happen in one place. */
#include "app.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/* Plain HTTP for public data (Open-Meteo): HTTPS needs 40+ KB of internal RAM,
   which isn't available with WiFi, BLE and the display running. */
static int http_get(const String &url, String &body) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return -100;
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    body = http.getString();
  } else {
    USBSerial.printf("HTTP %d (%s) for %s\n", code, HTTPClient::errorToString(code).c_str(), url.c_str());
    USBSerial.printf("Internal heap free %u, largest block %u\n",
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  }
  http.end();
  return code;
}

static void do_scan() {
  set_wifi_status("Scanning...");
  int n = WiFi.scanNetworks();

  char saved[33];
  LOCK();
  strlcpy(saved, g.ssid, sizeof(saved));
  UNLOCK();

  char list[1024] = "";
  size_t len = 0;
  int count = 0, sel = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;  // hidden network
    bool dup = false;
    for (int j = 0; j < i; j++) {
      if (WiFi.SSID(j) == s) {
        dup = true;
        break;
      }
    }
    if (dup) continue;
    if (len + s.length() + 2 >= sizeof(list)) break;
    if (count) list[len++] = '\n';
    memcpy(list + len, s.c_str(), s.length());
    len += s.length();
    list[len] = 0;
    if (s == saved) sel = count;
    count++;
  }
  WiFi.scanDelete();

  LOCK();
  if (count) strlcpy(g.scan_list, list, sizeof(g.scan_list));
  else strlcpy(g.scan_list, NO_NET_FOUND, sizeof(g.scan_list));
  g.scan_sel = sel;
  g.scan_ready = true;
  UNLOCK();

  if (WiFi.status() == WL_CONNECTED)
    set_wifi_status("Connected to %s\nIP %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  else
    set_wifi_status("Found %d networks", count);
}

static void do_geocode() {
  char city[64];
  LOCK();
  strlcpy(city, g.city, sizeof(city));
  UNLOCK();
  if (!city[0]) return;

  /* "City, Country" -> search for "City", then pick the result in "Country" */
  char name[64], country[64] = "";
  strlcpy(name, city, sizeof(name));
  char *comma = strchr(name, ',');
  if (comma) {
    *comma = 0;
    const char *c = comma + 1;
    while (*c == ' ') c++;
    strlcpy(country, c, sizeof(country));
  }
  char *start = name;
  while (*start == ' ') start++;
  memmove(name, start, strlen(start) + 1);
  for (int i = (int)strlen(name) - 1; i >= 0 && name[i] == ' '; i--) name[i] = 0;
  for (int i = (int)strlen(country) - 1; i >= 0 && country[i] == ' '; i--) country[i] = 0;
  if (!name[0]) return;

  USBSerial.printf("Geocode: name='%s' country='%s'\n", name, country);
  set_loc_status("Looking up %s...", city);

  String url = String("http://geocoding-api.open-meteo.com/v1/search?count=10&language=en&format=json&name=") + url_encode(name);
  String body;
  int code = http_get(url, body);
  if (code != HTTP_CODE_OK) {
    set_loc_status("Lookup failed (error %d) - try again", code);
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    USBSerial.printf("Geocode JSON error: %s\n", err.c_str());
    set_loc_status("Lookup failed (bad response)");
    return;
  }
  JsonArray results = doc["results"].as<JsonArray>();
  USBSerial.printf("Geocode: %u results\n", (unsigned)results.size());

  JsonObject r;
  for (JsonObject c : results) {
    if (!country[0] || !strcasecmp(c["country"] | "", country) || !strcasecmp(c["country_code"] | "", country)) {
      r = c;
      break;
    }
  }
  if (r.isNull()) {
    if (country[0]) set_loc_status("Not found: %s in %s", name, country);
    else set_loc_status("Not found: %s", name);
    return;
  }

  char place[96];
  snprintf(place, sizeof(place), "%s, %s", r["name"] | "?", r["country"] | "");
  float lat = r["latitude"].as<float>();
  float lon = r["longitude"].as<float>();

  LOCK();
  strlcpy(g.place, place, sizeof(g.place));
  g.lat = lat;
  g.lon = lon;
  g.has_loc = true;
  UNLOCK();

  prefs.putString("city", city);
  prefs.putString("place", place);
  prefs.putFloat("lat", lat);
  prefs.putFloat("lon", lon);
  prefs.putBool("hasloc", true);

  set_loc_status("Location: %s", place);
  cmd_weather = true;
}

static bool do_weather() {
  float lat, lon;
  LOCK();
  lat = g.lat;
  lon = g.lon;
  UNLOCK();

  char url[400];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&timezone=auto&forecast_days=4&wind_speed_unit=ms",
           lat, lon);

  String body;
  if (http_get(url, body) != HTTP_CODE_OK) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  JsonObject cur = doc["current"];
  JsonObject d = doc["daily"];
  if (cur.isNull() || d.isNull()) return false;

  Weather w;
  w.temp = cur["temperature_2m"].as<float>();
  w.feels = cur["apparent_temperature"].as<float>();
  w.humidity = cur["relative_humidity_2m"].as<int>();
  w.wind = cur["wind_speed_10m"].as<float>();
  w.code = cur["weather_code"].as<int>();
  w.today_max = d["temperature_2m_max"][0].as<float>();
  w.today_min = d["temperature_2m_min"][0].as<float>();

  for (int i = 0; i < 3; i++) {
    const char *date = d["time"][i + 1] | "";
    int yy, mm, dd;
    if (sscanf(date, "%d-%d-%d", &yy, &mm, &dd) == 3) {
      struct tm tm = {};
      tm.tm_year = yy - 1900;
      tm.tm_mon = mm - 1;
      tm.tm_mday = dd;
      tm.tm_hour = 12;
      mktime(&tm);  // fills in tm_wday
      strftime(w.fc_day[i], sizeof(w.fc_day[i]), "%a", &tm);
    } else {
      strlcpy(w.fc_day[i], "?", sizeof(w.fc_day[i]));
    }
    w.fc_code[i] = d["weather_code"][i + 1].as<int>();
    w.fc_max[i] = d["temperature_2m_max"][i + 1].as<float>();
    w.fc_min[i] = d["temperature_2m_min"][i + 1].as<float>();
  }
  w.fetched = time(nullptr);
  w.valid = true;

  int32_t off = doc["utc_offset_seconds"] | 0;

  LOCK();
  g.weather = w;
  g.weather_changed = true;
  bool off_changed = !g.offset_valid || g.utc_offset != off;
  g.utc_offset = off;
  g.offset_valid = true;
  UNLOCK();

  if (off_changed) prefs.putInt("utcoff", off);
  return true;
}

/* Flash writes requested by the UI */
static void save_pending() {
  if (cmd_save_ruuvi) {
    cmd_save_ruuvi = false;
    RuuviCfg copy[MAX_RUUVI];
    LOCK();
    memcpy(copy, ruuvi_cfg, sizeof(copy));
    UNLOCK();
    prefs.putBytes("ruuvis", copy, sizeof(copy));
  }
  if (cmd_save_relay) {
    cmd_save_relay = false;
    RelayCfg copy;
    LOCK();
    copy = relay_cfg;
    UNLOCK();
    prefs.putBytes("relay", &copy, sizeof(copy));
  }
  if (cmd_save_bl) {
    cmd_save_bl = false;
    prefs.putUChar("bl", bl_normal);
    prefs.putUChar("blsaver", bl_saver);
  }
  if (cmd_save_scan) {
    cmd_save_scan = false;
    prefs.putUChar("scanint", scan_interval_s);
  }
  if (cmd_save_vic) {
    cmd_save_vic = false;
    static VicCfg copy[MAX_VIC];
    xSemaphoreTake(vic_mtx, portMAX_DELAY);
    memcpy(copy, vic_cfg, sizeof(copy));
    xSemaphoreGive(vic_mtx);
    prefs.putBytes("victron", copy, sizeof(copy));
  }
}

static void net_task(void *arg) {
  WiFi.persistent(false);  // we store credentials ourselves; avoid extra flash writes
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  bool was_up = false, ntp_started = false;
  uint32_t connect_started = 0, next_weather = 0;

  LOCK();
  bool have_ssid = g.ssid[0] != 0;
  UNLOCK();
  if (have_ssid) cmd_connect = true;  // auto-connect with saved credentials

  for (;;) {
    if (cmd_scan) {
      cmd_scan = false;
      do_scan();
    }

    if (cmd_connect) {
      cmd_connect = false;
      char ssid[33], pass[65];
      LOCK();
      strlcpy(ssid, g.ssid, sizeof(ssid));
      strlcpy(pass, g.pass, sizeof(pass));
      UNLOCK();
      // only write to flash when something changed (flash writes can disturb the RGB display)
      if (prefs.getString("ssid", "") != ssid) prefs.putString("ssid", ssid);
      if (prefs.getString("pass", "") != pass) prefs.putString("pass", pass);
      WiFi.disconnect();
      vTaskDelay(pdMS_TO_TICKS(200));
      WiFi.begin(ssid, pass);
      connect_started = millis() | 1;
      was_up = false;
      set_wifi_status("Connecting to %s...", ssid);
    }

    save_pending();

    bool up = (WiFi.status() == WL_CONNECTED);
    if (up && !was_up) {
      set_wifi_status("Connected to %s\nIP %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      USBSerial.printf("WiFi connected. Internal heap free %u, largest block %u\n",
                       heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                       heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
      connect_started = 0;
      if (!ntp_started) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC; local offset comes from Open-Meteo
        ntp_started = true;
      }
      cmd_weather = true;
    } else if (!up && was_up) {
      set_wifi_status("Connection lost - reconnecting...");
    }
    was_up = up;

    if (!up && connect_started && millis() - connect_started > 20000) {
      set_wifi_status("Could not connect. Check the password.");
      connect_started = 0;
    }

    if (cmd_geocode) {
      cmd_geocode = false;
      if (up) do_geocode();
      else set_loc_status("Connect to WiFi first");
    }

    LOCK();
    bool has_loc = g.has_loc;
    UNLOCK();
    if (up && has_loc && (cmd_weather || (int32_t)(millis() - next_weather) >= 0)) {
      cmd_weather = false;
      bool ok = do_weather();
      next_weather = millis() + (ok ? 15UL * 60 * 1000 : 60UL * 1000);  // 15 min, retry after 1 min
      if (!ok) USBSerial.println("Weather fetch failed");
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void net_start() {
  xTaskCreatePinnedToCore(net_task, "net", 6144, NULL, 1, NULL, 0);  // plain HTTP needs a small stack
}
