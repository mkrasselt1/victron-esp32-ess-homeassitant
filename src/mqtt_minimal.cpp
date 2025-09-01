#include "mqtt_minimal.h"

MQTTMinimal* mqttInstance = nullptr;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (mqttInstance) {
        mqttInstance->onMessage(topic, payload, length);
    }
}

MQTTMinimal::MQTTMinimal() : client(wifiClient), lastReconnect(0), mqttPort(1883) {
    mqttInstance = this;
    client.setCallback(mqttCallback);
}

void MQTTMinimal::begin(const String& server, int port) {
    mqttServer = server;
    mqttPort = port;
    client.setServer(server.c_str(), port);
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

void MQTTMinimal::publish(const String& topic, const String& value) {
    if (isConnected()) {
        client.publish(topic.c_str(), value.c_str());
    }
}

void MQTTMinimal::setCallback(std::function<void(String topic, String payload)> callback) {
    messageCallback = callback;
}

void MQTTMinimal::connect() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    String clientId = "ESP32ESS-" + String(WiFi.macAddress());
    if (client.connect(clientId.c_str())) {
        client.subscribe("ess/feedin/+");
    }
}

void MQTTMinimal::onMessage(char* topic, byte* payload, unsigned int length) {
    if (messageCallback) {
        String topicStr = String(topic);
        String payloadStr = "";
        for (int i = 0; i < length; i++) {
            payloadStr += (char)payload[i];
        }
        messageCallback(topicStr, payloadStr);
    }
}
