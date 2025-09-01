#include "wifi_provisioning.h"

const char* WiFiProvisioning::IMPROV_SERVICE_TYPE = "_improv._tcp";
const char* WiFiProvisioning::IMPROV_SERVICE_NAME = "Victron ESS";
const char* WiFiProvisioning::IMPROV_DEVICE_NAME = "Victron ESP32 ESS Controller";
const char* WiFiProvisioning::IMPROV_DEVICE_VERSION = "1.0.0";
const char* WiFiProvisioning::IMPROV_FIRMWARE_NAME = "victron-esp32-ess";
const char* WiFiProvisioning::IMPROV_FIRMWARE_VERSION = "1.0.0";

WiFiProvisioning wifiProvisioning;

WiFiProvisioning::WiFiProvisioning() {
    // Constructor
}

WiFiProvisioning::~WiFiProvisioning() {
    // Destructor
}

bool WiFiProvisioning::begin() {
    preferences.begin("wifi", false);
    
    // Try to load saved credentials
    String ssid, password;
    if (loadWiFiCredentials(ssid, password)) {
        Serial.printf("Connecting to saved WiFi: %s\n", ssid.c_str());
        if (connectToWiFi(ssid.c_str(), password.c_str())) {
            Serial.println("WiFi connected successfully!");
            currentState = STATE_PROVISIONED;
            return true;
        }
        Serial.println("Failed to connect to saved WiFi");
    }
    
    // Enter provisioning mode
    Serial.println("Starting Improv WiFi Serial Provisioning...");
    enableProvisioningMode();
    return false;
}

void WiFiProvisioning::loop() {
    handleImprovSerial();
    
    if (currentState == STATE_PROVISIONING) {
        // Check for timeout on provisioning attempts
        if (millis() - lastProvisioningAttempt > 30000) { // 30 second timeout
            currentState = STATE_AUTHORIZED;
            sendImprovError(ERROR_UNABLE_TO_CONNECT);
        }
    }
    
    // Check connection status in normal mode
    if (currentState == STATE_PROVISIONED && WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected - entering provisioning mode");
        enableProvisioningMode();
    }
}

bool WiFiProvisioning::isConnected() {
    return WiFi.status() == WL_CONNECTED && currentState == STATE_PROVISIONED;
}

void WiFiProvisioning::resetWiFiCredentials() {
    preferences.clear();
    Serial.println("WiFi credentials cleared - restarting");
    ESP.restart();
}

void WiFiProvisioning::enableProvisioningMode() {
    currentState = STATE_AUTHORIZED; // Skip authorization for simplicity
    isProvisioningMode = true;
    
    // Send identification message
    Serial.println("\n=== Improv WiFi Serial Ready ===");
    Serial.println("Device: " + String(IMPROV_DEVICE_NAME));
    Serial.println("Version: " + String(IMPROV_DEVICE_VERSION));
    Serial.println("Visit: https://www.improv-wifi.com/");
    Serial.println("Or use any Improv-compatible tool");
    Serial.println("================================\n");
    
    sendCurrentState();
    sendDeviceInfo();
}

void WiFiProvisioning::disableProvisioningMode() {
    isProvisioningMode = false;
    currentState = STATE_PROVISIONED;
}

