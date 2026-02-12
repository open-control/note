#include <unity.h>

#include <oc/note/clock/InternalClock.hpp>

using oc::note::clock::InternalClock;

void setUp() {}

void tearDown() {}

void test_internal_clock_smoke() {
    InternalClock clk;
    clk.setBpm(123.0f);
    clk.setPlaying(true);

    clk.update(1);
    clk.update(2);
    clk.update(2);

    TEST_ASSERT_TRUE(clk.isPlaying());
    TEST_ASSERT_EQUAL_UINT32(2, clk.tick());
    TEST_ASSERT_EQUAL_FLOAT(123.0f, clk.bpm());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_internal_clock_smoke);
    return UNITY_END();
}
