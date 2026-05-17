#include <unity.h>

#include <cstdint>

#include <oc/note/sequencer/StepSequencerVariation.hpp>

using oc::note::sequencer::StepSequencerResolvedVariation;
using oc::note::sequencer::StepSequencerStepValues;
using oc::note::sequencer::StepSequencerVariationRanges;
using oc::note::sequencer::resolveStepVariation;

namespace {

constexpr uint16_t MAX_GATE_PERCENT = 200;

StepSequencerResolvedVariation resolve(StepSequencerStepValues base,
                                        StepSequencerVariationRanges ranges,
                                        uint32_t seed = 11,
                                        uint32_t cycle = 3,
                                        uint8_t step = 5) {
    return resolveStepVariation(base, ranges, MAX_GATE_PERCENT, seed, cycle, step);
}

}  // namespace

void setUp() {}

void tearDown() {}

void test_zero_ranges_keep_base_values() {
    const auto out = resolve({.note = 60, .velocity = 96, .gate = 100, .nudge = -8}, {});

    TEST_ASSERT_EQUAL_UINT8(60, out.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(96, out.resolved.velocity);
    TEST_ASSERT_EQUAL_UINT16(100, out.resolved.gate);
    TEST_ASSERT_EQUAL_INT8(-8, out.resolved.nudge);
    TEST_ASSERT_EQUAL_INT8(0, out.pitchDelta);
    TEST_ASSERT_EQUAL_INT16(0, out.velocityDelta);
    TEST_ASSERT_EQUAL_INT16(0, out.gateDelta);
    TEST_ASSERT_EQUAL_INT8(0, out.nudgeDelta);
}

void test_deltas_stay_inside_requested_ranges() {
    const StepSequencerVariationRanges ranges{
        .pitchSemitones = 36,
        .velocity = 127,
        .gatePercent = 100,
        .nudge = 50,
    };

    for (uint32_t cycle = 0; cycle < 16; ++cycle) {
        for (uint8_t step = 0; step < 16; ++step) {
            const auto out = resolveStepVariation(
                {.note = 64, .velocity = 80, .gate = 100, .nudge = 0},
                ranges,
                MAX_GATE_PERCENT,
                31,
                cycle,
                step
            );
            TEST_ASSERT_GREATER_OR_EQUAL_INT8(-36, out.pitchDelta);
            TEST_ASSERT_LESS_OR_EQUAL_INT8(36, out.pitchDelta);
            TEST_ASSERT_GREATER_OR_EQUAL_INT16(-127, out.velocityDelta);
            TEST_ASSERT_LESS_OR_EQUAL_INT16(127, out.velocityDelta);
            TEST_ASSERT_GREATER_OR_EQUAL_INT16(-100, out.gateDelta);
            TEST_ASSERT_LESS_OR_EQUAL_INT16(100, out.gateDelta);
            TEST_ASSERT_GREATER_OR_EQUAL_INT8(-50, out.nudgeDelta);
            TEST_ASSERT_LESS_OR_EQUAL_INT8(50, out.nudgeDelta);
        }
    }
}

void test_resolved_values_are_clamped_to_runtime_ranges() {
    const StepSequencerVariationRanges ranges{
        .pitchSemitones = 36,
        .velocity = 127,
        .gatePercent = 100,
        .nudge = 50,
    };

    for (uint32_t cycle = 0; cycle < 32; ++cycle) {
        const auto high = resolveStepVariation(
            {.note = 127, .velocity = 127, .gate = 200, .nudge = 50},
            ranges,
            MAX_GATE_PERCENT,
            19,
            cycle,
            1
        );
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(127, high.resolved.note);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(127, high.resolved.velocity);
        TEST_ASSERT_LESS_OR_EQUAL_UINT16(MAX_GATE_PERCENT, high.resolved.gate);
        TEST_ASSERT_LESS_OR_EQUAL_INT8(50, high.resolved.nudge);

        const auto low = resolveStepVariation(
            {.note = 0, .velocity = 0, .gate = 0, .nudge = -50},
            ranges,
            MAX_GATE_PERCENT,
            23,
            cycle,
            2
        );
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(0, low.resolved.note);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(0, low.resolved.velocity);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT16(0, low.resolved.gate);
        TEST_ASSERT_GREATER_OR_EQUAL_INT8(-50, low.resolved.nudge);
    }
}

void test_resolution_is_deterministic_for_same_inputs() {
    const StepSequencerStepValues base{.note = 60, .velocity = 100, .gate = 100, .nudge = 0};
    const StepSequencerVariationRanges ranges{
        .pitchSemitones = 12,
        .velocity = 30,
        .gatePercent = 40,
        .nudge = 20,
    };

    const auto first = resolveStepVariation(base, ranges, MAX_GATE_PERCENT, 71, 9, 4);
    const auto second = resolveStepVariation(base, ranges, MAX_GATE_PERCENT, 71, 9, 4);

    TEST_ASSERT_EQUAL_INT8(first.pitchDelta, second.pitchDelta);
    TEST_ASSERT_EQUAL_INT16(first.velocityDelta, second.velocityDelta);
    TEST_ASSERT_EQUAL_INT16(first.gateDelta, second.gateDelta);
    TEST_ASSERT_EQUAL_INT8(first.nudgeDelta, second.nudgeDelta);
    TEST_ASSERT_EQUAL_UINT8(first.resolved.note, second.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(first.resolved.velocity, second.resolved.velocity);
    TEST_ASSERT_EQUAL_UINT16(first.resolved.gate, second.resolved.gate);
    TEST_ASSERT_EQUAL_INT8(first.resolved.nudge, second.resolved.nudge);
}

void test_untriggered_steps_keep_base_values_and_zero_deltas() {
    const StepSequencerVariationRanges ranges{
        .pitchSemitones = 12,
        .velocity = 30,
        .gatePercent = 40,
        .nudge = 20,
    };

    const auto out = resolveStepVariation(
        {.note = 60, .velocity = 100, .gate = 75, .nudge = -4},
        ranges,
        MAX_GATE_PERCENT,
        71,
        9,
        4,
        false
    );

    TEST_ASSERT_FALSE(out.triggered);
    TEST_ASSERT_EQUAL_UINT8(60, out.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(100, out.resolved.velocity);
    TEST_ASSERT_EQUAL_UINT16(75, out.resolved.gate);
    TEST_ASSERT_EQUAL_INT8(-4, out.resolved.nudge);
    TEST_ASSERT_EQUAL_INT8(0, out.pitchDelta);
    TEST_ASSERT_EQUAL_INT16(0, out.velocityDelta);
    TEST_ASSERT_EQUAL_INT16(0, out.gateDelta);
    TEST_ASSERT_EQUAL_INT8(0, out.nudgeDelta);
}

void test_range_inputs_are_clamped_before_resolution() {
    StepSequencerVariationRanges ranges{
        .pitchSemitones = 99,
        .velocity = 255,
        .gatePercent = 200,
        .nudge = 99,
    };

    const auto out = resolve({.note = 60, .velocity = 80, .gate = 100, .nudge = 0}, ranges);

    TEST_ASSERT_EQUAL_UINT8(36, out.ranges.pitchSemitones);
    TEST_ASSERT_EQUAL_UINT8(127, out.ranges.velocity);
    TEST_ASSERT_EQUAL_UINT8(100, out.ranges.gatePercent);
    TEST_ASSERT_EQUAL_UINT8(50, out.ranges.nudge);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_zero_ranges_keep_base_values);
    RUN_TEST(test_deltas_stay_inside_requested_ranges);
    RUN_TEST(test_resolved_values_are_clamped_to_runtime_ranges);
    RUN_TEST(test_resolution_is_deterministic_for_same_inputs);
    RUN_TEST(test_untriggered_steps_keep_base_values_and_zero_deltas);
    RUN_TEST(test_range_inputs_are_clamped_before_resolution);
    return UNITY_END();
}