// Improv Serial Protocol Implementation
void WiFiProvisioning::handleImprovSerial() {
    static uint8_t buffer[256];
    static size_t bufferPos = 0;
    
    while (Serial.available()) {
        uint8_t byte = Serial.read();
        
        if (bufferPos == 0 && byte != 'I') {
            // Look for legacy commands for backward compatibility
            if (byte >= 32 && byte <= 126) { // Printable ASCII
                String command = String((char)byte);
                command += Serial.readStringUntil('\n');
                command.trim();
                
                if (command.startsWith("wifi_set ")) {
                    int firstSpace = command.indexOf(' ', 9);
                    int secondSpace = command.indexOf(' ', firstSpace + 1);
                    if (secondSpace > 0) {
                        String ssid = command.substring(9, secondSpace);
                        String password = command.substring(secondSpace + 1);
                        setWiFiCredentials(ssid.c_str(), password.c_str());
                    } else {
                        Serial.println("Usage: wifi_set <ssid> <password>");
                    }
                }
                else if (command == "wifi_status") {
                    printStatus();
                }
                else if (command == "wifi_reset") {
                    resetWiFiCredentials();
                }
                else if (command == "help" || command == "?") {
                    printCommands();
                }
            }
            continue;
        }
        
        buffer[bufferPos++] = byte;
        
        // Check for complete packet
        if (bufferPos >= 9) { // Minimum packet size
            // Verify header: "IMPROV"
            if (memcmp(buffer, "IMPROV", 6) == 0) {
                uint8_t version = buffer[6];
                uint8_t command = buffer[7];
                uint8_t length = buffer[8];
                
                if (bufferPos >= 9 + length + 1) { // Header + data + checksum
                    if (parseImprovPacket(buffer, bufferPos)) {
                        // Packet processed successfully
                    }
                    bufferPos = 0; // Reset buffer
                }
            } else {
                // Invalid packet, reset buffer
                bufferPos = 0;
            }
        }
        
        // Prevent buffer overflow
        if (bufferPos >= sizeof(buffer)) {
            bufferPos = 0;
        }
    }
}

bool WiFiProvisioning::parseImprovPacket(uint8_t* buffer, size_t length) {
    if (length < 9) return false;
    
    uint8_t version = buffer[6];
    uint8_t command = buffer[7];
    uint8_t dataLength = buffer[8];
    
    if (length < 9 + dataLength + 1) return false;
    
    uint8_t* data = &buffer[9];
    uint8_t receivedChecksum = buffer[9 + dataLength];
    uint8_t calculatedChecksum = calculateChecksum(buffer, 9 + dataLength);
    
    if (receivedChecksum != calculatedChecksum) {
        sendImprovError(ERROR_INVALID_RPC);
        return false;
    }
    
    switch (command) {
        case COMMAND_GET_CURRENT_STATE:
            sendCurrentState();
            break;
            
        case COMMAND_GET_DEVICE_INFO:
            sendDeviceInfo();
            break;
            
        case COMMAND_IDENTIFY:
            sendIdentifyResponse();
            break;
            
        case COMMAND_WIFI_SETTINGS:
            handleWiFiSettings(data, dataLength);
            break;
            
        default:
            sendImprovError(ERROR_UNKNOWN_COMMAND);
            return false;
    }
    
    return true;
}

void WiFiProvisioning::handleWiFiSettings(const uint8_t* data, size_t length) {
    if (currentState != STATE_AUTHORIZED) {
        sendImprovError(ERROR_NOT_AUTHORIZED);
        return;
    }
    
    // Parse WiFi settings: ssid_length + ssid + password_length + password
    if (length < 2) {
        sendImprovError(ERROR_INVALID_RPC);
        return;
    }
    
    uint8_t ssidLength = data[0];
    if (length < 1 + ssidLength + 1) {
        sendImprovError(ERROR_INVALID_RPC);
        return;
    }
    
    String ssid = "";
    for (int i = 0; i < ssidLength; i++) {
        ssid += (char)data[1 + i];
    }
    
    uint8_t passwordLength = data[1 + ssidLength];
    if (length < 1 + ssidLength + 1 + passwordLength) {
        sendImprovError(ERROR_INVALID_RPC);
        return;
    }
    
    String password = "";
    for (int i = 0; i < passwordLength; i++) {
        password += (char)data[1 + ssidLength + 1 + i];
    }
    
    currentState = STATE_PROVISIONING;
    lastProvisioningAttempt = millis();
    sendCurrentState();
    
    Serial.printf("Attempting to connect to: %s\n", ssid.c_str());
    
    if (connectToWiFi(ssid.c_str(), password.c_str())) {
        saveWiFiCredentials(ssid.c_str(), password.c_str());
        currentState = STATE_PROVISIONED;
        disableProvisioningMode();
        
        // Send success response with IP address
        String url = "http://" + WiFi.localIP().toString();
        uint8_t urlData[url.length() + 1];
        urlData[0] = url.length();
        memcpy(&urlData[1], url.c_str(), url.length());
        sendImprovResponse(COMMAND_WIFI_SETTINGS, urlData, url.length() + 1);
        
        Serial.println("WiFi provisioning successful!");
        Serial.println("URL: " + url);
    } else {
        currentState = STATE_AUTHORIZED;
        sendImprovError(ERROR_UNABLE_TO_CONNECT);
        Serial.println("WiFi connection failed");
    }
}

