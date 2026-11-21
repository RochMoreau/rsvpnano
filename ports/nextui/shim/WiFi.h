#pragma once
// Wi-Fi is owned by the host OS (NextUI). The firmware's own station/AP/scan code sees no radio.
#include <Arduino.h>
#include <IPAddress.h>

typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
typedef enum {
    WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK,
} wifi_auth_mode_t;
typedef enum {
    WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED,
    WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED,
} wl_status_t;
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

class WiFiClass {
public:
    bool mode(wifi_mode_t) { return true; }
    wifi_mode_t getMode() { return WIFI_OFF; }
    int16_t scanNetworks(bool = false, bool = false) { return WIFI_SCAN_FAILED; }
    int16_t scanComplete() { return WIFI_SCAN_FAILED; }
    void scanDelete() {}
    String SSID(uint8_t = 0) { return String(); }
    String SSID() const { return String(); }
    int32_t RSSI(uint8_t = 0) { return 0; }
    wifi_auth_mode_t encryptionType(uint8_t) { return WIFI_AUTH_OPEN; }
    wl_status_t status() { return WL_DISCONNECTED; }
    bool isConnected() { return false; }
    bool disconnect(bool = false, bool = false) { return true; }
    bool softAPdisconnect(bool = false) { return true; }
    wl_status_t begin(const char*, const char* = nullptr) { return WL_CONNECT_FAILED; }
    IPAddress localIP() { return IPAddress(); }
    IPAddress softAPIP() { return IPAddress(); }
    String macAddress() { return String("00:00:00:00:00:00"); }
    bool setHostname(const char*) { return true; }
    bool setSleep(bool) { return true; }
};
extern WiFiClass WiFi;
