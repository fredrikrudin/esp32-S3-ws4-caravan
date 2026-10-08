// esp32-S3-ws4-caravan v1.0
/* Ruuvi tab: one card per RuuviTag added in Settings (max MAX_RUUVI).
 * Left: name, temperature, humidity and pressure, the 7-day high and low.
 * Right: the last 7 days as bars in the style of Victron VRM's charts, the day's
 * low in VRM blue and the day's high in VRM orange, today on the right. */
#include "app.h"

#define COL_LOW 0x4484C4   // VRM blue
#define COL_HIGH 0xF3A246  // VRM orange
#define COL_GRID 0x3A3A3A

struct RuuviCard {
  lv_obj_t *card, *temp, *name, *details, *info, *hilo, *chart;
  lv_chart_series_t *ser_lo, *ser_hi;
  RuuviHist shown;  // what the chart shows now, to redraw only on a change
  bool has_shown;
};
static RuuviCard cards[MAX_RUUVI];
static lv_obj_t *lbl_rv_empty;

/* Axis labels: tenths of a degree on the left, weekdays along the bottom */
static void chart_draw_cb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL) || !dsc->text) return;
  if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
    lv_snprintf(dsc->text, dsc->text_length, "%d" DEG, (int)lroundf(dsc->value / 10.0f));
  } else if (dsc->id == LV_CHART_AXIS_PRIMARY_X) {
    RuuviCard *c = (RuuviCard *)lv_event_get_user_data(e);
    if (!c->has_shown || c->shown.day <= 0) {
      dsc->text[0] = 0;
      return;
    }
    int32_t day = c->shown.day - (RUUVI_DAYS - 1 - dsc->value);
    int monday_first = (int)((day + 3) % 7);  // 1 Jan 1970 was a Thursday
    lv_snprintf(dsc->text, dsc->text_length, "%s", tr_day_short(monday_first));
  }
}

