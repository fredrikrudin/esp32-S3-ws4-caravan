/* Shelly tab: on/off and power for each paired Shelly device,
   plus the "Shelly (Bluetooth)" section in Settings, where devices are paired. */
#include "app.h"

/* ---------- Shelly tab ---------- */
struct ShellyCard {
  lv_obj_t *box, *name, *power, *sub, *seg;
};
static ShellyCard cards[MAX_SHELLY];
static lv_obj_t *lbl_shelly_empty;

static void shelly_seg_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  bool want_on = (lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1);
  shelly_set(i, want_on);  // the task switches and reads the state back
  set_label(cards[i].sub, "Switching...");
}

void build_shelly_tab() {
  lv_obj_set_flex_flow(tab_shelly, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab_shelly, 10, 0);

  lbl_shelly_empty = make_grey_label(tab_shelly);
  lv_obj_set_width(lbl_shelly_empty, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_shelly_empty, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl_shelly_empty, "");

  for (int i = 0; i < MAX_SHELLY; i++) {
    ShellyCard &c = cards[i];
    c.box = lv_obj_create(tab_shelly);
    lv_obj_set_size(c.box, LV_PCT(100), 92);
    lv_obj_set_style_pad_all(c.box, 12, 0);
    lv_obj_clear_flag(c.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c.box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c.box, LV_OBJ_FLAG_HIDDEN);

    c.name = lv_label_create(c.box);
    lv_obj_align(c.name, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(c.name, 240);
    lv_label_set_long_mode(c.name, LV_LABEL_LONG_DOT);

    c.power = lv_label_create(c.box);
    lv_obj_set_style_text_font(c.power, FONT_BIG, 0);
    lv_obj_align(c.power, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_label_set_text(c.power, "--");

    c.sub = make_grey_label(c.box);
    lv_obj_align(c.sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    c.seg = make_segment(c.box, shelly_seg_cb, (void *)(intptr_t)i);
    lv_obj_set_width(c.seg, 170);
    lv_obj_align(c.seg, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

void shelly_timer_cb(lv_timer_t *t) {
  char b[64];
  int shown = 0;

  for (int i = 0; i < MAX_SHELLY; i++) {
    ShellyCard &c = cards[i];
    if (!feat_shelly || !shelly_cfg[i].used) {
      lv_obj_add_flag(c.box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(c.box, LV_OBJ_FLAG_HIDDEN);
    shown++;

    ShellyData d;
    shelly_get(i, &d);
    set_label(c.name, shelly_cfg[i].name);

    if (d.valid && !isnan(d.power)) snprintf(b, sizeof(b), "%.1f W", d.power);
    else strcpy(b, "--");
    set_label(c.power, b);

    if (d.valid && !isnan(d.voltage)) snprintf(b, sizeof(b), "%s  -  %.0f V  %.2f A", d.status, d.voltage, d.current);
    else snprintf(b, sizeof(b), "%s", d.status);
    set_label(c.sub, b);

    if (d.valid) set_segment(c.seg, d.on);
  }

  if (shown) {
    lv_obj_add_flag(lbl_shelly_empty, LV_OBJ_FLAG_HIDDEN);
  } else {
    set_label(lbl_shelly_empty, feat_shelly ? "No Shelly devices paired yet.\nPair them under Settings."
                                            : "Shelly is switched off.\nSwitch it on under Settings.");
    lv_obj_clear_flag(lbl_shelly_empty, LV_OBJ_FLAG_HIDDEN);
  }
}

/* ---------- Settings: Shelly (Bluetooth) ---------- */
static lv_obj_t *dd_shelly, *shelly_list, *lbl_shelly_msg;
static lv_obj_t *shelly_list_lbl[MAX_SHELLY];
static char sh_dd_mac[MAX_BMS_SEEN][18];
static uint8_t sh_dd_type[MAX_BMS_SEEN];
static char sh_dd_name[MAX_BMS_SEEN][32];
static int sh_dd_count = 0;

static void shelly_enable_cb(lv_event_t *e) {
  feat_shelly = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  cmd_save_feat = true;
  lv_label_set_text(lbl_shelly_msg, feat_shelly ? "" : "Switched off: no Bluetooth traffic to Shelly devices.");
  ui_update_tabs();
}

static void shelly_list_rebuild() {
  lv_obj_clean(shelly_list);
  int count = 0;
  for (int i = 0; i < MAX_SHELLY; i++) {
    shelly_list_lbl[i] = NULL;
    if (!shelly_cfg[i].used) continue;
    lv_obj_t *row = make_row(shelly_list, LV_FLEX_ALIGN_START);
    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, shelly_cfg[i].name);
    shelly_list_lbl[i] = l;
    lv_obj_t *del = lv_btn_create(row);
    lv_obj_set_style_bg_color(del, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(
      del, [](lv_event_t *e) {
        shelly_remove((int)(intptr_t)lv_event_get_user_data(e));
        lv_label_set_text(lbl_shelly_msg, "Device removed");
        shelly_list_rebuild();
      },
      LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *dl = lv_label_create(del);
    lv_label_set_text(dl, LV_SYMBOL_TRASH);
    lv_obj_center(dl);
    count++;
  }
  if (!count) {
    lv_obj_t *l = make_grey_label(shelly_list);
    lv_label_set_text(l, "No devices paired yet");
  }
}

static void shelly_add_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_shelly);
  if (i >= sh_dd_count) return;
  int used = 0;
  for (int k = 0; k < MAX_SHELLY; k++) used += shelly_cfg[k].used;
  if (used >= MAX_SHELLY) {
    lv_label_set_text_fmt(lbl_shelly_msg, "Max %d devices. Remove one first.", MAX_SHELLY);
    return;
  }
  shelly_add(sh_dd_mac[i], sh_dd_type[i], sh_dd_name[i]);
  lv_label_set_text(lbl_shelly_msg, "Added. The first connection pairs with the device; this can take a moment.");
  shelly_list_rebuild();
}

void settings_shelly(lv_obj_t *page) {
  lv_obj_t *parent = make_section(page, LV_SYMBOL_POWER "  Shelly (Bluetooth)");
  make_switch_row(parent, "Use Shelly devices", feat_shelly, shelly_enable_cb);

  lv_obj_t *hint = make_grey_label(parent);
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint, "Pair plugs and switches over Bluetooth. Enable Bluetooth on the Shelly first; the board bonds with it on the first connection.");

  lv_obj_t *row = make_row(parent, LV_FLEX_ALIGN_START);
  dd_shelly = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_shelly, 1);
  lv_dropdown_set_options(dd_shelly, "Searching...");
  make_btn(row, LV_SYMBOL_PLUS " Pair", shelly_add_cb);

  shelly_list = lv_obj_create(parent);
  lv_obj_remove_style_all(shelly_list);
  lv_obj_set_size(shelly_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(shelly_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(shelly_list, 6, 0);
  lv_obj_clear_flag(shelly_list, LV_OBJ_FLAG_SCROLLABLE);

  lbl_shelly_msg = lv_label_create(parent);
  lv_obj_set_width(lbl_shelly_msg, LV_PCT(100));
  lv_label_set_long_mode(lbl_shelly_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(lbl_shelly_msg, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(lbl_shelly_msg, feat_shelly ? "" : "Switched off: no Bluetooth traffic to Shelly devices.");

  shelly_list_rebuild();
}

/* Called once per second with the list of named BLE devices in range */
void shelly_settings_refresh(const BmsSeen *seen, int n, uint32_t now) {
  if (lv_dropdown_is_open(dd_shelly)) return;

  char cur[18] = "";
  uint16_t ci = lv_dropdown_get_selected(dd_shelly);
  if (ci < sh_dd_count) strlcpy(cur, sh_dd_mac[ci], sizeof(cur));

  static char last_opts[MAX_BMS_SEEN * 40] = "";
  char opts[MAX_BMS_SEEN * 40] = "";
  int count = 0, cur_idx = 0;
  for (int i = 0; i < n; i++) {
    if (now - seen[i].last_seen > 120000UL) continue;
    if (strncasecmp(seen[i].name, "Shelly", 6)) continue;  // Shelly devices announce themselves by name
    bool added = false;
    for (int k = 0; k < MAX_SHELLY; k++)
      if (shelly_cfg[k].used && !strcmp(shelly_cfg[k].mac, seen[i].mac)) added = true;
    if (added) continue;
    char line[40];
    snprintf(line, sizeof(line), "%s%s", count ? "\n" : "", seen[i].name);
    strlcat(opts, line, sizeof(opts));
    strlcpy(sh_dd_mac[count], seen[i].mac, sizeof(sh_dd_mac[count]));
    strlcpy(sh_dd_name[count], seen[i].name, sizeof(sh_dd_name[count]));
    sh_dd_type[count] = seen[i].addr_type;
    if (!strcmp(seen[i].mac, cur)) cur_idx = count;
    count++;
  }
  sh_dd_count = count;
  if (!count) strcpy(opts, "Searching...");
  if (strcmp(opts, last_opts)) {
    strlcpy(last_opts, opts, sizeof(last_opts));
    lv_dropdown_set_options(dd_shelly, opts);
    lv_dropdown_set_selected(dd_shelly, cur_idx);
  }
}
