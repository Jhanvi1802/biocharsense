#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.begin("YOUR_WIFI_SSID","YOUR_WIFI_PASSWORD");
  unsigned long start = millis();
  while (millis() - start < 10000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("WiFi status=");
  Serial.println(WiFi.status());
}

void loop() {}
