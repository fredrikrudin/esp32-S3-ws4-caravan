/* Temp tab: one card per RuuviTag added in Settings (max MAX_RUUVI) */
#include "app.h"

struct RuuviCard {
  lv_obj_t *card, *temp, *name, *details, *info;
};
static RuuviCard cards[MAX_RUUVI];
static lv_obj_t *lbl_rv_empty;

void build_temp_tab() {
  lv_obj_set_flex_flow(tab_temp, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab_temp, 10, 0);

  lbl_rv_empty = make_grey_label(tab_temp);
  lv_obj_set_width(lbl_rv_empty, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_rv_empty, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl_rv_empty, "No RuuviTags added yet.\nAdd up to 3 under Settings.");

  for (int i = 0; i < MAX_RUUVI; i++) {
    RuuviCard &c = cards[i];
    c.card = lv_obj_create(tab_temp);
    lv_obj_set_width(c.card, LV_PCT(100));
    lv_obj_set_flex_grow(c.card, 1);  // cards share the tab height
    lv_obj_set_flex_flow(c.card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(c.card, 12, 0);
    lv_obj_set_style_pad_column(c.card, 16, 0);
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c.card, LV_OBJ_FLAG_HIDDEN);

    c.temp = lv_label_create(c.card);
    lv_obj_set_style_text_font(c.temp, FONT_CLOCK, 0);
    lv_obj_set_width(c.temp, 200);
    lv_label_set_text(c.temp, "--");

    lv_obj_t *col = lv_obj_create(c.card);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    c.name = lv_label_create(col);
    lv_obj_set_width(c.name, LV_PCT(100));
    lv_label_set_long_mode(c.name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(c.name, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(c.name, "");

    c.details = lv_label_create(col);
    lv_label_set_text(c.details, "");

    c.info = make_grey_label(col);
  }
}

/* Once per second: Temp tab + the RuuviTag part of Settings */
void ruuvi_timer_cb(lv_timer_t *t) {
  static RuuviTag copy[MAX_TAGS];
  xSemaphoreTake(ruuvi_mtx, portMAX_DELAY);
  memcpy(copy, tags, sizeof(tags));
  xSemaphoreGive(ruuvi_mtx);

  uint32_t now = millis();
  char b[96];
  int shown = 0;

  if (!feat_ruuvi) {
    for (int i = 0; i < MAX_RUUVI; i++) lv_obj_add_flag(cards[i].card, LV_OBJ_FLAG_HIDDEN);
    set_label(lbl_rv_empty, "Temperature reading is switched off.\nSwitch it on under Settings.");
    lv_obj_clear_flag(lbl_rv_empty, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  for (int i = 0; i < MAX_RUUVI; i++) {
    RuuviCard &c = cards[i];
    if (!ruuvi_cfg[i].used) {
      lv_obj_add_flag(c.card, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_HIDDEN);
    shown++;
    set_label(c.name, ruuvi_cfg[i].name);

    const RuuviTag *tag = NULL;
    for (int k = 0; k < MAX_TAGS; k++)
      if (copy[k].used && !strcmp(copy[k].addr, ruuvi_cfg[i].mac)) tag = &copy[k];

    if (!tag) {
      set_label(c.temp, "--");
      set_label(c.details, "");
      set_label(c.info, "Waiting for data...");
      continue;
    }

    if (tag->has_temp) snprintf(b, sizeof(b), "%.1f" DEG "C", tag->temp);
    else strcpy(b, "--");
    set_label(c.temp, b);

    char hum[24] = "", pres[24] = "";
    if (tag->has_hum) snprintf(hum, sizeof(hum), "%.0f %%RH", tag->hum);
    if (tag->has_pres) snprintf(pres, sizeof(pres), "%.0f hPa", tag->pres);
    snprintf(b, sizeof(b), "%s   %s", hum, pres);
    set_label(c.details, b);

    uint32_t age = (now - tag->last_seen) / 1000;
    char when[32];
    if (age < 120) snprintf(when, sizeof(when), "%lu s ago", (unsigned long)age);
    else snprintf(when, sizeof(when), "No data for %lu min", (unsigned long)(age / 60));
    snprintf(b, sizeof(b), "%.2f V   %d dBm   %s", tag->batt_mv / 1000.0f, tag->rssi, when);
    set_label(c.info, b);
  }

  if (shown) {
    lv_obj_add_flag(lbl_rv_empty, LV_OBJ_FLAG_HIDDEN);
  } else {
    set_label(lbl_rv_empty, "No RuuviTags added yet.\nAdd up to 3 under Settings.");
    lv_obj_clear_flag(lbl_rv_empty, LV_OBJ_FLAG_HIDDEN);
  }

  ruuvi_settings_refresh(copy, now);
}
