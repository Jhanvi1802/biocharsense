#include "configControl.h"
#include "deviceControl.h"
#include "mqttControl.h"
#include "networkControl.h"
#include <nvs_flash.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "variables.h"

Preferences prefs;


const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
const char* MQTT_SUBTOPIC = "";
const char* MQTT_PUBTOPIC = "";
const char* MQTT_KEEPALIVE_TOPIC = "";
const char* MQTT_BROKER = "";
int   MQTT_PORT = 0;
const char* MQTT_USERNAME = "";
const char* MQTT_PASSWORD = "";
const char* MQTT_RESPONSETOPIC = "";

bool shouldRestart = false;
bool configMode = false;



bool initNVS() {
  if (!prefs.begin("config", false)) {   // false = read + write mode
    Serial.println("Failed to initialize NVS!");
    return false;
  }
  Serial.println(" NVS Initialized Successfully");
  return true;
}

void printNVSStats() {
  nvs_stats_t nvsStats;
  nvs_handle handle;

  // Initialize NVS (if not already done)
  if (nvs_flash_init() != ESP_OK) {
    Serial.println("Failed to init NVS");
    return;
  }

  // Open the namespace (read-only is fine)
  if (nvs_open("config", NVS_READWRITE, &handle) == ESP_OK) {
    nvs_get_stats(NULL, &nvsStats);

    Serial.println("===== NVS MEMORY USAGE =====");
    Serial.printf("Total Entries : %d\n", nvsStats.total_entries);
    Serial.printf("Used  Entries : %d\n", nvsStats.used_entries);
    Serial.printf("Free  Entries : %d\n", nvsStats.free_entries);

    nvs_close(handle);
  } else {
    Serial.println("Failed to open NVS namespace");
  }
}

void loadConfigFromNVS() {
  WIFI_SSID          = strdup(prefs.getString("ssid", "YOUR_WIFI_SSID").c_str());
  WIFI_PASSWORD      = strdup(prefs.getString("pass", "YOUR_WIFI_PASSWORD").c_str());
  MQTT_SUBTOPIC      = strdup(prefs.getString("subtop", "varaha/Sub").c_str());
  MQTT_PUBTOPIC      = strdup(prefs.getString("pubtop", "varaha/sensor").c_str());
  MQTT_RESPONSETOPIC = strdup(prefs.getString("resptop", "varaha/Response").c_str());
  MQTT_KEEPALIVE_TOPIC = strdup(prefs.getString("keep", "varaha/keepalive").c_str());
  MQTT_BROKER        = strdup(prefs.getString("broker", "broker.emqx.io").c_str());
  MQTT_PORT          = prefs.getInt("port", 1883);
  MQTT_USERNAME      = strdup(prefs.getString("user", "").c_str());
  MQTT_PASSWORD      = strdup(prefs.getString("mqtpass", "").c_str());

  Serial.println(" Config Loaded from NVS:");
  Serial.println("--------------------------------------");
  Serial.printf("WIFI_SSID            : %s\n", WIFI_SSID);
  Serial.printf("WIFI_PASSWORD        : %s\n", WIFI_PASSWORD);
  Serial.printf("MQTT_SUBTOPIC        : %s\n", MQTT_SUBTOPIC);
  Serial.printf("MQTT_PUBTOPIC        : %s\n", MQTT_PUBTOPIC);
  Serial.printf("MQTT_RESPONSETOPIC   : %s\n", MQTT_RESPONSETOPIC);
  Serial.printf("MQTT_BROKER          : %s\n", MQTT_BROKER);
  Serial.printf("MQTT_PORT            : %d\n", MQTT_PORT);
  Serial.printf("MQTT_USERNAME        : %s\n", MQTT_USERNAME);
  Serial.printf("MQTT_PASSWORD        : %s\n", MQTT_PASSWORD);
  Serial.println("--------------------------------------");
}


