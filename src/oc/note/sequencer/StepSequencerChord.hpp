#pragma once

#include <array>
#include <cstdint>

#include "StepSequencerScale.hpp"
#include "StepSequencerVariation.hpp"

namespace oc::note::sequencer {

enum class StepSequencerChordMode : uint8_t {
    Single = 0,
    Inherit,
    Local,
};

enum class StepSequencerChordSource : uint8_t {
    Single = 0,
    Inherited,
    Local,
};

enum class StepSequencerChordIntervalBasis : uint8_t {
    // The stored value records the context in which a semantic formula was
    // authored. It is not a musician-facing override: playback always follows
    // the effective Step pitch context.
    FollowPitchContext = 0,
    ScaleDegrees,
    ChromaticSemitones,
    Count,
};

enum class StepSequencerChordQuality : uint8_t {
    Unknown = 0,
    Power,
    Major,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Dominant7,
    Major7,
    Minor7,
    MinorMajor7,
    Major6,
    Minor6,
    Diminished7,
    HalfDiminished7,
    Dominant9,
    Major9,
    Minor9,
    Add9,
    MinorAdd9,
};

enum class StepSequencerChordHarmony : uint8_t {
    DiatonicTriad = 0,
    DiatonicSeventh,
    Suspended,
    Quartal,
    Major,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Dominant7,
    Major7,
    Minor7,
    Custom,
    Count,
};

enum class StepSequencerChordVoicing : uint8_t {
    Close = 0,
    Open,
    Wide,
    Count,
};

struct StepSequencerChordSpec {
    static constexpr uint8_t MAX_VOICES = 8;
    static constexpr uint8_t MAX_CUSTOM_VOICES = 8;
    static constexpr uint8_t MAX_CUSTOM_INTERVAL = 31;
    static constexpr int8_t MIN_STRUM = -100;
    static constexpr int8_t MAX_STRUM = 100;
    static constexpr int8_t MIN_VELOCITY_CURVE = -63;
    static constexpr int8_t MAX_VELOCITY_CURVE = 63;
    static constexpr uint8_t HARMONY_MASK = 0x1FU;
    static constexpr uint8_t BASIS_MASK = 0x60U;
    static constexpr uint8_t BASIS_SHIFT = 5U;
    static constexpr uint8_t VOICE_COUNT_MASK = 0x0FU;
    static constexpr uint8_t VOICING_MASK = 0x03U;
    static constexpr uint8_t INVERSION_MASK = 0x07U;

    uint8_t voiceCount = 3;
    // Compact formula header. Bit 7 of harmonyData is reserved and canonical
    // encoders keep it clear; decoders reject non-canonical payloads.
    uint8_t harmonyData = 0;
    uint8_t voicingData = 0;
    uint8_t inversionData = 0;
    int8_t strum = 0;
    int8_t velocityCurve = 0;
    // Custom V5..V8 use the low 20 bits. The high nibble is reserved and must
    // remain zero in canonical state. V2..V4 retain their original packed
    // locations in voiceCount/voicingData/inversionData.
    std::array<uint8_t, 3> customIntervalExtension{};

    static StepSequencerChordSpec semantic(
        StepSequencerChordHarmony harmony,
        uint8_t voices = 3,
        StepSequencerChordVoicing voicing = StepSequencerChordVoicing::Close,
        uint8_t inversion = 0,
        StepSequencerChordIntervalBasis intervalBasis =
            StepSequencerChordIntervalBasis::FollowPitchContext
    );

