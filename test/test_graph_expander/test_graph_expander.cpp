#include <unity.h>

#include <cstdint>

#include <oc/note/sequencer/StepSequencerExpander.hpp>

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
using oc::note::sequencer::STEP_NODE_ENABLED_VALUE;
using oc::note::sequencer::STEP_NODE_GATE_OFFSET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::STEP_NODE_NUDGE_OFFSET;
using oc::note::sequencer::STEP_NODE_PROBABILITY_OFFSET;
using oc::note::sequencer::STEP_NODE_VELOCITY_OFFSET;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::StepSequencerExpander;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerRuntimeState;
using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerVariationRanges;

namespace {

StepSequencerRuntimeState baseState() {
    StepSequencerRuntimeState state;
    state.length = 4;
    state.stepsPerBeat = 4;
    state.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    state.note[0] = 60;
    state.velocity[0] = 96;
    state.gate[0] = 100;
    state.nudge[0] = 0;
    state.probability[0] = 100;
    return state;
}

StepSequencerGraph graphWithRoot(uint8_t rootLength = 4) {
    StepSequencerGraph graph;
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 1;
    graph.stepNodeCount = rootLength;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = rootLength;
    return graph;
}

void attachSequence(StepSequencerGraph& graph,
                    uint16_t parentNode,
                    uint8_t sequenceId,
                    uint16_t firstStep,
                    uint8_t length) {
    graph.stepNodes[parentNode].flags |= STEP_NODE_CHILD_SEQUENCE;
    graph.stepNodes[parentNode].childSequenceId = sequenceId;
    if (graph.sequenceCount <= sequenceId) {
        graph.sequenceCount = static_cast<uint8_t>(sequenceId + 1U);
    }
    graph.sequences[sequenceId].kind = StepSequencerSequenceKind::MicroSequence;
    graph.sequences[sequenceId].firstStepNode = firstStep;
    graph.sequences[sequenceId].length = length;
    const uint16_t end = static_cast<uint16_t>(firstStep + length);
    if (graph.stepNodeCount < end) {
        graph.stepNodeCount = end;
    }
}

}  // namespace

void setUp() {}

void tearDown() {}

void test_micro_sequence_expands_inside_parent_step() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 3);
    graph.stepNodes[4].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[4].noteOffset = 0;
    graph.stepNodes[5].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[5].noteOffset = 2;
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 4;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT32(0, out.notes[0].localTick);
    TEST_ASSERT_EQUAL_UINT32(2, out.notes[1].localTick);
    TEST_ASSERT_EQUAL_UINT32(4, out.notes[2].localTick);
    TEST_ASSERT_EQUAL_UINT16(2, out.notes[0].spanTicks);
    TEST_ASSERT_EQUAL_UINT8(60, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(62, out.notes[1].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(64, out.notes[2].variation.resolved.note);
}

void test_micro_sequence_positive_offset_moves_content_right() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 4);
    graph.sequences[1].offset = 1;
    for (uint8_t i = 0; i < 4; ++i) {
        graph.stepNodes[4 + i].flags = STEP_NODE_NOTE_OFFSET;
        graph.stepNodes[4 + i].noteOffset = static_cast<int8_t>(i);
    }

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 8, 1, true);

    TEST_ASSERT_EQUAL_UINT8(4, out.count);
    TEST_ASSERT_EQUAL_UINT8(63, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(60, out.notes[1].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(61, out.notes[2].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(62, out.notes[3].variation.resolved.note);
}

void test_micro_sequence_negative_offset_moves_content_left() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 4);
    graph.sequences[1].offset = -1;
    for (uint8_t i = 0; i < 4; ++i) {
        graph.stepNodes[4 + i].flags = STEP_NODE_NOTE_OFFSET;
        graph.stepNodes[4 + i].noteOffset = static_cast<int8_t>(i);
    }

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 8, 1, true);

    TEST_ASSERT_EQUAL_UINT8(4, out.count);
    TEST_ASSERT_EQUAL_UINT8(61, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(62, out.notes[1].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(63, out.notes[2].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(60, out.notes[3].variation.resolved.note);
}

void test_micro_sequence_uses_effective_parent_gate_span() {
    auto state = baseState();
    state.gate[0] = 150;
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 3);

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 12, 1, true);

    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT32(0, out.notes[0].localTick);
    TEST_ASSERT_EQUAL_UINT32(6, out.notes[1].localTick);
    TEST_ASSERT_EQUAL_UINT32(12, out.notes[2].localTick);
    TEST_ASSERT_EQUAL_UINT16(6, out.notes[0].spanTicks);
    TEST_ASSERT_EQUAL_UINT16(6, out.notes[1].spanTicks);
    TEST_ASSERT_EQUAL_UINT16(6, out.notes[2].spanTicks);
    TEST_ASSERT_EQUAL_UINT16(100, out.notes[0].variation.resolved.gate);
    TEST_ASSERT_EQUAL_UINT16(100, out.notes[1].variation.resolved.gate);
    TEST_ASSERT_EQUAL_UINT16(100, out.notes[2].variation.resolved.gate);
}

void test_note_offset_is_semitone_in_free_scale() {
    auto state = baseState();
    state.scaleSettings = StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::Free,
    };
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[4].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[4].noteOffset = 2;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(62, out.notes[0].variation.resolved.note);
    TEST_ASSERT_FALSE(out.notes[0].variation.pitchVariationUsesScaleDegrees);
}

