// esp32-S3-ws4-caravan v1.0
/* Settings tab: WiFi, web page password, weather location, RuuviTags, display, sensor scan interval.
   The Victron, relay board and I2C sections live in their own files. */
#include "app.h"

lv_obj_t *lbl_wifi_status, *dd_ssid, *lbl_loc;  // also updated by net_poll_cb()

static lv_obj_t *ta_pass, *ta_city;
static lv_obj_t *ta_webpass, *ta_webname, *lbl_web;
static lv_obj_t *dd_ruuvi, *ruuvi_list, *lbl_ruuvi_msg;
static lv_obj_t *ruuvi_editor, *lbl_ruuvi_edit_title, *ta_rname;
static lv_obj_t *ruuvi_list_lbl[MAX_RUUVI];
static int ruuvi_edit_idx = -1;  // slot being edited, -1 = new tag
static char ruuvi_edit_mac[18];
static lv_obj_t *sl_bl, *lbl_bl, *sl_bl_saver, *lbl_bl_saver;
static lv_obj_t *lbl_scan;
static lv_obj_t *dd_bms;
static lv_obj_t *lbl_sdlog, *lbl_csv, *dd_csv;
static lv_obj_t *lbl_lipo;
static lv_obj_t *lbl_alarm, *sl_soc_warn, *sl_soc_alarm, *sl_wind;
static char bms_dd_mac[MAX_BMS_SEEN][18];
static uint8_t bms_dd_type[MAX_BMS_SEEN];
static char bms_dd_name[MAX_BMS_SEEN][32];
static int bms_dd_count = 0;
static char dd_addr[MAX_TAGS][18];  // MAC for each row of the Ruuvi "Add" list
static int dd_count = 0;

/* ---------- WiFi ---------- */
static void connect_now() {
  char ssid[33];
  lv_dropdown_get_selected_str(dd_ssid, ssid, sizeof(ssid));
  if (!ssid[0] || !strcmp(ssid, TR(NO_NET_TEXT)) || !strcmp(ssid, TR(NO_NET_FOUND))) {
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
  char name[24];
  LOCK();
  strlcpy(name, g.web_name, sizeof(name));
  UNLOCK();
  if (!feat_web) {
    lv_label_set_text(lbl_web, "Web server switched off. Nothing is served, and about 12 kB of memory is freed.");
    return;
  }
  lv_label_set_text_fmt(lbl_web, "\"%s\" at http://%s.local/ on the same WiFi.\n%s%s", name, MDNS_NAME,
                        has_pass ? TR("Password required.") : TR("No password: anyone on the WiFi can view the page."),
                        feat_remote ? TR(" Switching allowed.") : "");
}

static void webpass_save_now() {
  LOCK();
  strlcpy(g.web_pass, lv_textarea_get_text(ta_webpass), sizeof(g.web_pass));
  const char *n = lv_textarea_get_text(ta_webname);
  strlcpy(g.web_name, n[0] ? n : DEFAULT_WEB_NAME, sizeof(g.web_name));
  UNLOCK();
  cmd_save_web = true;
  web_password_changed();  // logs everyone out, so the new password applies at once
  update_web_label();
  kb_hide();
}

static void webpass_save_cb(lv_event_t *e) {
  webpass_save_now();
}

static void web_enable_cb(lv_event_t *e) {
  feat_web = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_web = true;
  update_web_label();
}

static void remote_cb(lv_event_t *e) {
  feat_remote = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  update_web_label();
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
  else snprintf(name, sizeof(name), TR("Ruuvi %s"), s);
  lv_textarea_set_text(ta_rname, name);
  lv_label_set_text(lbl_ruuvi_msg, "");
  lv_obj_clear_flag(ruuvi_editor, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(lv_obj_get_parent(ruuvi_editor));
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
  else snprintf(c.name, sizeof(c.name), TR("Ruuvi %s"), s);
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
      snprintf(b, sizeof(b), TR("%s\nRuuvi %s  -  %.1f" DEG "C"), ruuvi_cfg[i].name, s, t->temp);
    else
      snprintf(b, sizeof(b), TR("%s\nRuuvi %s  -  not heard"), ruuvi_cfg[i].name, s);
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
    if (copy[i].has_temp) snprintf(line, sizeof(line), TR("%sRuuvi %s   %.1f" DEG), count ? "\n" : "", s, copy[i].temp);
    else snprintf(line, sizeof(line), TR("%sRuuvi %s"), count ? "\n" : "", s);
    strlcat(opts, line, sizeof(opts));
    strlcpy(dd_addr[count], copy[i].addr, sizeof(dd_addr[count]));
    if (!strcmp(copy[i].addr, cur)) cur_idx = count;
    count++;
  }
  dd_count = count;
  if (!count) strcpy(opts, TR("Searching..."));
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

/* ---------- Battery: choose the BMS device ---------- */
static bool contains_nocase(const char *name, const char *hint) {
  size_t hl = strlen(hint);
  for (const char *p = name; *p; p++)
    if (!strncasecmp(p, hint, hl)) return true;
  return false;
}

/* names that are probably a battery go to the top of the list */
static bool likely_battery(const char *name) {
  const char *hints[] = { "BWOB", "ECO", "JBD", "DCHOUSE", "BMC", "SP0", "DP0", "xiaoxiang", "BMS" };
  for (const char *h : hints)
    if (contains_nocase(name, h)) return true;
  return false;
}

static void bms_connect_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_bms);
  if (i >= bms_dd_count) return;  // "Searching..."
  bms_connect_to(bms_dd_mac[i], bms_dd_type[i], bms_dd_name[i]);
}

