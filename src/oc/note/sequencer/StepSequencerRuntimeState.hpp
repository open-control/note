#pragma once

#include <array>
#include <cstdint>

#include "StepBitMask128.hpp"

namespace oc::note::sequencer {

struct StepSequencerRuntimeState {
    static constexpr uint8_t MAX_STEPS = 128;
    static constexpr uint16_t MAX_GATE_PERCENT = 200;

    static constexpr uint8_t DEFAULT_LENGTH = 8;
    static constexpr uint8_t DEFAULT_STEPS_PER_BEAT = 4;
    static constexpr uint8_t DEFAULT_MIDI_CHANNEL_0BASED = 0;
    static constexpr uint8_t DEFAULT_NOTE = 48;
    static constexpr uint8_t DEFAULT_VELOCITY = 64;
    static constexpr uint16_t DEFAULT_GATE_PERCENT = 100;
    static constexpr uint8_t DEFAULT_PROBABILITY = 100;

    uint8_t length = DEFAULT_LENGTH;
    int16_t playheadStep = -1;
    uint8_t stepsPerBeat = DEFAULT_STEPS_PER_BEAT;
    uint8_t midiChannel = DEFAULT_MIDI_CHANNEL_0BASED;
    StepBitMask128 enabledMask{};

    uint32_t probabilityCycleRevision = 0;
    StepBitMask128 probabilityCycleMask{};
    uint32_t probabilityCycleIndex = 0;

    std::array<uint8_t, MAX_STEPS> note{};
    std::array<uint8_t, MAX_STEPS> velocity{};
    std::array<uint16_t, MAX_STEPS> gate{};
    std::array<int8_t, MAX_STEPS> nudge{};
    std::array<uint8_t, MAX_STEPS> probability{};

    StepSequencerRuntimeState() { reset(); }

    static uint8_t clampProbability(uint8_t value) {
        return (value > 100U) ? 100U : value;
    }

    void reset() {
        length = DEFAULT_LENGTH;
        playheadStep = -1;
        stepsPerBeat = DEFAULT_STEPS_PER_BEAT;
        midiChannel = DEFAULT_MIDI_CHANNEL_0BASED;
        enabledMask = {};
        probabilityCycleRevision = 0;
        probabilityCycleMask = {};
        probabilityCycleIndex = 0;

        for (uint8_t i = 0; i < MAX_STEPS; ++i) {
            note[i] = DEFAULT_NOTE;
            velocity[i] = DEFAULT_VELOCITY;
            gate[i] = DEFAULT_GATE_PERCENT;
            nudge[i] = 0;
            probability[i] = DEFAULT_PROBABILITY;
        }
    }

    uint8_t patternLength() const {
        return (length > MAX_STEPS) ? MAX_STEPS : length;
    }
};

}  // namespace oc::note::sequencer
