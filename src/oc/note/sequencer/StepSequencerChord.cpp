#include "StepSequencerChord.hpp"

#include <config/PlatformCompat.hpp>

#include <algorithm>
#include <initializer_list>

namespace oc::note::sequencer {

namespace {

constexpr uint8_t PALETTE_COUNT = StepSequencerChordSpec::MAX_COLOR + 1;
constexpr uint8_t VARIANT_COUNT = StepSequencerChordSpec::MAX_VARIANT + 1;
constexpr uint8_t VOICE_COUNT = StepSequencerChordSpec::MAX_VOICES;
constexpr uint8_t ANALYSIS_ROOT_CANDIDATE_COUNT = StepSequencerChordAnalysis::MAX_VOICES + 2;

// Color selects the harmonic family. Variant then selects a voicing path through
// that family, so both axes remain deterministic and musically distinct.
const int16_t CHROMATIC_FAMILY_INTERVALS[PALETTE_COUNT][VOICE_COUNT] PROGMEM = {
    {0, 4, 7, 11, 14, 16, 19, 23},
    {0, 3, 7, 10, 14, 15, 19, 22},
    {0, 5, 7, 10, 14, 17, 19, 22},
    {0, 5, 10, 14, 17, 22, 26, 29},
    {0, 7, 14, 19, 24, 26, 31, 36},
    {0, 4, 9, 14, 16, 21, 23, 28},
    {0, 3, 6, 10, 13, 16, 21, 24},
    {0, 2, 6, 9, 13, 17, 20, 23},
};

const int16_t DEGREE_FAMILY_INTERVALS[PALETTE_COUNT][VOICE_COUNT] PROGMEM = {
    {0, 2, 4, 6, 8, 10, 12, 14},
    {0, 2, 5, 6, 8, 10, 13, 14},
    {0, 3, 4, 6, 9, 10, 12, 15},
    {0, 3, 6, 9, 12, 15, 18, 21},
    {0, 4, 8, 11, 14, 15, 18, 22},
    {0, 1, 4, 7, 9, 11, 14, 16},
    {0, 1, 5, 6, 10, 12, 13, 17},
    {0, 2, 6, 8, 11, 13, 15, 18},
};

const uint8_t VARIANT_INTERVAL_PICK[VARIANT_COUNT][VOICE_COUNT] PROGMEM = {
    {0, 1, 2, 3, 4, 5, 6, 7},
    {0, 1, 3, 2, 4, 5, 6, 7},
    {0, 2, 3, 1, 4, 5, 6, 7},
    {0, 1, 4, 2, 3, 5, 6, 7},
    {0, 2, 4, 1, 3, 5, 6, 7},
    {0, 3, 4, 1, 2, 5, 6, 7},
    {0, 1, 5, 2, 3, 4, 6, 7},
    {0, 2, 5, 1, 3, 4, 6, 7},
};

// Spread is intentionally stepped: each value maps to a named-style spacing
// profile instead of a continuous formula that can produce inaudible ranges.
const uint8_t SPREAD_OCTAVE_SHIFTS[StepSequencerChordSpec::MAX_SPREAD + 1][VOICE_COUNT] PROGMEM = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 1, 1, 1, 1, 1},
    {0, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 2, 2, 2, 3, 3, 3},
    {0, 1, 2, 2, 3, 3, 3, 4},
    {0, 2, 2, 3, 3, 4, 4, 5},
    {0, 1, 3, 3, 4, 5, 5, 6},
    {0, 2, 3, 4, 4, 5, 6, 7},
};

struct ChordQualityPattern {
    StepSequencerChordQuality quality = StepSequencerChordQuality::Unknown;
    uint16_t mask = 0;
};

constexpr uint16_t intervalBit(uint8_t interval) {
    return static_cast<uint16_t>(1U << (interval % 12U));
}

constexpr uint16_t intervalMask(std::initializer_list<uint8_t> intervals) {
    uint16_t mask = 0;
    for (uint8_t interval : intervals) {
        mask = static_cast<uint16_t>(mask | intervalBit(interval));
    }
    return mask;
}

