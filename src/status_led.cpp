#include "status_led.h"

// Color definitions
const RGB StatusLED::COLOR_BLUE = RGB(0, 0, 255);
const RGB StatusLED::COLOR_RED = RGB(255, 0, 0);
const RGB StatusLED::COLOR_GREEN = RGB(0, 255, 0);
const RGB StatusLED::COLOR_ORANGE = RGB(255, 165, 0);
const RGB StatusLED::COLOR_OFF = RGB(0, 0, 0);

StatusLED::StatusLED() {
    currentMode = LED_MODE_BOOT;
    currentDirection = POWER_IDLE;
    lastUpdate = 0;
    blinkInterval = 500;
    ledState = false;
    breathBrightness = 0;
    breathDirection = true;
    lastBreathUpdate = 0;
}

// WS2812 Protocol implementation for ESP32
// Timing for WS2812: T0H=0.4µs, T0L=0.85µs, T1H=0.8µs, T1L=0.45µs
void StatusLED::sendByte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) {
            // Send '1' bit: HIGH for ~0.8µs, LOW for ~0.45µs
            digitalWrite(STATUS_LED_PIN, HIGH);
            delayMicroseconds(1);  // ~0.8µs
            digitalWrite(STATUS_LED_PIN, LOW);
            // No delay needed for T1L as next bit or end handles it
        } else {
            // Send '0' bit: HIGH for ~0.4µs, LOW for ~0.85µs  
            digitalWrite(STATUS_LED_PIN, HIGH);
            // Minimal delay for T0H
            digitalWrite(STATUS_LED_PIN, LOW);
            delayMicroseconds(1);  // ~0.85µs
        }
    }
}

void StatusLED::sendRGB(const RGB& color) {
    // WS2812 expects GRB order
    // Apply brightness scaling
    uint8_t brightness = STATUS_LED_BRIGHTNESS;
    uint8_t g = (color.g * brightness) / 255;
    uint8_t r = (color.r * brightness) / 255;
    uint8_t b = (color.b * brightness) / 255;
    
    sendByte(g);
    sendByte(r);
    sendByte(b);
}

void StatusLED::show() {
    // Send RESET pulse (>50µs LOW)
    digitalWrite(STATUS_LED_PIN, LOW);
    delayMicroseconds(60);
}

void StatusLED::begin() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    setOff();
    setBootMode();
    Serial.println("[StatusLED] Initialized on GPIO 4 (native WS2812)");
}

void StatusLED::setBootMode() {
    currentMode = LED_MODE_BOOT;
    blinkInterval = 200; // Fast blink during boot
    Serial.println("[StatusLED] Boot mode - fast blue blink");
}

void StatusLED::setWiFiConnecting() {
    currentMode = LED_MODE_WIFI_CONNECTING;
    blinkInterval = 500; // Medium blink while connecting
    Serial.println("[StatusLED] WiFi connecting - medium blue blink");
}

void StatusLED::setWiFiConnected() {
    currentMode = LED_MODE_WIFI_CONNECTED;
    setSolid(COLOR_BLUE);
    Serial.println("[StatusLED] WiFi connected - solid blue");
    
    // After 2 seconds, switch to normal operation
    delay(2000);
    setNormalOperation();
}

void StatusLED::setNormalOperation() {
    currentMode = LED_MODE_NORMAL_OPERATION;
    Serial.println("[StatusLED] Normal operation mode");
}

void StatusLED::setErrorMode() {
    currentMode = LED_MODE_ERROR;
    blinkInterval = 100; // Very fast blink for errors
    Serial.println("[StatusLED] Error mode - fast red blink");
}

void StatusLED::updatePowerFlow(float batteryPower) {
    if (currentMode != LED_MODE_NORMAL_OPERATION) {
        return; // Don't update power flow if not in normal mode
    }
    
    currentDirection = determinePowerDirection(batteryPower);
    blinkInterval = calculateBlinkInterval(abs(batteryPower));
}

