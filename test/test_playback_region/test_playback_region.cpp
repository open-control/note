#include <unity.h>

#include <cstdint>

#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>

using oc::note::sequencer::StepSequencerPlaybackPosition;
using oc::note::sequencer::StepSequencerPlaybackRegion;
using oc::note::sequencer::StepSequencerPlaybackTickPosition;
using oc::note::sequencer::tryResolvePlaybackOrdinal;
using oc::note::sequencer::tryResolvePlaybackTick;

void setUp() {}
void tearDown() {}

void test_default_and_full_length_regions_are_valid() {
    const StepSequencerPlaybackRegion defaultRegion{};
    TEST_ASSERT_TRUE(defaultRegion.isValid());
    TEST_ASSERT_EQUAL_UINT8(8, defaultRegion.contentLength);
    TEST_ASSERT_EQUAL_UINT8(0, defaultRegion.playStart);
    TEST_ASSERT_EQUAL_UINT8(0, defaultRegion.loopStart);
    TEST_ASSERT_EQUAL_UINT8(8, defaultRegion.loopEnd);
    TEST_ASSERT_EQUAL_UINT8(0, defaultRegion.preludeLength());
    TEST_ASSERT_EQUAL_UINT8(8, defaultRegion.loopLength());

    const auto full = StepSequencerPlaybackRegion::fullLength(128);
    TEST_ASSERT_TRUE(full.isValid());
    TEST_ASSERT_EQUAL_UINT8(128, full.contentLength);
    TEST_ASSERT_EQUAL_UINT8(128, full.loopEnd);
}

void test_invalid_regions_are_rejected_by_exact_invariants() {
    const StepSequencerPlaybackRegion invalid[] = {
        {.contentLength = 0, .playStart = 0, .loopStart = 0, .loopEnd = 0},
        {.contentLength = 129, .playStart = 0, .loopStart = 0, .loopEnd = 1},
        {.contentLength = 8, .playStart = 5, .loopStart = 4, .loopEnd = 8},
        {.contentLength = 8, .playStart = 0, .loopStart = 4, .loopEnd = 4},
        {.contentLength = 8, .playStart = 0, .loopStart = 4, .loopEnd = 9},
    };

    for (const auto& region : invalid) {
        TEST_ASSERT_FALSE(region.isValid());
        TEST_ASSERT_EQUAL_UINT8(0, region.preludeLength());
        TEST_ASSERT_EQUAL_UINT8(0, region.loopLength());
    }
}

void test_invalid_resolution_never_partially_mutates_output() {
    const StepSequencerPlaybackRegion invalid{
        .contentLength = 8,
        .playStart = 6,
        .loopStart = 4,
        .loopEnd = 8,
    };
    StepSequencerPlaybackPosition ordinalOutput{
        .ordinal = 99,
        .loopCycleIndex = 98,
        .stepIndex = 97,
        .loopOffset = 96,
        .inPrelude = true,
        .atLoopStart = true,
    };
    TEST_ASSERT_FALSE(tryResolvePlaybackOrdinal(invalid, 0, ordinalOutput));
    TEST_ASSERT_EQUAL_UINT32(99, ordinalOutput.ordinal);
    TEST_ASSERT_EQUAL_UINT8(97, ordinalOutput.stepIndex);

    const auto valid = StepSequencerPlaybackRegion::fullLength(8);
    StepSequencerPlaybackTickPosition tickOutput{};
    tickOutput.stepStartTick = 123;
    tickOutput.nextStepTick = 456;
    TEST_ASSERT_FALSE(tryResolvePlaybackTick(valid, 42, 0, tickOutput));
    TEST_ASSERT_EQUAL_UINT64(123, tickOutput.stepStartTick);
    TEST_ASSERT_EQUAL_UINT64(456, tickOutput.nextStepTick);
}

