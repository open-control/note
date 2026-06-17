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
using oc::note::sequencer::StepSequencerStepValues;
using oc::note::sequencer::StepSequencerVariationRanges;
using oc::note::sequencer::resolveStepVariation;

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

uint32_t mixVariationIdentityForTest(uint32_t parent,
                                     uint32_t kindSalt,
                                     uint8_t index,
                                     uint8_t depth) {
    uint32_t x = parent ^ (kindSalt * 2654435761u);
    x ^= static_cast<uint32_t>(index) * 2246822519u;
    x ^= static_cast<uint32_t>(depth) * 3266489917u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
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

void attachCycleSet(StepSequencerGraph& graph,
                    uint16_t parentNode,
                    uint8_t cycleSetId,
                    uint16_t firstState,
                    uint8_t length) {
    graph.stepNodes[parentNode].flags |= STEP_NODE_CYCLE_SET;
    graph.stepNodes[parentNode].cycleSetId = cycleSetId;
    if (graph.cycleSetCount <= cycleSetId) {
        graph.cycleSetCount = static_cast<uint8_t>(cycleSetId + 1U);
    }
    graph.cycleSets[cycleSetId].firstStateNode = firstState;
    graph.cycleSets[cycleSetId].length = length;
    const uint16_t end = static_cast<uint16_t>(firstState + length);
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

void test_micro_sequence_step_overrides_full_step_properties() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[4].flags =
        STEP_NODE_NOTE_OFFSET | STEP_NODE_VELOCITY_OFFSET | STEP_NODE_GATE_OFFSET |
        STEP_NODE_NUDGE_OFFSET;
    graph.stepNodes[4].noteOffset = 7;
    graph.stepNodes[4].velocityOffset = -20;
    graph.stepNodes[4].gateOffset = 25;
    graph.stepNodes[4].nudgeOffset = -12;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(67, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(76, out.notes[0].variation.resolved.velocity);
    TEST_ASSERT_EQUAL_UINT16(125, out.notes[0].variation.resolved.gate);
    TEST_ASSERT_EQUAL_INT8(-12, out.notes[0].variation.resolved.nudge);
}

void test_micro_sequence_step_can_mute_with_enabled_override() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[4].flags = STEP_NODE_ENABLED_OVERRIDE;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(0, out.count);
}

void test_micro_sequence_probability_offset_can_mute_child_step() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[4].flags = STEP_NODE_PROBABILITY_OFFSET;
    graph.stepNodes[4].probabilityOffset = -100;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(0, out.count);
}

void test_micro_sequence_can_own_micro_sequence() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 2);
    attachSequence(graph, 4, 2, 6, 2);
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 0;
    graph.stepNodes[7].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[7].noteOffset = 7;
    graph.stepNodes[5].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[5].noteOffset = 12;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 8, 1, true);

    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT32(0, out.notes[0].localTick);
    TEST_ASSERT_EQUAL_UINT32(2, out.notes[1].localTick);
    TEST_ASSERT_EQUAL_UINT32(4, out.notes[2].localTick);
    TEST_ASSERT_EQUAL_UINT8(60, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(67, out.notes[1].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(72, out.notes[2].variation.resolved.note);
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

void test_micro_sequence_pitch_offset_resolves_from_current_cycle_state_scale_degree() {
    auto state = baseState();
    state.scaleSettings = StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[4].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[4].noteOffset = 1;
    attachCycleSet(graph, 0, 0, 5, 2);
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 2;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(65, out.notes[0].variation.resolved.note);
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

void test_cycle_state_probability_offset_can_mute_step() {
    auto state = baseState();
    auto graph = graphWithRoot();
    graph.cycleSetCount = 1;
    graph.cycleSets[0].firstStateNode = 4;
    graph.cycleSets[0].length = 2;
    graph.stepNodeCount = 6;
    graph.stepNodes[0].flags = STEP_NODE_CYCLE_SET;
    graph.stepNodes[0].cycleSetId = 0;
    graph.stepNodes[5].flags = STEP_NODE_PROBABILITY_OFFSET;
    graph.stepNodes[5].probabilityOffset = -100;

    const auto muted = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);
    const auto active = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(0, muted.count);
    TEST_ASSERT_EQUAL_UINT8(1, active.count);
}

void test_cycle_state_offset_moves_content_right() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachCycleSet(graph, 0, 0, 4, 4);
    graph.cycleSets[0].offset = 1;
    graph.stepNodes[7].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[7].noteOffset = 7;

    const auto wrapped = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);
    const auto next = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, wrapped.count);
    TEST_ASSERT_EQUAL_UINT8(67, wrapped.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(1, next.count);
    TEST_ASSERT_EQUAL_UINT8(60, next.notes[0].variation.resolved.note);
}

