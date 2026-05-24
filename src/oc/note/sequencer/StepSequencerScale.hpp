#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>

namespace oc::note::sequencer {

enum class StepSequencerScaleType : uint8_t {
    Chromatic = 0,
    Major,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    WholeTone,
};

enum class StepSequencerScaleConstraintMode : uint8_t {
    Free = 0,
    ConstrainNearest,
    ConstrainUp,
    ConstrainDown,
};

struct StepSequencerScaleSettings {
    uint8_t root = 0;  // C = 0, C# = 1, ... B = 11
    StepSequencerScaleType type = StepSequencerScaleType::Chromatic;
    StepSequencerScaleConstraintMode mode = StepSequencerScaleConstraintMode::Free;

    void clamp() {
        root = static_cast<uint8_t>(root % 12U);

        if (static_cast<uint8_t>(type) >
            static_cast<uint8_t>(StepSequencerScaleType::WholeTone)) {
            type = StepSequencerScaleType::Chromatic;
        }

        if (static_cast<uint8_t>(mode) >
            static_cast<uint8_t>(StepSequencerScaleConstraintMode::ConstrainDown)) {
            mode = StepSequencerScaleConstraintMode::Free;
        }
    }

    bool isConstrained() const {
        return type != StepSequencerScaleType::Chromatic &&
               mode != StepSequencerScaleConstraintMode::Free;
    }
};

struct StepSequencerScaleResolution {
    uint8_t inputNote = 0;
    uint8_t outputNote = 0;
    bool inputInScale = true;
    bool constrained = false;
    int8_t semitoneDelta = 0;
};

inline uint8_t clampScaleMidiNote(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

inline constexpr uint16_t scaleMaskFromIntervals(std::initializer_list<uint8_t> intervals) {
    uint16_t mask = 0;
    for (const uint8_t interval : intervals) {
        mask = static_cast<uint16_t>(mask | static_cast<uint16_t>(1U << (interval % 12U)));
    }
    return mask;
}

inline uint16_t scaleMask(StepSequencerScaleType type) {
    switch (type) {
        case StepSequencerScaleType::Chromatic:
            return 0x0FFFU;
        case StepSequencerScaleType::Major:
            return scaleMaskFromIntervals({0, 2, 4, 5, 7, 9, 11});
        case StepSequencerScaleType::NaturalMinor:
            return scaleMaskFromIntervals({0, 2, 3, 5, 7, 8, 10});
        case StepSequencerScaleType::HarmonicMinor:
            return scaleMaskFromIntervals({0, 2, 3, 5, 7, 8, 11});
        case StepSequencerScaleType::MelodicMinor:
            return scaleMaskFromIntervals({0, 2, 3, 5, 7, 9, 11});
        case StepSequencerScaleType::Dorian:
            return scaleMaskFromIntervals({0, 2, 3, 5, 7, 9, 10});
        case StepSequencerScaleType::Phrygian:
            return scaleMaskFromIntervals({0, 1, 3, 5, 7, 8, 10});
        case StepSequencerScaleType::Lydian:
            return scaleMaskFromIntervals({0, 2, 4, 6, 7, 9, 11});
        case StepSequencerScaleType::Mixolydian:
            return scaleMaskFromIntervals({0, 2, 4, 5, 7, 9, 10});
        case StepSequencerScaleType::Locrian:
            return scaleMaskFromIntervals({0, 1, 3, 5, 6, 8, 10});
        case StepSequencerScaleType::MajorPentatonic:
            return scaleMaskFromIntervals({0, 2, 4, 7, 9});
        case StepSequencerScaleType::MinorPentatonic:
            return scaleMaskFromIntervals({0, 3, 5, 7, 10});
        case StepSequencerScaleType::Blues:
            return scaleMaskFromIntervals({0, 3, 5, 6, 7, 10});
        case StepSequencerScaleType::WholeTone:
            return scaleMaskFromIntervals({0, 2, 4, 6, 8, 10});
    }

    return 0x0FFFU;
}

inline uint8_t pitchClass(uint8_t note) {
    return static_cast<uint8_t>(note % 12U);
}

inline bool scaleContainsPitchClass(StepSequencerScaleSettings settings, uint8_t notePitchClass) {
    settings.clamp();
    const uint8_t relative = static_cast<uint8_t>((notePitchClass + 12U - settings.root) % 12U);
    return (scaleMask(settings.type) & static_cast<uint16_t>(1U << relative)) != 0;
}

inline bool scaleContainsNote(StepSequencerScaleSettings settings, uint8_t note) {
    return scaleContainsPitchClass(settings, pitchClass(note));
}

inline int nearestScaleOffset(StepSequencerScaleSettings settings, uint8_t note) {
    settings.clamp();
    if (scaleContainsNote(settings, note)) return 0;

    switch (settings.mode) {
        case StepSequencerScaleConstraintMode::ConstrainDown:
            for (int offset = -1; offset >= -12; --offset) {
                if (scaleContainsNote(settings, clampScaleMidiNote(static_cast<int>(note) + offset))) {
                    return offset;
                }
            }
            return 0;

        case StepSequencerScaleConstraintMode::ConstrainUp:
            for (int offset = 1; offset <= 12; ++offset) {
                if (scaleContainsNote(settings, clampScaleMidiNote(static_cast<int>(note) + offset))) {
                    return offset;
                }
            }
            return 0;

        case StepSequencerScaleConstraintMode::ConstrainNearest:
            for (int distance = 1; distance <= 12; ++distance) {
                const int up = static_cast<int>(note) + distance;
                if (up <= 127 && scaleContainsNote(settings, static_cast<uint8_t>(up))) {
                    return distance;
                }

                const int down = static_cast<int>(note) - distance;
                if (down >= 0 && scaleContainsNote(settings, static_cast<uint8_t>(down))) {
                    return -distance;
                }
            }
            return 0;

        case StepSequencerScaleConstraintMode::Free:
            return 0;
    }

    return 0;
}

inline StepSequencerScaleResolution resolveScaleNote(uint8_t note,
                                                     StepSequencerScaleSettings settings) {
    settings.clamp();

    StepSequencerScaleResolution result{};
    result.inputNote = note;
    result.outputNote = note;
    result.inputInScale = scaleContainsNote(settings, note);

    if (!settings.isConstrained()) {
        return result;
    }

    const int offset = nearestScaleOffset(settings, note);
    result.outputNote = clampScaleMidiNote(static_cast<int>(note) + offset);
    result.constrained = result.outputNote != note;
    result.semitoneDelta = static_cast<int8_t>(
        static_cast<int>(result.outputNote) - static_cast<int>(note)
    );
    return result;
}

inline uint8_t moveByScaleDegrees(uint8_t note,
                                  int8_t degreeDelta,
                                  StepSequencerScaleSettings settings) {
    settings.clamp();
    settings.mode = StepSequencerScaleConstraintMode::ConstrainNearest;

    uint8_t current = resolveScaleNote(note, settings).outputNote;
    if (degreeDelta == 0) return current;

    const int direction = degreeDelta > 0 ? 1 : -1;
    int remaining = degreeDelta > 0 ? degreeDelta : -degreeDelta;

    while (remaining > 0) {
        int next = static_cast<int>(current);
        do {
            next += direction;
            if (next < 0) return 0;
            if (next > 127) return 127;
        } while (!scaleContainsNote(settings, static_cast<uint8_t>(next)));

        current = static_cast<uint8_t>(next);
        --remaining;
    }

    return current;
}

}  // namespace oc::note::sequencer
