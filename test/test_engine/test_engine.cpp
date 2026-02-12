#include <unity.h>

#include <cstdint>
#include <vector>

#include <oc/note/sequencer/StepSequencerEngine.hpp>

using oc::note::sequencer::ISequencerOutput;
using oc::note::sequencer::StepSequencerEngine;
using oc::note::sequencer::StepSequencerState;

namespace {

enum class EventType : uint8_t {
    NoteOn,
    NoteOff,
    CC,
    AllNotesOff,
};

struct Event {
    EventType type;
    uint8_t ch;
    uint8_t note;
    uint8_t vel;
    uint8_t cc;
    uint8_t value;
};

class MockOutput final : public ISequencerOutput {
public:
    std::vector<Event> events;

    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
        events.push_back({EventType::NoteOn, channel, note, velocity, 0, 0});
    }

    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
        events.push_back({EventType::NoteOff, channel, note, velocity, 0, 0});
    }

    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override {
        events.push_back({EventType::CC, channel, 0, 0, cc, value});
    }

    void allNotesOff() override {
        events.push_back({EventType::AllNotesOff, 0, 0, 0, 0, 0});
    }
};

int countType(const std::vector<Event>& events, EventType type) {
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
    StepSequencerState st;
    st.length.set(4);
    st.stepsPerBeat.set(4);
    st.midiChannel.set(0);
    st.enabledMask.set(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 0;

    MockOutput out;
    StepSequencerEngine eng(st, out);

    eng.update(0, true);

    TEST_ASSERT_EQUAL(0, static_cast<int>(out.events.size()));
    TEST_ASSERT_EQUAL(0, st.playheadStep.get());
}

void test_velocity_zero_is_sent() {
    StepSequencerState st;
    st.length.set(4);
    st.stepsPerBeat.set(4);
    st.midiChannel.set(0);
    st.enabledMask.set(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 0;
    st.gate[0] = 50;

    MockOutput out;
    StepSequencerEngine eng(st, out);

    eng.update(0, true);

    TEST_ASSERT_EQUAL(1, static_cast<int>(out.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(EventType::NoteOn), static_cast<uint8_t>(out.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(0, out.events[0].ch);
    TEST_ASSERT_EQUAL_UINT8(60, out.events[0].note);
    TEST_ASSERT_EQUAL_UINT8(0, out.events[0].vel);
}

void test_note_off_follows_gate_percent() {
    // PPQN=24, stepsPerBeat=4 => ticksPerStep=6.
    // gate 50% => off at tick 3.
    StepSequencerState st;
    st.length.set(4);
    st.stepsPerBeat.set(4);
    st.midiChannel.set(0);
    st.enabledMask.set(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 50;

    MockOutput out;
    StepSequencerEngine eng(st, out);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(out.events.size()));

    eng.update(2, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(out.events.size()));

    eng.update(3, true);
    TEST_ASSERT_EQUAL(2, static_cast<int>(out.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(EventType::NoteOff), static_cast<uint8_t>(out.events[1].type));
    TEST_ASSERT_EQUAL_UINT8(0, out.events[1].ch);
    TEST_ASSERT_EQUAL_UINT8(60, out.events[1].note);
    TEST_ASSERT_EQUAL_UINT8(0, out.events[1].vel);
}

void test_boundary_order_note_off_before_next_step() {
    // gate 100% schedules note-off at the step boundary.
    StepSequencerState st;
    st.length.set(2);
    st.stepsPerBeat.set(4);
    st.midiChannel.set(0);
    st.enabledMask.set((1ULL << 0) | (1ULL << 1));
    st.note[0] = 60;
    st.note[1] = 62;
    st.velocity[0] = 100;
    st.velocity[1] = 100;
    st.gate[0] = 100;
    st.gate[1] = 100;

    MockOutput out;
    StepSequencerEngine eng(st, out);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(out.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(EventType::NoteOn), static_cast<uint8_t>(out.events[0].type));
    TEST_ASSERT_EQUAL_UINT8(60, out.events[0].note);

    eng.update(6, true);
    TEST_ASSERT_EQUAL(3, static_cast<int>(out.events.size()));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(EventType::NoteOff), static_cast<uint8_t>(out.events[1].type));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(EventType::NoteOn), static_cast<uint8_t>(out.events[2].type));
    TEST_ASSERT_EQUAL_UINT8(60, out.events[1].note);
    TEST_ASSERT_EQUAL_UINT8(62, out.events[2].note);
}

void test_stop_calls_all_notes_off_once() {
    StepSequencerState st;
    st.length.set(2);
    st.stepsPerBeat.set(4);
    st.midiChannel.set(0);
    st.enabledMask.set(1ULL << 0);
    st.note[0] = 60;
    st.velocity[0] = 100;
    st.gate[0] = 100;

    MockOutput out;
    StepSequencerEngine eng(st, out);

    eng.update(0, true);
    TEST_ASSERT_EQUAL(1, static_cast<int>(out.events.size()));

    eng.update(1, false);
    TEST_ASSERT_EQUAL(2, static_cast<int>(out.events.size()));
    TEST_ASSERT_EQUAL(1, countType(out.events, EventType::AllNotesOff));
    TEST_ASSERT_EQUAL(-1, st.playheadStep.get());

    eng.update(2, false);
    TEST_ASSERT_EQUAL(2, static_cast<int>(out.events.size()));
    TEST_ASSERT_EQUAL(1, countType(out.events, EventType::AllNotesOff));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_gate_zero_mutes_note);
    RUN_TEST(test_velocity_zero_is_sent);
    RUN_TEST(test_note_off_follows_gate_percent);
    RUN_TEST(test_boundary_order_note_off_before_next_step);
    RUN_TEST(test_stop_calls_all_notes_off_once);
    return UNITY_END();
}