void StatusLED::update() {
    unsigned long currentTime = millis();
    
    switch (currentMode) {
        case LED_MODE_BOOT:
            updateBootMode();
            break;
            
        case LED_MODE_WIFI_CONNECTING:
            updateWiFiMode();
            break;
            
        case LED_MODE_WIFI_CONNECTED:
            // Solid blue - no update needed
            break;
            
        case LED_MODE_NORMAL_OPERATION:
            updateNormalMode(0); // Power will be set via updatePowerFlow
            break;
            
        case LED_MODE_ERROR:
            updateErrorMode();
            break;
    }
}

void StatusLED::updateBootMode() {
    setBlink(COLOR_BLUE, blinkInterval);
}

void StatusLED::updateWiFiMode() {
    setBlink(COLOR_BLUE, blinkInterval);
}

void StatusLED::updateNormalMode(float power) {
    switch (currentDirection) {
        case POWER_IDLE:
            updateBreathing();
            break;
            
        case POWER_CHARGING:
            setBlink(COLOR_RED, blinkInterval);
            break;
            
        case POWER_DISCHARGING:
            setBlink(COLOR_GREEN, blinkInterval);
            break;
    }
}

void StatusLED::updateErrorMode() {
    setBlink(COLOR_RED, blinkInterval);
}

void StatusLED::updateBreathing() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastBreathUpdate >= 50) { // Update every 50ms for smooth breathing
        lastBreathUpdate = currentTime;
        
        if (breathDirection) {
            breathBrightness += 5;
            if (breathBrightness >= 255) {
                breathBrightness = 255;
                breathDirection = false;
            }
        } else {
            breathBrightness -= 5;
            if (breathBrightness <= 20) { // Don't go completely dark
                breathBrightness = 20;
                breathDirection = true;
            }
        }
        
        RGB breathColor(0, 0, breathBrightness);
        sendRGB(breathColor);
        show();
    }
}

void StatusLED::setBlink(const RGB& color, unsigned long interval) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastUpdate >= interval) {
        lastUpdate = currentTime;
        ledState = !ledState;
        
        if (ledState) {
            sendRGB(color);
        } else {
            sendRGB(COLOR_OFF);
        }
        show();
    }
}

void StatusLED::setSolid(const RGB& color) {
    sendRGB(color);
    show();
}

void StatusLED::setOff() {
    sendRGB(COLOR_OFF);
    show();
}

unsigned long StatusLED::calculateBlinkInterval(float absolutePower) {
    if (absolutePower < 500) {
        return 1000; // 1Hz - slow blink
    } else if (absolutePower < 1500) {
        return 500;  // 2Hz - medium blink
    } else if (absolutePower < 3000) {
        return 250;  // 4Hz - fast blink
    } else {
        return 125;  // 8Hz - very fast blink
    }
}

PowerDirection StatusLED::determinePowerDirection(float power) {
    if (power > 100) {
        return POWER_CHARGING;
    } else if (power < -100) {
        return POWER_DISCHARGING;
    } else {
        return POWER_IDLE;
    }
}

bool StatusLED::isInitialized() const {
    return true; // Simple implementation always returns true
}

void StatusLED::setBrightness(uint8_t brightness) {
    // Note: In this implementation, brightness is applied in sendRGB()
    // This is a placeholder for API compatibility
}

void StatusLED::test() {
    Serial.println("[StatusLED] Running test sequence...");
    
    // Test red
    setSolid(COLOR_RED);
    delay(1000);
    
    // Test green  
    setSolid(COLOR_GREEN);
    delay(1000);
    
    // Test blue
    setSolid(COLOR_BLUE);
    delay(1000);
    
    // Test orange
    setSolid(COLOR_ORANGE);
    delay(1000);
    
    // Turn off
    setOff();
    
    Serial.println("[StatusLED] Test sequence completed");
}
