#include <unity.h>

#include <initializer_list>
#include <iterator>

#include <oc/note/sequencer/StepSequencerChord.hpp>

using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordAnalysis;
using oc::note::sequencer::StepSequencerChordHarmony;
using oc::note::sequencer::StepSequencerChordIntervalBasis;
using oc::note::sequencer::StepSequencerChordQuality;
using oc::note::sequencer::StepSequencerChordProjection;
using oc::note::sequencer::StepSequencerChordProjectionWorkspace;
using oc::note::sequencer::StepSequencerChordResolution;
using oc::note::sequencer::StepSequencerResolvedChordVoice;
using oc::note::sequencer::StepSequencerChordSource;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerChordState;
using oc::note::sequencer::StepSequencerChordVoicing;
using oc::note::sequencer::StepSequencerInheritedChord;
using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;
using oc::note::sequencer::StepSequencerStepValues;
using oc::note::sequencer::chordHarmonyChoiceCount;
using oc::note::sequencer::chordHarmonyForChoice;
using oc::note::sequencer::chordPresetChoiceCount;
using oc::note::sequencer::chordPresetForChoice;
using oc::note::sequencer::defaultChildChordState;
using oc::note::sequencer::defaultRootChordState;
using oc::note::sequencer::analyzeResolvedChord;
using oc::note::sequencer::recommendedChordVoiceCount;
using oc::note::sequencer::projectChordSpec;
using oc::note::sequencer::resolveChordFormula;
using oc::note::sequencer::resolveStepChord;

namespace {

StepSequencerChordProjectionWorkspace projectionWorkspace{};

StepSequencerStepValues root(uint8_t note = 60) {
    return StepSequencerStepValues{
        .note = note,
        .velocity = 80,
        .gate = 100,
        .nudge = 0,
    };
}

StepSequencerScaleSettings cNaturalMinor() {
    return StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::NaturalMinor,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
}

StepSequencerScaleSettings cMajor() {
    return StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
}

StepSequencerScaleSettings fHarmonicMinor() {
    return StepSequencerScaleSettings{
        .root = 5,
        .type = StepSequencerScaleType::HarmonicMinor,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
}

StepSequencerChordResolution manualResolution(std::initializer_list<uint8_t> notes) {
    StepSequencerChordResolution resolution{};
    for (uint8_t note : notes) {
        TEST_ASSERT_TRUE(resolution.count < resolution.voices.size());
        resolution.voices[resolution.count++] = StepSequencerResolvedChordVoice{
            .note = note,
            .velocity = 80,
            .gate = 100,
            .nudge = 0,
            .delayTicks = 0,
            .interval = 0,
            .intervalUsesScaleDegrees = false,
        };
    }
    return resolution;
}

void assertQuality(StepSequencerChordQuality expected, const StepSequencerChordAnalysis& analysis) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(analysis.quality)
    );
}

}  // namespace

void setUp() {}

void tearDown() {}

void test_root_default_is_single_voice() {
    const auto out = resolveStepChord(root(), {}, defaultRootChordState());

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StepSequencerChordSource::Single),
                            static_cast<uint8_t>(out.source));
    TEST_ASSERT_FALSE(out.activeForChildren.valid);
    TEST_ASSERT_EQUAL_UINT8(60, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(80, out.voices[0].velocity);
}

void test_child_default_inherits_parent_recipe_from_child_root() {
    StepSequencerInheritedChord inherited{};
    inherited.valid = true;
    inherited.spec = StepSequencerChordSpec{};

    const auto out = resolveStepChord(root(62), cNaturalMinor(), defaultChildChordState(), inherited);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StepSequencerChordSource::Inherited),
                            static_cast<uint8_t>(out.source));
    TEST_ASSERT_TRUE(out.activeForChildren.valid);
    TEST_ASSERT_TRUE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(62, out.voices[0].note);  // D
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[1].note);  // F
    TEST_ASSERT_EQUAL_UINT8(68, out.voices[2].note);  // Ab
}

void test_child_inherit_without_parent_is_single_voice() {
    const auto out = resolveStepChord(root(64), {}, defaultChildChordState());

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StepSequencerChordSource::Single),
                            static_cast<uint8_t>(out.source));
    TEST_ASSERT_FALSE(out.activeForChildren.valid);
    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(64, out.voices[0].note);
}

void test_local_chord_in_chromatic_mode_uses_semitone_intervals() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec{};

    const auto out = resolveStepChord(root(60), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StepSequencerChordSource::Local),
                            static_cast<uint8_t>(out.source));
    TEST_ASSERT_TRUE(out.activeForChildren.valid);
    TEST_ASSERT_FALSE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(60, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(64, out.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(67, out.voices[2].note);
}

void test_local_chord_in_constrained_scale_uses_scale_degrees() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec{};

    const auto out = resolveStepChord(root(60), cNaturalMinor(), chord);

    TEST_ASSERT_TRUE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(60, out.voices[0].note);  // C
    TEST_ASSERT_EQUAL_UINT8(63, out.voices[1].note);  // Eb
    TEST_ASSERT_EQUAL_UINT8(67, out.voices[2].note);  // G
}

