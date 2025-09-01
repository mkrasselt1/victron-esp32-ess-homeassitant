/*
 * Main Application File for ESP32 ESS Controller
 * 
 * Features:
 * - StatusLED control with power flow visualization (WS2812 on GPIO 4)
 * - WiFiManager for easy WiFi configuration
 * - OTA updates over WiFi
 * - VE.Bus communication in separate task
 * - Pylontech CAN communication in separate task
 * - Web server for status and control
 * 
 * SPDX-FileCopyrightText: © 2023 PV Baxi <pv-baxi@gmx.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPIFFS.h>
#include "system_data.h"
#include "vebus_handler.h"
#include "status_led.h"
#include "pylontech_can.h"
#include "external_api.h"
#include "wifi_provisioning.h"

// Global objects
VeBusHandler veBusHandler;
StatusLED statusLED;
PylontechCAN pylontechCAN;
AsyncWebServer webServer(80);
AsyncWebSocket ws("/ws");
ExternalAPI externalAPI(&webServer, &veBusHandler);
SystemData systemData;

// Timer and timing variables
hw_timer_t* timer = nullptr;
volatile bool timerFlag = false;
unsigned long lastStatusUpdate = 0;
unsigned long lastLedUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 1000;  // 1 second
const unsigned long LED_UPDATE_INTERVAL = 50;       // 50ms

// Feed-in power control
float targetFeedInPower = 0.0;  // Target feed-in power in watts
float maxFeedInPower = 5000.0;  // Maximum allowed feed-in power in watts
bool feedInControlEnabled = false;  // Enable/disable feed-in control

// Function prototypes
String createBatteryJson();
String createMultiplusJson();
String createLedJson();
void updateStatusLED();
void processTimerEvents();
void setupWebServer();
void setupOTA();
void setupWiFiManager();
void onTimer();

// JSON creation functions
String createBatteryJson() {
  JsonDocument doc;
  
  doc["voltage"] = systemData.battery.voltage;
  doc["current"] = systemData.battery.current;
  doc["power"] = systemData.battery.power;
  doc["soc"] = systemData.battery.soc;
  doc["socMax"] = systemData.battery.socMax;
  doc["temperature"] = systemData.battery.temperature;
  doc["chargeCurrentLimit"] = systemData.battery.chargeCurrentLimit;
  doc["dischargeCurrentLimit"] = systemData.battery.dischargeCurrentLimit;
  doc["warningFlags1"] = systemData.battery.warningFlags1;
  doc["warningFlags2"] = systemData.battery.warningFlags2;
  doc["protectionFlags1"] = systemData.battery.protectionFlags1;
  doc["protectionFlags2"] = systemData.battery.protectionFlags2;
  
  String output;
  serializeJson(doc, output);
  return output;
}

String createMultiplusJson() {
  JsonDocument doc;
  
  doc["acPhase"] = systemData.multiplus.acPhase;
  doc["dcVoltage"] = systemData.multiplus.dcVoltage;
  doc["dcCurrent"] = systemData.multiplus.dcCurrent;
  doc["esspower"] = systemData.multiplus.esspower;
  doc["acFrequency"] = systemData.multiplus.acFrequency;
  doc["uMainsRMS"] = systemData.multiplus.uMainsRMS;
  doc["temp"] = systemData.multiplus.temp;
  doc["status80"] = systemData.multiplus.status80;
  doc["voltageStatus"] = systemData.multiplus.voltageStatus;
  doc["emergencyPowerStatus"] = systemData.multiplus.emergencyPowerStatus;
  
  String output;
  serializeJson(doc, output);
  return output;
}

String createLedJson() {
  JsonDocument doc;
  
  doc["enabled"] = true;
  doc["brightness"] = 50; // Default brightness
  doc["powerFlow"] = systemData.battery.power;
  doc["mode"] = (int)statusLED.getCurrentMode();
  doc["direction"] = (int)statusLED.getCurrentDirection();
  
  String output;
  serializeJson(doc, output);
  return output;
}

String createFullStatusJson() {
  JsonDocument doc;
  
  // Battery data
  doc["battery_soc"] = systemData.battery.soc;
  doc["battery_voltage"] = systemData.battery.voltage;
  doc["battery_current"] = systemData.battery.current;
  doc["battery_power"] = systemData.battery.power;
  doc["battery_temperature"] = systemData.battery.temperature;
  doc["battery_soh"] = systemData.battery.soh;
  doc["battery_chargeVoltage"] = systemData.battery.chargeVoltage;
  doc["battery_chargeCurrentLimit"] = systemData.battery.chargeCurrentLimit;
  doc["battery_dischargeCurrentLimit"] = systemData.battery.dischargeCurrentLimit;
  doc["battery_dischargeVoltage"] = systemData.battery.dischargeVoltage;
  doc["battery_manufacturer"] = systemData.battery.manufacturer;
  doc["battery_nrPacksInParallel"] = systemData.battery.nrPacksInParallel;
  doc["battery_protectionFlags1"] = systemData.battery.protectionFlags1;
  doc["battery_protectionFlags2"] = systemData.battery.protectionFlags2;
  doc["battery_warningFlags1"] = systemData.battery.warningFlags1;
  doc["battery_warningFlags2"] = systemData.battery.warningFlags2;
  doc["battery_requestFlags"] = systemData.battery.requestFlags;
  
  // MultiPlus data
  doc["multiplusDcVoltage"] = systemData.multiplus.dcVoltage;
  doc["multiplusDcCurrent"] = systemData.multiplus.dcCurrent;
  doc["multiplusESSpower"] = systemData.multiplus.esspower;
  doc["multiplusAcFrequency"] = systemData.multiplus.acFrequency;
  doc["multiplusUMainsRMS"] = systemData.multiplus.uMainsRMS;
  doc["multiplusTemp"] = systemData.multiplus.temp;
  doc["multiplusStatus80"] = systemData.multiplus.status80;
  doc["multiplusVoltageStatus"] = systemData.multiplus.voltageStatus;
  doc["multiplusEmergencyPowerStatus"] = systemData.multiplus.emergencyPowerStatus;
  doc["multiplusPinverterFiltered"] = systemData.multiplus.esspower; // Approximation
  doc["multiplusPmainsFiltered"] = 0; // Not available
  doc["multiplusPowerFactor"] = 1.0; // Default
  doc["masterMultiLED_ActualInputCurrentLimit"] = 16.0; // Default
  
  // VE.Bus communication stats (simplified for available methods)
  doc["veBus_isOnline"] = veBusHandler.isTaskRunning();
  doc["veBus_communicationQuality"] = veBusHandler.getCommunicationQuality();
  doc["veBus_framesSent"] = 0; // Not available
  doc["veBus_framesReceived"] = 0; // Not available  
  doc["veBus_checksumErrors"] = 0; // Not available
  doc["veBus_timeoutErrors"] = 0; // Not available
  
  // Status LED data
  doc["statusLED_mode"] = (int)statusLED.getCurrentMode();
  doc["statusLED_direction"] = (int)statusLED.getCurrentDirection();
  doc["statusLED_initialized"] = true;
  
  // System status
  doc["switchMode"] = "Auto";
  doc["essPowerStrategy"] = "Optimized";
  doc["minimumFeedIn"] = 0;
  doc["averageControlDeviationFeedIn"] = 0;
  doc["averageChargingPower"] = 0;
  doc["bmsPowerAverage"] = systemData.battery.power;
  doc["powerTrendConsumption"] = 0;
  doc["powerTrendFeedIn"] = 0;
  doc["secondsInMinStrategy"] = 0;
  doc["secondsInMaxStrategy"] = 0;
  
  // Feed-in power control
  doc["feedInControl_enabled"] = feedInControlEnabled;
  doc["feedInControl_target"] = targetFeedInPower;
  doc["feedInControl_max"] = maxFeedInPower;
  doc["feedInControl_current"] = systemData.multiplus.esspower; // Current AC power output
  
  String output;
  serializeJson(doc, output);
  return output;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    // Send initial data to new client
    client->text(createFullStatusJson());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
}

// Main processing functions
void updateStatusLED() {
  // Update LED based on current power flow
  if (systemData.battery.power > 100) {
    // Positive power = charging
    statusLED.updatePowerFlow(systemData.battery.power);
  } else if (systemData.battery.power < -100) {
    // Negative power = discharging
    statusLED.updatePowerFlow(systemData.battery.power);
  } else {
    // Low power = idle
    statusLED.updatePowerFlow(0);
  }
}

void processTimerEvents() {
  // WiFi provisioning is handled in its own loop
}

void setupWiFiConnection() {
  // Set status LED to connecting
  statusLED.setWiFiConnecting();
  
  // Start WiFi provisioning
  Serial.println("Starting WiFi provisioning...");
  Serial.println("Use serial commands or connect to 'ESP32-Setup' AP");
  wifiProvisioning.printCommands();
  
  if (wifiProvisioning.begin()) {
    Serial.println("WiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    statusLED.setWiFiConnected();
  } else {
    Serial.println("WiFi setup mode active");
    statusLED.setWiFiConnecting();
  }
}

void setupOTA() {
  // ArduinoOTA for PlatformIO remote upload
  ArduinoOTA.onStart([]() {
    String type;
    // The OTA type is not available via getCommand(), so just print "sketch" for U_FLASH and "filesystem" for U_SPIFFS
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();
  
  Serial.println("ArduinoOTA Ready");
  Serial.println("Hostname: victron-esp32-ess");
  Serial.println("Port: 3232");
  Serial.println("Password: victron123");
  Serial.println("IP: " + WiFi.localIP().toString());

  // Web-based OTA update (additional method)
  webServer.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    // Use static strings to save DRAM
    static const char html_start[] PROGMEM = "<html><body><h1>Victron ESS ESP32 - OTA Update</h1><h2>Web Upload</h2><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update' accept='.bin'><input type='submit' value='Update'></form><h2>PlatformIO OTA</h2><p>Hostname: victron-esp32-ess</p><p>Port: 3232</p><p>Password: victron123</p><p>Command: <code>pio run -t upload --upload-port ";
    static const char html_end[] PROGMEM = "</code></p><p>Aktuelle Version: 1.0.0</p><p>IP: ";
    static const char html_close[] PROGMEM = "</p></body></html>";
    
    // Build response with minimal string operations
    String response;
    response.reserve(512); // Pre-allocate to reduce fragmentation
    response += FPSTR(html_start);
    response += WiFi.localIP().toString();
    response += FPSTR(html_end);
    response += WiFi.localIP().toString();
    response += FPSTR(html_close);
    
    request->send(200, "text/html", response);
  });

  webServer.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    bool shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
    if(shouldReboot) ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      statusLED.setBootMode();
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %uB\n", index+len);
        statusLED.setWiFiConnected();
      } else {
        Update.printError(Serial);
        statusLED.setErrorMode();
      }
    }
  });
  
  Serial.println("Web OTA Ready at http://" + WiFi.localIP().toString() + "/update");
}

void setupWebServer() {
  // Setup external API endpoints
  externalAPI.setup();
  
  // Setup OTA update endpoint
  setupOTA();
  
  // Initialize SPIFFS for serving files
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }
  
  // Serve static files from SPIFFS
  webServer.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  
  // Feed-in power control endpoint
  webServer.on("/api/feedin", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("enabled", true)) {
      feedInControlEnabled = request->getParam("enabled", true)->value() == "true";
    }
    if (request->hasParam("target", true)) {
      targetFeedInPower = request->getParam("target", true)->value().toFloat();
      // Clamp to reasonable limits
      if (targetFeedInPower < 0) targetFeedInPower = 0;
      if (targetFeedInPower > maxFeedInPower) targetFeedInPower = maxFeedInPower;
    }
    if (request->hasParam("max", true)) {
      maxFeedInPower = request->getParam("max", true)->value().toFloat();
      // Ensure reasonable limits
      if (maxFeedInPower < 100) maxFeedInPower = 100;
      if (maxFeedInPower > 10000) maxFeedInPower = 10000;
    }
    
    // Send response with current settings
    JsonDocument response;
    response["enabled"] = feedInControlEnabled;
    response["target"] = targetFeedInPower;
    response["max"] = maxFeedInPower;
    response["current"] = systemData.multiplus.esspower;
    
    String output;
    serializeJson(response, output);
    request->send(200, "application/json", output);
    
    Serial.printf("Feed-in control updated: enabled=%s, target=%.1fW, max=%.1fW\n", 
                  feedInControlEnabled ? "true" : "false", targetFeedInPower, maxFeedInPower);
  });
  
  // Fallback endpoint if SPIFFS file not found
  webServer.onNotFound([](AsyncWebServerRequest *request){
    if (request->url() == "/") {
      // Use static strings to save DRAM as fallback
      static const char html_start[] PROGMEM = "<html><body><h1>Victron ESS ESP32 Controller</h1><p><a href='/update'>OTA Update</a></p><p><a href='/api/status'>API Status</a></p><p>WiFi: ";
      static const char html_ip[] PROGMEM = "</p><p>IP: ";
      static const char html_battery[] PROGMEM = "</p><p>Batteriezustand: ";
      static const char html_power[] PROGMEM = "%</p><p>Batterieleistung: ";
      static const char html_can[] PROGMEM = "W</p><p>CAN Status: ";
      static const char html_end[] PROGMEM = "</p><p><em>Note: SPIFFS not available, using fallback HTML</em></p></body></html>";
      
      // Build response with minimal string operations
      String response;
      response.reserve(512); // Pre-allocate to reduce fragmentation
      response += FPSTR(html_start);
      response += WiFi.isConnected() ? "Connected" : "Disconnected";
      response += FPSTR(html_ip);
      response += WiFi.localIP().toString();
      response += FPSTR(html_battery);
      response += systemData.battery.soc;
      response += FPSTR(html_power);
      response += systemData.battery.power;
      response += FPSTR(html_can);
      response += pylontechCAN.isBatteryOnline() ? "Online" : "Offline";
      response += FPSTR(html_end);
      
      request->send(200, "text/html", response);
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });
  
  // Setup WebSocket
  ws.onEvent(onWsEvent);
  webServer.addHandler(&ws);
  
  // Start the web server
  webServer.begin();
  
  Serial.println("Web server started");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void onTimer() {
  timerFlag = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nVictron ESS Controller Starting...");
  
  // Initialize system data with default values
  systemData.battery.voltage = 48.0;
  systemData.battery.current = 0.0;
  systemData.battery.power = 0;
  systemData.battery.soc = 0;
  systemData.battery.temperature = 25.0;
  
  systemData.multiplus.dcVoltage = 48.0;
  systemData.multiplus.dcCurrent = 0.0;
  systemData.multiplus.temp = 25.0;
  systemData.multiplus.acFrequency = 50.0;
  systemData.multiplus.uMainsRMS = 230.0;
  
  // Initialize StatusLED
  statusLED.begin();
  statusLED.setBootMode();
  
  // Setup WiFi connection
  setupWiFiConnection();
  
  // Setup OTA updates
  setupOTA();
  
  // Setup web server
  setupWebServer();
  
  // Initialize VE.Bus communication (separate task)
  if (!veBusHandler.begin()) {
    Serial.println("VE.Bus initialization failed");
    statusLED.setErrorMode();
  } else {
    Serial.println("VE.Bus communication started");
  }
  
  // Initialize Pylontech CAN communication (separate task)
  if (!pylontechCAN.begin()) {
    Serial.println("Pylontech CAN initialization failed");
    // Don't set error mode - CAN is optional
  } else {
    Serial.println("Pylontech CAN communication started");
  }
  
  // Setup timer for regular updates
  timer = timerBegin(0, 80, true);  // Timer 0, divider 80 (1MHz), count up
  timerAttachInterrupt(timer, &onTimer, true);  // Attach interrupt on edge
  timerAlarmWrite(timer, 100000, true);  // 100ms interval, auto-reload
  timerAlarmEnable(timer);
  
  Serial.println("Victron ESS Controller initialized successfully");
  Serial.println("==============================================");
  Serial.println("WiFi Status: " + String(WiFi.isConnected() ? "Connected" : "Disconnected"));
  Serial.println("IP Address: " + WiFi.localIP().toString());
  Serial.println("Web Interface: http://" + WiFi.localIP().toString());
  Serial.println("OTA Update: http://" + WiFi.localIP().toString() + "/update");
  Serial.println("VE.Bus Task: " + String(veBusHandler.isTaskRunning() ? "Running" : "Stopped"));
  Serial.println("CAN Task: " + String(pylontechCAN.isTaskRunning() ? "Running" : "Stopped"));
  Serial.println("==============================================");
}

void loop() {
  // Handle WiFi provisioning
  wifiProvisioning.loop();
  
  // Only run main application if WiFi is connected
  if (wifiProvisioning.isConnected()) {
    ArduinoOTA.handle();
    
    unsigned long currentTime = millis();
    
    // Handle timer events
    if (timerFlag) {
      timerFlag = false;
      processTimerEvents();
    }
    
    // Update status LED
    if (currentTime - lastLedUpdate >= LED_UPDATE_INTERVAL) {
      lastLedUpdate = currentTime;
      statusLED.update();
      updateStatusLED();
    }
    
    // Update system status
    if (currentTime - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
      lastStatusUpdate = currentTime;
      
      // Send WebSocket update to all connected clients
      String wsData = createFullStatusJson();
      ws.textAll(wsData);
      
      // Log current status
      Serial.printf("Battery: %.1fV, %.1fA, %dW, SOC:%d%% | ", 
                    systemData.battery.voltage, 
                    systemData.battery.current,
                    systemData.battery.power,
                    systemData.battery.soc);
      Serial.printf("CAN: %s, VE.Bus: %s | ",
                    pylontechCAN.isBatteryOnline() ? "Online" : "Offline",
                    veBusHandler.isTaskRunning() ? "Running" : "Stopped");
      Serial.printf("WiFi: %s\r\n", WiFi.isConnected() ? "Connected" : "Disconnected");
    }
    
    // Clean up WebSocket connections
    ws.cleanupClients();
  } else {
    // WiFi setup mode - just blink LED
    statusLED.update();
  }
  
  // Small delay to prevent watchdog issues
  delay(1);
}