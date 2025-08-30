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
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include "system_data.h"
#include "vebus_handler.h"
#include "status_led.h"
#include "pylontech_can.h"
#include "external_api.h"

// Global objects
VeBusHandler veBusHandler;
StatusLED statusLED;
PylontechCAN pylontechCAN;
AsyncWebServer webServer(80);
ExternalAPI externalAPI(&webServer, &veBusHandler);
SystemData systemData;
WiFiManager wm;

// Timer and timing variables
hw_timer_t* timer = nullptr;
volatile bool timerFlag = false;
unsigned long lastStatusUpdate = 0;
unsigned long lastLedUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 1000;  // 1 second
const unsigned long LED_UPDATE_INTERVAL = 50;       // 50ms

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
  static unsigned long wifiCheckTime = 0;
  
  // Check WiFi status every 5 seconds
  if (millis() - wifiCheckTime > 5000) {
    wifiCheckTime = millis();
    
    if (!WiFi.isConnected()) {
      statusLED.setWiFiConnecting();
    } else {
      statusLED.setNormalOperation();
    }
  }
}

void setupWiFiManager() {
  // Set custom parameters for WiFiManager
  wm.setConfigPortalTimeout(300); // 5 minutes timeout
  wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  
  // Set status LED to connecting
  statusLED.setWiFiConnecting();
  
  // Try to connect to WiFi
  if (!wm.autoConnect("VictronESS-Config", "12345678")) {
    Serial.println("Failed to connect to WiFi - restarting");
    statusLED.setErrorMode();
    ESP.restart();
  }
  
  Serial.println("WiFi connected successfully!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  statusLED.setWiFiConnected();
}

void setupOTA() {
  // ArduinoOTA for PlatformIO remote upload
  ArduinoOTA.begin(WiFi.localIP(), "victron-esp32-ess", "victron123", InternalStorage);
  
  Serial.println("ArduinoOTA Ready");
  Serial.println("Hostname: victron-esp32-ess");
  Serial.println("Port: 3232");
  Serial.println("Password: victron123");
  Serial.println("IP: " + WiFi.localIP().toString());

  // Web-based OTA update (additional method)
  webServer.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><body>";
    html += "<h1>Victron ESS ESP32 - OTA Update</h1>";
    html += "<h2>Web Upload</h2>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update' accept='.bin'>";
    html += "<input type='submit' value='Update'>";
    html += "</form>";
    html += "<h2>PlatformIO OTA</h2>";
    html += "<p>Hostname: victron-esp32-ess</p>";
    html += "<p>Port: 3232</p>";
    html += "<p>Password: victron123</p>";
    html += "<p>Command: <code>pio run -t upload --upload-port " + WiFi.localIP().toString() + "</code></p>";
    html += "<p>Aktuelle Version: 1.0.0</p>";
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    html += "</body></html>";
    request->send(200, "text/html", html);
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
  
  // Add info endpoint
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><body>";
    html += "<h1>Victron ESS ESP32 Controller</h1>";
    html += "<p><a href='/update'>OTA Update</a></p>";
    html += "<p><a href='/api/status'>API Status</a></p>";
    html += "<p>WiFi: " + String(WiFi.isConnected() ? "Connected" : "Disconnected") + "</p>";
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    html += "<p>Batteriezustand: " + String(systemData.battery.soc) + "%</p>";
    html += "<p>Batterieleistung: " + String(systemData.battery.power) + "W</p>";
    html += "<p>CAN Status: " + String(pylontechCAN.isBatteryOnline() ? "Online" : "Offline") + "</p>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });
  
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
  systemData.battery.soc = 50;
  systemData.battery.temperature = 25.0;
  
  systemData.multiplus.dcVoltage = 48.0;
  systemData.multiplus.dcCurrent = 0.0;
  systemData.multiplus.temp = 25.0;
  systemData.multiplus.acFrequency = 50.0;
  systemData.multiplus.uMainsRMS = 230.0;
  
  // Initialize StatusLED
  statusLED.begin();
  statusLED.setBootMode();
  
  // Setup WiFi with WiFiManager
  setupWiFiManager();
  
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
    
    // Log current status
    Serial.printf("Battery: %.1fV, %.1fA, %dW, SOC:%d%% | ", 
                  systemData.battery.voltage, 
                  systemData.battery.current,
                  systemData.battery.power,
                  systemData.battery.soc);
    Serial.printf("CAN: %s, VE.Bus: %s | ",
                  pylontechCAN.isBatteryOnline() ? "Online" : "Offline",
                  veBusHandler.isTaskRunning() ? "Running" : "Stopped");
    Serial.printf("WiFi: %s\n", WiFi.isConnected() ? "Connected" : "Disconnected");
  }
  
  // Small delay to prevent watchdog issues
  delay(1);
}