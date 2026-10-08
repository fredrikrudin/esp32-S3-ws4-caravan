<!-- esp32-S3-ws4-caravan v1.0 -->
# Changelog

The version is set by `FW_VERSION` in `app.h` and shown in Settings → Device → About,
on the web page and in the first log line at every start. Every file carries it on its
first line; `python3 tools/set_version.py <version>` changes it everywhere. Every change
gets an entry, newest first.

## 1.0 — 2026-10-08 — first release

Everything below "Development builds" is in this release. New since the last build:

- **Settings → Device → Debug**, with the **Serial monitor** switch: off by default, and
  then only faults (errors and warnings) go to USB serial. Switched on, everything goes there
  as well. The web page's `/log` and the SD card log always get everything. The switch is
  saved and read first thing at start-up, so the boot lines follow it; ESP-IDF's own
  messages follow it too. **Log memory, CPU and battery** moved here from Power.
- License: Creative Commons Attribution-NonCommercial 4.0 (CC BY-NC 4.0), in
  `LICENSE.md`, the README, the main source files and Settings → Device → About,
  with a note that the code was written with the help of Claude (Anthropic's AI).
- Version 1.0, with the version on the first line of every file and
  `tools/set_version.py` to change it everywhere.
- New README screenshots, rendered by the firmware's own UI code with LVGL on a PC
  (`tools/screenshots`), including Weather, Settings → Control, Debug and Swedish.
- Fix: the History chart labelled its 24 hourly bars -23h ... -17h; it now runs -23h ... now.
- History: the totals fit their boxes ("3.37 kWh" was cut off) and the scale on the left shows.
- Shelly: the power reading no longer overlaps the status line under it.
- Weather: fetched every hour while the screen is awake (was every 15 minutes, always),
  not at all while the screen saver is on, and straight away when the screen wakes if an
  update is due. A **Refresh** button under the forecast fetches it at once.

What 1.0 contains, in short: Home, Power (Victron), Ruuvi with 7-day history, Weather,
Battery (BMS, experimental), Shelly, Relays and Settings tabs; schedules; alarms; web page
with charts and JSON; SD card logging, CSV and settings backup; onboard LiPo battery mode;
starting screen; English and Swedish.

## Development builds

The builds before 1.0, newest first.

### dev 1.0.8 — 2026-10-08
- Reverts 1.0.7: mounting the SD card crashed the board. The SD code is back exactly as
  in 1.0.6 (SPI mount, with SD mode as a fallback). Everything else is 1.0.6.

### dev 1.0.7 — 2026-10-08 (withdrawn)
- Tried to bring back an SD card that stopped answering after a restart, by clocking
  extra bits before mounting. Mounting crashed the board, so 1.0.8 takes it out.

### dev 1.0.6 — 2026-10-08
- The Temp tab is now called **Ruuvi**.
- Each RuuviTag card has a chart of the last 7 days: the day's low in blue and high in
  orange (the colours of Victron VRM's charts), weekdays underneath, today on the right.
  The 7-day high and low are shown under the reading. The history is recorded whether
  or not the tab is open, follows the local day, and is kept in flash (saved at most
  every 30 minutes and at midnight), so it survives a restart without an SD card.
  It starts once the clock is set over NTP.
- The card shows the tag's battery and signal while it is heard, and how long it has
  been quiet otherwise.
- Fix: in Swedish, a, o-umlaut and a-ring showed as boxes on the real display. LVGL keeps
  the theme's font when only the font changes, so the translated font is now set on
  the screens and layers themselves.
- `tools/i18n.py update` brings the `.po` files in line with the code (like Poedit's
  Update from POT File); `python3 tools/i18n.py` now runs pot, update and build.

### dev 1.0.5 — 2026-10-08
- Language setting under Settings → Device → Language: English (default) or Swedish.
  Changing it restarts the board. Both are built in; no SD card needed.
- Translations are gettext `.po` files in `lang/` (edit with Poedit). `tools/i18n.py`
  makes the template `lang/caravan.pot` from the code and turns the `.po` files into
  `lang_tables.h`, which is compiled in. See `lang/README.md` to add a language.
- When translated, the built-in Montserrat fonts are swapped for ones that add the Latin-1
  letters (å ä ö é ü ñ ç ...), generated from the same Montserrat Medium TTF. English
  looks exactly as before.
- Swedish dates ("onsdag 8 oktober 2026"), weekday buttons in schedules and forecast days.
- The settings backup includes the language.

### dev 1.0.4 — 2026-10-08
- Home page: a WiFi indicator top right, with the WiFi symbol and four signal bars
  (green, yellow or red by strength; grey when not connected). Updated with the clock.

### dev 1.0.3 — 2026-10-07
- A starting screen, shown while the rest of the firmware comes up behind it.
  Text, size (20/28/32/48, the built-in Montserrat sizes), colour and how long it
  stays are set under Settings → Device → Starting screen, with a live preview.
  Defaults to CABBY in Cabby red for 5 seconds; 0 seconds skips it.

### dev 1.0.2 — 2026-10-07
- Schedules: the week now starts on Monday (bit 0 = Monday). Schedules saved before this
  are shifted by a day and need their days ticked again.
- The web page lists which days each schedule applies to.

### dev 1.0.1 — 2026-10-07

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

### Earlier work (before versioning)

Split from Waveshare's `09_LVGL_Widgets` example into modules; Victron BLE Instant Readout,
RuuviTags, PCF8574 relays, weather and NTP, screen saver, web page with login.
