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
    StaticJsonDocument<256> doc;
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

void ExternalAPI::handleGetStatus(AsyncWebServerRequest* request) {
    StaticJsonDocument<1024> doc;
    
    if (!veBusHandler->isInitialized()) {
        sendErrorResponse(request, "VE.Bus handler not initialized", 503);
        return;
    }
    
    // Get basic status
    doc["initialized"] = veBusHandler->isInitialized();
    doc["task_running"] = veBusHandler->isTaskRunning();
    doc["device_online"] = veBusHandler->isDeviceOnline();
    doc["communication_quality"] = veBusHandler->getCommunicationQuality();
    doc["last_communication"] = veBusHandler->getLastCommunicationTime();
    
    // Get device state
    VeBusDeviceState deviceState = veBusHandler->getDeviceState();
    doc["dc_voltage"] = deviceState.dcInfo.dcVoltage;
    doc["dc_current"] = deviceState.dcInfo.dcCurrent;
    doc["ac_voltage"] = deviceState.acInfo.acVoltage;
    doc["ac_frequency"] = deviceState.acInfo.acFrequency;
    doc["ac_power"] = deviceState.acInfo.acPower;
    doc["switch_state"] = deviceState.switchState;
    doc["device_status"] = deviceState.switchState;
    
    // Get detailed device status
    VeBusDeviceStatusInfo status;
    if (veBusHandler->requestDeviceStatus(status)) {
        doc["device_state"] = status.state;
        doc["device_mode"] = status.mode;
        doc["device_alarm"] = status.alarm;
        doc["device_warnings"] = status.warnings;
    }
    
    doc["api_version"] = "MK2-Extended-1.0";
    doc["timestamp"] = millis();
    
    sendJsonResponse(request, doc);
}

void ExternalAPI::handleGetVersion(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> doc;
    
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
    StaticJsonDocument<256> requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (!requestDoc.containsKey("state")) {
        sendErrorResponse(request, "Missing 'state' parameter", 400);
        return;
    }
    
    int state = requestDoc["state"];
    if (state < 1 || state > 4) {
        sendErrorResponse(request, "Invalid switch state. Valid values: 1=charger only, 2=inverter only, 3=on, 4=off", 400);
        return;
    }
    
    bool success = veBusHandler->setSwitchState((VeBusSwitchState)state);
    
    StaticJsonDocument<256> responseDoc;
    responseDoc["success"] = success;
    responseDoc["state"] = state;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set switch state";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleSetPower(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (!requestDoc.containsKey("power")) {
        sendErrorResponse(request, "Missing 'power' parameter", 400);
        return;
    }
    
    int16_t power = requestDoc["power"];
    bool success = veBusHandler->sendEssPowerCommand(power);
    
    StaticJsonDocument<256> responseDoc;
    responseDoc["success"] = success;
    responseDoc["power"] = power;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set ESS power";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleSetCurrent(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (!requestDoc.containsKey("current_limit")) {
        sendErrorResponse(request, "Missing 'current_limit' parameter", 400);
        return;
    }
    
    uint8_t currentLimit = requestDoc["current_limit"];
    bool success = veBusHandler->sendCurrentLimitCommand(currentLimit);
    
    StaticJsonDocument<256> responseDoc;
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
    
    StaticJsonDocument<256> responseDoc;
    responseDoc["success"] = success;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to reset device";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleClearErrors(AsyncWebServerRequest* request) {
    bool success = veBusHandler->clearErrors();
    
    StaticJsonDocument<256> responseDoc;
    responseDoc["success"] = success;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to clear errors";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleGetErrors(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> doc;
    
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
    StaticJsonDocument<512> doc;
    
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
    StaticJsonDocument<256> requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (!requestDoc.containsKey("enabled")) {
        sendErrorResponse(request, "Missing 'enabled' parameter", 400);
        return;
    }
    
    bool enabled = requestDoc["enabled"];
    bool success = veBusHandler->enableAutoRestart(enabled);
    
    StaticJsonDocument<256> responseDoc;
    responseDoc["success"] = success;
    responseDoc["auto_restart_enabled"] = enabled;
    responseDoc["timestamp"] = millis();
    
    if (!success) {
        responseDoc["error"] = "Failed to set auto restart configuration";
    }
    
    sendJsonResponse(request, responseDoc, success ? 200 : 500);
}

void ExternalAPI::handleSetVoltageRange(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (!requestDoc.containsKey("min_voltage") || !requestDoc.containsKey("max_voltage")) {
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
    
    StaticJsonDocument<256> responseDoc;
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
    StaticJsonDocument<512> requestDoc;
    
    if (!validateJsonRequest(request, requestDoc)) {
        sendErrorResponse(request, "Invalid JSON in request body", 400);
        return;
    }
    
    if (!requestDoc.containsKey("min_frequency") || !requestDoc.containsKey("max_frequency")) {
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
    
    StaticJsonDocument<256> responseDoc;
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
    StaticJsonDocument<1024> doc;
    
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