bool updateConfig(
    String ssid = "", 
    String pass = "", 
    String subtop = "", 
    String pubtop = "", 
    String resptop = "",
    String broker = "", 
    int port = -1, 
    String user = "", 
    String mqtpass = ""
    ) {

  // Update only if new value is provided
  if (ssid.length() > 0)       WIFI_SSID = strdup(ssid.c_str());
  if (pass.length() > 0)       WIFI_PASSWORD = strdup(pass.c_str());
  if (subtop.length() > 0)     MQTT_SUBTOPIC = strdup(subtop.c_str());
  if (pubtop.length() > 0)     MQTT_PUBTOPIC = strdup(pubtop.c_str());
  if (resptop.length() > 0)    MQTT_RESPONSETOPIC = strdup(resptop.c_str());
  if (broker.length() > 0)     MQTT_BROKER = strdup(broker.c_str());
  if (port > 0)                MQTT_PORT = port;
  if (user.length() > 0)       MQTT_USERNAME = strdup(user.c_str());
  if (mqtpass.length() > 0)    MQTT_PASSWORD = strdup(mqtpass.c_str());

  // Save back to NVS
  prefs.begin("config", false);
  prefs.putString("ssid", WIFI_SSID);
  prefs.putString("pass", WIFI_PASSWORD);
  prefs.putString("subtop", MQTT_SUBTOPIC);
  prefs.putString("pubtop", MQTT_PUBTOPIC);
  prefs.putString("resptop", MQTT_RESPONSETOPIC);
  prefs.putString("broker", MQTT_BROKER);
  prefs.putInt("port", MQTT_PORT);
  prefs.putString("user", MQTT_USERNAME);
  prefs.putString("mqtpass", MQTT_PASSWORD);
  prefs.end();

  Serial.println(" Partial Config Updated & Saved to NVS");
  return true;
}


bool initFileSystem() {
  if (!LittleFS.begin(true)) { // true = format if fails
    Serial.println(" LittleFS Mount Failed");
    return false;
  }
  Serial.println(" LittleFS Mounted Successfully");
  return true;
}

bool fileExists(const char* path) {
  return LittleFS.exists(path);
}

void createDefaultJson(const char* path) {
  // Create a default JSON structure
  DynamicJsonDocument doc(256);
  doc["device"] = "ESP32";
  
  doc["status"] = "initialized";

  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    Serial.println(" Failed to create default JSON file");
    return;
  }

  serializeJsonPretty(doc, file);
  file.close();
  Serial.println(" Default JSON file created");
}

DynamicJsonDocument readJsonFile(const char* path) {
  if (!fileExists(path)) {
    Serial.println(" JSON file not found. Creating default...");
    createDefaultJson(path);
  }

  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    Serial.println(" Failed to open JSON file for reading");
    return DynamicJsonDocument(0);
  }

  size_t size = file.size();
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, file);

  if (error) {
    Serial.print(" Failed to parse JSON: ");
    Serial.println(error.f_str());
  } else {
    Serial.println(" JSON file read successfully");
  }

  file.close();
  return doc;
}

bool updateJsonFile(const char* path, JsonDocument& doc) {
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    Serial.println(" Failed to open JSON file for writing");
    return false;
  }

  if (serializeJsonPretty(doc, file) == 0) {
    Serial.println(" Failed to write JSON data");
    file.close();
    return false;
  }

  file.close();
  Serial.println(" JSON file updated successfully");
  return true;
}

void printConfig(const JsonDocument &config) {
  Serial.println("===== Config File Content =====");
  serializeJsonPretty(config, Serial);  // Prints the JSON in a readable format
  Serial.println("\n===============================");
}

DynamicJsonDocument checkAndLoadConfig(const char *filename) {
  DynamicJsonDocument config(512); // adjust size as per your file
  //  Check if file exists
  if (!LittleFS.exists(filename)) {
    Serial.println(" Config file not found. Creating default one");
    File newFile = LittleFS.open(filename, "w");
    if (newFile) {
      
      config["ssid"] = "YOUR_WIFI_SSID";
      config["password"] = "YOUR_WIFI_PASSWORD";
      config["mqtt_subtopic"] = "default/subtopic";
      config["mqtt_pubtopic"] = "default/pubtopic";
      config["mqtt_responsetopic"] = "default/responsetopic";
      config["mqtt_broker"] = "broker.hivemq.com";
      config["mqtt_port"] = 1883;
      config["mqtt_username"] = "user";
      config["mqtt_password"] = "password";
      serializeJsonPretty(config, newFile);
      newFile.close();
      Serial.println("Default config file created.");
    }
    return config;
  }

  // Open existing file
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.println("Failed to open config file");
    return config;
  }

  // Try parsing JSON
  DeserializationError error = deserializeJson(config, file);
  file.close();

  if (error) {
    Serial.print("Invalid JSON format: ");
    Serial.println(error.c_str());

    //  Recreate valid JSON
    File newFile = LittleFS.open(filename, "w");
    if (newFile) {
      config.clear();
      config["device"] = "ESP32_Device";
      config["ssid"] = "defaultSSID";
      config["password"] = "defaultPassword";
      serializeJsonPretty(config, newFile);
      newFile.close();
      Serial.println("Recreated default config file.");
    }
  } else {
    Serial.println(" JSON file is valid!");
  }

  return config; // return usable JSON document
}
