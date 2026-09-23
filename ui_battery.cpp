/* Battery tab: BMS values and diagnostics.
   The battery itself is chosen under Settings -> Battery. */
#include "app.h"

static lv_obj_t *lbl_bms_status;
static lv_obj_t *lbl_soc, *lbl_main, *lbl_line1, *lbl_line2, *lbl_cells, *lbl_prot, *lbl_debug;

void build_battery_tab() {
  lv_obj_set_flex_flow(tab_battery, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_battery, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tab_battery, 8, 0);

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

void battery_timer_cb(lv_timer_t *t) {
  static BmsData d;
  bms_get(&d);
  uint32_t now = millis();
  char b[160];

  set_label(lbl_bms_status, feat_bms ? d.status : "Battery reading is switched off. Switch it on under Settings.");
  bool fresh = feat_bms && d.valid && now - d.updated < 30000;

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

    /* not every BMS reports current, temperature or the switches */
    if (!isnan(d.curr)) snprintf(b, sizeof(b), "%.2f V   %+.1f A", d.volt, d.curr);
    else snprintf(b, sizeof(b), "%.2f V", d.volt);
    set_label(lbl_main, b);

    b[0] = 0;
    if (!isnan(d.curr)) {
      char w[24];
      snprintf(w, sizeof(w), "%+.0f W   ", d.volt * d.curr);
      strlcat(b, w, sizeof(b));
    }
    char cap[48];
    snprintf(cap, sizeof(cap), "%.1f / %.0f Ah", d.remain_ah, d.nominal_ah);
    strlcat(b, cap, sizeof(b));
    if (d.cycles) {
      char cy[24];
      snprintf(cy, sizeof(cy), "   %d cycles", d.cycles);
      strlcat(b, cy, sizeof(b));
    }
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
    if (d.has_fets) {
      char fets[48];
      snprintf(fets, sizeof(fets), "Charge %s  Discharge %s", d.chg_fet ? "ON" : "OFF", d.dsg_fet ? "ON" : "OFF");
      strlcat(b, fets, sizeof(b));
    }
    set_label(lbl_line2, b);

    if (d.cells_valid && d.ncell) {
      float mn = d.cell[0], mx = d.cell[0];
      strcpy(b, d.ncell > 4 ? "" : "Cells");
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

  /* the device list and the Connect button live in Settings now */
  BmsSeen list[MAX_BMS_SEEN];
  int n = bms_get_seen(list, MAX_BMS_SEEN);
  bms_settings_refresh(list, n, now);
  shelly_settings_refresh(list, n, now);
}
