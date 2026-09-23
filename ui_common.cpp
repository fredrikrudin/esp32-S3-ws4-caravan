/* UI core: tabview, on-screen keyboard, widget helpers, timers.
   All LVGL code runs in the Arduino loop task only. */
#include "app.h"

static lv_obj_t *tabview;
lv_obj_t *tab_home, *tab_power, *tab_battery, *tab_relays, *tab_shelly, *tab_temp, *tab_weather, *tab_settings, *kb;
static lv_coord_t settings_pad_bottom = 0;

/* Hex keypad for the Victron encryption key */
static const char *hex_kb_map[] = { "1", "2", "3", "4", "5", "6", "\n",
                                    "7", "8", "9", "0", "A", "B", "\n",
                                    "C", "D", "E", "F", LV_SYMBOL_BACKSPACE, "\n",
                                    LV_SYMBOL_KEYBOARD, LV_SYMBOL_OK, "" };
static const lv_btnmatrix_ctrl_t hex_kb_ctrl[] = {
  1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
  LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

/* ---------- keyboard ---------- */
void kb_show(lv_obj_t *ta) {
  // text areas made with hex = true get the hex keypad
  lv_keyboard_set_mode(kb, lv_obj_has_flag(ta, LV_OBJ_FLAG_USER_1) ? LV_KEYBOARD_MODE_USER_1 : LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_pad_bottom(tab_settings, KB_H, 0);  // room to scroll above the keyboard
  lv_obj_update_layout(tab_settings);
  lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

void kb_hide() {
  lv_keyboard_set_textarea(kb, NULL);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_pad_bottom(tab_settings, settings_pad_bottom, 0);
}

/* The OK key calls the on_ready function given to make_ta() */
static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    kb_show(ta);
  } else if (code == LV_EVENT_DEFOCUSED) {
    kb_hide();
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    kb_hide();
    lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    if (code == LV_EVENT_READY) {
      void (*on_ready)() = reinterpret_cast<void (*)()>(lv_event_get_user_data(e));
      if (on_ready) on_ready();
    }
  }
}

/* ---------- widget helpers ---------- */
lv_obj_t *make_row(lv_obj_t *parent, lv_flex_align_t main_align) {
  lv_obj_t *r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 10, 0);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  return r;
}

lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

lv_obj_t *make_ta(lv_obj_t *parent, const char *placeholder, void (*on_ready)(), bool hex) {
  lv_obj_t *ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  if (hex) lv_obj_add_flag(ta, LV_OBJ_FLAG_USER_1);
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, reinterpret_cast<void *>(on_ready));
  return ta;
}

lv_obj_t *make_heading(lv_obj_t *parent, const char *txt) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_BLUE), 0);
  return l;
}

/* A Settings section: a card with a heading, so the page reads as groups */
lv_obj_t *make_section(lv_obj_t *parent, const char *title) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_style_pad_row(card, 10, 0);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *h = lv_label_create(card);
  lv_label_set_text(h, title);
  lv_obj_set_style_text_color(h, lv_palette_main(LV_PALETTE_BLUE), 0);
  return card;
}

/* Label on the left, switch on the right */
lv_obj_t *make_switch_row(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb) {
  lv_obj_t *row = make_row(parent, LV_FLEX_ALIGN_SPACE_BETWEEN);
  lv_obj_t *l = lv_label_create(row);
  lv_obj_set_flex_grow(l, 1);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l, text);
  lv_obj_t *sw = lv_switch_create(row);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
  return sw;
}

lv_obj_t *make_grey_label(lv_obj_t *parent) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_label_set_text(l, "");
  return l;
}

