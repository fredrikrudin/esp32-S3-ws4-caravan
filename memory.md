# Memory and latency optimisation

Notes on how to lower internal RAM use and improve touch response and smoothness.
Nothing here is implemented yet unless it is ticked.

## Where we are

The ESP32-S3 has about 320 KB of fast **internal RAM** and 8 MB of slower **PSRAM**.
WiFi, Bluetooth, the display driver and LVGL all compete for the internal RAM.

Measured so far (Serial Monitor):

| Moment | Internal RAM free |
|---|---|
| After display init | ~127–143 KB |
| After Bluetooth (NimBLE) start | ~63–79 KB (BLE takes ~63 KB) |
| After WiFi connected | ~16 KB, largest free block ~7.6 KB |

~16 KB free is too tight: WiFi and BLE allocate memory on the fly, and running out
causes crashes or dropped connections. HTTPS (which needs 40+ KB) is not possible,
which is why Open-Meteo is fetched over plain HTTP.

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

- [ ] Step 1: performance log + `DEBUG_LOG` switch (5)
- [ ] Step 2: visible-tab-only updates, paused animations (6); JSON changes (3)
- [ ] Step 3: `lv_conf.h` — memory pool to PSRAM (1), touch read period (7)
- [ ] Step 4: draw buffers back to internal RAM (8), trim stacks (4), optionally NimBLE (2)

Steps 1–2 are code changes in this project; step 3 is in your `lv_conf.h`;
step 4 depends on the measurements.
