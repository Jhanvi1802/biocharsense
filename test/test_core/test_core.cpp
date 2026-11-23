// Host unit tests for lib/core/core.h.  Run: pio test -e native
#include <unity.h>
#include "core.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// 4-20 mA conversion (100 ohm shunt, 0-200 C transmitter)

static LoopReading conv(float mA, float rMax = 200.0f) {
    return convertLoop(mA * 100.0f / 1000.0f, 100.0f, 0.0f, rMax);
}

void test_conv_endpoints(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, conv(4.0f).tempC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, conv(20.0f).tempC);
    TEST_ASSERT_EQUAL_UINT8(FAULT_OK, conv(4.0f).fault);
    TEST_ASSERT_EQUAL_UINT8(FAULT_OK, conv(20.0f).fault);
}
void test_conv_midpoint(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, conv(12.0f).tempC);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 600.0f, conv(12.0f, 1200.0f).tempC);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, conv(12.0f).mA);
}
void test_conv_loop_open(void) {
    LoopReading r = conv(0.0f);
    TEST_ASSERT_EQUAL_UINT8(FAULT_LOOP_OPEN, r.fault);
    TEST_ASSERT_TRUE(std::isnan(r.tempC));
    TEST_ASSERT_EQUAL_UINT8(FAULT_LOOP_OPEN, conv(3.5f).fault);
}
void test_conv_under_over_range(void) {
    LoopReading u = conv(3.7f);
    TEST_ASSERT_EQUAL_UINT8(FAULT_UNDER_RANGE, u.fault);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, u.tempC);          // clamped
    LoopReading o = conv(20.8f);
    TEST_ASSERT_EQUAL_UINT8(FAULT_OVER_RANGE, o.fault);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, o.tempC);        // clamped
}
void test_conv_sensor_break(void) {
    LoopReading r = conv(22.0f);
    TEST_ASSERT_EQUAL_UINT8(FAULT_SENSOR_BREAK, r.fault);
    TEST_ASSERT_TRUE(std::isnan(r.tempC));
}
void test_conv_bad_inputs(void) {
    TEST_ASSERT_EQUAL_UINT8(FAULT_NO_ADC, convertLoop(NAN, 100.0f, 0, 200).fault);
    TEST_ASSERT_EQUAL_UINT8(FAULT_NO_ADC, convertLoop(1.0f, 0.0f, 0, 200).fault);
    TEST_ASSERT_EQUAL_UINT8(FAULT_NO_ADC, convertLoop(1.0f, 100.0f, 200, 200).fault);
}
void test_conv_real_shunt_value(void) {
    // Calibrated shunt of 99.6 ohm: 12 mA gives 1.1952 V, still exactly mid-range.
    LoopReading r = convertLoop(0.0120f * 99.6f, 99.6f, 0.0f, 1200.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 600.0f, r.tempC);
}

// battery percentage

void test_liion_percent(void) {
    TEST_ASSERT_EQUAL_UINT8(100, liionPercent(4.25f));
    TEST_ASSERT_EQUAL_UINT8(100, liionPercent(4.20f));
    TEST_ASSERT_EQUAL_UINT8(50, liionPercent(3.80f));
    TEST_ASSERT_EQUAL_UINT8(0, liionPercent(3.20f));
    TEST_ASSERT_EQUAL_UINT8(0, liionPercent(NAN));
    uint8_t mid = liionPercent(3.85f);
    TEST_ASSERT_TRUE(mid > 50 && mid < 65);
}

// ring buffer

void test_ring_append_and_ack(void) {
    RingSeq r; r.capacity = 4;
    TEST_ASSERT_EQUAL_UINT32(0, r.append());
    TEST_ASSERT_EQUAL_UINT32(1, r.append());
    TEST_ASSERT_EQUAL_UINT32(2, r.append());
    TEST_ASSERT_EQUAL_UINT32(3, r.count());
    r.ackThrough(1);
    TEST_ASSERT_EQUAL_UINT32(2, r.head);
    TEST_ASSERT_EQUAL_UINT32(1, r.count());
    r.ackThrough(0);                       // stale ack is ignored
    TEST_ASSERT_EQUAL_UINT32(2, r.head);
    r.ackThrough(99);                      // ack beyond tail is ignored
    TEST_ASSERT_EQUAL_UINT32(2, r.head);
}
void test_ring_overflow_drops_oldest(void) {
    RingSeq r; r.capacity = 3;
    for (int i = 0; i < 5; i++) r.append();   // seqs 0..4, capacity 3
    TEST_ASSERT_EQUAL_UINT32(3, r.count());
    TEST_ASSERT_EQUAL_UINT32(2, r.head);
    TEST_ASSERT_EQUAL_UINT32(2, r.dropped);
    TEST_ASSERT_EQUAL_UINT32(2, r.oldest());
    TEST_ASSERT_EQUAL_UINT32(1, r.slotOf(4));
}
void test_ring_rewind(void) {
    RingSeq r; r.capacity = 10;
    for (int i = 0; i < 8; i++) r.append();
    r.ackThrough(7);                          // everything sent
    TEST_ASSERT_EQUAL_UINT32(0, r.count());
    TEST_ASSERT_EQUAL_UINT32(5, r.rewind(5)); // resend 5..7
    TEST_ASSERT_EQUAL_UINT32(3, r.count());
    for (int i = 0; i < 10; i++) r.append(); // tail 18, oldest 8
    TEST_ASSERT_EQUAL_UINT32(8, r.rewind(0)); // clamped to what is still stored
}

// cycle detection

