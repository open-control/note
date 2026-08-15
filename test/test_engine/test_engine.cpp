#include <unity.h>

#include <cstdint>
#include <vector>

#include <oc/note/sequencer/SequencerEvent.hpp>
#include <oc/note/sequencer/NoteScheduler.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>
#include <oc/note/sequencer/StepSequencerVariation.hpp>

using oc::note::sequencer::ISequencerEventSink;
using oc::note::sequencer::BoundedNoteScheduler;
using oc::note::sequencer::NoteScheduler;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;
using oc::note::sequencer::StepSequencerEngine;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSource;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerPlaybackRegion;
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

bool hasEvent(const std::vector<SequencerEvent>& events,
              SequencerEventType type,
              uint32_t tick,
              uint8_t note) {
    for (const auto& event : events) {
        if (event.type == type && event.tick == tick && event.note == note) {
            return true;
        }
    }
    return false;
}

void setLocalChord(StepSequencerGraph& graph, uint16_t nodeId, StepSequencerChordSpec spec = {}) {
    graph.stepNodes[nodeId].flags |= STEP_NODE_CHORD_MODE | STEP_NODE_CHORD_LOCAL;
    graph.stepNodes[nodeId].chordMode = StepSequencerChordMode::Local;
    graph.stepNodes[nodeId].chordSpec = spec;
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

}  // namespace

void setUp() {}

void tearDown() {}

void test_note_scheduler_tracks_same_pitch_by_channel() {
    NoteScheduler scheduler;
    MockEventSink sink;

    TEST_ASSERT_TRUE(scheduler.scheduleNote(0, 10, 0, 60, 100));
    TEST_ASSERT_TRUE(scheduler.scheduleNote(1, 11, 1, 60, 101));

    TEST_ASSERT_TRUE(scheduler.processUntil(1, sink));
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(0, countType(sink.events, SequencerEventType::NoteOff));
    TEST_ASSERT_EQUAL(2, countType(sink.events, SequencerEventType::NoteOn));
    TEST_ASSERT_EQUAL_UINT8(0, sink.events[0].channel);
    TEST_ASSERT_EQUAL_UINT8(1, sink.events[1].channel);

    TEST_ASSERT_TRUE(scheduler.processUntil(10, sink));
    TEST_ASSERT_EQUAL(1, countType(sink.events, SequencerEventType::NoteOff));
    TEST_ASSERT_EQUAL_UINT8(0, sink.events[2].channel);

    TEST_ASSERT_TRUE(scheduler.processUntil(11, sink));
    TEST_ASSERT_EQUAL(2, countType(sink.events, SequencerEventType::NoteOff));
    TEST_ASSERT_EQUAL_UINT8(1, sink.events[3].channel);
}

void test_retrigger_scheduler_preserves_later_same_note_pair() {
    BoundedNoteScheduler<8U, 2U> scheduler;
    MockEventSink sink;

    // A lane can submit a later MicroSequence hit before another lane submits
    // an earlier hit for the same MIDI voice.
    TEST_ASSERT_TRUE(scheduler.scheduleRetriggeringNote(8U, 12U, 0U, 60U, 100U));
    TEST_ASSERT_TRUE(scheduler.scheduleRetriggeringNote(4U, 6U, 0U, 60U, 80U));
    TEST_ASSERT_EQUAL_UINT32(4U, scheduler.size());

    TEST_ASSERT_TRUE(scheduler.processUntil(4U, sink));
    TEST_ASSERT_TRUE(scheduler.processUntil(6U, sink));
    TEST_ASSERT_TRUE(scheduler.processUntil(8U, sink));
    TEST_ASSERT_TRUE(scheduler.processUntil(12U, sink));

    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 4U, 60U));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOff, 6U, 60U));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 8U, 60U));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOff, 12U, 60U));
}

void test_retrigger_scheduler_preserves_manual_safety_off() {
    BoundedNoteScheduler<8U, 2U> scheduler;
    MockEventSink sink;

    TEST_ASSERT_TRUE(scheduler.scheduleNoteOff(12U, 0U, 60U));
    TEST_ASSERT_TRUE(scheduler.scheduleRetriggeringNote(4U, 6U, 0U, 60U, 80U));
    TEST_ASSERT_EQUAL_UINT32(3U, scheduler.size());

    TEST_ASSERT_TRUE(scheduler.processUntil(12U, sink));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOff, 12U, 60U));
}

