#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>
#include <FastLED.h>

/**
 * Status LED Controller for WS2812 LED on GPIO 2 (LilyGO T-CAN485)
 * 
 * LED Behavior:
 * - Boot: Blink blue rapidly to indicate activity
 * - WiFi Connected: Solid blue for 2 seconds
 * - Charging (positive power): Red, blink speed increases with power
 * - Discharging (negative power): Green, blink speed increases with power  
 * - No activity (power near 0): Slow blue breathing effect
 * - Error states: Fast red blink
 * 
 * Power ranges:
 * - 0-500W: Slow blink (1Hz)
 * - 500-1500W: Medium blink (2Hz)
 * - 1500-3000W: Fast blink (4Hz)
 * - >3000W: Very fast blink (8Hz)
 */

// LilyGO T-CAN485 Board Status LED pin definition
#define STATUS_LED_PIN 4
#define STATUS_LED_COUNT 1
#define STATUS_LED_BRIGHTNESS 50  // 0-255

enum StatusLedMode {
    LED_MODE_BOOT,
    LED_MODE_WIFI_CONNECTING,
    LED_MODE_WIFI_CONNECTED,
    LED_MODE_NORMAL_OPERATION,
    LED_MODE_ERROR
};

enum PowerDirection {
    POWER_IDLE,      // Power near 0 (-100W to +100W)
    POWER_CHARGING,  // Positive power (battery charging)
    POWER_DISCHARGING // Negative power (battery discharging)
};

class StatusLED {
private:
    CRGB leds[STATUS_LED_COUNT];
    StatusLedMode currentMode;
    PowerDirection currentDirection;
    
    unsigned long lastUpdate;
    unsigned long blinkInterval;
    bool ledState;
    
    // Breathing effect variables
    uint8_t breathBrightness;
    bool breathDirection;
    unsigned long lastBreathUpdate;
    
    // Color definitions
    static const CRGB COLOR_BLUE;
    static const CRGB COLOR_RED;
    static const CRGB COLOR_GREEN;
    static const CRGB COLOR_ORANGE;
    static const CRGB COLOR_OFF;
    
    // Helper methods
    void updateBootMode();
    void updateWiFiMode();
    void updateNormalMode(float power);
    void updateErrorMode();
    void updateBreathing();
    void setBlink(CRGB color, unsigned long interval);
    void setSolid(CRGB color);
    void setOff();
    unsigned long calculateBlinkInterval(float absolutePower);
    PowerDirection determinePowerDirection(float power);
    
public:
    StatusLED();
    
    // Initialization
    void begin();
    
    // Mode control
    void setBootMode();
    void setWiFiConnecting();
    void setWiFiConnected();
    void setNormalOperation();
    void setErrorMode();
    
    // Power flow indication (called regularly from main loop)
    void updatePowerFlow(float batteryPower);
    
    // Update method (call in main loop)
    void update();
    
    // Utility methods
    void setBrightness(uint8_t brightness);
    void test(); // Test all colors
    bool isInitialized() const;
    
    // Public getters for status reporting
    StatusLedMode getCurrentMode() const { return currentMode; }
    PowerDirection getCurrentDirection() const { return currentDirection; }
};

// Global instance
extern StatusLED statusLED;

#endif // STATUS_LED_H