    bool isCustom() const;
    uint8_t voices() const;
    StepSequencerChordHarmony harmony() const;
    StepSequencerChordIntervalBasis intervalBasis() const;
    StepSequencerChordVoicing voicing() const;
    uint8_t inversion() const;
    uint8_t customInterval(uint8_t voiceIndex) const;
    void setVoices(uint8_t voices);
    void setHarmony(StepSequencerChordHarmony harmony);
    void setIntervalBasis(StepSequencerChordIntervalBasis intervalBasis);
    void setVoicing(StepSequencerChordVoicing voicing);
    void setInversion(uint8_t inversion);
    void setCustomInterval(uint8_t voiceIndex, uint8_t interval);
    void setCustomIntervals(
        const std::array<uint8_t, MAX_CUSTOM_VOICES>& intervals
    );
    void clamp();
};

static_assert(sizeof(StepSequencerChordSpec) == 9, "Eight-voice chord specs must stay compact");
static_assert(alignof(StepSequencerChordSpec) == 1, "Chord specs must remain byte-aligned");

bool chordSpecsEqual(
    const StepSequencerChordSpec& lhs,
    const StepSequencerChordSpec& rhs
);
bool chordSpecsEqualCanonical(
    StepSequencerChordSpec lhs,
    StepSequencerChordSpec rhs
);

struct StepSequencerChordFormula {
    static constexpr uint8_t MAX_VOICES = StepSequencerChordSpec::MAX_VOICES;

    bool valid = false;
    bool intervalUsesScaleDegrees = false;
    bool harmonyAdjustedForPitchMode = false;
    uint8_t count = 0;
    StepSequencerChordHarmony harmony = StepSequencerChordHarmony::DiatonicTriad;
    std::array<int16_t, MAX_VOICES> intervals{};
};

struct StepSequencerChordProjection {
    bool valid = false;
    bool changed = false;
    bool exact = false;
    bool adapted = false;
    bool rangeLimited = false;
    bool directionLimited = false;
    bool voiceCountLimited = false;
    uint8_t droppedVoiceCount = 0;
    StepSequencerChordSpec spec{};
    StepSequencerChordFormula sourceFormula{};
    StepSequencerChordFormula targetFormula{};
};

/**
 * Fixed caller-owned scratch for cross-basis chord projection.
 *
 * The fields are public only so the workspace remains a simple allocation-free
 * POD across embedded and native builds. Callers must treat their contents as
 * opaque and may reuse one instance between sequential cold-path projections.
 */
struct StepSequencerChordProjectionWorkspace {
    static constexpr uint8_t MAX_VOICES =
        StepSequencerChordSpec::MAX_CUSTOM_VOICES;
    static constexpr uint8_t MAX_CANDIDATES =
        StepSequencerChordSpec::MAX_CUSTOM_INTERVAL;

    struct Candidate {
        uint8_t interval = 0;
        int16_t semitones = 0;
        bool rangeLimited = false;
    };

    struct State {
        bool valid = false;
        uint8_t inexactCount = 0;
        uint16_t maximumMovement = 0;
        uint16_t totalMovement = 0;
        uint16_t structuralDistortion = 0;
        std::array<int16_t, MAX_VOICES> semitones{};
        std::array<uint8_t, MAX_VOICES> intervals{};
    };

    std::array<int16_t, MAX_VOICES> desired{};
    std::array<Candidate, MAX_CANDIDATES> candidates{};
    std::array<
        std::array<State, MAX_CANDIDATES>,
        2
    > states{};
    uint8_t candidateCount = 0;
};

static_assert(
    sizeof(StepSequencerChordProjectionWorkspace) <= 2304U,
    "Chord projection scratch must remain a small fixed cold-path workspace"
);

struct StepSequencerChordState {
    StepSequencerChordMode mode = StepSequencerChordMode::Single;
    StepSequencerChordSpec local{};
};

struct StepSequencerInheritedChord {
    bool valid = false;
    StepSequencerChordSpec spec{};
};

struct StepSequencerResolvedChordVoice {
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
    uint16_t delayTicks = 0;
    int16_t interval = 0;
    bool intervalUsesScaleDegrees = false;
    bool inSelectedScale = true;
};

struct StepSequencerChordResolution {
    static constexpr uint8_t MAX_VOICES = StepSequencerChordSpec::MAX_VOICES;

