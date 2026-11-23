#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "configControl.h"
#include "deviceControl.h"
#include "mqttControl.h"
#include "networkControl.h"
#include "variables.h"

unsigned long lastReconnectMillis = 0;
unsigned long lastPublishMillis   = 0;
unsigned long lastKeepAliveMillis = 0;
unsigned long lastPrintMillis     = 0;

const unsigned long RECONNECT_INTERVAL = 30000;   // 30 sec
const unsigned long PUBLISH_INTERVAL   = 120000;  // 2 min
const unsigned long KEEPALIVE_INTERVAL = 60000;   // 1 min
const unsigned long PRINT_INTERVAL     = 5000;    // 5 sec

// -------- Simple Software Restart --------
void restartDevice() {
  Serial.println("🔄 Restarting ESP32 by software...");
  delay(1000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("<<<<<<<<<<<<<< THERMOCOUPLE + HUMIDITY Boot >>>>>>>>>>>>>>");

  initNVS();
  printNVSStats();
  loadConfigFromNVS();
  initFileSystem();

  initSensors();   // MAX6675 + DHT11

  if (!configMode) {
    connect_to_wifi(WIFI_SSID, WIFI_PASSWORD);
    initMqtt(MQTT_BROKER, MQTT_PORT);
    reconnect(MQTT_SUBTOPIC, MQTT_USERNAME, MQTT_PASSWORD);
    publish_device_info();
  }
}

void loop() {

  unsigned long now = millis();

  // ---- Reconnect every 30 sec ----
  if (!configMode && now - lastReconnectMillis >= RECONNECT_INTERVAL) {
    lastReconnectMillis = now;
    checkWiFiConnection();
    reconnect(MQTT_SUBTOPIC, MQTT_USERNAME, MQTT_PASSWORD);
  }

  // ---- Print temperature & humidity every 5 sec ----
  if (now - lastPrintMillis >= PRINT_INTERVAL) {
    lastPrintMillis = now;
    SensorReadings s = readSensors();

    Serial.print(" Temperature: ");
    Serial.print(s.kTypeTempC);
    Serial.print(" °C   ");

    Serial.print(" Humidity: ");
    Serial.print(s.humidity);
    Serial.println(" %");
  }

  // ---- Normal publish every 2 min ----
  if (!configMode && now - lastPublishMillis >= PUBLISH_INTERVAL) {
    lastPublishMillis = now;

    SensorReadings s = readSensors();
    String ts = getISTTimeString();

    DynamicJsonDocument doc(256);
    doc["DN"] = "TEMP";
    doc["Temperature"] = s.kTypeTempC;
    doc["Humidity"] = s.humidity;
    doc["Timestamp"] = ts;

    String payload;
    serializeJson(doc, payload);

    publish(MQTT_PUBTOPIC, payload);
    Serial.println(" Normal Publish: " + payload);
  }

  // ---- Keepalive every 1 min ----
  if (!configMode && now - lastKeepAliveMillis >= KEEPALIVE_INTERVAL) {
    lastKeepAliveMillis = now;

    SensorReadings s = readSensors();
    String ts = getISTTimeString();

    DynamicJsonDocument keepDoc(256);
    keepDoc["DN"] = "TEMP";
    keepDoc["Temperature"] = s.kTypeTempC;
    keepDoc["Humidity"] = s.humidity;
    keepDoc["Timestamp"] = ts;
    keepDoc["type"] = "keepalive";

    String keepPayload;
    serializeJson(keepDoc, keepPayload);

    publish(MQTT_KEEPALIVE_TOPIC, keepPayload);
    Serial.println(" Keepalive: " + keepPayload);
  }

  mqttLoop();
  delay(10);
}
