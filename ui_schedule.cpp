// esp32-S3-ws4-caravan v1.0
/* Settings -> Control -> Schedules.
   One output at a time: pick it from the dropdown, set the times and the days.
   Relays come first in the list, then Shelly devices. */
#include "app.h"

static lv_obj_t *dd_target, *sw_enabled, *ta_on, *ta_off, *bm_days, *lbl_sched;
static int sel = 0;  // which output is being edited

static const char *day_map[8];  // bit 0 = Monday; filled when built, translated

static void fmt_time(uint16_t minutes, char *out, size_t len) {
  snprintf(out, len, "%02d:%02d", minutes / 60, minutes % 60);
}

/* "7:5", "0730" and "07:30" all mean half past seven */
static bool parse_time(const char *in, uint16_t *out) {
  int h = 0, m = 0;
  if (sscanf(in, "%d:%d", &h, &m) != 2) {
    int n = atoi(in);
    if (strlen(in) < 3) return false;
    h = n / 100;
    m = n % 100;
  }
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  *out = h * 60 + m;
  return true;
}

static void show_selected() {
  const Schedule &s = sched[sel];
  char b[8];
  if (s.enabled) lv_obj_add_state(sw_enabled, LV_STATE_CHECKED);
  else lv_obj_clear_state(sw_enabled, LV_STATE_CHECKED);
  fmt_time(s.on_min, b, sizeof(b));
  lv_textarea_set_text(ta_on, b);
  fmt_time(s.off_min, b, sizeof(b));
  lv_textarea_set_text(ta_off, b);
  for (int d = 0; d < 7; d++) {
    if (s.days & (1 << d)) lv_btnmatrix_set_btn_ctrl(bm_days, d, LV_BTNMATRIX_CTRL_CHECKED);
    else lv_btnmatrix_clear_btn_ctrl(bm_days, d, LV_BTNMATRIX_CTRL_CHECKED);
  }
  char st[64];
  schedule_status(sel, st, sizeof(st));
  lv_label_set_text_fmt(lbl_sched, "%s: %s", schedule_target_name(sel), st);
}

/* Rebuilds the dropdown: only outputs that exist */
static int dd_index[SCHED_COUNT];
static int dd_count = 0;

static void rebuild_targets() {
  char opts[SCHED_COUNT * 24] = "";
  dd_count = 0;
  for (int i = 0; i < SCHED_COUNT; i++) {
    if (!schedule_target_exists(i)) continue;
    char line[28];
    snprintf(line, sizeof(line), "%s%s", dd_count ? "\n" : "", schedule_target_name(i));
    strlcat(opts, line, sizeof(opts));
    dd_index[dd_count++] = i;
  }
  if (!dd_count) {
    lv_dropdown_set_options(dd_target, "No relays or Shelly devices");
    lv_label_set_text(lbl_sched, "Add a relay board or a Shelly device first.");
    return;
  }
  lv_dropdown_set_options(dd_target, opts);
  lv_dropdown_set_selected(dd_target, 0);
  sel = dd_index[0];
  show_selected();
}

static void target_cb(lv_event_t *e) {
  uint16_t i = lv_dropdown_get_selected(dd_target);
  if (i < dd_count) {
    sel = dd_index[i];
    show_selected();
  }
}

static void save_now() {
  Schedule &s = sched[sel];
  uint16_t on, off;
  if (!parse_time(lv_textarea_get_text(ta_on), &on) || !parse_time(lv_textarea_get_text(ta_off), &off)) {
    lv_label_set_text(lbl_sched, "Times must look like 07:30");
    return;
  }
  LOCK();
  s.on_min = on;
  s.off_min = off;
  s.enabled = lv_obj_has_state(sw_enabled, LV_STATE_CHECKED);
  s.days = 0;
  for (int d = 0; d < 7; d++)
    if (lv_btnmatrix_has_btn_ctrl(bm_days, d, LV_BTNMATRIX_CTRL_CHECKED)) s.days |= (1 << d);
  if (!s.days) s.days = 0x7F;  // no day chosen means every day
  UNLOCK();
  cmd_save_sched = true;
  schedule_resume(sel);  // a changed schedule takes effect at once
  kb_hide();
  show_selected();
}

