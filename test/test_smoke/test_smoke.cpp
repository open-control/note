#include <unity.h>

#include <oc/note/clock/InternalClock.hpp>

using oc::note::clock::InternalClock;

void setUp() {}

void tearDown() {}

void test_internal_clock_smoke() {
    InternalClock clk;
    clk.setBpm(125.0f);  // tick period = 20ms (PPQN=24)

    // Init while stopped
    clk.setPlaying(false);
    clk.update(0);

    // Start playback resets tick domain
    clk.setPlaying(true);
    clk.update(0);

    clk.update(20);
    clk.update(40);

    TEST_ASSERT_TRUE(clk.isPlaying());
    TEST_ASSERT_EQUAL_UINT32(2, clk.tick());
    TEST_ASSERT_EQUAL_FLOAT(125.0f, clk.bpm());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_internal_clock_smoke);
    return UNITY_END();
}