void test_cycle_state_set_over_limit_is_ignored_safely() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachCycleSet(
        graph,
        0,
        0,
        4,
        static_cast<uint8_t>(StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET + 1U)
    );
    graph.stepNodes[4].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[4].noteOffset = 12;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(60, out.notes[0].variation.resolved.note);
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

void test_cycle_state_can_own_cycle_state_with_owner_activation_count() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachCycleSet(graph, 0, 0, 4, 3);
    attachCycleSet(graph, 5, 1, 7, 5);
    for (uint8_t i = 0; i < 5; ++i) {
        graph.stepNodes[7 + i].flags = STEP_NODE_NOTE_OFFSET;
        graph.stepNodes[7 + i].noteOffset = static_cast<int8_t>(i);
    }

    const auto firstOwnerPass =
        StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);
    const auto secondOwnerPass =
        StepSequencerExpander::expandRootStep(state, graph, 0, 4, 6, 1, true);
    const auto thirdOwnerPass =
        StepSequencerExpander::expandRootStep(state, graph, 0, 7, 6, 1, true);
    const auto nonOwnerPass =
        StepSequencerExpander::expandRootStep(state, graph, 0, 2, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, firstOwnerPass.count);
    TEST_ASSERT_EQUAL_UINT8(1, secondOwnerPass.count);
    TEST_ASSERT_EQUAL_UINT8(1, thirdOwnerPass.count);
    TEST_ASSERT_EQUAL_UINT8(1, nonOwnerPass.count);
    TEST_ASSERT_EQUAL_UINT8(60, firstOwnerPass.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(61, secondOwnerPass.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(62, thirdOwnerPass.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(60, nonOwnerPass.notes[0].variation.resolved.note);
}

void test_state_owned_cycle_state_replaces_same_level_micro_sequence() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 2);
    graph.stepNodes[4].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[4].noteOffset = 0;
    graph.stepNodes[5].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[5].noteOffset = 12;
    attachCycleSet(graph, 0, 0, 6, 2);
    attachCycleSet(graph, 7, 1, 8, 1);
    graph.stepNodes[8].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[8].noteOffset = 5;

    const auto defaultPass = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 8, 1, true);
    const auto stateOwnedPass = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 8, 1, true);

    TEST_ASSERT_EQUAL_UINT8(2, defaultPass.count);
    TEST_ASSERT_EQUAL_UINT8(60, defaultPass.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(72, defaultPass.notes[1].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(1, stateOwnedPass.count);
    TEST_ASSERT_EQUAL_UINT8(65, stateOwnedPass.notes[0].variation.resolved.note);
}

void test_state_owned_micro_sequence_child_cycle_uses_owner_activation_count() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachCycleSet(graph, 0, 0, 4, 3);
    attachSequence(graph, 5, 1, 7, 1);
    attachCycleSet(graph, 7, 1, 8, 5);
    for (uint8_t i = 0; i < 5; ++i) {
        graph.stepNodes[8 + i].flags = STEP_NODE_NOTE_OFFSET;
        graph.stepNodes[8 + i].noteOffset = static_cast<int8_t>(i);
    }

    const auto firstOwnerPass =
        StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, 1, true);
    const auto secondOwnerPass =
        StepSequencerExpander::expandRootStep(state, graph, 0, 4, 6, 1, true);
    const auto thirdOwnerPass =
        StepSequencerExpander::expandRootStep(state, graph, 0, 7, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, firstOwnerPass.count);
    TEST_ASSERT_EQUAL_UINT8(1, secondOwnerPass.count);
    TEST_ASSERT_EQUAL_UINT8(1, thirdOwnerPass.count);
    TEST_ASSERT_EQUAL_UINT8(60, firstOwnerPass.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(61, secondOwnerPass.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(62, thirdOwnerPass.notes[0].variation.resolved.note);
}

void test_same_level_default_micro_sequence_keeps_parent_cycle_index() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    attachCycleSet(graph, 4, 1, 5, 5);
    for (uint8_t i = 0; i < 5; ++i) {
        graph.stepNodes[5 + i].flags = STEP_NODE_NOTE_OFFSET;
        graph.stepNodes[5 + i].noteOffset = static_cast<int8_t>(i);
    }
    attachCycleSet(graph, 0, 0, 10, 3);
    graph.stepNodes[11].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[11].noteOffset = 10;

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 4, 6, 1, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(74, out.notes[0].variation.resolved.note);
}

void test_depth_limit_stops_before_unbounded_nesting() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    attachSequence(graph, 4, 2, 5, 1);
    attachSequence(graph, 5, 3, 6, 1);
    attachSequence(graph, 6, 4, 7, 1);
    graph.stepNodes[7].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[7].noteOffset = 12;

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

