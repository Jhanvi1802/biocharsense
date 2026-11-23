#ifndef CORE_LOGIC_H
#define CORE_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// =============================================================================
//  coreLogic.h — Pure, dependency-free helpers (no Arduino/ESP headers).
//
//  The trickiest, most bug-prone logic (RTC ring-buffer wrap, interval clamping,
//  OTA URL host parsing + allowlist) lives here so it can be unit-tested on the
//  host (see test/test_core). The firmware modules call these; the algorithm is
//  defined once and verified independently of hardware.
// =============================================================================

namespace core {

// Clamp v into [lo, hi].
inline uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Fixed-capacity ring buffer bookkeeping. `head` indexes the oldest element,
// `count` is the number of valid elements. The actual element storage is owned
// by the caller (e.g. an RTC_DATA_ATTR array).
struct Ring {
    uint16_t head;
    uint16_t count;
    uint16_t capacity;
};

// Return the slot to write the next element into, updating the ring. When full,
// the oldest element is overwritten (head advances) and count stays at capacity.
inline uint16_t ringAppendSlot(Ring& r) {
    uint16_t slot = (uint16_t)((r.head + r.count) % r.capacity);
    if (r.count < r.capacity) {
        r.count++;
    } else {
        r.head = (uint16_t)((r.head + 1) % r.capacity);
    }
    return slot;
}

// Slot of the i-th oldest element (0 == oldest). Caller ensures i < count.
inline uint16_t ringSlotAt(const Ring& r, uint16_t i) {
    return (uint16_t)((r.head + i) % r.capacity);
}

// Locate the host span of a URL: the text between "://" (or the start, if no
// scheme) and the next ':' or '/'. Writes [start, start+len) into the outputs.
// Returns false for a null/empty URL.
inline bool urlHostSpan(const char* url, size_t& start, size_t& len) {
    if (!url || !*url) { start = 0; len = 0; return false; }
    size_t n = strlen(url);
    const char* sep = strstr(url, "://");
    size_t s = sep ? (size_t)(sep - url) + 3 : 0;
    size_t e = n;
    for (size_t i = s; i < n; i++) {
        if (url[i] == '/' || url[i] == ':') { e = i; break; }
    }
    start = s;
    len   = (e >= s) ? (e - s) : 0;
    return true;
}

// OTA host allowlist check: an empty allow string permits any host; otherwise
// the allow string must appear as a substring of host.
inline bool hostMatchesAllow(const char* host, const char* allow) {
    if (!allow || !*allow) return true;
    if (!host) return false;
    return strstr(host, allow) != nullptr;
}

// Strictly parse a TCP port from a string entered in the setup portal. Accepts
// only all-digit strings that resolve to 1..65535. Rejects empty, non-numeric,
// trailing-garbage ("8000x"), zero, and out-of-range. On success writes `out`
// and returns true; otherwise leaves `out` untouched and returns false (the
// caller then keeps the existing port rather than corrupting it).
inline bool parsePort(const char* s, uint16_t& out) {
    if (!s || !*s) return false;
    long v = 0;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return false;   // non-digit anywhere -> reject
        v = v * 10 + (*p - '0');
        if (v > 65535) return false;               // out of range (also caps growth)
    }
    if (v < 1) return false;
    out = (uint16_t)v;
    return true;
}

} // namespace core

#endif // CORE_LOGIC_H
