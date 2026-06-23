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

struct StepSequencerChordSpec {
    static constexpr uint8_t MAX_VOICES = 8;
    static constexpr uint8_t MAX_COLOR = 7;
    static constexpr uint8_t MAX_VARIANT = 7;
    static constexpr uint8_t MAX_SPREAD = 7;
    static constexpr int8_t MIN_STRUM = -100;
    static constexpr int8_t MAX_STRUM = 100;
    static constexpr int8_t MIN_VELOCITY_CURVE = -63;
    static constexpr int8_t MAX_VELOCITY_CURVE = 63;

    uint8_t voiceCount = 3;
    uint8_t color = 0;
    uint8_t variant = 0;
    uint8_t spread = 0;
    int8_t strum = 0;
    int8_t velocityCurve = 0;

    void clamp();
};

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
    uint8_t count = 0;
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

StepSequencerChordResolution resolveStepChord(
    StepSequencerStepValues root,
    StepSequencerScaleSettings scaleSettings,
    StepSequencerChordState chord,
    StepSequencerInheritedChord inherited = {},
    uint16_t spanTicks = 1
);

StepSequencerChordAnalysis analyzeResolvedChord(
    const StepSequencerChordResolution& resolution,
    StepSequencerStepValues root
);

}  // namespace oc::note::sequencer
