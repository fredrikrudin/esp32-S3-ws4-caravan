# esp32-S3-ws4-caravan

Caravan display for the **Waveshare ESP32-S3-Touch-LCD-4 (V4)**, the 480×480 touch board with the CH32V003 IO expander. Built with Arduino and LVGL 8.

![Power tab](power-tab.png)

*The Power tab with example values. Dots move along the lines where energy flows.*

## Features

| Tab | What it shows |
|---|---|
| **Power** | Victron devices over Bluetooth (Instant Readout): solar chargers, battery monitor, DC-DC, AC charger and inverter, in a classic Victron overview style with animated energy flows |
| **Relays** | Up to 8 relays on an external PCF8574 I2C board, with your own names |
| **Temp** | Up to 3 named RuuviTags: temperature, humidity, pressure, battery |
| **Weather** | NTP clock, current weather and a 3-day forecast from Open-Meteo (no API key) |
| **⚙ Settings** | WiFi, weather location, RuuviTags, display brightness, sensor scan interval, Victron devices, relay board, I2C scan |

After 30 seconds without touch, a screen saver shows a dim clock and the battery state of charge (bar, percentage and a charging symbol), and lowers the backlight. A tap wakes it.

All settings are saved in flash and survive restarts.

## Screen saver

To adjust the look, change these lines at the top of `ui_saver.cpp`:

```cpp
#define SAVER_BAR_BG 0x202020     // empty part of the SOC bar
#define SAVER_LOW_COLOR 0x702020  // bar color at low SOC (dim red)
#define SAVER_LOW_SOC 20          // below this % the bar turns red
```

After 30 seconds without touch, a screen saver shows a dim clock and lowers the backlight. A tap wakes it.

## Getting started

1. Install the libraries and set the Arduino IDE options listed below.
2. Open `esp32-S3-ws4-caravan.ino` and upload.
3. On the board, open **⚙ Settings**:
   - **WiFi:** tap Scan, pick your network, enter the password, tap Connect.
   - **Weather location:** type a city (or `City, Country`) and tap Set.
   - **RuuviTags:** pick a tag from the list, tap Add and give it a name. Up to 3 tags; tap one in the list to rename or delete it.
   - **Victron devices:** pick a device, tap Add, then enter a name and its 32-character encryption key.
   - **Relay board:** tap *Scan I2C bus* to find the PCF8574, then choose its address.

The Serial Monitor (115200 baud) logs startup, free memory, WiFi and weather lookups.

## Files

Everything is in one flat folder. `app.h` holds the shared configuration, data types and declarations; each `.cpp` file is one module.

| File | Contents |
|---|---|
| `esp32-S3-ws4-caravan.ino` | `setup()` and `loop()` |
| `app.h` | Configuration, data types, shared state, module functions |
| `state.cpp` | Shared state, loading settings, small helpers |
| `board.cpp` | Display, touch, IO expander, LVGL driver, backlight |
| `net.cpp` | Network task: WiFi, NTP, weather, all flash writes |
| `ble.cpp` | Bluetooth scanning |
| `ruuvi.cpp` | RuuviTag decoding |
| `victron.cpp` | Victron decryption and decoding |
| `relays.cpp` | PCF8574 relays and I2C scan |
| `ui_common.cpp` | Tabs, keyboard, widget helpers, timers |
| `ui_power.cpp` | Power tab |
| `ui_relays.cpp` | Relays tab + relay and I2C settings |
| `ui_temp.cpp` | Temp tab (RuuviTag cards) |
| `ui_weather.cpp` | Weather tab and clock |
| `ui_settings.cpp` | Settings tab (WiFi, location, RuuviTags, display, scan interval) |
| `ui_victron_settings.cpp` | Victron device settings |
| `ui_saver.cpp` | Screen saver |
| `power-tab.png` | Power tab picture for this README |

## Libraries

- **lvgl 8.x** (with the `lv_conf.h` from Waveshare's examples)
- **GFX Library for Arduino**, **SensorLib** and **WS_CH32_IO**: the versions bundled with Waveshare's ESP32-S3-Touch-LCD-4 examples
- **ArduinoJson** v7
- **NimBLE-Arduino** by h2zero, v2.x

## Arduino IDE settings

- Board: ESP32S3 Dev Module
- **PSRAM: OPI PSRAM** (the display framebuffer and LVGL buffers live there)
- **Erase All Flash Before Sketch Upload: Disabled**, otherwise every upload wipes the saved settings

Optional, in `lv_conf.h`:

- Enable `LV_FONT_MONTSERRAT_32` and `LV_FONT_MONTSERRAT_48` for a large clock and temperatures
- To free internal RAM, move LVGL's memory pool to PSRAM:
  ```c
  #define LV_MEM_POOL_INCLUDE <esp32-hal-psram.h>
  #define LV_MEM_POOL_ALLOC ps_malloc
  ```

## Hardware notes

- **Backlight** PWM comes from the CH32V003 and is inverted on this board (0 = full brightness, 255 = off). This is handled in `set_backlight()`.
- **External I2C** shares GPIO15/7 with touch and the CH32V003. The PCF8574 only supports 100 kHz, so the bus slows down briefly while talking to it.
- ⚠️ Many PCF8574 relay boards pull I2C up to 5 V. The ESP32 is **not** 5 V tolerant: power the PCF8574 from 3.3 V, or make sure its pull-ups go to 3.3 V.
- **Victron**: enable *Instant readout via Bluetooth* in VictronConnect and copy the encryption key from *Product info*.
- **Weather** uses plain HTTP: HTTPS needs more internal RAM than is free with WiFi, Bluetooth and the display running.

## Troubleshooting

| Problem | Likely cause |
|---|---|
| Settings are gone after an upload | *Erase All Flash Before Sketch Upload* is enabled |
| City lookup or weather fails | Check the `HTTP` and `Internal heap` lines in the Serial Monitor. Low free RAM breaks network requests |
| Victron device shows "Wrong key" | Key mistyped, or it changed in VictronConnect |
| Victron device shows "No signal" | Out of range, or Instant readout switched off |
| Relays show "No answer from PCF8574" | Wrong address (run the I2C scan), wiring, or power |
| Relays switch the wrong way | Toggle *Active low* in Settings |
| Image shifted a few pixels down | RGB panel lost sync during heavy WiFi/flash activity. Press reset |
