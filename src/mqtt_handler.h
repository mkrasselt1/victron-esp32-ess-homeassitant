/*
 * MQTT Handler for ESP32 ESS Controller (Memory Optimized)
 * 
 * Features:
 * - Basic MQTT publishing for sensors
 * - Feed-in power control via MQTT
 * - Minimal memory footprint
 * 
 * SPDX-FileCopyrightText: © 2023 PV Baxi <pv-baxi@gmx.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "system_data.h"

class MQTTHandler {
public:
    MQTTHandler();
    
    // Initialization and configuration
    void begin();
    void setCredentials(const String& server, int port, const String& username = "", const String& password = "");
    
    // Connection management
    void loop();
    bool isConnected();
    void reconnect();
    
    // Publishing (simplified)
    void publishSystemData(const SystemData& data);
    void publishFeedInControl(bool enabled, float target, float max, float current);
    
    // Feed-in control callbacks
    void setFeedInControlCallback(std::function<void(bool enabled, float target, float max)> callback);
    
    // Configuration
    void saveConfig();
    void loadConfig();
    
    // Public configuration access
    String mqttServer;
    int mqttPort;
    String mqttUsername;
    String mqttPassword;
    String deviceId;

private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    Preferences preferences;
    
    // Device configuration
    String baseTopic;
    
    // Timing
    unsigned long lastReconnectAttempt;
    unsigned long lastPublish;
    const unsigned long RECONNECT_INTERVAL = 5000;   // 5 seconds
    const unsigned long PUBLISH_INTERVAL = 2000;     // 2 seconds (less frequent)
    
    // Callbacks
    std::function<void(bool enabled, float target, float max)> feedInControlCallback;
    
    // Internal methods
    void onMqttMessage(char* topic, byte* payload, unsigned int length);
    static void staticMqttCallback(char* topic, byte* payload, unsigned int length);
    void setupSubscriptions();
    void publishValue(const String& topic, const String& value);
    
    // Static instance for callback
    static MQTTHandler* instance;
};
