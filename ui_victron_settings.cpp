/* Settings: Victron devices (add, name, encryption key, delete) */
#include "app.h"

static lv_obj_t *dd_vic, *vic_list, *vic_editor, *lbl_vic_edit_title, *ta_vname, *ta_vkey, *lbl_vic_msg;
static lv_obj_t *vic_list_lbl[MAX_VIC];
static char vic_dd_mac[MAX_VIC_SEEN][18];
static uint8_t vic_dd_type[MAX_VIC_SEEN];
static int vic_dd_count = 0;
static int vic_edit_idx = -1;  // slot being edited, -1 = new device
static char vic_edit_mac[18];
static uint8_t vic_edit_type = 0;

static void vic_close_editor() {
  lv_obj_add_flag(vic_editor, LV_OBJ_FLAG_HIDDEN);
  vic_edit_idx = -1;
  kb_hide();
}

static void vic_open_editor(const char *mac, uint8_t type, int slot) {
  vic_edit_idx = slot;
  vic_edit_type = type;
  strlcpy(vic_edit_mac, mac, sizeof(vic_edit_mac));
  lv_label_set_text_fmt(lbl_vic_edit_title, "%s  %s", vic_type_name(type), mac);

  char name[24], keyhex[33] = "";
  if (slot >= 0) {
    xSemaphoreTake(vic_mtx, portMAX_DELAY);
    strlcpy(name, vic_cfg[slot].name, sizeof(name));
    for (int i = 0; i < 16; i++) sprintf(keyhex + i * 2, "%02X", vic_cfg[slot].key[i]);
    xSemaphoreGive(vic_mtx);
  } else {
    char s[8];
    mac_short(mac, s);
    snprintf(name, sizeof(name), "%s %s", vic_type_name(type), s);
  }
  lv_textarea_set_text(ta_vname, name);
  lv_textarea_set_text(ta_vkey, keyhex);
  lv_label_set_text(lbl_vic_msg, vic_supported(type) ? "" : "Note: this device type can't be shown on the Power tab yet.");
  lv_obj_clear_flag(vic_editor, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(tab_settings);
  lv_obj_scroll_to_view_recursive(vic_editor, LV_ANIM_ON);
}

static void vic_list_btn_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  char mac[18];
  uint8_t type;
  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  strlcpy(mac, vic_cfg[i].mac, sizeof(mac));
  type = vic_cfg[i].type;
  xSemaphoreGive(vic_mtx);
  vic_open_editor(mac, type, i);
}

static void vic_rebuild_list() {
  lv_obj_clean(vic_list);
  int count = 0;
  for (int i = 0; i < MAX_VIC; i++) {
    vic_list_lbl[i] = NULL;
    if (!vic_cfg[i].used) continue;
    lv_obj_t *b = lv_btn_create(vic_list);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_add_event_cb(b, vic_list_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_text(l, vic_cfg[i].name);
    vic_list_lbl[i] = l;
    count++;
  }
  if (!count) {
    lv_obj_t *l = make_grey_label(vic_list);
    lv_label_set_text(l, "No devices added yet");
  }
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parse_key(const char *s, uint8_t key[16]) {
  if (strlen(s) != 32) return false;
  for (int i = 0; i < 16; i++) {
    int h = hexval(s[i * 2]), l = hexval(s[i * 2 + 1]);
    if (h < 0 || l < 0) return false;
    key[i] = (h << 4) | l;
  }
  return true;
}

static void vic_save_now() {
  uint8_t key[16];
  if (!parse_key(lv_textarea_get_text(ta_vkey), key)) {
    lv_label_set_text(lbl_vic_msg, "The key must be 32 characters (0-9, A-F).");
    return;
  }
  const char *name = lv_textarea_get_text(ta_vname);

  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  int slot = vic_edit_idx;
  if (slot < 0) {
    for (int i = 0; i < MAX_VIC; i++) {
      if (!vic_cfg[i].used) {
        slot = i;
        break;
      }
    }
  }
  if (slot >= 0) {
    VicCfg &c = vic_cfg[slot];
    c.used = true;
    c.type = vic_edit_type;
    strlcpy(c.mac, vic_edit_mac, sizeof(c.mac));
    if (name[0]) strlcpy(c.name, name, sizeof(c.name));
    else strlcpy(c.name, vic_type_name(c.type), sizeof(c.name));
    memcpy(c.key, key, 16);
    vic_clear_data(vic_data[slot]);
  }
  xSemaphoreGive(vic_mtx);

  if (slot < 0) {
    lv_label_set_text_fmt(lbl_vic_msg, "Max %d devices. Delete one first.", MAX_VIC);
    return;
  }
  cmd_save_vic = true;
  vic_close_editor();
  vic_rebuild_list();
}

static void vic_save_cb(lv_event_t *e) {
  vic_save_now();
}

static void vic_delete_cb(lv_event_t *e) {
  if (vic_edit_idx >= 0) {
    xSemaphoreTake(vic_mtx, portMAX_DELAY);
    memset(&vic_cfg[vic_edit_idx], 0, sizeof(VicCfg));
    vic_clear_data(vic_data[vic_edit_idx]);
    xSemaphoreGive(vic_mtx);
    cmd_save_vic = true;
  }
  vic_close_editor();
  vic_rebuild_list();
}

static void vic_cancel_cb(lv_event_t *e) {
  vic_close_editor();
}

static void vic_add_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_vic);
  if (i >= vic_dd_count) return;  // "Searching..."
  vic_open_editor(vic_dd_mac[i], vic_dd_type[i], -1);
}