void test_retrigger_scheduler_discards_active_voice_stale_off() {
    BoundedNoteScheduler<8U, 2U> scheduler;
    MockEventSink sink;

    TEST_ASSERT_TRUE(scheduler.scheduleRetriggeringNote(0U, 10U, 0U, 60U, 100U));
    TEST_ASSERT_TRUE(scheduler.processUntil(0U, sink));
    TEST_ASSERT_TRUE(scheduler.scheduleRetriggeringNote(5U, 7U, 0U, 60U, 80U));
    TEST_ASSERT_EQUAL_UINT32(2U, scheduler.size());

    TEST_ASSERT_TRUE(scheduler.processUntil(10U, sink));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOff, 5U, 60U));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 5U, 60U));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOff, 7U, 60U));
    TEST_ASSERT_FALSE(hasEvent(sink.events, SequencerEventType::NoteOff, 10U, 60U));
}

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

void test_extended_gate_can_release_after_pattern_boundary() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 3);
    st.note[3] = 60;
    st.velocity[3] = 100;
    st.gate[3] = 200;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));

    eng.update(18, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT32(18, sink.events[0].tick);

    eng.update(24, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));

    eng.update(30, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL_UINT32(30, sink.events[1].tick);
}

void test_extended_gate_allows_x10_length() {
    StepSequencerRuntimeState st;
    st.length = 16;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 3);
    st.note[3] = 60;
    st.velocity[3] = 100;
    st.gate[3] = 1000;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(18, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT32(18, sink.events[0].tick);

    eng.update(77, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));

    eng.update(78, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL_UINT32(78, sink.events[1].tick);
}

void test_same_pitch_overlap_retriggers_and_cancels_stale_note_off() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 0) | (1ULL << 1));
    st.note[0] = 60;
    st.note[1] = 60;
    st.velocity[0] = 100;
    st.velocity[1] = 80;
    st.gate[0] = 300;
    st.gate[1] = 50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT32(0, sink.events[0].tick);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(3, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[2].type));
    TEST_ASSERT_EQUAL_UINT32(6, sink.events[1].tick);
    TEST_ASSERT_EQUAL_UINT32(6, sink.events[2].tick);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[1].note);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[2].note);

    eng.update(9, true);
    TEST_ASSERT_EQUAL(4, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[3].type));
    TEST_ASSERT_EQUAL_UINT32(9, sink.events[3].tick);

    eng.update(18, true);
    TEST_ASSERT_EQUAL(4, static_cast<int>(sink.events.size()));
}

void test_different_pitch_overlap_keeps_original_note_off() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 0) | (1ULL << 1));
    st.note[0] = 60;
    st.note[1] = 62;
    st.velocity[0] = 100;
    st.velocity[1] = 80;
    st.gate[0] = 300;
    st.gate[1] = 50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[1].note);

    eng.update(9, true);
    TEST_ASSERT_EQUAL(3, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[2].type));
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[2].note);

    eng.update(18, true);
    TEST_ASSERT_EQUAL(4, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[3].type));
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[3].note);
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

void test_engine_default_region_tracks_state_length() {
    StepSequencerRuntimeState st;
    st.length = 4;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    auto region = eng.playbackRegion();
    TEST_ASSERT_TRUE(region.isValid());
    TEST_ASSERT_EQUAL_UINT8(4, region.contentLength);
    TEST_ASSERT_EQUAL_UINT8(0, region.playStart);
    TEST_ASSERT_EQUAL_UINT8(0, region.loopStart);
    TEST_ASSERT_EQUAL_UINT8(4, region.loopEnd);

    st.length = 16;
    region = eng.playbackRegion();
    TEST_ASSERT_TRUE(region.isValid());
    TEST_ASSERT_EQUAL_UINT8(16, region.contentLength);
    TEST_ASSERT_EQUAL_UINT8(16, region.loopEnd);
}

void test_engine_rejects_invalid_region_without_changing_active_region() {
    StepSequencerRuntimeState st;
    st.length = 8;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    const auto before = eng.playbackRegion();
    const StepSequencerPlaybackRegion invalid{
        .contentLength = 8,
        .playStart = 5,
        .loopStart = 4,
        .loopEnd = 8,
    };

    TEST_ASSERT_FALSE(eng.setPlaybackRegion(invalid));
    const auto after = eng.playbackRegion();
    TEST_ASSERT_EQUAL_UINT8(before.contentLength, after.contentLength);
    TEST_ASSERT_EQUAL_UINT8(before.playStart, after.playStart);
    TEST_ASSERT_EQUAL_UINT8(before.loopStart, after.loopStart);
    TEST_ASSERT_EQUAL_UINT8(before.loopEnd, after.loopEnd);
}

