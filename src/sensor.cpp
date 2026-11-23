#include "sensor.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "config.h"
#include "log.h"

static Adafruit_ADS1115 ads;

bool Sensor::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    _present = ads.begin(ADS_I2C_ADDR, &Wire);
    if (_present) {
        ads.setGain(GAIN_ONE);                  // +/-4.096 V range, 0.125 mV per step
        ads.setDataRate(RATE_ADS1115_128SPS);
        LOGI("ADS1115 found at 0x%02X", ADS_I2C_ADDR);
    } else {
        LOGW("ADS1115 not found at 0x%02X", ADS_I2C_ADDR);
    }
    return _present;
}

float Sensor::readVolts(uint8_t channel) {
    int32_t sum = 0;
    for (int i = 0; i < ADC_AVERAGE; i++) sum += ads.readADC_SingleEnded(channel);
    return ads.computeVolts((int16_t)(sum / ADC_AVERAGE));
}

// A 4-hour burn scaled to the configured range: 30 min heat-up to 80% of the
// range, a 2.5 h burn with slow wander, then 1 h of cooling.
float Sensor::simulatedTempC(const Settings& s) const {
    const float span = s.rangeMaxC - s.rangeMinC;
    const float ambient = s.rangeMinC + 0.10f * span;
    const float peak = s.rangeMinC + 0.80f * span;
    const float m = fmodf(millis() / 60000.0f, 240.0f);   // minutes into the cycle
    if (m < 30.0f)  return ambient + (peak - ambient) * (m / 30.0f);
    if (m < 180.0f) return peak + 0.03f * span * sinf(m / 9.0f);
    return peak - (peak - ambient) * ((m - 180.0f) / 60.0f);
}

Sample Sensor::read(const Settings& s) {
    Sample out{};

    if (s.simulate) {
        // Go through the same conversion as a real reading.
        float mA = 4.0f + 16.0f * (simulatedTempC(s) - s.rangeMinC) / (s.rangeMaxC - s.rangeMinC);
        out.loop = core::convertLoop(mA * s.shuntOhms / 1000.0f, s.shuntOhms, s.rangeMinC, s.rangeMaxC);
        out.battV = 3.95f;
        return out;
    }

    if (!_present && !begin()) {
        out.loop = core::LoopReading{NAN, NAN, core::FAULT_NO_ADC};
        out.battV = NAN;
        return out;
    }
    out.loop = core::convertLoop(readVolts(ADS_CH_LOOP), s.shuntOhms, s.rangeMinC, s.rangeMaxC);
    out.battV = readVolts(ADS_CH_BATT) * BATT_DIVIDER;
    if (out.battV < 2.5f) out.battV = NAN;   // divider not connected (e.g. USB power on the bench)
    return out;
}
