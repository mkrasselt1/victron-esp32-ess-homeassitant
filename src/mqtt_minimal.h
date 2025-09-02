#pragma once

#include <WiFi.h>
#include <PubSubClient.h>

class MQTTMinimal {
public:
    MQTTMinimal();
    
    void begin(const char* server, int port, const char* username = "", const char* password = "");
    void loop();
    bool isConnected();
    void publish(const char* topic, const char* value);
    void publishDebug(const char* message);
    void setCallback(std::function<void(const char* topic, const char* payload)> callback);
    void onMessage(char* topic, byte* payload, unsigned int length);
    
    char mqttServer[64];
    int mqttPort;
    char mqttUsername[32];
    char mqttPassword[32];
    
private:
    WiFiClient wifiClient;
    PubSubClient client;
    std::function<void(const char* topic, const char* payload)> messageCallback;
    unsigned long lastReconnect;
    char payloadBuffer[128];
    
    void connect();
};
