#include "StepSequencerChord.hpp"

#include <algorithm>

namespace oc::note::sequencer {

namespace {

constexpr uint8_t PALETTE_COUNT = 4;
constexpr uint8_t VARIANT_COUNT = 4;
constexpr uint8_t VOICE_COUNT = StepSequencerChordSpec::MAX_VOICES;

constexpr int16_t CHROMATIC_INTERVALS[PALETTE_COUNT][VARIANT_COUNT][VOICE_COUNT] = {
    {
        {0, 4, 7, 11, 14, 17, 21, 24},
        {0, 4, 7, 9, 12, 16, 19, 24},
        {0, 5, 7, 11, 14, 17, 21, 24},
        {0, 4, 8, 11, 14, 20, 23, 26},
    },
    {
        {0, 3, 7, 10, 14, 17, 21, 24},
        {0, 3, 7, 9, 12, 15, 19, 22},
        {0, 5, 7, 10, 14, 17, 19, 22},
        {0, 3, 6, 10, 13, 17, 20, 24},
    },
    {
        {0, 5, 7, 10, 14, 17, 21, 24},
        {0, 2, 7, 10, 14, 19, 22, 26},
        {0, 5, 9, 12, 17, 21, 24, 28},
        {0, 7, 10, 14, 17, 22, 26, 29},
    },
    {
        {0, 4, 7, 10, 14, 18, 21, 25},
        {0, 3, 7, 11, 14, 17, 22, 24},
        {0, 4, 6, 11, 13, 18, 20, 25},
        {0, 2, 6, 9, 13, 16, 21, 23},
    },
};

constexpr int16_t DEGREE_INTERVALS[PALETTE_COUNT][VARIANT_COUNT][VOICE_COUNT] = {
    {
        {0, 2, 4, 6, 8, 10, 12, 14},
        {0, 2, 4, 5, 7, 9, 11, 13},
        {0, 3, 4, 6, 8, 10, 12, 14},
        {0, 2, 5, 6, 8, 11, 12, 14},
    },
    {
        {0, 2, 4, 6, 8, 10, 12, 14},
        {0, 2, 4, 5, 7, 9, 11, 13},
        {0, 2, 5, 6, 8, 10, 13, 14},
        {0, 3, 5, 6, 9, 10, 12, 15},
    },
    {
        {0, 3, 4, 6, 9, 10, 12, 15},
        {0, 1, 4, 6, 8, 11, 13, 15},
        {0, 3, 5, 7, 10, 12, 14, 17},
        {0, 4, 6, 8, 11, 13, 15, 18},
    },
    {
        {0, 2, 4, 5, 8, 10, 11, 14},
        {0, 2, 3, 6, 8, 9, 12, 14},
        {0, 1, 4, 5, 7, 10, 11, 13},
        {0, 3, 4, 7, 8, 11, 12, 15},
    },
};

uint8_t clampMidi(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

uint8_t clampVelocity(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

int8_t clampSigned(int value, int8_t minValue, int8_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return static_cast<int8_t>(value);
}

uint8_t scaleDegreeOctaveSpan(StepSequencerScaleSettings settings) {
    settings.clamp();
    uint16_t mask = scaleMask(settings.type);
    uint8_t count = 0;
    for (uint8_t bit = 0; bit < 12; ++bit) {
        if ((mask & static_cast<uint16_t>(1U << bit)) != 0) {
            ++count;
        }
    }
    return count == 0 ? 12 : count;
}

int16_t spreadInterval(int16_t interval,
                       uint8_t voiceIndex,
                       uint8_t spread,
                       bool usesScaleDegrees,
                       StepSequencerScaleSettings scaleSettings) {
    if (spread == 0 || voiceIndex == 0) return interval;

    const uint8_t octaveSize = usesScaleDegrees ? scaleDegreeOctaveSpan(scaleSettings) : 12U;
    const uint8_t octaves = static_cast<uint8_t>(
        (static_cast<uint16_t>(voiceIndex) * spread) / 8U
    );
    return static_cast<int16_t>(interval + static_cast<int16_t>(octaveSize * octaves));
}

uint16_t voiceDelayTicks(uint8_t voiceIndex,
                         uint8_t voiceCount,
                         int8_t strum,
                         uint16_t spanTicks) {
    if (voiceCount <= 1 || strum == 0 || spanTicks <= 1) return 0;

    const uint16_t absStrum = strum < 0
        ? static_cast<uint16_t>(-static_cast<int16_t>(strum))
        : static_cast<uint16_t>(strum);
    uint32_t totalDelay =
        (static_cast<uint32_t>(spanTicks) * static_cast<uint32_t>(absStrum)) / 100U;
    if (totalDelay >= spanTicks) {
        totalDelay = static_cast<uint32_t>(spanTicks - 1U);
    }

    const uint8_t position = strum >= 0
        ? voiceIndex
        : static_cast<uint8_t>((voiceCount - 1U) - voiceIndex);
    return static_cast<uint16_t>(
        (totalDelay * static_cast<uint32_t>(position)) /
        static_cast<uint32_t>(voiceCount - 1U)
    );
}

uint8_t voiceVelocity(uint8_t rootVelocity,
                      uint8_t voiceIndex,
                      uint8_t voiceCount,
                      int8_t velocityCurve) {
    if (voiceCount <= 1 || velocityCurve == 0) return rootVelocity;
    const int delta =
        (static_cast<int>(velocityCurve) * static_cast<int>(voiceIndex)) /
        static_cast<int>(voiceCount - 1U);
    return clampVelocity(static_cast<int>(rootVelocity) + delta);
}

const StepSequencerChordSpec* effectiveSpec(const StepSequencerChordState& chord,
                                            const StepSequencerInheritedChord& inherited,
                                            StepSequencerChordSource& source) {
    switch (chord.mode) {
        case StepSequencerChordMode::Local:
            source = StepSequencerChordSource::Local;
            return &chord.local;

        case StepSequencerChordMode::Inherit:
            if (inherited.valid) {
                source = StepSequencerChordSource::Inherited;
                return &inherited.spec;
            }
            source = StepSequencerChordSource::Single;
            return nullptr;

        case StepSequencerChordMode::Single:
            source = StepSequencerChordSource::Single;
            return nullptr;
    }

    source = StepSequencerChordSource::Single;
    return nullptr;
}

void appendVoice(StepSequencerChordResolution& result,
                 StepSequencerResolvedChordVoice voice) {
    for (uint8_t i = 0; i < result.count; ++i) {
        auto& existing = result.voices[i];
        if (existing.note != voice.note) continue;

        existing.velocity = std::max(existing.velocity, voice.velocity);
        existing.gate = std::max(existing.gate, voice.gate);
        existing.delayTicks = std::min(existing.delayTicks, voice.delayTicks);
        return;
    }

    if (result.count >= result.voices.size()) return;
    result.voices[result.count++] = voice;
}

}  // namespace

void StepSequencerChordSpec::clamp() {
    if (voiceCount == 0) voiceCount = 1;
    if (voiceCount > MAX_VOICES) voiceCount = MAX_VOICES;
    if (color > MAX_COLOR) color = MAX_COLOR;
    if (variant > MAX_VARIANT) variant = MAX_VARIANT;
    if (spread > MAX_SPREAD) spread = MAX_SPREAD;
    strum = clampSigned(strum, MIN_STRUM, MAX_STRUM);
    velocityCurve = clampSigned(velocityCurve, MIN_VELOCITY_CURVE, MAX_VELOCITY_CURVE);
}

StepSequencerChordState defaultRootChordState() {
    StepSequencerChordState state{};
    state.mode = StepSequencerChordMode::Single;
    return state;
}

StepSequencerChordState defaultChildChordState() {
    StepSequencerChordState state{};
    state.mode = StepSequencerChordMode::Inherit;
    return state;
}

StepSequencerChordResolution resolveStepChord(StepSequencerStepValues root,
                                              StepSequencerScaleSettings scaleSettings,
                                              StepSequencerChordState chord,
                                              StepSequencerInheritedChord inherited,
                                              uint16_t spanTicks) {
    scaleSettings.clamp();
    chord.local.clamp();
    inherited.spec.clamp();

    StepSequencerChordResolution result{};
    result.intervalUsesScaleDegrees = scaleSettings.isConstrained();

    StepSequencerChordSource source = StepSequencerChordSource::Single;
    const StepSequencerChordSpec* spec = effectiveSpec(chord, inherited, source);
    result.source = source;

    if (spec == nullptr) {
        appendVoice(
            result,
            StepSequencerResolvedChordVoice{
                .note = root.note,
                .velocity = root.velocity,
                .gate = root.gate,
                .nudge = root.nudge,
                .delayTicks = 0,
                .interval = 0,
                .intervalUsesScaleDegrees = result.intervalUsesScaleDegrees,
            }
        );
        return result;
    }

    result.activeForChildren.valid = true;
    result.activeForChildren.spec = *spec;

    const uint8_t palette = static_cast<uint8_t>(spec->color % PALETTE_COUNT);
    const uint8_t variant = static_cast<uint8_t>(spec->variant % VARIANT_COUNT);
    const uint8_t voiceCount = spec->voiceCount;

    for (uint8_t i = 0; i < voiceCount; ++i) {
        const int16_t baseInterval = result.intervalUsesScaleDegrees
            ? DEGREE_INTERVALS[palette][variant][i]
            : CHROMATIC_INTERVALS[palette][variant][i];
        const int16_t interval = spreadInterval(
            baseInterval,
            i,
            spec->spread,
            result.intervalUsesScaleDegrees,
            scaleSettings
        );
        const uint8_t note = result.intervalUsesScaleDegrees
            ? moveByScaleDegrees(root.note, static_cast<int8_t>(interval), scaleSettings)
            : clampMidi(static_cast<int>(root.note) + static_cast<int>(interval));

        appendVoice(
            result,
            StepSequencerResolvedChordVoice{
                .note = note,
                .velocity = voiceVelocity(root.velocity, i, voiceCount, spec->velocityCurve),
                .gate = root.gate,
                .nudge = root.nudge,
                .delayTicks = voiceDelayTicks(i, voiceCount, spec->strum, spanTicks),
                .interval = interval,
                .intervalUsesScaleDegrees = result.intervalUsesScaleDegrees,
            }
        );
    }

    return result;
}

}  // namespace oc::note::sequencer
