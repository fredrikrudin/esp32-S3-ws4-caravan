/* Home tab: the start page, in the style of the Victron GX Boat Page.
   Straight gradient gauges along the left and right edges (battery and solar),
   the clock as the central figure, values beside each gauge, and a
   summary line for temperatures and relays at the bottom. */
#include "app.h"

#define COL_TRACK 0x10202C  // unlit part of a gauge
/* gauge gradients: deep shade at the bottom, bright at the top */
#define COL_BATT_DEEP 0x0B4A7A
#define COL_BATT_A 0x38A9F5  // battery blue
#define COL_SOLAR_DEEP 0x8A4D06
#define COL_SOLAR_A 0xF5B32B
#define COL_LOW_DEEP 0x6E1E16
#define COL_LOW 0xE74C3C  // below 20 %
#define CARD_BG 0x1B1F24

static lv_obj_t *bar_batt, *bar_solar;
static lv_obj_t *lbl_soc, *lbl_batt_sub, *lbl_ttg_cap, *lbl_ttg;
static lv_obj_t *lbl_solar, *lbl_solar_cap, *lbl_solar_sub;
static lv_obj_t *home_clock, *home_date, *lbl_charging;
static lv_obj_t *lbl_inside, *lbl_outside, *lbl_relays;

static float solar_scale = 400;  // gauge top of scale, grows with the highest reading seen

/* A straight gauge along one screen edge: fills from the bottom (0 %) upwards,
   with a gradient from a deep shade at the bottom to a bright one at the top */
