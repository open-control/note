#include <unity.h>

#include <cstdint>
#include <vector>

#include <oc/note/sequencer/SequencerEvent.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>
#include <oc/note/sequencer/StepSequencerVariation.hpp>

using oc::note::sequencer::ISequencerEventSink;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;
using oc::note::sequencer::StepSequencerEngine;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;
using oc::note::sequencer::StepSequencerStepValues;
using oc::note::sequencer::StepSequencerRuntimeState;
using oc::note::sequencer::StepSequencerVariationRanges;
using oc::note::sequencer::resolveStepVariation;

namespace {

class MockEventSink final : public ISequencerEventSink {
public:
    std::vector<SequencerEvent> events;

    bool emitSequencerEvent(const SequencerEvent& event) override {
        events.push_back(event);
        return true;
    }
};

int countType(const std::vector<SequencerEvent>& events, SequencerEventType type) {
    int count = 0;
    for (const auto& e : events) {
        if (e.type == type) ++count;
    }
    return count;
}

const SequencerEvent* firstEventOfType(const std::vector<SequencerEvent>& events,
                                       SequencerEventType type) {
    for (const auto& e : events) {
        if (e.type == type) return &e;
    }
    return nullptr;
}

}  // namespace

void setUp() {}

void tearDown() {}

void test_gate_zero_mutes_note() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 0;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);

    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(0, st.playheadStep);
}

void test_velocity_zero_is_sent() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 0;
    st.gate[0] = 50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);

    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(0, sink.events[0].channel);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);
    TEST_ASSERT_EQUAL_UINT8(0, sink.events[0].velocity);
}

void test_note_off_follows_gate_percent() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));

    eng.update(2, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));

    eng.update(3, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL_UINT8(0, sink.events[1].channel);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[1].note);
    TEST_ASSERT_EQUAL_UINT8(0, sink.events[1].velocity);
}

void test_boundary_order_note_off_before_next_step() {
    StepSequencerRuntimeState st;
    st.length = 2;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 0) | (1ULL << 1));
    st.note[0] = 60;
    st.note[1] = 62;
    st.velocity[0] = 100;
    st.velocity[1] = 100;
    st.gate[0] = 100;
    st.gate[1] = 100;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(3, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[2].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[1].note);
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[2].note);
}

void test_playhead_tick_position_tracks_offset_inside_current_step() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(0, st.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(0, st.playheadStepTickOffset);
    TEST_ASSERT_EQUAL_UINT16(6, st.playheadStepTicks);

    eng.update(2, true);
    TEST_ASSERT_EQUAL(0, st.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(2, st.playheadStepTickOffset);
    TEST_ASSERT_EQUAL_UINT16(6, st.playheadStepTicks);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(1, st.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(0, st.playheadStepTickOffset);
    TEST_ASSERT_EQUAL_UINT16(6, st.playheadStepTicks);

    eng.update(6, false);
    TEST_ASSERT_EQUAL(-1, st.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(0, st.playheadStepTickOffset);
}

void test_positive_nudge_delays_note_on_and_note_off() {
    StepSequencerRuntimeState st;
    st.length = 2;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 50;
    st.nudge[0] = 50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(0, st.playheadStep);

    eng.update(2, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));

    eng.update(3, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);

    eng.update(5, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));

    eng.update(6, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[1].note);
}

void test_negative_nudge_triggers_before_quantized_boundary() {
    StepSequencerRuntimeState st;
    st.length = 2;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 1);
    st.note[1] = 62;
    st.velocity[1] = 100;
    st.gate[1] = 50;
    st.nudge[1] = -50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(0, st.playheadStep);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));

    eng.update(2, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(0, st.playheadStep);

    eng.update(3, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[0].note);
    TEST_ASSERT_EQUAL(0, st.playheadStep);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[1].note);
    TEST_ASSERT_EQUAL(1, st.playheadStep);
}

