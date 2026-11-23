#pragma once
#include <Arduino.h>
#include <esp_timer.h>
#include "config.h"
#include "core.h"

// Readable time for JSON, e.g. "2026-10-22 10:44:55" (India time, TZ_OFFSET_MIN).
inline String isoTime(uint32_t epochUtc) {
    char buf[32];
    core::formatIso(epochUtc, TZ_OFFSET_MIN, buf, sizeof buf);
    return String(buf);
}

enum TimeSource : uint8_t { TS_NONE = 0, TS_NETWORK = 1, TS_NTP = 2, TS_BACKFILL = 3 };

inline const char* timeSourceName(uint8_t s) {
    switch (s) {
        case TS_NETWORK:  return "network";
        case TS_NTP:      return "ntp";
        case TS_BACKFILL: return "backfill";
        default:          return "none";
    }
}

// UTC clock anchored to the network. Until the first sync, now() returns 0
// and readings keep only "seconds since boot", which are back-filled later.
class TimeKeeper {
public:
    uint32_t monoS() const { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }
    bool     valid() const { return _syncEpoch != 0; }
    uint32_t now() const { return valid() ? _syncEpoch + (monoS() - _syncMono) : 0; }
    uint8_t  source() const { return _src; }
    uint32_t syncEpoch() const { return _syncEpoch; }
    uint32_t syncMono() const { return _syncMono; }
    bool     needsSync(uint32_t resyncS) const { return !valid() || monoS() - _syncMono >= resyncS; }

    void set(uint32_t epochUtc, uint8_t src) {
        _syncEpoch = epochUtc;
        _syncMono = monoS();
        _src = src;
    }

private:
    uint32_t _syncEpoch = 0;
    uint32_t _syncMono = 0;
    uint8_t  _src = TS_NONE;
};
