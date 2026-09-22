/* Relays tab (PCF8574) + Settings sections for the relay board and I2C scan.
   Relays marked "hold to switch" only toggle after the button is held for
   RELAY_HOLD_MS; the button is orange while it is being held. */
#include "app.h"

static lv_obj_t *lbl_relay_status, *relay_grid;
static lv_obj_t *relay_btn[MAX_RELAYS], *relay_lbl[MAX_RELAYS];
static lv_obj_t *dd_pcf_addr, *sw_active_low, *dd_relay_count, *dd_relay_pick, *ta_relay_name, *lbl_i2c_scan;
static lv_obj_t *sw_relay_hold;
static uint32_t hold_start[MAX_RELAYS];  // millis() when a hold began, 0 = not holding

#define LV_STATE_HOLDING LV_STATE_USER_1  // custom state: button is being held (orange)

/* ---------- Relays tab ---------- */
static void relay_refresh_ui() {
  for (int i = 0; i < MAX_RELAYS; i++) {
    if (!relay_btn[i]) continue;
    bool on = (relay_state >> i) & 1;
    if (on) lv_obj_add_state(relay_btn[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(relay_btn[i], LV_STATE_CHECKED);
    if (hold_start[i]) lv_label_set_text_fmt(relay_lbl[i], "%s\nHold...", relay_cfg.names[i]);
    else lv_label_set_text_fmt(relay_lbl[i], "%s\n%s", relay_cfg.names[i], on ? "ON" : "OFF");
  }
  if (!relay_cfg.addr) lv_label_set_text(lbl_relay_status, "No relay board set up.\nChoose its I2C address under Settings.");
  else if (!pcf_ok) lv_label_set_text_fmt(lbl_relay_status, LV_SYMBOL_WARNING " No answer from PCF8574 at 0x%02X", relay_cfg.addr);
  else lv_label_set_text_fmt(lbl_relay_status, "PCF8574 at 0x%02X", relay_cfg.addr);
}

static void relay_toggle(int i) {
  uint8_t old = relay_state;
  relay_state ^= (1 << i);
  if (!relay_apply()) relay_state = old;  // write failed: keep showing the real state
}

static void hold_end(int i) {
  hold_start[i] = 0;
  if (relay_btn[i]) lv_obj_clear_state(relay_btn[i], LV_STATE_HOLDING);
}

static void relay_btn_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  bool hold = (relay_hold_mask >> i) & 1;

  if (code == LV_EVENT_CLICKED && !hold) {
    relay_toggle(i);  // normal relay: a tap switches it
    relay_refresh_ui();
  } else if (code == LV_EVENT_PRESSED && hold) {
    hold_start[i] = millis() | 1;  // start holding: orange until the time is up
    lv_obj_add_state(relay_btn[i], LV_STATE_HOLDING);
    relay_refresh_ui();
  } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && hold_start[i]) {
    hold_end(i);  // let go too early: nothing happens
    relay_refresh_ui();
  }
}

/* Checks the buttons being held; switches the relay when the time is up */
static void relay_hold_timer_cb(lv_timer_t *t) {
  for (int i = 0; i < MAX_RELAYS; i++) {
    if (!hold_start[i] || millis() - hold_start[i] < RELAY_HOLD_MS) continue;
    hold_end(i);
    relay_toggle(i);
    relay_refresh_ui();
  }
}

static void relay_rebuild_tab() {
  lv_obj_clean(relay_grid);
  for (int i = 0; i < MAX_RELAYS; i++) {
    relay_btn[i] = relay_lbl[i] = NULL;
    hold_start[i] = 0;
  }
  if (relay_cfg.addr) {
    for (int i = 0; i < relay_cfg.count; i++) {
      lv_obj_t *b = lv_btn_create(relay_grid);
      lv_obj_set_size(b, LV_PCT(48), 78);
      lv_obj_set_style_bg_color(b, lv_color_hex(0x3A3F44), 0);
      lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_GREEN), LV_STATE_CHECKED);
      lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_ORANGE), LV_STATE_HOLDING);
      lv_obj_add_event_cb(b, relay_btn_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
      lv_obj_t *l = lv_label_create(b);
      lv_obj_set_width(l, LV_PCT(100));
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
      lv_obj_center(l);
      relay_btn[i] = b;
      relay_lbl[i] = l;
    }
  }
  relay_refresh_ui();
}