void test_note_off_stays_before_next_note_on_when_nudged() {
    StepSequencerRuntimeState st;
    st.length = 2;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 0) | (1ULL << 1));
    st.note[0] = 60;
    st.note[1] = 62;
    st.velocity[0] = 100;
    st.velocity[1] = 100;
    st.gate[0] = 50;
    st.gate[1] = 50;
    st.nudge[0] = 50;
    st.nudge[1] = 0;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));

    eng.update(3, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(3, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[2].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[1].note);
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[2].note);
}

void test_stop_calls_all_notes_off_once() {
    StepSequencerRuntimeState st;
    st.length = 2;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 100;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));

    eng.update(1, false);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(1, countType(sink.events, SequencerEventType::AllNotesOff));
    TEST_ASSERT_EQUAL(-1, st.playheadStep);

    eng.update(2, false);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(1, countType(sink.events, SequencerEventType::AllNotesOff));
}

void test_variation_changes_scheduled_note_velocity_gate_and_telemetry() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 96;
    st.gate[0] = 100;
    st.nudge[0] = 0;
    st.variationRanges = StepSequencerVariationRanges{
        .pitchSemitones = 12,
        .velocity = 32,
        .gatePercent = 50,
        .nudge = 0,
    };

    const auto expected = resolveStepVariation(
        StepSequencerStepValues{.note = 60, .velocity = 96, .gate = 100, .nudge = 0},
        st.variationRanges,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        1,
        0,
        0
    );

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    const auto telemetry = st.lastResolvedVariation;
    const auto cycleTelemetry = st.cycleVariationTelemetry;
    const uint32_t telemetryRevision = st.variationTelemetryRevision;
    eng.update(12, true);

    const auto* on = firstEventOfType(sink.events, SequencerEventType::NoteOn);
    const auto* off = firstEventOfType(sink.events, SequencerEventType::NoteOff);
    TEST_ASSERT_NOT_NULL(on);
    TEST_ASSERT_NOT_NULL(off);
    TEST_ASSERT_EQUAL_UINT8(expected.resolved.note, on->note);
    TEST_ASSERT_EQUAL_UINT8(expected.resolved.velocity, on->velocity);
    TEST_ASSERT_EQUAL_UINT8(expected.resolved.note, off->note);
    TEST_ASSERT_EQUAL_UINT32(0, on->tick);
    TEST_ASSERT_EQUAL_UINT32(
        (static_cast<uint32_t>(expected.resolved.gate) * 6U) / 100U,
        off->tick
    );

    TEST_ASSERT_EQUAL_UINT32(1, telemetryRevision);
    TEST_ASSERT_TRUE(telemetry.triggered);
    TEST_ASSERT_EQUAL_UINT8(0, telemetry.stepIndex);
    TEST_ASSERT_EQUAL_UINT8(expected.resolved.note, telemetry.resolved.note);
    TEST_ASSERT_EQUAL_UINT8(expected.resolved.velocity, telemetry.resolved.velocity);
    TEST_ASSERT_EQUAL_UINT16(expected.resolved.gate, telemetry.resolved.gate);
    TEST_ASSERT_EQUAL_UINT32(0, cycleTelemetry.cycleIndex);
    TEST_ASSERT_TRUE(cycleTelemetry.validMask.test(0));
    TEST_ASSERT_TRUE(cycleTelemetry.triggeredMask.test(0));
    TEST_ASSERT_EQUAL_UINT8(expected.resolved.note, cycleTelemetry.resolvedNote[0]);
    TEST_ASSERT_EQUAL_UINT8(expected.resolved.velocity, cycleTelemetry.resolvedVelocity[0]);
    TEST_ASSERT_EQUAL_UINT16(expected.resolved.gate, cycleTelemetry.resolvedGate[0]);
    TEST_ASSERT_EQUAL_INT8(expected.pitchDelta, cycleTelemetry.pitchDelta[0]);
    TEST_ASSERT_EQUAL_INT16(expected.velocityDelta, cycleTelemetry.velocityDelta[0]);
    TEST_ASSERT_EQUAL_INT16(expected.gateDelta, cycleTelemetry.gateDelta[0]);
}

