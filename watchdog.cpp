// esp32-S3-ws4-caravan v1.0
/* Watchdog for the main loop.
 *
 * The screen, the web server and most of the services run in loop(). If one of
 * them gets stuck, the display freezes and the web page stops answering, with
 * nothing in the log to say why. A small task checks that loop() keeps coming
 * round. After 30 s without a turn it prints where the loop was stuck, keeps
 * that across the restart (RTC memory survives a software reset) and restarts
 * the board. The first log lines after the restart then say what happened.
 *
 * It also logs why the board last restarted, when that was not a normal
 * power-on or restart: a crash, a brown-out (power dip) or a hardware watchdog.
 */
#include "app.h"
#include <esp_system.h>

#define WD_TIMEOUT_MS 30000
#define WD_MAGIC 0x57444F47  // "WDOG"

RTC_NOINIT_ATTR static uint32_t wd_magic;
RTC_NOINIT_ATTR static char wd_where[24];

static volatile const char *stage = "start";
static volatile uint32_t beat = 0;

void wd_stage(const char *where) {
  stage = where;
  beat = millis();
}

void wd_beat() {
  beat = millis();
}

static const char *reset_text(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_PANIC: return "a crash (panic)";
    case ESP_RST_INT_WDT: return "the interrupt watchdog";
    case ESP_RST_TASK_WDT: return "the task watchdog";
    case ESP_RST_WDT: return "a hardware watchdog";
    case ESP_RST_BROWNOUT: return "a brown-out (the supply voltage dipped)";
    default: return NULL;  // power-on, reset button, software restart, deep sleep
  }
}

static void wd_task(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (millis() - beat < WD_TIMEOUT_MS) continue;
    char where[24];
    strlcpy(where, (const char *)stage, sizeof(where));
    strlcpy(wd_where, where, sizeof(wd_where));
    wd_magic = WD_MAGIC;
    /* straight to USB serial: the log may be what is stuck */
    USBSerial.printf("WATCHDOG: the main loop has been stuck in '%s' for %u s - restarting\n",
                     where, (unsigned)(WD_TIMEOUT_MS / 1000));
    USBSerial.flush();
    delay(200);
    ESP.restart();
  }
}

void watchdog_begin() {
  if (wd_magic == WD_MAGIC) {
    wd_where[sizeof(wd_where) - 1] = 0;
    log_fault("Restarted by the watchdog: the main loop was stuck in '%s'", wd_where);
  } else {
    const char *why = reset_text(esp_reset_reason());
    if (why) log_fault("The board restarted after %s", why);
  }
  wd_magic = 0;
  beat = millis();
  xTaskCreatePinnedToCore(wd_task, "wdog", 3072, NULL, 2, NULL, 0);
}
