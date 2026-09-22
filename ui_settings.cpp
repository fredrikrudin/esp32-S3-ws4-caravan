/* Settings tab: WiFi, weather location, RuuviTag, display, sensor scan interval.
   The Victron, relay board and I2C sections live in their own files. */
#include "app.h"

lv_obj_t *lbl_wifi_status, *dd_ssid, *lbl_loc;  // also updated by net_poll_cb()

static lv_obj_t *ta_pass, *ta_city;
static lv_obj_t *dd_ruuvi, *lbl_ruuvi_sel;
static lv_obj_t *sl_bl, *lbl_bl, *sl_bl_saver, *lbl_bl_saver;
static lv_obj_t *lbl_scan;
static char dd_addr[MAX_TAGS][18];  // MAC for each Ruuvi dropdown row
static int dd_count = 0;

/* ---------- WiFi ---------- */
static void connect_now() {
  char ssid[33];
  lv_dropdown_get_selected_str(dd_ssid, ssid, sizeof(ssid));
  if (!ssid[0] || !strcmp(ssid, NO_NET_TEXT) || !strcmp(ssid, NO_NET_FOUND)) {
    lv_label_set_text(lbl_wifi_status, "Scan and choose a network first");
    return;
  }
  LOCK();
  strlcpy(g.ssid, ssid, sizeof(g.ssid));
  strlcpy(g.pass, lv_textarea_get_text(ta_pass), sizeof(g.pass));
  UNLOCK();
  cmd_connect = true;
  kb_hide();
}

static void scan_btn_cb(lv_event_t *e) {
  cmd_scan = true;
}
static void connect_btn_cb(lv_event_t *e) {
  connect_now();
}

/* ---------- Location ---------- */
static void locate_now() {
  const char *c = lv_textarea_get_text(ta_city);
  if (!c[0]) return;
  LOCK();
  strlcpy(g.city, c, sizeof(g.city));
  UNLOCK();
  cmd_geocode = true;
  kb_hide();
}

static void locate_btn_cb(lv_event_t *e) {
  locate_now();
}

/* ---------- RuuviTag ---------- */
static void update_ruuvi_sel_label() {
  char sel[18], s[8];
  LOCK();
  strlcpy(sel, g.ruuvi_sel, sizeof(sel));
  UNLOCK();
  if (!sel[0]) {
    lv_label_set_text(lbl_ruuvi_sel, "No tag selected");
    return;
  }
  mac_short(sel, s);
  lv_label_set_text_fmt(lbl_ruuvi_sel, "Showing Ruuvi %s on the Temp tab", s);
}

static void ruuvi_use_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_ruuvi);
  if (i >= dd_count) return;  // "Searching..." row
  LOCK();
  strlcpy(g.ruuvi_sel, dd_addr[i], sizeof(g.ruuvi_sel));
  UNLOCK();
  cmd_save_ruuvi = true;
  update_ruuvi_sel_label();
}

/* Called once per second by ruuvi_timer_cb() with a copy of the tag table */
void ruuvi_settings_refresh(const RuuviTag *copy, uint32_t now) {
  if (lv_dropdown_is_open(dd_ruuvi)) return;  // don't change it under the user's finger

  char cur[18];
  uint16_t ci = lv_dropdown_get_selected(dd_ruuvi);
  if (ci < dd_count) strlcpy(cur, dd_addr[ci], sizeof(cur));
  else {
    LOCK();
    strlcpy(cur, g.ruuvi_sel, sizeof(cur));
    UNLOCK();
  }

  static char last_opts[MAX_TAGS * 40] = "";
  char opts[MAX_TAGS * 40] = "";
  int count = 0, cur_idx = 0;
  for (int i = 0; i < MAX_TAGS; i++) {
    if (!copy[i].used || now - copy[i].last_seen > 10UL * 60 * 1000) continue;  // hide tags gone > 10 min
    char s[8], line[40];
    mac_short(copy[i].addr, s);
    if (copy[i].has_temp) snprintf(line, sizeof(line), "%sRuuvi %s   %.1f" DEG, count ? "\n" : "", s, copy[i].temp);
    else snprintf(line, sizeof(line), "%sRuuvi %s", count ? "\n" : "", s);
    strlcat(opts, line, sizeof(opts));
    strlcpy(dd_addr[count], copy[i].addr, sizeof(dd_addr[count]));
    if (!strcmp(copy[i].addr, cur)) cur_idx = count;
    count++;
  }
  dd_count = count;
  if (!count) strcpy(opts, "Searching...");
  if (strcmp(opts, last_opts)) {
    strlcpy(last_opts, opts, sizeof(last_opts));
    lv_dropdown_set_options(dd_ruuvi, opts);
    lv_dropdown_set_selected(dd_ruuvi, cur_idx);
  }
}

