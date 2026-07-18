#include <unity.h>

#include <initializer_list>

#include <oc/note/sequencer/StepSequencerChord.hpp>

using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordAnalysis;
using oc::note::sequencer::StepSequencerChordHarmony;
using oc::note::sequencer::StepSequencerChordQuality;
using oc::note::sequencer::StepSequencerChordResolution;
using oc::note::sequencer::StepSequencerResolvedChordVoice;
using oc::note::sequencer::StepSequencerChordSource;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerChordState;
using oc::note::sequencer::StepSequencerChordVoicing;
using oc::note::sequencer::StepSequencerInheritedChord;
using oc::note::sequencer::StepSequencerLegacyChordRecipe;
using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;
using oc::note::sequencer::StepSequencerStepValues;
using oc::note::sequencer::defaultChildChordState;
using oc::note::sequencer::defaultRootChordState;
using oc::note::sequencer::analyzeResolvedChord;
using oc::note::sequencer::resolveStepChord;

namespace {

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

uint32_t triadIntervalSignature(const StepSequencerChordResolution& out) {
    TEST_ASSERT_TRUE(out.count >= 3);
    return (static_cast<uint32_t>(out.voices[0].interval + 128) << 16U) |
           (static_cast<uint32_t>(out.voices[1].interval + 128) << 8U) |
           static_cast<uint32_t>(out.voices[2].interval + 128);
}

void assertNoPreviousSignatureMatch(const uint32_t* signatures, uint8_t count, uint32_t candidate) {
    for (uint8_t i = 0; i < count; ++i) {
        TEST_ASSERT_NOT_EQUAL_UINT32(signatures[i], candidate);
    }
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
    child.local = StepSequencerChordSpec{};
    child.local.setLegacyRecipe({.color = 1});  // Minor chromatic palette.

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
    chord.local.harmonyData = 99;
    chord.local.voicingData = 99;
    chord.local.inversionData = 99;
    chord.local.strum = 120;
    chord.local.velocityCurve = -120;

    const auto out = resolveStepChord(root(), {}, chord, {}, 16);
    const auto legacy = out.activeForChildren.spec.legacyRecipe();

    TEST_ASSERT_EQUAL_UINT8(1, out.activeForChildren.spec.voiceCount);
    TEST_ASSERT_EQUAL_UINT8(StepSequencerChordSpec::MAX_COLOR, legacy.color);
    TEST_ASSERT_EQUAL_UINT8(StepSequencerChordSpec::MAX_VARIANT, legacy.variant);
    TEST_ASSERT_EQUAL_UINT8(StepSequencerChordSpec::MAX_SPREAD, legacy.spread);
    TEST_ASSERT_EQUAL_INT8(StepSequencerChordSpec::MAX_STRUM, out.activeForChildren.spec.strum);
    TEST_ASSERT_EQUAL_INT8(
        StepSequencerChordSpec::MIN_VELOCITY_CURVE,
        out.activeForChildren.spec.velocityCurve
    );
}

void test_color_axis_changes_default_triad_in_chromatic_mode() {
    uint32_t signatures[StepSequencerChordSpec::MAX_COLOR + 1]{};

    for (uint8_t color = 0; color <= StepSequencerChordSpec::MAX_COLOR; ++color) {
        StepSequencerChordState chord{};
        chord.mode = StepSequencerChordMode::Local;
        chord.local.setLegacyRecipe({.color = color});

        const auto out = resolveStepChord(root(60), {}, chord);
        const uint32_t signature = triadIntervalSignature(out);
        assertNoPreviousSignatureMatch(signatures, color, signature);
        signatures[color] = signature;
    }
}

void test_color_axis_changes_default_triad_in_constrained_scale() {
    uint32_t signatures[StepSequencerChordSpec::MAX_COLOR + 1]{};

    for (uint8_t color = 0; color <= StepSequencerChordSpec::MAX_COLOR; ++color) {
        StepSequencerChordState chord{};
        chord.mode = StepSequencerChordMode::Local;
        chord.local.setLegacyRecipe({.color = color});

        const auto out = resolveStepChord(root(60), cMajor(), chord);
        const uint32_t signature = triadIntervalSignature(out);
        assertNoPreviousSignatureMatch(signatures, color, signature);
        signatures[color] = signature;
    }
}

void test_variant_axis_changes_default_color_triad_in_chromatic_mode() {
    uint32_t signatures[StepSequencerChordSpec::MAX_VARIANT + 1]{};

    for (uint8_t variant = 0; variant <= StepSequencerChordSpec::MAX_VARIANT; ++variant) {
        StepSequencerChordState chord{};
        chord.mode = StepSequencerChordMode::Local;
        chord.local.setLegacyRecipe({.variant = variant});

        const auto out = resolveStepChord(root(60), {}, chord);
        const uint32_t signature = triadIntervalSignature(out);
        assertNoPreviousSignatureMatch(signatures, variant, signature);
        signatures[variant] = signature;
    }
}

void test_variant_axis_changes_default_color_triad_in_constrained_scale() {
    uint32_t signatures[StepSequencerChordSpec::MAX_VARIANT + 1]{};

    for (uint8_t variant = 0; variant <= StepSequencerChordSpec::MAX_VARIANT; ++variant) {
        StepSequencerChordState chord{};
        chord.mode = StepSequencerChordMode::Local;
        chord.local.setLegacyRecipe({.variant = variant});

        const auto out = resolveStepChord(root(60), cMajor(), chord);
        const uint32_t signature = triadIntervalSignature(out);
        assertNoPreviousSignatureMatch(signatures, variant, signature);
        signatures[variant] = signature;
    }
}

void test_spread_axis_changes_three_voice_spacing_at_each_step() {
    uint32_t signatures[StepSequencerChordSpec::MAX_SPREAD + 1]{};

    for (uint8_t spread = 0; spread <= StepSequencerChordSpec::MAX_SPREAD; ++spread) {
        StepSequencerChordState chord{};
        chord.mode = StepSequencerChordMode::Local;
        chord.local.setLegacyRecipe({.spread = spread});

        const auto out = resolveStepChord(root(60), {}, chord);
        const uint32_t signature = triadIntervalSignature(out);
        assertNoPreviousSignatureMatch(signatures, spread, signature);
        signatures[spread] = signature;
    }
}

void test_spread_opens_upper_voices_by_octaves() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local.setLegacyRecipe({.spread = 1});

