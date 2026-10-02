# Changelog

Versions are set by `FW_VERSION` in `app.h` and shown in Settings → About,
on the web page and in the first log line at every start.

## 1.4
- Warnings and alarms: banner on Home, list on the web page and in `/json`,
  thresholds under Settings → Alarms. No temperature alarms (a caravan may be left unheated).
- Graphs: ten-minute sparklines in the Power tiles and on Home, SVG charts on the web page.
- Power saving: WiFi modem sleep, CPU down to 80 MHz while the screen sleeps,
  hidden tabs no longer redrawn, `DEBUG_LOG` switch, performance log every 30 s.
- Onboard LiPo: voltage, rough percentage and a low alarm (read on demand, not at startup).
- Boot banner with the build environment check (LVGL pool location, PSRAM).

## 1.3
- SD card working: the V4 board is SPI, not SDMMC. Probe in Settings tries every combination.
- Logging to Serial, a memory buffer at `/log` and the card; CSV measurements; settings backup.
- System section: restart, shut down, full reset, each with a confirmation.

## 1.2
- Home page in GX Boat Page style, Power page rebuilt around the battery,
  relays as Off | On segmented controls, history charts (24 hours / 7 days).
- Shelly over BLE RPC (off by default), web page with login and remote admin.

## 1.1
- Split into modules, Victron (solar, battery monitor, DC-DC, AC charger, inverter),
  RuuviTags, PCF8574 relays, weather, screen saver, web page.

## 1.0
- First version from Waveshare's `09_LVGL_Widgets` example: dark theme, tabs, WiFi, weather.
