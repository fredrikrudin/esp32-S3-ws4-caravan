/* Screen saver: dim clock + lower backlight after SAVER_TIMEOUT_MS without touch.
   It is a separate LVGL screen, so the main UI isn't redrawn while it shows. */
#include "app.h"

static lv_obj_t *main_scr, *saver_scr, *saver_box, *saver_clock, *saver_date;

static void saver_wake_cb(lv_event_t *e) {
  set_backlight(bl_normal);
  lv_scr_load(main_scr);
  lv_disp_trig_activity(NULL);
}

void build_saver() {
  main_scr = lv_scr_act();

  saver_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(saver_scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(saver_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(saver_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(saver_scr, saver_wake_cb, LV_EVENT_PRESSED, NULL);  // the waking tap goes nowhere else

  saver_box = lv_obj_create(saver_scr);
  lv_obj_remove_style_all(saver_box);
  lv_obj_set_size(saver_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(saver_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(saver_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_text_color(saver_box, lv_color_hex(SAVER_COLOR), 0);
  lv_obj_clear_flag(saver_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(saver_box);

  saver_clock = lv_label_create(saver_box);
  lv_obj_set_style_text_font(saver_clock, FONT_CLOCK, 0);
  lv_label_set_text(saver_clock, "--:--");
  saver_date = lv_label_create(saver_box);
  lv_label_set_text(saver_date, "");
}

void saver_timer_cb(lv_timer_t *t) {
  bool active = (lv_scr_act() == saver_scr);
  if (!active) {
    if (lv_disp_get_inactive_time(NULL) < SAVER_TIMEOUT_MS) return;
    kb_hide();
    lv_scr_load(saver_scr);
    set_backlight(bl_saver);
  }

  char tb[16], db[64];
  format_local_time(tb, db);
  if (strcmp(lv_label_get_text(saver_clock), tb)) {
    lv_label_set_text(saver_clock, tb);
    /* move the clock a little every minute so nothing stays in one place */
    lv_obj_align(saver_box, LV_ALIGN_CENTER, (int)(esp_random() % 121) - 60, (int)(esp_random() % 161) - 80);
  }
  set_label(saver_date, db);
}
