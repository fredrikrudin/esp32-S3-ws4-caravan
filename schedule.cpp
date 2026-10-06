/* Timed switching for relays and Shelly devices.
 *
 * One schedule per output: an on time, an off time and the days it applies.
 * A window may cross midnight (22:00 to 06:00 works as you would expect).
 *
 * Switching something by hand sets an override, which holds until the next
 * scheduled change, so turning the pump off at 19:00 doesn't get undone a minute
 * later, but the morning switch-on still happens.
 *
 * Nothing runs until the clock is set over NTP: without the time, a schedule
 * would be guesswork.
 */
#include "app.h"

Schedule sched[SCHED_COUNT];

static bool override_on[SCHED_COUNT];   // a manual change is holding
static bool override_state[SCHED_COUNT];
static int8_t last_wanted[SCHED_COUNT];  // -1 = not known yet

void schedule_begin() {
  memset(sched, 0, sizeof(sched));
  if (prefs.getBytesLength("sched") == sizeof(sched)) prefs.getBytes("sched", sched, sizeof(sched));
  for (int i = 0; i < SCHED_COUNT; i++) last_wanted[i] = -1;
}

/* "Water pump" or "Fridge plug (Shelly)" */
const char *schedule_target_name(int i) {
  static char name[32];
  if (i < MAX_RELAYS) {
    snprintf(name, sizeof(name), "%s", relay_cfg.names[i]);
  } else {
    int s = i - MAX_RELAYS;
    snprintf(name, sizeof(name), "%s", shelly_cfg[s].used ? shelly_cfg[s].name : "");
  }
  return name;
}

/* True when this output exists at all */
bool schedule_target_exists(int i) {
  if (i < MAX_RELAYS) return feat_relays && relay_cfg.addr && i < relay_cfg.count;
  int s = i - MAX_RELAYS;
  return feat_shelly && shelly_cfg[s].used;
}

bool schedule_target_on(int i) {
  if (i < MAX_RELAYS) return (relay_state >> i) & 1;
  ShellyData d;
  shelly_get(i - MAX_RELAYS, &d);
  return d.valid && d.on;
}

static void set_target(int i, bool on) {
  if (i < MAX_RELAYS) {
    uint8_t old = relay_state;
    if (on) relay_state |= (1 << i);
    else relay_state &= ~(1 << i);
    if (!relay_apply()) relay_state = old;
  } else {
    shelly_set(i - MAX_RELAYS, on);
  }
}

/* Switch by hand: holds until the next scheduled change */
void schedule_override(int i, bool on) {
  if (i < 0 || i >= SCHED_COUNT) return;
  override_on[i] = sched[i].enabled;  // only meaningful where a schedule runs
  override_state[i] = on;
  set_target(i, on);
  logf("Schedule: %s overridden %s", schedule_target_name(i), on ? "on" : "off");
}

/* Back to the schedule, with effect now */
void schedule_resume(int i) {
  if (i < 0 || i >= SCHED_COUNT) return;
  override_on[i] = false;
  last_wanted[i] = -1;  // apply the schedule again at the next pass
  logf("Schedule: %s back on schedule", schedule_target_name(i));
}

bool schedule_overridden(int i) {
  return i >= 0 && i < SCHED_COUNT && override_on[i];
}

/* Minutes since midnight, or -1 when the clock is not set */
static int local_minutes(int *weekday) {
  time_t t = time(nullptr);
  if (t < 1700000000) return -1;
  t += g.utc_offset;
  struct tm tm;
  gmtime_r(&t, &tm);
  if (weekday) *weekday = tm.tm_wday;  // 0 = Sunday
  return tm.tm_hour * 60 + tm.tm_min;
}

/* Is "now" inside the window? Handles a window that crosses midnight. */
static bool in_window(const Schedule &s, int now_min) {
  if (s.on_min == s.off_min) return false;
  if (s.on_min < s.off_min) return now_min >= s.on_min && now_min < s.off_min;
  return now_min >= s.on_min || now_min < s.off_min;  // crosses midnight
}

/* What a schedule would do right now, as text for the UI */
void schedule_status(int i, char *out, size_t len) {
  const Schedule &s = sched[i];
  if (!s.enabled) {
    snprintf(out, len, "No schedule");
    return;
  }
  int wd = 0;
  int now_min = local_minutes(&wd);
  if (now_min < 0) {
    snprintf(out, len, "%02d:%02d-%02d:%02d (waiting for the clock)", s.on_min / 60, s.on_min % 60, s.off_min / 60, s.off_min % 60);
    return;
  }
  snprintf(out, len, "%02d:%02d-%02d:%02d%s%s", s.on_min / 60, s.on_min % 60, s.off_min / 60, s.off_min % 60,
           (s.days & (1 << wd)) ? "" : " (not today)", override_on[i] ? ", overridden" : "");
}

/* Called from loop(); acts once a minute */
void schedule_service() {
  static int last_min = -1;
  int wd = 0;
  int now_min = local_minutes(&wd);
  if (now_min < 0 || now_min == last_min) return;
  last_min = now_min;

  for (int i = 0; i < SCHED_COUNT; i++) {
    Schedule &s = sched[i];
    if (!s.enabled || !schedule_target_exists(i)) continue;

    bool wanted = (s.days & (1 << wd)) && in_window(s, now_min);
    if (last_wanted[i] == (int8_t)wanted) continue;  // no change at this minute

    /* the schedule has changed its mind: that clears any override */
    if (override_on[i]) {
      override_on[i] = false;
      logf("Schedule: %s override released", schedule_target_name(i));
    }
    last_wanted[i] = wanted;
    if (schedule_target_on(i) != wanted) {
      logf("Schedule: %s %s at %02d:%02d", schedule_target_name(i), wanted ? "on" : "off", now_min / 60, now_min % 60);
      set_target(i, wanted);
    }
  }
}