void test_single_blocks_inherited_parent_chord() {
    StepSequencerInheritedChord inherited{};
    inherited.valid = true;
    inherited.spec = StepSequencerChordSpec{};
    StepSequencerChordState child{};
    child.mode = StepSequencerChordMode::Single;

    const auto out = resolveStepChord(root(65), cNaturalMinor(), child, inherited);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StepSequencerChordSource::Single),
                            static_cast<uint8_t>(out.source));
    TEST_ASSERT_FALSE(out.activeForChildren.valid);
    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[0].note);
}

void test_local_child_chord_overrides_inherited_parent_chord() {
    StepSequencerInheritedChord inherited{};
    inherited.valid = true;
    inherited.spec = StepSequencerChordSpec{};

    StepSequencerChordState child{};
    child.mode = StepSequencerChordMode::Local;
    child.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Minor
    );

    const auto out = resolveStepChord(root(62), {}, child, inherited);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StepSequencerChordSource::Local),
                            static_cast<uint8_t>(out.source));
    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(62, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(69, out.voices[2].note);
}

void test_voice_count_is_bounded_to_eight() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local.voiceCount = 99;

    const auto out = resolveStepChord(root(), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(8, out.count);
    TEST_ASSERT_EQUAL_UINT8(8, out.activeForChildren.spec.voiceCount);
}

void test_spec_inputs_are_clamped_before_resolution() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local.voiceCount = 0;
    chord.local.harmonyData = 0xFFU;
    chord.local.voicingData = 99;
    chord.local.inversionData = 99;
    chord.local.strum = 120;
    chord.local.velocityCurve = -120;

    const auto out = resolveStepChord(root(), {}, chord, {}, 16);
    TEST_ASSERT_EQUAL_UINT8(1, out.activeForChildren.spec.voiceCount);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordHarmony::DiatonicTriad),
        static_cast<uint8_t>(out.activeForChildren.spec.harmony())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordVoicing::Close),
        static_cast<uint8_t>(out.activeForChildren.spec.voicing())
    );
    TEST_ASSERT_EQUAL_UINT8(3, out.activeForChildren.spec.inversion());
    TEST_ASSERT_EQUAL_INT8(StepSequencerChordSpec::MAX_STRUM, out.activeForChildren.spec.strum);
    TEST_ASSERT_EQUAL_INT8(
        StepSequencerChordSpec::MIN_VELOCITY_CURVE,
        out.activeForChildren.spec.velocityCurve
    );
}

void test_signed_strum_distributes_voice_delays() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local.strum = 50;

    const auto positive = resolveStepChord(root(), {}, chord, {}, 12);

    TEST_ASSERT_EQUAL_UINT16(0, positive.voices[0].delayTicks);
    TEST_ASSERT_EQUAL_UINT16(3, positive.voices[1].delayTicks);
    TEST_ASSERT_EQUAL_UINT16(6, positive.voices[2].delayTicks);

    chord.local.strum = -50;
    const auto negative = resolveStepChord(root(), {}, chord, {}, 12);

    TEST_ASSERT_EQUAL_UINT16(6, negative.voices[0].delayTicks);
    TEST_ASSERT_EQUAL_UINT16(3, negative.voices[1].delayTicks);
    TEST_ASSERT_EQUAL_UINT16(0, negative.voices[2].delayTicks);
}

void test_velocity_curve_is_applied_per_voice() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local.velocityCurve = 20;

    const auto out = resolveStepChord(root(), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(80, out.voices[0].velocity);
    TEST_ASSERT_EQUAL_UINT8(90, out.voices[1].velocity);
    TEST_ASSERT_EQUAL_UINT8(100, out.voices[2].velocity);
}

void test_semantic_spec_uses_nine_byte_eight_voice_payload() {
    TEST_ASSERT_EQUAL_UINT8(9, sizeof(StepSequencerChordSpec));

    const auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Minor7,
        4,
        StepSequencerChordVoicing::Open,
        2
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordHarmony::Minor7),
        static_cast<uint8_t>(spec.harmony())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordVoicing::Open),
        static_cast<uint8_t>(spec.voicing())
    );
    TEST_ASSERT_EQUAL_UINT8(2, spec.inversion());
}

