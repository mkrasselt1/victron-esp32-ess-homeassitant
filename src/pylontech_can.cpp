#include "pylontech_can.h"
#include <esp_log.h>

static const char* TAG = "PylontechCAN";

// External reference to system data
extern SystemData systemData;

PylontechCAN::PylontechCAN() : canTaskHandle(nullptr), isInitialized(false), isRunning(false) {
    // Initialize CAN configuration using the proper initializer with correct types
    twai_general_config_t temp_g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    g_config = temp_g_config;
    t_config = CAN_BITRATE;
    f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

PylontechCAN::~PylontechCAN() {
    end();
}

bool PylontechCAN::begin() {
    ESP_LOGI(TAG, "Initializing Pylontech CAN communication...");
    
    // Install TWAI driver
    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Start TWAI driver
    ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver: %s", esp_err_to_name(ret));
        twai_driver_uninstall();
        return false;
    }
    
    isInitialized = true;
    
    // Create CAN communication task
    BaseType_t taskResult = xTaskCreatePinnedToCore(
        canTaskWrapper,
        "PylontechCAN",
        4096,           // Stack size
        this,           // Task parameter
        2,              // Priority
        &canTaskHandle,
        1               // Pin to core 1
    );
    
    if (taskResult != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CAN task");
        twai_stop();
        twai_driver_uninstall();
        isInitialized = false;
        return false;
    }
    
    ESP_LOGI(TAG, "Pylontech CAN communication started successfully");
    return true;
}

void PylontechCAN::end() {
    if (isRunning && canTaskHandle != nullptr) {
        isRunning = false;
        vTaskDelete(canTaskHandle);
        canTaskHandle = nullptr;
    }
    
    if (isInitialized) {
        twai_stop();
        twai_driver_uninstall();
        isInitialized = false;
    }
    
    ESP_LOGI(TAG, "Pylontech CAN communication stopped");
}

void PylontechCAN::canTaskWrapper(void* parameter) {
    PylontechCAN* instance = static_cast<PylontechCAN*>(parameter);
    instance->canTask();
}

void PylontechCAN::canTask() {
    isRunning = true;
    twai_message_t message;
    
    ESP_LOGI(TAG, "CAN task started");
    
    while (isRunning) {
        // Wait for CAN message with timeout
        esp_err_t ret = twai_receive(&message, pdMS_TO_TICKS(100));
        
        if (ret == ESP_OK) {
            messagesReceived++;
            lastMessageTime = millis();
            processCanMessage(message);
        } else if (ret == ESP_ERR_TIMEOUT) {
            // Normal timeout, continue
        } else {
            messagesErrors++;
            ESP_LOGW(TAG, "CAN receive error: %s", esp_err_to_name(ret));
        }
        
        // Small delay to prevent task starvation
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "CAN task ended");
    vTaskDelete(nullptr);
}

void PylontechCAN::processCanMessage(const twai_message_t& message) {
    // Process based on CAN ID
    switch (message.identifier) {
        case PYLONTECH_BATTERY_VOLTAGE_ID:
            processBatteryVoltage(message);
            break;
            
        case PYLONTECH_BATTERY_CURRENT_ID:
            processBatteryCurrent(message);
            break;
            
        case PYLONTECH_BATTERY_SOC_ID:
            processBatterySOC(message);
            break;
            
        case PYLONTECH_BATTERY_TEMP_ID:
            processBatteryTemperature(message);
            break;
            
        case PYLONTECH_BATTERY_LIMITS_ID:
            processBatteryLimits(message);
            break;
            
        case PYLONTECH_BATTERY_STATUS_ID:
            processBatteryStatus(message);
            break;
            
        default:
            // Unknown message ID
            break;
    }
}

void PylontechCAN::processBatteryVoltage(const twai_message_t& message) {
    if (message.data_length_code >= 4) {
        uint16_t voltage_raw = bytesToUint16(message.data[1], message.data[0]);
        systemData.battery.voltage = voltage_raw / 100.0f; // Convert to volts
        
        ESP_LOGD(TAG, "Battery voltage: %.2fV", systemData.battery.voltage);
    }
}

