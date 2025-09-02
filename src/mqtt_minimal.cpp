#include "mqtt_minimal.h"
#include <string.h>

MQTTMinimal* mqttInstance = nullptr;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (mqttInstance) {
        mqttInstance->onMessage(topic, payload, length);
    }
}

MQTTMinimal::MQTTMinimal() : client(wifiClient), lastReconnect(0), mqttPort(1883) {
    mqttInstance = this;
    client.setCallback(mqttCallback);
    strcpy(mqttServer, "192.168.30.1"); // Default
    mqttUsername[0] = '\0';
    mqttPassword[0] = '\0';
    payloadBuffer[0] = '\0';
}

void MQTTMinimal::begin(const char* server, int port, const char* username, const char* password) {
    strncpy(mqttServer, server, sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';
    
    mqttPort = port;
    
    strncpy(mqttUsername, username, sizeof(mqttUsername) - 1);
    mqttUsername[sizeof(mqttUsername) - 1] = '\0';
    
    strncpy(mqttPassword, password, sizeof(mqttPassword) - 1);
    mqttPassword[sizeof(mqttPassword) - 1] = '\0';
    
    client.setServer(server, port);
}

void MQTTMinimal::loop() {
    if (!client.connected()) {
        if (millis() - lastReconnect > 5000) {
            connect();
            lastReconnect = millis();
        }
    } else {
        client.loop();
    }
}

bool MQTTMinimal::isConnected() {
    return client.connected();
}

void MQTTMinimal::publish(const char* topic, const char* value) {
    if (isConnected()) {
        client.publish(topic, value);
    }
}

void MQTTMinimal::publishDebug(const char* message) {
    if (isConnected()) {
        client.publish("esp32victron/debug/vebus", message);
    }
}

void MQTTMinimal::setCallback(std::function<void(const char* topic, const char* payload)> callback) {
    messageCallback = callback;
}

void MQTTMinimal::connect() {
    if (WiFi.status() != WL_CONNECTED || strlen(mqttServer) == 0) return;
    
    const char* clientId = "ESP32ESS";
    bool connected = false;
    
    if (strlen(mqttUsername) > 0) {
        connected = client.connect(clientId, mqttUsername, mqttPassword);
    } else {
        connected = client.connect(clientId);
    }
    
    if (connected) {
        client.subscribe("ess/feedin/+");
    }
}

void MQTTMinimal::onMessage(char* topic, byte* payload, unsigned int length) {
    if (messageCallback) {
        // Null-terminate payload in buffer
        if (length < sizeof(payloadBuffer)) {
            memcpy(payloadBuffer, payload, length);
            payloadBuffer[length] = '\0';
            messageCallback(topic, payloadBuffer);
        }
    }
}
