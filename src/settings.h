#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Field-adjustable configuration, persisted in NVS.
struct Settings {
    char     deviceId[24];
    char     apn[40];        // empty = pick from the operator automatically
    char     mqttHost[64];
    uint16_t mqttPort;
    bool     mqttTls;
    char     mqttUser[40];
    char     mqttPass[64];
    uint32_t sampleS;        // idle: sample interval
    uint32_t uploadS;        // idle: upload interval
    uint32_t liveSampleS;    // batch running: sample interval, each reading sent live
    float    rangeMinC;      // transmitter's programmed range
    float    rangeMaxC;
    float    shuntOhms;      // measured value of the 100 Ohm shunt
    float    cycleStartC;
    float    cycleEndC;
    uint32_t cycleHoldS;
    float    battLowV;
    bool     allowRoaming;   // false: refuse non-Indian networks (border areas)
    bool     modemAlwaysOn;  // true: stay connected so commands arrive at once
    bool     simulate;       // true: generate a fake burn (no sensor needed)
};

namespace settings {
void load(Settings& s);
void save(const Settings& s);
void erase();
// Set one value by key (serial console / set_config). Returns false and
// fills `err` if the key is unknown or the value is out of range.
bool apply(Settings& s, const String& key, const String& value, String& err);
// All settings except secrets (the password is reported only as set / not set).
void toJson(const Settings& s, JsonObject out);
String keyList();
}