lv_obj_t *make_slider(lv_obj_t *parent, int min, int max, int val, lv_event_cb_t cb) {
  lv_obj_t *row = make_row(parent, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_hor(row, 14, 0);  // room for the knob at both ends
  lv_obj_set_style_pad_ver(row, 8, 0);
  lv_obj_t *sl = lv_slider_create(row);
  lv_obj_set_flex_grow(sl, 1);
  lv_slider_set_range(sl, min, max);
  lv_slider_set_value(sl, val, LV_ANIM_OFF);
  lv_obj_clear_flag(sl, LV_OBJ_FLAG_SCROLL_CHAIN);  // dragging must not swipe the tabs
  lv_obj_add_event_cb(sl, cb, LV_EVENT_ALL, NULL);
  return sl;
}

/* Segmented Off | On control, in the style of the Victron switch pane.
   The caller gets the button matrix; use set_segment() to show the state. */
static const char *seg_map[] = { "Off", "On", "" };

lv_obj_t *make_segment(lv_obj_t *parent, lv_event_cb_t cb, void *user_data) {
  lv_obj_t *bm = lv_btnmatrix_create(parent);
  lv_btnmatrix_set_map(bm, seg_map);
  lv_obj_set_size(bm, LV_PCT(100), 52);
  lv_btnmatrix_set_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_NO_REPEAT);
  lv_btnmatrix_set_one_checked(bm, true);

  lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bm, 0, 0);
  lv_obj_set_style_pad_all(bm, 0, 0);
  lv_obj_set_style_pad_column(bm, 0, 0);

  lv_obj_set_style_radius(bm, 8, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, lv_color_hex(0x0E2233), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_border_width(bm, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_color(bm, lv_color_hex(0x2F6FB5), LV_PART_ITEMS);
  lv_obj_set_style_text_color(bm, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, lv_color_hex(0x2F6FB5), LV_PART_ITEMS | LV_STATE_CHECKED);

  lv_obj_add_event_cb(bm, cb, LV_EVENT_VALUE_CHANGED, user_data);
  return bm;
}

void set_segment(lv_obj_t *seg, bool on) {
  lv_btnmatrix_clear_btn_ctrl(seg, on ? 0 : 1, LV_BTNMATRIX_CTRL_CHECKED);
  lv_btnmatrix_set_btn_ctrl(seg, on ? 1 : 0, LV_BTNMATRIX_CTRL_CHECKED);
}

void set_label(lv_obj_t *l, const char *txt) {
  if (strcmp(lv_label_get_text(l), txt)) lv_label_set_text(l, txt);
}

/* Tabs of switched-off features are hidden: their button is not drawn and is
   squeezed to a sliver, and if you were on such a tab you land on Home. */
void ui_update_tabs() {
  if (!tabview) return;
  lv_obj_t *btns = lv_tabview_get_tab_btns(tabview);
  const struct {
    uint16_t id;
    volatile bool *feature;
  } tabs[] = {
    { 2, &feat_bms },     // Battery
    { 3, &feat_relays },  // Relays
    { 4, &feat_shelly },  // Shelly
    { 5, &feat_ruuvi },   // Temp
  };

  uint16_t act = lv_tabview_get_tab_act(tabview);
  bool act_hidden = false;
  for (auto &t : tabs) {
    if (*t.feature) {
      lv_btnmatrix_clear_btn_ctrl(btns, t.id, LV_BTNMATRIX_CTRL_HIDDEN);
      lv_btnmatrix_clear_btn_ctrl(btns, t.id, LV_BTNMATRIX_CTRL_DISABLED);
      lv_btnmatrix_set_btn_width(btns, t.id, 10);
    } else {
      lv_btnmatrix_set_btn_ctrl(btns, t.id, LV_BTNMATRIX_CTRL_HIDDEN);
      lv_btnmatrix_set_btn_ctrl(btns, t.id, LV_BTNMATRIX_CTRL_DISABLED);
      lv_btnmatrix_set_btn_width(btns, t.id, 1);  // a sliver instead of a full-width gap
      if (act == t.id) act_hidden = true;
    }
  }
  /* the remaining tabs share the width evenly */
  const uint16_t always_on[] = { 0, 1, 6, 7 };
  for (uint16_t id : always_on) lv_btnmatrix_set_btn_width(btns, id, 10);

  if (act_hidden) lv_tabview_set_act(tabview, 0, LV_ANIM_OFF);
}

/* ---------- build everything ---------- */
void build_ui() {
  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t *th = lv_theme_default_init(disp,
                                         lv_palette_main(LV_PALETTE_BLUE),
                                         lv_palette_main(LV_PALETTE_RED),
                                         true,  // dark mode
                                         LV_FONT_DEFAULT);
  lv_disp_set_theme(disp, th);

  lv_obj_t *tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 50);
  tabview = tv;
  tab_home = lv_tabview_add_tab(tv, LV_SYMBOL_HOME);  // start page
  tab_power = lv_tabview_add_tab(tv, "Power");
  tab_battery = lv_tabview_add_tab(tv, "Battery");
  tab_relays = lv_tabview_add_tab(tv, "Relays");
  tab_shelly = lv_tabview_add_tab(tv, "Shelly");
  tab_temp = lv_tabview_add_tab(tv, "Temp");
  tab_weather = lv_tabview_add_tab(tv, "Weather");
  tab_settings = lv_tabview_add_tab(tv, LV_SYMBOL_SETTINGS);  // gear icon
  settings_pad_bottom = lv_obj_get_style_pad_bottom(tab_settings, LV_PART_MAIN);

  build_home_tab();
  build_power_tab();
  build_battery_tab();
  build_relays_tab();
  build_shelly_tab();
  build_temp_tab();
  build_weather_tab();
  build_settings_tab();

  kb = lv_keyboard_create(lv_scr_act());
  lv_obj_set_size(kb, LV_PCT(100), KB_H);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, hex_kb_map, hex_kb_ctrl);

  ui_update_tabs();  // hide the tabs of features that are switched off

  build_saver();  // must come after the main screen is complete

  lv_timer_create(clock_timer_cb, 500, NULL);
  lv_timer_create(net_poll_cb, 200, NULL);
  lv_timer_create(ruuvi_timer_cb, 1000, NULL);
  lv_timer_create(home_timer_cb, 1000, NULL);
  lv_timer_create(power_timer_cb, 1000, NULL);
  lv_timer_create(battery_timer_cb, 1000, NULL);
  lv_timer_create(shelly_timer_cb, 1000, NULL);
  lv_timer_create(saver_timer_cb, 500, NULL);
  lv_timer_create(history_timer_cb, 5000, NULL);
}
