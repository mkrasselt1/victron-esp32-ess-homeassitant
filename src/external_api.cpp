#include "external_api.h"

ExternalAPI::ExternalAPI(AsyncWebServer* webServer, VeBusHandler* veBus) 
    : server(webServer), veBusHandler(veBus) {
}

void ExternalAPI::setup() {
    if (!server || !veBusHandler) {
        Serial.println("[ExternalAPI] Error: server or veBusHandler is null");
        return;
    }
    
    Serial.println("[ExternalAPI] Setting up REST API endpoints...");
    
    // General status endpoint (simplified for testing without hardware)
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetGeneralStatus(request);
    });
    
    // Status and information endpoints
    server->on("/api/vebus/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatus(request);
    });
    
    server->on("/api/vebus/version", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetVersion(request);
    });
    
    server->on("/api/vebus/errors", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetErrors(request);
    });
    
    server->on("/api/vebus/warnings", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetWarnings(request);
    });
    
    server->on("/api/vebus/statistics", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatistics(request);
    });
    
    // Control endpoints
    server->on("/api/vebus/switch", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSetSwitch(request);
    });
    
    server->on("/api/vebus/power", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSetPower(request);
    });
    
    server->on("/api/vebus/current", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSetCurrent(request);
    });
    
    server->on("/api/vebus/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleReset(request);
    });
    
    server->on("/api/vebus/clear-errors", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleClearErrors(request);
    });
    
    // Configuration endpoints
    server->on("/api/vebus/config/auto-restart", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSetAutoRestart(request);
    });
    
    server->on("/api/vebus/config/voltage-range", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSetVoltageRange(request);
    });
    
    server->on("/api/vebus/config/frequency-range", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSetFrequencyRange(request);
    });
    
    Serial.println("[ExternalAPI] REST API endpoints registered successfully");
}

void ExternalAPI::sendJsonResponse(AsyncWebServerRequest* request, const JsonDocument& doc, int statusCode) {
    String json;
    serializeJson(doc, json);
    request->send(statusCode, "application/json", json);
}

void ExternalAPI::sendErrorResponse(AsyncWebServerRequest* request, const char* message, int statusCode) {
    JsonDocument doc;
    doc["error"] = message;
    doc["timestamp"] = millis();
    sendJsonResponse(request, doc, statusCode);
}

bool ExternalAPI::validateJsonRequest(AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!request->hasParam("plain", true)) {
        return false;
    }
    
    String body = request->getParam("plain", true)->value();
    DeserializationError error = deserializeJson(doc, body);
    return error == DeserializationError::Ok;
}

void ExternalAPI::handleGetGeneralStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    Serial.println("[API] Processing /api/status request (general status)");
    
    // System information
    doc["system"]["uptime"] = millis();
    doc["system"]["free_heap"] = ESP.getFreeHeap();
    doc["system"]["chip_model"] = ESP.getChipModel();
    doc["system"]["chip_cores"] = ESP.getChipCores();
    doc["system"]["chip_revision"] = ESP.getChipRevision();
    doc["system"]["flash_size"] = ESP.getFlashChipSize();
    
    // WiFi status
    doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
    if (WiFi.status() == WL_CONNECTED) {
        doc["wifi"]["ip"] = WiFi.localIP().toString();
        doc["wifi"]["ssid"] = WiFi.SSID();
        doc["wifi"]["rssi"] = WiFi.RSSI();
    }
    
    // VE.Bus status (safe check)
    if (veBusHandler) {
        doc["vebus"]["initialized"] = veBusHandler->isInitialized();
        doc["vebus"]["task_running"] = veBusHandler->isTaskRunning();
        doc["vebus"]["device_online"] = veBusHandler->isDeviceOnline();
    } else {
        doc["vebus"]["initialized"] = false;
        doc["vebus"]["task_running"] = false;
        doc["vebus"]["device_online"] = false;
        doc["vebus"]["note"] = "No hardware connected";
    }
    
    doc["api_version"] = "MK2-Extended-1.0";
    doc["timestamp"] = millis();
    
    Serial.println("[API] Sending general status response");
    sendJsonResponse(request, doc);
    Serial.println("[API] General status request completed successfully");
}

