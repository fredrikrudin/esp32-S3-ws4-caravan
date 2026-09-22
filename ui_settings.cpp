/* Settings tab: WiFi, web page password, weather location, RuuviTags, display, sensor scan interval.
   The Victron, relay board and I2C sections live in their own files. */
#include "app.h"

lv_obj_t *lbl_wifi_status, *dd_ssid, *lbl_loc;  // also updated by net_poll_cb()

static lv_obj_t *ta_pass, *ta_city;
static lv_obj_t *ta_webpass, *lbl_web;
static lv_obj_t *dd_ruuvi, *ruuvi_list, *lbl_ruuvi_msg;
static lv_obj_t *ruuvi_editor, *lbl_ruuvi_edit_title, *ta_rname;
static lv_obj_t *ruuvi_list_lbl[MAX_RUUVI];
static int ruuvi_edit_idx = -1;  // slot being edited, -1 = new tag
static char ruuvi_edit_mac[18];
static lv_obj_t *sl_bl, *lbl_bl, *sl_bl_saver, *lbl_bl_saver;
static lv_obj_t *lbl_scan;
static char dd_addr[MAX_TAGS][18];  // MAC for each row of the Ruuvi "Add" list
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

/* ---------- Web page ---------- */
static void update_web_label() {
  bool has_pass;
  LOCK();
  has_pass = g.web_pass[0] != 0;
  UNLOCK();
  lv_label_set_text_fmt(lbl_web, "Open http://%s.local/ on the same WiFi.\n%s", MDNS_NAME,
                        has_pass ? "Password required." : "No password: anyone on the WiFi can view the page.");
}

static void webpass_save_now() {
  LOCK();
  strlcpy(g.web_pass, lv_textarea_get_text(ta_webpass), sizeof(g.web_pass));
  UNLOCK();
  cmd_save_web = true;
  web_password_changed();  // logs everyone out, so the new password applies at once
  update_web_label();
  kb_hide();
}

