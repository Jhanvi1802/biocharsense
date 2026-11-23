#pragma once
#include "core.h"
#include "settings.h"

struct Sample {
    core::LoopReading loop;   // mA, deg C and fault code
    float battV;              // NaN if it could not be measured
};

// Reads the Tempsens 4-20 mA loop and the battery through an ADS1115.
// In simulate mode it produces a realistic burn curve instead, so the whole
// data path can be tested before the sensor hardware is connected.
class Sensor {
public:
    bool begin();
    Sample read(const Settings& s);
    bool present() const { return _present; }

private:
    bool  _present = false;
    float readVolts(uint8_t channel);
    float simulatedTempC(const Settings& s) const;
};