void test_semantic_chromatic_major_resolves_named_quality() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(StepSequencerChordHarmony::Major);

    const auto out = resolveStepChord(root(60), {}, chord);
    const auto analysis = analyzeResolvedChord(out, root(60));

    TEST_ASSERT_FALSE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(60, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(64, out.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(67, out.voices[2].note);
    TEST_ASSERT_TRUE(analysis.recognized);
    assertQuality(StepSequencerChordQuality::Major, analysis);
}

void test_semantic_diatonic_seventh_stays_inside_scale() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::DiatonicSeventh,
        4
    );

    const auto out = resolveStepChord(root(62), cMajor(), chord);

    TEST_ASSERT_TRUE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(4, out.count);
    TEST_ASSERT_EQUAL_UINT8(62, out.voices[0].note);  // D
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[1].note);  // F
    TEST_ASSERT_EQUAL_UINT8(69, out.voices[2].note);  // A
    TEST_ASSERT_EQUAL_UINT8(72, out.voices[3].note);  // C
}

void test_semantic_true_inversion_rotates_lowest_voices() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Major,
        3,
        StepSequencerChordVoicing::Close,
        1
    );

    const auto first = resolveStepChord(root(60), {}, chord);
    const auto firstAnalysis = analyzeResolvedChord(first, root(60));
    TEST_ASSERT_EQUAL_UINT8(64, first.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(67, first.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(72, first.voices[2].note);
    TEST_ASSERT_EQUAL_UINT8(1, first.effectiveInversion);
    TEST_ASSERT_TRUE(firstAnalysis.slash);
    TEST_ASSERT_EQUAL_UINT8(4, firstAnalysis.bassPitchClass);

    chord.local.setInversion(2);
    const auto second = resolveStepChord(root(60), {}, chord);
    TEST_ASSERT_EQUAL_UINT8(67, second.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(72, second.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(76, second.voices[2].note);
    TEST_ASSERT_EQUAL_UINT8(2, second.effectiveInversion);
}

void test_semantic_voicing_changes_register_not_membership() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(StepSequencerChordHarmony::Major);

    const auto close = resolveStepChord(root(60), {}, chord);
    chord.local.setVoicing(StepSequencerChordVoicing::Open);
    const auto open = resolveStepChord(root(60), {}, chord);
    chord.local.setVoicing(StepSequencerChordVoicing::Wide);
    const auto wide = resolveStepChord(root(60), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(67, close.voices[2].note);
    TEST_ASSERT_EQUAL_UINT8(79, open.voices[2].note);
    TEST_ASSERT_EQUAL_UINT8(76, wide.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(79, wide.voices[2].note);
    assertQuality(
        StepSequencerChordQuality::Major,
        analyzeResolvedChord(open, root(60))
    );
    assertQuality(
        StepSequencerChordQuality::Major,
        analyzeResolvedChord(wide, root(60))
    );
}

void test_extended_inversion_raises_lowest_above_highest_without_merge() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        3,
        StepSequencerChordVoicing::Close,
        1,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    std::array<uint8_t, 8> intervals{};
    intervals[1] = 12U;
    intervals[2] = 24U;
    chord.local.setCustomIntervals(intervals);

    const auto out = resolveStepChord(root(60), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(72, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(84, out.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(96, out.voices[2].note);
    TEST_ASSERT_EQUAL_INT16(12, out.voices[0].interval);
    TEST_ASSERT_EQUAL_INT16(24, out.voices[1].interval);
    TEST_ASSERT_EQUAL_INT16(36, out.voices[2].interval);
    TEST_ASSERT_EQUAL_UINT8(0, out.droppedVoiceCount);
}

void test_semantic_voicing_uses_one_global_register_shift() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Major,
        3,
        StepSequencerChordVoicing::Wide
    );

    const auto out = resolveStepChord(root(120), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_INT8(-1, out.registerShiftOctaves);
    TEST_ASSERT_FALSE(out.spreadLimited);
    TEST_ASSERT_FALSE(out.rangeLimited);
    TEST_ASSERT_EQUAL_UINT8(108, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(124, out.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(127, out.voices[2].note);
}

void test_eight_voice_semantic_placement_preserves_cardinality_at_boundary() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        8,
        StepSequencerChordVoicing::Wide,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    constexpr std::array<uint8_t, 8> intervals{
        0U, 3U, 5U, 8U, 12U, 17U, 24U, 31U,
    };
    chord.local.setCustomIntervals(intervals);

    const auto out = resolveStepChord(root(127), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(8, out.count);
    TEST_ASSERT_EQUAL_UINT8(0, out.droppedVoiceCount);
    TEST_ASSERT_FALSE(out.rangeLimited);
    for (uint8_t voice = 1U; voice < out.count; ++voice) {
        TEST_ASSERT_TRUE(
            out.voices[voice].note > out.voices[voice - 1U].note
        );
    }
}

void test_semantic_harmony_is_safely_adapted_to_pitch_mode() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(StepSequencerChordHarmony::Major);

    const auto out = resolveStepChord(root(60), cNaturalMinor(), chord);

    TEST_ASSERT_TRUE(out.harmonyAdjustedForPitchMode);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordHarmony::DiatonicTriad),
        static_cast<uint8_t>(out.harmony)
    );
    TEST_ASSERT_EQUAL_UINT8(60, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(63, out.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(67, out.voices[2].note);
}

void test_context_owns_a_small_fixed_shape_catalog() {
    using Harmony = StepSequencerChordHarmony;

    const Harmony expectedScale[] = {
        Harmony::DiatonicTriad,
        Harmony::DiatonicSeventh,
        Harmony::Suspended,
        Harmony::Quartal,
        Harmony::Custom,
    };
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(std::size(expectedScale)),
        chordHarmonyChoiceCount(true)
    );
    for (uint8_t index = 0; index < std::size(expectedScale); ++index) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(expectedScale[index]),
            static_cast<uint8_t>(chordHarmonyForChoice(index, true))
        );
    }

    const Harmony expectedChromatic[] = {
        Harmony::Major,
        Harmony::Minor,
        Harmony::Diminished,
        Harmony::Augmented,
        Harmony::Sus2,
        Harmony::Sus4,
        Harmony::Dominant7,
        Harmony::Major7,
        Harmony::Minor7,
        Harmony::Custom,
    };
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(std::size(expectedChromatic)),
        chordHarmonyChoiceCount(false)
    );
    for (uint8_t index = 0; index < std::size(expectedChromatic); ++index) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(expectedChromatic[index]),
            static_cast<uint8_t>(chordHarmonyForChoice(index, false))
        );
    }

    const auto triad = resolveChordFormula(
        StepSequencerChordSpec::semantic(
            Harmony::DiatonicTriad,
            recommendedChordVoiceCount(Harmony::DiatonicTriad)
        ),
        true
    );
    TEST_ASSERT_TRUE(triad.valid);
    TEST_ASSERT_TRUE(triad.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(3, triad.count);
    TEST_ASSERT_EQUAL_INT16(0, triad.intervals[0]);
    TEST_ASSERT_EQUAL_INT16(2, triad.intervals[1]);
    TEST_ASSERT_EQUAL_INT16(4, triad.intervals[2]);

    const auto minorSeventh = resolveChordFormula(
        StepSequencerChordSpec::semantic(
            Harmony::Minor7,
            recommendedChordVoiceCount(Harmony::Minor7)
        ),
        false
    );
    TEST_ASSERT_TRUE(minorSeventh.valid);
    TEST_ASSERT_FALSE(minorSeventh.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(4, minorSeventh.count);
    TEST_ASSERT_EQUAL_INT16(0, minorSeventh.intervals[0]);
    TEST_ASSERT_EQUAL_INT16(3, minorSeventh.intervals[1]);
    TEST_ASSERT_EQUAL_INT16(7, minorSeventh.intervals[2]);
    TEST_ASSERT_EQUAL_INT16(10, minorSeventh.intervals[3]);
}

void test_constrained_context_owns_stored_chromatic_formula() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Minor,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );

    const auto out = resolveStepChord(root(65), fHarmonicMinor(), chord);

    TEST_ASSERT_TRUE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_TRUE(out.intervalBasisAdjusted);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordIntervalBasis::ScaleDegrees),
        static_cast<uint8_t>(out.intervalBasis)
    );
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[0].note);  // F
    TEST_ASSERT_EQUAL_UINT8(68, out.voices[1].note);  // Ab
    TEST_ASSERT_EQUAL_UINT8(72, out.voices[2].note);  // C
}