/* ---------- Display ---------- */
static void update_bl_labels() {
  lv_label_set_text_fmt(lbl_bl, "Brightness: %d%%", bl_normal);
  if (bl_saver == 0) lv_label_set_text(lbl_bl_saver, "Screen saver brightness: off");
  else lv_label_set_text_fmt(lbl_bl_saver, "Screen saver brightness: %d%%", bl_saver);
}

static void bl_slider_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *sl = lv_event_get_target(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    if (sl == sl_bl) bl_normal = lv_slider_get_value(sl);
    else bl_saver = lv_slider_get_value(sl);
    update_bl_labels();
    set_backlight(sl == sl_bl ? bl_normal : bl_saver);  // live preview while dragging
  } else if (code == LV_EVENT_RELEASED) {
    set_backlight(bl_normal);  // back to normal after previewing the screen saver level
    cmd_save_bl = true;
  }
}

/* ---------- Sensor scan interval ---------- */
static void update_scan_label() {
  if (scan_interval_s <= 1) lv_label_set_text(lbl_scan, "Update sensors: continuously (1 s)");
  else lv_label_set_text_fmt(lbl_scan, "Update sensors: every %d s", scan_interval_s);
}

static void scan_slider_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    scan_interval_s = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    update_scan_label();
  } else if (code == LV_EVENT_RELEASED) {
    scan_restart = true;   // apply the new interval
    cmd_save_scan = true;  // save once, when the finger lifts
  }
}

/* ---------- build ---------- */
void build_settings_tab() {
  lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab_settings, 12, 0);

  /* WiFi */
  make_heading(tab_settings, LV_SYMBOL_WIFI "  WiFi");

  lv_obj_t *row = make_row(tab_settings, LV_FLEX_ALIGN_START);
  dd_ssid = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_ssid, 1);
  lv_dropdown_set_options(dd_ssid, g.ssid[0] ? g.ssid : NO_NET_TEXT);
  make_btn(row, LV_SYMBOL_REFRESH " Scan", scan_btn_cb);

  ta_pass = make_ta(tab_settings, "Password", connect_now);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_obj_set_width(ta_pass, LV_PCT(100));
  lv_textarea_set_text(ta_pass, g.pass);

  row = make_row(tab_settings, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_OK " Connect", connect_btn_cb);
  lbl_wifi_status = lv_label_create(row);
  lv_obj_set_flex_grow(lbl_wifi_status, 1);
  lv_label_set_long_mode(lbl_wifi_status, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_wifi_status, "");

  /* Location */
  make_heading(tab_settings, LV_SYMBOL_GPS "  Weather location");

  row = make_row(tab_settings, LV_FLEX_ALIGN_START);
  ta_city = make_ta(row, "City (or City, Country)", locate_now);
  lv_obj_set_flex_grow(ta_city, 1);
  lv_textarea_set_text(ta_city, g.city);
  make_btn(row, LV_SYMBOL_OK " Set", locate_btn_cb);

  lbl_loc = lv_label_create(tab_settings);
  lv_obj_set_width(lbl_loc, LV_PCT(100));
  lv_label_set_long_mode(lbl_loc, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_loc, "");

  /* RuuviTag */
  make_heading(tab_settings, LV_SYMBOL_BLUETOOTH "  RuuviTag");

  row = make_row(tab_settings, LV_FLEX_ALIGN_START);
  dd_ruuvi = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_ruuvi, 1);
  lv_dropdown_set_options(dd_ruuvi, "Searching...");
  make_btn(row, LV_SYMBOL_OK " Use", ruuvi_use_cb);

  lbl_ruuvi_sel = lv_label_create(tab_settings);
  lv_obj_set_width(lbl_ruuvi_sel, LV_PCT(100));
  lv_label_set_long_mode(lbl_ruuvi_sel, LV_LABEL_LONG_WRAP);
  update_ruuvi_sel_label();

  /* Display */
  make_heading(tab_settings, LV_SYMBOL_IMAGE "  Display");
  lbl_bl = lv_label_create(tab_settings);
  sl_bl = make_slider(tab_settings, 5, 100, bl_normal, bl_slider_cb);  // min 5% so the screen never goes black
  lbl_bl_saver = lv_label_create(tab_settings);
  sl_bl_saver = make_slider(tab_settings, 0, 100, bl_saver, bl_slider_cb);
  update_bl_labels();

  /* Sensor scan interval (RuuviTag + Victron) */
  make_heading(tab_settings, LV_SYMBOL_REFRESH "  Sensor scan interval");
  lbl_scan = lv_label_create(tab_settings);
  make_slider(tab_settings, 1, 10, scan_interval_s, scan_slider_cb);
  update_scan_label();

  /* Sections in their own files */
  settings_victron(tab_settings);
  settings_relays(tab_settings);
  settings_i2c(tab_settings);
}
