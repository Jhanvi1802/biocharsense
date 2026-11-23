#include <Arduino.h>

// GPIO2 = D4 = onboard blue LED on NodeMCU. Active LOW (LOW = on, HIGH = off).
#define LED_PIN 2

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("================ BLINK TEST ================");
    Serial.println("If you see this -> Serial communication works");
    Serial.println("If onboard blue LED blinks -> GPIO works");
    Serial.println("============================================");
    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_PIN, LOW);   // ON
    Serial.println("LED ON");
    delay(500);
    digitalWrite(LED_PIN, HIGH);  // OFF
    Serial.println("LED OFF");
    delay(500);
}
