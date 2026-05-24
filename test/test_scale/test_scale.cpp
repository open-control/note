#include <unity.h>

#include <oc/note/sequencer/StepSequencerScale.hpp>

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;
using oc::note::sequencer::moveByScaleDegrees;
using oc::note::sequencer::resolveScaleNote;
using oc::note::sequencer::scaleContainsNote;

void setUp() {}

void tearDown() {}

void test_default_scale_is_chromatic_free() {
    StepSequencerScaleSettings settings{};

    TEST_ASSERT_EQUAL_UINT8(0, settings.root);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerScaleType::Chromatic),
        static_cast<uint8_t>(settings.type)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerScaleConstraintMode::Free),
        static_cast<uint8_t>(settings.mode)
    );

    for (uint8_t note = 0; note < 128; ++note) {
        TEST_ASSERT_TRUE(scaleContainsNote(settings, note));
        const auto resolved = resolveScaleNote(note, settings);
        TEST_ASSERT_EQUAL_UINT8(note, resolved.outputNote);
        TEST_ASSERT_FALSE(resolved.constrained);
    }
}

void test_major_scale_membership_uses_root() {
    StepSequencerScaleSettings cMajor{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::Free,
    };

    TEST_ASSERT_TRUE(scaleContainsNote(cMajor, 60));   // C
    TEST_ASSERT_TRUE(scaleContainsNote(cMajor, 62));   // D
    TEST_ASSERT_FALSE(scaleContainsNote(cMajor, 61));  // C#

    StepSequencerScaleSettings dMajor = cMajor;
    dMajor.root = 2;

    TEST_ASSERT_TRUE(scaleContainsNote(dMajor, 62));   // D
    TEST_ASSERT_TRUE(scaleContainsNote(dMajor, 66));   // F#
    TEST_ASSERT_FALSE(scaleContainsNote(dMajor, 65));  // F
}

void test_free_mode_reports_out_of_scale_without_changing_note() {
    StepSequencerScaleSettings settings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::Free,
    };

    const auto resolved = resolveScaleNote(61, settings);

    TEST_ASSERT_EQUAL_UINT8(61, resolved.inputNote);
    TEST_ASSERT_EQUAL_UINT8(61, resolved.outputNote);
    TEST_ASSERT_FALSE(resolved.inputInScale);
    TEST_ASSERT_FALSE(resolved.constrained);
}

void test_constrain_nearest_resolves_tie_upward() {
    StepSequencerScaleSettings settings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };

    const auto resolved = resolveScaleNote(61, settings);  // C# between C and D

    TEST_ASSERT_EQUAL_UINT8(61, resolved.inputNote);
    TEST_ASSERT_EQUAL_UINT8(62, resolved.outputNote);
    TEST_ASSERT_FALSE(resolved.inputInScale);
    TEST_ASSERT_TRUE(resolved.constrained);
    TEST_ASSERT_EQUAL_INT8(1, resolved.semitoneDelta);
}

void test_constrain_up_and_down_are_directional() {
    StepSequencerScaleSettings settings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainUp,
    };

    TEST_ASSERT_EQUAL_UINT8(62, resolveScaleNote(61, settings).outputNote);

    settings.mode = StepSequencerScaleConstraintMode::ConstrainDown;
    TEST_ASSERT_EQUAL_UINT8(60, resolveScaleNote(61, settings).outputNote);
}

void test_scale_degree_movement_skips_non_scale_notes() {
    StepSequencerScaleSettings settings{
        .root = 0,
        .type = StepSequencerScaleType::NaturalMinor,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };

    TEST_ASSERT_EQUAL_UINT8(63, moveByScaleDegrees(60, 2, settings));  // C -> Eb
    TEST_ASSERT_EQUAL_UINT8(58, moveByScaleDegrees(60, -1, settings)); // C -> Bb
}

void test_settings_clamp_invalid_values_to_defaults() {
    StepSequencerScaleSettings settings{
        .root = 14,
        .type = static_cast<StepSequencerScaleType>(255),
        .mode = static_cast<StepSequencerScaleConstraintMode>(255),
    };

    settings.clamp();

    TEST_ASSERT_EQUAL_UINT8(2, settings.root);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerScaleType::Chromatic),
        static_cast<uint8_t>(settings.type)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerScaleConstraintMode::Free),
        static_cast<uint8_t>(settings.mode)
    );
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_scale_is_chromatic_free);
    RUN_TEST(test_major_scale_membership_uses_root);
    RUN_TEST(test_free_mode_reports_out_of_scale_without_changing_note);
    RUN_TEST(test_constrain_nearest_resolves_tie_upward);
    RUN_TEST(test_constrain_up_and_down_are_directional);
    RUN_TEST(test_scale_degree_movement_skips_non_scale_notes);
    RUN_TEST(test_settings_clamp_invalid_values_to_defaults);
    return UNITY_END();
}