void test_variation_changes_scheduled_nudge() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 96;
    st.gate[0] = 50;
    st.nudge[0] = 0;
    st.variationRanges = StepSequencerVariationRanges{
        .pitchSemitones = 0,
        .velocity = 0,
        .gatePercent = 0,
        .nudge = 50,
    };

    const auto expected = resolveStepVariation(
        StepSequencerStepValues{.note = 60, .velocity = 96, .gate = 50, .nudge = 0},
        st.variationRanges,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        1,
        0,
        0
    );
    int32_t expectedOnTick = (static_cast<int32_t>(expected.resolved.nudge) * 6 + 50) / 100;
    if (expected.resolved.nudge < 0) {
        expectedOnTick = -((((-static_cast<int32_t>(expected.resolved.nudge)) * 6) + 50) / 100);
    }
    if (expectedOnTick < 0) {
        expectedOnTick = 0;
    }

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    const auto telemetry = st.lastResolvedVariation;
    eng.update(12, true);

    const auto* on = firstEventOfType(sink.events, SequencerEventType::NoteOn);
    TEST_ASSERT_NOT_NULL(on);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expectedOnTick), on->tick);
    TEST_ASSERT_EQUAL_INT8(expected.resolved.nudge, telemetry.resolved.nudge);
}

void test_cycle_variation_telemetry_can_be_disabled_and_reenabled_mid_cycle() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 96;
    st.gate[0] = 100;
    st.variationRanges = StepSequencerVariationRanges{
        .pitchSemitones = 12,
        .velocity = 32,
        .gatePercent = 50,
        .nudge = 0,
    };
    st.variationTelemetryEnabled = false;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL_UINT32(0, st.variationTelemetryRevision);
    TEST_ASSERT_FALSE(st.cycleVariationTelemetry.validMask.test(0));

    st.variationTelemetryEnabled = true;
    eng.update(6, true);
    TEST_ASSERT_EQUAL_UINT32(1, st.variationTelemetryRevision);
    TEST_ASSERT_TRUE(st.cycleVariationTelemetry.validMask.test(0));
}

void test_scale_constraint_changes_scheduled_note_and_telemetry() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 61;  // C# in C major, nearest tie resolves upward to D.
    st.velocity[0] = 100;
    st.gate[0] = 50;
    st.scaleSettings = StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);

    const auto* on = firstEventOfType(sink.events, SequencerEventType::NoteOn);
    TEST_ASSERT_NOT_NULL(on);
    TEST_ASSERT_EQUAL_UINT8(62, on->note);

    TEST_ASSERT_TRUE(st.lastResolvedVariation.triggered);
    TEST_ASSERT_EQUAL_UINT8(61, st.lastResolvedVariation.scale.inputNote);
    TEST_ASSERT_EQUAL_UINT8(62, st.lastResolvedVariation.scale.outputNote);
    TEST_ASSERT_FALSE(st.lastResolvedVariation.scale.inputInScale);
    TEST_ASSERT_TRUE(st.lastResolvedVariation.scale.constrained);
    TEST_ASSERT_TRUE(st.cycleVariationTelemetry.validMask.test(0));
    TEST_ASSERT_FALSE(st.cycleVariationTelemetry.scaleInMask.test(0));
    TEST_ASSERT_TRUE(st.cycleVariationTelemetry.scaleConstrainedMask.test(0));
    TEST_ASSERT_EQUAL_UINT8(62, st.cycleVariationTelemetry.resolvedNote[0]);
}