void test_local_variation_combines_with_global_ranges() {
    auto state = baseState();
    state.variationRanges = StepSequencerVariationRanges{
        .pitchSemitones = 34,
        .velocity = 120,
        .gatePercent = 90,
        .nudge = 48,
    };
    auto graph = graphWithRoot();
    graph.stepNodes[0].localVariation = StepSequencerVariationRanges{
        .pitchSemitones = 6,
        .velocity = 10,
        .gatePercent = 15,
        .nudge = 9,
    };

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, 9, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(36, out.notes[0].variation.ranges.pitchSemitones);
    TEST_ASSERT_EQUAL_UINT8(127, out.notes[0].variation.ranges.velocity);
    TEST_ASSERT_EQUAL_UINT8(100, out.notes[0].variation.ranges.gatePercent);
    TEST_ASSERT_EQUAL_UINT8(50, out.notes[0].variation.ranges.nudge);
}

void test_parent_local_variation_inherits_into_micro_child_base() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 1);
    graph.stepNodes[0].localVariation.pitchSemitones = 6;
    graph.stepNodes[4].localVariation.velocity = 22;

    constexpr uint32_t kRunSeed = 1;
    const auto expectedParent = resolveStepVariation(
        StepSequencerStepValues{
            .note = 60,
            .velocity = 96,
            .gate = 100,
            .nudge = 0,
        },
        StepSequencerVariationRanges{
            .pitchSemitones = 6,
            .velocity = 0,
            .gatePercent = 0,
            .nudge = 0,
        },
        state.scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        kRunSeed,
        0,
        0,
        true,
        0
    );

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 0, 6, kRunSeed, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(expectedParent.resolved.note, out.notes[0].variation.base.note);
    TEST_ASSERT_EQUAL_UINT8(expectedParent.resolved.note, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(0, out.notes[0].variation.ranges.pitchSemitones);
    TEST_ASSERT_EQUAL_UINT8(22, out.notes[0].variation.ranges.velocity);
    TEST_ASSERT_EQUAL_UINT8(0, out.notes[0].variation.ranges.gatePercent);
    TEST_ASSERT_EQUAL_UINT8(0, out.notes[0].variation.ranges.nudge);
}

void test_parent_local_variation_inherits_into_cycle_state_base() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachCycleSet(graph, 0, 0, 4, 2);
    graph.stepNodes[0].localVariation.pitchSemitones = 6;
    graph.stepNodes[5].localVariation.gatePercent = 12;

    constexpr uint32_t kRunSeed = 1;
    const auto expectedParent = resolveStepVariation(
        StepSequencerStepValues{
            .note = 60,
            .velocity = 96,
            .gate = 100,
            .nudge = 0,
        },
        StepSequencerVariationRanges{
            .pitchSemitones = 6,
            .velocity = 0,
            .gatePercent = 0,
            .nudge = 0,
        },
        state.scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        kRunSeed,
        1,
        0,
        true,
        0
    );

    const auto out = StepSequencerExpander::expandRootStep(state, graph, 0, 1, 6, kRunSeed, true);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(expectedParent.resolved.note, out.notes[0].variation.base.note);
    TEST_ASSERT_EQUAL_UINT8(expectedParent.resolved.note, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(0, out.notes[0].variation.ranges.pitchSemitones);
    TEST_ASSERT_EQUAL_UINT8(0, out.notes[0].variation.ranges.velocity);
    TEST_ASSERT_EQUAL_UINT8(12, out.notes[0].variation.ranges.gatePercent);
    TEST_ASSERT_EQUAL_UINT8(0, out.notes[0].variation.ranges.nudge);
}

void test_cycle_state_local_variation_sets_micro_sequence_pitch_origin() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachCycleSet(graph, 0, 0, 4, 2);
    attachSequence(graph, 5, 1, 6, 2);
    graph.stepNodes[5].localVariation.pitchSemitones = 6;
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 0;
    graph.stepNodes[7].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[7].noteOffset = 2;

    constexpr uint32_t kRunSeed = 7;
    constexpr uint32_t kCycleIndex = 1;
    const uint32_t stateIdentity =
        mixVariationIdentityForTest(0, 0x4359434Cu, 1, 0);
    const auto expectedParent = resolveStepVariation(
        StepSequencerStepValues{
            .note = 60,
            .velocity = 96,
            .gate = 100,
            .nudge = 0,
        },
        StepSequencerVariationRanges{
            .pitchSemitones = 6,
            .velocity = 0,
            .gatePercent = 0,
            .nudge = 0,
        },
        state.scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        kRunSeed,
        kCycleIndex,
        0,
        true,
        stateIdentity
    );

    const auto out = StepSequencerExpander::expandRootStep(
        state,
        graph,
        0,
        kCycleIndex,
        8,
        kRunSeed,
        true
    );

    TEST_ASSERT_EQUAL_UINT8(2, out.count);
    TEST_ASSERT_EQUAL_UINT8(expectedParent.resolved.note, out.notes[0].variation.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expectedParent.resolved.note + 2U),
        out.notes[1].variation.resolved.note
    );
}