static lv_obj_t *make_vbar(lv_obj_t *parent, bool left, uint32_t deep, uint32_t bright) {
  lv_obj_t *b = lv_bar_create(parent);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, 16, 336);
  lv_obj_set_pos(b, left ? 14 : 450, 40);
  lv_bar_set_range(b, 0, 1000);
  lv_bar_set_value(b, 0, LV_ANIM_OFF);

  lv_obj_set_style_bg_color(b, lv_color_hex(COL_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(b, 8, LV_PART_MAIN);

  lv_obj_set_style_bg_color(b, lv_color_hex(bright), LV_PART_INDICATOR);       // top of the gradient
  lv_obj_set_style_bg_grad_color(b, lv_color_hex(deep), LV_PART_INDICATOR);    // bottom of the gradient
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(b, 8, LV_PART_INDICATOR);
  return b;
}

static lv_obj_t *make_value(lv_obj_t *parent, lv_align_t align, int x, int y, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_align(l, align, x, y);
  lv_label_set_text(l, "");
  return l;
}

void build_home_tab() {
  lv_obj_set_style_pad_all(tab_home, 0, 0);
  lv_obj_clear_flag(tab_home, LV_OBJ_FLAG_SCROLLABLE);

  bar_batt = make_vbar(tab_home, true, COL_BATT_DEEP, COL_BATT_A);
  bar_solar = make_vbar(tab_home, false, COL_SOLAR_DEEP, COL_SOLAR_A);

  /* centre: charging indicator, clock, date */
  lbl_charging = make_value(tab_home, LV_ALIGN_TOP_MID, 0, 44, LV_FONT_DEFAULT, 0x2ECC71);
  home_clock = lv_label_create(tab_home);
  lv_obj_set_style_text_font(home_clock, FONT_SAVER, 0);
  lv_label_set_text(home_clock, "--:--");
  lv_obj_align(home_clock, LV_ALIGN_TOP_MID, 0, 68);
  home_date = make_value(tab_home, LV_ALIGN_TOP_MID, 0, 190, LV_FONT_DEFAULT, 0x9E9E9E);

  /* left: battery */
  lbl_soc = make_value(tab_home, LV_ALIGN_TOP_LEFT, 40, 232, FONT_BIG, 0xFFFFFF);
  lbl_batt_sub = make_value(tab_home, LV_ALIGN_TOP_LEFT, 42, 274, LV_FONT_DEFAULT, 0xC8C8C8);
  lbl_ttg_cap = make_value(tab_home, LV_ALIGN_TOP_LEFT, 42, 300, LV_FONT_DEFAULT, 0x9E9E9E);
  lbl_ttg = make_value(tab_home, LV_ALIGN_TOP_LEFT, 42, 320, LV_FONT_DEFAULT, 0xFFFFFF);

  /* right: solar */
  lbl_solar = make_value(tab_home, LV_ALIGN_TOP_RIGHT, -40, 232, FONT_BIG, 0xFFFFFF);
  lbl_solar_cap = make_value(tab_home, LV_ALIGN_TOP_RIGHT, -42, 274, LV_FONT_DEFAULT, 0x9E9E9E);
  lbl_solar_sub = make_value(tab_home, LV_ALIGN_TOP_RIGHT, -42, 300, LV_FONT_DEFAULT, 0xC8C8C8);

  /* bottom strip: temperatures and relays */
  lv_obj_t *strip = lv_obj_create(tab_home);
  lv_obj_remove_style_all(strip);
  lv_obj_set_pos(strip, 60, 366);
  lv_obj_set_size(strip, 360, 52);
  lv_obj_set_style_bg_color(strip, lv_color_hex(CARD_BG), 0);
  lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(strip, 12, 0);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(strip, LV_OBJ_FLAG_CLICKABLE);
  lbl_inside = lv_label_create(strip);
  lv_label_set_text(lbl_inside, "");
  lbl_outside = lv_label_create(strip);
  lv_label_set_text(lbl_outside, "");
  lbl_relays = lv_label_create(strip);
  lv_label_set_text(lbl_relays, "");
}

void home_timer_cb(lv_timer_t *t) {
  char b[96], tb[16], db[64];
  uint32_t now = millis();

  format_local_time(tb, db);
  set_label(home_clock, tb);
  set_label(home_date, db);

  /* ---- Victron ---- */
  static VicCfg cfg[MAX_VIC];
  static VicData dat[MAX_VIC];
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  memcpy(cfg, vic_cfg, sizeof(cfg));
  memcpy(dat, vic_data, sizeof(dat));
  xSemaphoreGive(vic_mtx);

  float pv_sum = 0, yield_sum = 0;
  bool pv_any = false;
  int bm = -1;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used || !vic_fresh(dat[i], now) || !dat[i].key_ok) continue;
    if (cfg[i].type == VIC_SOLAR) {
      if (!isnan(dat[i].pv_w)) {
        pv_sum += dat[i].pv_w;
        pv_any = true;
      }
      if (!isnan(dat[i].yield_kwh)) yield_sum += dat[i].yield_kwh;
    } else if (cfg[i].type == VIC_BATTMON && bm < 0) {
      bm = i;
    }
  }

  /* ---- battery: Victron monitor first, otherwise the BMS ---- */
  static BmsData bms;
  bms_get(&bms);
  bool bms_fresh = feat_bms && bms.valid && now - bms.updated < 30000;
  float soc = NAN, batt_w = NAN;
  int ttg = -1;
  if (bm >= 0 && !isnan(dat[bm].soc)) {
    const VicData &d = dat[bm];
    soc = d.soc;
    if (!isnan(d.batt_v) && !isnan(d.batt_i)) batt_w = d.batt_v * d.batt_i;
    ttg = d.remaining_min;
    if (!isnan(d.batt_v)) {
      if (!isnan(batt_w)) snprintf(b, sizeof(b), "%.2f V  %+.0f W", d.batt_v, batt_w);
      else snprintf(b, sizeof(b), "%.2f V", d.batt_v);
      set_label(lbl_batt_sub, b);
    }
  } else if (bms_fresh) {
    soc = bms.soc;
    if (!isnan(bms.curr)) batt_w = bms.volt * bms.curr;
    if (!isnan(batt_w)) snprintf(b, sizeof(b), "%.2f V  %+.0f W", bms.volt, batt_w);
    else snprintf(b, sizeof(b), "%.2f V", bms.volt);
    set_label(lbl_batt_sub, b);
  } else {
    set_label(lbl_batt_sub, "No data");
  }

  if (isnan(soc)) {
    set_label(lbl_soc, "--");
    lv_bar_set_value(bar_batt, 0, LV_ANIM_OFF);
  } else {
    snprintf(b, sizeof(b), LV_SYMBOL_BATTERY_FULL " %.0f%%", soc);
    set_label(lbl_soc, b);
    lv_bar_set_value(bar_batt, (int)(soc * 10), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_batt, lv_color_hex(soc < 20 ? COL_LOW : COL_BATT_A), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar_batt, lv_color_hex(soc < 20 ? COL_LOW_DEEP : COL_BATT_DEEP), LV_PART_INDICATOR);
  }

  /* time to go, only while discharging */
  if (ttg >= 0 && !isnan(batt_w) && batt_w < 0) {
    set_label(lbl_ttg_cap, "Time To Go");
    snprintf(b, sizeof(b), "%dh %02dm", ttg / 60, ttg % 60);
    set_label(lbl_ttg, b);
  } else {
    set_label(lbl_ttg_cap, "");
    set_label(lbl_ttg, "");
  }

  /* charging indicator, like the boat page's status letter */
  if (!isnan(batt_w) && batt_w > 5) set_label(lbl_charging, LV_SYMBOL_CHARGE " Charging");
  else if (!isnan(batt_w) && batt_w < -5) set_label(lbl_charging, "");
  else set_label(lbl_charging, "");

  /* ---- solar ---- */
  if (pv_any) {
    if (pv_sum > solar_scale) solar_scale = pv_sum;  // gauge grows to fit the highest reading
    snprintf(b, sizeof(b), "%.0f W", pv_sum);
    set_label(lbl_solar, b);
    set_label(lbl_solar_cap, "Solar");
    snprintf(b, sizeof(b), "Today %.2f kWh", yield_sum);
    set_label(lbl_solar_sub, b);
    lv_bar_set_value(bar_solar, (int)(pv_sum / solar_scale * 1000), LV_ANIM_OFF);
  } else {
    set_label(lbl_solar, "--");
    set_label(lbl_solar_cap, "Solar");
    set_label(lbl_solar_sub, "No data");
    lv_bar_set_value(bar_solar, 0, LV_ANIM_OFF);
  }

  /* ---- bottom strip ---- */
  b[0] = 0;
  if (feat_ruuvi && ruuvi_cfg[0].used) {
    static RuuviTag copy[MAX_TAGS];
    xSemaphoreTake(ruuvi_mtx, portMAX_DELAY);
    memcpy(copy, tags, sizeof(tags));
    xSemaphoreGive(ruuvi_mtx);
    for (int i = 0; i < MAX_TAGS; i++) {
      if (copy[i].used && !strcmp(copy[i].addr, ruuvi_cfg[0].mac) && copy[i].has_temp && now - copy[i].last_seen < 600000UL) {
        snprintf(b, sizeof(b), LV_SYMBOL_HOME " %.1f" DEG "C", copy[i].temp);
        break;
      }
    }
  }
  set_label(lbl_inside, b[0] ? b : LV_SYMBOL_HOME " --");

  Weather w;
  LOCK();
  w = g.weather;
  UNLOCK();
  if (w.valid) snprintf(b, sizeof(b), LV_SYMBOL_GPS " %.1f" DEG "C", w.temp);
  else strcpy(b, LV_SYMBOL_GPS " --");
  set_label(lbl_outside, b);

  if (feat_relays && relay_cfg.addr) {
    int on = 0;
    for (int i = 0; i < relay_cfg.count; i++) on += (relay_state >> i) & 1;
    snprintf(b, sizeof(b), LV_SYMBOL_POWER " %d/%d", on, relay_cfg.count);
  } else {
    strcpy(b, LV_SYMBOL_POWER " --");
  }
  set_label(lbl_relays, b);
}