void test_free_scale_reports_out_of_scale_without_changing_scheduled_note() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 61;
    st.velocity[0] = 100;
    st.gate[0] = 50;
    st.scaleSettings = StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::Free,
    };

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);

    const auto* on = firstEventOfType(sink.events, SequencerEventType::NoteOn);
    TEST_ASSERT_NOT_NULL(on);
    TEST_ASSERT_EQUAL_UINT8(61, on->note);

    TEST_ASSERT_FALSE(st.lastResolvedVariation.scale.inputInScale);
    TEST_ASSERT_FALSE(st.lastResolvedVariation.scale.constrained);
    TEST_ASSERT_FALSE(st.cycleVariationTelemetry.scaleInMask.test(0));
    TEST_ASSERT_FALSE(st.cycleVariationTelemetry.scaleConstrainedMask.test(0));
    TEST_ASSERT_EQUAL_UINT8(61, st.cycleVariationTelemetry.resolvedNote[0]);
}

void test_graph_micro_sequence_schedules_multiple_notes_inside_step() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 100;
    st.probability[0] = 100;

    StepSequencerGraph graph;
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 2;
    graph.stepNodeCount = 7;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = 4;
    graph.sequences[1].kind = StepSequencerSequenceKind::MicroSequence;
    graph.sequences[1].firstStepNode = 4;
    graph.sequences[1].length = 3;
    graph.stepNodes[0].flags = STEP_NODE_CHILD_SEQUENCE;
    graph.stepNodes[0].childSequenceId = 1;
    graph.stepNodes[4].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[4].noteOffset = 0;
    graph.stepNodes[5].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[5].noteOffset = 2;
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 4;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    eng.setGraph(&graph);

    eng.update(0, true);
    eng.update(2, true);
    eng.update(4, true);

    TEST_ASSERT_EQUAL(3, countType(sink.events, SequencerEventType::NoteOn));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[2].note);
    TEST_ASSERT_EQUAL_UINT8(64, sink.events[4].note);
}

void test_graph_cycle_state_is_applied_by_engine_scheduler() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 100;
    st.probability[0] = 100;

    StepSequencerGraph graph;
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 1;
    graph.cycleSetCount = 1;
    graph.stepNodeCount = 6;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = 4;
    graph.cycleSets[0].firstStateNode = 4;
    graph.cycleSets[0].length = 2;
    graph.stepNodes[0].flags = STEP_NODE_CYCLE_SET;
    graph.stepNodes[0].cycleSetId = 0;
    graph.stepNodes[5].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[5].noteOffset = 7;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    eng.setGraph(&graph);

    eng.update(0, true);
    eng.update(24, true);

    TEST_ASSERT_EQUAL(2, countType(sink.events, SequencerEventType::NoteOn));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);
    TEST_ASSERT_EQUAL_UINT8(67, sink.events[2].note);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_gate_zero_mutes_note);
    RUN_TEST(test_velocity_zero_is_sent);
    RUN_TEST(test_note_off_follows_gate_percent);
    RUN_TEST(test_boundary_order_note_off_before_next_step);
    RUN_TEST(test_playhead_tick_position_tracks_offset_inside_current_step);
    RUN_TEST(test_positive_nudge_delays_note_on_and_note_off);
    RUN_TEST(test_negative_nudge_triggers_before_quantized_boundary);
    RUN_TEST(test_note_off_stays_before_next_note_on_when_nudged);
    RUN_TEST(test_stop_calls_all_notes_off_once);
    RUN_TEST(test_variation_changes_scheduled_note_velocity_gate_and_telemetry);
    RUN_TEST(test_variation_changes_scheduled_nudge);
    RUN_TEST(test_cycle_variation_telemetry_can_be_disabled_and_reenabled_mid_cycle);
    RUN_TEST(test_scale_constraint_changes_scheduled_note_and_telemetry);
    RUN_TEST(test_free_scale_reports_out_of_scale_without_changing_scheduled_note);
    RUN_TEST(test_graph_micro_sequence_schedules_multiple_notes_inside_step);
    RUN_TEST(test_graph_cycle_state_is_applied_by_engine_scheduler);
    return UNITY_END();
}