const ChordQualityPattern CHORD_QUALITY_PATTERNS[] PROGMEM = {
    {StepSequencerChordQuality::Major9, intervalMask({0, 2, 4, 7, 11})},
    {StepSequencerChordQuality::Minor9, intervalMask({0, 2, 3, 7, 10})},
    {StepSequencerChordQuality::Dominant9, intervalMask({0, 2, 4, 7, 10})},
    {StepSequencerChordQuality::Major7, intervalMask({0, 4, 7, 11})},
    {StepSequencerChordQuality::MinorMajor7, intervalMask({0, 3, 7, 11})},
    {StepSequencerChordQuality::Minor7, intervalMask({0, 3, 7, 10})},
    {StepSequencerChordQuality::Dominant7, intervalMask({0, 4, 7, 10})},
    {StepSequencerChordQuality::HalfDiminished7, intervalMask({0, 3, 6, 10})},
    {StepSequencerChordQuality::Diminished7, intervalMask({0, 3, 6, 9})},
    {StepSequencerChordQuality::Major6, intervalMask({0, 4, 7, 9})},
    {StepSequencerChordQuality::Minor6, intervalMask({0, 3, 7, 9})},
    {StepSequencerChordQuality::Add9, intervalMask({0, 2, 4, 7})},
    {StepSequencerChordQuality::MinorAdd9, intervalMask({0, 2, 3, 7})},
    {StepSequencerChordQuality::Major, intervalMask({0, 4, 7})},
    {StepSequencerChordQuality::Minor, intervalMask({0, 3, 7})},
    {StepSequencerChordQuality::Diminished, intervalMask({0, 3, 6})},
    {StepSequencerChordQuality::Augmented, intervalMask({0, 4, 8})},
    {StepSequencerChordQuality::Sus2, intervalMask({0, 2, 7})},
    {StepSequencerChordQuality::Sus4, intervalMask({0, 5, 7})},
    {StepSequencerChordQuality::Power, intervalMask({0, 7})},
};

uint8_t clampMidiValue(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

uint8_t chordPitchClass(uint8_t note) {
    return static_cast<uint8_t>(note % 12U);
}

uint8_t chromaticDistance(uint8_t fromPitchClass, uint8_t toPitchClass) {
    return static_cast<uint8_t>((toPitchClass + 12U - fromPitchClass) % 12U);
}

bool containsPitchClass(const StepSequencerChordAnalysis& analysis, uint8_t pitchClassValue) {
    for (uint8_t i = 0; i < analysis.pitchClassCount; ++i) {
        if (analysis.pitchClasses[i] == pitchClassValue) return true;
    }
    return false;
}

void appendPitchClass(StepSequencerChordAnalysis& analysis, uint8_t pitchClassValue) {
    if (analysis.pitchClassCount >= analysis.pitchClasses.size()) return;
    if (containsPitchClass(analysis, pitchClassValue)) return;
    analysis.pitchClasses[analysis.pitchClassCount++] = pitchClassValue;
}

uint16_t chordMaskForRoot(const StepSequencerChordAnalysis& analysis, uint8_t rootPitchClass) {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < analysis.pitchClassCount; ++i) {
        mask = static_cast<uint16_t>(
            mask | intervalBit(chromaticDistance(rootPitchClass, analysis.pitchClasses[i]))
        );
    }
    return mask;
}

StepSequencerChordQuality qualityForMask(uint16_t mask) {
    for (const auto& pattern : CHORD_QUALITY_PATTERNS) {
        if (pattern.mask == mask) return pattern.quality;
    }
    return StepSequencerChordQuality::Unknown;
}

void buildChromaticIntervals(StepSequencerChordAnalysis& analysis) {
    analysis.intervalCount = 0;
    for (uint8_t interval = 0; interval < 12U; ++interval) {
        const uint8_t pitch = static_cast<uint8_t>((analysis.rootPitchClass + interval) % 12U);
        if (!containsPitchClass(analysis, pitch)) continue;
        if (analysis.intervalCount >= analysis.chromaticIntervals.size()) return;
        analysis.chromaticIntervals[analysis.intervalCount++] = interval;
    }
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
                       uint8_t voiceCount,
                       uint8_t spread,
                       bool usesScaleDegrees,
                       StepSequencerScaleSettings scaleSettings) {
    if (spread == 0 || voiceIndex == 0) return interval;

    const uint8_t octaveSize = usesScaleDegrees ? scaleDegreeOctaveSpan(scaleSettings) : 12U;
    const uint8_t octaves = voiceCount <= 2
        ? static_cast<uint8_t>((spread + 1U) / 2U)
        : SPREAD_OCTAVE_SHIFTS[spread][voiceIndex];
    return static_cast<int16_t>(interval + static_cast<int16_t>(octaveSize * octaves));
}

