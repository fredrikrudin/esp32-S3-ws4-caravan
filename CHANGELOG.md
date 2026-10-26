<!-- esp32-S3-ws4-caravan v1.0 -->
# Changelog

The version is set by `FW_VERSION` in `app.h` and shown in Settings → Device → About,
on the web page and in the first log line at every start. Every file carries it on its
first line; `python3 tools/set_version.py <version>` changes it everywhere. Every change
gets an entry, newest first.

## 1.0 — 2026-10-08 — first release

Everything below "Development builds" is in this release. New since the last build:

- **Settings scroll without opening the keyboard.** The keyboard opened as soon as a finger
  landed on a text field, also when the finger went on to scroll the page. It now opens
  only on a tap.
- **On the LiPo, Bluetooth keeps reading the sensors**: one scan every 5 minutes (5 s long,
  with the radio to itself since WiFi is off) instead of switching Bluetooth off, so Victron
  and RuuviTag values stay current. With Settings open it still scans every 10 s.
  Shelly and the battery BMS don't connect while on the LiPo.
- **Battery mode no longer looks switched off.** Going onto the LiPo dimmed the main screen
  to 10% directly, and nothing brought it back: touches went to the dark screen, so the
  board seemed dead until the screen saver happened to take over. It now shows the screen
  saver straight away (dim clock and the "Running on battery" banner); a tap brings the
  screen back at full brightness and it dims again after 30 s without touch.
- The Ruuvi tab on the display is called **Temp** (the full word doesn't fit beside the
  other tabs here; the mini, with fewer tabs, says Temperature), and with no tags added it
  says "No sensors added."
- **Settings → Device → Debug**, with the **Serial monitor** switch: off by default, and
  then only faults (errors and warnings) go to USB serial. Switched on, everything goes there
  as well. The web page's `/log` and the SD card log always get everything. The switch is
  saved and read first thing at start-up, so the boot lines follow it; ESP-IDF's own
  messages follow it too. **Log memory, CPU and battery** moved here from Power.
