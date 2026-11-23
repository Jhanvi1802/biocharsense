#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <deque>
#include "config.h"
#include "timekeeper.h"

// Queue of event messages (cycle start/end, faults, low battery), sent on the
// data topic at the next upload. Kept in flash as well, one JSON per line, so
// a cycle_end is not lost if the device restarts or loses power before it is
// sent. The oldest is dropped when full.
class EventQueue {
public:
    // Call after LittleFS is mounted (RecordStore::begin).
    void begin(const char* deviceId, const TimeKeeper* clock) {
        _id = deviceId;
        _clock = clock;
        File f = LittleFS.open(PATH, "r");
        while (f && f.available() && _q.size() < EVENT_QUEUE_LEN) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length()) _q.push_back(line);
        }
        if (f) f.close();
    }

    // Starts an event document with the common fields filled in.
    JsonDocument make(const char* type) const {
        JsonDocument d;
        d["type"] = type;
        d["device_id"] = _id;
        uint32_t ts = _clock ? _clock->now() : 0;
        if (ts) d["ts"] = isoTime(ts); else d["up_s"] = _clock ? _clock->monoS() : 0;
        return d;
    }
    void push(const JsonDocument& d) {
        String s;
        serializeJson(d, s);
        if (_q.size() >= EVENT_QUEUE_LEN) _q.pop_front();
        _q.push_back(s);
        save();
    }
    bool empty() const { return _q.empty(); }
    const String& front() const { return _q.front(); }
    void pop() {
        _q.pop_front();
        save();
    }

private:
    static constexpr const char* PATH = "/events.txt";
    const char* _id = "";
    const TimeKeeper* _clock = nullptr;
    std::deque<String> _q;

    void save() const {
        File f = LittleFS.open(PATH, "w");
        if (!f) return;
        for (const String& s : _q) f.println(s);
        f.close();
    }
};
