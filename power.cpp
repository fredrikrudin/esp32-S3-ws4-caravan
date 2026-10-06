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
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include "WS_CH32_IO.h"

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

  static uint32_t worst = 0xFFFFFFFF;
  uint32_t now_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (now_free < worst) worst = now_free;

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
  char line[140] = "perf: stack left";
  bool tight = false;
  for (const char *n : names) {
    TaskHandle_t h = xTaskGetHandle(n);
    if (!h) continue;
    unsigned left = uxTaskGetStackHighWaterMark(h);
    if (left < 1024) tight = true;  // under 1 kB is close to a stack overflow
    char part[32];
    snprintf(part, sizeof(part), " %s=%u%s", n, left, left < 1024 ? "!" : "");
    strlcat(line, part, sizeof(line));
  }
  logf("%s%s", line, tight ? "  <- marked tasks are close to overflowing" : "");
}

/* ---------- running on the onboard LiPo ----------
 * There is no "external power lost" signal, so it is inferred from the cell:
 * not charging, and the voltage falling steadily. On battery the board becomes a
 * local display: WiFi and Bluetooth off, screen dimmed, CPU slowed, and whatever
 * was last collected stays on screen. Below the shutdown threshold it closes the
 * log, unmounts the card and sleeps, so the cell is never run flat.
 */
static bool on_battery = false;
static lv_obj_t *batt_banner = nullptr, *batt_banner_lbl = nullptr;

bool power_on_battery() {
  return on_battery;
}

static void make_banner() {
  if (batt_banner) return;
  batt_banner = lv_obj_create(lv_layer_top());  // on top of every tab and screen
  lv_obj_remove_style_all(batt_banner);
  lv_obj_set_size(batt_banner, 480, 34);
  lv_obj_align(batt_banner, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(batt_banner, lv_color_hex(0x8A4D06), 0);
  lv_obj_set_style_bg_opa(batt_banner, LV_OPA_COVER, 0);
  lv_obj_clear_flag(batt_banner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(batt_banner, LV_OBJ_FLAG_SCROLLABLE);
  batt_banner_lbl = lv_label_create(batt_banner);
  lv_obj_set_style_text_color(batt_banner_lbl, lv_color_white(), 0);
  lv_obj_center(batt_banner_lbl);
}

static void enter_battery_mode(int pct) {
  on_battery = true;
  logf("Power: external power lost, running on the battery (%d%%)", pct);

  /* a local display from here on */
  ble_pause_scan(true);
  if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);

  set_backlight(10);
  setCpuFrequencyMhz(80);

  make_banner();
  lv_obj_clear_flag(batt_banner, LV_OBJ_FLAG_HIDDEN);
}

static void leave_battery_mode() {
  on_battery = false;
  logf("Power: external power is back - restarting to bring WiFi and Bluetooth up");
  if (batt_banner) lv_obj_add_flag(batt_banner, LV_OBJ_FLAG_HIDDEN);
  set_backlight(bl_normal);
  lv_refr_now(NULL);
  delay(500);
  ESP.restart();  // the cleanest way back: both radios start from scratch
}

static void soft_shutdown(int pct) {
  logf("Power: battery at %d%% - shutting down", pct);
  make_banner();
  lv_label_set_text(batt_banner_lbl, "Battery empty - shutting down");
  lv_obj_clear_flag(batt_banner, LV_OBJ_FLAG_HIDDEN);
  lv_refr_now(NULL);
  delay(1500);
  sd_log_unmount();  // flush and close the log first
  set_backlight(0);
  delay(200);
  esp_deep_sleep_start();  // the reset button, or external power, starts it again
}

/* Called from loop(); samples the cell every 20 s.
 * The reading jitters by about +-50 mV, so it is smoothed before anything is
 * decided, and a USB host attached means external power whatever the voltage
 * says. Without a USB host (a plain charger, or a wired 5 V supply) the
 * smoothed voltage has to drop a long way below the charger's float level
 * before battery mode starts. */
void power_battery_service() {
  if (!feat_battmode) return;
  static uint32_t next = 0;
  if (next && (int32_t)(millis() - next) < 0) return;
  next = millis() + 20000;

  if (millis() < 120000) return;  // never act on the first two minutes of readings

  float v;
  int pct;
  bool charging;
  if (!board_battery(&v, &pct, &charging)) {  // no cell fitted, or no reading
    if (on_battery) logf("Power: lost the battery reading");
    return;
  }
  if (v < 3.0f || v > 4.6f) {  // implausible: don't act on a bad measurement
    logf("Power: ignoring a battery reading of %.2f V", v);
    return;
  }

  /* smooth: a running average over about a minute */
  static float avg = 0;
  avg = avg ? avg * 0.7f + v * 0.3f : v;

  bool usb = (bool)USBSerial;  // a USB host is attached: external power, certainly

  if (!on_battery) {
    /* Two independent signs are wanted before the radios go off:
       no USB host, and the smoothed voltage clearly below the charge rail. */
    if (!usb && avg < 4.05f && !charging) {
      static int low = 0;
      if (++low >= 2) {  // about 40 s of agreement
        low = 0;
        enter_battery_mode((int)(avg > 4.0f ? 100 : pct));
      }
    }
  } else {
    if (usb || charging || avg > 4.10f) {  // power is back
      leave_battery_mode();
      return;
    }
    static int low_count = 0;
    low_count = (pct <= batt_shutdown_pct) ? low_count + 1 : 0;
    if (low_count >= 2) {  // two readings below the threshold
      soft_shutdown(pct);
      return;
    }
    if (batt_banner_lbl) {
      char b[64];
      snprintf(b, sizeof(b), LV_SYMBOL_BATTERY_2 "  Running on battery  -  %d%%  (%.2f V)", pct, avg);
      lv_label_set_text(batt_banner_lbl, b);
    }
  }
}

/* ---------- battery and USB status on the Serial Monitor ----------
 * One line every 5 s while "Log memory and CPU" is on in Settings -> Power, or
 * with DEBUG_LOG set. USB is detected through the CDC port: a connected host
 * means USB power, though a plain charger gives no CDC connection, so the
 * voltage and its trend are what really tell the story.
 */
void battery_monitor() {
  static uint32_t next = 0;
  if (next && (int32_t)(millis() - next) < 0) return;
  next = millis() + 5000;
  if (!DEBUG_LOG && !feat_perflog) return;

  float v = 0;
  uint16_t raw = 0;
  bool read_ok = WS_CH32_IO::readBatteryVoltage(Wire, &v, &raw, 4);
  bool usb = (bool)USBSerial;  // true while a USB host is attached

  static float prev = 0;
  const char *trend = "steady";
  if (prev > 0) {
    if (v - prev > 0.005f) trend = "rising";
    else if (prev - v > 0.005f) trend = "falling";
  }
  prev = v;

  if (!read_ok) {
    logf("BATT: no reading from the CH32 (I2C problem?)  USB=%s", usb ? "yes" : "no");
    return;
  }
  if (!raw || v < 2.5f) {
    logf("BATT: no cell connected (raw=%u, %.3f V)  USB=%s", raw, v, usb ? "yes" : "no");
    return;
  }
  logf("BATT: %.3f V (raw=%u) %s%s  USB=%s  mode=%s", v, raw, trend,
       v >= 4.15f ? ", full or charging" : "", usb ? "yes" : "no",
       on_battery ? "battery" : "external");
}