void test_chromatic_context_owns_stored_scale_formula() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::DiatonicTriad,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ScaleDegrees
    );

    const auto out = resolveStepChord(
        root(65),
        fHarmonicMinor(),
        chord,
        {},
        1,
        false
    );

    TEST_ASSERT_FALSE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_TRUE(out.intervalBasisAdjusted);
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[0].note);  // F
    TEST_ASSERT_EQUAL_UINT8(69, out.voices[1].note);  // A
    TEST_ASSERT_EQUAL_UINT8(72, out.voices[2].note);  // C
}

void test_custom_chromatic_triad_resolves_zero_three_five() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    chord.local.setCustomInterval(1, 3);
    chord.local.setCustomInterval(2, 5);

    const auto out = resolveStepChord(
        root(65),
        fHarmonicMinor(),
        chord,
        {},
        1,
        false
    );

    TEST_ASSERT_FALSE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[0].note);  // F
    TEST_ASSERT_EQUAL_UINT8(68, out.voices[1].note);  // Ab
    TEST_ASSERT_EQUAL_UINT8(70, out.voices[2].note);  // Bb
    TEST_ASSERT_TRUE(out.voices[0].inSelectedScale);
    TEST_ASSERT_TRUE(out.voices[1].inSelectedScale);
    TEST_ASSERT_TRUE(out.voices[2].inSelectedScale);
}

