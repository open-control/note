#pragma once

#include <array>
#include <cstdint>

#include "StepBitMask128.hpp"
#include "StepSequencerScale.hpp"
#include "StepSequencerVariation.hpp"

namespace oc::note::sequencer {

struct StepSequencerExpandedVariationTelemetry {
    static constexpr uint8_t MAX_NOTES = 16;
    static constexpr uint16_t INVALID_NODE_ID = 0xFFFFU;

    bool valid = false;
    uint8_t rootStepIndex = 0;
    uint32_t cycleIndex = 0;
    uint8_t count = 0;
    std::array<uint16_t, MAX_NOTES> nodeId{};
    std::array<uint32_t, MAX_NOTES> localTick{};
    std::array<uint16_t, MAX_NOTES> spanTicks{};
    std::array<StepSequencerResolvedVariation, MAX_NOTES> variation{};

    void reset() {
        valid = false;
        rootStepIndex = 0;
        cycleIndex = 0;
        count = 0;
        nodeId.fill(INVALID_NODE_ID);
        localTick.fill(0);
        spanTicks.fill(1);
        variation.fill({});
    }

    void store(uint8_t index,
               uint16_t sourceNodeId,
               uint32_t sourceLocalTick,
               uint16_t sourceSpanTicks,
               const StepSequencerResolvedVariation& sourceVariation) {
        if (index >= MAX_NOTES) return;
        nodeId[index] = sourceNodeId;
        localTick[index] = sourceLocalTick;
        spanTicks[index] = sourceSpanTicks == 0 ? 1 : sourceSpanTicks;
        variation[index] = sourceVariation;
        if (count <= index) {
            count = static_cast<uint8_t>(index + 1U);
        }
    }
};

struct StepSequencerRuntimeState {
    static constexpr uint8_t MAX_STEPS = 128;
    static constexpr uint16_t MAX_GATE_PERCENT = 1600;
    static constexpr uint8_t MAX_SWING_PERCENT = 75;
    static constexpr int8_t MIN_PATTERN_NUDGE_PERCENT = -50;
    static constexpr int8_t MAX_PATTERN_NUDGE_PERCENT = 50;

    static constexpr uint8_t DEFAULT_LENGTH = 8;
    static constexpr uint8_t DEFAULT_STEPS_PER_BEAT = 4;
    static constexpr uint8_t DEFAULT_MIDI_CHANNEL_0BASED = 0;
    static constexpr uint8_t DEFAULT_NOTE = 48;
    static constexpr uint8_t DEFAULT_VELOCITY = 64;
    static constexpr uint16_t DEFAULT_GATE_PERCENT = 100;
    static constexpr uint8_t DEFAULT_PROBABILITY = 100;

    uint8_t length = DEFAULT_LENGTH;
    int16_t playheadStep = -1;
    uint16_t playheadStepTickOffset = 0;
    uint16_t playheadStepTicks = 1;
    uint8_t stepsPerBeat = DEFAULT_STEPS_PER_BEAT;
    uint8_t midiChannel = DEFAULT_MIDI_CHANNEL_0BASED;
    uint8_t effectiveSwingPercent = 0;
    int8_t patternNudgePercent = 0;
    StepBitMask128 enabledMask{};

    uint32_t probabilityCycleRevision = 0;
    StepBitMask128 probabilityCycleMask{};
    uint32_t probabilityCycleIndex = 0;

    std::array<uint8_t, MAX_STEPS> note{};
    std::array<uint8_t, MAX_STEPS> velocity{};
    std::array<uint16_t, MAX_STEPS> gate{};
    std::array<int8_t, MAX_STEPS> nudge{};
    std::array<uint8_t, MAX_STEPS> probability{};

    StepSequencerScaleSettings scaleSettings{};
    StepSequencerVariationRanges variationRanges{};
    bool variationTelemetryEnabled = true;
    uint32_t variationTelemetryRevision = 0;
    StepSequencerResolvedVariation lastResolvedVariation{};
    StepSequencerCycleVariationTelemetry cycleVariationTelemetry{};
    StepSequencerExpandedVariationTelemetry expandedVariationTelemetry{};

    StepSequencerRuntimeState() { reset(); }

    static uint8_t clampProbability(uint8_t value) {
        return (value > 100U) ? 100U : value;
    }

    static uint8_t clampSwingPercent(uint8_t value) {
        return (value > MAX_SWING_PERCENT) ? MAX_SWING_PERCENT : value;
    }

    static int8_t clampPatternNudgePercent(int value) {
        if (value < MIN_PATTERN_NUDGE_PERCENT) return MIN_PATTERN_NUDGE_PERCENT;
        if (value > MAX_PATTERN_NUDGE_PERCENT) return MAX_PATTERN_NUDGE_PERCENT;
        return static_cast<int8_t>(value);
    }

    void reset() {
        length = DEFAULT_LENGTH;
        playheadStep = -1;
        playheadStepTickOffset = 0;
        playheadStepTicks = 1;
        stepsPerBeat = DEFAULT_STEPS_PER_BEAT;
        midiChannel = DEFAULT_MIDI_CHANNEL_0BASED;
        effectiveSwingPercent = 0;
        patternNudgePercent = 0;
        enabledMask = {};
        probabilityCycleRevision = 0;
        probabilityCycleMask = {};
        probabilityCycleIndex = 0;
        variationTelemetryRevision = 0;
        variationTelemetryEnabled = true;
        scaleSettings = {};
        variationRanges = {};
        lastResolvedVariation = {};
        cycleVariationTelemetry.reset();
        expandedVariationTelemetry.reset();

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
