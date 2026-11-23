// Host unit tests for the pure logic in coreLogic.h.  Run: pio test -e native
#include <unity.h>
#include <string>
#include "coreLogic.h"

void setUp(void) {}
void tearDown(void) {}

// helper: extract host as a std::string for easy comparison
static std::string hostOf(const char* url) {
    size_t s, l;
    if (!core::urlHostSpan(url, s, l)) return std::string();
    return std::string(url + s, l);
}

// ---- clampU32 --------------------------------------------------------------

void test_clamp_within(void) { TEST_ASSERT_EQUAL_UINT32(50,  core::clampU32(50, 10, 100)); }
void test_clamp_below(void)  { TEST_ASSERT_EQUAL_UINT32(10,  core::clampU32(5,  10, 100)); }
void test_clamp_above(void)  { TEST_ASSERT_EQUAL_UINT32(100, core::clampU32(150,10, 100)); }
void test_clamp_edges(void)  {
    TEST_ASSERT_EQUAL_UINT32(10,  core::clampU32(10,  10, 100));
    TEST_ASSERT_EQUAL_UINT32(100, core::clampU32(100, 10, 100));
}

// ---- ring buffer -----------------------------------------------------------

void test_ring_fill_in_order(void) {
    core::Ring r{0, 0, 3};
    TEST_ASSERT_EQUAL_UINT16(0, core::ringAppendSlot(r));
    TEST_ASSERT_EQUAL_UINT16(1, core::ringAppendSlot(r));
    TEST_ASSERT_EQUAL_UINT16(2, core::ringAppendSlot(r));
    TEST_ASSERT_EQUAL_UINT16(3, r.count);
    TEST_ASSERT_EQUAL_UINT16(0, r.head);
    TEST_ASSERT_EQUAL_UINT16(0, core::ringSlotAt(r, 0));   // oldest
    TEST_ASSERT_EQUAL_UINT16(1, core::ringSlotAt(r, 1));
    TEST_ASSERT_EQUAL_UINT16(2, core::ringSlotAt(r, 2));
}

void test_ring_overflow_drops_oldest(void) {
    core::Ring r{0, 0, 3};
    core::ringAppendSlot(r);            // slot 0
    core::ringAppendSlot(r);            // slot 1
    core::ringAppendSlot(r);            // slot 2 (now full)

    uint16_t slot = core::ringAppendSlot(r);   // overwrite oldest
    TEST_ASSERT_EQUAL_UINT16(0, slot);
    TEST_ASSERT_EQUAL_UINT16(1, r.head);
    TEST_ASSERT_EQUAL_UINT16(3, r.count);      // stays at capacity
    TEST_ASSERT_EQUAL_UINT16(1, core::ringSlotAt(r, 0));   // new oldest
    TEST_ASSERT_EQUAL_UINT16(2, core::ringSlotAt(r, 1));
    TEST_ASSERT_EQUAL_UINT16(0, core::ringSlotAt(r, 2));   // newest (just written)

    slot = core::ringAppendSlot(r);            // overwrite again
    TEST_ASSERT_EQUAL_UINT16(1, slot);
    TEST_ASSERT_EQUAL_UINT16(2, r.head);
    TEST_ASSERT_EQUAL_UINT16(3, r.count);
}

// ---- URL host parsing ------------------------------------------------------

void test_urlhost_http_path(void)   { TEST_ASSERT_EQUAL_STRING("example.com",  hostOf("http://example.com/fw.bin").c_str()); }
void test_urlhost_with_port(void)   { TEST_ASSERT_EQUAL_STRING("192.168.1.5",  hostOf("http://192.168.1.5:8000/firmware-1.0.7.bin").c_str()); }
void test_urlhost_https_no_path(void){ TEST_ASSERT_EQUAL_STRING("ota.example.org", hostOf("https://ota.example.org").c_str()); }
void test_urlhost_no_scheme(void)   { TEST_ASSERT_EQUAL_STRING("example.com",  hostOf("example.com/path").c_str()); }
void test_urlhost_bare_host(void)   { TEST_ASSERT_EQUAL_STRING("h",            hostOf("http://h").c_str()); }
void test_urlhost_empty(void)       { TEST_ASSERT_EQUAL_STRING("",             hostOf("").c_str()); }

