#include "configControl.h"
#include "deviceControl.h"
#include "mqttControl.h"
#include "networkControl.h"

#include <WiFi.h>
#include <time.h>

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 19800   // IST = UTC + 5:30
#define DAYLIGHT_OFFSET_SEC 0

void connect_to_wifi(const char* ssid, const char* password) {
  Serial.println();
  Serial.println(" Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 15000;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // ---- NTP TIME SYNC (IST) ----
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      Serial.print(" IST Time Synced: ");
      Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
    } else {
      Serial.println(" Failed to get IST time");
    }
  } else {
    Serial.println("\n Failed to connect to WiFi!");
  }
}

bool checkWiFiConnection() {
  return (WiFi.status() == WL_CONNECTED);
}

// Returns formatted IST time string
String getISTTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "TIME_NOT_SYNCED";
  }

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}
