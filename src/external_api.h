#ifndef EXTERNAL_API_H
#define EXTERNAL_API_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "vebus_handler.h"
#include "system_data.h"

/**
 * External API for Multiplus Control via HTTP REST endpoints
 * 
 * This header defines HTTP REST API endpoints for external control
 * of the Victron Multiplus device through the MK2 protocol.
 * 
 * Endpoints:
 * GET /api/vebus/status - Get complete device status
 * GET /api/vebus/version - Get firmware/protocol version info
 * POST /api/vebus/switch - Set switch state (on/off/charger only/inverter only)
 * POST /api/vebus/power - Set ESS power target
 * POST /api/vebus/current - Set input current limit
 * POST /api/vebus/reset - Reset device
 * POST /api/vebus/clear-errors - Clear error flags
 * GET /api/vebus/errors - Get error information
 * GET /api/vebus/warnings - Get warning information
 * POST /api/vebus/config/auto-restart - Enable/disable auto restart
 * POST /api/vebus/config/voltage-range - Set voltage range limits
 * POST /api/vebus/config/frequency-range - Set frequency range limits
 */

class ExternalAPI {
private:
    AsyncWebServer* server;
    VeBusHandler* veBusHandler;
    
    // Helper methods
    void sendJsonResponse(AsyncWebServerRequest* request, const JsonDocument& doc, int statusCode = 200);
    void sendErrorResponse(AsyncWebServerRequest* request, const char* message, int statusCode = 400);
    bool validateJsonRequest(AsyncWebServerRequest* request, JsonDocument& doc);
    
public:
    ExternalAPI(AsyncWebServer* webServer, VeBusHandler* veBus);
    void setup();
    
    // API endpoint handlers
    void handleGetGeneralStatus(AsyncWebServerRequest* request);  // Simple status without hardware
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetVersion(AsyncWebServerRequest* request);
    void handleSetSwitch(AsyncWebServerRequest* request);
    void handleSetPower(AsyncWebServerRequest* request);
    void handleSetCurrent(AsyncWebServerRequest* request);
    void handleReset(AsyncWebServerRequest* request);
    void handleClearErrors(AsyncWebServerRequest* request);
    void handleGetErrors(AsyncWebServerRequest* request);
    void handleGetWarnings(AsyncWebServerRequest* request);
    void handleSetAutoRestart(AsyncWebServerRequest* request);
    void handleSetVoltageRange(AsyncWebServerRequest* request);
    void handleSetFrequencyRange(AsyncWebServerRequest* request);
    void handleGetStatistics(AsyncWebServerRequest* request);
};

// Global instance declaration
extern ExternalAPI externalAPI;

#endif // EXTERNAL_API_H
