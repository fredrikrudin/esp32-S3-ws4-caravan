/* History screen, opened from the Power tab.
   Bars of solar and consumption per hour (last 24 h) or per day (last 7 days),
   with totals beside them, in the style of Victron VRM. */
#include "app.h"

static lv_obj_t *hist_scr, *chart, *lbl_period, *lbl_solar, *lbl_load, *lbl_soc, *period_sel;
static lv_chart_series_t *ser_solar, *ser_load;
static lv_obj_t *back_from = nullptr;
static bool daily = false;  // false = 24 hours, true = 30 days

static const char *period_map[] = { "24 hours", "7 days", "" };

/* X axis labels: hours ago / days ago, every few bars so they stay readable */
static void chart_draw_cb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;
  if (dsc->id != LV_CHART_AXIS_PRIMARY_X || !dsc->text) return;

  int n = daily ? HIST_DAYS : HIST_HOURS;
  int ago = n - 1 - dsc->value;  // 0 = the period just finished
  if (ago == 0) lv_snprintf(dsc->text, dsc->text_length, "now");
  else lv_snprintf(dsc->text, dsc->text_length, "-%d%s", ago, daily ? "d" : "h");
}

static void refresh_chart() {
  HistBucket *hours, *days;
  history_get(&hours, &days);
  if (!hours) return;
  HistBucket *a = daily ? days : hours;
  int n = daily ? HIST_DAYS : HIST_HOURS;

  lv_chart_set_point_count(chart, n);
  float scale = daily ? 1000.0f : 1.0f;  // days in kWh, hours in Wh
  int max = 1;
  for (int i = 0; i < n; i++) {
    int s = (int)(a[i].solar_wh / scale * (daily ? 10 : 1));  // days: 0.1 kWh steps
    int l = (int)(a[i].load_wh / scale * (daily ? 10 : 1));
    lv_chart_set_value_by_id(chart, ser_solar, i, s);
    lv_chart_set_value_by_id(chart, ser_load, i, l);
    if (s > max) max = s;
    if (l > max) max = l;
  }
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, max + max / 10 + 1);

  float solar_kwh, load_kwh;
  history_totals(daily, &solar_kwh, &load_kwh);
  lv_label_set_text_fmt(lbl_period, "%s  %s", daily ? "Last 7 days" : "Last 24 hours",
                        daily ? "(kWh per day)" : "(Wh per hour)");
  char b[48];
  snprintf(b, sizeof(b), "%.2f kWh", solar_kwh);
  set_label(lbl_solar, b);
  snprintf(b, sizeof(b), "%.2f kWh", load_kwh);
  set_label(lbl_load, b);

  float soc = a[n - 1].soc;
  if (isnan(soc)) strcpy(b, "--");
  else snprintf(b, sizeof(b), "%.0f%%", soc);
  set_label(lbl_soc, b);
}

static void period_cb(lv_event_t *e) {
  daily = (lv_btnmatrix_get_selected_btn(period_sel) == 1);
  refresh_chart();
}

static void back_cb(lv_event_t *e) {
  if (back_from) lv_scr_load(back_from);
}

static lv_obj_t *summary_box(lv_obj_t *parent, const char *title, uint32_t color, lv_obj_t **value) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_size(box, 148, 66);
  lv_obj_set_style_pad_all(box, 8, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *t = lv_label_create(box);
  lv_obj_set_style_text_color(t, lv_color_hex(color), 0);
  lv_label_set_text(t, title);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  *value = lv_label_create(box);
  lv_obj_set_style_text_font(*value, FONT_BIG, 0);
  lv_obj_align(*value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_label_set_text(*value, "--");
  return box;
}

void build_history_screen() {
  hist_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(hist_scr, lv_color_hex(0x15171A), 0);
  lv_obj_set_style_pad_all(hist_scr, 8, 0);
  lv_obj_clear_flag(hist_scr, LV_OBJ_FLAG_SCROLLABLE);

  /* header: back button and period selector */
  lv_obj_t *row = make_row(hist_scr, LV_FLEX_ALIGN_START);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 0);
  make_btn(row, LV_SYMBOL_LEFT " Back", back_cb);
  period_sel = lv_btnmatrix_create(row);
  lv_btnmatrix_set_map(period_sel, period_map);
  lv_obj_set_size(period_sel, 260, 44);
  lv_btnmatrix_set_btn_ctrl_all(period_sel, LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_NO_REPEAT);
  lv_btnmatrix_set_one_checked(period_sel, true);
  lv_btnmatrix_set_btn_ctrl(period_sel, 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_style_bg_opa(period_sel, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(period_sel, 0, 0);
  lv_obj_set_style_pad_all(period_sel, 0, 0);
  lv_obj_set_style_radius(period_sel, 8, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(period_sel, lv_color_hex(0x0E2233), LV_PART_ITEMS);
  lv_obj_set_style_border_width(period_sel, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_color(period_sel, lv_color_hex(0x2F6FB5), LV_PART_ITEMS);
  lv_obj_set_style_text_color(period_sel, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(period_sel, lv_color_hex(0x2F6FB5), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(period_sel, period_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lbl_period = make_grey_label(hist_scr);
  lv_obj_align(lbl_period, LV_ALIGN_TOP_LEFT, 4, 56);

  /* the bars */
  chart = lv_chart_create(hist_scr);
  lv_obj_set_size(chart, 460, 230);
  lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 78);
  lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
  lv_chart_set_div_line_count(chart, 5, 0);
  lv_obj_set_style_bg_color(chart, lv_color_hex(0x1B1F24), 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_column(chart, 1, 0);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);  // no dots on the bars
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 5, 1, true, 50);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 4, 2, 7, 1, true, 30);
  lv_obj_add_event_cb(chart, chart_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
  ser_solar = lv_chart_add_series(chart, lv_color_hex(0xF39C12), LV_CHART_AXIS_PRIMARY_Y);
  ser_load = lv_chart_add_series(chart, lv_color_hex(0xE74C3C), LV_CHART_AXIS_PRIMARY_Y);

  /* totals */
  lv_obj_t *box;
  box = summary_box(hist_scr, LV_SYMBOL_CHARGE " Solar", 0xF39C12, &lbl_solar);
  lv_obj_align(box, LV_ALIGN_BOTTOM_LEFT, 0, -4);
  box = summary_box(hist_scr, LV_SYMBOL_POWER " Consumption", 0xE74C3C, &lbl_load);
  lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -4);
  box = summary_box(hist_scr, LV_SYMBOL_BATTERY_FULL " Battery now", 0x3498DB, &lbl_soc);
  lv_obj_align(box, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
}

/* Opened by tapping a tile on the Power tab */
void show_history_screen() {
  if (!hist_scr) build_history_screen();
  back_from = lv_scr_act();
  refresh_chart();
  lv_scr_load(hist_scr);
}

void history_timer_cb(lv_timer_t *t) {
  if (hist_scr && lv_scr_act() == hist_scr) refresh_chart();
}