void test_custom_scale_triad_uses_user_facing_one_three_five() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ScaleDegrees
    );
    chord.local.setCustomInterval(1, 2);
    chord.local.setCustomInterval(2, 4);

    const auto out = resolveStepChord(root(65), fHarmonicMinor(), chord);

    TEST_ASSERT_TRUE(out.intervalUsesScaleDegrees);
    TEST_ASSERT_EQUAL_UINT8(65, out.voices[0].note);  // F
    TEST_ASSERT_EQUAL_UINT8(68, out.voices[1].note);  // Ab
    TEST_ASSERT_EQUAL_UINT8(72, out.voices[2].note);  // C
}

void test_custom_offsets_cover_eight_voices_and_canonicalize_extension() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        8,
        StepSequencerChordVoicing::Open,
        2,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    spec.setCustomInterval(7, 31);
    spec.setCustomInterval(6, 24);
    spec.setCustomInterval(5, 17);
    spec.setCustomInterval(4, 12);
    spec.setCustomInterval(3, 8);
    spec.setCustomInterval(2, 5);
    spec.setCustomInterval(1, 3);
    spec.strum = -37;
    spec.velocityCurve = 21;

    const auto persisted = spec;
    spec.clamp();

    constexpr uint8_t EXPECTED[] = {0, 3, 5, 8, 12, 17, 24, 31};
    TEST_ASSERT_EQUAL_UINT8(9, sizeof(StepSequencerChordSpec));
    TEST_ASSERT_EQUAL_UINT8(8, spec.voices());
    for (uint8_t voice = 0; voice < spec.voices(); ++voice) {
        TEST_ASSERT_EQUAL_UINT8(EXPECTED[voice], spec.customInterval(voice));
    }
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordVoicing::Open),
        static_cast<uint8_t>(spec.voicing())
    );
    TEST_ASSERT_EQUAL_UINT8(2, spec.inversion());
    TEST_ASSERT_EQUAL_INT8(-37, spec.strum);
    TEST_ASSERT_EQUAL_INT8(21, spec.velocityCurve);
    TEST_ASSERT_EQUAL_UINT8(persisted.voiceCount, spec.voiceCount);
    TEST_ASSERT_EQUAL_UINT8(persisted.voicingData, spec.voicingData);
    TEST_ASSERT_EQUAL_UINT8(persisted.inversionData, spec.inversionData);
    TEST_ASSERT_TRUE(chordSpecsEqual(persisted, spec));
    TEST_ASSERT_EQUAL_UINT8(
        0,
        static_cast<uint8_t>(spec.customIntervalExtension[2] & 0xF0U)
    );

    auto nonCanonical = spec;
    nonCanonical.customIntervalExtension[2] |= 0xF0U;
    TEST_ASSERT_FALSE(chordSpecsEqual(nonCanonical, spec));
    TEST_ASSERT_TRUE(chordSpecsEqualCanonical(nonCanonical, spec));

    spec.setVoices(4);
    TEST_ASSERT_EQUAL_UINT8(4, spec.voices());
    for (uint8_t voice = 4;
         voice < StepSequencerChordSpec::MAX_CUSTOM_VOICES;
         ++voice) {
        TEST_ASSERT_EQUAL_UINT8(0, spec.customInterval(voice));
    }
}

void test_shape_catalog_excludes_custom_formula() {
    TEST_ASSERT_EQUAL_UINT8(4, chordPresetChoiceCount(true));
    TEST_ASSERT_EQUAL_UINT8(9, chordPresetChoiceCount(false));
    for (const bool scaleConstrained : {false, true}) {
        const uint8_t count = chordPresetChoiceCount(scaleConstrained);
        for (uint8_t index = 0; index < count; ++index) {
            TEST_ASSERT_NOT_EQUAL_UINT8(
                static_cast<uint8_t>(StepSequencerChordHarmony::Custom),
                static_cast<uint8_t>(
                    chordPresetForChoice(index, scaleConstrained)
                )
            );
        }
    }
}

void test_chromatic_custom_zero_three_five_projects_exactly_to_scale_degrees() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    spec.setCustomInterval(1, 3);
    spec.setCustomInterval(2, 5);

    const auto projection = projectChordSpec(
        spec,
        {},
        fHarmonicMinor(),
        65,
        65,
        false,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_TRUE(projection.exact);
    TEST_ASSERT_FALSE(projection.adapted);
    TEST_ASSERT_TRUE(projection.changed);
    TEST_ASSERT_TRUE(projection.spec.isCustom());
    TEST_ASSERT_EQUAL_UINT8(2, projection.spec.customInterval(1));
    TEST_ASSERT_EQUAL_UINT8(3, projection.spec.customInterval(2));
}

