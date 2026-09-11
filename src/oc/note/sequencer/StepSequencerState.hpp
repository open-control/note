#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "StepBitMask128.hpp"
#include "StepSequencerStepData.hpp"
#include "StepSequencerScale.hpp"
#include "StepSequencerVariation.hpp"

namespace oc::note::sequencer {

using oc::state::Signal;

/**
 * @brief Mono-track step sequencer state
 *
 * This state is designed to be:
 * - UI-friendly (reactive signals for key UI updates)
 * - Engine-friendly (arrays + masks for fast access)
 * - Settings-ready (explicit defaults, no magic numbers)
 */
struct StepSequencerState : StepSequencerStepData {

    // Defaults
    static constexpr uint8_t DEFAULT_LENGTH = 8;
    static constexpr uint8_t DEFAULT_STEPS_PER_BEAT = 4;  // 1/16

    StepSequencerState();

    // Playback / transport
    Signal<uint8_t, 8> length{DEFAULT_LENGTH};
    Signal<int16_t> playheadStep{-1};
    Signal<uint8_t, 6> stepsPerBeat{DEFAULT_STEPS_PER_BEAT};

    // Step enable flags
    Signal<StepBitMask128> enabledMask{};

    // Runtime probability resolution for the currently active cycle.
    Signal<uint32_t> probabilityCycleRevision{0};
    StepBitMask128 probabilityCycleMask{};
    uint32_t probabilityCycleIndex = 0;

    StepSequencerScaleSettings scaleSettings{};
    StepSequencerVariationRanges variationRanges{};

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
