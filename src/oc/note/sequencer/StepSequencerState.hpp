#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

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
    static constexpr uint8_t MAX_STEPS = 64;
    static constexpr uint16_t MAX_GATE_PERCENT = 200;

    // Defaults (v0)
    static constexpr uint8_t DEFAULT_LENGTH = 8;
    static constexpr uint8_t DEFAULT_STEPS_PER_BEAT = 2;  // 1/8
    static constexpr uint8_t DEFAULT_MIDI_CHANNEL_0BASED = 0;  // channel 1
    static constexpr uint8_t DEFAULT_NOTE = 48;  // C3
    static constexpr uint8_t DEFAULT_VELOCITY = 64;
    static constexpr uint16_t DEFAULT_GATE_PERCENT = 100;

    StepSequencerState() { reset(); }

    // Playback / transport
    Signal<uint8_t> length{DEFAULT_LENGTH};
    Signal<int16_t> playheadStep{-1};
    Signal<uint8_t> stepsPerBeat{DEFAULT_STEPS_PER_BEAT};
    Signal<uint8_t> midiChannel{DEFAULT_MIDI_CHANNEL_0BASED};

    // Step enable flags
    Signal<uint64_t> enabledMask{0};

    // Step properties (v0)
    std::array<uint8_t, MAX_STEPS> note{};       // MIDI note number 0..127
    std::array<uint8_t, MAX_STEPS> velocity{};   // 0..127 (0 is valid)
    std::array<uint16_t, MAX_STEPS> gate{};      // percent (0..MAX_GATE_PERCENT)
    std::array<int8_t, MAX_STEPS> nudge{};       // -50..50 (not used by v0 engine)

    void reset() {
        length.set(DEFAULT_LENGTH);
        playheadStep.set(-1);
        stepsPerBeat.set(DEFAULT_STEPS_PER_BEAT);
        midiChannel.set(DEFAULT_MIDI_CHANNEL_0BASED);
        enabledMask.set(0);

        for (uint8_t i = 0; i < MAX_STEPS; ++i) {
            note[i] = DEFAULT_NOTE;
            velocity[i] = DEFAULT_VELOCITY;
            gate[i] = DEFAULT_GATE_PERCENT;
            nudge[i] = 0;
        }
    }

    uint8_t patternLength() const {
        const uint8_t len = length.get();
        return (len > MAX_STEPS) ? MAX_STEPS : len;
    }

    bool isEnabled(uint8_t step) const {
        if (step >= MAX_STEPS) return false;
        return (enabledMask.get() & (1ULL << step)) != 0;
    }

    void setEnabled(uint8_t step, bool enabled) {
        if (step >= MAX_STEPS) return;
        uint64_t m = enabledMask.get();
        if (enabled) {
            m |= (1ULL << step);
        } else {
            m &= ~(1ULL << step);
        }
        enabledMask.set(m);
    }

    void toggle(uint8_t step) {
        if (step >= MAX_STEPS) return;
        enabledMask.set(enabledMask.get() ^ (1ULL << step));
    }
};

}  // namespace oc::note::sequencer
