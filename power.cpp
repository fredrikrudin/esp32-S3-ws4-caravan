/* Power saving and a periodic performance log.
 *
 * Power saving does two things while the screen saver is showing: the CPU drops
 * from 240 to 80 MHz and the backlight is already off or dim. WiFi modem sleep
 * is on at all times, which costs nothing in responsiveness for a page that is
 * polled, not pushed.
 *
 * The performance log prints one line every 30 s: free memory, the largest
 * block, PSRAM, the LVGL pool and how much stack each task has left. It is the
 * measurement that told us the LVGL pool belonged in PSRAM. */
#include "app.h"

static bool saving_now = false;

/* Called by the screen saver when it starts and stops */
void power_set_saving(bool screen_asleep) {
  if (!feat_powersave) {
    if (saving_now) {  // the setting was switched off while asleep
      setCpuFrequencyMhz(240);
      saving_now = false;
    }
    return;
  }
  if (screen_asleep == saving_now) return;
  saving_now = screen_asleep;
  /* 80 MHz is the lowest that keeps WiFi and Bluetooth working */
  setCpuFrequencyMhz(screen_asleep ? 80 : 240);
  logf("Power: CPU at %d MHz", screen_asleep ? 80 : 240);
}

bool power_saving_active() {
  return saving_now;
}

/* Called from loop() */
void perf_service() {
  static uint32_t next = 0;
  if (next && (int32_t)(millis() - next) < 0) return;
  next = millis() + 30000;
  if (!DEBUG_LOG && !feat_perflog) return;

  lv_mem_monitor_t mem;
  lv_mem_monitor(&mem);
  logf("perf: internal free %u (min %u, largest %u), psram free %u, lvgl %u%% of %u, cpu %d MHz",
       heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
       heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
       heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
       heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
       (unsigned)mem.used_pct, (unsigned)mem.total_size,
       getCpuFrequencyMhz());

  /* stack headroom, so the task sizes can be trimmed with confidence */
  const char *names[] = { "net", "bms", "shelly", "loopTask" };
  char line[120] = "perf: stack left";
  for (const char *n : names) {
    TaskHandle_t h = xTaskGetHandle(n);
    if (!h) continue;
    char part[32];
    snprintf(part, sizeof(part), " %s=%u", n, (unsigned)(uxTaskGetStackHighWaterMark(h)));
    strlcat(line, part, sizeof(line));
  }
  logf("%s", line);
}
