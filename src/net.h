#pragma once
#include <Arduino.h>
#include <functional>
#include "settings.h"

// 4G + MQTT link. TinyGSM drives the SIMCom A7672S (network, data, TLS,
// clock); arduino-mqtt (256dpi) runs MQTT on top with QoS 1, so a publish
// returns true only after the broker has confirmed it.
class Net {
public:
    using MessageFn = std::function<void(const String& topic, const String& payload)>;

    void begin(const Settings& cfg, MessageFn onMessage);
    bool connect();                     // modem on -> network -> data -> MQTT -> subscribe
    bool connected();
    void loop();                        // keeps MQTT alive and delivers commands
    bool publish(const char* leaf, const String& payload, bool retain = false);
    void disconnect(bool powerOffModem);
    void restartModem();

    // Reads the network clock (falls back to NTP). Returns false if unknown.
    bool readClock(uint32_t& epochUtc, uint8_t& source);

    String topic(const char* leaf) const;
    int csq() const { return _csq; }
    const String& operatorName() const { return _operator; }
    const String& lastError() const { return _lastError; }
    String at(const String& command);   // raw AT command (serial console)

private:
    const Settings* _cfg = nullptr;
    MessageFn _onMessage;
    bool _modemReady = false;
    int _csq = 99;
    String _operator, _mccmnc, _lastError, _willTopic;

    bool powerOn();
    String presence(bool online) const;   // online/offline packet for the keepalive topic
    int registration();
    void readOperator();
    String apnFor() const;
};
