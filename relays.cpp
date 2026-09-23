/* External I2C: PCF8574 relays and bus scan.
   Same bus as touch and the CH32 chip (GPIO15/7), only used from the main loop.
   The PCF8574 is rated for 100 kHz, so the bus is slowed down while talking to it. */
#include "app.h"
#include "WS_CH32_IO.h"

static bool pcf_write(uint8_t addr, uint8_t v) {
  Wire.setClock(100000);
  Wire.beginTransmission(addr);
  Wire.write(v);
  bool ok = (Wire.endTransmission() == 0);
  Wire.setClock(WS_CH32_IO::DEFAULT_I2C_FREQ);
  return ok;
}

static bool pcf_read(uint8_t addr, uint8_t *v) {
  Wire.setClock(100000);
  bool ok = (Wire.requestFrom(addr, (uint8_t)1) == 1);
  if (ok) *v = Wire.read();
  Wire.setClock(WS_CH32_IO::DEFAULT_I2C_FREQ);
  return ok;
}

uint8_t relay_mask() {
  return relay_cfg.count >= 8 ? 0xFF : (uint8_t)((1 << relay_cfg.count) - 1);
}

bool relay_apply() {
  if (!feat_relays || !relay_cfg.addr) {
    pcf_ok = false;
    return false;
  }
  uint8_t on = relay_state & relay_mask();
  uint8_t port = relay_cfg.active_low ? (uint8_t)~on : on;
  pcf_ok = pcf_write(relay_cfg.addr, port);
  return pcf_ok;
}

void relay_read_back() {
  uint8_t port;
  if (feat_relays && relay_cfg.addr && pcf_read(relay_cfg.addr, &port)) {
    pcf_ok = true;
    relay_state = (relay_cfg.active_low ? (uint8_t)~port : port) & relay_mask();
  } else {
    pcf_ok = false;
    relay_state = 0;
  }
}

const char *i2c_guess(uint8_t a) {
  switch (a) {
    case 0x24: return "CH32 IO chip (on board)";
    case 0x51: return "RTC PCF85063 (on board)";
    case 0x14:
    case 0x5D: return "Touch GT911 (on board)";
    case 0x20 ... 0x22:
    case 0x25 ... 0x27: return "PCF8574";
    case 0x23: return "PCF8574 / BH1750";
    case 0x38: return "PCF8574A / AHT20";
    case 0x39 ... 0x3B:
    case 0x3E ... 0x3F: return "PCF8574A";
    case 0x3C ... 0x3D: return "PCF8574A / OLED";
    case 0x40: return "INA219 / HTU21 / Si7021";
    case 0x44 ... 0x45: return "SHT3x / SHT4x";
    case 0x48 ... 0x4B: return "ADS1115 / TMP102";
    case 0x68: return "DS3231 / MPU6050";
    case 0x76 ... 0x77: return "BME280 / BMP280";
    default: return "unknown";
  }
}

int i2c_scan(char *out, size_t len) {
  out[0] = 0;
  int n = 0;
  Wire.setClock(100000);
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      char line[64];
      snprintf(line, sizeof(line), "%s0x%02X  %s", n ? "\n" : "", a, i2c_guess(a));
      strlcat(out, line, len);
      n++;
    }
  }
  Wire.setClock(WS_CH32_IO::DEFAULT_I2C_FREQ);
  return n;
}