static void webpass_save_cb(lv_event_t *e) {
  webpass_save_now();
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

/* ---------- RuuviTags (up to MAX_RUUVI, with names) ---------- */
static void ruuvi_close_editor() {
  lv_obj_add_flag(ruuvi_editor, LV_OBJ_FLAG_HIDDEN);
  ruuvi_edit_idx = -1;
  kb_hide();
}

static void ruuvi_open_editor(const char *mac, int slot) {
  ruuvi_edit_idx = slot;
  strlcpy(ruuvi_edit_mac, mac, sizeof(ruuvi_edit_mac));
  char s[8], name[20];
  mac_short(mac, s);
  lv_label_set_text_fmt(lbl_ruuvi_edit_title, "Ruuvi %s  (%s)", s, mac);
  if (slot >= 0) strlcpy(name, ruuvi_cfg[slot].name, sizeof(name));
  else snprintf(name, sizeof(name), "Ruuvi %s", s);
  lv_textarea_set_text(ta_rname, name);
  lv_label_set_text(lbl_ruuvi_msg, "");
  lv_obj_clear_flag(ruuvi_editor, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(tab_settings);
  lv_obj_scroll_to_view_recursive(ruuvi_editor, LV_ANIM_ON);
}

static void ruuvi_list_btn_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  ruuvi_open_editor(ruuvi_cfg[i].mac, i);
}

static void ruuvi_rebuild_list() {
  lv_obj_clean(ruuvi_list);
  int count = 0;
  for (int i = 0; i < MAX_RUUVI; i++) {
    ruuvi_list_lbl[i] = NULL;
    if (!ruuvi_cfg[i].used) continue;
    lv_obj_t *b = lv_btn_create(ruuvi_list);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_add_event_cb(b, ruuvi_list_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_text(l, ruuvi_cfg[i].name);
    ruuvi_list_lbl[i] = l;
    count++;
  }
  if (!count) {
    lv_obj_t *l = make_grey_label(ruuvi_list);
    lv_label_set_text(l, "No tags added yet");
  }
}

static void ruuvi_save_now() {
  const char *name = lv_textarea_get_text(ta_rname);
  int slot = ruuvi_edit_idx;
  if (slot < 0) {
    for (int i = 0; i < MAX_RUUVI; i++) {
      if (!ruuvi_cfg[i].used) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {
    lv_label_set_text_fmt(lbl_ruuvi_msg, "Max %d tags. Delete one first.", MAX_RUUVI);
    return;
  }
  char s[8];
  mac_short(ruuvi_edit_mac, s);
  LOCK();
  RuuviCfg &c = ruuvi_cfg[slot];
  c.used = true;
  strlcpy(c.mac, ruuvi_edit_mac, sizeof(c.mac));
  if (name[0]) strlcpy(c.name, name, sizeof(c.name));
  else snprintf(c.name, sizeof(c.name), "Ruuvi %s", s);
  UNLOCK();
  cmd_save_ruuvi = true;
  ruuvi_close_editor();
  ruuvi_rebuild_list();
}

static void ruuvi_save_cb(lv_event_t *e) {
  ruuvi_save_now();
}

static void ruuvi_delete_cb(lv_event_t *e) {
  if (ruuvi_edit_idx >= 0) {
    LOCK();
    memset(&ruuvi_cfg[ruuvi_edit_idx], 0, sizeof(RuuviCfg));
    UNLOCK();
    cmd_save_ruuvi = true;
  }
  ruuvi_close_editor();
  ruuvi_rebuild_list();
}

static void ruuvi_cancel_cb(lv_event_t *e) {
  ruuvi_close_editor();
}

static void ruuvi_add_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_ruuvi);
  if (i >= dd_count) return;  // "Searching..." row
  int used = 0;
  for (int k = 0; k < MAX_RUUVI; k++) used += ruuvi_cfg[k].used;
  if (used >= MAX_RUUVI) {
    lv_label_set_text_fmt(lbl_ruuvi_msg, "Max %d tags. Tap one above and delete it first.", MAX_RUUVI);
    return;
  }
  ruuvi_open_editor(dd_addr[i], -1);
}

static int ruuvi_cfg_index(const char *mac) {
  for (int i = 0; i < MAX_RUUVI; i++)
    if (ruuvi_cfg[i].used && !strcmp(ruuvi_cfg[i].mac, mac)) return i;
  return -1;
}

/* Called once per second by ruuvi_timer_cb() with a copy of the tags in range */
void ruuvi_settings_refresh(const RuuviTag *copy, uint32_t now) {
  char b[96];

  /* added tags: live temperature */
  for (int i = 0; i < MAX_RUUVI; i++) {
    if (!ruuvi_cfg[i].used || !ruuvi_list_lbl[i]) continue;
    const RuuviTag *t = NULL;
    for (int k = 0; k < MAX_TAGS; k++)
      if (copy[k].used && !strcmp(copy[k].addr, ruuvi_cfg[i].mac)) t = &copy[k];
    char s[8];
    mac_short(ruuvi_cfg[i].mac, s);
    if (t && t->has_temp && now - t->last_seen < 10UL * 60 * 1000)
      snprintf(b, sizeof(b), "%s\nRuuvi %s  -  %.1f" DEG "C", ruuvi_cfg[i].name, s, t->temp);
    else
      snprintf(b, sizeof(b), "%s\nRuuvi %s  -  not heard", ruuvi_cfg[i].name, s);
    set_label(ruuvi_list_lbl[i], b);
  }

  /* tags in range that aren't added yet (don't change the list under the user's finger) */
  if (lv_dropdown_is_open(dd_ruuvi)) return;
  char cur[18] = "";
  uint16_t ci = lv_dropdown_get_selected(dd_ruuvi);
  if (ci < dd_count) strlcpy(cur, dd_addr[ci], sizeof(cur));

  static char last_opts[MAX_TAGS * 40] = "";
  char opts[MAX_TAGS * 40] = "";
  int count = 0, cur_idx = 0;
  for (int i = 0; i < MAX_TAGS; i++) {
    if (!copy[i].used || now - copy[i].last_seen > 10UL * 60 * 1000) continue;  // gone > 10 min
    if (ruuvi_cfg_index(copy[i].addr) >= 0) continue;                          // already added
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

  /* Web page */
  make_heading(tab_settings, LV_SYMBOL_EYE_OPEN "  Web page");
  lbl_web = lv_label_create(tab_settings);
  lv_obj_set_width(lbl_web, LV_PCT(100));
  lv_label_set_long_mode(lbl_web, LV_LABEL_LONG_WRAP);
  row = make_row(tab_settings, LV_FLEX_ALIGN_START);
  ta_webpass = make_ta(row, "Password (empty = no login)", webpass_save_now);
  lv_textarea_set_password_mode(ta_webpass, true);
  lv_textarea_set_max_length(ta_webpass, 32);
  lv_obj_set_flex_grow(ta_webpass, 1);
  lv_textarea_set_text(ta_webpass, g.web_pass);
  make_btn(row, LV_SYMBOL_SAVE " Save", webpass_save_cb);
  update_web_label();

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

  /* RuuviTags */
  make_heading(tab_settings, LV_SYMBOL_BLUETOOTH "  RuuviTags (max 3)");

  row = make_row(tab_settings, LV_FLEX_ALIGN_START);
  dd_ruuvi = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_ruuvi, 1);
  lv_dropdown_set_options(dd_ruuvi, "Searching...");
  make_btn(row, LV_SYMBOL_PLUS " Add", ruuvi_add_cb);

  ruuvi_list = lv_obj_create(tab_settings);
  lv_obj_remove_style_all(ruuvi_list);
  lv_obj_set_size(ruuvi_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(ruuvi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ruuvi_list, 6, 0);
  lv_obj_clear_flag(ruuvi_list, LV_OBJ_FLAG_SCROLLABLE);

  ruuvi_editor = lv_obj_create(tab_settings);
  lv_obj_set_size(ruuvi_editor, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(ruuvi_editor, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ruuvi_editor, 8, 0);
  lv_obj_clear_flag(ruuvi_editor, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ruuvi_editor, LV_OBJ_FLAG_HIDDEN);
  lbl_ruuvi_edit_title = lv_label_create(ruuvi_editor);
  ta_rname = make_ta(ruuvi_editor, "Name, e.g. Inside", ruuvi_save_now);
  lv_obj_set_width(ta_rname, LV_PCT(100));
  lv_textarea_set_max_length(ta_rname, 19);
  row = make_row(ruuvi_editor, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SAVE " Save", ruuvi_save_cb);
  lv_obj_t *del = make_btn(row, LV_SYMBOL_TRASH " Delete", ruuvi_delete_cb);
  lv_obj_set_style_bg_color(del, lv_palette_main(LV_PALETTE_RED), 0);
  make_btn(row, "Cancel", ruuvi_cancel_cb);

  lbl_ruuvi_msg = lv_label_create(tab_settings);
  lv_obj_set_width(lbl_ruuvi_msg, LV_PCT(100));
  lv_label_set_long_mode(lbl_ruuvi_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(lbl_ruuvi_msg, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(lbl_ruuvi_msg, "");

  ruuvi_rebuild_list();

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
