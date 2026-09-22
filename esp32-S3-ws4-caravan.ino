/*
 * esp32-S3-ws4-caravan
 * Caravan display for the Waveshare ESP32-S3-Touch-LCD-4 (V4, CH32V003 IO expander).
 *
 * Tabs: Power (Victron BLE) | Battery (BMS, experimental) | Relays (PCF8574) | Temp (RuuviTag) | Weather | Settings
 * See README.md for libraries and Arduino IDE settings.
 */

#include "app.h"

void setup() {
  USBSerial.begin(115200);
  USBSerial.printf("Internal heap at start: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  board_init();  // IO expander, touch, display, LVGL
  state_init();
  load_cfg();
  relay_read_back();  // adopt the relays' current state (they keep it across ESP32 restarts)

  build_ui();
  set_backlight(bl_normal);
  USBSerial.printf("Internal heap after UI: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  bms_start();  // before ble_start(): the scan reports device names to it
  ble_start();
  USBSerial.printf("Internal heap after BLE: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  net_start();
  USBSerial.printf("Setup done. Internal heap free %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void loop() {
  ble_scan_service();
  lv_timer_handler();
  delay(5);
}