void ExternalAPI::handleGetStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    try {
        Serial.println("[API] Processing /api/status request");
        
        if (!veBusHandler) {
            Serial.println("[API] ERROR: veBusHandler is null");
            sendErrorResponse(request, "VE.Bus handler not available", 503);
            return;
        }
        
        if (!veBusHandler->isInitialized()) {
            Serial.println("[API] ERROR: VE.Bus handler not initialized");
            sendErrorResponse(request, "VE.Bus handler not initialized", 503);
            return;
        }
        
        Serial.println("[API] Adding basic status information");
        
        // Get basic status
        doc["initialized"] = veBusHandler->isInitialized();
        doc["task_running"] = veBusHandler->isTaskRunning();
        doc["device_online"] = veBusHandler->isDeviceOnline();
        doc["communication_quality"] = veBusHandler->getCommunicationQuality();
        doc["last_communication"] = veBusHandler->getLastCommunicationTime();
        
        Serial.println("[API] Getting device state");
        
        // Get device state - with error handling
        VeBusDeviceState deviceState = veBusHandler->getDeviceState();
        doc["dc_voltage"] = deviceState.dcInfo.dcVoltage;
        doc["dc_current"] = deviceState.dcInfo.dcCurrent;
        doc["ac_voltage"] = deviceState.acInfo.acVoltage;
        doc["ac_frequency"] = deviceState.acInfo.acFrequency;
        doc["ac_power"] = deviceState.acInfo.acPower;
        doc["switch_state"] = deviceState.switchState;
        doc["device_status"] = deviceState.switchState;
        
        Serial.println("[API] Attempting to get detailed device status");
        
        // Get detailed device status - with error handling
        VeBusDeviceStatusInfo status;
        if (veBusHandler->requestDeviceStatus(status)) {
            Serial.println("[API] Device status request successful");
            doc["device_state"] = status.state;
            doc["device_mode"] = status.mode;
            doc["device_alarm"] = status.alarm;
            doc["device_warnings"] = status.warnings;
        } else {
            Serial.println("[API] Device status request failed - using defaults");
            doc["device_state"] = 0;
            doc["device_mode"] = 0;
            doc["device_alarm"] = 0;
            doc["device_warnings"] = 0;
        }
        
        doc["api_version"] = "MK2-Extended-1.0";
        doc["timestamp"] = millis();
        
        Serial.println("[API] Sending JSON response");
        sendJsonResponse(request, doc);
        Serial.println("[API] Status request completed successfully");
        
    } catch (...) {
        Serial.println("[API] EXCEPTION in handleGetStatus");
        sendErrorResponse(request, "Internal server error in status handler", 500);
    }
}

void ExternalAPI::handleGetVersion(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    if (!veBusHandler->isInitialized()) {
        sendErrorResponse(request, "VE.Bus handler not initialized", 503);
        return;
    }
    
    VeBusVersionInfo versionInfo;
    if (veBusHandler->requestVersionInfo(versionInfo)) {
        doc["product_id"] = versionInfo.productId;
        doc["firmware_version"] = versionInfo.firmwareVersion;
        doc["protocol_version"] = versionInfo.protocolVersion;
        doc["api_version"] = "MK2-Extended-1.0";
        doc["success"] = true;
    } else {
        doc["success"] = false;
        doc["error"] = "Failed to retrieve version information";
    }
    
    doc["timestamp"] = millis();
    sendJsonResponse(request, doc);
}

void ExternalAPI::handleSetSwitch(AsyncWebServerRequest* request) {
    JsonDocument requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (requestDoc["state"].isNull()) {
        sendErrorResponse(request, "Missing 'state' parameter", 400);
        return;
    }
    
    int state = requestDoc["state"];
    if (state < 1 || state > 4) {
        sendErrorResponse(request, "Invalid switch state. Valid values: 1=charger only, 2=inverter only, 3=on, 4=off", 400);
        return;
    }
    
    bool success = veBusHandler->setSwitchState((VeBusSwitchState)state);
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["state"] = state;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set switch state";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleSetPower(AsyncWebServerRequest* request) {
    JsonDocument requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (requestDoc["power"].isNull()) {
        sendErrorResponse(request, "Missing 'power' parameter", 400);
        return;
    }
    
    int16_t power = requestDoc["power"];
    bool success = veBusHandler->sendEssPowerCommand(power);
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["power"] = power;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set ESS power";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleSetCurrent(AsyncWebServerRequest* request) {
    JsonDocument requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (requestDoc["current_limit"].isNull()) {
        sendErrorResponse(request, "Missing 'current_limit' parameter", 400);
        return;
    }
    
    uint8_t currentLimit = requestDoc["current_limit"];
    bool success = veBusHandler->sendCurrentLimitCommand(currentLimit);
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["current_limit"] = currentLimit;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set current limit";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleReset(AsyncWebServerRequest* request) {
    bool success = veBusHandler->resetDevice();
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to reset device";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleClearErrors(AsyncWebServerRequest* request) {
    bool success = veBusHandler->clearErrors();
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to clear errors";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleGetErrors(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    if (!veBusHandler->isInitialized()) {
        sendErrorResponse(request, "VE.Bus handler not initialized", 503);
        return;
    }
    
    VeBusErrorInfo errorInfo;
    if (veBusHandler->requestErrorInfo(errorInfo)) {
        doc["error_code"] = errorInfo.errorCode;
        doc["error_sub_code"] = errorInfo.errorSubCode;
        doc["error_counter"] = errorInfo.errorCounter;
        doc["timestamp"] = errorInfo.timestamp;
        doc["success"] = true;
    } else {
        doc["success"] = false;
        doc["error"] = "Failed to retrieve error information";
    }
    
    doc["request_timestamp"] = millis();
    sendJsonResponse(request, doc);
}