void build_relays_tab() {
  lv_obj_set_flex_flow(tab_relays, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab_relays, 10, 0);
  lbl_relay_status = make_grey_label(tab_relays);

  relay_grid = lv_obj_create(tab_relays);
  lv_obj_remove_style_all(relay_grid);
  lv_obj_set_size(relay_grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(relay_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(relay_grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(relay_grid, 10, 0);
  lv_obj_clear_flag(relay_grid, LV_OBJ_FLAG_SCROLLABLE);
  relay_rebuild_tab();
  lv_timer_create(relay_hold_timer_cb, 50, NULL);
}

/* ---------- Settings: relay board ---------- */
static const char *PCF_ADDR_OPTS =
  "Not used\n0x20\n0x21\n0x22\n0x23\n0x24\n0x25\n0x26\n0x27\n"
  "0x38\n0x39\n0x3A\n0x3B\n0x3C\n0x3D\n0x3E\n0x3F";

static uint8_t pcf_index_to_addr(uint16_t i) {
  if (i == 0) return 0;
  return i <= 8 ? 0x20 + (i - 1) : 0x38 + (i - 9);
}

static uint16_t pcf_addr_to_index(uint8_t a) {
  if (a >= 0x20 && a <= 0x27) return a - 0x20 + 1;
  if (a >= 0x38 && a <= 0x3F) return a - 0x38 + 9;
  return 0;
}

static void relay_cfg_changed() {
  relay_rebuild_tab();
  cmd_save_relay = true;
}

static void pcf_addr_cb(lv_event_t *e) {
  LOCK();
  relay_cfg.addr = pcf_index_to_addr(lv_dropdown_get_selected(dd_pcf_addr));
  UNLOCK();
  relay_read_back();  // pick up whatever the relays are doing now
  relay_cfg_changed();
}

static void active_low_cb(lv_event_t *e) {
  LOCK();
  relay_cfg.active_low = lv_obj_has_state(sw_active_low, LV_STATE_CHECKED);
  UNLOCK();
  relay_read_back();  // same pins, new meaning of ON/OFF
  relay_cfg_changed();
}

static void relay_count_cb(lv_event_t *e) {
  LOCK();
  relay_cfg.count = lv_dropdown_get_selected(dd_relay_count) + 1;
  UNLOCK();
  relay_state &= relay_mask();
  relay_apply();  // relays no longer in use are switched off
  relay_cfg_changed();
}

static void update_hold_switch() {
  int i = lv_dropdown_get_selected(dd_relay_pick);
  if ((relay_hold_mask >> i) & 1) lv_obj_add_state(sw_relay_hold, LV_STATE_CHECKED);
  else lv_obj_clear_state(sw_relay_hold, LV_STATE_CHECKED);
}

static void relay_pick_cb(lv_event_t *e) {
  lv_textarea_set_text(ta_relay_name, relay_cfg.names[lv_dropdown_get_selected(dd_relay_pick)]);
  update_hold_switch();
}

static void relay_hold_cb(lv_event_t *e) {
  int i = lv_dropdown_get_selected(dd_relay_pick);
  LOCK();
  if (lv_obj_has_state(sw_relay_hold, LV_STATE_CHECKED)) relay_hold_mask |= (1 << i);
  else relay_hold_mask &= ~(1 << i);
  UNLOCK();
  relay_cfg_changed();
}

static void relay_rename_now() {
  int i = lv_dropdown_get_selected(dd_relay_pick);
  const char *n = lv_textarea_get_text(ta_relay_name);
  LOCK();
  if (n[0]) strlcpy(relay_cfg.names[i], n, sizeof(relay_cfg.names[i]));
  else snprintf(relay_cfg.names[i], sizeof(relay_cfg.names[i]), "Relay %d", i + 1);
  UNLOCK();
  kb_hide();
  relay_cfg_changed();
}

static void relay_rename_cb(lv_event_t *e) {
  relay_rename_now();
}

void settings_relays(lv_obj_t *parent) {
  make_heading(parent, LV_SYMBOL_POWER "  Relay board (PCF8574)");

  lv_obj_t *row = make_row(parent, LV_FLEX_ALIGN_START);
  lv_label_set_text(lv_label_create(row), "I2C address");
  dd_pcf_addr = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_pcf_addr, 1);
  lv_dropdown_set_options_static(dd_pcf_addr, PCF_ADDR_OPTS);
  lv_dropdown_set_selected(dd_pcf_addr, pcf_addr_to_index(relay_cfg.addr));
  lv_obj_add_event_cb(dd_pcf_addr, pcf_addr_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = make_row(parent, LV_FLEX_ALIGN_SPACE_BETWEEN);
  lv_label_set_text(lv_label_create(row), "Active low (most relay boards)");
  sw_active_low = lv_switch_create(row);
  if (relay_cfg.active_low) lv_obj_add_state(sw_active_low, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw_active_low, active_low_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = make_row(parent, LV_FLEX_ALIGN_START);
  lv_label_set_text(lv_label_create(row), "Number of relays");
  dd_relay_count = lv_dropdown_create(row);
  lv_dropdown_set_options_static(dd_relay_count, "1\n2\n3\n4\n5\n6\n7\n8");
  lv_dropdown_set_selected(dd_relay_count, relay_cfg.count - 1);
  lv_obj_add_event_cb(dd_relay_count, relay_count_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = make_row(parent, LV_FLEX_ALIGN_START);
  dd_relay_pick = lv_dropdown_create(row);
  lv_obj_set_width(dd_relay_pick, 70);
  lv_dropdown_set_options_static(dd_relay_pick, "1\n2\n3\n4\n5\n6\n7\n8");
  lv_obj_add_event_cb(dd_relay_pick, relay_pick_cb, LV_EVENT_VALUE_CHANGED, NULL);
  ta_relay_name = make_ta(row, "Relay name", relay_rename_now);
  lv_obj_set_flex_grow(ta_relay_name, 1);
  lv_textarea_set_max_length(ta_relay_name, 19);
  lv_textarea_set_text(ta_relay_name, relay_cfg.names[0]);
  make_btn(row, LV_SYMBOL_OK, relay_rename_cb);

  row = make_row(parent, LV_FLEX_ALIGN_SPACE_BETWEEN);
  char txt[40];
  snprintf(txt, sizeof(txt), "Hold to switch (%.1f s)", RELAY_HOLD_MS / 1000.0f);  // LVGL's printf has no floats
  lv_label_set_text(lv_label_create(row), txt);
  sw_relay_hold = lv_switch_create(row);
  lv_obj_add_event_cb(sw_relay_hold, relay_hold_cb, LV_EVENT_VALUE_CHANGED, NULL);
  update_hold_switch();
}

/* ---------- Settings: I2C scan ---------- */
static void i2c_scan_cb(lv_event_t *e) {
  char out[512];
  int n = i2c_scan(out, sizeof(out));
  lv_label_set_text(lbl_i2c_scan, n ? out : "No I2C devices found");
}

void settings_i2c(lv_obj_t *parent) {
  make_heading(parent, LV_SYMBOL_LIST "  I2C devices");
  make_btn(parent, LV_SYMBOL_REFRESH " Scan I2C bus", i2c_scan_cb);
  lbl_i2c_scan = lv_label_create(parent);
  lv_label_set_text(lbl_i2c_scan, "");
}
