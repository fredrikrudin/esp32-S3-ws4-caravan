/* Battery tab (experimental): choose the battery's BLE device, show BMS data,
   and show diagnostics (services, last raw frame) to identify unknown protocols. */
#include "app.h"

static lv_obj_t *dd_bms, *lbl_bms_status;
static lv_obj_t *lbl_soc, *lbl_main, *lbl_line1, *lbl_line2, *lbl_cells, *lbl_prot, *lbl_debug;
static BmsSeen dd_seen[MAX_BMS_SEEN];  // device for each dropdown row
static int dd_count = 0;

/* case-insensitive "does name contain hint" */
static bool contains_nocase(const char *name, const char *hint) {
  size_t hl = strlen(hint);
  for (const char *p = name; *p; p++)
    if (!strncasecmp(p, hint, hl)) return true;
  return false;
}

/* Names that are probably a battery go to the top of the list */
static bool likely_battery(const char *name) {
  const char *hints[] = { "BWOB", "ECO", "JBD", "DCHOUSE", "BMC", "SP0", "DP0", "xiaoxiang", "BMS" };
  for (const char *h : hints)
    if (contains_nocase(name, h)) return true;
  return false;
}

static void connect_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_bms);
  if (i >= dd_count) return;  // "Searching..."
  bms_connect_to(dd_seen[i].mac, dd_seen[i].addr_type, dd_seen[i].name);
}

void build_battery_tab() {
  lv_obj_set_flex_flow(tab_battery, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_battery, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tab_battery, 8, 0);

  lv_obj_t *row = make_row(tab_battery, LV_FLEX_ALIGN_START);
  dd_bms = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_bms, 1);
  lv_dropdown_set_options(dd_bms, "Searching...");
  make_btn(row, LV_SYMBOL_BLUETOOTH " Connect", connect_cb);

  lbl_bms_status = make_grey_label(tab_battery);
  lv_obj_set_width(lbl_bms_status, LV_PCT(100));
  lv_label_set_long_mode(lbl_bms_status, LV_LABEL_LONG_WRAP);

  lbl_soc = lv_label_create(tab_battery);
  lv_obj_set_style_text_font(lbl_soc, FONT_CLOCK, 0);
  lv_label_set_text(lbl_soc, "--%");

  lbl_main = lv_label_create(tab_battery);
  lv_obj_set_style_text_font(lbl_main, FONT_BIG, 0);
  lv_label_set_text(lbl_main, "");

  lbl_line1 = lv_label_create(tab_battery);
  lv_label_set_text(lbl_line1, "");
  lbl_line2 = lv_label_create(tab_battery);
  lv_label_set_text(lbl_line2, "");

  lbl_cells = lv_label_create(tab_battery);
  lv_obj_set_width(lbl_cells, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_cells, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_cells, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_cells, "");

  lbl_prot = lv_label_create(tab_battery);
  lv_obj_set_style_text_color(lbl_prot, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(lbl_prot, "");

  lbl_debug = make_grey_label(tab_battery);
  lv_obj_set_width(lbl_debug, LV_PCT(100));
  lv_label_set_long_mode(lbl_debug, LV_LABEL_LONG_WRAP);
}