static lv_obj_t *make_chart(lv_obj_t *parent, RuuviCard &c) {
  /* the chart sits in a box whose padding leaves room for the axis labels */
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_height(box, LV_PCT(100));
  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_style_pad_left(box, 34, 0);
  lv_obj_set_style_pad_bottom(box, 20, 0);
  lv_obj_set_style_pad_top(box, 6, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *ch = lv_chart_create(box);
  lv_obj_set_size(ch, LV_PCT(100), LV_PCT(100));
  lv_chart_set_type(ch, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(ch, RUUVI_DAYS);
  lv_chart_set_div_line_count(ch, 3, 0);
  lv_obj_set_style_bg_opa(ch, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ch, 0, 0);
  lv_obj_set_style_radius(ch, 0, 0);
  lv_obj_set_style_pad_all(ch, 0, 0);
  lv_obj_set_style_line_color(ch, lv_color_hex(COL_GRID), LV_PART_MAIN);
  lv_obj_set_style_pad_column(ch, 6, LV_PART_MAIN);   // between days
  lv_obj_set_style_pad_column(ch, 1, LV_PART_ITEMS);  // between the two bars of a day
  lv_obj_set_style_radius(ch, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(ch, lv_color_hex(0x9E9E9E), LV_PART_TICKS);
  lv_obj_set_style_line_opa(ch, LV_OPA_TRANSP, LV_PART_TICKS);
  lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 3, 1, true, 34);
  lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_X, 0, 0, RUUVI_DAYS, 1, true, 20);
  lv_obj_add_event_cb(ch, chart_draw_cb, LV_EVENT_DRAW_PART_BEGIN, &c);
  lv_obj_clear_flag(ch, LV_OBJ_FLAG_CLICKABLE);
  c.ser_lo = lv_chart_add_series(ch, lv_color_hex(COL_LOW), LV_CHART_AXIS_PRIMARY_Y);
  c.ser_hi = lv_chart_add_series(ch, lv_color_hex(COL_HIGH), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(ch, c.ser_lo, LV_CHART_POINT_NONE);
  lv_chart_set_all_value(ch, c.ser_hi, LV_CHART_POINT_NONE);
  return ch;
}

static void show_history(RuuviCard &c, const RuuviHist *h) {
  if (!h) {
    if (!c.has_shown) return;
    c.has_shown = false;
    lv_chart_set_all_value(c.chart, c.ser_lo, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(c.chart, c.ser_hi, LV_CHART_POINT_NONE);
    set_label(c.hilo, "");
    lv_chart_refresh(c.chart);
    return;
  }
  if (c.has_shown && !memcmp(&c.shown, h, sizeof(*h))) return;
  c.shown = *h;
  c.has_shown = true;

  int mn = INT16_MAX, mx = INT16_MIN;
  for (int i = 0; i < RUUVI_DAYS; i++) {
    const RuuviDay &d = h->d[i];
    if (d.lo != RUUVI_NONE && d.lo < mn) mn = d.lo;
    if (d.hi != RUUVI_NONE && d.hi > mx) mx = d.hi;
  }
  bool any = mn <= mx;
  if (!any) {  // nothing recorded yet: an empty 0-20 degree scale
    mn = 0;
    mx = 200;
  }
  /* a scale in whole steps of 5, 10, 20 ... degrees, at most 4 lines, with room
     under the lowest bar so it never disappears */
  int lo_t = mn - 20, hi_t = mx + 10;
  int step = 50;  // tenths
  while ((int)ceilf((float)hi_t / step) - (int)floorf((float)lo_t / step) > 3) step *= 2;
  int lo = (int)floorf((float)lo_t / step) * step;
  int hi = (int)ceilf((float)hi_t / step) * step;
  int lines = (hi - lo) / step + 1;
  lv_chart_set_range(c.chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
  lv_chart_set_div_line_count(c.chart, lines, 0);
  lv_chart_set_axis_tick(c.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, lines, 1, true, 34);
  for (int i = 0; i < RUUVI_DAYS; i++) {
    const RuuviDay &d = h->d[i];
    lv_chart_set_value_by_id(c.chart, c.ser_lo, i, d.lo == RUUVI_NONE ? LV_CHART_POINT_NONE : d.lo);
    lv_chart_set_value_by_id(c.chart, c.ser_hi, i, d.hi == RUUVI_NONE ? LV_CHART_POINT_NONE : d.hi);
  }
  lv_chart_refresh(c.chart);

  if (any) {
    char b[64];
    snprintf(b, sizeof(b), "#F3A246 " LV_SYMBOL_UP " %.1f" DEG "#   #4484C4 " LV_SYMBOL_DOWN " %.1f" DEG "#",
             mx / 10.0f, mn / 10.0f);
    set_label(c.hilo, b);
  } else {
    set_label(c.hilo, "");
  }
}

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
    lv_obj_set_style_min_height(c.card, 120, 0);
    lv_obj_set_flex_flow(c.card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(c.card, 10, 0);
    lv_obj_set_style_pad_column(c.card, 8, 0);
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c.card, LV_OBJ_FLAG_HIDDEN);

    /* left: the readings */
    lv_obj_t *col = lv_obj_create(c.card);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 178, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    c.name = lv_label_create(col);
    lv_obj_set_width(c.name, LV_PCT(100));
    lv_label_set_long_mode(c.name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(c.name, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(c.name, "");

    c.temp = lv_label_create(col);
    lv_obj_set_style_text_font(c.temp, FONT_BIG, 0);
    lv_label_set_text(c.temp, "--");

    c.details = lv_label_create(col);
    lv_label_set_text(c.details, "");

    c.hilo = lv_label_create(col);
    lv_label_set_recolor(c.hilo, true);
    lv_label_set_text(c.hilo, "");

    c.info = make_grey_label(col);
    lv_obj_set_width(c.info, LV_PCT(100));
    lv_label_set_long_mode(c.info, LV_LABEL_LONG_DOT);

    /* right: the last 7 days */
    c.chart = make_chart(c.card, c);
  }
}

/* Once per second: records the history, updates the Ruuvi tab and the RuuviTag part of Settings */
void ruuvi_timer_cb(lv_timer_t *t) {
  static RuuviTag copy[MAX_TAGS];
  xSemaphoreTake(ruuvi_mtx, portMAX_DELAY);
  memcpy(copy, tags, sizeof(tags));
  xSemaphoreGive(ruuvi_mtx);

  uint32_t now = millis();

  /* the history is kept whether or not the tab is on screen */
  auto tag_for = [&](int i) -> const RuuviTag * {
    for (int k = 0; k < MAX_TAGS; k++)
      if (copy[k].used && !strcmp(copy[k].addr, ruuvi_cfg[i].mac)) return &copy[k];
    return NULL;
  };
  if (feat_ruuvi)
    for (int i = 0; i < MAX_RUUVI; i++) {
      if (!ruuvi_cfg[i].used) continue;
      const RuuviTag *tag = tag_for(i);
      if (tag && tag->has_temp && now - tag->last_seen < 120000UL) ruuvi_hist_note(i, ruuvi_cfg[i].mac, tag->temp);
    }

  char b[96];
  int shown = 0;
  if (!tab_visible(tab_temp)) {  // the Settings list still wants the tag list
    ruuvi_settings_refresh(copy, now);
    return;
  }

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

    static RuuviHist h;
    show_history(c, ruuvi_hist_get(i, ruuvi_cfg[i].mac, &h) ? &h : NULL);

    const RuuviTag *tag = tag_for(i);
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
    if (tag->has_pres) snprintf(pres, sizeof(pres), TR("%.0f hPa"), tag->pres);
    snprintf(b, sizeof(b), "%s   %s", hum, pres);
    set_label(c.details, b);

    /* the tag's battery and signal while it is heard, otherwise how long it has been quiet */
    uint32_t age = (now - tag->last_seen) / 1000;
    if (age < 60) snprintf(b, sizeof(b), "%.2f V   %d dBm", tag->batt_mv / 1000.0f, tag->rssi);
    else if (age < 120) snprintf(b, sizeof(b), TR("%lu s ago"), (unsigned long)age);
    else snprintf(b, sizeof(b), TR("No data for %lu min"), (unsigned long)(age / 60));
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
