#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "StepBitMask128.hpp"
#include "StepSequencerScale.hpp"

namespace oc::note::sequencer {

struct StepSequencerVariationRanges {
    static constexpr uint8_t MAX_PITCH_SEMITONES = 36;
    static constexpr uint8_t MAX_VELOCITY = 127;
    static constexpr uint8_t MAX_GATE_PERCENT = 100;
    static constexpr uint8_t MAX_NUDGE = 50;

    // Interpreted as semitones in Free/Chromatic mode and as scale degrees in
    // constrained non-chromatic scale modes.
    uint8_t pitchSemitones = 0;
    uint8_t velocity = 0;
    uint8_t gatePercent = 0;
    uint8_t nudge = 0;

    void clamp() {
        pitchSemitones = std::min<uint8_t>(pitchSemitones, MAX_PITCH_SEMITONES);
        velocity = std::min<uint8_t>(velocity, MAX_VELOCITY);
        gatePercent = std::min<uint8_t>(gatePercent, MAX_GATE_PERCENT);
        nudge = std::min<uint8_t>(nudge, MAX_NUDGE);
    }
};

struct StepSequencerStepValues {
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
};

struct StepSequencerResolvedVariation {
    uint8_t stepIndex = 0;
    uint32_t cycleIndex = 0;
    bool triggered = false;

    StepSequencerStepValues base{};
    StepSequencerStepValues resolved{};
    StepSequencerVariationRanges ranges{};
    StepSequencerScaleSettings scaleSettings{};
    StepSequencerScaleResolution scale{};

    int8_t pitchDelta = 0;
    int16_t velocityDelta = 0;
    int16_t gateDelta = 0;
    int8_t nudgeDelta = 0;
    bool pitchVariationUsesScaleDegrees = false;
};

struct StepSequencerCycleVariationTelemetry {
    static constexpr uint8_t MAX_STEPS = 128;

    uint32_t cycleIndex = 0;
    StepBitMask128 validMask{};
    StepBitMask128 triggeredMask{};
    StepBitMask128 scaleInMask{};
    StepBitMask128 scaleConstrainedMask{};
    StepSequencerVariationRanges ranges{};
    StepSequencerScaleSettings scaleSettings{};

    std::array<uint8_t, MAX_STEPS> resolvedNote{};
    std::array<uint8_t, MAX_STEPS> resolvedVelocity{};
    std::array<uint16_t, MAX_STEPS> resolvedGate{};
    std::array<int8_t, MAX_STEPS> resolvedNudge{};
    std::array<int8_t, MAX_STEPS> pitchDelta{};
    std::array<int16_t, MAX_STEPS> velocityDelta{};
    std::array<int16_t, MAX_STEPS> gateDelta{};
    std::array<int8_t, MAX_STEPS> nudgeDelta{};

    void reset() {
        cycleIndex = 0;
        validMask = {};
        triggeredMask = {};
        scaleInMask = {};
        scaleConstrainedMask = {};
        ranges = {};
        scaleSettings = {};
        resolvedNote.fill(0);
        resolvedVelocity.fill(0);
        resolvedGate.fill(0);
        resolvedNudge.fill(0);
        pitchDelta.fill(0);
        velocityDelta.fill(0);
        gateDelta.fill(0);
        nudgeDelta.fill(0);
    }

