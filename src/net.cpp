#define TINY_GSM_MODEM_A7672X
#define TINY_GSM_RX_BUFFER 1024
#include <TinyGsmClient.h>
#include <MQTT.h>
#include <deque>
#include <esp_task_wdt.h>
#include "net.h"
#include "config.h"
#include "core.h"
#include "log.h"
#include "timekeeper.h"

namespace {
HardwareSerial& SerialAT = Serial2;
TinyGsm modem(SerialAT);
TinyGsmClientSecure tlsClient(modem, 0);
TinyGsmClient tcpClient(modem, 1);
MQTTClient mqtt(MQTT_BUFFER_BYTES);

// arduino-mqtt must not be used from inside its callback, so incoming
// commands are queued here and handed over from Net::loop().
std::deque<std::pair<String, String>> inbox;

void onMqttMessage(String& topic, String& payload) {
    if (inbox.size() < 8) inbox.emplace_back(topic, payload);
}

void pressPwrKey(uint32_t ms) {
#if MODEM_PWRKEY_ACTIVE_HIGH
    pinMode(PIN_MODEM_PWRKEY, OUTPUT);
    digitalWrite(PIN_MODEM_PWRKEY, HIGH);
    delay(ms);
    digitalWrite(PIN_MODEM_PWRKEY, LOW);
#else
    pinMode(PIN_MODEM_PWRKEY, OUTPUT);          // pull low = press
    digitalWrite(PIN_MODEM_PWRKEY, LOW);
    delay(ms);
    pinMode(PIN_MODEM_PWRKEY, INPUT);           // release (modem has its own pull-up)
#endif
}

String quoted(const String& s) {
    int a = s.indexOf('"'), b = s.indexOf('"', a + 1);
    return (a >= 0 && b > a) ? s.substring(a + 1, b) : String();
}
}  // namespace