void test_engine_plays_prelude_once_then_repeats_internal_loop() {
    StepSequencerRuntimeState st;
    st.length = 8;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(0xFFULL);
    for (uint8_t i = 0; i < 8; ++i) {
        st.note[i] = static_cast<uint8_t>(60 + i);
        st.velocity[i] = 100;
        st.gate[i] = 100;
    }

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    TEST_ASSERT_TRUE(eng.setPlaybackRegion(StepSequencerPlaybackRegion{
        .contentLength = 8,
        .playStart = 2,
        .loopStart = 4,
        .loopEnd = 6,
    }));

    eng.update(0, true);
    eng.update(6, true);
    eng.update(12, true);
    eng.update(18, true);
    eng.update(24, true);
    eng.update(30, true);

    TEST_ASSERT_EQUAL(6, countType(sink.events, SequencerEventType::NoteOn));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 0, 62));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 6, 63));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 12, 64));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 18, 65));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 24, 64));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 30, 65));
    TEST_ASSERT_EQUAL(5, st.playheadStep);
    TEST_ASSERT_EQUAL_UINT32(1, st.probabilityCycleIndex);
}

void test_engine_resync_uses_same_region_for_playhead_probability_and_next_note() {
    StepSequencerRuntimeState st;
    st.length = 8;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 4) | (1ULL << 5));
    st.note[4] = 64;
    st.note[5] = 65;
    st.velocity[4] = 100;
    st.velocity[5] = 100;
    st.gate[4] = 100;
    st.gate[5] = 100;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    TEST_ASSERT_TRUE(eng.setPlaybackRegion(StepSequencerPlaybackRegion{
        .contentLength = 8,
        .playStart = 2,
        .loopStart = 4,
        .loopEnd = 6,
    }));

    eng.resyncToTick(31);
    TEST_ASSERT_EQUAL(5, st.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(1, st.playheadStepTickOffset);
    TEST_ASSERT_EQUAL_UINT32(1, st.probabilityCycleIndex);
    TEST_ASSERT_TRUE(st.lastResolvedVariation.stepIndex == 5);

    sink.events.clear();
    eng.update(35, true);
    TEST_ASSERT_EQUAL(0, countType(sink.events, SequencerEventType::NoteOn));
    eng.update(36, true);
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 36, 64));
    TEST_ASSERT_EQUAL(4, st.playheadStep);
    TEST_ASSERT_EQUAL_UINT32(2, st.probabilityCycleIndex);
}

void test_engine_can_restore_state_length_region_after_explicit_region() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 100;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    TEST_ASSERT_TRUE(eng.setPlaybackRegion(StepSequencerPlaybackRegion{
        .contentLength = 4,
        .playStart = 2,
        .loopStart = 2,
        .loopEnd = 4,
    }));
    eng.useStateLengthPlaybackRegion();

    eng.update(0, true);
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 0, 60));
    const auto region = eng.playbackRegion();
    TEST_ASSERT_EQUAL_UINT8(0, region.playStart);
    TEST_ASSERT_EQUAL_UINT8(0, region.loopStart);
    TEST_ASSERT_EQUAL_UINT8(4, region.loopEnd);
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

void test_reordered_same_pitch_nudge_preserves_delayed_note_on() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.effectiveSwingPercent = 100;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 1) | (1ULL << 2));
    st.note[1] = 60;
    st.note[2] = 60;
    st.velocity[1] = 100;
    st.velocity[2] = 80;
    st.gate[1] = 100;
    st.gate[2] = 100;
    st.nudge[1] = 50;
    st.nudge[2] = -50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));

    eng.update(6, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));

    eng.update(9, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[0].type));
    TEST_ASSERT_EQUAL_UINT32(9, sink.events[0].tick);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);
    TEST_ASSERT_EQUAL_UINT8(80, sink.events[0].velocity);

    eng.update(11, true);
    TEST_ASSERT_EQUAL(3, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[1].type));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOn), static_cast<uint8_t>(sink.events[2].type));
    TEST_ASSERT_EQUAL_UINT32(11, sink.events[1].tick);
    TEST_ASSERT_EQUAL_UINT32(11, sink.events[2].tick);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[1].note);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[2].note);
    TEST_ASSERT_EQUAL_UINT8(100, sink.events[2].velocity);

    eng.update(15, true);
    TEST_ASSERT_EQUAL(3, static_cast<int>(sink.events.size()));

    eng.update(17, true);
    TEST_ASSERT_EQUAL(4, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(SequencerEventType::NoteOff), static_cast<uint8_t>(sink.events[3].type));
    TEST_ASSERT_EQUAL_UINT32(17, sink.events[3].tick);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[3].note);
}

