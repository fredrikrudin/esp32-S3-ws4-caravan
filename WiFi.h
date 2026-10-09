#pragma once
#include <Arduino.h>
#include "IPAddress.h"
typedef enum { WL_IDLE_STATUS, WL_CONNECTED, WL_DISCONNECTED } wl_status_t;
typedef int wifi_err_reason_t;
enum { WIFI_STA = 1, WIFI_OFF = 0 };
enum { WIFI_REASON_NO_AP_FOUND = 201, WIFI_REASON_AUTH_FAIL = 202, WIFI_REASON_HANDSHAKE_TIMEOUT = 204, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15, WIFI_REASON_AUTH_EXPIRE = 2, WIFI_REASON_ASSOC_EXPIRE = 4, WIFI_REASON_BEACON_TIMEOUT = 200, WIFI_REASON_ASSOC_FAIL = 203, WIFI_REASON_CONNECTION_FAIL = 205 };
typedef int arduino_event_id_t;
enum { ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 5, ARDUINO_EVENT_WIFI_STA_GOT_IP = 7 };
struct wifi_sta_disconnected_t { uint8_t reason; };
union arduino_event_info_t { wifi_sta_disconnected_t wifi_sta_disconnected; };
class WiFiClass { public:
  wl_status_t status(); String SSID(); String SSID(int); int32_t RSSI(); int32_t RSSI(int); IPAddress localIP();
  void begin(const char *, const char *); bool disconnect(bool = false, bool = false); bool mode(int); void setAutoReconnect(bool);
  bool setSleep(bool); void persistent(bool); int scanNetworks(); void scanDelete(); int encryptionType(int);
  void onEvent(void (*)(arduino_event_id_t, arduino_event_info_t)); const char *disconnectReasonName(wifi_err_reason_t); bool setHostname(const char *); };
extern WiFiClass WiFi;
class WiFiClient : public Stream { public: int connect(const char *, uint16_t); bool connected(); void stop(); void setTimeout(uint32_t); };
