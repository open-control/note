#include <array>
#include <cstddef>
#include <cstdint>

#include <unity.h>

#include <oc/note/sequencer/SequencerEvent.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>
#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

void setUp() {}
void tearDown() {}

namespace {

using oc::note::sequencer::ISequencerEventSink;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::StepSequencerEngine;
using oc::note::sequencer::StepSequencerPlaybackRegion;
using oc::note::sequencer::StepSequencerRuntimeState;

struct FixedEventSink final : ISequencerEventSink {
    static constexpr size_t CAPACITY = 256;

    bool emitSequencerEvent(const SequencerEvent& event) override {
        if (count >= events.size()) {
            return false;
        }
        events[count++] = event;
        return true;
    }

    size_t countType(SequencerEventType type) const {
        size_t result = 0;
        for (size_t i = 0; i < count; ++i) {
            if (events[i].type == type) {
                ++result;
            }
        }
        return result;
    }

    bool has(SequencerEventType type, uint32_t tick, uint8_t note) const {
        for (size_t i = 0; i < count; ++i) {
            if (events[i].type == type && events[i].tick == tick && events[i].note == note) {
                return true;
            }
        }
        return false;
    }

    std::array<SequencerEvent, CAPACITY> events{};
    size_t count = 0;
};

StepSequencerRuntimeState twoStepState() {
    StepSequencerRuntimeState state{};
    state.length = 2;
    state.stepsPerBeat = 4;
    state.midiChannel = 3;
    state.enabledMask = StepBitMask128::fromLower64(0x3ULL);
    state.note[0] = 60;
    state.note[1] = 62;
    state.velocity[0] = 100;
    state.velocity[1] = 90;
    state.gate[0] = 50;
    state.gate[1] = 50;
    return state;
}

void test_future_horizon_emits_first_and_next_step_without_advancing_playhead() {
    auto state = twoStepState();
    FixedEventSink sink{};
    StepSequencerEngine engine(state, sink);

    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(0, 6, true));

    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 0, 60));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOff, 3, 60));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 6, 62));
    TEST_ASSERT_EQUAL_INT16(0, state.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(0, state.playheadStepTickOffset);
    TEST_ASSERT_EQUAL_UINT16(6, state.playheadStepTicks);
    TEST_ASSERT_EQUAL_UINT8(0, state.lastResolvedVariation.stepIndex);
    TEST_ASSERT_EQUAL_UINT32(0, state.probabilityCycleIndex);
}

void test_equal_horizon_does_not_replay_and_note_off_advances_independently() {
    auto state = twoStepState();
    FixedEventSink sink{};
    StepSequencerEngine engine(state, sink);

    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(0, 6, true));
    const size_t firstCount = sink.count;
    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(1, 6, true));
    TEST_ASSERT_EQUAL_UINT32(firstCount, sink.count);
    TEST_ASSERT_EQUAL_INT16(0, state.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(1, state.playheadStepTickOffset);

    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(2, 9, true));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOff, 9, 62));
    TEST_ASSERT_EQUAL_INT16(0, state.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(2, state.playheadStepTickOffset);
}

void test_future_horizon_respects_prelude_and_internal_loop() {
    StepSequencerRuntimeState state{};
    state.length = 8;
    state.stepsPerBeat = 4;
    state.enabledMask = StepBitMask128::fromLower64(0xFFULL);
    for (uint8_t i = 0; i < 8; ++i) {
        state.note[i] = static_cast<uint8_t>(60U + i);
        state.velocity[i] = 100;
        state.gate[i] = 50;
    }

    FixedEventSink sink{};
    StepSequencerEngine engine(state, sink);
    TEST_ASSERT_TRUE(engine.setPlaybackRegion(StepSequencerPlaybackRegion{
        .contentLength = 8,
        .playStart = 2,
        .loopStart = 4,
        .loopEnd = 6,
    }));

    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(0, 30, true));

    TEST_ASSERT_EQUAL_UINT32(6, sink.countType(SequencerEventType::NoteOn));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 0, 62));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 6, 63));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 12, 64));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 18, 65));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 24, 64));
    TEST_ASSERT_TRUE(sink.has(SequencerEventType::NoteOn, 30, 65));
    TEST_ASSERT_EQUAL_INT16(2, state.playheadStep);
    TEST_ASSERT_EQUAL_UINT8(2, state.lastResolvedVariation.stepIndex);
    TEST_ASSERT_EQUAL_UINT32(0, state.probabilityCycleIndex);
}

