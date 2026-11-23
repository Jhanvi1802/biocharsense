#ifndef VARIABLES_H
#define VARIABLES_H

#include <Arduino.h>
#include <ArduinoOTA.h>

extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;
extern const char* MQTT_SUBTOPIC;
extern const char* MQTT_PUBTOPIC;
extern const char* MQTT_BROKER;
extern const char* MQTT_KEEPALIVE_TOPIC;
extern int MQTT_PORT;
extern const char* MQTT_USERNAME;
extern const char* MQTT_PASSWORD;
extern const char* MQTT_RESPONSETOPIC;

extern bool shouldRestart;
extern bool configMode;

#endif