static void refresh_device_list(uint32_t now) {
  if (lv_dropdown_is_open(dd_bms)) return;  // don't change it under the user's finger

  char cur[18] = "";
  uint16_t ci = lv_dropdown_get_selected(dd_bms);
  if (ci < dd_count) strlcpy(cur, dd_seen[ci].mac, sizeof(cur));
  else {
    LOCK();
    strlcpy(cur, bms_cfg.mac, sizeof(cur));
    UNLOCK();
  }

  BmsSeen list[MAX_BMS_SEEN];
  int n = bms_get_seen(list, MAX_BMS_SEEN);

  /* likely batteries first, then the rest; skip devices gone for > 2 min */
  static char last_opts[MAX_BMS_SEEN * 48] = "";
  char opts[MAX_BMS_SEEN * 48] = "";
  int count = 0, cur_idx = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < n; i++) {
      if (now - list[i].last_seen > 120000UL) continue;
      if (likely_battery(list[i].name) != (pass == 0)) continue;
      char line[48];
      snprintf(line, sizeof(line), "%s%s", count ? "\n" : "", list[i].name);
      strlcat(opts, line, sizeof(opts));
      dd_seen[count] = list[i];
      if (!strcmp(list[i].mac, cur)) cur_idx = count;
      count++;
    }
  }
  dd_count = count;
  if (!count) strcpy(opts, "Searching...");
  if (strcmp(opts, last_opts)) {
    strlcpy(last_opts, opts, sizeof(last_opts));
    lv_dropdown_set_options(dd_bms, opts);
    lv_dropdown_set_selected(dd_bms, cur_idx);
  }
}

void battery_timer_cb(lv_timer_t *t) {
  static BmsData d;
  bms_get(&d);
  uint32_t now = millis();
  char b[160];

  set_label(lbl_bms_status, d.status);
  bool fresh = d.valid && now - d.updated < 30000;

  if (!fresh) {
    set_label(lbl_soc, "--%");
    set_label(lbl_main, "");
    set_label(lbl_line1, "");
    set_label(lbl_line2, "");
    set_label(lbl_cells, "");
    set_label(lbl_prot, "");
  } else {
    snprintf(b, sizeof(b), "%d%%", d.soc);
    set_label(lbl_soc, b);

    snprintf(b, sizeof(b), "%.2f V   %+.1f A", d.volt, d.curr);
    set_label(lbl_main, b);

    snprintf(b, sizeof(b), "%+.0f W   %.1f / %.0f Ah   %d cycles", d.volt * d.curr, d.remain_ah, d.nominal_ah, d.cycles);
    set_label(lbl_line1, b);

    b[0] = 0;
    if (d.ntemp) {
      strlcat(b, "Temp", sizeof(b));
      for (int i = 0; i < d.ntemp; i++) {
        char tt[16];
        snprintf(tt, sizeof(tt), " %.0f" DEG "C", d.temp[i]);
        strlcat(b, tt, sizeof(b));
      }
      strlcat(b, "   ", sizeof(b));
    }
    char fets[48];
    snprintf(fets, sizeof(fets), "Charge %s  Discharge %s", d.chg_fet ? "ON" : "OFF", d.dsg_fet ? "ON" : "OFF");
    strlcat(b, fets, sizeof(b));
    set_label(lbl_line2, b);

    if (d.cells_valid && d.ncell) {
      float mn = d.cell[0], mx = d.cell[0];
      strcpy(b, "Cells");
      for (int i = 0; i < d.ncell; i++) {
        char c[12];
        snprintf(c, sizeof(c), "  %.3f", d.cell[i]);
        strlcat(b, c, sizeof(b));
        if (d.cell[i] < mn) mn = d.cell[i];
        if (d.cell[i] > mx) mx = d.cell[i];
      }
      char dl[24];
      snprintf(dl, sizeof(dl), "\nDifference %.0f mV", (mx - mn) * 1000);
      strlcat(b, dl, sizeof(b));
      set_label(lbl_cells, b);
    }

    const char *prot = bms_protection_text(d.protection);
    if (prot) {
      snprintf(b, sizeof(b), LV_SYMBOL_WARNING " %s", prot);
      set_label(lbl_prot, b);
    } else {
      set_label(lbl_prot, "");
    }
  }

  /* diagnostics */
  if (d.connected) {
    snprintf(b, sizeof(b), "Services: %s\nLast data: %s", d.services[0] ? d.services : "-", d.last_frame[0] ? d.last_frame : "-");
    set_label(lbl_debug, b);
  } else {
    set_label(lbl_debug, "Close the ECO-WORTHY app on your phone: the battery accepts only one connection.");
  }

  refresh_device_list(now);
}