int16_t chordFamilyInterval(uint8_t palette,
                            uint8_t variant,
                            uint8_t voiceIndex,
                            int16_t previousInterval,
                            uint8_t octaveSize,
                            bool usesScaleDegrees) {
    const uint8_t pick = VARIANT_INTERVAL_PICK[variant][voiceIndex];
    const int16_t* family = usesScaleDegrees
        ? DEGREE_FAMILY_INTERVALS[palette]
        : CHROMATIC_FAMILY_INTERVALS[palette];
    int16_t interval = family[pick];

    while (voiceIndex > 0 && interval <= previousInterval) {
        interval = static_cast<int16_t>(interval + octaveSize);
    }

    return interval;
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
    return clampMidiValue(static_cast<int>(rootVelocity) + delta);
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
                                              uint16_t spanTicks,
                                              bool pitchUsesScaleDegrees) {
    scaleSettings.clamp();
    chord.local.clamp();
    inherited.spec.clamp();

    StepSequencerChordResolution result{};
    result.intervalUsesScaleDegrees =
        pitchUsesScaleDegrees && scaleSettings.isConstrained();

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
    const uint8_t octaveSize = result.intervalUsesScaleDegrees
        ? scaleDegreeOctaveSpan(scaleSettings)
        : 12U;
    int16_t previousBaseInterval = 0;

    for (uint8_t i = 0; i < voiceCount; ++i) {
        const int16_t baseInterval = chordFamilyInterval(
            palette,
            variant,
            i,
            previousBaseInterval,
            octaveSize,
            result.intervalUsesScaleDegrees
        );
        previousBaseInterval = baseInterval;

        const int16_t interval = spreadInterval(
            baseInterval,
            i,
            voiceCount,
            spec->spread,
            result.intervalUsesScaleDegrees,
            scaleSettings
        );
        const uint8_t note = result.intervalUsesScaleDegrees
            ? moveByScaleDegrees(root.note, static_cast<int8_t>(interval), scaleSettings)
            : clampMidiValue(static_cast<int>(root.note) + static_cast<int>(interval));

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

StepSequencerChordAnalysis analyzeResolvedChord(
    const StepSequencerChordResolution& resolution,
    StepSequencerStepValues root
) {
    StepSequencerChordAnalysis analysis{};
    analysis.rootPitchClass = chordPitchClass(root.note);
    analysis.bassPitchClass = analysis.rootPitchClass;

    if (resolution.count == 0) {
        buildChromaticIntervals(analysis);
        return analysis;
    }

    uint8_t bassNote = resolution.voices[0].note;
    for (uint8_t i = 0; i < resolution.count; ++i) {
        appendPitchClass(analysis, chordPitchClass(resolution.voices[i].note));
        bassNote = std::min<uint8_t>(bassNote, resolution.voices[i].note);
    }
    analysis.bassPitchClass = chordPitchClass(bassNote);

    const uint8_t preferredRoot = chordPitchClass(root.note);
    uint8_t candidates[ANALYSIS_ROOT_CANDIDATE_COUNT] = {};
    uint8_t candidateCount = 0;
    auto appendCandidate = [&](uint8_t candidate) {
        for (uint8_t i = 0; i < candidateCount; ++i) {
            if (candidates[i] == candidate) return;
        }
        if (candidateCount >= ANALYSIS_ROOT_CANDIDATE_COUNT) return;
        candidates[candidateCount++] = candidate;
    };

    appendCandidate(preferredRoot);
    appendCandidate(analysis.bassPitchClass);
    for (uint8_t i = 0; i < analysis.pitchClassCount; ++i) {
        appendCandidate(analysis.pitchClasses[i]);
    }

    for (uint8_t i = 0; i < candidateCount; ++i) {
        const uint8_t candidate = candidates[i];
        const auto quality = qualityForMask(chordMaskForRoot(analysis, candidate));
        if (quality == StepSequencerChordQuality::Unknown) continue;

        analysis.rootPitchClass = candidate;
        analysis.quality = quality;
        analysis.recognized = true;
        analysis.slash = analysis.bassPitchClass != analysis.rootPitchClass;
        buildChromaticIntervals(analysis);
        return analysis;
    }

    analysis.rootPitchClass = preferredRoot;
    analysis.quality = StepSequencerChordQuality::Unknown;
    analysis.recognized = false;
    analysis.slash = false;
    buildChromaticIntervals(analysis);
    return analysis;
}

}  // namespace oc::note::sequencer
