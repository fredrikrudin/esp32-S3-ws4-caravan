/* Board hardware: IO expander, touch, RGB display, LVGL driver setup, backlight */
#include "app.h"
#include "Arduino_GFX_Library.h"
#include "TouchDrvGT911.hpp"
#include "WS_CH32_IO.h"  // after TouchDrvGT911 (it clears SensorLib's DEFAULT_SDA/SCL macros)

#define LVGL_TICK_PERIOD_MS 2

HWCDC USBSerial;

static TouchDrvGT911 GT911;
static int16_t touch_x[5], touch_y[5];
static uint8_t gt911_i2c_addr = 0;
static bool gt911_available = false;

static lv_disp_draw_buf_t draw_buf;

static Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /* DC */, 42 /* CS */,
  2 /* SCK */, 1 /* MOSI */, GFX_NOT_DEFINED /* MISO */);

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  40 /* DE */, 39 /* VSYNC */, 38 /* HSYNC */, 41 /* PCLK */,
  46 /* R0 */, 3 /* R1 */, 8 /* R2 */, 18 /* R3 */, 17 /* R4 */,
  14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
  5 /* B0 */, 45 /* B1 */, 48 /* B2 */, 47 /* B3 */, 21 /* B4 */,
  1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
  1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
  /* A lower pixel clock needs less PSRAM bandwidth, which stops the panel from
     losing sync (the whole image shifted down) when WiFi, Bluetooth, the SD card
     and the log buffer are all using PSRAM. 10 MHz is about 47 frames/s.
     The proper fix is bounce buffers, which need a newer GFX Library for Arduino. */
  0 /* pclk_active_neg */, 10000000 /* prefer_speed */, false /* useBigEndian */,
  0 /* de_idle_high */, 0 /* pclk_idle_high */);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480 /* width */, 480 /* height */, rgbpanel, 2 /* rotation */, true /* auto_flush */,
  bus, GFX_NOT_DEFINED /* RST */, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

#if LV_USE_LOG != 0
static void my_print(const char *buf) {
  USBSerial.printf("%s", buf);
  USBSerial.flush();
}
#endif

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(disp);
}

static void lvgl_tick(void *arg) {
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (!gt911_available) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  uint8_t touched = GT911.getPoint(touch_x, touch_y, GT911.getSupportTouchPoint());
  if (touched > 0) {
    for (int i = 0; i < touched; ++i) {
      int16_t tx = touch_x[i];
      int16_t ty = touch_y[i];
      switch (gfx->getRotation()) {
        case 0: break;
        case 1:
          tx = touch_y[i];
          ty = gfx->height() - touch_x[i];
          break;
        case 2:
          tx = gfx->width() - touch_x[i];
          ty = gfx->height() - touch_y[i];
          break;
        case 3:
          tx = gfx->width() - touch_y[i];
          ty = touch_x[i];
          break;
      }
      data->state = LV_INDEV_STATE_PR;
      data->point.x = tx;
      data->point.y = ty;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void boot_i2c_scan() {
  USBSerial.println("Scanning I2C bus...");
  int n = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      USBSerial.printf("I2C device found at address 0x%02X\n", address);
      n++;
      if (address == GT911_SLAVE_ADDRESS_L || address == GT911_SLAVE_ADDRESS_H) gt911_i2c_addr = address;
    }
  }
  USBSerial.println(n ? "I2C scan completed" : "No I2C devices found");
}

static bool init_gt911(int sda_pin, int scl_pin) {
  Wire.begin(sda_pin, scl_pin);
  delay(100);
  boot_i2c_scan();
  if (gt911_i2c_addr == 0) {
    USBSerial.println("GT911 not found in I2C scan");
    return false;
  }
  GT911.setPins(-1, -1);
  if (GT911.begin(Wire, gt911_i2c_addr, sda_pin, scl_pin)) {
    USBSerial.printf("GT911 initialized at 0x%02X\n", gt911_i2c_addr);
    return true;
  }
  USBSerial.printf("Failed to initialize GT911 at 0x%02X\n", gt911_i2c_addr);
  return false;
}

void board_init() {
  if (!WS_CH32_IO::begin(Wire, WS_CH32_IO::DEFAULT_I2C_SDA, WS_CH32_IO::DEFAULT_I2C_SCL,
                         WS_CH32_IO::DEFAULT_I2C_FREQ, &USBSerial)) {
    USBSerial.println("CH32V003 IO expander init failed");
  }

  gt911_available = init_gt911(WS_CH32_IO::DEFAULT_I2C_SDA, WS_CH32_IO::DEFAULT_I2C_SCL);
  if (gt911_available) {
    GT911.setHomeButtonCallback([](void *user_data) {
      USBSerial.println("Home button pressed!");
    },
                                NULL);
    GT911.setMaxTouchPoint(1);
  } else {
    USBSerial.println("GT911 not found; LVGL will run without touch input.");
  }

  gfx->begin();
  delay(50);  // let the panel settle before the rest of the system wakes up
  uint32_t screen_w = gfx->width();
  uint32_t screen_h = gfx->height();

  lv_init();
  USBSerial.printf("Internal heap after display init: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  /* LVGL draw buffers in PSRAM, so internal RAM stays free for WiFi and BLE */
  size_t buf_px = screen_w * 60;
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) USBSerial.println("LVGL buffer allocation failed!");

#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print);
#endif

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_px);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screen_w;
  disp_drv.ver_res = screen_h;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.sw_rotate = 1;
  lv_disp_drv_register(&disp_drv);

  if (gt911_available) {
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
  }

  const esp_timer_create_args_t tick_args = {
    .callback = &lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t tick_timer = NULL;
  esp_timer_create(&tick_args, &tick_timer);
  esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000);
}

/* Backlight PWM comes from the CH32V003 chip. Inverted on this board: 0 = full, 255 = off */
void set_backlight(uint8_t pct) {
  uint8_t pwm = 255 - (uint16_t)pct * 255 / 100;
  static int last = -1;
  if (pwm == last) return;  // skip repeated I2C writes
  if (WS_CH32_IO::setPwm(Wire, pwm)) last = pwm;
  else USBSerial.printf("Backlight: setPwm(%u) failed\n", pwm);
}
