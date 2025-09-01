#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

/**
 * WiFi Provisioning for ESP32 - Improv Serial Protocol
 * 
 * Features:
 * - Improv WiFi Serial Protocol support
 * - Browser-based configuration (Web Serial API)
 * - Stores WiFi credentials in NVS
 * - Minimal memory footprint
 * - Compatible with https://www.improv-wifi.com/
 */

class WiFiProvisioning {
private:
    Preferences preferences;
    bool isProvisioningMode = false;
    
    // Improv Serial Protocol constants
    static const uint8_t IMPROV_SERIAL_VERSION = 1;
    static const char* IMPROV_SERVICE_TYPE;
    static const char* IMPROV_SERVICE_NAME;
    static const char* IMPROV_DEVICE_NAME;
    static const char* IMPROV_DEVICE_VERSION;
    static const char* IMPROV_FIRMWARE_NAME;
    static const char* IMPROV_FIRMWARE_VERSION;
    
    // Command types
    enum ImprovCommand {
        COMMAND_WIFI_SETTINGS = 0x01,
        COMMAND_GET_CURRENT_STATE = 0x02,
        COMMAND_GET_DEVICE_INFO = 0x03,
        COMMAND_GET_WIFI_SETTINGS = 0x04,
        COMMAND_IDENTIFY = 0x05
    };
    
    // States
    enum ImprovState {
        STATE_STOPPED = 0x00,
        STATE_AWAITING_AUTHORIZATION = 0x01,
        STATE_AUTHORIZED = 0x02,
        STATE_PROVISIONING = 0x03,
        STATE_PROVISIONED = 0x04
    };
    
    // Error codes
    enum ImprovError {
        ERROR_NONE = 0x00,
        ERROR_INVALID_RPC = 0x01,
        ERROR_UNKNOWN_COMMAND = 0x02,
        ERROR_UNABLE_TO_CONNECT = 0x03,
        ERROR_NOT_AUTHORIZED = 0x04,
        ERROR_UNKNOWN = 0xFF
    };
    
    ImprovState currentState = STATE_STOPPED;
    unsigned long lastProvisioningAttempt = 0;
    
    // Internal methods
    void handleImprovSerial();
    bool parseImprovPacket(uint8_t* buffer, size_t length);
    void sendImprovResponse(uint8_t command, const uint8_t* data = nullptr, size_t dataLen = 0);
    void sendImprovError(ImprovError error);
    void sendCurrentState();
    void sendDeviceInfo();
    void sendIdentifyResponse();
    void handleWiFiSettings(const uint8_t* data, size_t length);
    bool connectToWiFi(const char* ssid, const char* password);
    void saveWiFiCredentials(const char* ssid, const char* password);
    bool loadWiFiCredentials(String& ssid, String& password);
    uint8_t calculateChecksum(const uint8_t* data, size_t length);
    void sendImprovPacket(uint8_t command, const uint8_t* data, size_t dataLen);
    
public:
    WiFiProvisioning();
    ~WiFiProvisioning();
    
    // Main methods
    bool begin();
    void loop();
    bool isConnected();
    void resetWiFiCredentials();
    
    // Improv protocol methods
    void enableProvisioningMode();
    void disableProvisioningMode();
    bool isInProvisioningMode() const { return isProvisioningMode; }
    
    // Legacy serial commands (for debugging)
    void setWiFiCredentials(const char* ssid, const char* password);
    void printStatus();
    void printCommands();
};

extern WiFiProvisioning wifiProvisioning;

#endif
