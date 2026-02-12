#include <unity.h>

#include <oc/note/clock/InternalClock.hpp>

using oc::note::clock::InternalClock;

void setUp() {}

void tearDown() {}

void test_internal_clock_stopped_does_not_advance() {
    InternalClock clk;
    clk.setBpm(125.0f);
    clk.setPlaying(false);

    clk.update(0);
    clk.update(100);

    TEST_ASSERT_EQUAL_UINT32(0, clk.tick());
}

void test_internal_clock_resets_on_start() {
    InternalClock clk;
    clk.setBpm(125.0f);  // tick period = 20ms

    // Initialize at t=0 while stopped
    clk.setPlaying(false);
    clk.update(0);

    // Start at t=40 -> reset tick
    clk.setPlaying(true);
    clk.update(40);
    TEST_ASSERT_EQUAL_UINT32(0, clk.tick());

    // After 20ms -> 1 tick
    clk.update(60);
    TEST_ASSERT_EQUAL_UINT32(1, clk.tick());

    // Stop does not change tick
    clk.setPlaying(false);
    clk.update(80);
    TEST_ASSERT_EQUAL_UINT32(1, clk.tick());

    // Restart resets again
    clk.setPlaying(true);
    clk.update(80);
    TEST_ASSERT_EQUAL_UINT32(0, clk.tick());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_internal_clock_stopped_does_not_advance);
    RUN_TEST(test_internal_clock_resets_on_start);
    return UNITY_END();
}