void test_full_length_profile_matches_default_modulo_and_cycle_for_all_steps() {
    for (uint8_t length = 1; length <= 128; ++length) {
        const auto region = StepSequencerPlaybackRegion::fullLength(length);
        for (uint32_t ordinal = 0; ordinal < 512; ++ordinal) {
            StepSequencerPlaybackPosition position{};
            TEST_ASSERT_TRUE(tryResolvePlaybackOrdinal(region, ordinal, position));
            TEST_ASSERT_EQUAL_UINT32(ordinal, position.ordinal);
            TEST_ASSERT_EQUAL_UINT8(
                static_cast<uint8_t>(ordinal % static_cast<uint32_t>(length)),
                position.stepIndex
            );
            TEST_ASSERT_EQUAL_UINT32(
                ordinal / static_cast<uint32_t>(length),
                position.loopCycleIndex
            );
            TEST_ASSERT_FALSE(position.inPrelude);
            TEST_ASSERT_EQUAL_UINT8(position.stepIndex, position.loopOffset);
            TEST_ASSERT_EQUAL(ordinal % length == 0, position.atLoopStart);
        }
    }
}

void test_prelude_is_played_once_before_first_loop_and_later_cycles() {
    const StepSequencerPlaybackRegion region{
        .contentLength = 12,
        .playStart = 2,
        .loopStart = 5,
        .loopEnd = 9,
    };
    const uint8_t expectedStep[] = {2, 3, 4, 5, 6, 7, 8, 5, 6, 7, 8, 5};
    const uint32_t expectedCycle[] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2};

    for (uint32_t ordinal = 0; ordinal < 12; ++ordinal) {
        StepSequencerPlaybackPosition position{};
        TEST_ASSERT_TRUE(tryResolvePlaybackOrdinal(region, ordinal, position));
        TEST_ASSERT_EQUAL_UINT8(expectedStep[ordinal], position.stepIndex);
        TEST_ASSERT_EQUAL_UINT32(expectedCycle[ordinal], position.loopCycleIndex);
        TEST_ASSERT_EQUAL(ordinal < 3, position.inPrelude);
        TEST_ASSERT_EQUAL(ordinal == 3 || ordinal == 7 || ordinal == 11,
                          position.atLoopStart);
    }
}

void test_loop_can_start_immediately_at_nonzero_content_step() {
    const StepSequencerPlaybackRegion region{
        .contentLength = 16,
        .playStart = 4,
        .loopStart = 4,
        .loopEnd = 7,
    };
    const uint8_t expectedStep[] = {4, 5, 6, 4, 5, 6, 4};

    for (uint32_t ordinal = 0; ordinal < 7; ++ordinal) {
        StepSequencerPlaybackPosition position{};
        TEST_ASSERT_TRUE(tryResolvePlaybackOrdinal(region, ordinal, position));
        TEST_ASSERT_EQUAL_UINT8(expectedStep[ordinal], position.stepIndex);
        TEST_ASSERT_FALSE(position.inPrelude);
        TEST_ASSERT_EQUAL_UINT32(ordinal / 3U, position.loopCycleIndex);
    }
}