/* Called once per second from the Battery tab with the BLE devices in range */
void bms_settings_refresh(const BmsSeen *seen, int n, uint32_t now) {
  if (lv_dropdown_is_open(dd_bms)) return;

  char cur[18] = "";
  uint16_t ci = lv_dropdown_get_selected(dd_bms);
  if (ci < bms_dd_count) strlcpy(cur, bms_dd_mac[ci], sizeof(cur));
  else {
    LOCK();
    strlcpy(cur, bms_cfg.mac, sizeof(cur));
    UNLOCK();
  }

  static char last_opts[MAX_BMS_SEEN * 40] = "";
  char opts[MAX_BMS_SEEN * 40] = "";
  int count = 0, cur_idx = 0;
  for (int pass = 0; pass < 2; pass++) {  // likely batteries first
    for (int i = 0; i < n; i++) {
      if (now - seen[i].last_seen > 120000UL) continue;
      if (likely_battery(seen[i].name) != (pass == 0)) continue;
      char line[40];
      snprintf(line, sizeof(line), "%s%s", count ? "\n" : "", seen[i].name);
      strlcat(opts, line, sizeof(opts));
      strlcpy(bms_dd_mac[count], seen[i].mac, sizeof(bms_dd_mac[count]));
      strlcpy(bms_dd_name[count], seen[i].name, sizeof(bms_dd_name[count]));
      bms_dd_type[count] = seen[i].addr_type;
      if (!strcmp(seen[i].mac, cur)) cur_idx = count;
      count++;
    }
  }
  bms_dd_count = count;
  if (!count) strcpy(opts, TR("Searching..."));
  if (strcmp(opts, last_opts)) {
    strlcpy(last_opts, opts, sizeof(last_opts));
    lv_dropdown_set_options(dd_bms, opts);
    lv_dropdown_set_selected(dd_bms, cur_idx);
  }
}

/* ---------- starting screen ---------- */
static lv_obj_t *ta_splash, *dd_splash_size, *sl_splash_r, *sl_splash_g, *sl_splash_b, *sl_splash_sec, *lbl_splash_prev;

static void splash_preview() {
  lv_obj_set_style_text_color(lbl_splash_prev, lv_color_make(splash_cfg.r, splash_cfg.g, splash_cfg.b), 0);
  lv_obj_set_style_text_font(lbl_splash_prev, splash_font(splash_cfg.size), 0);
  lv_label_set_text(lbl_splash_prev, splash_cfg.text[0] ? splash_cfg.text : "CABBY");
}

static void splash_save_now() {
  LOCK();
  strlcpy(splash_cfg.text, lv_textarea_get_text(ta_splash), sizeof(splash_cfg.text));
  const uint8_t sizes[] = { 20, 28, 32, 48 };
  splash_cfg.size = sizes[lv_dropdown_get_selected(dd_splash_size)];
  splash_cfg.r = lv_slider_get_value(sl_splash_r);
  splash_cfg.g = lv_slider_get_value(sl_splash_g);
  splash_cfg.b = lv_slider_get_value(sl_splash_b);
  splash_cfg.seconds = lv_slider_get_value(sl_splash_sec);
  UNLOCK();
  cmd_save_splash = true;
  splash_preview();
  kb_hide();
}

static void splash_cb(lv_event_t *e) {
  splash_save_now();
}

/* ---------- power ---------- */
/* Not called while the page is being built: reading the battery goes out over
   I2C to the CH32 chip, and a fault there must not stop the UI coming up. */
