#include "powerControl.h"
#include "configControl.h"
#include "appConfig.h"
#include "coreLogic.h"

#include <time.h>
#include <math.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// ---- RTC-persisted state (survives deep sleep, lost on power cycle/brownout) -
#define RTC_MAGIC 0x42494F43UL   // 'BIOC'

RTC_DATA_ATTR static uint32_t  rtcMagic            = 0;
RTC_DATA_ATTR static uint32_t  rtcBootCount        = 0;
RTC_DATA_ATTR static uint32_t  rtcLastUploadEpoch  = 0;
RTC_DATA_ATTR static bool      rtcTimeValid        = false;
RTC_DATA_ATTR static uint16_t  rtcSampleHead       = 0;   // index of oldest sample
RTC_DATA_ATTR static uint16_t  rtcSampleCount      = 0;   // number of valid samples
RTC_DATA_ATTR static RtcSample rtcSamples[RTC_SAMPLE_CAPACITY];

// Set true on cold boot so callers can special-case the first cycle.
static bool s_coldBoot = false;

// ---- init ------------------------------------------------------------------

WakeReason powerInit() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (rtcMagic != RTC_MAGIC) {
        // Cold boot (or RTC RAM lost to a power cut / brownout): reset state.
        rtcMagic           = RTC_MAGIC;
        rtcBootCount       = 0;
        rtcLastUploadEpoch = 0;
        rtcTimeValid       = false;
        rtcSampleHead      = 0;
        rtcSampleCount     = 0;
        s_coldBoot         = true;
        rtcBootCount++;
        return WAKE_COLD_BOOT;
    }

    rtcBootCount++;
    s_coldBoot = false;

    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER: return WAKE_TIMER;
        case ESP_SLEEP_WAKEUP_EXT0:  return WAKE_BUTTON;
        default:                     return WAKE_OTHER;
    }
}

const char* wakeReasonString(WakeReason r) {
    switch (r) {
        case WAKE_COLD_BOOT: return "COLD_BOOT";
        case WAKE_TIMER:     return "TIMER";
        case WAKE_BUTTON:    return "BUTTON";
        default:             return "OTHER";
    }
}

uint32_t powerBootCount()      { return rtcBootCount; }
bool     powerTimeWasValid()   { return rtcTimeValid; }
void     powerSetTimeValid(bool v) { rtcTimeValid = v; }

// ---- sample buffer (ring) --------------------------------------------------

void powerRecordSample(const SensorReadings& s) {
    RtcSample r;
    r.epoch     = (s.epoch >= (time_t)EPOCH_VALID_THRESHOLD) ? (uint32_t)s.epoch : 0;
    r.tempC     = s.tempC;
    r.internalC = s.internalC;
    r.fault     = s.fault;

    bool wasFull = (rtcSampleCount >= RTC_SAMPLE_CAPACITY);

    core::Ring ring{ rtcSampleHead, rtcSampleCount, RTC_SAMPLE_CAPACITY };
    uint16_t slot = core::ringAppendSlot(ring);
    rtcSampleHead  = ring.head;
    rtcSampleCount = ring.count;

    rtcSamples[slot] = r;
    if (wasFull) Serial.println("[PWR] RTC sample buffer full — dropped oldest reading");
}

uint16_t powerSampleCount() { return rtcSampleCount; }

const RtcSample& powerSampleAt(uint16_t i) {
    core::Ring ring{ rtcSampleHead, rtcSampleCount, RTC_SAMPLE_CAPACITY };
    return rtcSamples[ core::ringSlotAt(ring, i) ];
}

bool powerUploadDue() {
    if (s_coldBoot)        return true;   // first cycle: sync time + announce
    if (!rtcTimeValid)     return true;   // must get NTP before timestamps mean anything
    if (rtcSampleCount >= RTC_SAMPLE_CAPACITY) return true;   // avoid data loss

    time_t now = time(nullptr);
    if (now < (time_t)EPOCH_VALID_THRESHOLD) return true;     // time looks wrong; resync

    return (uint32_t)now - rtcLastUploadEpoch >= cfg.uploadIntervalS;
}

void powerRestampUntimed(time_t now) {
    if (now < (time_t)EPOCH_VALID_THRESHOLD) return;
    for (uint16_t i = 0; i < rtcSampleCount; i++) {
        uint16_t idx = (rtcSampleHead + i) % RTC_SAMPLE_CAPACITY;
        if (rtcSamples[idx].epoch == 0) rtcSamples[idx].epoch = (uint32_t)now;
    }
}

void powerMarkUploaded() {
    time_t now = time(nullptr);
    if (now >= (time_t)EPOCH_VALID_THRESHOLD) {
        rtcLastUploadEpoch = (uint32_t)now;
    }
    rtcSampleHead  = 0;
    rtcSampleCount = 0;
}

// ---- deep sleep ------------------------------------------------------------

void powerDeepSleep(uint32_t sleepSeconds) {
    if (sleepSeconds < 1) sleepSeconds = 1;

    Serial.printf("[PWR] Entering deep sleep for %u s (buffered samples: %u)\n",
                  sleepSeconds, rtcSampleCount);

    // Timer wake for the duty cycle.
    esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);

    // Setup-button wake (ext0, active-low). GPIO4 is RTC-capable. Hold the
    // internal pull-up through sleep so the idle pad reads HIGH and a press
    // pulls it LOW to wake.
    gpio_num_t btn = (gpio_num_t)PIN_SETUP_BTN;
    rtc_gpio_pullup_en(btn);
    rtc_gpio_pulldown_dis(btn);
    esp_sleep_enable_ext0_wakeup(btn, 0);   // wake on level 0 (LOW)

    Serial.flush();
    esp_deep_sleep_start();   // does not return
}