void settings_victron(lv_obj_t *page) {
  lv_obj_t *parent = make_section(page, LV_SYMBOL_CHARGE "  Victron devices");
  lv_obj_t *hint = make_grey_label(parent);
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint, "In VictronConnect: enable Instant readout, then copy the key from Product info > Encryption data.");

  lv_obj_t *row = make_row(parent, LV_FLEX_ALIGN_START);
  dd_vic = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_vic, 1);
  lv_dropdown_set_options(dd_vic, "Searching...");
  make_btn(row, LV_SYMBOL_PLUS " Add", vic_add_cb);

  vic_list = lv_obj_create(parent);
  lv_obj_remove_style_all(vic_list);
  lv_obj_set_size(vic_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(vic_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(vic_list, 6, 0);
  lv_obj_clear_flag(vic_list, LV_OBJ_FLAG_SCROLLABLE);

  vic_editor = lv_obj_create(parent);
  lv_obj_set_size(vic_editor, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(vic_editor, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(vic_editor, 8, 0);
  lv_obj_clear_flag(vic_editor, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(vic_editor, LV_OBJ_FLAG_HIDDEN);

  lbl_vic_edit_title = lv_label_create(vic_editor);
  ta_vname = make_ta(vic_editor, "Name");
  lv_obj_set_width(ta_vname, LV_PCT(100));
  lv_textarea_set_max_length(ta_vname, 23);
  ta_vkey = make_ta(vic_editor, "Encryption key (32 characters)", vic_save_now, true);  // hex keypad
  lv_obj_set_width(ta_vkey, LV_PCT(100));
  lv_textarea_set_accepted_chars(ta_vkey, "0123456789abcdefABCDEF");
  lv_textarea_set_max_length(ta_vkey, 32);

  row = make_row(vic_editor, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SAVE " Save", vic_save_cb);
  lv_obj_t *del = make_btn(row, LV_SYMBOL_TRASH " Delete", vic_delete_cb);
  lv_obj_set_style_bg_color(del, lv_palette_main(LV_PALETTE_RED), 0);
  make_btn(row, "Cancel", vic_cancel_cb);

  lbl_vic_msg = lv_label_create(vic_editor);
  lv_obj_set_width(lbl_vic_msg, LV_PCT(100));
  lv_label_set_long_mode(lbl_vic_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(lbl_vic_msg, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(lbl_vic_msg, "");

  vic_rebuild_list();
}

/* Called once per second by power_timer_cb() with copies of the Victron tables */
void victron_settings_refresh(const VicCfg *cfg, const VicData *dat, const VicSeen *seen, uint32_t now) {
  char b[96];

  /* added devices: live status */
  for (int i = 0; i < MAX_VIC; i++) {
    if (!cfg[i].used || !vic_list_lbl[i]) continue;
    const char *st = vic_status(dat[i], cfg[i].type, now);
    if (!strcmp(st, "OK")) snprintf(b, sizeof(b), "%s\n%s  -  OK (%d dBm)", cfg[i].name, vic_type_name(cfg[i].type), dat[i].rssi);
    else snprintf(b, sizeof(b), "%s\n%s  -  %s", cfg[i].name, vic_type_name(cfg[i].type), st);
    set_label(vic_list_lbl[i], b);
  }

  /* devices in range that aren't added yet (don't touch the list while it's open) */
  if (lv_dropdown_is_open(dd_vic)) return;
  char cur[18] = "";
  uint16_t ci = lv_dropdown_get_selected(dd_vic);
  if (ci < vic_dd_count) strlcpy(cur, vic_dd_mac[ci], sizeof(cur));

  static char last_opts[MAX_VIC_SEEN * 40] = "";
  char opts[MAX_VIC_SEEN * 40] = "";
  int count = 0, cur_idx = 0;
  for (int i = 0; i < MAX_VIC_SEEN; i++) {
    if (!seen[i].used || now - seen[i].last_seen > 5UL * 60 * 1000) continue;
    bool added = false;
    for (int j = 0; j < MAX_VIC; j++)
      if (cfg[j].used && !strcmp(cfg[j].mac, seen[i].mac)) added = true;
    if (added) continue;
    char s[8], line[40];
    mac_short(seen[i].mac, s);
    snprintf(line, sizeof(line), "%s%s  %s", count ? "\n" : "", vic_type_name(seen[i].type), s);
    strlcat(opts, line, sizeof(opts));
    strlcpy(vic_dd_mac[count], seen[i].mac, sizeof(vic_dd_mac[count]));
    vic_dd_type[count] = seen[i].type;
    if (!strcmp(seen[i].mac, cur)) cur_idx = count;
    count++;
  }
  vic_dd_count = count;
  if (!count) strcpy(opts, "Searching...");
  if (strcmp(opts, last_opts)) {
    strlcpy(last_opts, opts, sizeof(last_opts));
    lv_dropdown_set_options(dd_vic, opts);
    lv_dropdown_set_selected(dd_vic, cur_idx);
  }
}