void test_note_offset_is_scale_degree_in_constrained_scale() {
    auto state = baseState();
    state.scaleSettings = StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[4].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[4].noteOffset = 2;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(64, out.notes[0].variation.resolved.note);
    TEST_ASSERT_TRUE(out.notes[0].variation.pitchVariationUsesScaleDegrees);
}

void test_cycle_state_overrides_parent_values() {
    auto state = baseState();
    auto graph = graphWithRoot();
    graph.cycleSetCount = 1;
    graph.cycleSets[0].firstStateNode = 4;
    graph.cycleSets[0].length = 2;
    graph.stepNodeCount = 6;
    graph.stepNodes[0].flags = STEP_NODE_CYCLE_SET;
    graph.stepNodes[0].cycleSetId = 0;
    graph.stepNodes[5].flags =
        STEP_NODE_NOTE_OFFSET | STEP_NODE_VELOCITY_OFFSET | STEP_NODE_GATE_OFFSET |
        STEP_NODE_NUDGE_OFFSET;
    graph.stepNodes[5].noteOffset = 7;
    graph.stepNodes[5].velocityOffset = -16;
    graph.stepNodes[5].gateOffset = -25;
    graph.stepNodes[5].nudgeOffset = 10;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(67, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(80, out.notes[0].variation.resolved.velocity);
    TEST_ASSERT_EQUAL_UINT16(75, out.notes[0].variation.resolved.gate);
    TEST_ASSERT_EQUAL_INT8(10, out.notes[0].variation.resolved.nudge);
}

void test_inactive_cycle_state_mutes_step_for_cycle() {
    auto state = baseState();
    auto graph = graphWithRoot();
    graph.cycleSetCount = 1;
    graph.cycleSets[0].firstStateNode = 4;
    graph.cycleSets[0].length = 2;
    graph.stepNodeCount = 6;
    graph.stepNodes[0].flags = STEP_NODE_CYCLE_SET;
    graph.stepNodes[0].cycleSetId = 0;
    graph.stepNodes[5].flags = STEP_NODE_ENABLED_OVERRIDE;

    const auto muted = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);
    const auto active = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(0, muted.count);
    TEST_ASSERT_EQUAL_UINT8(1, active.count);
}

void test_cycle_state_can_own_micro_sequence() {
    auto state = baseState();
    auto graph = graphWithRoot();
    graph.cycleSetCount = 1;
    graph.cycleSets[0].firstStateNode = 4;
    graph.cycleSets[0].length = 1;
    graph.stepNodeCount = 5;
    graph.stepNodes[0].flags = STEP_NODE_CYCLE_SET;
    graph.stepNodes[0].cycleSetId = 0;
    attachSequence(graph, 4, 1, 5, 2);
    graph.stepNodes[5].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[5].noteOffset = 0;
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 12;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(2, out.count);
    TEST_ASSERT_EQUAL_UINT8(60, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(72, out.notes[1].variation.resolved.note);
}

void test_micro_sequence_step_can_own_cycle_state() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[4].flags = STEP_NODE_CYCLE_SET;
    graph.stepNodes[4].cycleSetId = 0;
    graph.cycleSetCount = 1;
    graph.cycleSets[0].firstStateNode = 5;
    graph.cycleSets[0].length = 2;
    graph.stepNodeCount = 7;
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 5;

    const auto cycle0 = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);
    const auto cycle1 = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, cycle0.count);
    TEST_ASSERT_EQUAL_UINT8(1, cycle1.count);
    TEST_ASSERT_EQUAL_UINT8(60, cycle0.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(65, cycle1.notes[0].variation.resolved.note);
}

void test_depth_limit_stops_before_unbounded_nesting() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    attachSequence(graph, 4, 2, 5, 1);
    attachSequence(graph, 5, 3, 6, 1);
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 12;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_TRUE(out.depthLimitReached);
    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(60, out.notes[0].variation.resolved.note);
}

void test_note_budget_is_bounded() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(
        graph,
        0,
        1,
        4,
        static_cast<uint8_t>(StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP + 1U)
    );

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 17, 1, true);

    TEST_ASSERT_TRUE(out.noteBudgetExceeded);
    TEST_ASSERT_EQUAL_UINT8(StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP, out.count);
}

void test_scale_and_variation_apply_after_expansion() {
    auto state = baseState();
    state.note[0] = 61;
    state.variationRanges = StepSequencerVariationRanges{
        .pitchSemitones = 0,
        .velocity = 0,
        .gatePercent = 0,
        .nudge = 0,
    };
    state.scaleSettings = StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    auto graph = graphWithRoot();

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(62, out.notes[0].variation.resolved.note);
    TEST_ASSERT_TRUE(out.notes[0].variation.scale.constrained);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_micro_sequence_expands_inside_parent_step);
    RUN_TEST(test_micro_sequence_positive_offset_moves_content_right);
    RUN_TEST(test_micro_sequence_negative_offset_moves_content_left);
    RUN_TEST(test_micro_sequence_uses_effective_parent_gate_span);
    RUN_TEST(test_note_offset_is_semitone_in_free_scale);
    RUN_TEST(test_note_offset_is_scale_degree_in_constrained_scale);
    RUN_TEST(test_cycle_state_overrides_parent_values);
    RUN_TEST(test_inactive_cycle_state_mutes_step_for_cycle);
    RUN_TEST(test_cycle_state_can_own_micro_sequence);
    RUN_TEST(test_micro_sequence_step_can_own_cycle_state);
    RUN_TEST(test_depth_limit_stops_before_unbounded_nesting);
    RUN_TEST(test_note_budget_is_bounded);
    RUN_TEST(test_scale_and_variation_apply_after_expansion);
    return UNITY_END();
}