    StepSequencerChordSource source = StepSequencerChordSource::Single;
    StepSequencerChordIntervalBasis requestedIntervalBasis =
        StepSequencerChordIntervalBasis::FollowPitchContext;
    StepSequencerChordIntervalBasis intervalBasis =
        StepSequencerChordIntervalBasis::ChromaticSemitones;
    bool intervalUsesScaleDegrees = false;
    bool intervalBasisAdjusted = false;
    bool harmonyAdjustedForPitchMode = false;
    bool inversionClamped = false;
    bool spreadLimited = false;
    bool rangeLimited = false;
    uint8_t count = 0;
    uint8_t requestedVoiceCount = 1;
    uint8_t effectiveInversion = 0;
    uint8_t droppedVoiceCount = 0;
    int8_t registerShiftOctaves = 0;
    StepSequencerChordHarmony harmony = StepSequencerChordHarmony::DiatonicTriad;
    StepSequencerChordVoicing voicing = StepSequencerChordVoicing::Close;
    std::array<StepSequencerResolvedChordVoice, MAX_VOICES> voices{};
    StepSequencerInheritedChord activeForChildren{};
};

struct StepSequencerChordAnalysis {
    static constexpr uint8_t MAX_VOICES = StepSequencerChordSpec::MAX_VOICES;

    bool recognized = false;
    bool slash = false;
    uint8_t rootPitchClass = 0;
    uint8_t bassPitchClass = 0;
    StepSequencerChordQuality quality = StepSequencerChordQuality::Unknown;
    uint8_t pitchClassCount = 0;
    std::array<uint8_t, MAX_VOICES> pitchClasses{};
    uint8_t intervalCount = 0;
    std::array<uint8_t, MAX_VOICES> chromaticIntervals{};
};

StepSequencerChordState defaultRootChordState();
StepSequencerChordState defaultChildChordState();

bool chordHarmonyAvailable(StepSequencerChordHarmony harmony, bool scaleConstrained);
uint8_t chordHarmonyChoiceCount(bool scaleConstrained);
StepSequencerChordHarmony chordHarmonyForChoice(uint8_t index, bool scaleConstrained);
uint8_t chordHarmonyChoiceIndex(
    StepSequencerChordHarmony harmony,
    bool scaleConstrained
);
// Musician-facing Shape choices. Custom is edited through Formula and is
// deliberately absent from this catalog.
uint8_t chordPresetChoiceCount(bool scaleConstrained);
StepSequencerChordHarmony chordPresetForChoice(
    uint8_t index,
    bool scaleConstrained
);
uint8_t chordPresetChoiceIndex(
    StepSequencerChordHarmony harmony,
    bool scaleConstrained
);
StepSequencerChordHarmony defaultChordHarmony(bool scaleConstrained);
uint8_t recommendedChordVoiceCount(StepSequencerChordHarmony harmony);
StepSequencerChordFormula resolveChordFormula(
    StepSequencerChordSpec spec,
    bool intervalUsesScaleDegrees
);
StepSequencerChordHarmony recognizeChordFormula(
    const StepSequencerChordFormula& formula,
    bool intervalUsesScaleDegrees
);
StepSequencerChordSpec canonicalizeChordSpec(
    StepSequencerChordSpec spec,
    bool intervalUsesScaleDegrees
);
StepSequencerChordProjection projectChordSpec(
    StepSequencerChordSpec spec,
    StepSequencerScaleSettings sourceScale,
    StepSequencerScaleSettings targetScale,
    uint8_t sourceRootNote,
    uint8_t targetRootNote,
    bool sourceUsesScaleDegrees,
    bool targetUsesScaleDegrees,
    StepSequencerChordProjectionWorkspace& workspace
);

StepSequencerChordResolution resolveStepChord(
    StepSequencerStepValues root,
    StepSequencerScaleSettings scaleSettings,
    StepSequencerChordState chord,
    StepSequencerInheritedChord inherited = {},
    uint16_t spanTicks = 1,
    bool pitchFollowsScale = true
);

StepSequencerChordAnalysis analyzeResolvedChord(
    const StepSequencerChordResolution& resolution,
    StepSequencerStepValues root
);

}  // namespace oc::note::sequencer