// ---- host allowlist --------------------------------------------------------

void test_allow_empty_permits_any(void) { TEST_ASSERT_TRUE (core::hostMatchesAllow("a.example.com", "")); }
void test_allow_substring_match(void)   { TEST_ASSERT_TRUE (core::hostMatchesAllow("a.example.com", "example.com")); }
void test_allow_exact_ip(void)          { TEST_ASSERT_TRUE (core::hostMatchesAllow("192.168.1.5", "192.168.1.5")); }
void test_allow_rejects_other(void)     { TEST_ASSERT_FALSE(core::hostMatchesAllow("evil.com", "example.com")); }
void test_allow_null_host(void)         { TEST_ASSERT_FALSE(core::hostMatchesAllow(nullptr, "example.com")); }

// ---- port parsing (setup portal) -------------------------------------------

void test_port_valid(void) {
    uint16_t p = 0;
    TEST_ASSERT_TRUE(core::parsePort("1883", p));
    TEST_ASSERT_EQUAL_UINT16(1883, p);
}
void test_port_tls(void) {
    uint16_t p = 0;
    TEST_ASSERT_TRUE(core::parsePort("8883", p));
    TEST_ASSERT_EQUAL_UINT16(8883, p);
}
void test_port_boundaries(void) {
    uint16_t p = 0;
    TEST_ASSERT_TRUE(core::parsePort("1", p));     TEST_ASSERT_EQUAL_UINT16(1, p);
    TEST_ASSERT_TRUE(core::parsePort("65535", p)); TEST_ASSERT_EQUAL_UINT16(65535, p);
}
void test_port_leading_zeros(void) {
    uint16_t p = 0;
    TEST_ASSERT_TRUE(core::parsePort("0080", p));
    TEST_ASSERT_EQUAL_UINT16(80, p);
}
void test_port_rejects_and_preserves(void) {
    // On any rejection, parsePort must NOT touch the caller's value.
    uint16_t p = 1883;            // pretend this is the existing configured port
    TEST_ASSERT_FALSE(core::parsePort("", p));         // empty
    TEST_ASSERT_FALSE(core::parsePort("0", p));        // zero
    TEST_ASSERT_FALSE(core::parsePort("00000", p));    // zero (padded)
    TEST_ASSERT_FALSE(core::parsePort("8000x", p));    // trailing garbage
    TEST_ASSERT_FALSE(core::parsePort("x8000", p));    // leading garbage
    TEST_ASSERT_FALSE(core::parsePort(" 80", p));      // leading space
    TEST_ASSERT_FALSE(core::parsePort("-5", p));       // sign
    TEST_ASSERT_FALSE(core::parsePort("65536", p));    // out of range
    TEST_ASSERT_FALSE(core::parsePort("99999999", p)); // way out of range
    TEST_ASSERT_FALSE(core::parsePort(nullptr, p));    // null
    TEST_ASSERT_EQUAL_UINT16(1883, p);                 // unchanged throughout
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_clamp_within);
    RUN_TEST(test_clamp_below);
    RUN_TEST(test_clamp_above);
    RUN_TEST(test_clamp_edges);

    RUN_TEST(test_ring_fill_in_order);
    RUN_TEST(test_ring_overflow_drops_oldest);

    RUN_TEST(test_urlhost_http_path);
    RUN_TEST(test_urlhost_with_port);
    RUN_TEST(test_urlhost_https_no_path);
    RUN_TEST(test_urlhost_no_scheme);
    RUN_TEST(test_urlhost_bare_host);
    RUN_TEST(test_urlhost_empty);

    RUN_TEST(test_allow_empty_permits_any);
    RUN_TEST(test_allow_substring_match);
    RUN_TEST(test_allow_exact_ip);
    RUN_TEST(test_allow_rejects_other);
    RUN_TEST(test_allow_null_host);

    RUN_TEST(test_port_valid);
    RUN_TEST(test_port_tls);
    RUN_TEST(test_port_boundaries);
    RUN_TEST(test_port_leading_zeros);
    RUN_TEST(test_port_rejects_and_preserves);

    return UNITY_END();
}
