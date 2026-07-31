#include "StepSequencerChord.hpp"

#include <config/PlatformCompat.hpp>

#include <algorithm>
#include <initializer_list>

namespace oc::note::sequencer {
namespace {

constexpr uint8_t VOICE_COUNT = StepSequencerChordSpec::MAX_VOICES;
constexpr uint8_t ANALYSIS_ROOT_CANDIDATE_COUNT =
    StepSequencerChordAnalysis::MAX_VOICES + 2;

// Voicing is a register transform applied after true inversion. Shape
// membership itself is resolved by StepSequencerChordSpec.cpp.
const uint8_t SEMANTIC_VOICING_OCTAVE_SHIFTS[
    static_cast<uint8_t>(StepSequencerChordVoicing::Count)
][VOICE_COUNT] PROGMEM = {
    {0, 0, 0, 0, 0, 0, 0, 0},  // Close
    {0, 0, 1, 0, 1, 0, 1, 0},  // Open
    {0, 1, 1, 2, 2, 3, 3, 4},  // Wide
};

struct ChordQualityPattern {
    StepSequencerChordQuality quality = StepSequencerChordQuality::Unknown;
    uint16_t mask = 0;
};

constexpr uint16_t intervalBit(uint8_t interval) {
    return static_cast<uint16_t>(1U << (interval % 12U));
}

constexpr uint16_t intervalMask(
    std::initializer_list<uint8_t> intervals
) {
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
    {
        StepSequencerChordQuality::MinorMajor7,
        intervalMask({0, 3, 7, 11}),
    },
    {StepSequencerChordQuality::Minor7, intervalMask({0, 3, 7, 10})},
    {StepSequencerChordQuality::Dominant7, intervalMask({0, 4, 7, 10})},
    {
        StepSequencerChordQuality::HalfDiminished7,
        intervalMask({0, 3, 6, 10}),
    },
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

FLASHMEM uint8_t chordPitchClass(uint8_t note) {
    return static_cast<uint8_t>(note % 12U);
}

FLASHMEM uint8_t chromaticDistance(
    uint8_t fromPitchClass,
    uint8_t toPitchClass
) {
    return static_cast<uint8_t>(
        (toPitchClass + 12U - fromPitchClass) % 12U
    );
}

FLASHMEM bool containsPitchClass(
    const StepSequencerChordAnalysis& analysis,
    uint8_t pitchClassValue
) {
    for (uint8_t i = 0; i < analysis.pitchClassCount; ++i) {
        if (analysis.pitchClasses[i] == pitchClassValue) return true;
    }
    return false;
}

FLASHMEM void appendPitchClass(
    StepSequencerChordAnalysis& analysis,
    uint8_t pitchClassValue
) {
    if (analysis.pitchClassCount >= analysis.pitchClasses.size()) return;
    if (containsPitchClass(analysis, pitchClassValue)) return;
    analysis.pitchClasses[analysis.pitchClassCount++] = pitchClassValue;
}

FLASHMEM uint16_t chordMaskForRoot(
    const StepSequencerChordAnalysis& analysis,
    uint8_t rootPitchClass
) {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < analysis.pitchClassCount; ++i) {
        mask = static_cast<uint16_t>(
            mask |
            intervalBit(chromaticDistance(
                rootPitchClass,
                analysis.pitchClasses[i]
            ))
        );
    }
    return mask;
}

FLASHMEM StepSequencerChordQuality qualityForMask(uint16_t mask) {
    for (const auto& pattern : CHORD_QUALITY_PATTERNS) {
        if (pattern.mask == mask) return pattern.quality;
    }
    return StepSequencerChordQuality::Unknown;
}

FLASHMEM void buildChromaticIntervals(
    StepSequencerChordAnalysis& analysis
) {
    analysis.intervalCount = 0;
    for (uint8_t interval = 0; interval < 12U; ++interval) {
        const uint8_t pitch = static_cast<uint8_t>(
            (analysis.rootPitchClass + interval) % 12U
        );
        if (!containsPitchClass(analysis, pitch)) continue;
        if (analysis.intervalCount >= analysis.chromaticIntervals.size()) {
            return;
        }
        analysis.chromaticIntervals[analysis.intervalCount++] = interval;
    }
}

StepSequencerChordIntervalBasis contextIntervalBasis(
    StepSequencerScaleSettings scaleSettings,
    bool pitchFollowsScale
) {
    return pitchFollowsScale && scaleSettings.isConstrained()
        ? StepSequencerChordIntervalBasis::ScaleDegrees
        : StepSequencerChordIntervalBasis::ChromaticSemitones;
}

uint8_t scaleDegreeOctaveSpan(
    StepSequencerScaleSettings settings
) {
    settings.clamp();
    const uint16_t mask = scaleMask(settings.type);
    uint8_t count = 0;
    for (uint8_t bit = 0; bit < 12; ++bit) {
        if ((mask & static_cast<uint16_t>(1U << bit)) != 0) {
            ++count;
        }
    }
    return count == 0 ? 12 : count;
}

void applyCyclicInversion(
    std::array<int16_t, VOICE_COUNT>& intervals,
    uint8_t voiceCount,
    uint8_t inversion,
    uint8_t octaveSize
) {
    std::sort(intervals.begin(), intervals.begin() + voiceCount);
    for (uint8_t i = 0; i < inversion; ++i) {
        const int16_t lowest = intervals[0];
        const int16_t highest = intervals[voiceCount - 1U];
        const int16_t octaveCount = static_cast<int16_t>(
            ((highest - lowest) / octaveSize) + 1
        );
        const int16_t raised = static_cast<int16_t>(
            lowest + octaveCount * octaveSize
        );
        for (uint8_t voice = 1U; voice < voiceCount; ++voice) {
            intervals[voice - 1U] = intervals[voice];
        }
        intervals[voiceCount - 1U] = raised;
    }
}

void applySemanticVoicing(
    std::array<int16_t, VOICE_COUNT>& intervals,
    uint8_t voiceCount,
    StepSequencerChordVoicing voicing,
    uint8_t octaveSize
) {
    const auto voicingIndex = static_cast<uint8_t>(voicing);
    for (uint8_t i = 0; i < voiceCount; ++i) {
        const uint8_t octaveShift =
            SEMANTIC_VOICING_OCTAVE_SHIFTS[voicingIndex][i];
        intervals[i] = static_cast<int16_t>(
            intervals[i] +
            static_cast<int16_t>(octaveSize * octaveShift)
        );
    }
    std::sort(intervals.begin(), intervals.begin() + voiceCount);
    for (uint8_t voice = 1U; voice < voiceCount; ++voice) {
        if (intervals[voice] > intervals[voice - 1U]) continue;
        const int16_t octaveCount = static_cast<int16_t>(
            ((intervals[voice - 1U] - intervals[voice]) /
             octaveSize) +
            1
        );
        intervals[voice] = static_cast<int16_t>(
            intervals[voice] + octaveCount * octaveSize
        );
    }
}

int16_t unboundedScaleDegreeNote(
    uint8_t rootNote,
    int16_t degreeOffset,
    StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    scaleSettings.mode =
        StepSequencerScaleConstraintMode::ConstrainNearest;
    int16_t current = resolveScaleNote(
        rootNote,
        scaleSettings
    ).outputNote;
    const int direction = degreeOffset < 0 ? -1 : 1;
    int remaining = degreeOffset < 0
        ? -static_cast<int>(degreeOffset)
        : static_cast<int>(degreeOffset);
    while (remaining-- > 0) {
        do {
            current = static_cast<int16_t>(current + direction);
            const int pitchClassValue =
                (static_cast<int>(current) % 12 + 12) % 12;
            if (scaleContainsPitchClass(
                    scaleSettings,
                    static_cast<uint8_t>(pitchClassValue)
                )) {
                break;
            }
        } while (true);
    }
    return current;
}

struct SemanticChordPlacement {
    bool valid = false;
    StepSequencerChordVoicing voicing =
        StepSequencerChordVoicing::Close;
    int8_t registerShiftOctaves = 0;
    std::array<int16_t, VOICE_COUNT> intervals{};
    std::array<uint8_t, VOICE_COUNT> notes{};
};

bool trySemanticPlacement(
    SemanticChordPlacement& placement,
    StepSequencerStepValues root,
    StepSequencerScaleSettings scaleSettings,
    const StepSequencerChordFormula& formula,
    StepSequencerChordVoicing voicing,
    uint8_t inversion,
    uint8_t octaveSize
) {
    std::array<int16_t, VOICE_COUNT> intervals{};
    for (uint8_t voice = 0U; voice < formula.count; ++voice) {
        intervals[voice] = formula.intervals[voice];
    }
    applyCyclicInversion(
        intervals,
        formula.count,
        inversion,
        octaveSize
    );
    applySemanticVoicing(
        intervals,
        formula.count,
        voicing,
        octaveSize
    );

    std::array<int16_t, VOICE_COUNT> absoluteNotes{};
    for (uint8_t voice = 0U; voice < formula.count; ++voice) {
        absoluteNotes[voice] = formula.intervalUsesScaleDegrees
            ? unboundedScaleDegreeNote(
                  root.note,
                  intervals[voice],
                  scaleSettings
              )
            : static_cast<int16_t>(
                  static_cast<int16_t>(root.note) + intervals[voice]
              );
        if (voice > 0U &&
            absoluteNotes[voice] <= absoluteNotes[voice - 1U]) {
            return false;
        }
    }

    bool foundShift = false;
    int bestShift = 0;
    int bestMagnitude = 0;
    for (int shift = -16; shift <= 16; ++shift) {
        const int shiftedLowest =
            static_cast<int>(absoluteNotes[0]) + shift * 12;
        const int shiftedHighest =
            static_cast<int>(absoluteNotes[formula.count - 1U]) +
            shift * 12;
        if (shiftedLowest < 0 || shiftedHighest > 127) continue;

        const int magnitude = shift < 0 ? -shift : shift;
        if (!foundShift || magnitude < bestMagnitude ||
            (magnitude == bestMagnitude && shift < bestShift)) {
            foundShift = true;
            bestShift = shift;
            bestMagnitude = magnitude;
        }
    }
    if (!foundShift) return false;

    const int16_t intervalShift = static_cast<int16_t>(
        bestShift * static_cast<int>(octaveSize)
    );
    placement = {};
    placement.valid = true;
    placement.voicing = voicing;
    placement.registerShiftOctaves =
        static_cast<int8_t>(bestShift);
    for (uint8_t voice = 0U; voice < formula.count; ++voice) {
        placement.intervals[voice] = static_cast<int16_t>(
            intervals[voice] + intervalShift
        );
        placement.notes[voice] = static_cast<uint8_t>(
            static_cast<int>(absoluteNotes[voice]) + bestShift * 12
        );
    }
    return true;
}

SemanticChordPlacement buildSemanticPlacement(
    StepSequencerStepValues root,
    StepSequencerScaleSettings scaleSettings,
    const StepSequencerChordFormula& formula,
    StepSequencerChordVoicing requestedVoicing,
    uint8_t inversion,
    uint8_t octaveSize
) {
    const uint8_t requested =
        static_cast<uint8_t>(requestedVoicing);
    for (int candidate = requested; candidate >= 0; --candidate) {
        SemanticChordPlacement placement{};
        if (trySemanticPlacement(
                placement,
                root,
                scaleSettings,
                formula,
                static_cast<StepSequencerChordVoicing>(candidate),
                inversion,
                octaveSize
            )) {
            return placement;
        }
    }
    return {};
}

uint16_t voiceDelayTicks(
    uint8_t voiceIndex,
    uint8_t voiceCount,
    int8_t strum,
    uint16_t spanTicks
) {
    if (voiceCount <= 1 || strum == 0 || spanTicks <= 1) return 0;

    const uint16_t absStrum = strum < 0
        ? static_cast<uint16_t>(-static_cast<int16_t>(strum))
        : static_cast<uint16_t>(strum);
    uint32_t totalDelay =
        (static_cast<uint32_t>(spanTicks) *
         static_cast<uint32_t>(absStrum)) /
        100U;
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

uint8_t voiceVelocity(
    uint8_t rootVelocity,
    uint8_t voiceIndex,
    uint8_t voiceCount,
    int8_t velocityCurve
) {
    if (voiceCount <= 1 || velocityCurve == 0) return rootVelocity;
    const int delta =
        (static_cast<int>(velocityCurve) * static_cast<int>(voiceIndex)) /
        static_cast<int>(voiceCount - 1U);
    return clampMidiValue(static_cast<int>(rootVelocity) + delta);
}

const StepSequencerChordSpec* effectiveSpec(
    const StepSequencerChordState& chord,
    const StepSequencerInheritedChord& inherited,
    StepSequencerChordSource& source
) {
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

void appendVoice(
    StepSequencerChordResolution& result,
    StepSequencerResolvedChordVoice voice
) {
    for (uint8_t i = 0; i < result.count; ++i) {
        auto& existing = result.voices[i];
        if (existing.note != voice.note) continue;

        existing.velocity = std::max(existing.velocity, voice.velocity);
        existing.gate = std::max(existing.gate, voice.gate);
        existing.delayTicks = std::min(
            existing.delayTicks,
            voice.delayTicks
        );
        return;
    }

    if (result.count >= result.voices.size()) return;
    result.voices[result.count++] = voice;
}

}  // namespace

FLASHMEM StepSequencerChordState defaultRootChordState() {
    StepSequencerChordState state{};
    state.mode = StepSequencerChordMode::Single;
    return state;
}

FLASHMEM StepSequencerChordState defaultChildChordState() {
    StepSequencerChordState state{};
    state.mode = StepSequencerChordMode::Inherit;
    return state;
}

StepSequencerChordResolution resolveStepChord(
    StepSequencerStepValues root,
    StepSequencerScaleSettings scaleSettings,
    StepSequencerChordState chord,
    StepSequencerInheritedChord inherited,
    uint16_t spanTicks,
    bool pitchFollowsScale
) {
    scaleSettings.clamp();
    chord.local.clamp();
    inherited.spec.clamp();

    StepSequencerChordResolution result{};
    StepSequencerChordSource source = StepSequencerChordSource::Single;
    const StepSequencerChordSpec* spec = effectiveSpec(
        chord,
        inherited,
        source
    );
    result.source = source;
    result.requestedIntervalBasis =
        spec != nullptr
            ? spec->intervalBasis()
            : StepSequencerChordIntervalBasis::FollowPitchContext;
    result.intervalBasis = contextIntervalBasis(
        scaleSettings,
        pitchFollowsScale
    );
    result.intervalBasisAdjusted =
        result.requestedIntervalBasis !=
            StepSequencerChordIntervalBasis::FollowPitchContext &&
        result.requestedIntervalBasis != result.intervalBasis;
    result.intervalUsesScaleDegrees =
        result.intervalBasis ==
        StepSequencerChordIntervalBasis::ScaleDegrees;

    if (spec == nullptr) {
        result.requestedVoiceCount = 1;
        appendVoice(
            result,
            StepSequencerResolvedChordVoice{
                .note = root.note,
                .velocity = root.velocity,
                .gate = root.gate,
                .nudge = root.nudge,
                .delayTicks = 0,
                .interval = 0,
                .intervalUsesScaleDegrees =
                    result.intervalUsesScaleDegrees,
                .inSelectedScale =
                    scaleSettings.type ==
                        StepSequencerScaleType::Chromatic ||
                    scaleContainsNote(scaleSettings, root.note),
            }
        );
        return result;
    }

    result.activeForChildren.valid = true;
    result.activeForChildren.spec = *spec;

    const uint8_t voiceCount = spec->voices();
    result.requestedVoiceCount = voiceCount;
    const uint8_t octaveSize = result.intervalUsesScaleDegrees
        ? scaleDegreeOctaveSpan(scaleSettings)
        : 12U;

    const auto formula = resolveChordFormula(
        *spec,
        result.intervalUsesScaleDegrees
    );
    result.harmony = formula.harmony;
    result.harmonyAdjustedForPitchMode =
        formula.harmonyAdjustedForPitchMode;
    result.voicing = spec->voicing();
    const uint8_t maximumInversion = static_cast<uint8_t>(
        voiceCount - 1U
    );
    result.effectiveInversion = std::min(
        spec->inversion(),
        maximumInversion
    );
    result.inversionClamped =
        result.effectiveInversion != spec->inversion();
    const auto placement = buildSemanticPlacement(
        root,
        scaleSettings,
        formula,
        result.voicing,
        result.effectiveInversion,
        octaveSize
    );
    if (!placement.valid) {
        result.rangeLimited = true;
        result.droppedVoiceCount = voiceCount;
        return result;
    }

    result.spreadLimited =
        placement.voicing != result.voicing;
    result.voicing = placement.voicing;
    result.registerShiftOctaves =
        placement.registerShiftOctaves;
    for (uint8_t i = 0U; i < voiceCount; ++i) {
        const uint8_t note = placement.notes[i];
        appendVoice(
            result,
            StepSequencerResolvedChordVoice{
                .note = note,
                .velocity = voiceVelocity(
                    root.velocity,
                    i,
                    voiceCount,
                    spec->velocityCurve
                ),
                .gate = root.gate,
                .nudge = root.nudge,
                .delayTicks = voiceDelayTicks(
                    i,
                    voiceCount,
                    spec->strum,
                    spanTicks
                ),
                .interval = placement.intervals[i],
                .intervalUsesScaleDegrees =
                    result.intervalUsesScaleDegrees,
                .inSelectedScale =
                    scaleSettings.type ==
                        StepSequencerScaleType::Chromatic ||
                    scaleContainsNote(scaleSettings, note),
            }
        );
    }

    result.droppedVoiceCount = static_cast<uint8_t>(
        voiceCount - result.count
    );
    return result;
}

FLASHMEM StepSequencerChordAnalysis analyzeResolvedChord(
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
        appendPitchClass(
            analysis,
            chordPitchClass(resolution.voices[i].note)
        );
        bassNote = std::min<uint8_t>(
            bassNote,
            resolution.voices[i].note
        );
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
        const auto quality = qualityForMask(
            chordMaskForRoot(analysis, candidate)
        );
        if (quality == StepSequencerChordQuality::Unknown) continue;

        analysis.rootPitchClass = candidate;
        analysis.quality = quality;
        analysis.recognized = true;
        analysis.slash =
            analysis.bassPitchClass != analysis.rootPitchClass;
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
