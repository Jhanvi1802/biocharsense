#ifndef MQTT_CONTROL_H
#define MQTT_CONTROL_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <WiFi.h>  


void initMqtt(const char* MQTT_BROKER, int  MQTT_PORT);
void reconnect(const char* MQTT_SUBTOPIC, const char* MQTT_USERNAME, const char* mqttPassword);
bool publish(const char* topic, const String& payload) ;
void callback(char* topic, byte* payload, unsigned int length);
bool publish_device_info();
void mqttLoop(); 
void publishKeepAlive(float temp, unsigned long ts);

#endif // MQTT_CONTROL_H
