/* BLE passive scanning (NimBLE-Arduino 2.x). Every advertisement is offered to
   the RuuviTag and Victron decoders, which ignore data that isn't theirs. */
#include "app.h"
#include <NimBLEDevice.h>

class SensorScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (!dev->haveManufacturerData()) return;
    std::string md = dev->getManufacturerData();
    std::string addr = dev->getAddress().toString();
    ruuvi_parse(md, addr.c_str(), dev->getRSSI());
    vic_handle(md, addr.c_str(), dev->getRSSI());
  }
};
static SensorScanCB scan_cb;

void ble_start() {
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scan_cb, true);  // true = report every advertisement
  scan->setActiveScan(false);              // sensor data is in the normal advertisement
  scan->setInterval(100);
  scan->setWindow(50);     // leave radio time for WiFi
  scan->setMaxResults(0);  // don't store results, saves RAM
  // scanning is started by ble_scan_service()
}

/* Called from loop(). Interval 1 s = scan continuously.
   Otherwise: listen for 1.5 s every N seconds (RuuviTags and Victron
   devices advertise about once per second, so 1.5 s catches each one). */
void ble_scan_service() {
  static uint32_t next_scan = 0;
  NimBLEScan *scan = NimBLEDevice::getScan();

  if (scan_restart) {
    scan_restart = false;
    if (scan->isScanning()) scan->stop();
    next_scan = millis();
  }
  if (scan->isScanning() || (int32_t)(millis() - next_scan) < 0) return;

  uint8_t n = scan_interval_s;
  if (n <= 1) {
    scan->start(0, false, true);  // forever
  } else {
    scan->start(1500, false, true);
    next_scan = millis() + n * 1000UL;
  }
}
