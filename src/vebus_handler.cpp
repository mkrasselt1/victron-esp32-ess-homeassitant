/*
 * VE.Bus Communication Handler Implementation
 * 
 * SPDX-FileCopyrightText: © 2023 PV Baxi <pv-baxi@gmx.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vebus_handler.h"
#include "system_data.h"

// Global instance
// VeBusHandler veBusHandler; // Removed - defined in main.cpp

VeBusHandler::VeBusHandler() {
    serial = nullptr;
    taskHandle = nullptr;
    commandQueue = nullptr;
    mutex = nullptr;
    lastCommandId = 0;
    isRunning = false;
    debugMode = false;
    rxBufferPos = 0;
    lastRxTime = 0;
    waitingForResponse = false;
    responseTimeout = 0;
}

VeBusHandler::~VeBusHandler() {
    end();
}

bool VeBusHandler::begin(int rxPin, int txPin, long baudRate) {
    // Initialize hardware serial
    serial = &Serial2;
    serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
    
    // Create mutex for thread-safe access
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr) {
        Serial.println("VeBus: Failed to create mutex");
        return false;
    }
    
    // Create command queue
    commandQueue = xQueueCreate(VEBUS_QUEUE_SIZE, sizeof(VeBusCommandItem));
    if (commandQueue == nullptr) {
        Serial.println("VeBus: Failed to create command queue");
        vSemaphoreDelete(mutex);
        return false;
    }
    
    // Create communication task
    BaseType_t result = xTaskCreatePinnedToCore(
        taskWrapper,
        "VeBusTask",
        VEBUS_TASK_STACK_SIZE,
        this,
        VEBUS_TASK_PRIORITY,
        &taskHandle,
        VEBUS_TASK_CORE
    );
    
    if (result != pdPASS) {
        Serial.println("VeBus: Failed to create task");
        vQueueDelete(commandQueue);
        vSemaphoreDelete(mutex);
        return false;
    }
    
    isRunning = true;
    stats.reset();
    resetRxBuffer();
    
    Serial.println("VeBus: Communication handler initialized");
    return true;
}

void VeBusHandler::end() {
    isRunning = false;
    
    // Wait for task to finish
    if (taskHandle != nullptr) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
    }
    
    // Clean up resources
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }
    
    if (mutex != nullptr) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
    
    if (serial != nullptr) {
        serial->end();
        serial = nullptr;
    }
}

void VeBusHandler::taskWrapper(void* parameter) {
    VeBusHandler* handler = static_cast<VeBusHandler*>(parameter);
    handler->communicationTask();
}

void VeBusHandler::communicationTask() {
    VeBusFrame receivedFrame;
    VeBusCommandItem commandItem;
    TickType_t lastWakeTime = xTaskGetTickCount();
    
    while (isRunning) {
        // Process incoming frames
        if (receiveFrame(receivedFrame)) {
            processReceivedFrame(receivedFrame);
            stats.framesReceived++;
        }
        
        // Process command queue
        if (xQueueReceive(commandQueue, &commandItem, 0) == pdTRUE) {
            if (sendFrame(commandItem.frame)) {
                stats.framesSent++;
                
                if (commandItem.waitForResponse) {
                    pendingCommand = commandItem;
                    waitingForResponse = true;
                    responseTimeout = millis() + VEBUS_TIMEOUT_MS;
                }
            } else {
                stats.framesDropped++;
                
                // Retry if possible
                if (commandItem.retryCount < VEBUS_MAX_RETRY_COUNT) {
                    commandItem.retryCount++;
                    xQueueSendToBack(commandQueue, &commandItem, 0);
                    stats.retransmissions++;
                }
            }
        }
        
        // Handle timeouts
        if (waitingForResponse && millis() > responseTimeout) {
            handleTimeout();
        }
        
        // Update device online status
        if (deviceState.isStale()) {
            if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
                deviceState.isOnline = false;
                xSemaphoreGive(mutex);
            }
        }
        
        // Task timing - run at 100Hz
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
    }
    
    vTaskDelete(nullptr);
}

bool VeBusHandler::receiveFrame(VeBusFrame& frame) {
    while (serial->available()) {
        uint8_t byte = serial->read();
        lastRxTime = millis();
        
        // Look for sync byte
        if (rxBufferPos == 0 && byte != VEBUS_SYNC_BYTE) {
            continue;
        }
        
        // Store byte in buffer
        if (rxBufferPos < VEBUS_FRAME_SIZE) {
            rxBuffer[rxBufferPos++] = byte;
        } else {
            // Buffer overflow - reset
            resetRxBuffer();
            stats.framesDropped++;
            continue;
        }
        
        // Check if frame is complete
        if (isFrameComplete()) {
            // Copy to frame structure
            frame.sync = rxBuffer[0];
            frame.address = rxBuffer[1];
            frame.command = rxBuffer[2];
            frame.length = rxBuffer[3];
            
            uint8_t dataLength = min(frame.length, (uint8_t)28);
            memcpy(frame.data, &rxBuffer[4], dataLength);
            frame.checksum = rxBuffer[4 + dataLength];
            
            resetRxBuffer();
            
            // Validate checksum
            if (frame.isChecksumValid()) {
                return true;
            } else {
                stats.checksumErrors++;
                if (debugMode) {
                    Serial.printf("VeBus: Checksum error on frame type 0x%02X\n", frame.command);
                }
            }
        }
    }
    
    // Check for incomplete frame timeout
    if (rxBufferPos > 0 && (millis() - lastRxTime) > 100) {
        resetRxBuffer();
        stats.framesDropped++;
    }
    
    return false;
}

bool VeBusHandler::sendFrame(const VeBusFrame& frame) {
    if (serial == nullptr || !serial) {
        return false;
    }
    
    // Send frame bytes
    serial->write(frame.sync);
    serial->write(frame.address);
    serial->write(frame.command);
    serial->write(frame.length);
    
    for (int i = 0; i < frame.length && i < 28; i++) {
        serial->write(frame.data[i]);
    }
    
    serial->write(frame.checksum);
    serial->flush();
    
    if (debugMode) {
        Serial.printf("VeBus: Sent frame type 0x%02X, length %d\n", frame.command, frame.length);
    }
    
    return true;
}

void VeBusHandler::processReceivedFrame(const VeBusFrame& frame) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        deviceState.updateTimestamp();
        
        switch (frame.command) {
            case 0x02: // DC Info
                deviceState.dcInfo.fromFrame(frame);
                break;
                
            case 0x03: // AC Info
                deviceState.acInfo.fromFrame(frame);
                break;
                
            case 0x04: // LED Status
                deviceState.ledStatus.fromFrame(frame);
                break;
                
            case VEBUS_CMD_SET_ESS_POWER: // ESS Power Response
                if (waitingForResponse && pendingCommand.frame.command == VEBUS_CMD_SET_ESS_POWER) {
                    waitingForResponse = false;
                    if (debugMode) {
                        Serial.println("VeBus: ESS power command acknowledged");
                    }
                }
                break;
                
            default:
                if (debugMode) {
                    Serial.printf("VeBus: Unknown frame type 0x%02X\n", frame.command);
                }
                break;
        }
        
        xSemaphoreGive(mutex);
    }
    
    // Update legacy variables for compatibility
    updateLegacyVariables();
}

void VeBusHandler::handleTimeout() {
    waitingForResponse = false;
    stats.timeoutErrors++;
    
    if (debugMode) {
        Serial.println("VeBus: Command timeout");
    }
    
    // Retry pending command if possible
    if (pendingCommand.retryCount < VEBUS_MAX_RETRY_COUNT) {
        pendingCommand.retryCount++;
        pendingCommand.timestamp = millis();
        
        if (xQueueSendToBack(commandQueue, &pendingCommand, 0) == pdTRUE) {
            stats.retransmissions++;
        }
    }
}

bool VeBusHandler::isFrameComplete() {
    if (rxBufferPos < 4) {
        return false; // Need at least header
    }
    
    uint8_t expectedLength = 4 + rxBuffer[3] + 1; // Header + data + checksum
    return rxBufferPos >= expectedLength;
}

void VeBusHandler::resetRxBuffer() {
    rxBufferPos = 0;
    memset(rxBuffer, 0, sizeof(rxBuffer));
}

VeBusDeviceState VeBusHandler::getDeviceState() {
    VeBusDeviceState state;
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = deviceState;
        xSemaphoreGive(mutex);
    }
    
    return state;
}

VeBusStatistics VeBusHandler::getStatistics() {
    return stats; // Statistics are updated atomically
}

void VeBusHandler::resetStatistics() {
    stats.reset();
}

bool VeBusHandler::sendEssPowerCommand(int16_t targetPower) {
    VeBusEssPowerCommand cmd;
    cmd.targetPower = targetPower;
    cmd.commandId = ++lastCommandId;
    
    VeBusCommandItem item;
    item.frame = cmd.toFrame();
    item.waitForResponse = true;
    item.timestamp = millis();
    
    return xQueueSendToBack(commandQueue, &item, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool VeBusHandler::sendCurrentLimitCommand(uint8_t currentLimit) {
    VeBusCurrentLimitCommand cmd;
    cmd.currentLimit = currentLimit;
    cmd.commandId = ++lastCommandId;
    
    VeBusCommandItem item;
    item.frame = cmd.toFrame();
    item.waitForResponse = true;
    item.timestamp = millis();
    
    return xQueueSendToBack(commandQueue, &item, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool VeBusHandler::sendSwitchCommand(uint8_t switchState) {
    VeBusSwitchCommand cmd;
    cmd.switchState = switchState;
    cmd.commandId = ++lastCommandId;
    
    VeBusCommandItem item;
    item.frame = cmd.toFrame();
    item.waitForResponse = true;
    item.timestamp = millis();
    
    return xQueueSendToBack(commandQueue, &item, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool VeBusHandler::sendCustomCommand(const VeBusFrame& frame, bool waitForResponse) {
    VeBusCommandItem item;
    item.frame = frame;
    item.waitForResponse = waitForResponse;
    item.timestamp = millis();
    
    return xQueueSendToBack(commandQueue, &item, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool VeBusHandler::isDeviceOnline() const {
    return deviceState.isOnline && !deviceState.isStale();
}

uint32_t VeBusHandler::getLastCommunicationTime() const {
    return deviceState.lastUpdateTime;
}

float VeBusHandler::getCommunicationQuality() const {
    uint32_t totalFrames = stats.framesSent + stats.framesReceived;
    if (totalFrames == 0) return 0.0f;
    
    uint32_t errors = stats.checksumErrors + stats.timeoutErrors + stats.framesDropped;
    return 1.0f - (float)errors / totalFrames;
}

// Compatibility methods for existing code
float VeBusHandler::getDcVoltage() {
    return getDeviceState().dcInfo.dcVoltage;
}

float VeBusHandler::getDcCurrent() {
    return getDeviceState().dcInfo.dcCurrent;
}

float VeBusHandler::getAcVoltage() {
    return getDeviceState().acInfo.acVoltage;
}

float VeBusHandler::getAcFrequency() {
    return getDeviceState().acInfo.acFrequency;
}

int16_t VeBusHandler::getAcPower() {
    return getDeviceState().acInfo.acPower;
}

uint8_t VeBusHandler::getDeviceStatus() {
    return getDeviceState().dcInfo.status;
}

bool VeBusHandler::getLedStatus() {
    return getDeviceState().ledStatus.ledOn;
}

float VeBusHandler::getInputCurrentLimit() {
    return getDeviceState().ledStatus.inputCurrentLimit;
}

void VeBusHandler::updateLegacyVariables() {
    // Update the legacy variables in systemData for backward compatibility
    VeBusDeviceState state = getDeviceState();
    
    systemData.multiplus.dcVoltage = state.dcInfo.dcVoltage;
    systemData.multiplus.dcCurrent = state.dcInfo.dcCurrent;
    systemData.multiplus.batteryAh = state.dcInfo.batteryAh;
    systemData.multiplus.uMainsRMS = state.acInfo.acVoltage;
    systemData.multiplus.acFrequency = state.acInfo.acFrequency;
    systemData.multiplus.powerFactor = state.acInfo.powerFactor;
    // Temporarily comment out LED status updates to avoid macro conflicts
    // TODO: Fix macro conflicts and re-enable these assignments
    // systemData.multiplus.masterMultiLED_Status = state.ledStatus.ledStatus;
    // systemData.multiplus.masterMultiLED_SwitchRegister = state.ledStatus.switchRegister;
    // systemData.multiplus.masterMultiLED_LEDon = state.ledStatus.ledOn;
    // systemData.multiplus.masterMultiLED_LEDblink = state.ledStatus.ledBlink;
    // systemData.multiplus.masterMultiLED_ActualInputCurrentLimit = state.ledStatus.inputCurrentLimit;
    // systemData.multiplus.masterMultiLED_AcInputConfiguration = state.ledStatus.inputConfig;
}

// New MK2 Protocol API Functions for External Control
bool VeBusHandler::requestVersionInfo(VeBusVersionInfo& info) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_GET_VERSION;
    frame.length = 1;
    frame.data[0] = 0x00; // Request all version info
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    if (success) {
        // Wait for response with timeout
        TickType_t startTime = xTaskGetTickCount();
        while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(responseTimeout)) {
            VeBusFrame response;
            if (receiveFrame(response) && response.command == VEBUS_CMD_GET_VERSION) {
                info.productId = response.data[0];
                info.firmwareVersion = response.data[1];
                info.protocolVersion = response.data[2];
                xSemaphoreGive(mutex);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    xSemaphoreGive(mutex);
    return false;
}

bool VeBusHandler::requestDeviceStatus(VeBusDeviceStatusInfo& status) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_GET_DEVICE_STATUS;
    frame.length = 1;
    frame.data[0] = 0x00;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    if (success) {
        TickType_t startTime = xTaskGetTickCount();
        while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(responseTimeout)) {
            VeBusFrame response;
            if (receiveFrame(response) && response.command == VEBUS_CMD_GET_DEVICE_STATUS) {
                status.state = response.data[0];
                status.mode = response.data[1];
                status.alarm = response.data[2];
                status.warnings = response.data[3];
                xSemaphoreGive(mutex);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    xSemaphoreGive(mutex);
    return false;
}

bool VeBusHandler::requestErrorInfo(VeBusErrorInfo& error) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_GET_ERROR_INFO;
    frame.length = 1;
    frame.data[0] = 0x00;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    if (success) {
        TickType_t startTime = xTaskGetTickCount();
        while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(responseTimeout)) {
            VeBusFrame response;
            if (receiveFrame(response) && response.command == VEBUS_CMD_GET_ERROR_INFO) {
                error.errorCode = response.data[0];
                error.errorSubCode = response.data[1];
                error.errorCounter = (uint32_t(response.data[2]) << 24) | 
                                   (uint32_t(response.data[3]) << 16) |
                                   (uint32_t(response.data[4]) << 8) | 
                                   uint32_t(response.data[5]);
                error.timestamp = (uint32_t(response.data[6]) << 24) |
                                 (uint32_t(response.data[7]) << 16) |
                                 (uint32_t(response.data[8]) << 8) |
                                 uint32_t(response.data[9]);
                xSemaphoreGive(mutex);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    xSemaphoreGive(mutex);
    return false;
}

bool VeBusHandler::requestWarningInfo(VeBusWarningInfo& warning) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_GET_WARNING_INFO;
    frame.length = 1;
    frame.data[0] = 0x00;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    if (success) {
        TickType_t startTime = xTaskGetTickCount();
        while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(responseTimeout)) {
            VeBusFrame response;
            if (receiveFrame(response) && response.command == VEBUS_CMD_GET_WARNING_INFO) {
                warning.warningFlags = (uint16_t(response.data[0]) << 8) | uint16_t(response.data[1]);
                warning.batteryVoltageWarning = response.data[2];
                warning.temperatureWarning = response.data[3];
                warning.overloadWarning = response.data[4];
                warning.dcRippleWarning = response.data[5];
                xSemaphoreGive(mutex);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    xSemaphoreGive(mutex);
    return false;
}

bool VeBusHandler::requestLedStatus(VeBusLedStatus& ledStatus) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_GET_LED_STATUS;
    frame.length = 1;
    frame.data[0] = 0x00;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    if (success) {
        TickType_t startTime = xTaskGetTickCount();
        while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(responseTimeout)) {
            VeBusFrame response;
            if (receiveFrame(response) && response.command == VEBUS_CMD_GET_LED_STATUS) {
                ledStatus.mainLed = response.data[0];
                ledStatus.absorbLed = response.data[1];
                ledStatus.bulkLed = response.data[2];
                ledStatus.floatLed = response.data[3];
                ledStatus.invertLed = response.data[4];
                ledStatus.overloadLed = response.data[5];
                ledStatus.lowBatteryLed = response.data[6];
                ledStatus.temperatureLed = response.data[7];
                xSemaphoreGive(mutex);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    xSemaphoreGive(mutex);
    return false;
}

bool VeBusHandler::setSwitchState(VeBusSwitchState state) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_SET_SWITCH;
    frame.length = 2;
    frame.data[0] = 0x00; // Device address
    frame.data[1] = (uint8_t)state;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    xSemaphoreGive(mutex);
    
    if (success) {
        // Update device state
        deviceState.switchState = (uint8_t)state;
        stats.framesSent++;
    }
    
    return success;
}

bool VeBusHandler::resetDevice() {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_DEVICE_RESET;
    frame.length = 2;
    frame.data[0] = 0x00; // Device address
    frame.data[1] = 0x01; // Reset command
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    xSemaphoreGive(mutex);
    
    if (success) {
        stats.framesSent++;
        // Clear device state after reset
        memset(&deviceState, 0, sizeof(deviceState));
    }
    
    return success;
}

bool VeBusHandler::clearErrors() {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_CLEAR_ERRORS;
    frame.length = 2;
    frame.data[0] = 0x00; // Device address
    frame.data[1] = 0x01; // Clear command
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    xSemaphoreGive(mutex);
    
    if (success) {
        stats.framesSent++;
    }
    
    return success;
}

bool VeBusHandler::enableAutoRestart(bool enable) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_SET_AUTO_RESTART;
    frame.length = 2;
    frame.data[0] = 0x00; // Device address
    frame.data[1] = enable ? 0x01 : 0x00;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    xSemaphoreGive(mutex);
    
    if (success) {
        stats.framesSent++;
    }
    
    return success;
}

bool VeBusHandler::setVoltageRange(float minVoltage, float maxVoltage) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_SET_VOLTAGE_RANGE;
    frame.length = 5;
    frame.data[0] = 0x00; // Device address
    
    // Convert float to 16-bit integer (voltage * 100)
    uint16_t minV = (uint16_t)(minVoltage * 100);
    uint16_t maxV = (uint16_t)(maxVoltage * 100);
    
    frame.data[1] = minV >> 8;
    frame.data[2] = minV & 0xFF;
    frame.data[3] = maxV >> 8;
    frame.data[4] = maxV & 0xFF;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    xSemaphoreGive(mutex);
    
    if (success) {
        stats.framesSent++;
    }
    
    return success;
}

bool VeBusHandler::setFrequencyRange(float minFreq, float maxFreq) {
    if (!isInitialized()) {
        return false;
    }
    
    VeBusFrame frame;
    frame.command = VEBUS_CMD_SET_FREQUENCY_RANGE;
    frame.length = 5;
    frame.data[0] = 0x00; // Device address
    
    // Convert float to 16-bit integer (frequency * 100)
    uint16_t minF = (uint16_t)(minFreq * 100);
    uint16_t maxF = (uint16_t)(maxFreq * 100);
    
    frame.data[1] = minF >> 8;
    frame.data[2] = minF & 0xFF;
    frame.data[3] = maxF >> 8;
    frame.data[4] = maxF & 0xFF;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool success = sendFrame(frame);
    xSemaphoreGive(mutex);
    
    if (success) {
        stats.framesSent++;
    }
    
    return success;
}