void Net::begin(const Settings& cfg, MessageFn onMessage) {
    _cfg = &cfg;
    _onMessage = onMessage;
    SerialAT.setRxBufferSize(2048);
    SerialAT.begin(MODEM_BAUD, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
    mqtt.onMessage(onMqttMessage);
}

// Topics per device: biocharsense/<id>/data (readings, batch start/stop,
// replies), /cmd (commands to the device), /keepalive (health, online/offline).
String Net::topic(const char* leaf) const {
    return String(TOPIC_ROOT) + "/" + _cfg->deviceId + "/" + leaf;
}

String Net::presence(bool online) const {
    return String("{\"type\":\"") + (online ? "online" : "offline") + "\",\"device_id\":\"" + _cfg->deviceId + "\"}";
}

bool Net::powerOn() {
    if (modem.testAT(1000)) return true;
    LOGI("modem: no reply, pressing PWRKEY");
    pressPwrKey(1200);
    for (uint32_t t0 = millis(); millis() - t0 < 20000;) {
        esp_task_wdt_reset();
        if (modem.testAT(1000)) return true;
    }
    _lastError = "modem_not_responding";
    return false;
}

// 1 = registered (home), 5 = roaming, anything else = not registered.
// Checks LTE (CEREG) first: on 4G-only networks such as Jio, CREG alone can
// report "not registered" although data works.
int Net::registration() {
    for (const char* q : {"+CEREG?", "+CREG?"}) {
        modem.sendAT(q);
        String r;
        if (modem.waitResponse(2000L, r) == 1) {
            int comma = r.indexOf(',');
            int st = comma > 0 ? r.substring(comma + 1).toInt() : 0;
            if (st == 1 || st == 5) return st;
        }
    }
    return 0;
}

void Net::readOperator() {
    String r;
    modem.sendAT(GF("+COPS=3,2"));               // numeric MCC+MNC
    modem.waitResponse();
    modem.sendAT(GF("+COPS?"));
    if (modem.waitResponse(2000L, r) == 1) _mccmnc = quoted(r);
    r = "";
    modem.sendAT(GF("+COPS=3,0"));               // operator name
    modem.waitResponse();
    modem.sendAT(GF("+COPS?"));
    if (modem.waitResponse(2000L, r) == 1) _operator = quoted(r);
}

String Net::apnFor() const {
    if (_cfg->apn[0]) return _cfg->apn;
    String op = _operator;
    op.toLowerCase();
    if (op.indexOf("airtel") >= 0) return "airtelgprs.com";
    if (op.indexOf("jio") >= 0) return "jionet";
    if (op.indexOf("bsnl") >= 0 || op.indexOf("cellone") >= 0) return "bsnlnet";
    if (op.indexOf("vodafone") >= 0 || op.indexOf("idea") >= 0 || op.startsWith("vi")) return "www";
    return "";                                    // let the network assign one
}

bool Net::connect() {
    if (mqtt.connected()) return true;
    _lastError = "";

    if (!powerOn()) return false;
    if (!_modemReady) {
        modem.init();
        // TinyGSM 0.12.0's A7672X init sends ATV0 (numeric result codes) in
        // non-debug builds, after which its own "OK" matching fails. Switch
        // back to text results and turn on readable error codes.
        modem.sendAT(GF("V1"));
        modem.waitResponse(2000L);
        if (!modem.testAT(2000)) { _lastError = "modem_init_failed"; return false; }
        modem.sendAT(GF("+CMEE=2"));
        modem.waitResponse();
        modem.sendAT(GF("+CTZU=1"));              // keep the modem clock synced to the network
        modem.waitResponse();
        _modemReady = true;
        LOGI("modem: %s", modem.getModemInfo().c_str());
    }
    if (modem.getSimStatus() != SIM_READY) { _lastError = "sim_not_ready"; return false; }

    LOGI("net: waiting for network");
    int reg = 0;
    for (uint32_t t0 = millis(); millis() - t0 < NETWORK_TIMEOUT_MS;) {
        esp_task_wdt_reset();
        if ((reg = registration()) != 0) break;
        delay(2000);
    }
    if (!reg) { _lastError = "no_network"; return false; }

    readOperator();
    _csq = modem.getSignalQuality();
    bool india = _mccmnc.startsWith("404") || _mccmnc.startsWith("405");
    if (_mccmnc.length() && !india && !_cfg->allowRoaming) {
        _lastError = "foreign_network_" + _mccmnc;   // e.g. a neighbouring country near the border
        return false;
    }
    LOGI("net: %s (%s), %s, signal %d", _operator.c_str(), _mccmnc.c_str(),
         reg == 5 ? "roaming" : "home", _csq);

    if (!modem.isGprsConnected()) {
        String apn = apnFor();
        LOGI("net: starting data, APN '%s'", apn.c_str());
        if (!modem.gprsConnect(apn.c_str())) { _lastError = "data_failed"; return false; }
    }

    // TinyGSM opens the modem's TLS service (AT+CCHSTART) or socket service
    // (AT+NETOPEN) on every connect but never stops it when a connection
    // drops, so a reconnect would be refused. Stop it first (an error just
    // means it was not running).
    modem.sendAT(_cfg->mqttTls ? GF("+CCHSTOP") : GF("+NETCLOSE"));
    modem.waitResponse(10000L);

    Client& link = _cfg->mqttTls ? (Client&)tlsClient : (Client&)tcpClient;
    mqtt.begin(_cfg->mqttHost, _cfg->mqttPort, link);
    // Persistent session (clean = false): the broker keeps QoS 1 commands
    // sent while the device was offline and delivers them on reconnect.
    mqtt.setOptions(MQTT_KEEPALIVE_S, false, MQTT_TIMEOUT_MS);
    // Online/offline go on the keepalive topic (retained, so a newly started
    // server sees the last state). The broker sends "offline" if we vanish.
    _willTopic = topic("keepalive");
    mqtt.setWill(_willTopic.c_str(), presence(false).c_str(), true, 1);

    LOGI("mqtt: connecting to %s:%u (%s)", _cfg->mqttHost, _cfg->mqttPort, _cfg->mqttTls ? "TLS" : "plain");
    esp_task_wdt_reset();
    const char* user = _cfg->mqttUser[0] ? _cfg->mqttUser : nullptr;
    const char* pass = _cfg->mqttPass[0] ? _cfg->mqttPass : nullptr;
    if (!mqtt.connect(_cfg->deviceId, user, pass)) {
        _lastError = "mqtt_connect_err_" + String((int)mqtt.lastError()) + "_rc_" + String((int)mqtt.returnCode());
        return false;
    }
    mqtt.subscribe(topic("cmd").c_str(), 1);
    mqtt.publish(_willTopic, presence(true), true, 1);
    LOGI("mqtt: connected as %s", _cfg->deviceId);
    return true;
}

bool Net::connected() {
    return mqtt.connected();
}

void Net::loop() {
    if (mqtt.connected()) mqtt.loop();
    while (!inbox.empty()) {
        auto msg = inbox.front();
        inbox.pop_front();
        if (_onMessage) _onMessage(msg.first, msg.second);
    }
}

bool Net::publish(const char* leaf, const String& payload, bool retain) {
    if (!mqtt.connected()) return false;
    esp_task_wdt_reset();
    bool ok = mqtt.publish(topic(leaf), payload, retain, 1);   // QoS 1: waits for PUBACK
    if (!ok) _lastError = "publish_err_" + String((int)mqtt.lastError());
    return ok;
}

void Net::disconnect(bool powerOffModem) {
    if (mqtt.connected()) {
        mqtt.publish(_willTopic, presence(false), true, 1);
        mqtt.disconnect();
    }
    if (powerOffModem) {
        modem.poweroff();
        _modemReady = false;
    }
}

void Net::restartModem() {
    LOGW("modem: restarting");
    mqtt.disconnect();
    esp_task_wdt_reset();
    if (!modem.restart()) {
        pressPwrKey(3000);                      // long press = power off
        delay(3000);
    }
    _modemReady = false;
}

bool Net::readClock(uint32_t& epochUtc, uint8_t& source) {
    if (core::parseCclk(modem.getGSMDateTime(DATE_FULL).c_str(), epochUtc)) {
        source = TS_NETWORK;
        return true;
    }
    esp_task_wdt_reset();
    if (modem.NTPServerSync("pool.ntp.org", 0) == 0 &&
        core::parseCclk(modem.getGSMDateTime(DATE_FULL).c_str(), epochUtc)) {
        source = TS_NTP;
        return true;
    }
    return false;
}

String Net::at(const String& command) {
    String c = command;
    if (c.startsWith("AT") || c.startsWith("at")) c = c.substring(2);
    modem.sendAT(c);
    String r;
    int res = modem.waitResponse(10000L, r);
    r.trim();
    return r + (res == 1 ? "\nOK" : res == 2 ? "\nERROR" : "\n(timeout)");
}