void test_major_triad_projects_globally_to_natural_minor_triad() {
    const auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Major,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );

    const auto projection = projectChordSpec(
        spec,
        {},
        cNaturalMinor(),
        60,
        60,
        false,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_FALSE(projection.exact);
    TEST_ASSERT_TRUE(projection.adapted);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            StepSequencerChordHarmony::DiatonicTriad
        ),
        static_cast<uint8_t>(projection.spec.harmony())
    );
    TEST_ASSERT_EQUAL_INT16(0, projection.targetFormula.intervals[0]);
    TEST_ASSERT_EQUAL_INT16(2, projection.targetFormula.intervals[1]);
    TEST_ASSERT_EQUAL_INT16(4, projection.targetFormula.intervals[2]);
}

void test_scale_formula_materializes_losslessly_when_becoming_chromatic() {
    const auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::DiatonicTriad,
        3,
        StepSequencerChordVoicing::Open,
        1,
        StepSequencerChordIntervalBasis::ScaleDegrees
    );

    const auto projection = projectChordSpec(
        spec,
        fHarmonicMinor(),
        {},
        65,
        65,
        true,
        false,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_TRUE(projection.exact);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordHarmony::Minor),
        static_cast<uint8_t>(projection.spec.harmony())
    );
    TEST_ASSERT_EQUAL_INT16(0, projection.targetFormula.intervals[0]);
    TEST_ASSERT_EQUAL_INT16(3, projection.targetFormula.intervals[1]);
    TEST_ASSERT_EQUAL_INT16(7, projection.targetFormula.intervals[2]);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordVoicing::Open),
        static_cast<uint8_t>(projection.spec.voicing())
    );
    TEST_ASSERT_EQUAL_UINT8(1, projection.spec.inversion());
}

void test_single_voice_formula_crosses_pitch_context_exactly() {
    const auto source = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Major,
        1,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );

    const auto projected = projectChordSpec(
        source,
        {},
        cMajor(),
        60,
        60,
        false,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projected.valid);
    TEST_ASSERT_TRUE(projected.exact);
    TEST_ASSERT_FALSE(projected.adapted);
    TEST_ASSERT_EQUAL_UINT8(1, projected.targetFormula.count);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerChordIntervalBasis::ScaleDegrees),
        static_cast<uint8_t>(projected.spec.intervalBasis())
    );
}

void test_scale_to_scale_preserves_raw_degree_formula() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        4,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ScaleDegrees
    );
    spec.setCustomInterval(1, 2);
    spec.setCustomInterval(2, 5);
    spec.setCustomInterval(3, 9);

    const auto projection = projectChordSpec(
        spec,
        fHarmonicMinor(),
        cMajor(),
        65,
        65,
        true,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_TRUE(projection.exact);
    TEST_ASSERT_FALSE(projection.adapted);
    TEST_ASSERT_EQUAL_UINT8(2, projection.spec.customInterval(1));
    TEST_ASSERT_EQUAL_UINT8(5, projection.spec.customInterval(2));
    TEST_ASSERT_EQUAL_UINT8(9, projection.spec.customInterval(3));
}

void test_projection_direction_is_hard_and_nearest_ties_lower() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        2,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    spec.setCustomInterval(1, 6);

    auto nearestScale = cMajor();
    const auto nearest = projectChordSpec(
        spec,
        {},
        nearestScale,
        60,
        60,
        false,
        true,
        projectionWorkspace
    );
    TEST_ASSERT_EQUAL_INT16(3, nearest.targetFormula.intervals[1]);

    auto upScale = cMajor();
    upScale.mode = StepSequencerScaleConstraintMode::ConstrainUp;
    const auto up = projectChordSpec(
        spec,
        {},
        upScale,
        60,
        60,
        false,
        true,
        projectionWorkspace
    );
    TEST_ASSERT_EQUAL_INT16(4, up.targetFormula.intervals[1]);

    auto downScale = cMajor();
    downScale.mode = StepSequencerScaleConstraintMode::ConstrainDown;
    const auto down = projectChordSpec(
        spec,
        {},
        downScale,
        60,
        60,
        false,
        true,
        projectionWorkspace
    );
    TEST_ASSERT_EQUAL_INT16(3, down.targetFormula.intervals[1]);
}

void test_unused_high_range_candidates_do_not_flag_exact_projection() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        2,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    spec.setCustomInterval(1, 2);

    const auto projection = projectChordSpec(
        spec,
        {},
        cMajor(),
        120,
        120,
        false,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_TRUE(projection.exact);
    TEST_ASSERT_FALSE(projection.adapted);
    TEST_ASSERT_FALSE(projection.rangeLimited);
    TEST_ASSERT_EQUAL_UINT8(2, projection.targetFormula.count);
    TEST_ASSERT_EQUAL_INT16(1, projection.targetFormula.intervals[1]);
}

