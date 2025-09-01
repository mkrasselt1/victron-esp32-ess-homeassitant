/*
 * MQTT Handler Implementation (Memory Optimized)
 */

#include "mqtt_handler.h"

// Static instance for callback
MQTTHandler* MQTTHandler::instance = nullptr;

MQTTHandler::MQTTHandler() : 
    mqttClient(wifiClient),
    mqttPort(1883),
    deviceId("victron-esp32-ess"),
    baseTopic("victron-esp32"),
    lastReconnectAttempt(0),
    lastPublish(0) {
    
    instance = this;
}

void MQTTHandler::begin() {
    loadConfig();
    
    mqttClient.setCallback(staticMqttCallback);
    mqttClient.setBufferSize(512); // Smaller buffer to save memory
    
    Serial.println("MQTT Handler initialized (optimized)");
    Serial.printf("Base Topic: %s\n", baseTopic.c_str());
}

void MQTTHandler::setCredentials(const String& server, int port, const String& username, const String& password) {
    mqttServer = server;
    mqttPort = port;
    mqttUsername = username;
    mqttPassword = password;
    
    saveConfig();
    
    mqttClient.setServer(server.c_str(), port);
    Serial.printf("MQTT Server configured: %s:%d\n", server.c_str(), port);
}

void MQTTHandler::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            reconnect();
        }
    } else {
        mqttClient.loop();
    }
}

bool MQTTHandler::isConnected() {
    return mqttClient.connected();
}

void MQTTHandler::reconnect() {
    if (mqttServer.length() == 0) {
        return; // No server configured
    }
    
    Serial.print("Attempting MQTT connection...");
    
    String clientId = deviceId + "-" + String(random(0xffff), HEX);
    String willTopic = baseTopic + "/status";
    
    bool connected;
    if (mqttUsername.length() > 0) {
        connected = mqttClient.connect(clientId.c_str(), 
                                     mqttUsername.c_str(), 
                                     mqttPassword.c_str(),
                                     willTopic.c_str(), 0, true, "offline");
    } else {
        connected = mqttClient.connect(clientId.c_str(),
                                     willTopic.c_str(), 0, true, "offline");
    }
    
    if (connected) {
        Serial.println(" connected");
        
        // Publish online status
        publishValue("status", "online");
        
        // Setup subscriptions
        setupSubscriptions();
    } else {
        Serial.printf(" failed, rc=%d\n", mqttClient.state());
    }
}

void MQTTHandler::publishSystemData(const SystemData& data) {
    if (!mqttClient.connected()) return;
    
    unsigned long now = millis();
    if (now - lastPublish < PUBLISH_INTERVAL) return;
    lastPublish = now;
    
    // Publish key battery data only
    publishValue("battery/soc", String(data.battery.soc));
    publishValue("battery/voltage", String(data.battery.voltage, 1));
    publishValue("battery/power", String(data.battery.power));
    
    // Publish key MultiPlus data only
    publishValue("multiplus/power", String(data.multiplus.esspower));
    publishValue("multiplus/ac_voltage", String(data.multiplus.uMainsRMS, 0));
}

void MQTTHandler::publishFeedInControl(bool enabled, float target, float max, float current) {
    if (!mqttClient.connected()) return;
    
    publishValue("feedin/enabled", enabled ? "ON" : "OFF");
    publishValue("feedin/target", String(target, 0));
    publishValue("feedin/max", String(max, 0));
    publishValue("feedin/current", String(current, 0));
}

void MQTTHandler::setFeedInControlCallback(std::function<void(bool enabled, float target, float max)> callback) {
    feedInControlCallback = callback;
}

void MQTTHandler::saveConfig() {
    preferences.begin("mqtt", false);
    preferences.putString("server", mqttServer);
    preferences.putInt("port", mqttPort);
    preferences.putString("username", mqttUsername);
    preferences.putString("password", mqttPassword);
    preferences.putString("device_id", deviceId);
    preferences.end();
}

void MQTTHandler::loadConfig() {
    preferences.begin("mqtt", true);
    mqttServer = preferences.getString("server", "");
    mqttPort = preferences.getInt("port", 1883);
    mqttUsername = preferences.getString("username", "");
    mqttPassword = preferences.getString("password", "");
    deviceId = preferences.getString("device_id", "victron-esp32-ess");
    preferences.end();
    
    baseTopic = deviceId;
}

void MQTTHandler::staticMqttCallback(char* topic, byte* payload, unsigned int length) {
    if (instance) {
        instance->onMqttMessage(topic, payload, length);
    }
}

void MQTTHandler::onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String payloadStr;
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }
    
    Serial.printf("MQTT: %s = %s\n", topic, payloadStr.c_str());
    
    // Feed-in control commands (simplified)
    if (topicStr.endsWith("/feedin/enabled/set") && feedInControlCallback) {
        bool enabled = (payloadStr == "ON");
        feedInControlCallback(enabled, -1, -1); // -1 means keep current value
    }
    else if (topicStr.endsWith("/feedin/target/set") && feedInControlCallback) {
        float target = payloadStr.toFloat();
        feedInControlCallback(false, target, -1); // Use false as placeholder
    }
    else if (topicStr.endsWith("/feedin/max/set") && feedInControlCallback) {
        float max = payloadStr.toFloat();
        feedInControlCallback(false, -1, max); // Use false as placeholder
    }
}

void MQTTHandler::setupSubscriptions() {
    // Subscribe to command topics
    String cmdTopic = baseTopic + "/feedin/enabled/set";
    mqttClient.subscribe(cmdTopic.c_str());
    
    cmdTopic = baseTopic + "/feedin/target/set";
    mqttClient.subscribe(cmdTopic.c_str());
    
    cmdTopic = baseTopic + "/feedin/max/set";
    mqttClient.subscribe(cmdTopic.c_str());
    
    Serial.println("MQTT subscriptions setup");
}

void MQTTHandler::publishValue(const String& topic, const String& value) {
    if (!mqttClient.connected()) return;
    
    String fullTopic = baseTopic + "/" + topic;
    mqttClient.publish(fullTopic.c_str(), value.c_str());
}