void test_swing_delays_odd_steps() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.effectiveSwingPercent = 50;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 0) | (1ULL << 1));
    st.note[0] = 60;
    st.note[1] = 62;
    st.velocity[0] = 100;
    st.velocity[1] = 100;
    st.gate[0] = 200;
    st.gate[1] = 50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL_UINT32(0, sink.events[0].tick);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);

    eng.update(7, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));

    eng.update(8, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL_UINT32(8, sink.events[1].tick);
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[1].note);
}

void test_pattern_nudge_moves_whole_step_before_step_nudge() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.patternNudgePercent = 50;
    st.enabledMask = StepBitMask128::fromLower64((1ULL << 0) | (1ULL << 1));
    st.note[0] = 60;
    st.note[1] = 62;
    st.velocity[0] = 100;
    st.velocity[1] = 100;
    st.gate[0] = 100;
    st.gate[1] = 50;
    st.nudge[1] = -50;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(0, static_cast<int>(sink.events.size()));

    eng.update(3, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL_UINT32(3, sink.events[0].tick);
    TEST_ASSERT_EQUAL_UINT8(60, sink.events[0].note);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(sink.events.size()));
    TEST_ASSERT_EQUAL_UINT32(6, sink.events[1].tick);
    TEST_ASSERT_EQUAL_UINT8(62, sink.events[1].note);
}

void test_division_change_to_slower_grid_resyncs_scheduler() {
    StepSequencerRuntimeState st;
    st.length = 16;
    st.stepsPerBeat = 8;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(0xFFFFULL);
    for (uint8_t i = 0; i < st.length; ++i) {
        st.note[i] = static_cast<uint8_t>(60 + i);
        st.velocity[i] = 100;
        st.gate[i] = 50;
    }

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);

    eng.update(0, true);
    eng.update(24, true);
    sink.events.clear();

    st.stepsPerBeat = 1;
    eng.update(25, true);
    TEST_ASSERT_TRUE(countType(sink.events, SequencerEventType::AllNotesOff) > 0);

    sink.events.clear();
    eng.update(31, true);
    sink.events.clear();

    eng.update(72, true);
    TEST_ASSERT_TRUE(countType(sink.events, SequencerEventType::NoteOn) > 0);
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

void test_graph_root_chord_schedules_all_voices() {
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
    graph.stepNodeCount = 4;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = 4;
    setLocalChord(graph, 0);

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    eng.setGraph(&graph);

    eng.update(0, true);
    eng.update(1, true);

    TEST_ASSERT_EQUAL(3, countType(sink.events, SequencerEventType::NoteOn));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 0, 60));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 0, 64));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 0, 67));
}

void test_graph_root_chord_strum_schedules_staggered_voices() {
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
    graph.stepNodeCount = 4;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = 4;
    StepSequencerChordSpec spec{};
    spec.strum = 100;
    setLocalChord(graph, 0, spec);

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    eng.setGraph(&graph);

    eng.update(0, true);
    eng.update(2, true);
    eng.update(5, true);

    TEST_ASSERT_EQUAL(3, countType(sink.events, SequencerEventType::NoteOn));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 0, 60));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 2, 64));
    TEST_ASSERT_TRUE(hasEvent(sink.events, SequencerEventType::NoteOn, 5, 67));
}

