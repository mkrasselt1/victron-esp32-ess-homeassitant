#include "status_led.h"

// Color definitions
const CRGB StatusLED::COLOR_BLUE = CRGB(0, 0, 255);
const CRGB StatusLED::COLOR_RED = CRGB(255, 0, 0);
const CRGB StatusLED::COLOR_GREEN = CRGB(0, 255, 0);
const CRGB StatusLED::COLOR_ORANGE = CRGB(255, 165, 0);
const CRGB StatusLED::COLOR_OFF = CRGB(0, 0, 0);

// Global instance
// StatusLED statusLED; // Removed - defined in main.cpp

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

void StatusLED::begin() {
    FastLED.addLeds<WS2812, STATUS_LED_PIN, GRB>(leds, STATUS_LED_COUNT);
    FastLED.setBrightness(STATUS_LED_BRIGHTNESS);
    FastLED.clear();
    FastLED.show();
    
    setBootMode();
    Serial.println("[StatusLED] Initialized on GPIO 4");
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
    
    FastLED.show();
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
    
    if (currentTime - lastBreathUpdate >= 20) { // Update every 20ms for smooth breathing
        lastBreathUpdate = currentTime;
        
        if (breathDirection) {
            breathBrightness += 2;
            if (breathBrightness >= 100) {
                breathDirection = false;
            }
        } else {
            breathBrightness -= 2;
            if (breathBrightness <= 10) {
                breathDirection = true;
            }
        }
        
        // Set blue color with breathing brightness
        leds[0] = CRGB(0, 0, breathBrightness);
    }
}

void StatusLED::setBlink(CRGB color, unsigned long interval) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastUpdate >= interval) {
        lastUpdate = currentTime;
        ledState = !ledState;
        
        if (ledState) {
            leds[0] = color;
        } else {
            leds[0] = COLOR_OFF;
        }
    }
}

void StatusLED::setSolid(CRGB color) {
    leds[0] = color;
}

void StatusLED::setOff() {
    leds[0] = COLOR_OFF;
}

unsigned long StatusLED::calculateBlinkInterval(float absolutePower) {
    if (absolutePower < 100) {
        return 0; // No blinking for very low power (breathing mode)
    } else if (absolutePower < 500) {
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
        return POWER_CHARGING;   // Positive power = charging
    } else if (power < -100) {
        return POWER_DISCHARGING; // Negative power = discharging
    } else {
        return POWER_IDLE;       // Power near zero = idle
    }
}

void StatusLED::setBrightness(uint8_t brightness) {
    FastLED.setBrightness(brightness);
}

void StatusLED::test() {
    Serial.println("[StatusLED] Testing all colors...");
    
    // Test Red
    setSolid(COLOR_RED);
    FastLED.show();
    delay(1000);
    
    // Test Green
    setSolid(COLOR_GREEN);
    FastLED.show();
    delay(1000);
    
    // Test Blue
    setSolid(COLOR_BLUE);
    FastLED.show();
    delay(1000);
    
    // Test Orange
    setSolid(COLOR_ORANGE);
    FastLED.show();
    delay(1000);
    
    // Off
    setOff();
    FastLED.show();
    delay(500);
    
    Serial.println("[StatusLED] Test completed");
}

bool StatusLED::isInitialized() const {
    return true; // FastLED doesn't provide an explicit initialization check
}
