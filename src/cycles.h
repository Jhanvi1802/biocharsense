#pragma once
#include <Arduino.h>
#include "config.h"
#include "core.h"
#include "events.h"
#include "record_store.h"
#include "settings.h"

// Tracks burn cycles and links them to Kalki batches. A cycle starts either
// from a `start_cycle` command (with the batch ID) or automatically when the
// kiln heats up; every reading taken during it carries the cycle number.
// State survives reboots (NVS).
class CycleManager {
public:
    struct State {
        uint8_t  active;
        uint8_t  byCommand;      // 1 = started by command, 0 = auto-detected
        uint32_t number;
        char     batchId[32];
        uint32_t startTs;
        uint32_t firstSeq;
        uint32_t lastSeq;
        uint32_t readings;
        int16_t  peakDeci;
    };

    void begin(const Settings& s, EventQueue& events);
    void configure(const Settings& s);

    // Call before storing a reading (may open a cycle), then after() with the
    // stored record (updates the summary, closes the cycle if it cooled down).
    void before(float tempC, bool valid, uint32_t monoS, uint32_t nowTs);
    void after(const Record& r, uint32_t nowTs);

    bool start(const char* batchId, uint32_t nowTs, String& err);
    bool end(uint32_t nowTs, const char* reason, String& err);

    uint32_t activeNumber() const { return _st.active ? _st.number : 0; }
    const State& state() const { return _st; }
    String cycleId(uint32_t number) const;
    void reset();
    void sync() { persist(); }

private:
    core::CycleDetector _det;
    State _st{};
    uint32_t _counter = 0;
    bool _endRequested = false;
    uint32_t _lastPersistMs = 0;
    String _deviceId;
    EventQueue* _events = nullptr;

    void open(const char* batchId, bool byCommand, uint32_t nowTs);
    void close(uint32_t nowTs, const char* reason);
    void persist();
};