void test_graph_expanded_variation_telemetry_tracks_nested_micro_node() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 96;
    st.gate[0] = 100;
    st.probability[0] = 100;

    StepSequencerGraph graph;
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 2;
    graph.cycleSetCount = 1;
    graph.stepNodeCount = 7;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = 4;
    graph.sequences[1].kind = StepSequencerSequenceKind::MicroSequence;
    graph.sequences[1].firstStepNode = 5;
    graph.sequences[1].length = 2;
    graph.cycleSets[0].firstStateNode = 4;
    graph.cycleSets[0].length = 1;
    graph.stepNodes[0].flags = STEP_NODE_CYCLE_SET;
    graph.stepNodes[0].cycleSetId = 0;
    graph.stepNodes[4].flags = STEP_NODE_CHILD_SEQUENCE;
    graph.stepNodes[4].childSequenceId = 1;
    graph.stepNodes[4].localVariation.pitchSemitones = 6;
    graph.stepNodes[5].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[5].noteOffset = 0;
    graph.stepNodes[6].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[6].noteOffset = 2;

    const auto expectedParent = resolveStepVariation(
        StepSequencerStepValues{.note = 60, .velocity = 96, .gate = 100, .nudge = 0},
        StepSequencerVariationRanges{
            .pitchSemitones = 6,
            .velocity = 0,
            .gatePercent = 0,
            .nudge = 0,
        },
        st.scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        1,
        0,
        0,
        true,
        mixVariationIdentityForTest(0, 0x4359434Cu, 0, 0)
    );

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    eng.setGraph(&graph);

    eng.update(0, true);

    TEST_ASSERT_TRUE(st.expandedVariationTelemetry.valid);
    TEST_ASSERT_EQUAL_UINT8(0, st.expandedVariationTelemetry.rootStepIndex);
    TEST_ASSERT_EQUAL_UINT8(2, st.expandedVariationTelemetry.count);
    TEST_ASSERT_EQUAL_UINT16(5, st.expandedVariationTelemetry.nodeId[0]);
    TEST_ASSERT_EQUAL_UINT16(6, st.expandedVariationTelemetry.nodeId[1]);
    TEST_ASSERT_EQUAL_UINT8(
        expectedParent.resolved.note,
        st.expandedVariationTelemetry.variation[0].resolved.note
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expectedParent.resolved.note + 2U),
        st.expandedVariationTelemetry.variation[1].resolved.note
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordSource::Single),
        static_cast<uint8_t>(st.expandedVariationTelemetry.chordSource[0])
    );
    TEST_ASSERT_EQUAL_UINT8(1, st.expandedVariationTelemetry.chordVoiceCount[0]);
    TEST_ASSERT_EQUAL_UINT8(0, st.expandedVariationTelemetry.chordVoiceIndex[0]);
    TEST_ASSERT_EQUAL_INT16(0, st.expandedVariationTelemetry.chordInterval[0]);
}

void test_graph_expanded_variation_telemetry_tracks_chord_voices() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.midiChannel = 0;
    st.enabledMask = StepBitMask128::fromLower64(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 96;
    st.gate[0] = 100;
    st.probability[0] = 100;

    StepSequencerGraph graph;
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 1;
    graph.stepNodeCount = 4;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = 4;
    setLocalChord(graph, 0);

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    eng.setGraph(&graph);

    eng.update(0, true);

    TEST_ASSERT_TRUE(st.expandedVariationTelemetry.valid);
    TEST_ASSERT_EQUAL_UINT8(3, st.expandedVariationTelemetry.count);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordSource::Local),
        static_cast<uint8_t>(st.expandedVariationTelemetry.chordSource[0])
    );
    TEST_ASSERT_EQUAL_UINT8(3, st.expandedVariationTelemetry.chordVoiceCount[0]);
    TEST_ASSERT_EQUAL_UINT8(0, st.expandedVariationTelemetry.chordVoiceIndex[0]);
    TEST_ASSERT_EQUAL_UINT8(1, st.expandedVariationTelemetry.chordVoiceIndex[1]);
    TEST_ASSERT_EQUAL_UINT8(2, st.expandedVariationTelemetry.chordVoiceIndex[2]);
    TEST_ASSERT_EQUAL_INT16(0, st.expandedVariationTelemetry.chordInterval[0]);
    TEST_ASSERT_EQUAL_INT16(4, st.expandedVariationTelemetry.chordInterval[1]);
    TEST_ASSERT_EQUAL_INT16(7, st.expandedVariationTelemetry.chordInterval[2]);
    TEST_ASSERT_FALSE(st.expandedVariationTelemetry.chordIntervalUsesScaleDegrees[0]);
}

