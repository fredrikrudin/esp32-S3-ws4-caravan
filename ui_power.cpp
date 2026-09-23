/* Power tab: classic Victron overview style, built around the battery.
   The battery (SmartShunt) sits large in the middle. DC sources (solar, DC-DC)
   are on its left, the AC side (shore charger, inverter) on its right, and
   DC loads below, connected by solid lines. A line takes the colour of its
   device while energy is flowing, and is dim grey when nothing is happening. */
#include "app.h"

#define COL_SOLAR 0xF39C12
#define COL_AC 0x34495E
#define COL_DCDC 0x16A085
#define COL_BATT_BG 0x1F5F8B
#define COL_BATT 0x3498DB
#define COL_LOADS 0x27AE60
#define COL_INV 0x8E44AD
#define COL_ALARM 0xC0392B
#define COL_LINE 0x5D6D7E
/* The extra button upset the page layout on the board, so it is off:
   tapping any tile opens the history instead. Set to 1 to try it again. */
#define SHOW_HISTORY_BUTTON 0

struct Tile {
  lv_obj_t *box, *fill, *title, *big, *l1, *l2;
  int w, h;
};
struct Flow {
  lv_obj_t *line;
  lv_point_t pts[2];
  uint32_t color;  // colour of the device this line belongs to
  int dir;         // 0 = no flow, 1 = flowing
};
enum { T_SOLAR, T_AC, T_DCDC, T_BATT, T_LOADS, T_INV, T_COUNT };

static Tile tiles[T_COUNT];
/* flows[0] solar, [1] DC-DC, [2] shore charger, [3] inverter, [4] DC loads */
static Flow flows[5];
static lv_obj_t *lbl_power_empty;