static void update_lipo_label(bool read_now = true) {
  float v;
  int pct;
  bool chg;
  if (!read_now) {
    lv_label_set_text(lbl_lipo, "Onboard battery: tap Check battery");
    return;
  }
  if (!board_battery(&v, &pct, &chg)) {
    lv_label_set_text(lbl_lipo, "Onboard battery: none connected (running on external power)");
    return;
  }
  lv_label_set_text_fmt(lbl_lipo, "Onboard battery: %d%%  (%d.%02d V)%s\n%s", pct, (int)v, (int)(v * 100) % 100,
                        chg ? TR("  " LV_SYMBOL_CHARGE " charging") : "",
                        sw6106_present() ? TR("SW6106 found: light-load shutdown disabled")
                                         : TR("No SW6106 on this board"));
}

static void lipo_refresh_cb(lv_event_t *e) {
  update_lipo_label();
}

static void powersave_cb(lv_event_t *e) {
  feat_powersave = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  if (!feat_powersave) power_set_saving(false);
}

static lv_obj_t *lbl_battmode, *sl_battpct;

static void update_battmode_label() {
  lv_label_set_text_fmt(lbl_battmode,
                        feat_battmode
                          ? "On battery: WiFi and Bluetooth off, screen at 10%%, last readings kept.\nShuts down at %d%%."
                          : TR("Battery mode off: the board keeps running normally until the cell is flat."),
                        batt_shutdown_pct);
}

static void battmode_cb(lv_event_t *e) {
  feat_battmode = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  update_battmode_label();
}

static void battpct_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    batt_shutdown_pct = lv_slider_get_value(lv_event_get_target(e));
    update_battmode_label();
  } else if (code == LV_EVENT_RELEASED) {
    cmd_save_feat = true;
  }
}

static void perflog_cb(lv_event_t *e) {
  feat_perflog = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

/* ---------- debug ---------- */
static void serial_cb(lv_event_t *e) {
  feat_serial = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  serial_apply();
  log_fault("Serial monitor %s", feat_serial ? "on: everything is logged to USB serial" : "off: only faults");
  cmd_save_feat = true;
}

/* ---------- alarms ---------- */
static void update_alarm_label() {
  char b[128];
  snprintf(b, sizeof(b), TR("Battery: warning below %d%%, alarm below %d%%%s"),
           alarm_cfg.soc_warn, alarm_cfg.soc_alarm, alarm_cfg.wind_warn ? "" : TR("\nWind warning off"));
  if (alarm_cfg.wind_warn) {
    char w[40];
    snprintf(w, sizeof(w), TR("\nWind warning at %d m/s"), alarm_cfg.wind_warn);
    strlcat(b, w, sizeof(b));
  }
  lv_label_set_text(lbl_alarm, b);
}

static void alarm_slider_cb(lv_event_t *e) {
  lv_obj_t *sl = lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    int v = lv_slider_get_value(sl);
    LOCK();
    if (sl == sl_soc_warn) alarm_cfg.soc_warn = v;
    else if (sl == sl_soc_alarm) alarm_cfg.soc_alarm = v;
    else if (sl == sl_wind) alarm_cfg.wind_warn = v;
    if (alarm_cfg.soc_alarm > alarm_cfg.soc_warn) alarm_cfg.soc_warn = alarm_cfg.soc_alarm;  // keep them in order
    UNLOCK();
    update_alarm_label();
  } else if (code == LV_EVENT_RELEASED) {
    cmd_save_alarm = true;
  }
}

static void alarm_enable_cb(lv_event_t *e) {
  alarm_cfg.enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_alarm = true;
}

static void alarm_sd_cb(lv_event_t *e) {
  alarm_cfg.warn_sd = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_alarm = true;
}

static void alarm_wake_cb(lv_event_t *e) {
  alarm_cfg.wake_saver = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_alarm = true;
}

/* ---------- confirmation dialog ---------- */
static void (*confirm_action)() = nullptr;

static void confirm_cb(lv_event_t *e) {
  lv_obj_t *mb = lv_event_get_current_target(e);
  uint16_t id = lv_msgbox_get_active_btn(mb);
  lv_msgbox_close(mb);
  if (id == 1 && confirm_action) confirm_action();  // 1 = "Yes"
}

