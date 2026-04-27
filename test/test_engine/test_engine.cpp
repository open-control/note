#include <unity.h>

#include <cstdint>
#include <vector>

#include <oc/note/sequencer/SequencerEvent.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

using oc::note::sequencer::ISequencerEventSink;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;
using oc::note::sequencer::StepSequencerEngine;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::StepSequencerRuntimeState;

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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_gate_zero_mutes_note);
    RUN_TEST(test_velocity_zero_is_sent);
    RUN_TEST(test_note_off_follows_gate_percent);
    RUN_TEST(test_boundary_order_note_off_before_next_step);
    RUN_TEST(test_positive_nudge_delays_note_on_and_note_off);
    RUN_TEST(test_negative_nudge_triggers_before_quantized_boundary);
    RUN_TEST(test_note_off_stays_before_next_note_on_when_nudged);
    RUN_TEST(test_stop_calls_all_notes_off_once);
    return UNITY_END();
}
