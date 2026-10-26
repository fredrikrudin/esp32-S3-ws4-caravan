// esp32-S3-ws4-caravan v1.0
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

  dlogf("BMS adv %s '%s' rssi %d", addr.c_str(), dev->getName().c_str(), dev->getRSSI());
  for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
    std::string md = dev->getManufacturerData(i);
    serf("  manufacturer data:");
    for (unsigned char c : md) serf(" %02X", c);
    serln();
  }
  for (uint8_t i = 0; i < dev->getServiceUUIDCount(); i++)
    serf("  service uuid: %s\n", dev->getServiceUUID(i).toString().c_str());
  for (uint8_t i = 0; i < dev->getServiceDataCount(); i++) {
    std::string sd = dev->getServiceData(i);
    serf("  service data %s:", dev->getServiceDataUUID(i).toString().c_str());
    for (unsigned char c : sd) serf(" %02X", c);
    serln();
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
  /* pairings (Shelly) are kept in flash: this shows whether they survived a restart */
  logf("Bluetooth: started, %d pairing(s) stored", NimBLEDevice::getNumBonds());
  for (int i = 0; i < NimBLEDevice::getNumBonds(); i++)
    logf("Bluetooth: paired with %s", NimBLEDevice::getBondedAddress(i).toString().c_str());
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scan_cb, true);  // true = report every advertisement
  scan->setActiveScan(false);              // active only while Settings is open (ble_scan_service)
  scan->setInterval(100);
  scan->setWindow(50);     // leave radio time for WiFi
  scan->setMaxResults(0);  // don't store results, saves RAM
  // scanning is started by ble_scan_service()
}

/* Called from loop(): listen for 3 s every scan_interval_s seconds (10 s to
   6 minutes, Settings -> Sensors). RuuviTags and Victron devices advertise about
   once a second, so 3 s catches each of them. While Settings is open it is every
   10 s, so devices to add show up quickly. Between scans the radio is WiFi's
   alone, and each scan starts with an empty list: a scan that never ended kept
   every device still owing a scan response, in internal RAM, and phones change
   their address all the time (internal RAM went from 32 kB to under 7 kB in an
   hour, and the web page stopped). */
static volatile bool scan_paused = false;

/* May be called from the network task, possibly before ble_start() has run:
   the flag alone then keeps scanning from starting until WiFi is connected. */
void ble_pause_scan(bool pause) {
  scan_paused = pause;
  if (pause && NimBLEDevice::isInitialized() && NimBLEDevice::getScan()->isScanning()) NimBLEDevice::getScan()->stop();
}

void ble_scan_now() {
  scan_restart = true;
}

/* A reading is "lost" after base_ms, or after three missed scans if that is longer */
static uint32_t scan_every_s() {
  if (tab_visible(tab_settings)) return SCAN_MIN_S;  // devices to add show up quickly
  if (power_on_battery()) return BATT_SCAN_S > scan_interval_s ? BATT_SCAN_S : scan_interval_s;
  return scan_interval_s;
}

uint32_t sensor_stale_ms(uint32_t base_ms) {
  uint32_t three = 3UL * scan_every_s() * 1000UL + 5000UL;
  return three > base_ms ? three : base_ms;
}

void ble_scan_service() {
  static uint32_t next_scan = 0;
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan_paused) return;

  if (scan_restart) {  // the interval changed, or Scan now
    scan_restart = false;
    if (scan->isScanning()) scan->stop();
    next_scan = millis();
  }
  uint32_t every = scan_every_s();
  if ((int32_t)(next_scan - millis()) > (int32_t)(every * 1000UL)) next_scan = millis() + every * 1000UL;  // Settings just opened
  /* how much internal RAM a scan leaves behind (power.cpp, "Memory:" line) */
  static bool measuring = false;
  static uint32_t free_before = 0;
  if (measuring && !scan->isScanning()) {
    measuring = false;
    mem_note(MEM_BLE_SCAN, (int32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) - (int32_t)free_before);
  }
  if (scan->isScanning() || (int32_t)(millis() - next_scan) < 0) return;

  /* While the web server is sending a page, the scan waits (up to 20 s): WiFi
     gets the radio to itself and no scan memory is in use during the transfer */
  static uint32_t waiting_since = 0;
  if (web_busy()) {
    if (!waiting_since) waiting_since = millis() | 1;
    if (millis() - waiting_since < 20000) return;
  }
  waiting_since = 0;

  free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  measuring = true;
  /* Passive, except while Settings is open. An active scan asks every device for
     its scan response (that is where names are), and NimBLE keeps each device in
     internal RAM until the answer comes or the scan ends. In a building full of
     phones, TVs and headphones that is many kB for the length of each scan, and
     free internal RAM fell to 1 kB. RuuviTags and Victron devices send their data
     in the advertisement itself, so a passive scan reads them just the same, and
     each device is let go as soon as it has been read. Names are only needed to
     pick devices in Settings. */
  scan->setActiveScan(tab_visible(tab_settings));
  /* On the LiPo WiFi is off, so a scan can have the radio to itself (window =
     interval) and runs a little longer, to be sure of catching every device in the
     one scan per 5 minutes; the radio sleeps in between. */
  bool batt = power_on_battery();
  scan->setWindow(batt ? 100 : 50);
  scan->start(batt ? 5000 : 3000, false, true);  // not "continue": starts with an empty list
  next_scan = millis() + every * 1000UL;
}
