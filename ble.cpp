/* BLE scanning (NimBLE-Arduino 2.x). Every advertisement is offered to the
   RuuviTag and Victron decoders (which ignore data that isn't theirs), and
   named devices are listed for the Battery tab. */
#include "app.h"
#include <NimBLEDevice.h>

/* Diagnostics for the Battery tab: log the chosen battery's advertisements
   (manufacturer data, service UUIDs, service data) at most every 10 s,
   in case its BMS broadcasts data instead of (or as well as) sending it on a connection. */
static void log_battery_adv(const NimBLEAdvertisedDevice *dev, const std::string &addr) {
  if (!bms_cfg.mac[0] || strcasecmp(addr.c_str(), bms_cfg.mac) != 0) return;
  static uint32_t last = 0;
  if (last && millis() - last < 10000) return;
  last = millis();

  USBSerial.printf("BMS adv %s '%s' rssi %d\n", addr.c_str(), dev->getName().c_str(), dev->getRSSI());
  for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
    std::string md = dev->getManufacturerData(i);
    USBSerial.print("  manufacturer data:");
    for (unsigned char c : md) USBSerial.printf(" %02X", c);
    USBSerial.println();
  }
  for (uint8_t i = 0; i < dev->getServiceUUIDCount(); i++)
    USBSerial.printf("  service uuid: %s\n", dev->getServiceUUID(i).toString().c_str());
  for (uint8_t i = 0; i < dev->getServiceDataCount(); i++) {
    std::string sd = dev->getServiceData(i);
    USBSerial.printf("  service data %s:", dev->getServiceDataUUID(i).toString().c_str());
    for (unsigned char c : sd) USBSerial.printf(" %02X", c);
    USBSerial.println();
  }
}

class SensorScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    std::string addr = dev->getAddress().toString();
    log_battery_adv(dev, addr);
    if (dev->haveName()) bms_note_adv(dev->getName().c_str(), addr.c_str(), dev->getAddress().getType(), dev->getRSSI());
    if (!dev->haveManufacturerData()) return;
    std::string md = dev->getManufacturerData();
    ruuvi_parse(md, addr.c_str(), dev->getRSSI());
    vic_handle(md, addr.c_str(), dev->getRSSI());
  }
};
static SensorScanCB scan_cb;

void ble_start() {
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scan_cb, true);  // true = report every advertisement
  scan->setActiveScan(true);               // active: also get device names (for the Battery tab)
  scan->setInterval(100);
  scan->setWindow(50);     // leave radio time for WiFi
  scan->setMaxResults(0);  // don't store results, saves RAM
  // scanning is started by ble_scan_service()
}

/* Called from loop(). Interval 1 s = scan continuously.
   Otherwise: listen for 1.5 s every N seconds (RuuviTags and Victron
   devices advertise about once per second, so 1.5 s catches each one). */
static volatile bool scan_paused = false;

void ble_pause_scan(bool pause) {
  scan_paused = pause;
  if (pause && NimBLEDevice::getScan()->isScanning()) NimBLEDevice::getScan()->stop();
}

void ble_scan_service() {
  static uint32_t next_scan = 0;
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan_paused) return;

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