static void save_cb(lv_event_t *e) {
  save_now();
}

static void resume_cb(lv_event_t *e) {
  schedule_resume(sel);
  show_selected();
}

void settings_schedule(lv_obj_t *page) {
  lv_obj_t *parent = make_section(page, LV_SYMBOL_BELL "  Schedules");

  lv_obj_t *hint = make_grey_label(parent);
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(hint, "One schedule per relay or Shelly device. A window may cross midnight. Switching by hand holds until the next scheduled change.");

  lv_obj_t *row = make_row(parent, LV_FLEX_ALIGN_START);
  dd_target = lv_dropdown_create(row);
  lv_obj_set_flex_grow(dd_target, 1);
  lv_obj_add_event_cb(dd_target, target_cb, LV_EVENT_VALUE_CHANGED, NULL);

  sw_enabled = make_switch_row(parent, "Use a schedule for this one", false, NULL);

  row = make_row(parent, LV_FLEX_ALIGN_START);
  lv_label_set_text(lv_label_create(row), "On");
  ta_on = make_ta(row, "07:30", save_now);
  lv_obj_set_width(ta_on, 90);
  lv_textarea_set_max_length(ta_on, 5);
  lv_label_set_text(lv_label_create(row), "Off");
  ta_off = make_ta(row, "22:00", save_now);
  lv_obj_set_width(ta_off, 90);
  lv_textarea_set_max_length(ta_off, 5);

  bm_days = lv_btnmatrix_create(parent);
  for (int i = 0; i < 7; i++) day_map[i] = tr_day_short(i);
  day_map[7] = "";
  lv_btnmatrix_set_map(bm_days, day_map);
  lv_obj_set_size(bm_days, LV_PCT(100), 46);
  lv_btnmatrix_set_btn_ctrl_all(bm_days, LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_NO_REPEAT);
  lv_obj_set_style_bg_opa(bm_days, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bm_days, 0, 0);
  lv_obj_set_style_pad_all(bm_days, 0, 0);
  lv_obj_set_style_radius(bm_days, 6, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm_days, lv_color_hex(0x0E2233), LV_PART_ITEMS);
  lv_obj_set_style_border_width(bm_days, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_color(bm_days, lv_color_hex(0x2F6FB5), LV_PART_ITEMS);
  lv_obj_set_style_text_color(bm_days, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm_days, lv_color_hex(0x2F6FB5), LV_PART_ITEMS | LV_STATE_CHECKED);

  row = make_row(parent, LV_FLEX_ALIGN_START);
  make_btn(row, LV_SYMBOL_SAVE " Save", save_cb);
  make_btn(row, LV_SYMBOL_REFRESH " Back on schedule", resume_cb);

  lbl_sched = lv_label_create(parent);
  lv_obj_set_width(lbl_sched, LV_PCT(100));
  lv_label_set_long_mode(lbl_sched, LV_LABEL_LONG_WRAP);

  rebuild_targets();
}

/* Once a second from the Relays tab: keeps the dropdown and status line honest */
void schedule_settings_refresh() {
  static int last_count = -1;
  int n = 0;
  for (int i = 0; i < SCHED_COUNT; i++) n += schedule_target_exists(i);
  if (n != last_count) {
    last_count = n;
    rebuild_targets();
  } else if (dd_count && !lv_obj_has_state(ta_on, LV_STATE_FOCUSED) && !lv_obj_has_state(ta_off, LV_STATE_FOCUSED)) {
    char st[64];
    schedule_status(sel, st, sizeof(st));
    lv_label_set_text_fmt(lbl_sched, "%s: %s", schedule_target_name(sel), st);
  }
}