void test_tick_resolution_is_exact_inside_steps_and_at_loop_boundaries() {
    const StepSequencerPlaybackRegion region{
        .contentLength = 12,
        .playStart = 2,
        .loopStart = 5,
        .loopEnd = 9,
    };
    StepSequencerPlaybackTickPosition position{};

    TEST_ASSERT_TRUE(tryResolvePlaybackTick(region, 17, 6, position));
    TEST_ASSERT_EQUAL_UINT8(4, position.playback.stepIndex);
    TEST_ASSERT_TRUE(position.playback.inPrelude);
    TEST_ASSERT_EQUAL_UINT16(5, position.tickOffset);
    TEST_ASSERT_FALSE(position.atStepBoundary);
    TEST_ASSERT_EQUAL_UINT64(12, position.stepStartTick);
    TEST_ASSERT_EQUAL_UINT64(18, position.nextStepTick);

    TEST_ASSERT_TRUE(tryResolvePlaybackTick(region, 18, 6, position));
    TEST_ASSERT_EQUAL_UINT8(5, position.playback.stepIndex);
    TEST_ASSERT_EQUAL_UINT32(0, position.playback.loopCycleIndex);
    TEST_ASSERT_TRUE(position.playback.atLoopStart);
    TEST_ASSERT_TRUE(position.atStepBoundary);

    TEST_ASSERT_TRUE(tryResolvePlaybackTick(region, 41, 6, position));
    TEST_ASSERT_EQUAL_UINT8(8, position.playback.stepIndex);
    TEST_ASSERT_EQUAL_UINT32(0, position.playback.loopCycleIndex);
    TEST_ASSERT_EQUAL_UINT16(5, position.tickOffset);

    TEST_ASSERT_TRUE(tryResolvePlaybackTick(region, 42, 6, position));
    TEST_ASSERT_EQUAL_UINT8(5, position.playback.stepIndex);
    TEST_ASSERT_EQUAL_UINT32(1, position.playback.loopCycleIndex);
    TEST_ASSERT_TRUE(position.playback.atLoopStart);
    TEST_ASSERT_TRUE(position.atStepBoundary);
}

void test_arbitrary_tick_resync_preserves_phase_and_next_boundary() {
    const StepSequencerPlaybackRegion region{
        .contentLength = 128,
        .playStart = 7,
        .loopStart = 11,
        .loopEnd = 127,
    };
    StepSequencerPlaybackTickPosition position{};

    TEST_ASSERT_TRUE(tryResolvePlaybackTick(region, 1234567, 24, position));
    const uint32_t ordinal = 1234567U / 24U;
    const uint32_t loopOrdinal = ordinal - 4U;
    TEST_ASSERT_EQUAL_UINT32(ordinal, position.playback.ordinal);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(11U + (loopOrdinal % 116U)),
        position.playback.stepIndex
    );
    TEST_ASSERT_EQUAL_UINT32(loopOrdinal / 116U, position.playback.loopCycleIndex);
    TEST_ASSERT_EQUAL_UINT16(1234567U % 24U, position.tickOffset);
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(ordinal) * 24U,
                             position.stepStartTick);
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(ordinal + 1U) * 24U,
                             position.nextStepTick);
}

void test_tick_resolution_does_not_wrap_boundary_after_uint32_max() {
    const auto region = StepSequencerPlaybackRegion::fullLength(8);
    StepSequencerPlaybackTickPosition position{};

    TEST_ASSERT_TRUE(tryResolvePlaybackTick(region, UINT32_MAX, 1, position));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, position.playback.ordinal);
    TEST_ASSERT_EQUAL_UINT64(UINT32_MAX, position.stepStartTick);
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(UINT32_MAX) + 1U,
                             position.nextStepTick);
    TEST_ASSERT_EQUAL_UINT16(0, position.tickOffset);
    TEST_ASSERT_TRUE(position.atStepBoundary);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_and_full_length_regions_are_valid);
    RUN_TEST(test_invalid_regions_are_rejected_by_exact_invariants);
    RUN_TEST(test_invalid_resolution_never_partially_mutates_output);
    RUN_TEST(test_full_length_profile_matches_default_modulo_and_cycle_for_all_steps);
    RUN_TEST(test_prelude_is_played_once_before_first_loop_and_later_cycles);
    RUN_TEST(test_loop_can_start_immediately_at_nonzero_content_step);
    RUN_TEST(test_tick_resolution_is_exact_inside_steps_and_at_loop_boundaries);
    RUN_TEST(test_arbitrary_tick_resync_preserves_phase_and_next_boundary);
    RUN_TEST(test_tick_resolution_does_not_wrap_boundary_after_uint32_max);
    return UNITY_END();
}