void test_graph_note_budget_publishes_telemetry_and_runtime_diagnostics() {
    StepSequencerRuntimeState st;
    st.length = 4;
    st.stepsPerBeat = 4;
    st.enabledMask = StepBitMask128::fromLower64(1ULL);
    st.note[0] = 60;
    st.velocity[0] = 96;
    st.gate[0] = 100;
    st.probability[0] = 100;

    StepSequencerGraph graph;
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 2;
    graph.stepNodeCount = 21;
    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length = 4;
    graph.sequences[1].kind = StepSequencerSequenceKind::MicroSequence;
    graph.sequences[1].firstStepNode = 4;
    graph.sequences[1].length = 17;
    graph.stepNodes[0].flags = STEP_NODE_CHILD_SEQUENCE;
    graph.stepNodes[0].childSequenceId = 1;

    MockEventSink sink;
    StepSequencerEngine eng(st, sink);
    eng.setGraph(&graph);
    eng.update(0, true);

    TEST_ASSERT_TRUE(st.expandedVariationTelemetry.valid);
    TEST_ASSERT_TRUE(st.expandedVariationTelemetry.noteBudgetExceeded);
    TEST_ASSERT_EQUAL_UINT8(16, st.expandedVariationTelemetry.count);
    TEST_ASSERT_EQUAL_UINT8(17, st.expandedVariationTelemetry.requestedNoteCount);
    TEST_ASSERT_TRUE(st.runtimeDiagnostics.noteBudgetExceeded);
    TEST_ASSERT_EQUAL_UINT32(1, st.runtimeDiagnostics.noteBudgetExceededCount);
    TEST_ASSERT_FALSE(st.runtimeDiagnostics.schedulerCapacityExceeded);
    TEST_ASSERT_EQUAL_UINT32(0, st.runtimeDiagnostics.schedulerCapacityExceededCount);

    eng.reset();
    TEST_ASSERT_FALSE(st.runtimeDiagnostics.noteBudgetExceeded);
    TEST_ASSERT_EQUAL_UINT32(0, st.runtimeDiagnostics.noteBudgetExceededCount);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_note_scheduler_tracks_same_pitch_by_channel);
    RUN_TEST(test_retrigger_scheduler_preserves_later_same_note_pair);
    RUN_TEST(test_retrigger_scheduler_preserves_manual_safety_off);
    RUN_TEST(test_retrigger_scheduler_discards_active_voice_stale_off);
    RUN_TEST(test_gate_zero_mutes_note);
    RUN_TEST(test_velocity_zero_is_sent);
    RUN_TEST(test_note_off_follows_gate_percent);
    RUN_TEST(test_extended_gate_can_release_after_pattern_boundary);
    RUN_TEST(test_extended_gate_allows_x10_length);
    RUN_TEST(test_same_pitch_overlap_retriggers_and_cancels_stale_note_off);
    RUN_TEST(test_different_pitch_overlap_keeps_original_note_off);
    RUN_TEST(test_boundary_order_note_off_before_next_step);
    RUN_TEST(test_playhead_tick_position_tracks_offset_inside_current_step);
    RUN_TEST(test_engine_default_region_tracks_state_length);
    RUN_TEST(test_engine_rejects_invalid_region_without_changing_active_region);
    RUN_TEST(test_engine_plays_prelude_once_then_repeats_internal_loop);
    RUN_TEST(test_engine_resync_uses_same_region_for_playhead_probability_and_next_note);
    RUN_TEST(test_engine_can_restore_state_length_region_after_explicit_region);
    RUN_TEST(test_positive_nudge_delays_note_on_and_note_off);
    RUN_TEST(test_negative_nudge_triggers_before_quantized_boundary);
    RUN_TEST(test_note_off_stays_before_next_note_on_when_nudged);
    RUN_TEST(test_reordered_same_pitch_nudge_preserves_delayed_note_on);
    RUN_TEST(test_swing_delays_odd_steps);
    RUN_TEST(test_pattern_nudge_moves_whole_step_before_step_nudge);
    RUN_TEST(test_division_change_to_slower_grid_resyncs_scheduler);
    RUN_TEST(test_stop_calls_all_notes_off_once);
    RUN_TEST(test_variation_changes_scheduled_note_velocity_gate_and_telemetry);
    RUN_TEST(test_variation_changes_scheduled_nudge);
    RUN_TEST(test_cycle_variation_telemetry_can_be_disabled_and_reenabled_mid_cycle);
    RUN_TEST(test_scale_constraint_changes_scheduled_note_and_telemetry);
    RUN_TEST(test_free_scale_reports_out_of_scale_without_changing_scheduled_note);
    RUN_TEST(test_graph_micro_sequence_schedules_multiple_notes_inside_step);
    RUN_TEST(test_graph_cycle_state_is_applied_by_engine_scheduler);
    RUN_TEST(test_graph_root_chord_schedules_all_voices);
    RUN_TEST(test_graph_root_chord_strum_schedules_staggered_voices);
    RUN_TEST(test_graph_expanded_variation_telemetry_tracks_nested_micro_node);
    RUN_TEST(test_graph_expanded_variation_telemetry_tracks_chord_voices);
    RUN_TEST(test_graph_note_budget_publishes_telemetry_and_runtime_diagnostics);
    return UNITY_END();
}
