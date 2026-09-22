/* Screen saver: dim clock + battery state of charge, lower backlight,
   after SAVER_TIMEOUT_MS without touch.
   It is a separate LVGL screen, so the main UI isn't redrawn while it shows. */
#include "app.h"

#define SAVER_BAR_BG 0x202020   // empty part of the SOC bar
#define SAVER_LOW_COLOR 0x702020  // bar color at low SOC (dim red)
#define SAVER_LOW_SOC 20          // below this % the bar turns red

static lv_obj_t *main_scr, *saver_scr, *saver_box, *saver_clock, *saver_date;
static lv_obj_t *soc_row, *soc_bar, *soc_label;

/* SOC from the first battery monitor with fresh data. Returns false if none.
   charging: current flowing into the battery */
static bool saver_get_soc(float *soc, bool *charging) {
  uint32_t now = millis();
  bool found = false;
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  for (int i = 0; i < MAX_VIC && !found; i++) {
    const VicData &d = vic_data[i];
    if (vic_cfg[i].used && vic_cfg[i].type == VIC_BATTMON && d.key_ok && vic_fresh(d, now) && !isnan(d.soc)) {
      *soc = d.soc;
      *charging = !isnan(d.batt_i) && d.batt_i > 0.05f;
      found = true;
    }
  }
  xSemaphoreGive(vic_mtx);
  return found;
}

static void saver_update_soc() {
  float soc;
  bool charging;
  if (!saver_get_soc(&soc, &charging)) {
    lv_obj_add_flag(soc_row, LV_OBJ_FLAG_HIDDEN);  // no battery monitor or no data
    return;
  }
  lv_obj_clear_flag(soc_row, LV_OBJ_FLAG_HIDDEN);
  lv_bar_set_value(soc_bar, (int)(soc + 0.5f), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(soc_bar, lv_color_hex(soc < SAVER_LOW_SOC ? SAVER_LOW_COLOR : SAVER_COLOR), LV_PART_INDICATOR);
  char b[24];
  snprintf(b, sizeof(b), "%s%.0f%%", charging ? LV_SYMBOL_CHARGE " " : "", soc);
  set_label(soc_label, b);
}

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

  /* battery state of charge: bar + percentage */
  soc_row = lv_obj_create(saver_box);
  lv_obj_remove_style_all(soc_row);
  lv_obj_set_size(soc_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(soc_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(soc_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(soc_row, 12, 0);
  lv_obj_set_style_pad_top(soc_row, 24, 0);
  lv_obj_clear_flag(soc_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(soc_row, LV_OBJ_FLAG_HIDDEN);

  soc_bar = lv_bar_create(soc_row);
  lv_obj_set_size(soc_bar, 200, 18);
  lv_bar_set_range(soc_bar, 0, 100);
  lv_obj_set_style_bg_color(soc_bar, lv_color_hex(SAVER_BAR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(soc_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(soc_bar, lv_color_hex(SAVER_COLOR), LV_PART_INDICATOR);
  lv_obj_set_style_radius(soc_bar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(soc_bar, 4, LV_PART_INDICATOR);

  soc_label = lv_label_create(soc_row);
  lv_obj_set_style_text_font(soc_label, FONT_BIG, 0);
  lv_label_set_text(soc_label, "");
}

void saver_timer_cb(lv_timer_t *t) {
  bool active = (lv_scr_act() == saver_scr);
  if (!active) {
    if (lv_disp_get_inactive_time(NULL) < SAVER_TIMEOUT_MS) return;
    kb_hide();
    saver_update_soc();  // fill it in before the screen shows
    lv_scr_load(saver_scr);
    set_backlight(bl_saver);
  }

  saver_update_soc();

  char tb[16], db[64];
  format_local_time(tb, db);
  if (strcmp(lv_label_get_text(saver_clock), tb)) {
    lv_label_set_text(saver_clock, tb);
    /* move the clock a little every minute so nothing stays in one place */
    lv_obj_align(saver_box, LV_ALIGN_CENTER, (int)(esp_random() % 121) - 60, (int)(esp_random() % 161) - 80);
  }
  set_label(saver_date, db);
}