void PylontechCAN::processBatteryCurrent(const twai_message_t& message) {
    if (message.data_length_code >= 4) {
        int16_t current_raw = bytesToInt16(message.data[1], message.data[0]);
        systemData.battery.current = current_raw / 10.0f; // Convert to amps
        
        // Calculate power
        systemData.battery.power = (int)(systemData.battery.voltage * systemData.battery.current);
        
        ESP_LOGD(TAG, "Battery current: %.1fA, power: %dW", 
                systemData.battery.current, systemData.battery.power);
    }
}

void PylontechCAN::processBatterySOC(const twai_message_t& message) {
    if (message.data_length_code >= 2) {
        systemData.battery.soc = message.data[0]; // SOC in percent
        
        ESP_LOGD(TAG, "Battery SOC: %d%%", systemData.battery.soc);
    }
}

void PylontechCAN::processBatteryTemperature(const twai_message_t& message) {
    if (message.data_length_code >= 4) {
        int16_t temp_raw = bytesToInt16(message.data[1], message.data[0]);
        systemData.battery.temperature = temp_raw / 10.0f; // Convert to Celsius
        
        ESP_LOGD(TAG, "Battery temperature: %.1f°C", systemData.battery.temperature);
    }
}

void PylontechCAN::processBatteryLimits(const twai_message_t& message) {
    if (message.data_length_code >= 8) {
        uint16_t charge_voltage = bytesToUint16(message.data[1], message.data[0]);
        uint16_t charge_current = bytesToUint16(message.data[3], message.data[2]);
        uint16_t discharge_current = bytesToUint16(message.data[5], message.data[4]);
        uint16_t discharge_voltage = bytesToUint16(message.data[7], message.data[6]);
        
        systemData.battery.chargeVoltage = charge_voltage / 100.0f;
        systemData.battery.chargeCurrentLimit = charge_current / 10.0f;
        systemData.battery.dischargeCurrentLimit = discharge_current / 10.0f;
        systemData.battery.dischargeVoltage = discharge_voltage / 100.0f;
        
        ESP_LOGD(TAG, "Battery limits - CV: %.2fV, CCL: %.1fA, DCL: %.1fA, DV: %.2fV",
                systemData.battery.chargeVoltage, systemData.battery.chargeCurrentLimit,
                systemData.battery.dischargeCurrentLimit, systemData.battery.dischargeVoltage);
    }
}

void PylontechCAN::processBatteryStatus(const twai_message_t& message) {
    if (message.data_length_code >= 4) {
        systemData.battery.protectionFlags1 = message.data[0];
        systemData.battery.protectionFlags2 = message.data[1];
        systemData.battery.warningFlags1 = message.data[2];
        systemData.battery.warningFlags2 = message.data[3];
        
        ESP_LOGD(TAG, "Battery status - P1: 0x%02X, P2: 0x%02X, W1: 0x%02X, W2: 0x%02X",
                systemData.battery.protectionFlags1, systemData.battery.protectionFlags2,
                systemData.battery.warningFlags1, systemData.battery.warningFlags2);
    }
}

bool PylontechCAN::isBatteryOnline() const {
    return (millis() - lastMessageTime) < 5000; // Online if message received in last 5 seconds
}

// Helper functions
uint16_t PylontechCAN::bytesToUint16(uint8_t high, uint8_t low) {
    return (static_cast<uint16_t>(high) << 8) | low;
}

int16_t PylontechCAN::bytesToInt16(uint8_t high, uint8_t low) {
    return static_cast<int16_t>(bytesToUint16(high, low));
}

uint32_t PylontechCAN::bytesToUint32(uint8_t byte3, uint8_t byte2, uint8_t byte1, uint8_t byte0) {
    return (static_cast<uint32_t>(byte3) << 24) |
           (static_cast<uint32_t>(byte2) << 16) |
           (static_cast<uint32_t>(byte1) << 8) |
           static_cast<uint32_t>(byte0);
}