void ExternalAPI::handleGetWarnings(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    if (!veBusHandler->isInitialized()) {
        sendErrorResponse(request, "VE.Bus handler not initialized", 503);
        return;
    }
    
    VeBusWarningInfo warningInfo;
    if (veBusHandler->requestWarningInfo(warningInfo)) {
        doc["warning_flags"] = warningInfo.warningFlags;
        doc["battery_voltage_warning"] = warningInfo.batteryVoltageWarning;
        doc["temperature_warning"] = warningInfo.temperatureWarning;
        doc["overload_warning"] = warningInfo.overloadWarning;
        doc["dc_ripple_warning"] = warningInfo.dcRippleWarning;
        doc["success"] = true;
    } else {
        doc["success"] = false;
        doc["error"] = "Failed to retrieve warning information";
    }
    
    doc["timestamp"] = millis();
    sendJsonResponse(request, doc);
}

void ExternalAPI::handleSetAutoRestart(AsyncWebServerRequest* request) {
    JsonDocument requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (requestDoc["enabled"].isNull()) {
        sendErrorResponse(request, "Missing 'enabled' parameter", 400);
        return;
    }
    
    bool enabled = requestDoc["enabled"];
    bool success = veBusHandler->enableAutoRestart(enabled);
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["auto_restart_enabled"] = enabled;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set auto restart configuration";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleSetVoltageRange(AsyncWebServerRequest* request) {
    JsonDocument requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (requestDoc["min_voltage"].isNull() || requestDoc["max_voltage"].isNull()) {
        sendErrorResponse(request, "Missing 'min_voltage' or 'max_voltage' parameter", 400);
        return;
    }
    
    float minVoltage = requestDoc["min_voltage"];
    float maxVoltage = requestDoc["max_voltage"];
    
    if (minVoltage >= maxVoltage || minVoltage < 0 || maxVoltage > 300) {
        sendErrorResponse(request, "Invalid voltage range. min_voltage must be < max_voltage and within 0-300V", 400);
        return;
    }
    
    bool success = veBusHandler->setVoltageRange(minVoltage, maxVoltage);
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["min_voltage"] = minVoltage;
    responseDoc["max_voltage"] = maxVoltage;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set voltage range";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleSetFrequencyRange(AsyncWebServerRequest* request) {
    JsonDocument requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (requestDoc["min_frequency"].isNull() || requestDoc["max_frequency"].isNull()) {
        sendErrorResponse(request, "Missing 'min_frequency' or 'max_frequency' parameter", 400);
        return;
    }
    
    float minFreq = requestDoc["min_frequency"];
    float maxFreq = requestDoc["max_frequency"];
    
    if (minFreq >= maxFreq || minFreq < 40 || maxFreq > 70) {
        sendErrorResponse(request, "Invalid frequency range. min_frequency must be < max_frequency and within 40-70Hz", 400);
        return;
    }
    
    bool success = veBusHandler->setFrequencyRange(minFreq, maxFreq);
    
    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["min_frequency"] = minFreq;
    responseDoc["max_frequency"] = maxFreq;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set frequency range";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleGetStatistics(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    if (!veBusHandler->isInitialized()) {
        sendErrorResponse(request, "VE.Bus handler not initialized", 503);
        return;
    }
    
    VeBusStatistics stats = veBusHandler->getStatistics();
    
    doc["frames_sent"] = stats.framesSent;
    doc["frames_received"] = stats.framesReceived;
    doc["frames_dropped"] = stats.framesDropped;
    doc["checksum_errors"] = stats.checksumErrors;
    doc["timeout_errors"] = stats.timeoutErrors;
    doc["retransmissions"] = stats.retransmissions;
    doc["last_reset_time"] = stats.lastResetTime;
    doc["communication_quality"] = veBusHandler->getCommunicationQuality();
    doc["device_online"] = veBusHandler->isDeviceOnline();
    doc["last_communication"] = veBusHandler->getLastCommunicationTime();
    doc["timestamp"] = millis();
    
    sendJsonResponse(request, doc);
}

// Global instance - will be initialized in main.cpp
// ExternalAPI externalAPI(&server, &veBusHandler);

