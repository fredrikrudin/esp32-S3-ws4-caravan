/* Weather tab: NTP clock, current weather, 3-day forecast.
   Also moves WiFi/weather results from the network task into the UI. */
#include "app.h"

static lv_obj_t *lbl_clock, *lbl_date, *lbl_place, *lbl_temp, *lbl_cond, *lbl_details, *lbl_updated;
static lv_obj_t *fc_day[3], *fc_cond[3], *fc_temp[3];

void build_weather_tab() {
  lv_obj_set_flex_flow(tab_weather, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_weather, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tab_weather, 6, 0);

  lbl_clock = lv_label_create(tab_weather);
  lv_obj_set_style_text_font(lbl_clock, FONT_CLOCK, 0);
  lv_label_set_text(lbl_clock, "--:--");

  lbl_date = lv_label_create(tab_weather);
  lv_label_set_text(lbl_date, "");

  lbl_place = make_grey_label(tab_weather);
  lv_label_set_text(lbl_place, g.has_loc ? g.place : "No location set");

  lv_obj_t *row = make_row(tab_weather, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 20, 0);
  lv_obj_set_style_pad_top(row, 10, 0);
  lbl_temp = lv_label_create(row);
  lv_obj_set_style_text_font(lbl_temp, FONT_BIG, 0);
  lv_label_set_text(lbl_temp, "--");
  lbl_cond = lv_label_create(row);
  lv_label_set_text(lbl_cond, g.has_loc ? "Loading weather..." : "Set a location\nin Settings");

  lbl_details = lv_label_create(tab_weather);
  lv_label_set_text(lbl_details, "");

  row = make_row(tab_weather, LV_FLEX_ALIGN_SPACE_EVENLY);
  lv_obj_set_style_pad_top(row, 10, 0);
  for (int i = 0; i < 3; i++) {
    lv_obj_t *card = lv_obj_create(row);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    fc_day[i] = lv_label_create(card);
    lv_label_set_text(fc_day[i], "-");
    fc_cond[i] = lv_label_create(card);
    lv_obj_set_width(fc_cond[i], LV_PCT(100));
    lv_obj_set_style_text_align(fc_cond[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fc_cond[i], LV_LABEL_LONG_WRAP);
    lv_label_set_text(fc_cond[i], "");
    fc_temp[i] = lv_label_create(card);
    lv_label_set_text(fc_temp[i], "");
  }

  lbl_updated = make_grey_label(tab_weather);
}

static void show_weather(const Weather &w) {
  if (!w.valid) return;
  char b[128];

  snprintf(b, sizeof(b), "%.1f" DEG "C", w.temp);
  lv_label_set_text(lbl_temp, b);

  snprintf(b, sizeof(b), "%s\nH %.0f" DEG "  L %.0f" DEG, wmo_text(w.code), w.today_max, w.today_min);
  lv_label_set_text(lbl_cond, b);

  snprintf(b, sizeof(b), "Feels %.1f" DEG "   Humidity %d%%   Wind %.1f m/s", w.feels, w.humidity, w.wind);
  lv_label_set_text(lbl_details, b);

  for (int i = 0; i < 3; i++) {
    lv_label_set_text(fc_day[i], w.fc_day[i]);
    lv_label_set_text(fc_cond[i], wmo_text(w.fc_code[i]));
    snprintf(b, sizeof(b), "%.0f" DEG " / %.0f" DEG, w.fc_max[i], w.fc_min[i]);
    lv_label_set_text(fc_temp[i], b);
  }

  if (w.fetched > 1700000000) {
    time_t t = w.fetched + g.utc_offset;
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(b, sizeof(b), "Updated %H:%M", &tm);
    lv_label_set_text(lbl_updated, b);
  }
}

void format_local_time(char *tb, char *db) {
  time_t now = time(nullptr);
  if (now < 1700000000) {  // not synced yet
    strcpy(tb, "--:--");
    strcpy(db, "Waiting for time sync");
    return;
  }
  time_t lt = now + g.utc_offset;
  struct tm tm;
  gmtime_r(&lt, &tm);
  strftime(tb, 16, "%H:%M", &tm);
  strftime(db, 64, "%A %d %B %Y", &tm);
  if (!g.offset_valid) strlcat(db, "  (UTC)", 64);
}

void clock_timer_cb(lv_timer_t *t) {
  char tb[16], db[64];
  format_local_time(tb, db);
  set_label(lbl_clock, tb);
  set_label(lbl_date, db);
}

/* Moves WiFi/weather results from the network task into the UI */
void net_poll_cb(lv_timer_t *t) {
  if (xSemaphoreTake(g_mtx, 0) != pdTRUE) return;  // busy, try next time
  if (g.status_changed) {
    lv_label_set_text(lbl_wifi_status, g.wifi_status);
    g.status_changed = false;
  }
  if (g.scan_ready) {
    lv_dropdown_set_options(dd_ssid, g.scan_list);
    lv_dropdown_set_selected(dd_ssid, g.scan_sel);
    g.scan_ready = false;
  }
  if (g.loc_changed) {
    lv_label_set_text(lbl_loc, g.loc_status);
    lv_label_set_text(lbl_place, g.has_loc ? g.place : "No location set");
    g.loc_changed = false;
  }
  bool wx = g.weather_changed;
  Weather w;
  if (wx) {
    w = g.weather;
    g.weather_changed = false;
  }
  xSemaphoreGive(g_mtx);
  if (wx) show_weather(w);
}
