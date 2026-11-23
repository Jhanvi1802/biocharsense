#pragma once
// Hardware-independent logic (plain C++, no Arduino headers), so it can be
// unit tested on a PC with `pio test -e native`.
#include <stdint.h>
#include <stdio.h>
#include <cmath>

namespace core {

// 4-20 mA loop conversion
// Thresholds follow NAMUR NE43, which most head transmitters use to signal
// faults: below 3.6 mA = loop open / downscale burnout, above 21 mA = upscale
// burnout (broken thermocouple). 3.6-3.8 and 20.5-21 mA are out-of-range
// readings that are still clamped to the range ends and flagged.
enum Fault : uint8_t {
    FAULT_OK = 0,
    FAULT_LOOP_OPEN,      // < 3.6 mA: wire cut, no 24 V, transmitter dead
    FAULT_UNDER_RANGE,    // 3.6-3.8 mA: below the programmed range
    FAULT_OVER_RANGE,     // 20.5-21 mA: above the programmed range
    FAULT_SENSOR_BREAK,   // > 21 mA: thermocouple broken (upscale burnout)
    FAULT_NO_ADC,         // ADS1115 not responding
};

inline const char* faultName(uint8_t f) {
    switch (f) {
        case FAULT_OK:           return "OK";
        case FAULT_LOOP_OPEN:    return "LOOP_OPEN";
        case FAULT_UNDER_RANGE:  return "UNDER_RANGE";
        case FAULT_OVER_RANGE:   return "OVER_RANGE";
        case FAULT_SENSOR_BREAK: return "SENSOR_BREAK";
        case FAULT_NO_ADC:       return "NO_ADC";
        default:                 return "UNKNOWN";
    }
}

constexpr float MA_LOOP_OPEN = 3.6f;
constexpr float MA_UNDER     = 3.8f;
constexpr float MA_OVER      = 20.5f;
constexpr float MA_BREAK     = 21.0f;

struct LoopReading {
    float   mA;      // loop current (NaN if the ADC failed)
    float   tempC;   // NaN when there is no usable temperature
    uint8_t fault;   // Fault
};

// shuntVolts: voltage across the shunt resistor; rMin/rMax: the transmitter's
// programmed range (e.g. 0-200 or 0-1200 C).
inline LoopReading convertLoop(float shuntVolts, float shuntOhms, float rMin, float rMax) {
    LoopReading r{NAN, NAN, FAULT_OK};
    if (std::isnan(shuntVolts) || shuntOhms <= 0.0f || rMax <= rMin) {
        r.fault = FAULT_NO_ADC;
        return r;
    }
    r.mA = shuntVolts / shuntOhms * 1000.0f;
    if (r.mA < MA_LOOP_OPEN) { r.fault = FAULT_LOOP_OPEN; return r; }
    if (r.mA > MA_BREAK)     { r.fault = FAULT_SENSOR_BREAK; return r; }
    float ma = r.mA < 4.0f ? 4.0f : (r.mA > 20.0f ? 20.0f : r.mA);
    r.tempC = rMin + (ma - 4.0f) * (rMax - rMin) / 16.0f;
    if (r.mA < MA_UNDER)     r.fault = FAULT_UNDER_RANGE;
    else if (r.mA > MA_OVER) r.fault = FAULT_OVER_RANGE;
    return r;
}

// Approximate state of charge of a Li-ion cell from its resting voltage.
inline uint8_t liionPercent(float v) {
    static const float tbl[][2] = {
        {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.90f, 65}, {3.80f, 50},
        {3.70f, 35},  {3.60f, 20}, {3.50f, 10}, {3.40f, 5},  {3.30f, 0},
    };
    const int n = sizeof(tbl) / sizeof(tbl[0]);
    if (std::isnan(v) || v <= tbl[n - 1][0]) return 0;
    if (v >= tbl[0][0]) return 100;
    for (int i = 1; i < n; i++) {
        if (v >= tbl[i][0]) {
            float f = (v - tbl[i][0]) / (tbl[i - 1][0] - tbl[i][0]);
            return (uint8_t)(tbl[i][1] + f * (tbl[i - 1][1] - tbl[i][1]) + 0.5f);
        }
    }
    return 0;
}

// Store-and-forward ring bookkeeping
// Sequence numbers only ever increase. `head` is the oldest reading not yet
// confirmed by the server, `tail` the next sequence number to assign. The
// physical slot of a sequence number is seq % capacity. When the buffer is
// full the oldest unsent reading is overwritten and counted in `dropped`.
struct RingSeq {
    uint32_t capacity = 1;
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t dropped = 0;

