#include "StepSequencerChord.hpp"

#include <config/PlatformCompat.hpp>

#include <algorithm>

namespace oc::note::sequencer {
namespace {

constexpr uint8_t VOICE_COUNT = StepSequencerChordSpec::MAX_VOICES;

const StepSequencerChordHarmony SCALE_HARMONIES[] PROGMEM = {
    StepSequencerChordHarmony::DiatonicTriad,
    StepSequencerChordHarmony::DiatonicSeventh,
    StepSequencerChordHarmony::Suspended,
    StepSequencerChordHarmony::Quartal,
    StepSequencerChordHarmony::Custom,
};

const StepSequencerChordHarmony CHROMATIC_HARMONIES[] PROGMEM = {
    StepSequencerChordHarmony::Major,
    StepSequencerChordHarmony::Minor,
    StepSequencerChordHarmony::Diminished,
    StepSequencerChordHarmony::Augmented,
    StepSequencerChordHarmony::Sus2,
    StepSequencerChordHarmony::Sus4,
    StepSequencerChordHarmony::Dominant7,
    StepSequencerChordHarmony::Major7,
    StepSequencerChordHarmony::Minor7,
    StepSequencerChordHarmony::Custom,
};

constexpr uint8_t SCALE_HARMONY_COUNT = static_cast<uint8_t>(
    sizeof(SCALE_HARMONIES) / sizeof(SCALE_HARMONIES[0])
);
constexpr uint8_t CHROMATIC_HARMONY_COUNT = static_cast<uint8_t>(
    sizeof(CHROMATIC_HARMONIES) / sizeof(CHROMATIC_HARMONIES[0])
);

// Shape owns pitch membership. Scale rows contain degree offsets; chromatic
// rows contain semitone offsets. Custom uses its packed offsets instead.
const int16_t HARMONY_FORMULA_INTERVALS[
    static_cast<uint8_t>(StepSequencerChordHarmony::Count)
][VOICE_COUNT] PROGMEM = {
    {0, 2, 4, 6, 8, 10, 12, 14},       // Diatonic triad/extensions
    {0, 2, 4, 6, 8, 10, 12, 14},       // Diatonic seventh/extensions
    {0, 3, 4, 7, 8, 11, 12, 15},       // Suspended
    {0, 3, 6, 9, 12, 15, 18, 21},      // Quartal
    {0, 4, 7, 11, 14, 16, 19, 23},     // Major
    {0, 3, 7, 10, 14, 15, 19, 22},     // Minor
    {0, 3, 6, 9, 12, 15, 18, 21},      // Diminished
    {0, 4, 8, 12, 16, 20, 24, 28},     // Augmented
    {0, 2, 7, 10, 14, 17, 19, 22},     // Sus2
    {0, 5, 7, 10, 14, 17, 19, 22},     // Sus4
    {0, 4, 7, 10, 14, 17, 21, 22},     // Dominant 7
    {0, 4, 7, 11, 14, 17, 21, 23},     // Major 7
    {0, 3, 7, 10, 14, 17, 19, 22},     // Minor 7
    {0, 3, 5, 7, 10, 12, 15, 17},      // Custom fallback
};

constexpr uint8_t CUSTOM_INTERVAL_VALUE_MASK =
    StepSequencerChordSpec::MAX_CUSTOM_INTERVAL;
constexpr uint8_t BASE_PACKED_CUSTOM_VOICES = 3U;
constexpr uint32_t CUSTOM_INTERVAL_EXTENSION_MASK = 0x000FFFFFU;

using CustomIntervals = std::array<
    uint8_t,
    StepSequencerChordSpec::MAX_CUSTOM_VOICES
>;

int8_t clampSigned(int value, int8_t minValue, int8_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return static_cast<int8_t>(value);
}

uint16_t packedCustomIntervals(const StepSequencerChordSpec& spec) {
    return static_cast<uint16_t>(
        ((spec.voiceCount >> 4U) & 0x0FU) |
        (static_cast<uint16_t>((spec.voicingData >> 2U) & 0x3FU) << 4U) |
        (static_cast<uint16_t>((spec.inversionData >> 3U) & 0x1FU) << 10U)
    );
}

void setPackedCustomIntervals(
    StepSequencerChordSpec& spec,
    uint16_t packed
) {
    spec.voiceCount = static_cast<uint8_t>(
        (spec.voiceCount & StepSequencerChordSpec::VOICE_COUNT_MASK) |
        static_cast<uint8_t>((packed & 0x0FU) << 4U)
    );
    spec.voicingData = static_cast<uint8_t>(
        (spec.voicingData & StepSequencerChordSpec::VOICING_MASK) |
        static_cast<uint8_t>(((packed >> 4U) & 0x3FU) << 2U)
    );
    spec.inversionData = static_cast<uint8_t>(
        (spec.inversionData & StepSequencerChordSpec::INVERSION_MASK) |
        static_cast<uint8_t>(((packed >> 10U) & 0x1FU) << 3U)
    );
}

uint32_t packedCustomIntervalExtension(
    const StepSequencerChordSpec& spec
) {
    return static_cast<uint32_t>(
        spec.customIntervalExtension[0] |
        (static_cast<uint32_t>(spec.customIntervalExtension[1]) << 8U) |
        (static_cast<uint32_t>(
            spec.customIntervalExtension[2] & 0x0FU
        ) << 16U)
    );
}

void setPackedCustomIntervalExtension(
    StepSequencerChordSpec& spec,
    uint32_t packed
) {
    packed &= CUSTOM_INTERVAL_EXTENSION_MASK;
    spec.customIntervalExtension[0] =
        static_cast<uint8_t>(packed & 0xFFU);
    spec.customIntervalExtension[1] =
        static_cast<uint8_t>((packed >> 8U) & 0xFFU);
    spec.customIntervalExtension[2] =
        static_cast<uint8_t>((packed >> 16U) & 0x0FU);
}

uint8_t packedCustomInterval(
    const StepSequencerChordSpec& spec,
    uint8_t voiceIndex
) {
    if (voiceIndex == 0 ||
        voiceIndex >= StepSequencerChordSpec::MAX_CUSTOM_VOICES) {
        return 0;
    }
    if (voiceIndex <= BASE_PACKED_CUSTOM_VOICES) {
        const uint8_t shift = static_cast<uint8_t>(
            (voiceIndex - 1U) * 5U
        );
        return static_cast<uint8_t>(
            (packedCustomIntervals(spec) >> shift) &
            CUSTOM_INTERVAL_VALUE_MASK
        );
    }

    const uint8_t shift = static_cast<uint8_t>(
        (voiceIndex - (BASE_PACKED_CUSTOM_VOICES + 1U)) * 5U
    );
    return static_cast<uint8_t>(
        (packedCustomIntervalExtension(spec) >> shift) &
        CUSTOM_INTERVAL_VALUE_MASK
    );
}

void setPackedCustomInterval(
    StepSequencerChordSpec& spec,
    uint8_t voiceIndex,
    uint8_t interval
) {
    if (voiceIndex == 0 ||
        voiceIndex >= StepSequencerChordSpec::MAX_CUSTOM_VOICES) {
        return;
    }
    if (voiceIndex <= BASE_PACKED_CUSTOM_VOICES) {
        const uint8_t shift = static_cast<uint8_t>(
            (voiceIndex - 1U) * 5U
        );
        const uint16_t mask = static_cast<uint16_t>(
            CUSTOM_INTERVAL_VALUE_MASK << shift
        );
        const uint16_t packed = static_cast<uint16_t>(
            (packedCustomIntervals(spec) & static_cast<uint16_t>(~mask)) |
            (static_cast<uint16_t>(
                interval & CUSTOM_INTERVAL_VALUE_MASK
            ) << shift)
        );
        setPackedCustomIntervals(spec, packed);
        return;
    }

    const uint8_t shift = static_cast<uint8_t>(
        (voiceIndex - (BASE_PACKED_CUSTOM_VOICES + 1U)) * 5U
    );
    const uint32_t mask = static_cast<uint32_t>(
        CUSTOM_INTERVAL_VALUE_MASK
    ) << shift;
    const uint32_t packed =
        (packedCustomIntervalExtension(spec) & ~mask) |
        (static_cast<uint32_t>(
            interval & CUSTOM_INTERVAL_VALUE_MASK
        ) << shift);
    setPackedCustomIntervalExtension(spec, packed);
}

CustomIntervals captureCustomIntervals(
    const StepSequencerChordSpec& spec
) {
    CustomIntervals intervals{};
    if (!spec.isCustom()) return intervals;
    for (uint8_t voiceIndex = 1U;
         voiceIndex < StepSequencerChordSpec::MAX_CUSTOM_VOICES;
         ++voiceIndex) {
        intervals[voiceIndex] = packedCustomInterval(spec, voiceIndex);
    }
    return intervals;
}

StepSequencerChordIntervalBasis sanitizeIntervalBasis(uint8_t raw) {
    if (raw >=
        static_cast<uint8_t>(StepSequencerChordIntervalBasis::Count)) {
        return StepSequencerChordIntervalBasis::FollowPitchContext;
    }
    return static_cast<StepSequencerChordIntervalBasis>(raw);
}

StepSequencerChordHarmony resolvedSemanticHarmony(
    StepSequencerChordHarmony requested,
    bool scaleConstrained,
    bool& adjusted
) {
    if (chordHarmonyAvailable(requested, scaleConstrained)) return requested;
    adjusted = true;
    return defaultChordHarmony(scaleConstrained);
}

bool formulasMatch(
    const StepSequencerChordFormula& lhs,
    const StepSequencerChordFormula& rhs
) {
    if (!lhs.valid || !rhs.valid || lhs.count != rhs.count) return false;
    for (uint8_t index = 0; index < lhs.count; ++index) {
        if (lhs.intervals[index] != rhs.intervals[index]) return false;
    }
    return true;
}

}  // namespace

FLASHMEM StepSequencerChordSpec StepSequencerChordSpec::semantic(
    StepSequencerChordHarmony harmonyValue,
    uint8_t voices,
    StepSequencerChordVoicing voicingValue,
    uint8_t inversionValue,
    StepSequencerChordIntervalBasis intervalBasisValue
) {
    StepSequencerChordSpec spec{};
    spec.voiceCount = voices;
    spec.harmonyData = static_cast<uint8_t>(
        (static_cast<uint8_t>(intervalBasisValue) << BASIS_SHIFT) |
        static_cast<uint8_t>(harmonyValue)
    );
    spec.voicingData = static_cast<uint8_t>(voicingValue);
    spec.inversionData = inversionValue;
    if (harmonyValue == StepSequencerChordHarmony::Custom) {
        const bool chromatic =
            intervalBasisValue ==
            StepSequencerChordIntervalBasis::ChromaticSemitones;
        setPackedCustomInterval(spec, 1, chromatic ? 3U : 2U);
        setPackedCustomInterval(spec, 2, chromatic ? 7U : 4U);
        setPackedCustomInterval(spec, 3, chromatic ? 10U : 6U);
    }
    spec.clamp();
    return spec;
}

bool StepSequencerChordSpec::isCustom() const {
    return harmony() == StepSequencerChordHarmony::Custom;
}

uint8_t StepSequencerChordSpec::voices() const {
    const uint8_t raw = isCustom()
        ? static_cast<uint8_t>(voiceCount & VOICE_COUNT_MASK)
        : voiceCount;
    if (isCustom()) {
        return std::clamp<uint8_t>(raw, 2U, MAX_CUSTOM_VOICES);
    }
    return std::clamp<uint8_t>(raw, 1U, MAX_VOICES);
}

StepSequencerChordHarmony StepSequencerChordSpec::harmony() const {
    return static_cast<StepSequencerChordHarmony>(
        harmonyData & HARMONY_MASK
    );
}

StepSequencerChordIntervalBasis
StepSequencerChordSpec::intervalBasis() const {
    return sanitizeIntervalBasis(static_cast<uint8_t>(
        (harmonyData & BASIS_MASK) >> BASIS_SHIFT
    ));
}

StepSequencerChordVoicing StepSequencerChordSpec::voicing() const {
    return static_cast<StepSequencerChordVoicing>(
        voicingData & VOICING_MASK
    );
}

uint8_t StepSequencerChordSpec::inversion() const {
    return static_cast<uint8_t>(inversionData & INVERSION_MASK);
}

uint8_t StepSequencerChordSpec::customInterval(uint8_t voiceIndex) const {
    if (!isCustom()) return 0;
    return packedCustomInterval(*this, voiceIndex);
}

FLASHMEM void StepSequencerChordSpec::setVoices(uint8_t voicesValue) {
    const uint8_t maximum = isCustom() ? MAX_CUSTOM_VOICES : MAX_VOICES;
    const uint8_t minimum = isCustom() ? 2U : 1U;
    const uint8_t clamped = std::clamp<uint8_t>(
        voicesValue,
        minimum,
        maximum
    );
    voiceCount = isCustom()
        ? static_cast<uint8_t>((voiceCount & 0xF0U) | clamped)
        : clamped;
    clamp();
}

FLASHMEM void StepSequencerChordSpec::setHarmony(
    StepSequencerChordHarmony harmonyValue
) {
    const bool wasCustom = isCustom();
    const uint8_t voiceCountValue = voices();
    const auto basisValue = intervalBasis();
    const auto voicingValue = voicing();
    const uint8_t inversionValue = inversion();
    const CustomIntervals customIntervals =
        wasCustom ? captureCustomIntervals(*this) : CustomIntervals{};

    harmonyData = static_cast<uint8_t>(
        (static_cast<uint8_t>(basisValue) << BASIS_SHIFT) |
        static_cast<uint8_t>(harmonyValue)
    );
    voiceCount = voiceCountValue;
    voicingData = static_cast<uint8_t>(voicingValue);
    inversionData = inversionValue;
    customIntervalExtension.fill(0);

    if (harmonyValue == StepSequencerChordHarmony::Custom) {
        if (wasCustom) {
            for (uint8_t voiceIndex = 1U;
                 voiceIndex < MAX_CUSTOM_VOICES;
                 ++voiceIndex) {
                setPackedCustomInterval(
                    *this,
                    voiceIndex,
                    customIntervals[voiceIndex]
                );
            }
        } else {
            const bool chromatic =
                basisValue ==
                StepSequencerChordIntervalBasis::ChromaticSemitones;
            setPackedCustomInterval(*this, 1, chromatic ? 3U : 2U);
            setPackedCustomInterval(*this, 2, chromatic ? 7U : 4U);
            setPackedCustomInterval(*this, 3, chromatic ? 10U : 6U);
        }
    }
    clamp();
}

FLASHMEM void StepSequencerChordSpec::setIntervalBasis(
    StepSequencerChordIntervalBasis intervalBasisValue
) {
    const auto sanitized = sanitizeIntervalBasis(
        static_cast<uint8_t>(intervalBasisValue)
    );
    harmonyData = static_cast<uint8_t>(
        (harmonyData & static_cast<uint8_t>(~BASIS_MASK)) |
        (static_cast<uint8_t>(sanitized) << BASIS_SHIFT)
    );
    clamp();
}

FLASHMEM void StepSequencerChordSpec::setVoicing(
    StepSequencerChordVoicing voicingValue
) {
    voicingData = static_cast<uint8_t>(
        (voicingData & static_cast<uint8_t>(~VOICING_MASK)) |
        (static_cast<uint8_t>(voicingValue) & VOICING_MASK)
    );
    clamp();
}

FLASHMEM void StepSequencerChordSpec::setInversion(
    uint8_t inversionValue
) {
    inversionValue = std::min<uint8_t>(
        inversionValue,
        static_cast<uint8_t>(MAX_VOICES - 1U)
    );
    inversionData = static_cast<uint8_t>(
        (inversionData & static_cast<uint8_t>(~INVERSION_MASK)) |
        (inversionValue & INVERSION_MASK)
    );
    clamp();
}

FLASHMEM void StepSequencerChordSpec::setCustomInterval(
    uint8_t voiceIndex,
    uint8_t interval
) {
    if (!isCustom()) setHarmony(StepSequencerChordHarmony::Custom);
    setPackedCustomInterval(
        *this,
        voiceIndex,
        std::min<uint8_t>(interval, MAX_CUSTOM_INTERVAL)
    );
    clamp();
}

FLASHMEM void StepSequencerChordSpec::setCustomIntervals(
    const std::array<uint8_t, MAX_CUSTOM_VOICES>& intervals
) {
    if (!isCustom()) setHarmony(StepSequencerChordHarmony::Custom);
    setPackedCustomIntervals(*this, 0U);
    customIntervalExtension.fill(0);
    for (uint8_t voiceIndex = 1U;
         voiceIndex < MAX_CUSTOM_VOICES;
         ++voiceIndex) {
        setPackedCustomInterval(
            *this,
            voiceIndex,
            std::min<uint8_t>(
                intervals[voiceIndex],
                MAX_CUSTOM_INTERVAL
            )
        );
    }
    clamp();
}

void StepSequencerChordSpec::clamp() {
    const auto basisValue = intervalBasis();
    uint8_t harmonyValue = static_cast<uint8_t>(harmony());
    if (harmonyValue >=
        static_cast<uint8_t>(StepSequencerChordHarmony::Count)) {
        harmonyValue = static_cast<uint8_t>(
            StepSequencerChordHarmony::DiatonicTriad
        );
    }
    const bool custom =
        harmonyValue ==
        static_cast<uint8_t>(StepSequencerChordHarmony::Custom);
    const uint8_t maximumVoices =
        custom ? MAX_CUSTOM_VOICES : MAX_VOICES;
    const uint8_t voiceCountValue = std::clamp<uint8_t>(
        custom
            ? static_cast<uint8_t>(voiceCount & VOICE_COUNT_MASK)
            : voiceCount,
        custom ? 2U : 1U,
        maximumVoices
    );
    const CustomIntervals customIntervals =
        custom ? captureCustomIntervals(*this) : CustomIntervals{};
    uint8_t voicingValue = static_cast<uint8_t>(
        voicingData & VOICING_MASK
    );
    if (voicingValue >=
        static_cast<uint8_t>(StepSequencerChordVoicing::Count)) {
        voicingValue = static_cast<uint8_t>(
            StepSequencerChordVoicing::Close
        );
    }
    const uint8_t inversionValue = std::min<uint8_t>(
        static_cast<uint8_t>(inversionData & INVERSION_MASK),
        MAX_VOICES - 1U
    );

    harmonyData = static_cast<uint8_t>(
        (static_cast<uint8_t>(basisValue) << BASIS_SHIFT) |
        harmonyValue
    );
    voiceCount = voiceCountValue;
    voicingData = voicingValue;
    inversionData = inversionValue;
    customIntervalExtension.fill(0);

    if (custom) {
        const bool chromatic =
            basisValue ==
            StepSequencerChordIntervalBasis::ChromaticSemitones;
        constexpr uint8_t SCALE_DEFAULTS[MAX_CUSTOM_VOICES] = {
            0U, 2U, 4U, 6U, 8U, 10U, 12U, 14U,
        };
        constexpr uint8_t CHROMATIC_DEFAULTS[MAX_CUSTOM_VOICES] = {
            0U, 3U, 7U, 10U, 14U, 15U, 19U, 22U,
        };
        uint8_t previous = 0;
        for (uint8_t voiceIndex = 1;
             voiceIndex < voiceCountValue;
             ++voiceIndex) {
            const uint8_t remaining = static_cast<uint8_t>(
                (voiceCountValue - 1U) - voiceIndex
            );
            const uint8_t minimum = static_cast<uint8_t>(
                previous + 1U
            );
            const uint8_t maximum = static_cast<uint8_t>(
                MAX_CUSTOM_INTERVAL - remaining
            );
            uint8_t interval = customIntervals[voiceIndex];
            if (interval < minimum || interval > maximum) {
                interval = std::clamp<uint8_t>(
                    chromatic
                        ? CHROMATIC_DEFAULTS[voiceIndex]
                        : SCALE_DEFAULTS[voiceIndex],
                    minimum,
                    maximum
                );
            }
            setPackedCustomInterval(*this, voiceIndex, interval);
            previous = interval;
        }
    }

    strum = clampSigned(strum, MIN_STRUM, MAX_STRUM);
    velocityCurve = clampSigned(
        velocityCurve,
        MIN_VELOCITY_CURVE,
        MAX_VELOCITY_CURVE
    );
}

FLASHMEM bool chordSpecsEqual(
    const StepSequencerChordSpec& lhs,
    const StepSequencerChordSpec& rhs
) {
    return lhs.voiceCount == rhs.voiceCount &&
           lhs.harmonyData == rhs.harmonyData &&
           lhs.voicingData == rhs.voicingData &&
           lhs.inversionData == rhs.inversionData &&
           lhs.strum == rhs.strum &&
           lhs.velocityCurve == rhs.velocityCurve &&
           lhs.customIntervalExtension == rhs.customIntervalExtension;
}

FLASHMEM bool chordSpecsEqualCanonical(
    StepSequencerChordSpec lhs,
    StepSequencerChordSpec rhs
) {
    lhs.clamp();
    rhs.clamp();
    return chordSpecsEqual(lhs, rhs);
}

FLASHMEM bool chordHarmonyAvailable(
    StepSequencerChordHarmony harmony,
    bool scaleConstrained
) {
    const auto* harmonies =
        scaleConstrained ? SCALE_HARMONIES : CHROMATIC_HARMONIES;
    const uint8_t count = chordHarmonyChoiceCount(scaleConstrained);
    for (uint8_t index = 0; index < count; ++index) {
        if (harmonies[index] == harmony) return true;
    }
    return false;
}

FLASHMEM uint8_t chordHarmonyChoiceCount(bool scaleConstrained) {
    return scaleConstrained
        ? SCALE_HARMONY_COUNT
        : CHROMATIC_HARMONY_COUNT;
}

FLASHMEM StepSequencerChordHarmony chordHarmonyForChoice(
    uint8_t index,
    bool scaleConstrained
) {
    const uint8_t count = chordHarmonyChoiceCount(scaleConstrained);
    if (index >= count) index = static_cast<uint8_t>(count - 1U);
    return scaleConstrained
        ? SCALE_HARMONIES[index]
        : CHROMATIC_HARMONIES[index];
}

FLASHMEM uint8_t chordHarmonyChoiceIndex(
    StepSequencerChordHarmony harmony,
    bool scaleConstrained
) {
    const auto* harmonies =
        scaleConstrained ? SCALE_HARMONIES : CHROMATIC_HARMONIES;
    const uint8_t count = chordHarmonyChoiceCount(scaleConstrained);
    for (uint8_t index = 0; index < count; ++index) {
        if (harmonies[index] == harmony) return index;
    }
    return 0;
}

FLASHMEM uint8_t chordPresetChoiceCount(bool scaleConstrained) {
    const uint8_t count = chordHarmonyChoiceCount(scaleConstrained);
    return count > 0U ? static_cast<uint8_t>(count - 1U) : 0U;
}

FLASHMEM StepSequencerChordHarmony chordPresetForChoice(
    uint8_t index,
    bool scaleConstrained
) {
    const uint8_t count = chordPresetChoiceCount(scaleConstrained);
    if (count == 0U) return defaultChordHarmony(scaleConstrained);
    if (index >= count) index = static_cast<uint8_t>(count - 1U);
    return chordHarmonyForChoice(index, scaleConstrained);
}

FLASHMEM uint8_t chordPresetChoiceIndex(
    StepSequencerChordHarmony harmony,
    bool scaleConstrained
) {
    const uint8_t count = chordPresetChoiceCount(scaleConstrained);
    for (uint8_t index = 0; index < count; ++index) {
        if (chordPresetForChoice(index, scaleConstrained) == harmony) {
            return index;
        }
    }
    return 0U;
}

FLASHMEM StepSequencerChordHarmony defaultChordHarmony(
    bool scaleConstrained
) {
    return scaleConstrained
        ? StepSequencerChordHarmony::DiatonicTriad
        : StepSequencerChordHarmony::Major;
}

FLASHMEM uint8_t recommendedChordVoiceCount(
    StepSequencerChordHarmony harmony
) {
    switch (harmony) {
        case StepSequencerChordHarmony::DiatonicSeventh:
        case StepSequencerChordHarmony::Dominant7:
        case StepSequencerChordHarmony::Major7:
        case StepSequencerChordHarmony::Minor7:
            return 4;
        default:
            return 3;
    }
}

FLASHMEM StepSequencerChordFormula resolveChordFormula(
    StepSequencerChordSpec spec,
    bool intervalUsesScaleDegrees
) {
    StepSequencerChordFormula formula{};
    spec.clamp();

    formula.valid = true;
    formula.intervalUsesScaleDegrees = intervalUsesScaleDegrees;
    formula.harmony = resolvedSemanticHarmony(
        spec.harmony(),
        intervalUsesScaleDegrees,
        formula.harmonyAdjustedForPitchMode
    );
    formula.count = spec.voices();

    if (formula.harmony == StepSequencerChordHarmony::Custom &&
        spec.isCustom()) {
        for (uint8_t i = 0; i < formula.count; ++i) {
            formula.intervals[i] = spec.customInterval(i);
        }
        return formula;
    }

    const auto harmonyIndex = static_cast<uint8_t>(formula.harmony);
    for (uint8_t i = 0; i < formula.count; ++i) {
        formula.intervals[i] =
            HARMONY_FORMULA_INTERVALS[harmonyIndex][i];
    }
    return formula;
}

FLASHMEM StepSequencerChordHarmony recognizeChordFormula(
    const StepSequencerChordFormula& formula,
    bool intervalUsesScaleDegrees
) {
    if (!formula.valid || formula.count == 0U) {
        return StepSequencerChordHarmony::Custom;
    }

    const uint8_t count = chordPresetChoiceCount(intervalUsesScaleDegrees);
    for (uint8_t pass = 0; pass < 2U; ++pass) {
        for (uint8_t index = 0; index < count; ++index) {
            const auto harmony = chordPresetForChoice(
                index,
                intervalUsesScaleDegrees
            );
            if (pass == 0U &&
                recommendedChordVoiceCount(harmony) != formula.count) {
                continue;
            }
            const auto candidate = resolveChordFormula(
                StepSequencerChordSpec::semantic(
                    harmony,
                    formula.count,
                    StepSequencerChordVoicing::Close,
                    0,
                    intervalUsesScaleDegrees
                        ? StepSequencerChordIntervalBasis::ScaleDegrees
                        : StepSequencerChordIntervalBasis::ChromaticSemitones
                ),
                intervalUsesScaleDegrees
            );
            if (formulasMatch(formula, candidate)) return harmony;
        }
    }
    return StepSequencerChordHarmony::Custom;
}

FLASHMEM StepSequencerChordSpec canonicalizeChordSpec(
    StepSequencerChordSpec spec,
    bool intervalUsesScaleDegrees
) {
    spec.clamp();
    if (!spec.isCustom()) return spec;

    const auto formula = resolveChordFormula(spec, intervalUsesScaleDegrees);
    const auto harmony = recognizeChordFormula(
        formula,
        intervalUsesScaleDegrees
    );
    if (harmony == StepSequencerChordHarmony::Custom) return spec;

    auto canonical = StepSequencerChordSpec::semantic(
        harmony,
        formula.count,
        spec.voicing(),
        std::min<uint8_t>(
            spec.inversion(),
            formula.count > 0U
                ? static_cast<uint8_t>(formula.count - 1U)
                : 0U
        ),
        intervalUsesScaleDegrees
            ? StepSequencerChordIntervalBasis::ScaleDegrees
            : StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    canonical.strum = spec.strum;
    canonical.velocityCurve = spec.velocityCurve;
    canonical.clamp();
    return canonical;
}

}  // namespace oc::note::sequencer
