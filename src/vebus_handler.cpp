/*
 * VE.Bus Communication Handler Implementation
 * 
 * SPDX-FileCopyrightText: © 2023 PV Baxi <pv-baxi@gmx.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vebus_handler.h"
#include "system_data.h"

// External debug function declaration
extern void publishDebugMessage(const String& message, const String& level);

// Global instance
// VeBusHandler veBusHandler; // Removed - defined in main.cpp

VeBusHandler::VeBusHandler() {
    serial = nullptr;
    taskHandle = nullptr;
    commandQueue = nullptr;
    mutex = nullptr;
    lastCommandId = 0;
    isRunning = false;
    debugMode = true;  // Enable debug mode by default
    rxBufferPos = 0;
    lastRxTime = 0;
    waitingForResponse = false;
    responseTimeout = 0;
}

VeBusHandler::~VeBusHandler() {
    end();
}

bool VeBusHandler::begin(int rxPin, int txPin, long baudRate) {
    Serial.println("VeBus: Starting initialization...");
    
    // Enable debug mode for troubleshooting
    debugMode = true;
    
    if (debugMode) publishDebugMessage("VeBus: Starting initialization...", "info");
    
    // Initialize hardware serial for RS485
    serial = &Serial2;
    serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
    Serial.printf("VeBus: Serial initialized on pins RX:%d TX:%d\n", rxPin, txPin);
    if (debugMode) {
        char msg[64];
        snprintf(msg, sizeof(msg), "VeBus: Serial initialized on pins RX:%d TX:%d", rxPin, txPin);
        publishDebugMessage(msg, "info");
    }
    
    // Configure RS485 half-duplex mode - try to enable it
    pinMode(VEBUS_DE_PIN, OUTPUT);  // Driver Enable
    pinMode(VEBUS_SE_PIN, OUTPUT);  // Send Enable
    digitalWrite(VEBUS_DE_PIN, LOW);  // Start in receive mode
    digitalWrite(VEBUS_SE_PIN, LOW);  // Start in receive mode
    Serial.printf("VeBus: RS485 pins configured - DE:%d SE:%d\n", VEBUS_DE_PIN, VEBUS_SE_PIN);
    if (debugMode) {
        char msg[64];
        snprintf(msg, sizeof(msg), "VeBus: RS485 pins configured - DE:%d SE:%d", VEBUS_DE_PIN, VEBUS_SE_PIN);
        publishDebugMessage(msg, "info");
    }
    
    // Try to set RS485 mode if available
    #ifdef UART_MODE_RS485_HALF_DUPLEX
    serial->setMode(UART_MODE_RS485_HALF_DUPLEX);
    Serial.println("VeBus: UART_MODE_RS485_HALF_DUPLEX enabled");
    #else
    Serial.println("VeBus: UART_MODE_RS485_HALF_DUPLEX not available");
    #endif
    
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
    
    Serial.printf("VeBus: MK3 Communication handler initialized at %ld baud (RX:IO%d, TX:IO%d, DE:IO%d, SE:IO%d)\n", 
                 baudRate, rxPin, txPin, VEBUS_DE_PIN, VEBUS_SE_PIN);
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
    
    // Send initial debug message
    Serial.println("VeBus: TASK STARTED - communicationTask is running!");
    Serial.println("VeBus: About to send WebSocket debug message");
    if (debugMode) {
        publishDebugMessage("VeBus: TASK STARTED - communicationTask is running!", "info");
        Serial.println("VeBus: WebSocket debug message sent");
    }
    
    while (isRunning) {
        // Debug: Show that task loop is running
        static uint32_t loopCounter = 0;
        if (++loopCounter % 100 == 0) {  // Every 100 iterations (1 second at 100Hz)
            Serial.printf("VeBus: Task loop running (iteration %u)\n", loopCounter);
        }
        
        // Send heartbeat debug message every 10 seconds
        static uint32_t lastHeartbeat = 0;
        if (millis() - lastHeartbeat > 10000 && debugMode) {
            char msg[64];
            snprintf(msg, sizeof(msg), "VeBus: communicationTask heartbeat (framesSent: %u)", stats.framesSent);
            publishDebugMessage(msg, "info");
        }
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
        
        // Send periodic status request to generate some frame traffic
        static uint32_t lastStatusRequest = 0;
        static uint8_t frameNumber = 0;
        
        // Debug: Check timing condition
        uint32_t currentMillis = millis();
        uint32_t timeDiff = currentMillis - lastStatusRequest;
        if (timeDiff % 1000 == 0 && timeDiff > 0) {  // Log every second
            Serial.printf("VeBus: Time check - current: %lu, last: %lu, diff: %lu\n", 
                         currentMillis, lastStatusRequest, timeDiff);
        }
        
        if (millis() - lastStatusRequest > 2000) { // Every 2 seconds (faster for testing)
            Serial.println("VeBus: PERIODIC REQUEST - About to send status frame");
            Serial.printf("VeBus: Current millis: %lu, lastStatusRequest: %lu\n", millis(), lastStatusRequest);
            if (debugMode) {
                publishDebugMessage("VeBus: PERIODIC REQUEST - About to send status frame", "info");
            }
            VeBusFrame statusFrame;
            statusFrame.isMk3Frame = true;
            statusFrame.frameNumber = frameNumber++;
            statusFrame.command = 0x30;  // Read RAM command
            statusFrame.length = 4;      // 4 bytes of data
            statusFrame.data[0] = 0x04;  // Battery voltage
            statusFrame.data[1] = 0x0E;  // AC Power
            statusFrame.data[2] = 0x00;  // Padding
            statusFrame.data[3] = 0x00;  // Padding
            
            // Calculate MK3 checksum
            statusFrame.calculateChecksum();
            
            // Send directly instead of queuing to ensure it gets sent
            Serial.println("VeBus: CALLING sendFrame function");
            Serial.printf("VeBus: Frame data - command: 0x%02X, length: %d\n", statusFrame.command, statusFrame.length);
            if (sendFrame(statusFrame)) {
                stats.framesSent++;  // Increment counter immediately
                lastStatusRequest = millis();
                Serial.printf("VeBus: ✓ Sent periodic MK3 status request #%d (framesSent: %u)\n", 
                             frameNumber - 1, stats.framesSent);
                if (debugMode) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "VeBus: ✓ Sent periodic MK3 status request #%d (framesSent: %u)", 
                             frameNumber - 1, stats.framesSent);
                    publishDebugMessage(msg, "success");
                }
            } else {
                Serial.println("VeBus: ✗ Failed to send periodic status request");
                if (debugMode) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "VeBus: ✗ Failed to send periodic status request #%d", frameNumber - 1);
                    publishDebugMessage(msg, "error");
                }
            }
        } else {
            // Debug: Show when condition is not met
            if (millis() % 5000 == 0) {  // Log every 5 seconds
                Serial.printf("VeBus: Waiting for periodic request - current: %lu, last: %lu, remaining: %lu ms\n",
                             millis(), lastStatusRequest, 2000 - (millis() - lastStatusRequest));
            }
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
    
    // Task cleanup - this should never be reached in normal operation
    Serial.println("VeBus: communicationTask ending");
    if (debugMode) publishDebugMessage("VeBus: communicationTask ending", "warning");
    vTaskDelete(nullptr);
}

bool VeBusHandler::receiveFrame(VeBusFrame& frame) {
    while (serial->available()) {
        uint8_t byte = serial->read();
        lastRxTime = millis();
        
        // Look for MK3 header or MK2 sync byte
        if (rxBufferPos == 0) {
            if (byte == VEBUS_MK3_HEADER1) {
                // Start of MK3 frame
                rxBuffer[rxBufferPos++] = byte;
                continue;
            } else if (byte == VEBUS_SYNC_BYTE) {
                // Start of MK2 frame
                rxBuffer[rxBufferPos++] = byte;
                continue;
            } else {
                // Not a valid frame start
                continue;
            }
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
            bool frameValid = false;
            
            if (rxBuffer[0] == VEBUS_MK3_HEADER1 && rxBuffer[1] == VEBUS_MK3_HEADER2) {
                // MK3 Frame
                frameValid = parseMk3Frame(frame);
            } else if (rxBuffer[0] == VEBUS_SYNC_BYTE) {
                // MK2 Frame
                frameValid = parseMk2Frame(frame);
            }
            
            resetRxBuffer();
            
            if (frameValid) {
                return true;
            } else {
                stats.checksumErrors++;
                if (debugMode) {
                    Serial.println("VeBus: Frame parsing/checksum error");
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

bool VeBusHandler::parseMk2Frame(VeBusFrame& frame) {
    if (rxBufferPos < 5) return false;  // Minimum MK2 frame size
    
    frame.isMk3Frame = false;
    frame.sync = rxBuffer[0];
    frame.address = rxBuffer[1];
    frame.command = rxBuffer[2];
    frame.length = rxBuffer[3];
    
    uint8_t expectedLength = 4 + frame.length + 1;  // Header + data + checksum
    if (rxBufferPos < expectedLength) return false;
    
    uint8_t dataLength = min(frame.length, (uint8_t)VEBUS_MK3_MAX_DATA_SIZE);
    memcpy(frame.data, &rxBuffer[4], dataLength);
    frame.checksum = rxBuffer[4 + dataLength];
    
    return frame.isChecksumValid();
}

bool VeBusHandler::parseMk3Frame(VeBusFrame& frame) {
    if (rxBufferPos < 8) return false;  // Minimum MK3 frame size
    
    frame.isMk3Frame = true;
    
    // Check for end of frame
    if (rxBuffer[rxBufferPos - 1] != VEBUS_MK3_END_FRAME) return false;
    
    // Extract frame number
    frame.frameNumber = rxBuffer[3];
    
    // Destuff the frame data
    uint8_t destuffedData[VEBUS_FRAME_SIZE];
    int destuffedLength = 0;
    
    // Skip MK3 header (4 bytes) and start destuffing
    for (int i = 4; i < rxBufferPos - 1; i++) {  // -1 to skip end frame
        uint8_t byte = rxBuffer[i];
        
        if (byte == VEBUS_MK3_STUFF_BYTE && i < rxBufferPos - 2) {
            // Next byte contains stuffed data
            i++;
            uint8_t nextByte = rxBuffer[i];
            if (nextByte >= 0x70 && nextByte <= 0x7F) {
                // Unstuff: 0x70-0x7F -> 0xFA-0xFF
                byte = 0xFA + (nextByte & 0x0F);
            } else {
                byte = nextByte + 0x80;
            }
        }
        
        if (destuffedLength < VEBUS_FRAME_SIZE) {
            destuffedData[destuffedLength++] = byte;
        }
    }
    
    if (destuffedLength < 4) return false;  // Need at least address + command + flags + data
    
    // Parse MK3 frame structure
    frame.address = destuffedData[0];
    frame.command = destuffedData[1];
    frame.length = destuffedLength - 4;  // Subtract address, command, flags, checksum
    
    // Copy data
    uint8_t dataLength = min(frame.length, (uint8_t)VEBUS_MK3_MAX_DATA_SIZE);
    memcpy(frame.data, &destuffedData[2], dataLength);
    
    // Checksum is the last byte before end frame
    frame.checksum = destuffedData[destuffedLength - 1];
    
    return frame.isChecksumValid();
}

bool VeBusHandler::sendFrame(const VeBusFrame& frame) {
    Serial.println("VeBus: sendFrame called");
    if (debugMode) publishDebugMessage("VeBus: sendFrame called", "debug");
    
    if (serial == nullptr || !serial) {
        return false;
    }
    
    if (frame.isMk3Frame) {
        // MK3 Protocol sending - use correct format from reference implementation
        return sendFrameMk3Correct(frame);
    } else {
        // MK2 Protocol sending (legacy)
        // RS485: Switch to transmit mode
        digitalWrite(VEBUS_DE_PIN, HIGH);
        digitalWrite(VEBUS_SE_PIN, HIGH);
        delayMicroseconds(50);
        
        serial->write(frame.sync);
        serial->write(frame.address);
        serial->write(frame.command);
        serial->write(frame.length);
        
        for (int i = 0; i < frame.length && i < VEBUS_MK3_MAX_DATA_SIZE; i++) {
            serial->write(frame.data[i]);
        }
        
        serial->write(frame.checksum);
        serial->flush();
        
        // RS485: Switch back to receive mode
        delayMicroseconds(50);
        digitalWrite(VEBUS_DE_PIN, LOW);
        digitalWrite(VEBUS_SE_PIN, LOW);
        
        if (debugMode) {
            Serial.printf("VeBus: Sent MK2 frame type 0x%02X, length %d\n", frame.command, frame.length);
        }
        return true;
    }
}

bool VeBusHandler::sendFrameMk3Correct(const VeBusFrame& frame) {
    Serial.println("VeBus: sendFrameMk3Correct called");
    if (debugMode) publishDebugMessage("VeBus: sendFrameMk3Correct called", "debug");
    
    // Check if serial is available
    if (!serial) {
        Serial.println("VeBus: ERROR - Serial interface not initialized!");
        if (debugMode) publishDebugMessage("VeBus: ERROR - Serial interface not initialized!", "error");
        return false;
    }
    
    // Use the correct MK3 format from reference implementation
    uint8_t txBuffer[64];  // Buffer for command assembly
    int txLength = 0;
    
    // Build MK3 frame according to reference implementation
    txBuffer[txLength++] = VEBUS_MK3_HEADER1;     // 0x98
    txBuffer[txLength++] = VEBUS_MK3_HEADER2;     // 0xF7
    txBuffer[txLength++] = VEBUS_MK3_DATA_FRAME;  // 0xFE
    txBuffer[txLength++] = frame.frameNumber;    // Frame number
    
    // Our own ID
    txBuffer[txLength++] = 0x00;  // Our own ID high byte
    txBuffer[txLength++] = 0xE6;  // Our own ID low byte
    
    // Command and flags
    txBuffer[txLength++] = frame.command;  // Command
    txBuffer[txLength++] = 0x02;           // Flags (RAM var, no EEPROM)
    
    // Add data from frame
    for (int i = 0; i < frame.length && txLength < sizeof(txBuffer) - 3; i++) {
        txBuffer[txLength++] = frame.data[i];
    }
    
    Serial.printf("VeBus: Built frame with %d bytes before stuffing\n", txLength);
    if (debugMode) {
        char msg[64];
        snprintf(msg, sizeof(msg), "VeBus: Built frame with %d bytes before stuffing", txLength);
        publishDebugMessage(msg, "debug");
    }
    
    // Apply byte stuffing to the entire frame after header
    uint8_t stuffedBuffer[128];
    int stuffedLength = commandReplaceFAtoFF(stuffedBuffer, &txBuffer[4], txLength - 4);
    
    // Rebuild frame with stuffed data
    uint8_t finalBuffer[128];
    int finalLength = 0;
    
    // Copy header (first 4 bytes, no stuffing)
    for (int i = 0; i < 4; i++) {
        finalBuffer[finalLength++] = txBuffer[i];
    }
    
    // Copy stuffed data
    for (int i = 0; i < stuffedLength; i++) {
        finalBuffer[finalLength++] = stuffedBuffer[i];
    }
    
    // Calculate and append checksum
    finalLength = appendChecksum(finalBuffer, finalLength);
    
    Serial.printf("VeBus: Final frame length: %d bytes\n", finalLength);
    if (debugMode) {
        char msg[64];
        snprintf(msg, sizeof(msg), "VeBus: Final frame length: %d bytes", finalLength);
        publishDebugMessage(msg, "debug");
    }
    
    // RS485: Switch to transmit mode
    digitalWrite(VEBUS_DE_PIN, HIGH);
    digitalWrite(VEBUS_SE_PIN, HIGH);
    delayMicroseconds(50);  // Small delay for transceiver switching
    if (debugMode) publishDebugMessage("VeBus: RS485 switched to TX mode", "debug");
    
    Serial.printf("VeBus: RS485 pins set - DE:%d SE:%d\n", digitalRead(VEBUS_DE_PIN), digitalRead(VEBUS_SE_PIN));
    if (debugMode) {
        char msg[64];
        snprintf(msg, sizeof(msg), "VeBus: RS485 pins state - DE:%d SE:%d", digitalRead(VEBUS_DE_PIN), digitalRead(VEBUS_SE_PIN));
        publishDebugMessage(msg, "debug");
    }
    
    // Send the frame
    size_t bytesSent = serial->write(finalBuffer, finalLength);
    serial->flush();
    
    // RS485: Switch back to receive mode
    delayMicroseconds(50);  // Allow last byte to be sent
    digitalWrite(VEBUS_DE_PIN, LOW);
    digitalWrite(VEBUS_SE_PIN, LOW);
    if (debugMode) publishDebugMessage("VeBus: RS485 switched to RX mode", "debug");
    
    Serial.printf("VeBus: Sent %d bytes via serial\n", bytesSent);
    if (debugMode) {
        char msg[128];
        snprintf(msg, sizeof(msg), "VeBus: Sent %d bytes via serial (MK3 frame type 0x%02X)", 
                 bytesSent, frame.command);
        publishDebugMessage(msg, "success");
    }
    
    bool success = (bytesSent == finalLength);
    Serial.printf("VeBus: sendFrame result: %s (%d/%d bytes)\n", success ? "SUCCESS" : "FAILED", bytesSent, finalLength);
    if (debugMode) {
        char msg[64];
        snprintf(msg, sizeof(msg), "VeBus: Frame send result: %s (%d/%d)", success ? "SUCCESS" : "FAILED", bytesSent, finalLength);
        publishDebugMessage(msg, success ? "success" : "error");
    }
    
    return success;
}

int VeBusHandler::commandReplaceFAtoFF(uint8_t *outbuf, const uint8_t *inbuf, int inlength) {
    int j = 0;
    
    // Starting from the beginning, replace 0xFA..FF with double-byte character
    for (int i = 0; i < inlength; i++) {
        uint8_t c = inbuf[i];
        if (c >= 0xFA) {
            outbuf[j++] = VEBUS_MK3_STUFF_BYTE;
            outbuf[j++] = 0x70 | (c & 0x0F);
        } else {
            outbuf[j++] = c;    // No replacement
        }
    }
    return j;   // New length of output frame
}

int VeBusHandler::appendChecksum(uint8_t *buf, int inlength) {
    int j = 0;
    
    // Calculate checksum starting from 3rd byte
    uint8_t cs = 1;
    for (int i = 2; i < inlength; i++) {
        cs -= buf[i];
    }
    j = inlength;
    if (cs >= 0xFB) {
        // EXCEPTION: Only replace starting from 0xFB
        buf[j++] = VEBUS_MK3_STUFF_BYTE;
        buf[j++] = (cs - 0xFA);
    } else {
        buf[j++] = cs;
    }
    buf[j++] = VEBUS_MK3_END_FRAME;  // Append End Of Frame symbol
    return j;   // New length of output frame
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
    
    // Check for MK3 frame (starts with 0x98 0xF7)
    if (rxBuffer[0] == VEBUS_MK3_HEADER1 && rxBuffer[1] == VEBUS_MK3_HEADER2) {
        // MK3 frame ends with 0xFF
        return (rxBufferPos > 0 && rxBuffer[rxBufferPos - 1] == VEBUS_MK3_END_FRAME);
    }
    
    // MK2 frame (starts with 0xFF)
    if (rxBuffer[0] == VEBUS_SYNC_BYTE) {
        uint8_t expectedLength = 4 + rxBuffer[3] + 1; // Header + data + checksum
        return rxBufferPos >= expectedLength;
    }
    
    return false; // Invalid frame type
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
