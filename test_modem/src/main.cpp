#include <Arduino.h>

// A7672S 4G modem test.
//
// Wiring (Robu A7672S board on ESP32 UART2, through a level shifter if the
// modem's TXD idles at about 1.8 V):
//   modem TXD    to GPIO16 (ESP32 RX2)
//   modem RXD    to GPIO17 (ESP32 TX2)
//   modem PWRKEY to GPIO25 (optional, leave open if the board starts by itself)
//   modem GND    to ESP32 GND
//
// On boot it runs the basic checks below, then anything typed in the serial
// monitor (e.g. AT+CSQ) is sent to the modem and the reply printed.

#define PIN_MODEM_RX     16     // ESP32 receives on this pin  (connect to modem TXD)
#define PIN_MODEM_TX     17     // ESP32 transmits on this pin (connect to modem RXD)
#define PIN_MODEM_PWRKEY 25     // -1 if PWRKEY is not wired
#define MODEM_BAUD       115200

HardwareSerial modem(2);

// Send one AT command and print everything the modem replies within waitMs.
void sendAT(const char* cmd, uint32_t waitMs = 1500) {
  Serial.printf("\n>> %s\n", cmd);
  modem.print(cmd);
  modem.print("\r\n");
  uint32_t start = millis();
  while (millis() - start < waitMs) {
    while (modem.available()) Serial.write(modem.read());
  }
}

void powerOnModem() {
  if (PIN_MODEM_PWRKEY < 0) return;
  // PWRKEY is active-low with the modem's own pull-up, so never drive it high:
  // pull it to GND for >= 1 s (like pressing a button), then release it.
  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  delay(1200);
  pinMode(PIN_MODEM_PWRKEY, INPUT);
}

void setup() {
  Serial.begin(115200);
  modem.begin(MODEM_BAUD, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
  delay(1000);

  Serial.println("A7672S modem test");
  powerOnModem();
  Serial.println("Waiting 10 s for the modem to boot...");
  delay(10000);

  sendAT("AT");                 // expect: OK            -> wiring + baud are right
  sendAT("ATI");                // module model + firmware
  sendAT("AT+CPIN?");           // expect: +CPIN: READY  -> SIM detected, no PIN lock
  sendAT("AT+CSQ");             // signal: 10-31 usable, 99 = no signal
  sendAT("AT+CEREG?");          // expect: +CEREG: 0,1 (home) or 0,5 (roaming) -> on 4G network
  sendAT("AT+COPS?");           // operator name (Jio / Airtel / Vi)
  sendAT("AT+CGATT?");          // expect: +CGATT: 1     -> data attached
  sendAT("AT+CGPADDR=1", 3000); // an IP address here    -> internet is working

  Serial.println("\nType AT commands below:");
}

void loop() {
  while (Serial.available()) modem.write(Serial.read());
  while (modem.available())  Serial.write(modem.read());
}
