/* UI core: tabview, on-screen keyboard, widget helpers, timers.
   All LVGL code runs in the Arduino loop task only. */
#include "app.h"

lv_obj_t *tab_power, *tab_relays, *tab_temp, *tab_weather, *tab_settings, *kb;
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

void set_label(lv_obj_t *l, const char *txt) {
  if (strcmp(lv_label_get_text(l), txt)) lv_label_set_text(l, txt);
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
  tab_power = lv_tabview_add_tab(tv, "Power");
  tab_relays = lv_tabview_add_tab(tv, "Relays");
  tab_temp = lv_tabview_add_tab(tv, "Temp");
  tab_weather = lv_tabview_add_tab(tv, "Weather");
  tab_settings = lv_tabview_add_tab(tv, LV_SYMBOL_SETTINGS);  // gear icon
  settings_pad_bottom = lv_obj_get_style_pad_bottom(tab_settings, LV_PART_MAIN);

  build_power_tab();
  build_relays_tab();
  build_temp_tab();
  build_weather_tab();
  build_settings_tab();

  kb = lv_keyboard_create(lv_scr_act());
  lv_obj_set_size(kb, LV_PCT(100), KB_H);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, hex_kb_map, hex_kb_ctrl);

  build_saver();  // must come after the main screen is complete

  lv_timer_create(clock_timer_cb, 500, NULL);
  lv_timer_create(net_poll_cb, 200, NULL);
  lv_timer_create(ruuvi_timer_cb, 1000, NULL);
  lv_timer_create(power_timer_cb, 1000, NULL);
  lv_timer_create(saver_timer_cb, 500, NULL);
}
