#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "StepBitMask128.hpp"

namespace oc::note::sequencer {

using oc::state::Signal;

/**
 * @brief Minimal mono-track step sequencer state (v0)
 *
 * This state is designed to be:
 * - UI-friendly (reactive signals for key UI updates)
 * - Engine-friendly (arrays + masks for fast access)
 * - Settings-ready (explicit defaults, no magic numbers)
 */
struct StepSequencerState {
    static constexpr uint8_t MAX_STEPS = 128;
    static constexpr uint16_t MAX_GATE_PERCENT = 200;

    // Defaults (v0)
    static constexpr uint8_t DEFAULT_LENGTH = 8;
    static constexpr uint8_t DEFAULT_STEPS_PER_BEAT = 4;  // 1/16
    static constexpr uint8_t DEFAULT_MIDI_CHANNEL_0BASED = 0;  // channel 1
    static constexpr uint8_t DEFAULT_NOTE = 48;  // C3
    static constexpr uint8_t DEFAULT_VELOCITY = 64;
    static constexpr uint16_t DEFAULT_GATE_PERCENT = 100;
    static constexpr uint8_t DEFAULT_PROBABILITY = 100;

    StepSequencerState();

    // Playback / transport
    Signal<uint8_t, 8> length{DEFAULT_LENGTH};
    Signal<int16_t> playheadStep{-1};
    Signal<uint8_t, 6> stepsPerBeat{DEFAULT_STEPS_PER_BEAT};
    Signal<uint8_t, 6> midiChannel{DEFAULT_MIDI_CHANNEL_0BASED};

    // Step enable flags
    Signal<StepBitMask128> enabledMask{};

    // Runtime probability resolution for the currently active cycle.
    Signal<uint32_t> probabilityCycleRevision{0};
    StepBitMask128 probabilityCycleMask{};
    uint32_t probabilityCycleIndex = 0;

    // Step properties (v0)
    std::array<uint8_t, MAX_STEPS> note{};         // MIDI note number 0..127
    std::array<uint8_t, MAX_STEPS> velocity{};     // 0..127 (0 is valid)
    std::array<uint16_t, MAX_STEPS> gate{};        // percent (0..MAX_GATE_PERCENT)
    std::array<int8_t, MAX_STEPS> nudge{};         // -50..50 (not used by v0 engine)
    std::array<uint8_t, MAX_STEPS> probability{};  // percent 0..100

    static uint8_t clampProbability(uint8_t value) {
        return (value > 100U) ? 100U : value;
    }

    void reset();

    uint8_t patternLength() const {
        const uint8_t len = length.get();
        return (len > MAX_STEPS) ? MAX_STEPS : len;
    }

    bool isEnabled(uint8_t step) const {
        if (step >= MAX_STEPS) return false;
        return enabledMask.get().test(step);
    }

    void setEnabled(uint8_t step, bool enabled) {
        if (step >= MAX_STEPS) return;
        StepBitMask128 m = enabledMask.get();
        m.setBit(step, enabled);
        enabledMask.set(m);
    }

    void toggle(uint8_t step) {
        if (step >= MAX_STEPS) return;
        StepBitMask128 m = enabledMask.get();
        m.toggleBit(step);
        enabledMask.set(m);
    }
};

}  // namespace oc::note::sequencer
