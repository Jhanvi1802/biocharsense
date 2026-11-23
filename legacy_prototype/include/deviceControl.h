#ifndef DEVICE_CONTROL_H
#define DEVICE_CONTROL_H

#include <Arduino.h>

typedef struct {
    unsigned long timestamp;
    float kTypeTempC;   // K-Type thermocouple temperature
    float humidity;     // DHT11 humidity
} SensorReadings;

void initSensors();
SensorReadings readSensors();

#endif
