#pragma once

#include <WiFi.h>
#include <PubSubClient.h>

class MQTTMinimal {
public:
    MQTTMinimal();
    
    void begin(const String& server, int port, const String& username = "", const String& password = "");
    void loop();
    bool isConnected();
    void publish(const String& topic, const String& value);
    void setCallback(std::function<void(String topic, String payload)> callback);
    void onMessage(char* topic, byte* payload, unsigned int length);
    
    String mqttServer;
    int mqttPort;
    
private:
    WiFiClient wifiClient;
    PubSubClient client;
    std::function<void(String topic, String payload)> messageCallback;
    unsigned long lastReconnect;
    String mqttUsername;
    String mqttPassword;
    
    void connect();
};