void WiFiProvisioning::sendCurrentState() {
    uint8_t state = (uint8_t)currentState;
    sendImprovResponse(COMMAND_GET_CURRENT_STATE, &state, 1);
}

void WiFiProvisioning::sendDeviceInfo() {
    String info = String(IMPROV_FIRMWARE_NAME) + "\x00" + 
                  String(IMPROV_FIRMWARE_VERSION) + "\x00" + 
                  String(IMPROV_DEVICE_NAME) + "\x00" + 
                  String("Victron Energy");
    
    sendImprovResponse(COMMAND_GET_DEVICE_INFO, (uint8_t*)info.c_str(), info.length());
}

void WiFiProvisioning::sendIdentifyResponse() {
    // Blink LED or give other feedback
    Serial.println("Device identified!");
    sendImprovResponse(COMMAND_IDENTIFY, nullptr, 0);
}

void WiFiProvisioning::sendImprovResponse(uint8_t command, const uint8_t* data, size_t dataLen) {
    sendImprovPacket(command, data, dataLen);
}

void WiFiProvisioning::sendImprovError(ImprovError error) {
    uint8_t errorCode = (uint8_t)error;
    sendImprovPacket(0xFF, &errorCode, 1); // Error packet
}

void WiFiProvisioning::sendImprovPacket(uint8_t command, const uint8_t* data, size_t dataLen) {
    uint8_t packet[256];
    size_t pos = 0;
    
    // Header
    memcpy(&packet[pos], "IMPROV", 6);
    pos += 6;
    
    // Version
    packet[pos++] = IMPROV_SERIAL_VERSION;
    
    // Command
    packet[pos++] = command;
    
    // Data length
    packet[pos++] = dataLen;
    
    // Data
    if (data && dataLen > 0) {
        memcpy(&packet[pos], data, dataLen);
        pos += dataLen;
    }
    
    // Checksum
    packet[pos] = calculateChecksum(packet, pos);
    pos++;
    
    // Send packet
    Serial.write(packet, pos);
    Serial.flush();
}

uint8_t WiFiProvisioning::calculateChecksum(const uint8_t* data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += data[i];
    }
    return checksum;
}

bool WiFiProvisioning::connectToWiFi(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    return WiFi.status() == WL_CONNECTED;
}

void WiFiProvisioning::saveWiFiCredentials(const char* ssid, const char* password) {
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    Serial.println("WiFi credentials saved to NVS");
}

bool WiFiProvisioning::loadWiFiCredentials(String& ssid, String& password) {
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    return ssid.length() > 0;
}

// Legacy methods for backward compatibility
void WiFiProvisioning::setWiFiCredentials(const char* ssid, const char* password) {
    Serial.printf("Setting WiFi credentials: %s\n", ssid);
    
    if (connectToWiFi(ssid, password)) {
        saveWiFiCredentials(ssid, password);
        currentState = STATE_PROVISIONED;
        disableProvisioningMode();
        Serial.println("WiFi credentials saved and connected!");
    } else {
        Serial.println("Failed to connect with provided credentials");
    }
}

void WiFiProvisioning::printStatus() {
    Serial.println("\n=== WiFi Status ===");
    Serial.printf("Mode: %s\n", isProvisioningMode ? "Provisioning" : "Connected");
    Serial.printf("State: %d\n", currentState);
    if (isConnected()) {
        Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
    }
    Serial.println("==================\n");
}

void WiFiProvisioning::printCommands() {
    Serial.println("\n=== WiFi Commands ===");
    Serial.println("Improv Serial: Use https://www.improv-wifi.com/");
    Serial.println("Legacy commands:");
    Serial.println("wifi_set <ssid> <password> - Set WiFi credentials");
    Serial.println("wifi_status                - Show WiFi status");
    Serial.println("wifi_reset                 - Reset WiFi credentials");
    Serial.println("=====================\n");
}
