#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include <Arduino.h>
#include "deviceControl.h"

// =============================================================================
//  powerControl.h — Deep-sleep duty-cycle engine
//
//  The node spends almost all of its time in deep sleep. On each wake it reads
//  the probe and appends the reading to a buffer held in RTC slow memory (which
//  survives deep sleep but not a power cycle / brownout). Every uploadInterval
//  it instead flushes the whole batch over WiFi/MQTT and listens briefly for
//  commands, then sleeps again. This matches the battery budget in the Hardware
//  Review (sample locally, radio on only in short bursts).
// =============================================================================

enum WakeReason : uint8_t {
    WAKE_COLD_BOOT,   // power-on / reset / brownout — RTC state was invalid
    WAKE_TIMER,       // scheduled duty-cycle wake
    WAKE_BUTTON,      // setup button (ext0) — operator wants the config portal
    WAKE_OTHER,       // some other wake source
};

// Compact form of a reading for RTC storage (epoch as 32-bit seconds).
struct RtcSample {
    uint32_t epoch;       // unix seconds (0 if RTC time was invalid when sampled)
    float    tempC;       // NAN if faulted
    float    internalC;   // cold-junction temperature
    uint8_t  fault;       // TcFault bitmask
};

// Classify this boot and (re)initialise RTC-persisted state. Call first in setup().
WakeReason powerInit();
const char* wakeReasonString(WakeReason r);

uint32_t powerBootCount();

// Whether NTP/RTC time has been validated at least once (persisted across sleep).
bool powerTimeWasValid();
void powerSetTimeValid(bool v);

// Append a reading to the RTC batch buffer (drops the oldest if full).
void powerRecordSample(const SensorReadings& s);

// True when the batch should be flushed: cold boot, time never synced, buffer
// full, or uploadInterval elapsed since the last successful upload.
bool powerUploadDue();

uint16_t         powerSampleCount();
const RtcSample& powerSampleAt(uint16_t i);

// Back-fill the timestamp of any buffered samples that were taken before the
// RTC clock was valid (i.e. the cold-boot sample). Call once NTP has synced.
void powerRestampUntimed(time_t now);

// Clear the buffer and stamp the last-upload time (call after a successful flush).
void powerMarkUploaded();

// Configure wake sources (timer + setup button) and enter deep sleep. Does not
// return. Pass the desired sleep duration in seconds.
void powerDeepSleep(uint32_t sleepSeconds);

#endif // POWER_CONTROL_H