void test_cycle_starts_after_confirmation(void) {
    CycleDetector d; d.startC = 100; d.endC = 80; d.endHoldS = 600; d.confirmN = 3;
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(150, true, 0));
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(150, true, 60));
    TEST_ASSERT_EQUAL(CycleDetector::STARTED, d.update(150, true, 120));
    TEST_ASSERT_TRUE(d.active);
}
void test_cycle_spike_does_not_start(void) {
    CycleDetector d; d.confirmN = 3;
    d.update(150, true, 0);
    d.update(150, true, 60);
    d.update(50, true, 120);                  // drops back: counter resets
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(150, true, 180));
    TEST_ASSERT_FALSE(d.active);
}
void test_cycle_ends_after_hold(void) {
    CycleDetector d; d.endC = 80; d.endHoldS = 600;
    d.forceStart();
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(70, true, 1000));
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(90, true, 1300));   // warms again: timer resets
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(70, true, 1400));
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(70, true, 1900));
    TEST_ASSERT_EQUAL(CycleDetector::ENDED, d.update(70, true, 2000));
    TEST_ASSERT_FALSE(d.active);
}
void test_cycle_ignores_faults(void) {
    CycleDetector d; d.confirmN = 1;
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(NAN, false, 0));
    TEST_ASSERT_FALSE(d.active);
    d.forceStart();
    TEST_ASSERT_EQUAL(CycleDetector::NONE, d.update(NAN, false, 99999));
    TEST_ASSERT_TRUE(d.active);
}

// time

void test_cclk_ist(void) {
    uint32_t t = 0;
    // 21 Oct 2026 14:00:00 IST (+22 quarter hours) = 08:30:00 UTC
    TEST_ASSERT_TRUE(parseCclk("26/10/21,14:00:00+22", t));
    TEST_ASSERT_EQUAL_UINT32(1792571400UL, t);
}
void test_cclk_utc_and_negative(void) {
    uint32_t t = 0;
    TEST_ASSERT_TRUE(parseCclk("24/01/01,00:00:00+00", t));
    TEST_ASSERT_EQUAL_UINT32(1704067200UL, t);
    TEST_ASSERT_TRUE(parseCclk("24/01/01,00:00:00-04", t));   // UTC-1h
    TEST_ASSERT_EQUAL_UINT32(1704067200UL + 3600, t);
}
void test_cclk_rejects_unset_clock(void) {
    uint32_t t = 0;
    TEST_ASSERT_FALSE(parseCclk("80/01/06,00:00:04+00", t));
    TEST_ASSERT_FALSE(parseCclk("70/01/01,00:00:00+00", t));
    TEST_ASSERT_FALSE(parseCclk("garbage", t));
    TEST_ASSERT_FALSE(parseCclk(nullptr, t));
}
void test_format_iso(void) {
    char buf[32];
    formatIso(1792571400UL, 330, buf, sizeof buf);           // 08:30 UTC = 14:00 IST
    TEST_ASSERT_EQUAL_STRING("2026-10-21 14:00:00", buf);
    formatIso(1704067200UL, 0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("2024-01-01 00:00:00", buf);
    formatIso(1709164800UL + 86399, 330, buf, sizeof buf);    // 29 Feb 2024 23:59:59 UTC (leap day)
    TEST_ASSERT_EQUAL_STRING("2024-03-01 05:29:59", buf);
    formatIso(1704067200UL, -240, buf, sizeof buf);           // negative offset
    TEST_ASSERT_EQUAL_STRING("2023-12-31 20:00:00", buf);
    // Round trip with the clock parser for every hour of a year.
    for (uint32_t t = 1704067200UL; t < 1704067200UL + 366UL * 86400; t += 3600) {
        formatIso(t, 0, buf, sizeof buf);
        uint32_t back = 0;
        char cclk[32];
        snprintf(cclk, sizeof cclk, "%.2s/%.2s/%.2s,%.8s+00", buf + 2, buf + 5, buf + 8, buf + 11);
        TEST_ASSERT_TRUE(parseCclk(cclk, back));
        TEST_ASSERT_EQUAL_UINT32(t, back);
    }
}

void test_backfill(void) {
    TEST_ASSERT_EQUAL_UINT32(1000, backfillTs(1600, 700, 100));   // 600 s before sync
    TEST_ASSERT_EQUAL_UINT32(0, backfillTs(1600, 700, 800));      // after the sync point
    TEST_ASSERT_EQUAL_UINT32(0, backfillTs(0, 700, 100));         // no sync yet
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_conv_endpoints);
    RUN_TEST(test_conv_midpoint);
    RUN_TEST(test_conv_loop_open);
    RUN_TEST(test_conv_under_over_range);
    RUN_TEST(test_conv_sensor_break);
    RUN_TEST(test_conv_bad_inputs);
    RUN_TEST(test_conv_real_shunt_value);
    RUN_TEST(test_liion_percent);
    RUN_TEST(test_ring_append_and_ack);
    RUN_TEST(test_ring_overflow_drops_oldest);
    RUN_TEST(test_ring_rewind);
    RUN_TEST(test_cycle_starts_after_confirmation);
    RUN_TEST(test_cycle_spike_does_not_start);
    RUN_TEST(test_cycle_ends_after_hold);
    RUN_TEST(test_cycle_ignores_faults);
    RUN_TEST(test_cclk_ist);
    RUN_TEST(test_cclk_utc_and_negative);
    RUN_TEST(test_cclk_rejects_unset_clock);
    RUN_TEST(test_format_iso);
    RUN_TEST(test_backfill);
    return UNITY_END();
}