void test_micro_sequence_steps_use_distinct_variation_identity() {
    auto state = baseState();
    auto graph = graphWithRoot();
    attachSequence(graph, 0, 1, 4, 2);
    graph.stepNodes[4].localVariation.pitchSemitones = 12;
    graph.stepNodes[5].localVariation.pitchSemitones = 12;

    constexpr uint32_t kRunSeed = 1;
    const auto out = StepSequencerExpander::expandRootStep(
        state,
        graph,
        0,
        0,
        8,
        kRunSeed,
        true
    );

    const StepSequencerStepValues expectedBase{
        .note = 60,
        .velocity = 96,
        .gate = StepSequencerRuntimeState::DEFAULT_GATE_PERCENT,
        .nudge = 0,
    };
    const StepSequencerVariationRanges expectedRanges{
        .pitchSemitones = 12,
        .velocity = 0,
        .gatePercent = 0,
        .nudge = 0,
    };
    const auto firstExpected = resolveStepVariation(
        expectedBase,
        expectedRanges,
        state.scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        kRunSeed,
        0,
        0,
        true,
        mixVariationIdentityForTest(0, 0x4D494352u, 0, 1)
    );
    const auto secondExpected = resolveStepVariation(
        expectedBase,
        expectedRanges,
        state.scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        kRunSeed,
        0,
        0,
        true,
        mixVariationIdentityForTest(0, 0x4D494352u, 1, 1)
    );

    TEST_ASSERT_EQUAL_UINT8(2, out.count);
    TEST_ASSERT_TRUE(firstExpected.pitchDelta != secondExpected.pitchDelta);
    TEST_ASSERT_EQUAL_INT8(firstExpected.pitchDelta, out.notes[0].variation.pitchDelta);
    TEST_ASSERT_EQUAL_INT8(secondExpected.pitchDelta, out.notes[1].variation.pitchDelta);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_micro_sequence_expands_inside_parent_step);
    RUN_TEST(test_micro_sequence_positive_offset_moves_content_right);
    RUN_TEST(test_micro_sequence_negative_offset_moves_content_left);
    RUN_TEST(test_micro_sequence_uses_effective_parent_gate_span);
    RUN_TEST(test_micro_sequence_step_overrides_full_step_properties);
    RUN_TEST(test_micro_sequence_step_can_mute_with_enabled_override);
    RUN_TEST(test_micro_sequence_probability_offset_can_mute_child_step);
    RUN_TEST(test_micro_sequence_can_own_micro_sequence);
    RUN_TEST(test_note_offset_is_semitone_in_free_scale);
    RUN_TEST(test_note_offset_is_scale_degree_in_constrained_scale);
    RUN_TEST(test_micro_sequence_pitch_offset_resolves_from_current_cycle_state_scale_degree);
    RUN_TEST(test_cycle_state_overrides_parent_values);
    RUN_TEST(test_inactive_cycle_state_mutes_step_for_cycle);
    RUN_TEST(test_cycle_state_probability_offset_can_mute_step);
    RUN_TEST(test_cycle_state_offset_moves_content_right);
    RUN_TEST(test_cycle_state_set_over_limit_is_ignored_safely);
    RUN_TEST(test_cycle_state_can_own_micro_sequence);
    RUN_TEST(test_micro_sequence_step_can_own_cycle_state);
    RUN_TEST(test_cycle_state_can_own_cycle_state_with_owner_activation_count);
    RUN_TEST(test_state_owned_cycle_state_replaces_same_level_micro_sequence);
    RUN_TEST(test_state_owned_micro_sequence_child_cycle_uses_owner_activation_count);
    RUN_TEST(test_same_level_default_micro_sequence_keeps_parent_cycle_index);
    RUN_TEST(test_depth_limit_stops_before_unbounded_nesting);
    RUN_TEST(test_note_budget_is_bounded);
    RUN_TEST(test_scale_and_variation_apply_after_expansion);
    RUN_TEST(test_local_variation_combines_with_global_ranges);
    RUN_TEST(test_parent_local_variation_inherits_into_micro_child_base);
    RUN_TEST(test_parent_local_variation_inherits_into_cycle_state_base);
    RUN_TEST(test_cycle_state_local_variation_sets_micro_sequence_pitch_origin);
    RUN_TEST(test_micro_sequence_steps_use_distinct_variation_identity);
    return UNITY_END();
}
