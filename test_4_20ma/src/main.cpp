#include <Arduino.h>

// 4-20 mA loop test for the Tempsens K-type thermocouple and head transmitter.
//
// Wiring (2-wire loop, transmitter needs about 24 V DC):
//   +24V to transmitter (+)
//   transmitter (-) to the 100 R shunt, other end of the shunt to GND
//   transmitter (-) also through 10k to GPIO34 (ADC)
//   GPIO34 to GND through a 3.3 V zener (protection) and 100 nF (filter)
//   24 V supply GND and ESP32 GND must be connected together.
//
// The ESP32 measures the voltage across the shunt, I(mA) = V(mV) / R(ohm),
// and maps 4-20 mA onto the transmitter's programmed temperature range.

#define PIN_LOOP_ADC   34       // ADC1 pin (ADC2 pins don't work while WiFi is on)
#define SHUNT_OHMS     100.0f   // measure your real resistor with a multimeter and put it here
#define RANGE_MIN_C    0.0f     // transmitter range as programmed by Tempsens (PI says 0-200 C)
#define RANGE_MAX_C    200.0f
#define SAMPLES        64       // averaging to smooth ESP32 ADC noise

#define LOOP_LOW_FAULT_MA    3.6f    // below this: open loop / no 24 V / broken wire
#define LOOP_HIGH_FAULT_MA   21.0f   // above this: thermocouple broken (burnout) or over-range

float readShuntMilliVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogReadMilliVolts(PIN_LOOP_ADC);   // uses the chip's factory ADC calibration
    delay(2);
  }
  return sum / (float)SAMPLES;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LOOP_ADC, ADC_11db);   // ~0.15 - 2.45 V usable range

  Serial.println("4-20 mA thermocouple test");
  Serial.printf("Shunt = %.1f ohm, range = %.0f..%.0f C, pin = GPIO%d\n",
                SHUNT_OHMS, RANGE_MIN_C, RANGE_MAX_C, PIN_LOOP_ADC);
  Serial.println("Expected at room temp (~25 C): ~6.0 mA, ~0.60 V across a 100 ohm shunt");
  Serial.println();
}

void loop() {
  float mV = readShuntMilliVolts();
  float mA = mV / SHUNT_OHMS;

  const char* status = "OK";
  float tempC = NAN;

  if (mA < LOOP_LOW_FAULT_MA) {
    status = "FAULT: loop open / no 24V / wire broken";
  } else if (mA > LOOP_HIGH_FAULT_MA) {
    status = "FAULT: sensor break (burnout) or over-range";
  } else {
    float clamped = constrain(mA, 4.0f, 20.0f);
    tempC = RANGE_MIN_C + (clamped - 4.0f) * (RANGE_MAX_C - RANGE_MIN_C) / 16.0f;
    if (mA >= 20.0f) status = "AT TOP OF RANGE (real temp may be higher)";
  }

  Serial.printf("Shunt: %7.1f mV | Loop: %6.2f mA | Temp: %7.1f C | %s\n",
                mV, mA, tempC, status);
  delay(1000);
}
