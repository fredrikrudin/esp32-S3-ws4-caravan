# Changelog

The version is set by `FW_VERSION` in `app.h` and shown in Settings → Device → About,
on the web page and in the first log line at every start. Every change from here on
gets an entry, newest first.

## 1.0.1 Stable — 2026-10-07

First version considered stable: everything below has run on the board.

**Pages**
- **Start**: clock between gradient battery and solar gauges, Time To Go, ten-minute
  graphs under each column, inside/outside temperature and relay count, warning banner.
- **Power**: battery in the middle, chargers left, AC side right, DC loads below,
  connections lit in each device's colour, ten-minute graphs inside the tiles.
  Tapping a tile opens the history (24 hours / 7 days).
- **Temp**: up to 3 named RuuviTags. **Weather**: current conditions and a 3-day forecast.
- **Shelly** and **Relays**: Off | On segmented controls; both hide themselves when
  switched off or when nothing is connected.
- **Settings**: four sub-tabs (Connect, Sensors, Control, Device).

**Power and battery**
- Runs on the onboard LiPo: radios off, screen at 10%, last readings kept, a
  "Running on battery" banner, soft shutdown at a threshold you set.
- SW6106 keep-alive, where the chip is fitted, so a light load can't switch the board off.
- WiFi modem sleep, CPU at 80 MHz while the screen sleeps, hidden tabs not redrawn.

**Switching**
- One schedule per relay and Shelly device, with days and a window that may cross midnight.
- Switching by hand holds until the next scheduled change.
- Shelly over WiFi (local HTTP RPC) or Bluetooth (BLE RPC), with failure reasons and backoff.

**Web page**: the same layout as the display, a 24-hour chart and three ten-minute graphs,
weather, Victron, relays, Shelly, schedules with override, board battery, and `/json`,
`/log` and `/files`. Streamed in pieces, so it costs about 1.6 kB rather than 12 kB.

**Storage and diagnostics**: TF card over SPI with a probe that finds the right mode,
log to Serial, memory and card, CSV measurements, settings backup and restore,
alarms with thresholds, performance log, version and environment check at boot.

## Earlier work (before versioning)

Split from Waveshare's `09_LVGL_Widgets` example into modules; Victron BLE Instant Readout,
RuuviTags, PCF8574 relays, weather and NTP, screen saver, web page with login.