static void confirm(const char *title, const char *text, void (*action)()) {
  static const char *btns[] = { TR("Cancel"), TR("Yes"), "" };
  confirm_action = action;
  lv_obj_t *mb = lv_msgbox_create(NULL, TR(title), TR(text), btns, false);  // NULL = modal
  lv_obj_add_event_cb(mb, confirm_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_center(mb);
}

/* ---------- system ---------- */
static void tidy_up() {
  logf("System: closing the log and unmounting the card");
  sd_log_unmount();
  lv_refr_now(NULL);
  delay(300);
}

static void do_reboot() {
  tidy_up();
  ESP.restart();
}

/* ---------- language ---------- */
/* Saved by the network task (it does all flash writes), then the board restarts
   so every screen is built again in the new language. */
static lv_obj_t *dd_lang;
#define MAX_LANGS 8
static char lang_codes[MAX_LANGS][LANG_CODE_LEN];
char lang_pending[LANG_CODE_LEN] = "en";

static void lang_restart_timer(lv_timer_t *t) {
  static uint8_t waited = 0;
  if (cmd_save_lang && ++waited < 30) return;  // up to 3 s for the write
  logf("Language changed to %s - restarting", lang_pending);
  do_reboot();
}

static void lang_cb(lv_event_t *e) {
  const char *code = lang_codes[lv_dropdown_get_selected(dd_lang)];
  if (!strcmp(code, ui_lang)) return;
  strlcpy(lang_pending, code, sizeof(lang_pending));
  cmd_save_lang = true;
  /* shown as the language's own name: its translation only arrives with the restart */
  static char msg[48];
  char name[32];
  lv_dropdown_get_selected_str(dd_lang, name, sizeof(name));
  snprintf(msg, sizeof(msg), LV_SYMBOL_REFRESH "  %s ...", name);
  lv_obj_t *mb = lv_msgbox_create(NULL, NULL, msg, NULL, false);
  lv_obj_center(mb);
  lv_timer_create(lang_restart_timer, 100, NULL);
}

static void do_shutdown() {
  tidy_up();
  set_backlight(0);
  delay(200);
  esp_deep_sleep_start();  // press the reset button to start again
}

static void do_factory_reset() {
  logf("System: erasing all settings");
  prefs.clear();  // everything in the "wx" namespace
  tidy_up();
  ESP.restart();
}

static void reboot_cb(lv_event_t *e) {
  confirm("Restart", "Restart the board now?", do_reboot);
}

static void shutdown_cb(lv_event_t *e) {
  confirm("Shut down", "Switch the display off?\n\nThe board goes to sleep and only the reset button (or cutting the power) starts it again.", do_shutdown);
}

static void factory_cb(lv_event_t *e) {
  confirm("Full reset",
          "Erase every setting?\n\nWiFi, weather location, RuuviTags, Victron devices and keys, relays, Shelly and the web password are all lost. The board restarts afterwards.",
          do_factory_reset);
}

/* ---------- SD card and logging ---------- */
static void update_sdlog_label() {
  char info[64];
  sd_card_info(info, sizeof(info));
  if (sd_log_ok())
    lv_label_set_text_fmt(lbl_sdlog, "%s\n%s, %u kB", info, sd_log_name(), (unsigned)(sd_log_size() / 1024));
  else
    lv_label_set_text_fmt(lbl_sdlog, "%s\n%s", TR(sd_log_status()),
                          feat_sdlog ? TR("Insert a card and tap Mount.") : TR("The log is kept in memory and readable at /log."));
}

static void sd_mount_cb(lv_event_t *e) {
  if (sd_log_ok()) {
    lv_label_set_text(lbl_sdlog, "Already mounted");
  } else if (sd_log_mount()) {
    logf("TF card mounted");
  }
  update_sdlog_label();
}

static void sd_eject_cb(lv_event_t *e) {
  sd_log_unmount();  // flushes and closes the file first
  update_sdlog_label();
}

static void sd_newfile_cb(lv_event_t *e) {
  if (sd_log_new_file()) logf("New log file started");
  update_sdlog_label();
}

static void sd_delete_cb(lv_event_t *e) {
  int n = sd_log_delete_old();
  lv_label_set_text_fmt(lbl_sdlog, "Deleted %d old log file(s)", n);
}

static void sdlog_cb(lv_event_t *e) {
  feat_sdlog = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  if (feat_sdlog) {
    sd_log_mount();
    logf("Logging to the TF card switched on");
  } else {
    sd_log_unmount();
  }
  update_sdlog_label();
}

static void sd_refresh_cb(lv_event_t *e) {
  update_sdlog_label();
}

static void update_csv_label() {
  lv_label_set_text_fmt(lbl_csv, "%s", feat_csv ? TR(csv_status()) : TR("Measurements are not being logged."));
}

static void csv_cb(lv_event_t *e) {
  feat_csv = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  if (feat_csv) sd_log_mount();
  update_csv_label();
}

static void csv_interval_cb(lv_event_t *e) {
  const uint8_t mins[] = { 1, 5, 15, 60 };
  csv_interval_min = mins[lv_dropdown_get_selected(dd_csv)];
  cmd_save_feat = true;
}

static void backup_cb(lv_event_t *e) {
  if (!sd_log_ok()) sd_log_mount();
  lv_label_set_text(lbl_csv, settings_backup() ? "Settings saved to /settings.json" : "Backup failed - is a card inserted?");
}

static void restore_cb(lv_event_t *e) {
  if (!sd_log_ok()) sd_log_mount();
  if (settings_restore()) {
    lv_label_set_text(lbl_csv, "Settings restored - restarting...");
    lv_refr_now(NULL);
    delay(1500);
    ESP.restart();
  } else {
    lv_label_set_text(lbl_csv, "Restore failed - no /settings.json on the card?");
  }
}

/* ---------- feature switches ---------- */
static void feat_ruuvi_cb(lv_event_t *e) {
  feat_ruuvi = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  ui_update_tabs();
}

static void feat_bms_cb(lv_event_t *e) {
  feat_bms = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  ui_update_tabs();
}

/* ---------- build ---------- */
/* Prepares one of the sub-pages: a scrolling column of section cards */
static lv_obj_t *settings_page(lv_obj_t *tv, const char *name) {
  lv_obj_t *p = lv_tabview_add_tab(tv, name);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(p, 16, 0);
  lv_obj_set_style_pad_bottom(p, 16, 0);
  return p;
}

void build_settings_tab() {
  lv_obj_set_style_pad_all(tab_settings, 0, 0);

  /* a second row of tabs, so no page is longer than a couple of screens */
  lv_obj_t *tv = lv_tabview_create(tab_settings, LV_DIR_TOP, 42);
  lv_obj_set_size(tv, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_t *btns = lv_tabview_get_tab_btns(tv);
  lv_obj_set_style_text_font(btns, FONT_UI, 0);
  lv_obj_set_style_bg_color(btns, lv_color_hex(0x1B1F24), 0);

  lv_obj_t *p_conn = settings_page(tv, "Connect");
  lv_obj_t *p_sens = settings_page(tv, "Sensors");
  lv_obj_t *p_ctrl = settings_page(tv, "Control");
  lv_obj_t *p_sys = settings_page(tv, "Device");

  /* ---- WiFi ---- */
  lv_obj_t *sec = make_section(p_conn, LV_SYMBOL_WIFI "  WiFi");

  lv_obj_t *row = make_row(sec, LV_FLEX_ALIGN_START);
  dd_ssid = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_ssid, 1);
  lv_dropdown_set_options(dd_ssid, g.ssid[0] ? g.ssid : NO_NET_TEXT);
  make_btn(row, LV_SYMBOL_REFRESH " Scan", scan_btn_cb);

  ta_pass = make_ta(sec, "Password", connect_now);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_obj_set_width(ta_pass, LV_PCT(100));
  lv_textarea_set_text(ta_pass, g.pass);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_OK " Connect", connect_btn_cb);
  lbl_wifi_status = lv_label_create(row);
  lv_obj_set_flex_grow(lbl_wifi_status, 1);
  lv_label_set_long_mode(lbl_wifi_status, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_wifi_status, "");

  /* ---- Web page ---- */
  sec = make_section(p_conn, LV_SYMBOL_EYE_OPEN "  Web page");
  lbl_web = lv_label_create(sec);
  lv_obj_set_width(lbl_web, LV_PCT(100));
  lv_label_set_long_mode(lbl_web, LV_LABEL_LONG_WRAP);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  ta_webname = make_ta(row, "Name of the page", webpass_save_now);
  lv_textarea_set_max_length(ta_webname, 23);
  lv_obj_set_flex_grow(ta_webname, 1);
  lv_textarea_set_text(ta_webname, g.web_name);

  make_switch_row(sec, "Run the web server", feat_web, web_enable_cb);
  make_switch_row(sec, "Remote admin: switch relays and Shelly from the web page", feat_remote, remote_cb);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  ta_webpass = make_ta(row, "Password (empty = no login)", webpass_save_now);
  lv_textarea_set_password_mode(ta_webpass, true);
  lv_textarea_set_max_length(ta_webpass, 32);
  lv_obj_set_flex_grow(ta_webpass, 1);
  lv_textarea_set_text(ta_webpass, g.web_pass);
  make_btn(row, LV_SYMBOL_SAVE " Save", webpass_save_cb);
  update_web_label();

  /* ---- Weather ---- */
  sec = make_section(p_conn, LV_SYMBOL_GPS "  Weather location");
  row = make_row(sec, LV_FLEX_ALIGN_START);
  ta_city = make_ta(row, "City (or City, Country)", locate_now);
  lv_obj_set_flex_grow(ta_city, 1);
  lv_textarea_set_text(ta_city, g.city);
  make_btn(row, LV_SYMBOL_OK " Set", locate_btn_cb);
  lbl_loc = lv_label_create(sec);
  lv_obj_set_width(lbl_loc, LV_PCT(100));
  lv_label_set_long_mode(lbl_loc, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_loc, "");

  /* ---- Temperature: RuuviTags ---- */
  sec = make_section(p_sens, LV_SYMBOL_BLUETOOTH "  Temperature (RuuviTags)");
  make_switch_row(sec, "Read RuuviTags", feat_ruuvi, feat_ruuvi_cb);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  dd_ruuvi = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_ruuvi, 1);
  lv_dropdown_set_options(dd_ruuvi, "Searching...");
  make_btn(row, LV_SYMBOL_PLUS " Add", ruuvi_add_cb);

  ruuvi_list = lv_obj_create(sec);
  lv_obj_remove_style_all(ruuvi_list);
  lv_obj_set_size(ruuvi_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(ruuvi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ruuvi_list, 6, 0);
  lv_obj_clear_flag(ruuvi_list, LV_OBJ_FLAG_SCROLLABLE);

  ruuvi_editor = lv_obj_create(sec);
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

  lbl_ruuvi_msg = lv_label_create(sec);
  lv_obj_set_width(lbl_ruuvi_msg, LV_PCT(100));
  lv_label_set_long_mode(lbl_ruuvi_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(lbl_ruuvi_msg, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(lbl_ruuvi_msg, "");
  ruuvi_rebuild_list();

  /* ---- Battery ---- */
  sec = make_section(p_sens, LV_SYMBOL_BATTERY_FULL "  Battery (BMS)");
  make_switch_row(sec, "Read the battery BMS", feat_bms, feat_bms_cb);
  lv_obj_t *hint = make_grey_label(sec);
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
#if ENABLE_BMS
  lv_label_set_text(hint, "Close the battery's phone app first: it accepts only one connection. Likely batteries are listed first.");
#else
  lv_label_set_text(hint, "Battery reading is switched off in the firmware (ENABLE_BMS in app.h). The ECO-WORTHY protocol is not fully decoded yet.");
#endif

  row = make_row(sec, LV_FLEX_ALIGN_START);
  dd_bms = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_bms, 1);
  lv_dropdown_set_options(dd_bms, "Searching...");
  make_btn(row, LV_SYMBOL_BLUETOOTH " Connect", bms_connect_cb);

  /* ---- sections in their own files ---- */
  settings_shelly(p_ctrl);
  settings_schedule(p_ctrl);
  settings_victron(p_sens);
  settings_relays(p_ctrl);

  /* ---- Language ---- */
  sec = make_section(p_sys, LV_SYMBOL_LIST "  Language");
  row = make_row(sec, LV_FLEX_ALIGN_SPACE_BETWEEN);
  lv_obj_t *lang_lbl = lv_label_create(row);
  lv_obj_set_flex_grow(lang_lbl, 1);
  lv_label_set_long_mode(lang_lbl, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lang_lbl, "Language on the display. The board restarts to change it.");
  dd_lang = lv_dropdown_create(row);
  static char lang_names[MAX_LANGS * 24];  // each name in its own language
  int nl = lang_list(&lang_codes[0][0], MAX_LANGS, lang_names, sizeof(lang_names));
  (lv_dropdown_set_options)(dd_lang, lang_names);  // not translated
  for (int i = 0; i < nl; i++)
    if (!strcmp(lang_codes[i], ui_lang)) lv_dropdown_set_selected(dd_lang, i);
  lv_obj_add_event_cb(dd_lang, lang_cb, LV_EVENT_VALUE_CHANGED, NULL);

  /* ---- Display ---- */
  sec = make_section(p_sys, LV_SYMBOL_IMAGE "  Display");
  lbl_bl = lv_label_create(sec);
  sl_bl = make_slider(sec, 5, 100, bl_normal, bl_slider_cb);  // min 5% so the screen never goes black
  lbl_bl_saver = lv_label_create(sec);
  sl_bl_saver = make_slider(sec, 0, 100, bl_saver, bl_slider_cb);
  update_bl_labels();

  /* ---- Sensor scan interval ---- */
  sec = make_section(p_sens, LV_SYMBOL_REFRESH "  Sensor scan interval");
  lbl_scan = lv_label_create(sec);
  make_slider(sec, 1, 10, scan_interval_s, scan_slider_cb);
  update_scan_label();

  settings_i2c(p_ctrl);

  /* ---- SD card ---- */
  sec = make_section(p_sys, LV_SYMBOL_SD_CARD "  SD card");
  make_switch_row(sec, "Write the log to the card", feat_sdlog, sdlog_cb);

  lbl_sdlog = lv_label_create(sec);
  lv_obj_set_width(lbl_sdlog, LV_PCT(100));
  lv_label_set_long_mode(lbl_sdlog, LV_LABEL_LONG_WRAP);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SD_CARD " Mount", sd_mount_cb);
  make_btn(row, LV_SYMBOL_EJECT " Eject", sd_eject_cb);
  make_btn(row, LV_SYMBOL_REFRESH, sd_refresh_cb);

  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SETTINGS " Probe card", [](lv_event_t *e) {
    sd_log_probe();
    update_sdlog_label();
  });
  make_btn(row, LV_SYMBOL_FILE " New log", sd_newfile_cb);
  lv_obj_t *del_logs = make_btn(row, LV_SYMBOL_TRASH " Delete old logs", sd_delete_cb);
  lv_obj_set_style_bg_color(del_logs, lv_palette_main(LV_PALETTE_RED), 0);

  /* measurements as CSV */
  make_switch_row(sec, "Log measurements to /data.csv", feat_csv, csv_cb);
  row = make_row(sec, LV_FLEX_ALIGN_START);
  lv_label_set_text(lv_label_create(row), "Every");
  dd_csv = lv_dropdown_create(row);
  lv_dropdown_set_options_static(dd_csv, "1 min\n5 min\n15 min\n60 min");
  lv_dropdown_set_selected(dd_csv, csv_interval_min == 1 ? 0 : csv_interval_min == 5 ? 1
                                                            : csv_interval_min == 15  ? 2
                                                                                      : 3);
  lv_obj_add_event_cb(dd_csv, csv_interval_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lbl_csv = lv_label_create(sec);
  lv_obj_set_width(lbl_csv, LV_PCT(100));
  lv_label_set_long_mode(lbl_csv, LV_LABEL_LONG_WRAP);
  update_csv_label();

  /* settings backup */
  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SAVE " Back up settings", backup_cb);
  make_btn(row, LV_SYMBOL_UPLOAD " Restore", restore_cb);

  lv_obj_t *sd_hint = make_grey_label(sec);
  lv_obj_set_width(sd_hint, LV_PCT(100));
  lv_label_set_long_mode(sd_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(sd_hint, "Eject before pulling the card out. Files can be downloaded from the web page under /files. The backup contains WiFi and Victron keys, so keep the card safe. Restoring restarts the board.");
  update_sdlog_label();

  /* ---- Power ---- */
  sec = make_section(p_sys, LV_SYMBOL_BATTERY_2 "  Power");
  make_switch_row(sec, "Slow the CPU while the screen sleeps", feat_powersave, powersave_cb);

  lbl_lipo = lv_label_create(sec);
  lv_obj_set_width(lbl_lipo, LV_PCT(100));
  lv_label_set_long_mode(lbl_lipo, LV_LABEL_LONG_WRAP);
  update_lipo_label(false);  // ask the chip only when the button is tapped
  make_btn(sec, LV_SYMBOL_REFRESH " Check battery", lipo_refresh_cb);

  make_switch_row(sec, "Keep running on the battery when power is lost", feat_battmode, battmode_cb);
  lbl_battmode = lv_label_create(sec);
  lv_obj_set_width(lbl_battmode, LV_PCT(100));
  lv_label_set_long_mode(lbl_battmode, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lv_label_create(sec), "Shut down at");
  sl_battpct = make_slider(sec, 5, 60, batt_shutdown_pct, battpct_cb);
  update_battmode_label();
  lv_obj_t *pwr_hint = make_grey_label(sec);
  lv_obj_set_width(pwr_hint, LV_PCT(100));
  lv_label_set_long_mode(pwr_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(pwr_hint, "The web page keeps working under the screen saver: WiFi modem sleep is used only while the web server is off, and a visit brings the CPU back to full speed. Tabs that are not on screen are not redrawn.");

  /* ---- Starting screen ---- */
  sec = make_section(p_sys, LV_SYMBOL_EYE_OPEN "  Starting screen");
  lv_obj_t *splash_hint = make_grey_label(sec);
  lv_obj_set_width(splash_hint, LV_PCT(100));
  lv_label_set_long_mode(splash_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(splash_hint, "Shown while the board starts. Set the time to 0 to skip it.");

  row = make_row(sec, LV_FLEX_ALIGN_START);
  ta_splash = make_ta(row, "Text", splash_save_now);
  lv_obj_set_flex_grow(ta_splash, 1);
  lv_textarea_set_max_length(ta_splash, 19);
  lv_textarea_set_text(ta_splash, splash_cfg.text);
  dd_splash_size = lv_dropdown_create(row);
  lv_dropdown_set_options_static(dd_splash_size, "20\n28\n32\n48");
  lv_dropdown_set_selected(dd_splash_size, splash_cfg.size == 20 ? 0 : splash_cfg.size == 28 ? 1
                                                                  : splash_cfg.size == 32   ? 2
                                                                                            : 3);
  lv_obj_add_event_cb(dd_splash_size, splash_cb, LV_EVENT_VALUE_CHANGED, NULL);
  make_btn(row, LV_SYMBOL_SAVE, splash_cb);

  lbl_splash_prev = lv_label_create(sec);
  splash_preview();

  lv_label_set_text(lv_label_create(sec), "Red");
  sl_splash_r = make_slider(sec, 0, 255, splash_cfg.r, splash_cb);
  lv_label_set_text(lv_label_create(sec), "Green");
  sl_splash_g = make_slider(sec, 0, 255, splash_cfg.g, splash_cb);
  lv_label_set_text(lv_label_create(sec), "Blue");
  sl_splash_b = make_slider(sec, 0, 255, splash_cfg.b, splash_cb);
  lv_label_set_text(lv_label_create(sec), "Seconds shown");
  sl_splash_sec = make_slider(sec, 0, 15, splash_cfg.seconds, splash_cb);

  /* ---- Alarms ---- */
  sec = make_section(p_sys, LV_SYMBOL_WARNING "  Alarms");
  make_switch_row(sec, "Show warnings and alarms", alarm_cfg.enabled, alarm_enable_cb);
  make_switch_row(sec, "A new alarm wakes the screen", alarm_cfg.wake_saver, alarm_wake_cb);
  make_switch_row(sec, "Warn when the SD card is missing", alarm_cfg.warn_sd, alarm_sd_cb);
  lv_obj_t *alarm_hint = make_grey_label(sec);
  lv_obj_set_width(alarm_hint, LV_PCT(100));
  lv_label_set_long_mode(alarm_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(alarm_hint, "Temperatures are never alarmed on: the caravan may be left unheated and a tag can sit outside or in a fridge.");

  lbl_alarm = lv_label_create(sec);
  lv_obj_set_width(lbl_alarm, LV_PCT(100));
  lv_label_set_long_mode(lbl_alarm, LV_LABEL_LONG_WRAP);

  lv_label_set_text(lv_label_create(sec), "Battery warning %");
  sl_soc_warn = make_slider(sec, 10, 90, alarm_cfg.soc_warn, alarm_slider_cb);
  lv_label_set_text(lv_label_create(sec), "Battery alarm %");
  sl_soc_alarm = make_slider(sec, 5, 50, alarm_cfg.soc_alarm, alarm_slider_cb);
  lv_label_set_text(lv_label_create(sec), "Wind warning m/s (0 = off)");
  sl_wind = make_slider(sec, 0, 25, alarm_cfg.wind_warn, alarm_slider_cb);
  update_alarm_label();

  /* ---- Debug ---- */
  sec = make_section(p_sys, LV_SYMBOL_SETTINGS "  Debug");
  make_switch_row(sec, "Serial monitor: log everything to USB serial", feat_serial, serial_cb);
  make_switch_row(sec, "Log memory, CPU and battery", feat_perflog, perflog_cb);
  lv_obj_t *dbg_hint = make_grey_label(sec);
  lv_obj_set_width(dbg_hint, LV_PCT(100));
  lv_label_set_long_mode(dbg_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(dbg_hint, "Off: only faults (errors and warnings) are written to USB serial. "
                              "The log on the web page (/log) and on the SD card always gets everything.");

  /* ---- About ---- */
  sec = make_section(p_sys, LV_SYMBOL_HOME "  About");
  lv_obj_t *about = make_grey_label(sec);
  lv_obj_set_width(about, LV_PCT(100));
  lv_label_set_long_mode(about, LV_LABEL_LONG_WRAP);
  /* LVGL's built-in fonts have no (c) sign and no a-umlaut, so plain ASCII here */
  lv_label_set_text(about,
                    "Version " FW_VERSION "  -  built " __DATE__ " " __TIME__ "\n"
                    "(c) Fredrik Rudin\n"
                    "github.com/fredrikrudin/esp32-S3-ws4-caravan\n"
                    "Written with the help of Claude (Anthropic AI)\n"
                    "License: CC BY-NC 4.0 - non-commercial use only");

  /* ---- System ---- */
  sec = make_section(p_sys, LV_SYMBOL_POWER "  System");
  row = make_row(sec, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_REFRESH " Restart", reboot_cb);
  make_btn(row, LV_SYMBOL_POWER " Shut down", shutdown_cb);
  lv_obj_t *reset_btn = make_btn(row, LV_SYMBOL_TRASH " Full reset", factory_cb);
  lv_obj_set_style_bg_color(reset_btn, lv_palette_main(LV_PALETTE_RED), 0);

  lv_obj_t *sys_hint = make_grey_label(sec);
  lv_obj_set_width(sys_hint, LV_PCT(100));
  lv_label_set_long_mode(sys_hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(sys_hint, "Each asks for confirmation first. Back up your settings to the card before a full reset.");
}
