# Board notes: Waveshare ESP32-S3-Touch-LCD-4 (V4)

Things that cost time to find out, so the next project doesn't have to.
Applies to the V4 board (CH32V003 helper chip, not the TCA9554 of earlier revisions),
Arduino IDE 2.3.x, LVGL 8.4, GFX Library for Arduino.

## Start here in a new project

Paste this file (or link it) at the start of a new chat, then say what's different.
The fastest order to bring a board up: display → touch → WiFi → Bluetooth → SD card,
each verified before adding the next.

## Hardware

**TF card is SPI, not SDMMC.** Waveshare's own `10_LVGL_SD` example uses `SD_MMC` and
fails on the V4 board with `send_op_cond ... 0x107` on every card. What works:

```cpp
SPI.begin(2 /*SCK*/, 4 /*MISO*/, 1 /*MOSI*/, -1);
SD.begin(44 /*any harmless pin; real CS is on the board*/, SPI, 20000000);
```

Release the pins first: GPIO2 and GPIO1 are also the display's setup bus (`Arduino_SWSPI`)
and stay driven after the panel starts, which stops the card answering.

```cpp
gpio_reset_pin(GPIO_NUM_2); gpio_reset_pin(GPIO_NUM_1); gpio_reset_pin(GPIO_NUM_4);
pinMode(1, INPUT_PULLUP); pinMode(4, INPUT_PULLUP);
```

**Backlight is inverted** and lives on the CH32 chip: `WS_CH32_IO::setPwm(Wire, pwm)`,
where 0 is full brightness and 255 is off. A default of "100% = 255" switches the screen off
and looks like a dead board.

**CH32 registers read back as 0x00** (direction and output), so don't trust a read-modify-write.
`WS_CH32_IO::OUT_DISPLAY_ON` keeps the display and touch alive; keep those bits set in any write.

**Touch (GT911) sometimes doesn't answer after an upload.** A full power cycle clears it.
Not a code fault; don't chase it.

**RGB panel loses sync** (whole image shifted down, tab bar too low) when PSRAM is busy.
Mitigations that worked: pixel clock 10–12 MHz via `prefer_speed`, fewer flash writes at
startup, less PSRAM traffic. The real fix is bounce buffers, which need a newer
GFX Library for Arduino than the bundled one.

**A page-layout mistake looks exactly the same.** If a reset clears it, it's the panel;
if it comes straight back, it's LVGL layout. Adding a button aligned to a page's bottom
edge was enough to trigger it once.

## Memory (the binding constraint)

Internal RAM is ~320 kB and everything competes for it. Measured on this project:

| Stage | Free internal RAM |
|---|---|
| At start | 264 kB |
| After display init | 185 kB |
| After WiFi init | 129 kB (WiFi ~49 kB) |
| After Bluetooth | 63 kB (BLE ~66 kB) |
| After WiFi connected | 54 kB, largest block 32 kB |

What helped, in order of effect:

1. **LVGL memory pool to PSRAM** in `lv_conf.h` (+45–60 kB):
   ```c
   #define LV_MEM_POOL_INCLUDE <esp32-hal-psram.h>
   #define LV_MEM_POOL_ALLOC ps_malloc
   ```
2. **Start WiFi before Bluetooth.** WiFi needs DMA-capable internal RAM; if Bluetooth takes
   it first you get `wifi: Expected to init 4 rx buffer, actual is 0` and no WiFi at all.
3. **LVGL draw buffers in PSRAM** (`MALLOC_CAP_SPIRAM`), 60 lines is plenty.

What didn't help: **NimBLE role trimming and `MEM_ALLOC_MODE_EXTERNAL`** gained 0.3 kB.
Most of Bluetooth's 66 kB is the radio controller, which is precompiled into the core.

**HTTPS is not realistic** here: it needs 40+ kB in one block. Use plain HTTP for public data
(Open-Meteo works fine over HTTP). `connection refused (-1)` with low free RAM means exactly this.

## LVGL 8 gotchas

- **No margins** (`lv_obj_set_style_margin_*` is LVGL 9). Use a padded parent row instead.
- **`lv_label_set_text_fmt` has no `%f`** unless `LV_SPRINTF_USE_FLOAT` is on. Use `snprintf`.
- **Built-in Montserrat fonts have no ©, å, ä, ö.** Only ASCII plus ° and the symbols.
  A custom font can be generated (digits-only at 96 px costs ~23 kB of flash).
- **Tabs can't be removed**, only hidden: `LV_BTNMATRIX_CTRL_HIDDEN` on the tab button plus
  `lv_btnmatrix_set_btn_width(..., 1)` so the gap is a sliver.
- **Helpers must be defined before use** in a `.cpp`. Moving a function that calls a helper
  above that helper is a classic compile error after an edit.
- **A re-clicked text area doesn't re-send `LV_EVENT_FOCUSED`**, so handle `LV_EVENT_CLICKED`
  too if a keyboard should reappear.
- **Segmented Off | On controls** (`lv_btnmatrix`, one_checked) beat toggle buttons for anything
  you don't want switched by a stray touch, and match the Victron switch-pane look.

## Wireless protocols worth keeping

- **Victron Instant Readout**: manufacturer data `0x02E1`, record `0x10`; AES-128-CTR with the
  16-bit counter little-endian; the byte before the ciphertext must equal `key[0]`. Payloads are
  little-endian bit fields. Needs "Instant readout" enabled in VictronConnect.
- **RuuviTag**: manufacturer data `0x0499`, formats 5 and 3, big-endian fields.
- **JBD / Jiabaida BMS**: service `0xFF00`, write `0xFF02`, notify `0xFF01`;
  request `DD A5 <cmd> 00 <chk_hi> <chk_lo> 77`; checksum = sum of status, length and data,
  inverted plus one.
