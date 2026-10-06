/* SW6106 power bank controller (I2C address 0x3C), where fitted.
 *
 * The chip switches its output off when it sees a light load, which is what a
 * board running on battery looks like, especially with the screen dimmed, the
 * CPU at 80 MHz and both radios off. Two ways to stop it:
 *   - write 0x0A to register 0x38 once at startup, and
 *   - write 0x01 to register 0x03 about once a second as a keep-alive.
 * Both are done here; the keep-alive is the one that matters in practice.
 *
 * Not every board revision has this chip. If it does not answer at startup,
 * nothing further is attempted and one line is logged.
 */
#include "app.h"
#include <Wire.h>

#define SW6106_ADDR 0x3C
#define SW6106_REG_KEEPALIVE 0x03
#define SW6106_REG_LIGHTLOAD 0x38

static bool present = false;
static bool checked = false;

static bool sw_write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(SW6106_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool sw6106_present() {
  return present;
}

/* Called once from setup(), after the I2C bus is up */
void sw6106_begin() {
  checked = true;
  Wire.beginTransmission(SW6106_ADDR);
  if (Wire.endTransmission() != 0) {
    logf("SW6106: not found at 0x%02X (this board may not have one)", SW6106_ADDR);
    return;
  }
  present = true;
  bool ok = sw_write(SW6106_REG_LIGHTLOAD, 0x0A);  // light-load shutdown off
  logf("SW6106: found at 0x%02X, light-load shutdown %s", SW6106_ADDR, ok ? "disabled" : "write failed");
}

/* Called from loop(): the keep-alive, once a second */
void sw6106_service() {
  if (!checked || !present) return;
  static uint32_t next = 0;
  if (next && (int32_t)(millis() - next) < 0) return;
  next = millis() + 1000;

  if (!sw_write(SW6106_REG_KEEPALIVE, 0x01)) {
    static uint8_t fails = 0;
    if (++fails >= 5) {  // the chip has gone: stop trying and say so once
      present = false;
      logf("SW6106: stopped answering, keep-alive abandoned");
    }
  }
}
