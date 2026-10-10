# Teknisk Projektspecifikation: BLE-Nivågivare för Cabby Husvagn
**Integration med `esp32-S3-ws4-caravan`-projektet**

Detta dokument beskriver hur man läser av en analog/pneumatisk Nortronix tryckbox (nivågivare för färskvatten/gråvatten) med en separat ESP32, och strömmar denna data trådlöst via passiva **BLE Beacons** till huvudsystemet (Waveshare ESP32-S3-skärmen).

---

## 1. Systemarkitektur & Fördelar
* **Hög impedans / Ingen störning:** Sändar-ESP32:an "tjuvar" de analoga signalerna parallellt via en spänningsdelare. Originalpanelen på väggen i husvagnen påverkas inte och fortsätter fungera som vanligt.
* **Passiv BLE-Broadcasting:** Sändaren ansluter (pairar) aldrig till huvudskärmen. Den skickar bara ut mätvärdena i luften via *Manufacturer Data*.
* **RAM-vänligt för Huvudskärmen:** Huvudskärmen slipper hantera Wi-Fi eller tunga webbservrar för tankarna. Skärmens befintliga `NimBLE`-skanner plockar bara upp paketet i farten, precis som en RuuviTag.

---

## 2. Hårdvara & Kopplingsschema (Sändarsidan)
Eftersom Nortronix-boxen kan skicka ut upp till 5V–12V beroende på system, och ESP32 bara tål **max 3,3V**, dämpas signalen säkert med två motstånd per kanal.

### Komponenter:
* 1 st valfri billig ESP32 (t.ex. ESP32 DevKit V1)
* 2 st 10 kΩ Resistorer (\(R_1\) och \(R_3\))
* 2 st 4,7 kΩ Resistorer (\(R_2\) och \(R_4\))
* 1 st 12V-till-5V DC-DC Buck Converter (för strömförsörjning i vagnen)

### Kretsritning:
```text
                         GEMENSAM JORD (GND)
  Husvagnens GND ──────────────────────────────────────┬──────────────────────┐
                                                       │                      │
                         FÄRSKVATTEN (FV)              │                      │
  Boxens FV-Signal ───────[ Resistor R1: 10 kΩ ]───────┼─────── GPIO 32       │
                                                       │       (Analog In)    │
                                             [ Resistor R2: 4,7 kΩ ]          │
                                                       │                      │
                                                       ▼                      │
                                                  (GND-Skarv)                 │
                                                       ▲                      │
                                                       │                      │
                         GRÅVATTEN (GV)                │                      │
  Boxens GV-Signal ───────[ Resistor R3: 10 kΩ ]───────┼─────── GPIO 34       │
                                                       │       (Analog In)    │
                                             [ Resistor R4: 4,7 kΩ ]          │
                                                       │                      │
                                                       │                      │
                                                       ▼                      ▼
                                                  [ESP32 GND]            [Converter GND]
```

---

## 3. Sändarkod (`transmitter.ino`)
Denna kod kompileras och laddas upp till den fristående ESP32-modulen som monteras intill tryckboxen vid tankarna.

