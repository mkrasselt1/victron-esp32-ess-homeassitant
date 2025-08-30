#ifndef PYLONTECH_CAN_H
#define PYLONTECH_CAN_H

#include <Arduino.h>
#include <driver/twai.h>
#include "system_data.h"

/**
 * Pylontech CAN Bus Communication Handler
 * 
 * Handles communication with Pylontech batteries via CAN bus
 * Runs in separate FreeRTOS task for non-blocking operation
 */

// LilyGO T-CAN485 Board CAN pin definitions
#ifndef CAN_TX_PIN
#define CAN_TX_PIN GPIO_NUM_27
#endif

#ifndef CAN_RX_PIN
#define CAN_RX_PIN GPIO_NUM_26
#endif
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// Pylontech CAN IDs
#define PYLONTECH_BATTERY_VOLTAGE_ID    0x359
#define PYLONTECH_BATTERY_CURRENT_ID    0x35A
#define PYLONTECH_BATTERY_SOC_ID        0x35B
#define PYLONTECH_BATTERY_TEMP_ID       0x35C
#define PYLONTECH_BATTERY_LIMITS_ID     0x35D
#define PYLONTECH_BATTERY_STATUS_ID     0x35E

class PylontechCAN {
private:
    TaskHandle_t canTaskHandle;
    bool isInitialized;
    bool isRunning;
    
    // CAN configuration
    twai_general_config_t g_config;
    twai_timing_config_t t_config;
    twai_filter_config_t f_config;
    
    // Task management
    static void canTaskWrapper(void* parameter);
    void canTask();
    
    // Message processing
    void processCanMessage(const twai_message_t& message);
    void processBatteryVoltage(const twai_message_t& message);
    void processBatteryCurrent(const twai_message_t& message);
    void processBatterySOC(const twai_message_t& message);
    void processBatteryTemperature(const twai_message_t& message);
    void processBatteryLimits(const twai_message_t& message);
    void processBatteryStatus(const twai_message_t& message);
    
    // Helper functions
    uint16_t bytesToUint16(uint8_t high, uint8_t low);
    int16_t bytesToInt16(uint8_t high, uint8_t low);
    uint32_t bytesToUint32(uint8_t byte3, uint8_t byte2, uint8_t byte1, uint8_t byte0);
    
public:
    PylontechCAN();
    ~PylontechCAN();
    
    // Initialization and control
    bool begin();
    void end();
    bool isTaskRunning() const { return isRunning; }
    
    // Statistics
    uint32_t messagesReceived = 0;
    uint32_t messagesErrors = 0;
    unsigned long lastMessageTime = 0;
    
    // Status
    bool isBatteryOnline() const;
    unsigned long getLastUpdateTime() const { return lastMessageTime; }
};

// Global instance
extern PylontechCAN pylontechCAN;

#endif // PYLONTECH_CAN_H
