/*
 * VE.Bus Message Structures
 * 
 * This header defines all VE.Bus message structures for communication
 * with Victron Energy devices.
 * 
 * SPDX-FileCopyrightText: © 2023 PV Baxi <pv-baxi@gmx.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef VEBUS_MESSAGES_H
#define VEBUS_MESSAGES_H

#include <Arduino.h>
#include <stdint.h>

// VE.Bus Constants
#define VEBUS_FRAME_SIZE 128  // Increased for MK3 protocol with stuffing
#define VEBUS_SYNC_BYTE 0xFF
#define VEBUS_MAX_RETRY_COUNT 3
#define VEBUS_TIMEOUT_MS 1000

// MK3 Protocol Frame Structure
#define VEBUS_MK3_HEADER_SIZE 4
#define VEBUS_MK3_MAX_DATA_SIZE 120

// VE.Bus Frame Types
enum VeBusFrameType {
    VEBUS_FRAME_SYNC = 0x00,
    VEBUS_FRAME_VERSION = 0x01,
    VEBUS_FRAME_DC_INFO = 0x02,
    VEBUS_FRAME_AC_INFO = 0x03,
    VEBUS_FRAME_LED_STATUS = 0x04,
    VEBUS_FRAME_COMMAND = 0x05,
    VEBUS_FRAME_RESPONSE = 0x06
};

// VE.Bus Command Types (MK2 Protocol)
enum VeBusCommand {
    VEBUS_CMD_GET_VERSION = 0x01,
    VEBUS_CMD_GET_DC_INFO = 0x02,
    VEBUS_CMD_GET_AC_INFO = 0x03,
    VEBUS_CMD_GET_LED_STATUS = 0x04,
    VEBUS_CMD_SET_SWITCH = 0x05,
    VEBUS_CMD_GET_DEVICE_STATUS = 0x06,
    VEBUS_CMD_SET_ESS_POWER = 0x37,
    VEBUS_CMD_SET_CHARGE_CURRENT = 0x40,
    VEBUS_CMD_SET_INPUT_CURRENT = 0x41,
    VEBUS_CMD_GET_STATUS = 0x42,
    VEBUS_CMD_GET_ERROR_INFO = 0x50,
    VEBUS_CMD_GET_WARNING_INFO = 0x51,
    VEBUS_CMD_DEVICE_RESET = 0x52,
    VEBUS_CMD_CLEAR_ERRORS = 0x53,
    VEBUS_CMD_SET_AUTO_RESTART = 0x54,
    VEBUS_CMD_SET_VOLTAGE_RANGE = 0x55,
    VEBUS_CMD_SET_FREQUENCY_RANGE = 0x56
};

// VE.Bus Device Status
enum VeBusDeviceStatus {
    VEBUS_STATUS_OFF = 0,
    VEBUS_STATUS_LOW_POWER = 1,
    VEBUS_STATUS_FAULT = 2,
    VEBUS_STATUS_BULK = 3,
    VEBUS_STATUS_ABSORPTION = 4,
    VEBUS_STATUS_FLOAT = 5,
    VEBUS_STATUS_STORAGE = 6,
    VEBUS_STATUS_EQUALIZE = 7,
    VEBUS_STATUS_PASSTHRU = 8,
    VEBUS_STATUS_INVERTING = 9,
    VEBUS_STATUS_POWER_ASSIST = 10,
    VEBUS_STATUS_POWER_SUPPLY = 11
};

// VE.Bus Switch States (MK2 Protocol)
enum VeBusSwitchState {
    VEBUS_SWITCH_CHARGER_ONLY = 1,
    VEBUS_SWITCH_INVERTER_ONLY = 2,
    VEBUS_SWITCH_ON = 3,
    VEBUS_SWITCH_OFF = 4
};

// MK2 Protocol Version Information Structure
struct VeBusVersionInfo {
    uint8_t productId;
    uint8_t firmwareVersion;
    uint8_t protocolVersion;
};

// MK2 Protocol Device Status Structure
struct VeBusDeviceStatusInfo {
    uint8_t state;          // Current device state
    uint8_t mode;           // Operating mode
    uint8_t alarm;          // Alarm status
    uint8_t warnings;       // Warning flags
};

// MK2 Protocol Error Information Structure
struct VeBusErrorInfo {
    uint8_t errorCode;
    uint8_t errorSubCode;
    uint32_t errorCounter;
    uint32_t timestamp;
};

// MK2 Protocol Warning Information Structure
struct VeBusWarningInfo {
    uint16_t warningFlags;
    uint8_t batteryVoltageWarning;
    uint8_t temperatureWarning;
    uint8_t overloadWarning;
    uint8_t dcRippleWarning;
};

// Basic VE.Bus Frame Structure (MK2/MK3 compatible)
struct VeBusFrame {
    uint8_t sync;           // MK2: 0xFF, MK3: 0x98
    uint8_t address;        // Device address
    uint8_t command;        // Command/Frame type
    uint8_t length;         // Data length
    uint8_t data[VEBUS_MK3_MAX_DATA_SIZE];  // Data payload (increased for MK3)
    uint8_t checksum;       // Frame checksum
    
    // MK3 specific fields
    uint8_t frameNumber;    // Frame sequence number for MK3
    bool isMk3Frame;        // Flag to indicate MK3 protocol
    
    VeBusFrame() {
        sync = VEBUS_SYNC_BYTE;
        address = 0;
        command = 0;
        length = 0;
        memset(data, 0, sizeof(data));
        checksum = 0;
        frameNumber = 0;
        isMk3Frame = false;
    }
    
    // MK2 Checksum calculation
    void calculateChecksumMk2() {
        checksum = 0x55 - sync - address - command - length;
        for (int i = 0; i < length && i < VEBUS_MK3_MAX_DATA_SIZE; i++) {
            checksum -= data[i];
        }
    }
    
    // MK3 Checksum calculation (from reference implementation)
    void calculateChecksumMk3() {
        checksum = 1;  // Start with 1
        for (int i = 2; i < length + 4; i++) {  // Skip first 2 bytes (address)
            checksum -= data[i - 4];
        }
        if (checksum >= 0xFB) {  // Exception: replace from 0xFB
            // This would require frame stuffing, simplified for now
            checksum = (checksum - 0xFA) | 0x70;
        }
    }
    
    void calculateChecksum() {
        if (isMk3Frame) {
            calculateChecksumMk3();
        } else {
            calculateChecksumMk2();
        }
    }
    
    // MK2 Checksum validation
    bool isChecksumValidMk2() const {
        uint8_t calc = 0x55 - sync - address - command - length;
        for (int i = 0; i < length && i < VEBUS_MK3_MAX_DATA_SIZE; i++) {
            calc -= data[i];
        }
        return calc == checksum;
    }
    
    // MK3 Checksum validation (simplified)
    bool isChecksumValidMk3() const {
        uint8_t cs = 0;
        for (int i = 2; i < length + 4; i++) {
            cs += data[i - 4];
        }
        return (cs == 0);
    }
    
    bool isChecksumValid() const {
        if (isMk3Frame) {
            return isChecksumValidMk3();
        } else {
            return isChecksumValidMk2();
        }
    }
};

// DC Information Message (0x02)
struct VeBusDcInfo {
    float dcVoltage;        // DC voltage in V
    float dcCurrent;        // DC current in A
    float batteryAh;        // Battery Ah remaining
    uint8_t status;         // Device status
    uint8_t errorCode;      // Error code if any
    
    void fromFrame(const VeBusFrame& frame) {
        if (frame.command == 0x02 && frame.length >= 8) {
            dcVoltage = ((frame.data[1] << 8) | frame.data[0]) / 100.0f;
            dcCurrent = ((frame.data[3] << 8) | frame.data[2]) / 10.0f;
            if (frame.data[3] & 0x80) dcCurrent = -dcCurrent; // Sign bit
            batteryAh = ((frame.data[5] << 8) | frame.data[4]) / 10.0f;
            status = frame.data[6];
            errorCode = frame.data[7];
        }
    }
};

// AC Information Message (0x03)
struct VeBusAcInfo {
    float acVoltage;        // AC voltage in V
    float acCurrent;        // AC current in A
    float acFrequency;      // AC frequency in Hz
    int16_t acPower;        // AC power in W
    float powerFactor;      // Power factor
    uint8_t acStatus;       // AC status flags
    
    void fromFrame(const VeBusFrame& frame) {
        if (frame.command == 0x03 && frame.length >= 12) {
            acVoltage = ((frame.data[1] << 8) | frame.data[0]) / 100.0f;
            acCurrent = ((frame.data[3] << 8) | frame.data[2]) / 100.0f;
            acFrequency = ((frame.data[5] << 8) | frame.data[4]) / 100.0f;
            acPower = (frame.data[7] << 8) | frame.data[6];
            powerFactor = frame.data[8] / 100.0f;
            acStatus = frame.data[9];
        }
    }
};

// LED Status Message (0x04)
struct VeBusLedStatus {
    uint8_t ledStatus;          // LED status register
    uint8_t switchRegister;     // Switch register
    bool ledOn;                 // LED on/off
    bool ledBlink;              // LED blinking
    float inputCurrentLimit;    // Input current limit in A
    uint8_t inputConfig;        // Input configuration
    
    // Individual LED states for extended status
    uint8_t mainLed;
    uint8_t absorbLed;
    uint8_t bulkLed;
    uint8_t floatLed;
    uint8_t invertLed;
    uint8_t overloadLed;
    uint8_t lowBatteryLed;
    uint8_t temperatureLed;
    
    void fromFrame(const VeBusFrame& frame) {
        if (frame.command == 0x04 && frame.length >= 6) {
            ledStatus = frame.data[0];
            switchRegister = frame.data[1];
            ledOn = (frame.data[2] & 0x01) != 0;
            ledBlink = (frame.data[2] & 0x02) != 0;
            inputCurrentLimit = frame.data[3] / 10.0f;
            inputConfig = frame.data[4];
        }
    }
};

// ESS Power Command Message
struct VeBusEssPowerCommand {
    int16_t targetPower;        // Target power in W (positive = charge, negative = discharge)
    uint8_t commandId;          // Command sequence ID
    
    VeBusFrame toFrame() const {
        VeBusFrame frame;
        frame.address = 0x00;       // Broadcast address
        frame.command = VEBUS_CMD_SET_ESS_POWER;
        frame.length = 3;
        frame.data[0] = targetPower & 0xFF;
        frame.data[1] = (targetPower >> 8) & 0xFF;
        frame.data[2] = commandId;
        frame.calculateChecksum();
        return frame;
    }
};

// Input Current Limit Command
struct VeBusCurrentLimitCommand {
    uint8_t currentLimit;       // Current limit in A
    uint8_t commandId;          // Command sequence ID
    
    VeBusFrame toFrame() const {
        VeBusFrame frame;
        frame.address = 0x00;
        frame.command = VEBUS_CMD_SET_INPUT_CURRENT;
        frame.length = 2;
        frame.data[0] = currentLimit;
        frame.data[1] = commandId;
        frame.calculateChecksum();
        return frame;
    }
};

// Switch Command (On/Off/Charger Only)
struct VeBusSwitchCommand {
    uint8_t switchState;        // 3 = On, 4 = Off, 1 = Charger Only
    uint8_t commandId;          // Command sequence ID
    
    VeBusFrame toFrame() const {
        VeBusFrame frame;
        frame.address = 0x00;
        frame.command = VEBUS_CMD_SET_SWITCH;
        frame.length = 2;
        frame.data[0] = switchState;
        frame.data[1] = commandId;
        frame.calculateChecksum();
        return frame;
    }
};

// Complete VE.Bus Device State
struct VeBusDeviceState {
    VeBusDcInfo dcInfo;
    VeBusAcInfo acInfo;
    VeBusLedStatus ledStatus;
    uint32_t lastUpdateTime;
    bool isOnline;
    uint8_t communicationErrors;
    uint8_t switchState;  // Add missing switchState member
    
    VeBusDeviceState() {
        lastUpdateTime = 0;
        isOnline = false;
        communicationErrors = 0;
        switchState = 0;
    }
    
    void updateTimestamp() {
        lastUpdateTime = millis();
        isOnline = true;
    }
    
    bool isStale(uint32_t timeoutMs = 5000) const {
        return (millis() - lastUpdateTime) > timeoutMs;
    }
};

#endif // VEBUS_MESSAGES_H
