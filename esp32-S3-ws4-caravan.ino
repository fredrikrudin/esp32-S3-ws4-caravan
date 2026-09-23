/*
 * esp32-S3-ws4-caravan
 * Caravan display for the Waveshare ESP32-S3-Touch-LCD-4 (V4, CH32V003 IO expander).
 *
 * Tabs: Home | Power (Victron BLE) | Battery (BMS) | Relays (PCF8574) | Shelly (BLE) |
 *       Temp (RuuviTag) | Weather | Settings
 * Web page with battery, Victron and relay status: http://<board IP>/ (JSON at /json)
 * See README.md for libraries and Arduino IDE settings.
 */

#include "app.h"

void setup() {
  USBSerial.begin(115200);
  USBSerial.printf("Internal heap at start: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  board_init();  // IO expander, touch, display, LVGL
  log_begin();   // Serial + ring buffer (+ TF card when switched on)
  history_begin();
  state_init();
  load_cfg();
  relay_read_back();  // adopt the relays' current state (they keep it across ESP32 restarts)

  build_ui();
  set_backlight(bl_normal);
  USBSerial.printf("Internal heap after UI: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  /* WiFi first: its buffers must be in DMA-capable internal RAM, and Bluetooth
     would otherwise take that memory before WiFi gets a chance. */
  net_start();
  if (!net_wait_wifi_init(5000)) USBSerial.println("WiFi init did not finish in time");
  USBSerial.printf("Internal heap after WiFi init: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  bms_start();  // before ble_start(): the scan reports device names to it
  shelly_start();
  ble_start();
  USBSerial.printf("Setup done. Internal heap free %u, largest block %u\n",
                   heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void loop() {
  ble_scan_service();
  web_service();
  csv_service();
  history_service();
  lv_timer_handler();
  delay(5);
}