- **ECO-WORTHY BWOB**: not JBD. Service `0x0001`, write `0x0002`, notify `0x0003`,
  commands `AA 00/20/21/22`; reply `0x21` has voltage (mV), SOC and capacity, reply `0x22`
  has eight cell voltages (big-endian). Current and temperature are still unidentified.
- **Shelly Gen2/3 BLE RPC**: service `5f6d4f53-5f52-5043-5f53-56435f49445f`, data/tx_ctl/rx_ctl
  characteristics; 4-byte big-endian length, then JSON in MTU-sized chunks. Requires bonding.
- **Scanning**: passive scan misses device names; use active scan if you need them.
  Pause scanning while WiFi joins, or the handshake can time out.

## Arduino IDE

- **Erase All Flash Before Sketch Upload: Disabled**, or every upload wipes saved settings.
- **PSRAM: OPI PSRAM** must be on.
- **Two `.ino` files in one folder won't build**: both define `setup()`. Keep test sketches
  in their own folder.
- `lv_conf.h` and `nimconfig.h` live in the libraries folder, are shared by every sketch,
  and are lost when the library updates. Note your edits somewhere.

## Habits that saved time here

- **Log to three places**: Serial, a PSRAM ring buffer served at `/log`, and the TF card.
  Reading a log on a phone beats carrying a laptop to the vehicle.
- **Settings backup to the card** (`/settings.json`) makes an accidental flash erase harmless.
- **A probe beats a guess**: when a bus won't come up, try every combination in code and log
  what works, rather than reasoning about which is right.
- **Ask what a reset does.** It separates hardware state from code state in one step.

## Firmware patterns that worked

**One thread owns the screen.** All LVGL calls happen in the Arduino loop task. Background
tasks (network, BLE callbacks, BMS, Shelly) only write to plain structs behind a mutex, and
UI timers copy those structs and update labels. No LVGL call ever happens from a callback.

**Copy, then draw.** A timer takes the mutex, `memcpy`s the table it needs, releases the
mutex, and only then formats text. Holding a lock while touching LVGL invites both jitter
and deadlocks.

**All flash writes in one place.** The UI sets a `cmd_save_*` flag; the network task performs
the write. Keeps NVS access single-threaded and stops write storms while a slider is dragged.
Also compare before writing: `if (prefs.getString("ssid","") != ssid) prefs.putString(...)`.

**Config as blobs.** Arrays of small POD structs (`RuuviCfg[3]`, `VicCfg[6]`, `ShellyCfg[4]`)
are stored with `putBytes`/`getBytes`, guarded by `getBytesLength() == sizeof(...)`. Changing
a struct silently invalidates the old blob, which is the desired behaviour. Version the key
name if that ever isn't wanted.

**Feature switches that really switch off.** A `feat_*` flag hides the tab *and* stops the
work: no BLE decoding, no I2C traffic, no connections. A switch that only hides the UI is a lie.

**Set text only when it changes** (`set_label()` comparing with `lv_label_get_text`) to avoid
pointless redraws once a second across dozens of labels.

**A separate LVGL screen for the screen saver**, rather than an overlay, so the main page isn't
redrawn while it shows. `lv_disp_get_inactive_time(NULL)` drives the timeout,
`lv_scr_load()` switches, and the waking tap is consumed by the saver's own press handler.

## Network and web

**Open-Meteo needs no key** and works over plain HTTP. Geocoding matches place names only, so
split "City, Country" yourself and filter the results by `country` or `country_code`.

**Timezone for free:** `&timezone=auto` returns `utc_offset_seconds`, which follows DST. Run
the clock on UTC (`configTime(0, 0, ...)`) and add that offset; no TZ database needed.

**Report WiFi failures properly.** `WiFi.onEvent` plus `ARDUINO_EVENT_WIFI_STA_DISCONNECTED`
gives a reason code; showing "wrong password" versus "network not found" beats a generic
failure message.

**Pause BLE scanning while joining WiFi.** They share one radio; an active scan at 50% duty can
make the association time out.

**A web server in the loop task** (`WebServer`, `server.handleClient()`) is simplest and reads
the same data the screen shows, with no extra locking. A page render takes a few milliseconds.

**mDNS** (`MDNS.begin("waveshare")`) removes the need to know the IP. Works on iOS, macOS and
Windows; some Android versions don't resolve `.local`.

**Cookie login without TLS:** a random token per boot, `Set-Cookie` after a correct password,
`server.collectHeaders({"Cookie"})` to read it back. Enough to keep strangers on a campsite
network out; not real security. HTTPS needs 40+ kB in one block, which this board hasn't got
(see Memory).

## Diagnostics

**Log to three places** (Serial, PSRAM ring buffer at `/log`, TF card). The browser view is what
makes field debugging bearable.

**Log state changes, not states.** Printing the same status every poll buries the log; compare
with the previous value first.

**Build a probe when a bus won't come up.** Trying every combination in code and logging the
result found the SD card's SPI wiring in one run, after a day of reasoning got nowhere.

**Ask what a reset does.** Hardware state (panel sync, a stuck touch controller, an SD card left
in SPI mode) clears on reset; code faults come straight back.

## Protocol decoding, practically

**Get the frame shape first**: start byte, length field, checksum, end byte. Feed every
notification to every decoder; they reject what isn't theirs, so mis-detection costs nothing.

**Prove the protocol by a valid frame**, not by a matching UUID. "It answered correctly" is the
only reliable detector, which is what finally identified the battery.

**Ask for the vendor app's numbers** at the same moment as a capture. Matching known values to
raw bytes is the whole game for undocumented protocols.