void test_projection_keeps_target_voices_strictly_ordered() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    spec.setCustomInterval(1, 1);
    spec.setCustomInterval(2, 2);

    const auto projection = projectChordSpec(
        spec,
        {},
        cMajor(),
        60,
        60,
        false,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_EQUAL_UINT8(3, projection.targetFormula.count);
    TEST_ASSERT_TRUE(
        projection.targetFormula.intervals[1] >
        projection.targetFormula.intervals[0]
    );
    TEST_ASSERT_TRUE(
        projection.targetFormula.intervals[2] >
        projection.targetFormula.intervals[1]
    );
}

void test_projection_dp_preserves_all_eight_voices() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        8,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    constexpr std::array<uint8_t, 8> semitones{
        0U, 2U, 4U, 5U, 7U, 9U, 11U, 12U,
    };
    spec.setCustomIntervals(semitones);

    const auto projection = projectChordSpec(
        spec,
        {},
        cMajor(),
        60,
        60,
        false,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_FALSE(projection.voiceCountLimited);
    TEST_ASSERT_EQUAL_UINT8(0, projection.droppedVoiceCount);
    TEST_ASSERT_EQUAL_UINT8(8, projection.targetFormula.count);
    for (uint8_t voice = 0U; voice < 8U; ++voice) {
        TEST_ASSERT_EQUAL_INT16(
            voice,
            projection.targetFormula.intervals[voice]
        );
    }
    TEST_ASSERT_TRUE(projection.exact);
    TEST_ASSERT_FALSE(projection.adapted);
}

void test_projection_retries_nearest_when_direction_cannot_fit() {
    auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        2,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    spec.setCustomInterval(1, 1);
    auto target = cMajor();
    target.mode = StepSequencerScaleConstraintMode::ConstrainDown;

    const auto projection = projectChordSpec(
        spec,
        {},
        target,
        60,
        60,
        false,
        true,
        projectionWorkspace
    );

    TEST_ASSERT_TRUE(projection.valid);
    TEST_ASSERT_TRUE(projection.directionLimited);
    TEST_ASSERT_TRUE(projection.adapted);
    TEST_ASSERT_FALSE(projection.voiceCountLimited);
    TEST_ASSERT_EQUAL_UINT8(2, projection.targetFormula.count);
    TEST_ASSERT_EQUAL_INT16(1, projection.targetFormula.intervals[1]);
}

void test_chromatic_voice_reports_out_of_selected_scale() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Major,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );

    const auto out = resolveStepChord(
        root(65),
        fHarmonicMinor(),
        chord,
        {},
        1,
        false
    );

    TEST_ASSERT_TRUE(out.voices[0].inSelectedScale);
    TEST_ASSERT_FALSE(out.voices[1].inSelectedScale);  // A natural
    TEST_ASSERT_TRUE(out.voices[2].inSelectedScale);
}

void test_semantic_inversion_is_bounded_and_reported() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local = StepSequencerChordSpec::semantic(StepSequencerChordHarmony::Major);
    chord.local.setInversion(7);

    const auto out = resolveStepChord(root(60), {}, chord);

    TEST_ASSERT_TRUE(out.inversionClamped);
    TEST_ASSERT_EQUAL_UINT8(2, out.effectiveInversion);
}

void test_analyze_resolved_major_triad() {
    const auto resolution = manualResolution({60, 64, 67});
    const auto analysis = analyzeResolvedChord(resolution, root(60));

    TEST_ASSERT_TRUE(analysis.recognized);
    assertQuality(StepSequencerChordQuality::Major, analysis);
    TEST_ASSERT_EQUAL_UINT8(0, analysis.rootPitchClass);
    TEST_ASSERT_EQUAL_UINT8(0, analysis.bassPitchClass);
    TEST_ASSERT_FALSE(analysis.slash);
    TEST_ASSERT_EQUAL_UINT8(3, analysis.intervalCount);
    TEST_ASSERT_EQUAL_UINT8(0, analysis.chromaticIntervals[0]);
    TEST_ASSERT_EQUAL_UINT8(4, analysis.chromaticIntervals[1]);
    TEST_ASSERT_EQUAL_UINT8(7, analysis.chromaticIntervals[2]);
}

void test_analyze_resolved_minor_seventh() {
    const auto resolution = manualResolution({57, 60, 64, 67});
    const auto analysis = analyzeResolvedChord(resolution, root(57));

    TEST_ASSERT_TRUE(analysis.recognized);
    assertQuality(StepSequencerChordQuality::Minor7, analysis);
    TEST_ASSERT_EQUAL_UINT8(9, analysis.rootPitchClass);
    TEST_ASSERT_EQUAL_UINT8(9, analysis.bassPitchClass);
    TEST_ASSERT_FALSE(analysis.slash);
}

