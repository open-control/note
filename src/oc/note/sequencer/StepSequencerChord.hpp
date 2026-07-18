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
    Fifths,
    Cluster,
    Major,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Dominant7,
    Major7,
    Minor7,
    Count,
};

enum class StepSequencerChordVoicing : uint8_t {
    Close = 0,
    Open,
    Wide,
    Count,
};

struct StepSequencerLegacyChordRecipe {
    uint8_t color = 0;
    uint8_t variant = 0;
    uint8_t spread = 0;
};

struct StepSequencerChordSpec {
    static constexpr uint8_t MAX_VOICES = 8;
    static constexpr uint8_t MAX_COLOR = 7;
    static constexpr uint8_t MAX_VARIANT = 7;
    static constexpr uint8_t MAX_SPREAD = 7;
    static constexpr int8_t MIN_STRUM = -100;
    static constexpr int8_t MAX_STRUM = 100;
    static constexpr int8_t MIN_VELOCITY_CURVE = -63;
    static constexpr int8_t MAX_VELOCITY_CURVE = 63;
    static constexpr uint8_t SEMANTIC_RECIPE_MARKER = 0x80U;
    static constexpr uint8_t SEMANTIC_HARMONY_MASK = 0x1FU;

    uint8_t voiceCount = 3;
    // These three bytes deliberately retain the legacy on-disk footprint.
    // Legacy recipes store Color/Variant/Spread directly. Semantic recipes
    // set SEMANTIC_RECIPE_MARKER and store Harmony/Voicing/Inversion instead.
    uint8_t harmonyData = 0;
    uint8_t voicingData = 0;
    uint8_t inversionData = 0;
    int8_t strum = 0;
    int8_t velocityCurve = 0;

    static StepSequencerChordSpec semantic(
        StepSequencerChordHarmony harmony,
        uint8_t voices = 3,
        StepSequencerChordVoicing voicing = StepSequencerChordVoicing::Close,
        uint8_t inversion = 0
    );

    bool isSemantic() const;
    StepSequencerChordHarmony harmony() const;
    StepSequencerChordVoicing voicing() const;
    uint8_t inversion() const;
    StepSequencerLegacyChordRecipe legacyRecipe() const;
    void setHarmony(StepSequencerChordHarmony harmony);
    void setVoicing(StepSequencerChordVoicing voicing);
    void setInversion(uint8_t inversion);
    void setLegacyRecipe(StepSequencerLegacyChordRecipe recipe);
    void clamp();
};

static_assert(sizeof(StepSequencerChordSpec) == 6, "Chord specs must stay graph-compact");

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
};

struct StepSequencerChordResolution {
    static constexpr uint8_t MAX_VOICES = StepSequencerChordSpec::MAX_VOICES;

    StepSequencerChordSource source = StepSequencerChordSource::Single;
    bool intervalUsesScaleDegrees = false;
    bool semanticRecipe = false;
    bool harmonyAdjustedForPitchMode = false;
    bool inversionClamped = false;
    bool rangeLimited = false;
    uint8_t count = 0;
    uint8_t requestedVoiceCount = 1;
    uint8_t effectiveInversion = 0;
    uint8_t droppedVoiceCount = 0;
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
StepSequencerChordHarmony defaultChordHarmony(bool scaleConstrained);
uint8_t recommendedChordVoiceCount(StepSequencerChordHarmony harmony);

StepSequencerChordResolution resolveStepChord(
    StepSequencerStepValues root,
    StepSequencerScaleSettings scaleSettings,
    StepSequencerChordState chord,
    StepSequencerInheritedChord inherited = {},
    uint16_t spanTicks = 1,
    bool pitchUsesScaleDegrees = true
);

StepSequencerChordAnalysis analyzeResolvedChord(
    const StepSequencerChordResolution& resolution,
    StepSequencerStepValues root
);

}  // namespace oc::note::sequencer