    void store(const StepSequencerResolvedVariation& variation) {
        if (variation.stepIndex >= MAX_STEPS) return;

        cycleIndex = variation.cycleIndex;
        ranges = variation.ranges;
        validMask.setBit(variation.stepIndex, true);
        triggeredMask.setBit(variation.stepIndex, variation.triggered);
        scaleInMask.setBit(variation.stepIndex, variation.scale.inputInScale);
        scaleConstrainedMask.setBit(variation.stepIndex, variation.scale.constrained);
        resolvedNote[variation.stepIndex] = variation.resolved.note;
        resolvedVelocity[variation.stepIndex] = variation.resolved.velocity;
        resolvedGate[variation.stepIndex] = variation.resolved.gate;
        resolvedNudge[variation.stepIndex] = variation.resolved.nudge;
        pitchDelta[variation.stepIndex] = variation.pitchDelta;
        velocityDelta[variation.stepIndex] = variation.velocityDelta;
        gateDelta[variation.stepIndex] = variation.gateDelta;
        nudgeDelta[variation.stepIndex] = variation.nudgeDelta;
    }
};

inline uint32_t variationHash(uint32_t runSeed,
                              uint32_t cycleIndex,
                              uint32_t stepIdentity,
                              uint32_t propertySalt) {
    uint32_t x = runSeed * 747796405u;
    x ^= cycleIndex * 2891336453u;
    x ^= stepIdentity * 277803737u;
    x ^= propertySalt * 1597334677u;
    x ^= 0x9E3779B9u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

inline int16_t centeredDelta(uint32_t runSeed,
                             uint32_t cycleIndex,
                             uint32_t stepIdentity,
                             uint8_t range,
                             uint32_t propertySalt) {
    if (range == 0) return 0;

    const uint16_t width = static_cast<uint16_t>(range) * 2U + 1U;
    const uint16_t value = static_cast<uint16_t>(
        variationHash(runSeed, cycleIndex, stepIdentity, propertySalt) % width
    );
    return static_cast<int16_t>(value) - static_cast<int16_t>(range);
}

inline uint8_t clampMidi7(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

inline uint16_t clampGatePercent(int value, uint16_t maxGatePercent) {
    if (value < 0) return 0;
    if (value > static_cast<int>(maxGatePercent)) return maxGatePercent;
    return static_cast<uint16_t>(value);
}

inline int8_t clampNudge(int value) {
    if (value < -50) return -50;
    if (value > 50) return 50;
    return static_cast<int8_t>(value);
}

inline StepSequencerResolvedVariation resolveStepVariation(
    StepSequencerStepValues base,
    StepSequencerVariationRanges ranges,
    StepSequencerScaleSettings scaleSettings,
    uint16_t maxGatePercent,
    uint32_t runSeed,
    uint32_t cycleIndex,
    uint8_t stepIndex,
    bool triggered = true,
    uint32_t stepIdentity = UINT32_MAX,
    bool pitchFollowsScale = true
) {
    ranges.clamp();
    scaleSettings.clamp();

    StepSequencerResolvedVariation result{};
    result.stepIndex = stepIndex;
    result.cycleIndex = cycleIndex;
    result.triggered = triggered;
    result.base = base;
    result.resolved = base;
    result.ranges = ranges;
    result.scaleSettings = scaleSettings;
    result.scale = resolveScaleNote(base.note, scaleSettings);

    if (!triggered) {
        return result;
    }

    if (stepIdentity == UINT32_MAX) {
        stepIdentity = stepIndex;
    }

    result.pitchVariationUsesScaleDegrees =
        pitchFollowsScale && scaleSettings.isConstrained();
    result.pitchDelta = static_cast<int8_t>(
        centeredDelta(runSeed, cycleIndex, stepIdentity, ranges.pitchSemitones, 0x50495443u)
    );
    result.velocityDelta =
        centeredDelta(runSeed, cycleIndex, stepIdentity, ranges.velocity, 0x56454C4Fu);
    result.gateDelta =
        centeredDelta(runSeed, cycleIndex, stepIdentity, ranges.gatePercent, 0x47415445u);
    result.nudgeDelta = static_cast<int8_t>(
        centeredDelta(runSeed, cycleIndex, stepIdentity, ranges.nudge, 0x4E554447u)
    );

    if (result.pitchVariationUsesScaleDegrees) {
        const auto anchor = resolveScaleNote(base.note, scaleSettings);
        const uint8_t movedNote = moveByScaleDegrees(anchor.outputNote, result.pitchDelta, scaleSettings);
        result.scale = anchor;
        result.scale.outputNote = movedNote;
        result.scale.semitoneDelta = static_cast<int8_t>(
            static_cast<int>(movedNote) - static_cast<int>(base.note)
        );
        result.resolved.note = result.scale.outputNote;
    } else {
        const uint8_t variedNote = clampMidi7(static_cast<int>(base.note) + result.pitchDelta);
        result.scale = resolveScaleNote(variedNote, scaleSettings);
        result.scale.outputNote = variedNote;
        result.scale.constrained = false;
        result.scale.semitoneDelta = static_cast<int8_t>(
            static_cast<int>(variedNote) - static_cast<int>(base.note)
        );
        result.resolved.note = variedNote;
    }

    result.resolved.velocity = clampMidi7(static_cast<int>(base.velocity) + result.velocityDelta);
    result.resolved.gate =
        clampGatePercent(static_cast<int>(base.gate) + result.gateDelta, maxGatePercent);
    result.resolved.nudge = clampNudge(static_cast<int>(base.nudge) + result.nudgeDelta);
    return result;
}

inline StepSequencerResolvedVariation resolveStepVariation(
    StepSequencerStepValues base,
    StepSequencerVariationRanges ranges,
    uint16_t maxGatePercent,
    uint32_t runSeed,
    uint32_t cycleIndex,
    uint8_t stepIndex,
    bool triggered = true,
    uint32_t stepIdentity = UINT32_MAX
) {
    return resolveStepVariation(
        base,
        ranges,
        StepSequencerScaleSettings{},
        maxGatePercent,
        runSeed,
        cycleIndex,
        stepIndex,
        triggered,
        stepIdentity
    );
}

}  // namespace oc::note::sequencer
