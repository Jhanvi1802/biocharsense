#ifndef NETWORK_CONTROL_H
#define NETWORK_CONTROL_H

#include <Arduino.h>
#include <WiFi.h>


void connect_to_wifi(const char* ssid, const char* password);
bool checkWiFiConnection();
void initTimeIST();
unsigned long getISTTimestamp();
String getISTTimeString();


#endif // NETWORK_CONTROL_H