    const auto out = resolveStepChord(root(60), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(3, out.count);
    TEST_ASSERT_EQUAL_UINT8(60, out.voices[0].note);
    TEST_ASSERT_EQUAL_UINT8(64, out.voices[1].note);
    TEST_ASSERT_EQUAL_UINT8(79, out.voices[2].note);
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

void test_duplicate_clamped_pitches_are_deduplicated() {
    StepSequencerChordState chord{};
    chord.mode = StepSequencerChordMode::Local;
    chord.local.voiceCount = 8;
    chord.local.setLegacyRecipe({.spread = 7});

    const auto out = resolveStepChord(root(127), {}, chord);

    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_UINT8(127, out.voices[0].note);
}

void test_semantic_spec_keeps_six_byte_graph_footprint() {
    TEST_ASSERT_EQUAL_UINT8(6, sizeof(StepSequencerChordSpec));

    const auto spec = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Minor7,
        4,
        StepSequencerChordVoicing::Open,
        2
    );
    TEST_ASSERT_TRUE(spec.isSemantic());
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

    TEST_ASSERT_TRUE(out.semanticRecipe);
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
    RUN_TEST(test_color_axis_changes_default_triad_in_chromatic_mode);
    RUN_TEST(test_color_axis_changes_default_triad_in_constrained_scale);
    RUN_TEST(test_variant_axis_changes_default_color_triad_in_chromatic_mode);
    RUN_TEST(test_variant_axis_changes_default_color_triad_in_constrained_scale);
    RUN_TEST(test_spread_axis_changes_three_voice_spacing_at_each_step);
    RUN_TEST(test_spread_opens_upper_voices_by_octaves);
    RUN_TEST(test_signed_strum_distributes_voice_delays);
    RUN_TEST(test_velocity_curve_is_applied_per_voice);
    RUN_TEST(test_duplicate_clamped_pitches_are_deduplicated);
    RUN_TEST(test_semantic_spec_keeps_six_byte_graph_footprint);
    RUN_TEST(test_semantic_chromatic_major_resolves_named_quality);
    RUN_TEST(test_semantic_diatonic_seventh_stays_inside_scale);
    RUN_TEST(test_semantic_true_inversion_rotates_lowest_voices);
    RUN_TEST(test_semantic_voicing_changes_register_not_membership);
    RUN_TEST(test_semantic_harmony_is_safely_adapted_to_pitch_mode);
    RUN_TEST(test_semantic_inversion_is_bounded_and_reported);
    RUN_TEST(test_analyze_resolved_major_triad);
    RUN_TEST(test_analyze_resolved_minor_seventh);
    RUN_TEST(test_analyze_prefers_step_root_for_inversion);
    RUN_TEST(test_analyze_unknown_chord_keeps_interval_fallback);
    return UNITY_END();
}