```cpp
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

#define FV_PIN 32
#define GV_PIN 34

BLEAdvertising *pAdvertising;

// Fiktivt tillverkarnummer (0xFFFF används ofta för open-source/DIY)
#define DEVICE_MANUFACTURER_ID 0xFFFF 

// Packad datastruktur för att minimera BLE-paketets storlek
struct __attribute__((packed)) TankPayload {
  uint8_t fv_percent;
  uint8_t gv_percent;
  uint16_t fv_millivolt; // Millivolt sparar precision utan att använda tunga floats
  uint16_t gv_millivolt;
};

void setup() {
  Serial.begin(115200);
  
  // Konfigurera ADC för max linjäritet upp till ~3.1V internt på ESP32
  analogSetAttenuation(ADC_11db);

  BLEDevice::init("Cabby-Tank-Sensor");
  pAdvertising = BLEDevice::getAdvertising();
}

void loop() {
  // 1. Läs analoga råvärden (0 - 4095)
  int fv_raw = analogRead(FV_PIN);
  int gv_raw = analogRead(GV_PIN);

  // 2. Beräkna spänning (skala om baserat på dämpning och din fysiska spänningsdelare)
  uint16_t fv_mv = (uint16_t)((fv_raw / 4095.0) * 3100 * 3.128);
  uint16_t gv_mv = (uint16_t)((gv_raw / 4095.0) * 3100 * 3.128);

  // 3. Kalibrering (Exempel: Justera 500mV [tom] och 4500mV [full] efter din tank)
  int fv_pct = map(fv_mv, 500, 4500, 0, 100);
  int gv_pct = map(gv_mv, 500, 4500, 0, 100);
  
  // Säkra värdena mellan 0 och 100%
  TankPayload payload;
  payload.fv_percent = constrain(fv_pct, 0, 100);
  payload.gv_percent = constrain(gv_pct, 0, 100);
  payload.fv_millivolt = fv_mv;
  payload.gv_millivolt = gv_mv;

  // 4. Strukturera upp BLE Manufacturer-data
  std::string strManufacturerData = "";
  strManufacturerData += (char)(DEVICE_MANUFACTURER_ID & 0xFF);
  strManufacturerData += (char)((DEVICE_MANUFACTURER_ID >> 8) & 0xFF);
  strManufacturerData.append((char*)&payload, sizeof(payload));

  BLEAdvertisementData pAdvertisementData;
  pAdvertisementData.setFlags(0x04); // BR_EDR_NOT_SUPPORTED (Sparar ström)
  pAdvertisementData.setManufacturerData(strManufacturerData);
  pAdvertisementData.setName("CabbyTank");

  // 5. Skicka ut i luften och vänta före nästa mätning
  pAdvertising->setAdvertisementData(pAdvertisementData);
  pAdvertising->start();
  
  Serial.printf("Sänder -> FV: %d%% (%dmV), GV: %d%% (%dmV)\n", payload.fv_percent, fv_mv, payload.gv_percent, gv_mv);
  
  delay(2000); // Sänd uppdatering varannan sekund
  pAdvertising->stop();
}
```

---

## 4. Integrationskod (Mottagarsidan / Huvudskärmen)
Dessa kodsnitt visar hur du väver in mottagningen i ditt existerande arkitekturmönster i `esp32-S3-ws4-caravan`.

### Steg A: Globala variabler (`app.h` eller `state.cpp`)
Lägg till följande rader där du deklarerar ditt delade systemstatus:

```cpp
// Lägg till i app.h eller state.cpp hos Waveshare-skärmen
int ble_fv_percent = 0;
int ble_gv_percent = 0;
float ble_fv_volt = 0.0;
float ble_gv_volt = 0.0;
unsigned long last_tank_seen = 0;
```

### Steg B: Uppdatering av BLE-Parsern (`ble.cpp`)
Leta upp din existerande `AdvertisedDeviceCallbacks`-klass (där du redan har logik som letar efter RuuviTags och Victron) och skjut in denna parser-block överst i loopen:

```cpp
void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    
    // Identifiera vår tankmodul baserat på namnet vi gav den
    if (advertisedDevice->getName() == "CabbyTank") {
        std::string strData = advertisedDevice->getManufacturerData();
        
        // Verifiera att paketet innehåller rätt antal bytes (2 byte ID + 6 byte payload)
        if (strData.length() >= 8) { 
            uint16_t manuId = (strData[1] << 8) | strData[0];
            
            if (manuId == 0xFFFF) { // Vårt unika projekt-ID
                
                // Lokal struktur som speglar sändarens data exakt
                struct TankPayload {
                  uint8_t fv_percent;
                  uint8_t gv_percent;
                  uint16_t fv_millivolt;
                  uint16_t gv_millivolt;
                } __attribute__((packed));

                // Peka ut datablocket (hoppa över de första 2 bytesen som är Manufacturer ID)
                TankPayload* payload = (TankPayload*)(strData.data() + 2);
                
                // Skriv till huvudskärmens globala variabler
                ble_fv_percent = payload->fv_percent;
                ble_gv_percent = payload->gv_percent;
                ble_fv_volt = payload->fv_millivolt / 1000.0;
                ble_gv_volt = payload->gv_millivolt / 1000.0;
                last_tank_seen = millis();
                
                // Valfritt: Skriv till ditt interna loggsystem i PSRAM (/log)
                // log_printf("BLE Tank uppdaterad: FV %d%%, GV %d%%\n", ble_fv_percent, ble_gv_percent);
            }
        }
    }
    
    // ... Här fortsätter din befintliga kod för Ruuvi och Victron ...
}
```