void test_analyze_prefers_step_root_for_inversion() {
    const auto resolution = manualResolution({64, 67, 72});
    const auto analysis = analyzeResolvedChord(resolution, root(60));

    TEST_ASSERT_TRUE(analysis.recognized);
    assertQuality(StepSequencerChordQuality::Major, analysis);
    TEST_ASSERT_EQUAL_UINT8(0, analysis.rootPitchClass);
    TEST_ASSERT_EQUAL_UINT8(4, analysis.bassPitchClass);
    TEST_ASSERT_TRUE(analysis.slash);
}

void test_analyze_unknown_chord_keeps_interval_fallback() {
    const auto resolution = manualResolution({60, 62, 66});
    const auto analysis = analyzeResolvedChord(resolution, root(60));

    TEST_ASSERT_FALSE(analysis.recognized);
    assertQuality(StepSequencerChordQuality::Unknown, analysis);
    TEST_ASSERT_EQUAL_UINT8(0, analysis.rootPitchClass);
    TEST_ASSERT_EQUAL_UINT8(3, analysis.intervalCount);
    TEST_ASSERT_EQUAL_UINT8(0, analysis.chromaticIntervals[0]);
    TEST_ASSERT_EQUAL_UINT8(2, analysis.chromaticIntervals[1]);
    TEST_ASSERT_EQUAL_UINT8(6, analysis.chromaticIntervals[2]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_root_default_is_single_voice);
    RUN_TEST(test_child_default_inherits_parent_recipe_from_child_root);
    RUN_TEST(test_child_inherit_without_parent_is_single_voice);
    RUN_TEST(test_local_chord_in_chromatic_mode_uses_semitone_intervals);
    RUN_TEST(test_local_chord_in_constrained_scale_uses_scale_degrees);
    RUN_TEST(test_single_blocks_inherited_parent_chord);
    RUN_TEST(test_local_child_chord_overrides_inherited_parent_chord);
    RUN_TEST(test_voice_count_is_bounded_to_eight);
    RUN_TEST(test_spec_inputs_are_clamped_before_resolution);
    RUN_TEST(test_signed_strum_distributes_voice_delays);
    RUN_TEST(test_velocity_curve_is_applied_per_voice);
    RUN_TEST(test_semantic_spec_uses_nine_byte_eight_voice_payload);
    RUN_TEST(test_semantic_chromatic_major_resolves_named_quality);
    RUN_TEST(test_semantic_diatonic_seventh_stays_inside_scale);
    RUN_TEST(test_semantic_true_inversion_rotates_lowest_voices);
    RUN_TEST(test_semantic_voicing_changes_register_not_membership);
    RUN_TEST(test_extended_inversion_raises_lowest_above_highest_without_merge);
    RUN_TEST(test_semantic_voicing_uses_one_global_register_shift);
    RUN_TEST(test_eight_voice_semantic_placement_preserves_cardinality_at_boundary);
    RUN_TEST(test_semantic_harmony_is_safely_adapted_to_pitch_mode);
    RUN_TEST(test_context_owns_a_small_fixed_shape_catalog);
    RUN_TEST(test_constrained_context_owns_stored_chromatic_formula);
    RUN_TEST(test_chromatic_context_owns_stored_scale_formula);
    RUN_TEST(test_custom_chromatic_triad_resolves_zero_three_five);
    RUN_TEST(test_custom_scale_triad_uses_user_facing_one_three_five);
    RUN_TEST(test_custom_offsets_cover_eight_voices_and_canonicalize_extension);
    RUN_TEST(test_shape_catalog_excludes_custom_formula);
    RUN_TEST(test_chromatic_custom_zero_three_five_projects_exactly_to_scale_degrees);
    RUN_TEST(test_major_triad_projects_globally_to_natural_minor_triad);
    RUN_TEST(test_scale_formula_materializes_losslessly_when_becoming_chromatic);
    RUN_TEST(test_single_voice_formula_crosses_pitch_context_exactly);
    RUN_TEST(test_scale_to_scale_preserves_raw_degree_formula);
    RUN_TEST(test_projection_direction_is_hard_and_nearest_ties_lower);
    RUN_TEST(test_unused_high_range_candidates_do_not_flag_exact_projection);
    RUN_TEST(test_projection_keeps_target_voices_strictly_ordered);
    RUN_TEST(test_projection_dp_preserves_all_eight_voices);
    RUN_TEST(test_projection_retries_nearest_when_direction_cannot_fit);
    RUN_TEST(test_chromatic_voice_reports_out_of_selected_scale);
    RUN_TEST(test_semantic_inversion_is_bounded_and_reported);
    RUN_TEST(test_analyze_resolved_major_triad);
    RUN_TEST(test_analyze_resolved_minor_seventh);
    RUN_TEST(test_analyze_prefers_step_root_for_inversion);
    RUN_TEST(test_analyze_unknown_chord_keeps_interval_fallback);
    return UNITY_END();
}
