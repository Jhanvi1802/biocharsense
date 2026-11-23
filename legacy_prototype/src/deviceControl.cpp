#include "configControl.h"
#include "deviceControl.h"
#include "mqttControl.h"
#include "networkControl.h"

#include <max6675.h>
#include <DHT.h>

// ===== MAX6675 Pin Mapping (ESP32) =====
#define MAX6675_SCK  18
#define MAX6675_CS   5
#define MAX6675_SO   19

// ===== DHT11 Pin Mapping =====
#define DHTPIN 26
#define DHTTYPE DHT11

MAX6675 thermocouple(MAX6675_SCK, MAX6675_CS, MAX6675_SO);
DHT dht(DHTPIN, DHTTYPE);

void initSensors()
{
    Serial.println("======================================");
    Serial.println(" Initializing Sensors");
    Serial.println(" MAX6675 Thermocouple");
    Serial.println(" DHT11 Humidity Sensor");
    Serial.printf(" SCK=%d, CS=%d, SO=%d, DHT_PIN=%d\n", MAX6675_SCK, MAX6675_CS, MAX6675_SO, DHTPIN);
    Serial.println("======================================");

    dht.begin();
    delay(1000);
}

SensorReadings readSensors()
{
    SensorReadings s;
    s.timestamp = millis();
    s.kTypeTempC = NAN;
    s.humidity = NAN;

    // ---- MAX6675 Temperature ----
    double tempC = thermocouple.readCelsius();
    if (!isnan(tempC) && tempC > -200 && tempC < 1350) {
        s.kTypeTempC = (float)tempC;
        Serial.printf("[MAX6675] Temperature = %.2f °C\n", s.kTypeTempC);
    } else {
        Serial.println("[MAX6675] Thermocouple error or disconnected");
    }

    // ---- DHT11 Humidity ----
    float hum = dht.readHumidity();
    if (!isnan(hum)) {
        s.humidity = hum;
        Serial.printf("[DHT11] Humidity = %.2f %%\n", s.humidity);
    } else {
        Serial.println("[DHT11] Humidity read failed");
    }

    return s;
}
