/*
 * VE.Bus Communication Handler
 * 
 * This class handles all VE.Bus communication with Victron Energy devices
 * in a separate thread with structured message handling.
 * 
 * SPDX-FileCopyrightText: © 2023 PV Baxi <pv-baxi@gmx.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef VEBUS_HANDLER_H
#define VEBUS_HANDLER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <HardwareSerial.h>
#include "vebus_messages.h"

// VE.Bus Communication Configuration
#define VEBUS_SERIAL_PORT 2
#define VEBUS_BAUD_RATE 256000  // Updated to MK3 protocol baudrate
#define VEBUS_RX_PIN 21         // RS485_RX = IO21
#define VEBUS_TX_PIN 22         // RS485_TX = IO22
#define VEBUS_DE_PIN 17         // RS485_EN = IO17 (Driver Enable)
#define VEBUS_SE_PIN 19         // RS485_SE = IO19 (Send Enable)
#define VEBUS_QUEUE_SIZE 10
#define VEBUS_TASK_STACK_SIZE 4096
#define VEBUS_TASK_PRIORITY 2
#define VEBUS_TASK_CORE 1

// MK3 Protocol Constants
#define VEBUS_MK3_HEADER1 0x98
#define VEBUS_MK3_HEADER2 0xF7
#define VEBUS_MK3_DATA_FRAME 0xFE
#define VEBUS_MK3_END_FRAME 0xFF
#define VEBUS_MK3_STUFF_BYTE 0xFA
#define VEBUS_BROADCAST_ADDRESS 0x00

// Command Queue Item
struct VeBusCommandItem {
    VeBusFrame frame;
    uint8_t retryCount;
    uint32_t timestamp;
    bool waitForResponse;
    
    VeBusCommandItem() {
        retryCount = 0;
        timestamp = 0;
        waitForResponse = false;
    }
};

// VE.Bus Statistics
struct VeBusStatistics {
    uint32_t framesSent = 0;
    uint32_t framesReceived = 0;
    uint32_t framesDropped = 0;
    uint32_t checksumErrors = 0;
    uint32_t timeoutErrors = 0;
    uint32_t retransmissions = 0;
    uint32_t lastResetTime = 0;
    
    void reset() {
        framesSent = 0;
        framesReceived = 0;
        framesDropped = 0;
        checksumErrors = 0;
        timeoutErrors = 0;
        retransmissions = 0;
        lastResetTime = millis();
    }
};

class VeBusHandler {
private:
    // Hardware and FreeRTOS objects
    HardwareSerial* serial;
    TaskHandle_t taskHandle;
    QueueHandle_t commandQueue;
    SemaphoreHandle_t mutex;  // Renamed from stateMutex for consistency
    
    // Communication state
    VeBusDeviceState deviceState;
    VeBusStatistics stats;  // Renamed from statistics for consistency
    uint8_t lastCommandId;
    bool isRunning;
    bool debugMode;
    
    // Frame buffers
    uint8_t rxBuffer[VEBUS_FRAME_SIZE];
    uint8_t rxBufferPos;
    uint32_t lastRxTime;
    
    // Pending command tracking
    VeBusCommandItem pendingCommand;
    bool waitingForResponse;
    uint32_t responseTimeout;
    
    // Private methods
    static void taskWrapper(void* parameter);
    void communicationTask();
    bool receiveFrame(VeBusFrame& frame);
    bool parseMk2Frame(VeBusFrame& frame);
    bool parseMk3Frame(VeBusFrame& frame);
    bool sendFrame(const VeBusFrame& frame);
    bool sendFrameMk3Correct(const VeBusFrame& frame);
    int commandReplaceFAtoFF(uint8_t *outbuf, const uint8_t *inbuf, int inlength);
    int appendChecksum(uint8_t *buf, int inlength);
    void processReceivedFrame(const VeBusFrame& frame);
    void handleTimeout();
    void updateStatistics();
    bool isFrameComplete();
    void resetRxBuffer();
    
public:
    VeBusHandler();
    ~VeBusHandler();
    
    // Initialization and control
    bool begin(int rxPin = VEBUS_RX_PIN, int txPin = VEBUS_TX_PIN, 
               long baudRate = VEBUS_BAUD_RATE);
    void end();
    bool isInitialized() const { return serial != nullptr; }
    bool isTaskRunning() const { return isRunning; }
    
    // Device state access (thread-safe)
    VeBusDeviceState getDeviceState();
    VeBusStatistics getStatistics();
    void resetStatistics();
    
    // Command interface
    bool sendEssPowerCommand(int16_t targetPower);
    bool sendCurrentLimitCommand(uint8_t currentLimit);
    bool sendSwitchCommand(uint8_t switchState);
    bool sendCustomCommand(const VeBusFrame& frame, bool waitForResponse = false);
    
    // Status and diagnostics
    bool isDeviceOnline() const;
    uint32_t getLastCommunicationTime() const;
    float getCommunicationQuality() const; // Returns 0.0-1.0
    void enableDebugMode(bool enable) { debugMode = enable; }
    
    // New MK2 Protocol API Functions for External Control
    bool requestVersionInfo(VeBusVersionInfo& info);
    bool requestDeviceStatus(VeBusDeviceStatusInfo& status);
    bool requestErrorInfo(VeBusErrorInfo& error);
    bool requestWarningInfo(VeBusWarningInfo& warning);
    bool requestLedStatus(VeBusLedStatus& ledStatus);
    bool setSwitchState(VeBusSwitchState state);
    bool resetDevice();
    bool clearErrors();
    
    // Enhanced configuration methods
    bool enableAutoRestart(bool enable);
    bool setVoltageRange(float minVoltage, float maxVoltage);
    bool setFrequencyRange(float minFreq, float maxFreq);
    
    // Compatibility methods for existing code
    float getDcVoltage();
    float getDcCurrent();
    float getAcVoltage();
    float getAcFrequency();
    int16_t getAcPower();
    uint8_t getDeviceStatus();
    bool getLedStatus();
    float getInputCurrentLimit();
    
    // Legacy compatibility - map old variables to new structure
    void updateLegacyVariables();
    
    // Compatibility functions for legacy code
    void sendMessageToRemote(byte* message, int length) { sendCustomCommand(*((VeBusFrame*)message), false); }
    void sendEssPowerCommand(int power) { sendEssPowerCommand((int16_t)power); }
    void sendCurrentLimitCommand(int limit) { sendCurrentLimitCommand((uint8_t)limit); }
};

// Global instance declaration
extern VeBusHandler veBusHandler;

#endif // VEBUS_HANDLER_H
