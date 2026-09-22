/* Temp tab: the RuuviTag chosen in Settings */
#include "app.h"

static lv_obj_t *lbl_rv_name, *lbl_rv_temp, *lbl_rv_details, *lbl_rv_info;

void build_temp_tab() {
  lv_obj_set_flex_flow(tab_temp, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_temp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tab_temp, 12, 0);

  lbl_rv_name = make_grey_label(tab_temp);

  lbl_rv_temp = lv_label_create(tab_temp);
  lv_obj_set_style_text_font(lbl_rv_temp, FONT_CLOCK, 0);
  lv_label_set_text(lbl_rv_temp, "--");

  lbl_rv_details = lv_label_create(tab_temp);
  lv_obj_set_style_text_font(lbl_rv_details, FONT_BIG, 0);
  lv_obj_set_style_text_align(lbl_rv_details, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl_rv_details, "");

  lbl_rv_info = make_grey_label(tab_temp);
  lv_obj_set_style_text_align(lbl_rv_info, LV_TEXT_ALIGN_CENTER, 0);
}

/* Once per second: Temp tab + the RuuviTag list in Settings */
void ruuvi_timer_cb(lv_timer_t *t) {
  static RuuviTag copy[MAX_TAGS];
  xSemaphoreTake(ruuvi_mtx, portMAX_DELAY);
  memcpy(copy, tags, sizeof(tags));
  xSemaphoreGive(ruuvi_mtx);

  char sel[18], s[8], b[128];
  LOCK();
  strlcpy(sel, g.ruuvi_sel, sizeof(sel));
  UNLOCK();
  uint32_t now = millis();

  const RuuviTag *tag = NULL;
  for (int i = 0; i < MAX_TAGS; i++)
    if (copy[i].used && !strcmp(copy[i].addr, sel)) tag = &copy[i];

  if (!sel[0]) {
    set_label(lbl_rv_name, "No RuuviTag selected");
    set_label(lbl_rv_temp, "--");
    set_label(lbl_rv_details, "");
    set_label(lbl_rv_info, "Choose a tag under Settings");
  } else {
    mac_short(sel, s);
    snprintf(b, sizeof(b), "Ruuvi %s", s);
    set_label(lbl_rv_name, b);
    if (!tag) {
      set_label(lbl_rv_temp, "--");
      set_label(lbl_rv_details, "");
      set_label(lbl_rv_info, "Waiting for data...");
    } else {
      if (tag->has_temp) snprintf(b, sizeof(b), "%.1f" DEG "C", tag->temp);
      else strcpy(b, "--");
      set_label(lbl_rv_temp, b);

      char hum[24] = "", pres[24] = "";
      if (tag->has_hum) snprintf(hum, sizeof(hum), "%.0f %%RH", tag->hum);
      if (tag->has_pres) snprintf(pres, sizeof(pres), "%.0f hPa", tag->pres);
      snprintf(b, sizeof(b), "%s\n%s", hum, pres);
      set_label(lbl_rv_details, b);

      uint32_t age = (now - tag->last_seen) / 1000;
      char when[32];
      if (age < 120) snprintf(when, sizeof(when), "%lu s ago", (unsigned long)age);
      else snprintf(when, sizeof(when), "No data for %lu min", (unsigned long)(age / 60));
      snprintf(b, sizeof(b), "Battery %.2f V   Signal %d dBm\n%s", tag->batt_mv / 1000.0f, tag->rssi, when);
      set_label(lbl_rv_info, b);
    }
  }

  ruuvi_settings_refresh(copy, now);
}