    uint32_t count() const { return tail - head; }
    uint32_t slotOf(uint32_t seq) const { return seq % capacity; }
    // Oldest sequence number still physically stored (sent or not).
    uint32_t oldest() const { return tail > capacity ? tail - capacity : 0; }

    uint32_t append() {
        if (count() >= capacity) { head++; dropped++; }
        return tail++;
    }
    // Server confirmed everything up to and including `seq`.
    void ackThrough(uint32_t seq) {
        if (seq >= head && seq < tail) head = seq + 1;
    }
    // Send again from `from` (clamped to what is still stored). Returns the
    // sequence number sending will restart from.
    uint32_t rewind(uint32_t from) {
        if (from < oldest()) from = oldest();
        if (from < head) head = from;
        return head;
    }
};

// Burn-cycle detection
// Starts a cycle after `confirmN` consecutive readings at or above startC.
// Ends it once the temperature has stayed below endC for endHoldS seconds.
// Invalid readings (sensor faults) never start or end a cycle.
struct CycleDetector {
    enum Event : uint8_t { NONE = 0, STARTED, ENDED };

    float    startC = 100.0f;
    float    endC = 80.0f;
    uint32_t endHoldS = 1800;
    uint8_t  confirmN = 3;

    bool     active = false;
    uint8_t  aboveCount = 0;
    bool     belowTiming = false;
    uint32_t belowSince = 0;

    Event update(float tempC, bool valid, uint32_t nowS) {
        if (!valid) return NONE;
        if (!active) {
            if (tempC >= startC) {
                if (++aboveCount >= confirmN) { forceStart(); return STARTED; }
            } else {
                aboveCount = 0;
            }
            return NONE;
        }
        if (tempC < endC) {
            if (!belowTiming) { belowTiming = true; belowSince = nowS; }
            if (nowS - belowSince >= endHoldS) { forceEnd(); return ENDED; }
        } else {
            belowTiming = false;
        }
        return NONE;
    }
    void forceStart() { active = true; aboveCount = 0; belowTiming = false; }
    void forceEnd()   { active = false; aboveCount = 0; belowTiming = false; }
};

// Time
// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
inline int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// Parses the SIMCom AT+CCLK clock string "yy/MM/dd,hh:mm:ss+zz" (zz = time
// zone in quarter hours, e.g. +22 for IST) into a UTC epoch. Rejects the
// modem's unset default clock, which reads as 1970/1980 ("70"/"80"): only
// years 2024-2069 are accepted.
inline bool parseCclk(const char* s, uint32_t& epochUtc) {
    int yy, MM, dd, hh, mm, ss, tz;
    char sign;
    if (!s || sscanf(s, "%d/%d/%d,%d:%d:%d%c%d", &yy, &MM, &dd, &hh, &mm, &ss, &sign, &tz) != 8) return false;
    if (yy < 24 || yy > 69 || MM < 1 || MM > 12 || dd < 1 || dd > 31 || hh > 23 || mm > 59 || ss > 60) return false;
    if (sign != '+' && sign != '-') return false;
    int64_t t = daysFromCivil(2000 + yy, (unsigned)MM, (unsigned)dd) * 86400 + hh * 3600 + mm * 60 + ss;
    int64_t offset = (int64_t)tz * 15 * 60;
    t -= (sign == '-') ? -offset : offset;
    epochUtc = (uint32_t)t;
    return true;
}

// Civil date for a count of days since 1970-01-01 (Howard Hinnant's algorithm).
inline void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = (int)(yoe + era * 400 + (m <= 2));
}

// UTC epoch -> readable local time, e.g. "2026-10-22 10:44:55" for
// offsetMin = 330 (IST). `out` needs at least 20 bytes.
inline void formatIso(uint32_t epochUtc, int offsetMin, char* out, size_t outLen) {
    int64_t t = (int64_t)epochUtc + (int64_t)offsetMin * 60;
    int64_t days = t / 86400;
    int64_t secs = t % 86400;
    if (secs < 0) { secs += 86400; days -= 1; }
    int y; unsigned mo, d;
    civilFromDays(days, y, mo, d);
    snprintf(out, outLen, "%04d-%02u-%02u %02u:%02u:%02u", y, mo, d,
             (unsigned)(secs / 3600), (unsigned)(secs % 3600 / 60), (unsigned)(secs % 60));
}

// A reading taken before the clock was known stores only "seconds since
// boot". Once the clock is synced in the same boot, its real time is exact:
// syncEpoch - (syncMono - readingMono). Returns 0 if it cannot be worked out.
inline uint32_t backfillTs(uint32_t syncEpoch, uint32_t syncMonoS, uint32_t readingMonoS) {
    if (!syncEpoch || readingMonoS > syncMonoS) return 0;
    return syncEpoch - (syncMonoS - readingMonoS);
}

}  // namespace core
