# Memory and latency optimisation

Notes on how to lower internal RAM use and improve touch response and smoothness.
Items are ticked in the plan at the bottom when done.

## Where we are

The ESP32-S3 has about 320 KB of fast **internal RAM** and 8 MB of slower **PSRAM**.
WiFi, Bluetooth, the display driver and LVGL all compete for the internal RAM.

Measured (Serial Monitor), before and after the first round of changes:

| Moment | Before | After |
|---|---|---|
| At start | – | 264 KB |
| After display init | ~127–143 KB | 185 KB |
| After WiFi init | – | 128 KB (WiFi driver ~49 KB) |
| Setup done (after Bluetooth) | ~56–66 KB | 63 KB (Bluetooth ~66 KB) |
| **After WiFi connected** | **~16 KB, largest block 7.6 KB** | **54 KB, largest block 32 KB** |

What made the difference:
- LVGL memory pool moved to PSRAM (`lv_conf.h`, 128 KB pool)
- WiFi is started before Bluetooth, so it gets its DMA-capable buffers first
  (fixed "wifi: Expected to init 4 rx buffer, actual is 0")
- LVGL draw buffers in PSRAM

Bluetooth is now the biggest remaining consumer (~66 KB). HTTPS (40+ KB in one block)
is still not realistic, so Open-Meteo stays on plain HTTP.

NimBLE trimming was tried (peripheral and broadcaster roles off, 1 connection,
`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1`): **no measurable RAM gain** (+0.3 KB).
Almost all of Bluetooth's ~66 KB is the radio controller, which is precompiled into the
ESP32 Arduino core. Shrinking it would need a custom core build (own `sdkconfig`),
which is not worth it at 54 KB free.

## Memory

1. **LVGL memory pool to PSRAM** — biggest single win.
   LVGL reserves a fixed pool (`LV_MEM_SIZE`) in internal RAM at startup; every
   tab, label and button lives there. In `lv_conf.h`, keep `LV_MEM_SIZE` and set:
   ```c
   #define LV_MEM_POOL_INCLUDE <esp32-hal-psram.h>
   #define LV_MEM_POOL_ALLOC ps_malloc
   ```
   Gain: tens of KB. Effort: 2 lines in `lv_conf.h`.

2. **Trim NimBLE** — we only use the observer (scan) and central (battery connection) roles.
   In NimBLE-Arduino's `nimconfig.h`: disable the peripheral and broadcaster roles,
   and set max connections to 1.
   Gain: ~10–20 KB. Effort: small, but it edits the library, so it affects all projects.

3. **Web requests out of internal RAM** — each weather/city lookup allocates and frees
   a text buffer and a JSON document, which fragments internal RAM over time.
   - ArduinoJson v7 custom allocator using PSRAM
   - JSON filter, so only the fields we read are stored
   - `count=5` instead of 10 in the city lookup

   Gain: less fragmentation, larger free blocks. Effort: small (`net.cpp`).

4. **Right-size task stacks** — network task (6 KB) and BMS task (4 KB) are estimates.
   Measure with `uxTaskGetStackHighWaterMark()` and trim to the real need plus a margin.
   Gain: a few KB. Effort: small, after measuring.

Not worth it: moving the small static buffers (timer copies, option strings) to PSRAM,
which saves only a few KB for extra complexity.

## Latency

5. **Switchable diagnostic logging** ⚠️ — BLE callbacks print a lot (BMS notifications,
   battery advertisements, Victron/HTTP errors). Without a computer reading the USB port,
   these prints can block for a moment inside the Bluetooth task.
   Add `#define DEBUG_LOG 0` in `app.h` and route diagnostic prints through it.
   Effort: small.

6. **Only update the visible tab** — the 1-second timers rebuild text for Power, Battery,
   Temp and Settings even when hidden. Check the active tab first, and pause the Power
   tab's flow animations when it isn't visible or the screen saver is showing.
   Effort: small.

7. **Faster touch polling** — in `lv_conf.h`, `LV_INDEV_DEF_READ_PERIOD` from 30 to 10–15 ms.
   Taps and sliders feel snappier for a small CPU cost. Effort: 1 line.

8. **Rendering speed**
   - Once 1–3 have freed internal RAM: move the LVGL draw buffers back from PSRAM to
     internal RAM (`board.cpp`). Easiest speed-up.
   - Later, optionally: LVGL "direct mode", drawing straight into the display's framebuffer.
     Removes the draw buffers and a copy of every changed pixel (best for both RAM and speed),
     but it is a larger change and can cause visible tearing.

## Measure first

Before and after each change:

- Serial log every 30 s: internal RAM free and minimum-ever
  (`heap_caps_get_minimum_free_size`), largest free block, PSRAM free,
  LVGL pool usage (`lv_mem_monitor`), unused stack per task
- On screen: `LV_USE_PERF_MONITOR 1` in `lv_conf.h` shows FPS and CPU load

## Plan

- [x] WiFi before Bluetooth at startup; Bluetooth scanning paused while WiFi joins;
      WiFi failure reason shown in Settings and on the Serial Monitor
- [x] `lv_conf.h`: memory pool to PSRAM (1)
- [ ] Step 1: performance log + `DEBUG_LOG` switch (5)
- [ ] Step 2: visible-tab-only updates, paused animations (6); JSON changes (3)
- [ ] Step 3: `lv_conf.h` touch read period (7)
- [x] NimBLE trimming (2): tried, no measurable gain (see above)
- [ ] Step 4: draw buffers back to internal RAM (8), trim stacks (4)

Remember: `lv_conf.h` and `nimconfig.h` edits live in the libraries folder, are shared
by all sketches, and are lost when the library is updated.

Steps 1–2 are code changes in this project; step 3 is in your `lv_conf.h`;
step 4 depends on the measurements.
