#include "configControl.h"
#include "deviceControl.h"
#include "mqttControl.h"
#include "networkControl.h"
#include <PubSubClient.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "variables.h"


WiFiClient espClient;
PubSubClient client(espClient);


void initMqtt(const char* MQTT_BROKER, int MQTT_PORT) {
    if (checkWiFiConnection()){
        client.setServer(MQTT_BROKER, MQTT_PORT);
        client.setCallback(callback);
        client.setKeepAlive(40);
        delay(1000);
    }
}



void reconnect(const char* MQTT_SUBTOPIC, const char* MQTT_USERNAME, const char* mqttPassword) {
    unsigned long startTime = millis();  
    if (checkWiFiConnection()){     
        while (!client.connected()) {
            Serial.println(F("Attempting MQTT connection..."));
            // Attempt to connect
            String devicemac_string  =  WiFi.macAddress();
            String subTopic = MQTT_SUBTOPIC;
            Serial.println("subTopic : "+ subTopic);
            if (client.connect(devicemac_string.c_str(), MQTT_USERNAME, mqttPassword)) {
                digitalWrite(2,HIGH);      // for turning on the leds
                client.subscribe(subTopic.c_str());  // Subscribe to device topics
                Serial.println(F("connected to MQTT server"));
            } else {
                Serial.print("failed, rc=");
                Serial.print(client.state());
                Serial.println(" try again in 5 seconds");
                digitalWrite(2,LOW);
                delay(5000);  // Wait 5 seconds before retrying
            }
            // if (millis() - startTime >= timeoutDuration) {
            //     Serial.println("Max retry attempts reached or timeout exceeded. Exiting retry loop.");
            //     break;

            // }
            delay(500);
        }
    }
}



void callback(char* topic, byte* payload, unsigned int length) {   //// this is my function to listen to our subtopic 
    Serial.print("Received [");
    Serial.print(topic);
    Serial.print("]: ");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();


    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
        // Check if the message contains the "deviceid"
        const char* DN = doc["DN"];


        if (DN == "THEMP") {
            Serial.println("Data is for me.");
                const char* command = doc["command"];
                
                if (String(command) == "SendCongigData") {

                    DynamicJsonDocument configuration(200);

                    configuration["wifi_ssid"] = WIFI_SSID;
                    configuration["wifi_password"] = WIFI_PASSWORD;
                    configuration["mqtt_broker"] = MQTT_BROKER;
                    configuration["mqtt_port"] = MQTT_PORT;
                    configuration["mqtt_username"] = MQTT_USERNAME;
                    configuration["mqtt_password"] = MQTT_PASSWORD;
                    configuration["mqtt_sub_topic"] = MQTT_SUBTOPIC;
                    configuration["mqtt_pub_topic"] = MQTT_PUBTOPIC;
                    configuration["mqtt_response_topic"] = MQTT_RESPONSETOPIC;

                    // Convert JSON to string
                    String payload;
                    serializeJson(configuration, payload);

                    publish(MQTT_RESPONSETOPIC, payload);
                    Serial.println("Response sent: " + payload);
                }  
                else {
                    Serial.println(F("invalid Mqtt command"));
                }
            // }
        } else {
            Serial.println("Data is not for me.");
        }
    } else {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
    }
}


bool publish(const char* topic, const String& payload) {
    if (!client.connected()) {
        Serial.println(" MQTT client not connected. Publish failed.");
        return false;
    }

    bool success = client.publish(topic, payload.c_str());

    if (success) {
        Serial.println("MQTT Publish successful!");
        Serial.print("Topic: ");
        Serial.println(topic);
        Serial.print("Payload: ");
        Serial.println(payload);
    } else {
        Serial.println("MQTT Publish failed!");
    }

    return success;  // true if published, false otherwise
}
void publishKeepAlive(float temp, unsigned long ts) {
    StaticJsonDocument<200> doc;
    doc["DN"] = "THEMP";
    doc["temperature"] = temp;
    doc["timestamp"] = ts;
    doc["type"] = "keepalive";

    String out;
    serializeJson(doc, out);
    publish(MQTT_KEEPALIVE_TOPIC, out);
}

void mqttLoop() {
    client.loop();
}


bool publish_device_info() {
    DynamicJsonDocument doc(256);
    doc["MAC_ID"] = WiFi.macAddress();                    // ESP32 MAC address
    doc["IP"] = WiFi.localIP().toString();                // Local IP address
    doc["CHIP_No"] = String((uint32_t)ESP.getEfuseMac()); // Unique chip ID (lower 32 bits)
    doc["DN"] = "THEMP";                                   // Device name or ID tag
    String jsonString;
    serializeJson(doc, jsonString);
    return publish(MQTT_PUBTOPIC, jsonString);
}