void test_reduced_horizon_is_rejected_until_explicit_resync() {
    auto state = twoStepState();
    FixedEventSink sink{};
    StepSequencerEngine engine(state, sink);

    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(0, 6, true));
    const size_t emittedCount = sink.count;

    TEST_ASSERT_FALSE(engine.updateWithEmissionHorizon(1, 5, true));
    TEST_ASSERT_EQUAL_UINT32(emittedCount, sink.count);
    TEST_ASSERT_EQUAL_INT16(0, state.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(0, state.playheadStepTickOffset);

    engine.resyncToTick(1);
    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(1, 5, true));
    TEST_ASSERT_EQUAL_INT16(0, state.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(1, state.playheadStepTickOffset);
}

void test_tempo_grid_change_is_rejected_until_explicit_resync() {
    auto state = twoStepState();
    FixedEventSink sink{};
    StepSequencerEngine engine(state, sink);

    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(0, 6, true));
    const size_t emittedCount = sink.count;
    state.stepsPerBeat = 8;

    TEST_ASSERT_FALSE(engine.updateWithEmissionHorizon(1, 7, true));
    TEST_ASSERT_EQUAL_UINT32(emittedCount, sink.count);
    TEST_ASSERT_EQUAL_UINT16(6, state.playheadStepTicks);

    engine.resyncToTick(1);
    TEST_ASSERT_TRUE(engine.updateWithEmissionHorizon(1, 7, true));
    TEST_ASSERT_EQUAL_UINT16(3, state.playheadStepTicks);
    TEST_ASSERT_EQUAL_INT16(0, state.playheadStep);
    TEST_ASSERT_EQUAL_UINT16(1, state.playheadStepTickOffset);
    TEST_ASSERT_TRUE(sink.countType(SequencerEventType::AllNotesOff) > 0);
}

void test_future_horizon_matches_incremental_probability_and_event_order() {
    StepSequencerRuntimeState incrementalState{};
    incrementalState.length = 4;
    incrementalState.stepsPerBeat = 4;
    incrementalState.enabledMask = StepBitMask128::fromLower64(0xFULL);
    for (uint8_t i = 0; i < 4; ++i) {
        incrementalState.note[i] = static_cast<uint8_t>(60U + i);
        incrementalState.velocity[i] = static_cast<uint8_t>(100U - i);
        incrementalState.gate[i] = 50;
        incrementalState.probability[i] = static_cast<uint8_t>(25U + (i * 17U));
    }
    StepSequencerRuntimeState horizonState = incrementalState;

    FixedEventSink incrementalSink{};
    FixedEventSink horizonSink{};
    StepSequencerEngine incrementalEngine(incrementalState, incrementalSink);
    StepSequencerEngine horizonEngine(horizonState, horizonSink);

    for (uint32_t tick = 0; tick <= 24; ++tick) {
        incrementalEngine.update(tick, true);
    }
    TEST_ASSERT_TRUE(horizonEngine.updateWithEmissionHorizon(0, 24, true));

    TEST_ASSERT_EQUAL_UINT32(incrementalSink.count, horizonSink.count);
    for (size_t i = 0; i < incrementalSink.count; ++i) {
        const auto& expected = incrementalSink.events[i];
        const auto& actual = horizonSink.events[i];
        TEST_ASSERT_EQUAL_UINT32(expected.tick, actual.tick);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.type),
                                static_cast<uint8_t>(actual.type));
        TEST_ASSERT_EQUAL_UINT8(expected.channel, actual.channel);
        TEST_ASSERT_EQUAL_UINT8(expected.note, actual.note);
        TEST_ASSERT_EQUAL_UINT8(expected.velocity, actual.velocity);
    }
    TEST_ASSERT_EQUAL_INT16(0, horizonState.playheadStep);
    TEST_ASSERT_EQUAL_INT16(0, incrementalState.playheadStep);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_future_horizon_emits_first_and_next_step_without_advancing_playhead);
    RUN_TEST(test_equal_horizon_does_not_replay_and_note_off_advances_independently);
    RUN_TEST(test_future_horizon_respects_prelude_and_internal_loop);
    RUN_TEST(test_reduced_horizon_is_rejected_until_explicit_resync);
    RUN_TEST(test_tempo_grid_change_is_rejected_until_explicit_resync);
    RUN_TEST(test_future_horizon_matches_incremental_probability_and_event_order);
    return UNITY_END();
}
