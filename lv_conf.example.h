// esp32-S3-ws4-caravan v1.0
/**
 * lv_conf.h for LVGL 8.3/8.4 - shared by every Arduino sketch on this machine.
 *
 * Install: copy to <Arduino>/libraries/lv_conf.h (NEXT TO the "lvgl" folder).
 *
 * This file is shared, so it adapts to the board being compiled:
 *   - With PSRAM (Waveshare ESP32-S3-Touch-LCD-4): a large pool in PSRAM, which
 *     keeps internal RAM free for WiFi and Bluetooth. Without it, a big UI runs
 *     out of memory and LVGL crashes on a null pointer inside the UI build.
 *   - Without PSRAM (ESP32-C3 round display): a small pool in internal RAM,
 *     exactly as before.
 *
 * Everything not listed here falls back to LVGL's defaults (lv_conf_internal.h).
 * The full list is in lvgl/lv_conf_template.h.
 */

#if 1 /* "1" = enable this file (the template ships with "0") */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Colour: RGB565, no byte swap */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* ----------------------------------------------------------------------------
 * Memory
 * BOARD_HAS_PSRAM is defined by the Arduino core when PSRAM is enabled under
 * Tools. Check the boot log: the firmware prints the pool size and warns if the
 * pool ended up in internal RAM on a board that has PSRAM.
 * ------------------------------------------------------------------------- */
#define LV_MEM_CUSTOM 0

#if defined(BOARD_HAS_PSRAM)
#define LV_MEM_SIZE (128U * 1024U)
#define LV_MEM_ADR 0
#if LV_MEM_ADR == 0
#define LV_MEM_POOL_INCLUDE <esp32-hal-psram.h>
#define LV_MEM_POOL_ALLOC ps_malloc
#endif
#else /* no PSRAM: small pool in internal RAM */
#define LV_MEM_SIZE (40U * 1024U)
#define LV_MEM_ADR 0
#endif

/* Time base: the sketches call lv_tick_inc() themselves */
#define LV_TICK_CUSTOM 0

/* Touch is polled a little more often than the default 30 ms */
#define LV_INDEV_DEF_READ_PERIOD 20

/* ----------------------------------------------------------------------------
 * Fonts. Only flash is used, so the ones a sketch doesn't reference cost nothing
 * at run time. 32 and 48 are needed by the caravan and boat displays.
 * ------------------------------------------------------------------------- */
#define LV_FONT_MONTSERRAT_14 1 /* default: labels, symbols */
#define LV_FONT_MONTSERRAT_16 1 /* clock, arrows */
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1 /* big values on tiles */
#define LV_FONT_MONTSERRAT_48 1 /* clock, temperature */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/