/* ---------- flow lines ---------- */
static void make_flow(Flow &f, lv_obj_t *parent, uint32_t color) {
  f.dir = 0;
  f.color = color;
  f.pts[0].x = f.pts[0].y = f.pts[1].x = f.pts[1].y = 0;
  f.line = lv_line_create(parent);
  lv_obj_set_style_line_width(f.line, 4, 0);
  lv_obj_set_style_line_color(f.line, lv_color_hex(COL_LINE), 0);
  lv_obj_set_style_line_rounded(f.line, true, 0);
  lv_obj_clear_flag(f.line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(f.line, LV_OBJ_FLAG_HIDDEN);
  lv_line_set_points(f.line, f.pts, 2);
}

static void set_flow_geom(Flow &f, int x0, int y0, int x1, int y1) {
  f.pts[0].x = x0;
  f.pts[0].y = y0;
  f.pts[1].x = x1;
  f.pts[1].y = y1;
  lv_line_set_points(f.line, f.pts, 2);
}

static void set_flow_visible(Flow &f, bool visible) {
  if (visible) lv_obj_clear_flag(f.line, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(f.line, LV_OBJ_FLAG_HIDDEN);
  if (!visible) f.dir = 0;
}

/* flowing: the line lights up in the device's colour */
static void set_flow_dir(Flow &f, int dir) {
  if (lv_obj_has_flag(f.line, LV_OBJ_FLAG_HIDDEN)) dir = 0;
  f.dir = dir;
  lv_obj_set_style_line_color(f.line, lv_color_hex(dir ? f.color : COL_LINE), 0);
  lv_obj_set_style_line_width(f.line, dir ? 6 : 4, 0);
}

/* ---------- tiles ---------- */
static void make_tile(Tile &t, lv_obj_t *parent, const char *title, uint32_t color, int w, int h) {
  t.w = w;
  t.h = h;
  t.box = lv_obj_create(parent);
  lv_obj_remove_style_all(t.box);
  lv_obj_set_size(t.box, w, h);
  lv_obj_set_style_bg_color(t.box, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(t.box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(t.box, 4, 0);
  lv_obj_set_style_clip_corner(t.box, true, 0);
  lv_obj_set_style_text_color(t.box, lv_color_white(), 0);
  lv_obj_clear_flag(t.box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(t.box, LV_OBJ_FLAG_HIDDEN);
  /* tapping any tile opens the history */
  lv_obj_add_flag(t.box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
    t.box, [](lv_event_t *e) {
      show_history_screen();
    },
    LV_EVENT_CLICKED, NULL);

  /* level fill (used by the battery tile for state of charge) */
  t.fill = lv_obj_create(t.box);
  lv_obj_remove_style_all(t.fill);
  lv_obj_set_size(t.fill, w, 0);
  lv_obj_set_style_bg_opa(t.fill, LV_OPA_COVER, 0);
  lv_obj_align(t.fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_add_flag(t.fill, LV_OBJ_FLAG_HIDDEN);

  /* darker header strip */
  lv_obj_t *hdr = lv_obj_create(t.box);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_size(hdr, w, 24);
  lv_obj_set_style_bg_color(hdr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_30, 0);
  t.title = lv_label_create(hdr);
  lv_obj_set_width(t.title, w - 12);
  lv_label_set_long_mode(t.title, LV_LABEL_LONG_DOT);
  lv_obj_align(t.title, LV_ALIGN_LEFT_MID, 6, 0);
  lv_label_set_text(t.title, title);

  t.big = lv_label_create(t.box);
  lv_obj_set_style_text_font(t.big, FONT_BIG, 0);
  lv_obj_align(t.big, LV_ALIGN_TOP_MID, 0, 30);
  lv_label_set_text(t.big, "--");

  t.l1 = lv_label_create(t.box);
  lv_obj_align(t.l1, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_label_set_text(t.l1, "");

  t.l2 = lv_label_create(t.box);
  lv_obj_set_style_text_opa(t.l2, LV_OPA_80, 0);
  lv_obj_align(t.l2, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_label_set_text(t.l2, "");
}

void build_power_tab() {
  lv_obj_set_style_pad_all(tab_power, 0, 0);
  lv_obj_clear_flag(tab_power, LV_OBJ_FLAG_SCROLLABLE);

  /* lines first, tiles on top; each line carries its device's colour */
  make_flow(flows[0], tab_power, COL_SOLAR);
  make_flow(flows[1], tab_power, COL_DCDC);
  make_flow(flows[2], tab_power, COL_AC);
  make_flow(flows[3], tab_power, COL_INV);
  make_flow(flows[4], tab_power, COL_LOADS);

  /* side tiles are narrow; the battery is the big one in the middle */
  make_tile(tiles[T_SOLAR], tab_power, "PV charger", COL_SOLAR, 120, 96);
  make_tile(tiles[T_DCDC], tab_power, "DC-DC", COL_DCDC, 120, 96);
  make_tile(tiles[T_AC], tab_power, "Shore charger", COL_AC, 120, 96);
  make_tile(tiles[T_INV], tab_power, "Inverter", COL_INV, 120, 96);
  make_tile(tiles[T_BATT], tab_power, "Battery", COL_BATT_BG, 160, 180);
  make_tile(tiles[T_LOADS], tab_power, "DC loads", COL_LOADS, 160, 80);

  /* the side tiles have less room: move their lines up a little */
  const int side[4] = { T_SOLAR, T_DCDC, T_AC, T_INV };
  for (int i = 0; i < 4; i++) {
    lv_obj_align(tiles[side[i]].big, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_align(tiles[side[i]].l1, LV_ALIGN_BOTTOM_MID, 0, -22);
  }

  lv_obj_set_style_bg_color(tiles[T_BATT].fill, lv_color_hex(COL_BATT), 0);
  lv_obj_clear_flag(tiles[T_BATT].fill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(tiles[T_BATT].big, FONT_CLOCK, 0);
  lv_obj_align(tiles[T_BATT].big, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_align(tiles[T_BATT].l1, LV_ALIGN_BOTTOM_MID, 0, -46);
  lv_obj_align(tiles[T_BATT].l2, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_add_flag(tiles[T_LOADS].l1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(tiles[T_LOADS].big, LV_ALIGN_TOP_MID, 0, 26);

#if SHOW_HISTORY_BUTTON
  /* A link in the corner, for when no tile is showing. Fixed position and size,
     so it can never make the page taller than the screen (which would let the
     tabview scroll and look like the tab bar had moved). */
  lv_obj_t *hist_btn = make_btn(
    tab_power, LV_SYMBOL_LIST " History", [](lv_event_t *e) {
      show_history_screen();
    });
  lv_obj_set_style_bg_color(hist_btn, lv_color_hex(0x2F3A45), 0);
  lv_obj_set_size(hist_btn, 104, 34);
  lv_obj_set_pos(hist_btn, 370, 392);
#endif

  lbl_power_empty = lv_label_create(tab_power);
  lv_obj_set_style_text_align(lbl_power_empty, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl_power_empty, "No Victron devices yet.\nAdd them under Settings.");
  lv_obj_add_flag(lbl_power_empty, LV_OBJ_FLAG_CLICKABLE);  // tapping here opens the history too
  lv_obj_add_event_cb(
    lbl_power_empty, [](lv_event_t *e) {
      show_history_screen();
    },
    LV_EVENT_CLICKED, NULL);
  lv_obj_center(lbl_power_empty);
}

/* Positions the tiles that are in use: DC sources left, AC side right,
   battery in the middle, DC loads below */
static void power_layout(const bool has[T_COUNT]) {
  const int W = 480;
  const int tw = 120, th = 96, gap = 10;
  const int bat_w = 160, bat_h = 180, bat_x = (W - bat_w) / 2, bat_y = 110;
  const int load_w = 160, load_h = 80, load_x = (W - load_w) / 2, load_y = 310;
  const int left_x = 6, right_x = W - 6 - tw;

  /* one column of up to two tiles beside the battery */
  auto column = [&](int t_a, int t_b, int f_a, int f_b, bool left) {
    const int tile[2] = { t_a, t_b };
    const int flow[2] = { f_a, f_b };
    int n = has[t_a] + has[t_b];
    int y = (n == 2) ? bat_y : bat_y + (bat_h - th) / 2;  // one tile: level with the battery
    for (int i = 0; i < 2; i++) {
      Tile &t = tiles[tile[i]];
      Flow &f = flows[flow[i]];
      if (!has[tile[i]]) {
        lv_obj_add_flag(t.box, LV_OBJ_FLAG_HIDDEN);
        set_flow_visible(f, false);
        set_flow_dir(f, 0);
        continue;
      }
      lv_obj_clear_flag(t.box, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(t.box, left ? left_x : right_x, y);
      int cy = y + th / 2;
      if (left) set_flow_geom(f, left_x + tw, cy, bat_x, cy);                    // source -> battery
      else if (tile[i] == T_INV) set_flow_geom(f, bat_x + bat_w, cy, right_x, cy);  // battery -> inverter
      else set_flow_geom(f, right_x, cy, bat_x + bat_w, cy);                     // charger -> battery
      set_flow_visible(f, true);
      y += th + gap;
    }
  };

  column(T_SOLAR, T_DCDC, 0, 1, true);
  column(T_AC, T_INV, 2, 3, false);

  if (has[T_BATT]) {
    lv_obj_clear_flag(tiles[T_BATT].box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(tiles[T_BATT].box, bat_x, bat_y);
  } else {
    lv_obj_add_flag(tiles[T_BATT].box, LV_OBJ_FLAG_HIDDEN);
  }

  if (has[T_LOADS]) {
    lv_obj_clear_flag(tiles[T_LOADS].box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(tiles[T_LOADS].box, load_x, load_y);
    set_flow_geom(flows[4], W / 2, bat_y + bat_h, W / 2, load_y);
    set_flow_visible(flows[4], true);
  } else {
    lv_obj_add_flag(tiles[T_LOADS].box, LV_OBJ_FLAG_HIDDEN);
    set_flow_visible(flows[4], false);
    set_flow_dir(flows[4], 0);
  }
}

/* Once per second: Power tab + the Victron part of Settings */
void power_timer_cb(lv_timer_t *timer) {
  static VicCfg cfg[MAX_VIC];
  static VicData dat[MAX_VIC];
  static VicSeen seen[MAX_VIC_SEEN];
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  memcpy(cfg, vic_cfg, sizeof(cfg));
  memcpy(dat, vic_data, sizeof(dat));
  memcpy(seen, vic_seen, sizeof(seen));
  xSemaphoreGive(vic_mtx);

  uint32_t now = millis();
  char b[96];
  auto ok = [&](int i) {
    return i >= 0 && vic_fresh(dat[i], now) && dat[i].key_ok;
  };

  /* ---- which tiles are needed ---- */
  int first[T_COUNT];
  for (int i = 0; i < T_COUNT; i++) first[i] = -1;
  int n_solar = 0;
  float pv_sum = 0, solar_out = 0, yield_sum = 0;
  bool pv_any = false, yield_any = false, any = false;
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used || !vic_supported(cfg[i].type)) continue;
    any = true;
    int t = cfg[i].type == VIC_SOLAR      ? T_SOLAR
            : cfg[i].type == VIC_ACCHG    ? T_AC
            : cfg[i].type == VIC_DCDC     ? T_DCDC
            : cfg[i].type == VIC_INVERTER ? T_INV
                                          : T_BATT;  // battery monitor
    if (first[t] < 0) first[t] = i;
    if (t == T_SOLAR) {
      n_solar++;
      if (ok(i)) {
        if (!isnan(dat[i].pv_w)) {
          pv_sum += dat[i].pv_w;
          pv_any = true;
        }
        if (!isnan(dat[i].batt_v) && !isnan(dat[i].batt_i)) solar_out += dat[i].batt_v * dat[i].batt_i;
        if (!isnan(dat[i].yield_kwh)) {
          yield_sum += dat[i].yield_kwh;
          yield_any = true;
        }
      }
    }
  }
  int bm = first[T_BATT];  // battery monitor
  bool has[T_COUNT];
  has[T_SOLAR] = first[T_SOLAR] >= 0;
  has[T_AC] = first[T_AC] >= 0;
  has[T_DCDC] = first[T_DCDC] >= 0;
  has[T_BATT] = any;
  has[T_LOADS] = bm >= 0;
  has[T_INV] = first[T_INV] >= 0;

  static int last_mask = -1;
  int mask = 0;
  for (int i = 0; i < T_COUNT; i++) mask |= has[i] << i;
  if (mask != last_mask) {
    last_mask = mask;
    power_layout(has);
    if (any) lv_obj_add_flag(lbl_power_empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(lbl_power_empty, LV_OBJ_FLAG_HIDDEN);
  }

  /* ---- solar ---- */
  if (has[T_SOLAR]) {
    int i = first[T_SOLAR];
    Tile &t = tiles[T_SOLAR];
    set_label(t.title, n_solar > 1 ? "PV chargers" : cfg[i].name);
    if (pv_any) snprintf(b, sizeof(b), "%.0f W", pv_sum);
    else strcpy(b, "--");
    set_label(t.big, b);
    if (!ok(i)) set_label(t.l1, vic_status(dat[i], cfg[i].type, now));
    else if (n_solar > 1) {
      snprintf(b, sizeof(b), "%d chargers", n_solar);
      set_label(t.l1, b);
    } else set_label(t.l1, vic_state_name(dat[i].state));
    if (yield_any) snprintf(b, sizeof(b), "Today %.2f kWh", yield_sum);
    else b[0] = 0;
    set_label(t.l2, b);
    set_flow_dir(flows[0], pv_any && pv_sum > 2);
  }

  /* ---- AC charger ---- */
  float ac_w = NAN;
  if (has[T_AC]) {
    int i = first[T_AC];
    Tile &t = tiles[T_AC];
    set_label(t.title, cfg[i].name);
    if (ok(i) && !isnan(dat[i].batt_v) && !isnan(dat[i].batt_i)) ac_w = dat[i].batt_v * dat[i].batt_i;
    if (!isnan(ac_w)) snprintf(b, sizeof(b), "%.0f W", ac_w);
    else strcpy(b, "--");
    set_label(t.big, b);
    set_label(t.l1, ok(i) ? vic_state_name(dat[i].state) : vic_status(dat[i], cfg[i].type, now));
    if (!isnan(ac_w)) snprintf(b, sizeof(b), "%.2f V  %.1f A", dat[i].batt_v, dat[i].batt_i);
    else b[0] = 0;
    set_label(t.l2, b);
    set_flow_dir(flows[2], !isnan(ac_w) && ac_w > 2);  // shore charger
  }

  /* ---- DC-DC ---- */
  bool dcdc_active = false;
  if (has[T_DCDC]) {
    int i = first[T_DCDC];
    Tile &t = tiles[T_DCDC];
    set_label(t.title, cfg[i].name);
    if (ok(i) && !isnan(dat[i].out_v)) snprintf(b, sizeof(b), "%.2f V", dat[i].out_v);
    else strcpy(b, "--");
    set_label(t.big, b);
    set_label(t.l1, ok(i) ? vic_state_name(dat[i].state) : vic_status(dat[i], cfg[i].type, now));
    if (ok(i) && !isnan(dat[i].in_v)) snprintf(b, sizeof(b), "In %.2f V", dat[i].in_v);
    else b[0] = 0;
    set_label(t.l2, b);
    dcdc_active = ok(i) && vic_active_state(dat[i].state);
    set_flow_dir(flows[1], dcdc_active);
  }

  /* ---- battery ---- */
  if (has[T_BATT]) {
    Tile &t = tiles[T_BATT];
    int fill_h = 0;
    if (bm >= 0) {
      const VicData &d = dat[bm];
      set_label(t.title, cfg[bm].name);
      if (ok(bm)) {
        if (!isnan(d.soc)) {
          snprintf(b, sizeof(b), "%.0f%%", d.soc);
          fill_h = (int)((t.h - 24) * d.soc / 100.0f);
        } else strcpy(b, "--");
        set_label(t.big, b);

        char v[16] = "--", a[16] = "--";
        if (!isnan(d.batt_v)) snprintf(v, sizeof(v), "%.2f V", d.batt_v);
        if (!isnan(d.batt_i)) snprintf(a, sizeof(a), "%+.1f A", d.batt_i);
        snprintf(b, sizeof(b), "%s   %s", v, a);
        set_label(t.l1, b);

        b[0] = 0;
        if (!isnan(d.batt_v) && !isnan(d.batt_i)) snprintf(b, sizeof(b), "%+.0f W", d.batt_v * d.batt_i);
        if (!isnan(d.batt_i) && d.batt_i < -0.05f && d.remaining_min >= 0) {
          char tl[24];
          snprintf(tl, sizeof(tl), "   %dh %02dm left", d.remaining_min / 60, d.remaining_min % 60);
          strlcat(b, tl, sizeof(b));
        } else if (!isnan(d.batt_i) && d.batt_i > 0.05f) {
          strlcat(b, "   Charging", sizeof(b));
        }
        if (!isnan(d.temp_c)) {
          char tc[16];
          snprintf(tc, sizeof(tc), "   %.0f" DEG "C", d.temp_c);
          strlcat(b, tc, sizeof(b));
        }
        set_label(t.l2, b);
      } else {
        set_label(t.big, "--");
        set_label(t.l1, vic_status(d, cfg[bm].type, now));
        set_label(t.l2, "");
      }
    } else {
      /* no battery monitor: show the voltage a charger or inverter reports */
      set_label(t.title, "Battery");
      set_label(t.big, "--");
      b[0] = 0;
      for (int i = 0; i < MAX_VIC; i++) {
        if (cfg[i].used && (cfg[i].type == VIC_SOLAR || cfg[i].type == VIC_ACCHG || cfg[i].type == VIC_INVERTER) && ok(i) && !isnan(dat[i].batt_v)) {
          snprintf(b, sizeof(b), "%.2f V", dat[i].batt_v);
          break;
        }
      }
      set_label(t.l1, b);
      set_label(t.l2, "No battery monitor");
    }
    lv_obj_set_height(t.fill, fill_h);
  }

  /* ---- inverter ---- */
  bool inv_active = false;
  if (has[T_INV]) {
    int i = first[T_INV];
    Tile &t = tiles[T_INV];
    const VicData &d = dat[i];
    set_label(t.title, cfg[i].name);
    if (ok(i) && !isnan(d.ac_va)) snprintf(b, sizeof(b), "%.0f VA", d.ac_va);
    else strcpy(b, "--");
    set_label(t.big, b);

    const char *alarm = ok(i) ? vic_alarm_text(d.alarm) : NULL;
    if (!ok(i)) set_label(t.l1, vic_status(d, cfg[i].type, now));
    else if (alarm) {
      snprintf(b, sizeof(b), LV_SYMBOL_WARNING " %s", alarm);
      set_label(t.l1, b);
    } else set_label(t.l1, d.state == 1 ? "ECO (search)" : vic_state_name(d.state));
    /* red tile on alarm or fault */
    lv_obj_set_style_bg_color(t.box, lv_color_hex((alarm || (ok(i) && d.state == 2)) ? COL_ALARM : COL_INV), 0);

    b[0] = 0;
    if (ok(i) && !isnan(d.ac_v)) {
      if (!isnan(d.ac_i)) snprintf(b, sizeof(b), "%.0f V  %.1f A", d.ac_v, d.ac_i);
      else snprintf(b, sizeof(b), "%.0f V", d.ac_v);
    }
    set_label(t.l2, b);
    inv_active = ok(i) && d.state == 9;  // inverting
    set_flow_dir(flows[3], inv_active);
  }

  /* ---- DC loads (calculated: chargers in - battery in) ---- */
  if (has[T_LOADS]) {
    Tile &t = tiles[T_LOADS];
    const VicData &d = dat[bm];
    float loads = NAN;
    if (ok(bm) && !isnan(d.batt_v) && !isnan(d.batt_i)) {
      loads = solar_out + (isnan(ac_w) ? 0 : ac_w) - d.batt_v * d.batt_i;
      if (loads < 0) loads = 0;
    }
    if (!isnan(loads)) snprintf(b, sizeof(b), "%.0f W", loads);
    else strcpy(b, "--");
    set_label(t.big, b);
    const char *note = isnan(loads)                ? ""
                       : dcdc_active && inv_active ? "Calc. excl. DC-DC, incl. inv."
                       : dcdc_active               ? "Calculated, excl. DC-DC"
                       : inv_active                ? "Calculated, incl. inverter"
                                                   : "Calculated";
    set_label(t.l2, note);
    set_flow_dir(flows[4], !isnan(loads) && loads > 2);
  }

  victron_settings_refresh(cfg, dat, seen, now);
}