- License: Creative Commons Attribution-NonCommercial 4.0 (CC BY-NC 4.0), in
  `LICENSE.md`, the README, the main source files, Settings → Device → About and the web page,
  with a note that the code was written with the help of Claude (Anthropic's AI).
- The CPU is no longer slowed to 80 MHz under the screen saver or in battery mode, and
  the "Slow the CPU while the screen sleeps" switch is gone: the board froze (display and
  web page) soon after slowing down. The screen saver still dims the backlight.
- **The web server is new** (`web_http.cpp`), because the page loaded once and then
  stopped answering, and could freeze the display. Arduino's WebServer serves one
  connection at a time and waits on it: a browser's spare connection that sends nothing
  held it for 5 s, and a phone that went to sleep halfway through a reply for 10 s per
  piece. The new one handles six connections at once, only ever sends what the network
  takes without waiting, and drops connections that stall. When lwIP is short of memory
  for a moment it tries again instead of giving up (the first version did, and the page
  arrived cut off after 1436 bytes), and it lets the browser close the connection, so
  nothing still on its way is lost.
- **The web page is new**: one fixed file (`web_page.h`) that loads `/json` every 30 s and
  draws itself, so the board builds only a few kB of JSON per update instead of the whole
  page. It adapts to phone, tablet and PC, says when the board doesn't answer and keeps the
  last values, and pauses while the browser tab is hidden. `/json` has the charts' history
  and more fields; the old fields are unchanged. `tools/webpreview` runs it on a PC.
- Web page layout: cards fill the gaps in the grid instead of leaving holes, "Last 10
  minutes" takes the full width only with two or more charts.
- The web page follows the display's language (Settings → Device → Language): its texts
  come from the new `/lang` and the same `.po` files as the display (`tools/i18n.py` finds
  the page's `T("...")` texts), and `/json` sends the date, weather, Victron types and
  states and schedule days in it. The login, file list and log pages are translated too.
  Before, the page was English with a Swedish date and warnings.
- A watchdog: if the main loop (screen, web page and most services) stops for 30 s, the
  board restarts, and the first log lines after the restart say where it was stuck. A
  crash, brown-out or hardware watchdog reset is logged at start-up too.
- Renaming the web page (Settings → Connect → Web page) now also changes its address:
  "Cabby 620" is at `http://cabby-620.local/`, straight away and without a restart.
- The web page is now at **`http://caravan.local/`** (was `waveshare.local`), and its
  default title is "Caravan". A title you have set yourself is kept.
- The web page keeps working while the screen saver shows.
- WiFi modem sleep stays on. Turning it off while the web server ran (for faster `.local`
  answers) made the page load only now and then: WiFi and Bluetooth share the antenna, and
  without modem sleep Bluetooth cuts into WiFi traffic and packets are lost. Espressif calls
  modem sleep the essential prerequisite for running both.
- The web server keeps 12 kB of internal RAM free for WiFi: only ~35 kB is free with the
  display, WiFi and Bluetooth running, and replies sent at full speed could use the rest.
  It now sends two segments at a time, waits while memory is short, lets new connections
  wait while all six are busy (they got an error page before), and writes a line to the
  log once a minute when anything went wrong: stalled replies, send errors, or time spent
  waiting for memory or a free connection.
- `.local` keeps working for days: the board announces its name and address once a
  minute (with modem sleep it misses some of the questions, and phones and PCs keep an
  announcement for two minutes), starts mDNS again when WiFi comes back after a drop,
  and opens the web server's port again if it ever stops accepting connections. WiFi
  drops and reconnects are now in the log.
- Internal RAM no longer drains away: it fell from 32 kB to under 7 kB within an hour and
  the web page stopped. The Bluetooth scan ran without end, and NimBLE keeps every device
  still owing a scan response until a scan ends (phones change their address all the
  time, so in a busy place that list only grew). Low internal RAM is logged ("Memory:
  internal RAM low ..."), at most every 10 minutes, even without the performance log.
- **Update sensors** (Settings → Sensors) is now every 10 s to 6 minutes, 10 s by default
  (was continuously to every 10 s). Each scan listens for 3 s and starts with an empty
  list; between scans the radio is WiFi's alone. While Settings is open it scans every
  10 s so devices to add show up, and **Scan now** scans at once. Readings count as lost
  after three missed scans when that is longer than the usual limit, so a long interval
  doesn't show "No signal". An interval saved by an earlier build is not carried over.
- Every log line (`/log`, the card's `.log` file, USB serial) starts with the date and
  time, or the time since start ("+0:00:12") until the clock is set. The first lines after a
  restart showed UTC (the clock survives a restart, the local offset is loaded a moment
  later); they now show the time since start too.
- The web page updates every 30 s (was 5 s), and every 2 minutes once it has been left
  alone for 2 minutes: the sensors are read every 10 s at most, so faster updates only
  fetched the same numbers again, and each one is work for the board. **Update** (next to
  the status) updates at once and makes it quick again, as do switching something and
  coming back to the browser tab.
- The web page shows something at once: grey placeholder cards on the first visit, and on
  later visits the last values this browser saw, dimmed and marked with their time, until
  the board answers. The board sends the page compressed (8 kB instead of 22 kB), made by
  `tools/web_gz.py` from `web_page.h`; the build stops with a reminder if `web_page.h` has
  changed and the tool hasn't been run.
- The bottom of the web page is an **About** card, styled like the others but quieter:
  last update and the current update rate, the project, version, uptime and the log,
  files and JSON links, the source code, and the copyright and licence.
- With battery reading built in (`ENABLE_BMS 1`, off by default) and the battery out of
  reach, the board waits longer between connection
  attempts: 30 s, then 1, 2, 4, 8 and from then on 10 minutes (was every 30 s for ever). Each attempt holds
  Bluetooth memory for up to 8 s. Connect in Settings still tries at once.
- Bluetooth scans are passive except while Settings is open. An active scan asks every
  device for its scan response, and NimBLE keeps each one in internal RAM until it answers
  or the scan ends: in a building full of phones, TVs and headphones that is many kB for
  every scan, and free internal RAM fell to 1 kB. RuuviTags and Victron devices send their
  data in the advertisement itself, so nothing is lost; names (needed only to pick
  devices in Settings) come with the active scans while Settings is open.
- Shelly over Bluetooth recovers by itself: when the Shelly no longer knows the board
  (reset, or its paired list cleared), the board forgets its old pairing and pairs again
  (start pairing on the Shelly); before, it kept trying the old keys and could never pair
  again. Retries after failures wait at most 4 minutes (was up to 32). A read that stops
  moving for 40 s is ended (the log says at which step), so Shelly control can't hang.
- The Shelly task waits until Bluetooth is started: the pairing setup touched NimBLE before
  `NimBLEDevice::init()` and the board crashed at every start ("assert failed:
  npl_freertos_mutex_pend").
- Shelly over Bluetooth never got an answer: the two control characteristics (TX and RX
  control in the mongoose-os RPC profile) had their UUIDs swapped, so the request length
  went to the wrong one and the Shelly never saw a request. The answer's length is now
  also read when the Shelly doesn't notify it.
- Shelly over Bluetooth works with Shelly firmware 2.0 and later, which only answers
  paired devices: the board now pairs ("Just Works", no code) and encrypts the connection.
  Pair once: start Bluetooth pairing on the Shelly (app or web page: Bluetooth → Pair new
  device), then tap the Shelly card on the display within the 3 minutes. Until then the
  card says "Not paired - pair on the Shelly, then tap". Before, every read got no answer.
  Pairing and the RPC exchange follow esp32-shelly-ble-rpc: legacy "Just Works" pairing
  with bonding (with "secure connections" the link was encrypted but no bond was kept),
  MTU 517, writes with or without response as the characteristic allows, and the answer
  read until complete. Each connection logs what it got: "Shelly: secured, encrypted yes,
  bonded yes ...".
- Shelly over Bluetooth: after each read the services and characteristics found on the
  device are deleted again (NimBLE kept them in internal RAM), and the "Memory:" line also
  shows the change during Shelly Bluetooth reads.
- On/off switches show their state at a glance, on the display and the web page: the
  chosen half is green for On and grey for Off (was the same blue for both).
- Shelly tab: each card has a green (on) or grey (off) edge, the power figure is green
  while on, and the status line starts with ON or OFF; the cards are taller so the power
  figure no longer covers the status line. Settings → Shelly explains the pairing as it
  now works, and each added device shows how it is doing ("OK via Bluetooth"). A working
  Bluetooth connection is logged once, a failing one each time.
- **About 58 kB more internal RAM with GFX Library for Arduino 1.6 or later.** The display's
  bounce buffers (internal RAM the picture is fed from) were the biggest single user of it:
  GFX 1.5.3 always takes 40 lines, 2 × 38.4 kB. With 1.6 the firmware chooses 10 lines
  (`BOUNCE_LINES` in `board.cpp`, 2 × 9.6 kB, as in Espressif's own example). It works
  with either version: with 1.5.3 nothing changes, and the boot log says which applies.
- The firmware is now compiled before release with the ESP32 core 3.3.11, NimBLE-Arduino
  2.5.1, ArduinoJson 7.4.3, SensorLib 0.2.6, lvgl 8.4.0 and GFX Library 1.5.3 and 1.6.9.
- A "Memory:" line in the log every 10 minutes: free internal RAM, how it changed, and how
  much of that change happened during Bluetooth scans and battery connection attempts, to
  find what still uses up internal RAM over time.
- While the web server is receiving a request or sending a reply, the next Bluetooth scan
  waits (at most 20 s), so WiFi has the radio to itself during the transfer.
- With little internal RAM left, the web server now slows down (one segment at a time)
  instead of stopping, and only holds back below 6 kB.
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
