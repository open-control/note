#include <unity.h>

#include <array>
#include <cstring>

#include <oc/note/sequencer/StepSequencerChordPreset.hpp>

using namespace oc::note::sequencer;

namespace {

constexpr uint16_t CHECKSUM_OFFSET = 8;
constexpr uint16_t CHECKSUM_PAYLOAD_OFFSET =
    STEP_SEQUENCER_CHORD_PRESET_PREFIX_SIZE;

uint32_t crc32(const uint8_t* data, uint16_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint16_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void refreshChecksum(std::array<
    uint8_t,
    STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
>& bytes) {
    const uint32_t value = crc32(
        bytes.data() + CHECKSUM_PAYLOAD_OFFSET,
        static_cast<uint16_t>(
            bytes.size() - CHECKSUM_PAYLOAD_OFFSET
        )
    );
    bytes[CHECKSUM_OFFSET] = static_cast<uint8_t>(value & 0xFFU);
    bytes[CHECKSUM_OFFSET + 1U] =
        static_cast<uint8_t>((value >> 8U) & 0xFFU);
    bytes[CHECKSUM_OFFSET + 2U] =
        static_cast<uint8_t>((value >> 16U) & 0xFFU);
    bytes[CHECKSUM_OFFSET + 3U] =
        static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

StepSequencerChordPreset eightVoicePreset() {
    StepSequencerChordPreset preset{};
    preset.reset();
    preset.valid = true;
    TEST_ASSERT_TRUE(setChordPresetMetadata(
        preset,
        "minor-open-001",
        "Minor \xC2\xB7 8v \xC2\xB7 Open"
    ));
    preset.formula = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        8,
        StepSequencerChordVoicing::Open,
        3,
        StepSequencerChordIntervalBasis::ScaleDegrees
    );
    preset.formula.setCustomIntervals({
        0, 2, 4, 6, 8, 10, 12, 14,
    });
    preset.formula.strum = -17;
    preset.formula.velocityCurve = 23;
    preset.sourceShapeHint =
        StepSequencerChordHarmony::DiatonicSeventh;
    TEST_ASSERT_TRUE(setChordPresetSourceContext(
        preset,
        {
            .root = 5,
            .type = StepSequencerScaleType::HarmonicMinor,
            .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
        },
        8
    ));
    return preset;
}

void test_chord_preset_round_trip_preserves_explicit_formula() {
    const auto source = eightVoicePreset();
    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    const auto encoded = encodeChordPreset(
        source,
        bytes.data(),
        bytes.size()
    );
    TEST_ASSERT_TRUE(encoded.ok());
    TEST_ASSERT_EQUAL_UINT16(bytes.size(), encoded.bytesWritten);

    StepSequencerChordPreset decoded{};
    StepSequencerChordPresetCodecReport report{};
    TEST_ASSERT_TRUE(decodeChordPreset(
        bytes.data(),
        bytes.size(),
        decoded,
        &report
    ));
    TEST_ASSERT_TRUE(report.ok());
    TEST_ASSERT_EQUAL_UINT16(bytes.size(), report.bytesProcessed);
    TEST_ASSERT_TRUE(decoded.valid);
    TEST_ASSERT_EQUAL_STRING(source.technicalId, decoded.technicalId);
    TEST_ASSERT_EQUAL_STRING(source.semanticName, decoded.semanticName);
    TEST_ASSERT_TRUE(chordSpecsEqual(source.formula, decoded.formula));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(source.sourceShapeHint),
        static_cast<uint8_t>(decoded.sourceShapeHint)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(source.sourceScale.type),
        static_cast<uint8_t>(decoded.sourceScale.type)
    );
    TEST_ASSERT_EQUAL_UINT8(
        source.sourceRootPitchClass,
        decoded.sourceRootPitchClass
    );
}

void test_metadata_decodes_from_header_without_payload() {
    const auto source = eightVoicePreset();
    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    TEST_ASSERT_TRUE(encodeChordPreset(
        source,
        bytes.data(),
        bytes.size()
    ).ok());

    StepSequencerChordPresetMetadataView metadata{};
    StepSequencerChordPresetCodecReport report{};
    TEST_ASSERT_TRUE(decodeChordPresetMetadata(
        bytes.data(),
        STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE,
        metadata,
        &report
    ));
    TEST_ASSERT_EQUAL_STRING(source.technicalId, metadata.technicalId);
    TEST_ASSERT_EQUAL_STRING(source.semanticName, metadata.semanticName);
    TEST_ASSERT_EQUAL_UINT16(
        STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE,
        report.bytesProcessed
    );
}

void test_full_decode_rejects_checksum_corruption() {
    const auto source = eightVoicePreset();
    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    TEST_ASSERT_TRUE(encodeChordPreset(
        source,
        bytes.data(),
        bytes.size()
    ).ok());
    bytes.back() ^= 0x01U;

    StepSequencerChordPreset decoded{};
    StepSequencerChordPresetCodecReport report{};
    TEST_ASSERT_FALSE(decodeChordPreset(
        bytes.data(),
        bytes.size(),
        decoded,
        &report
    ));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            StepSequencerChordPresetCodecStatus::CHECKSUM_MISMATCH
        ),
        static_cast<uint8_t>(report.status)
    );
}

void test_full_decode_rejects_noncanonical_formula_with_valid_checksum() {
    const auto source = eightVoicePreset();
    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    TEST_ASSERT_TRUE(encodeChordPreset(
        source,
        bytes.data(),
        bytes.size()
    ).ok());

    constexpr uint16_t VOICE_COUNT_OFFSET =
        STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE;
    constexpr uint16_t INTERVALS_OFFSET = VOICE_COUNT_OFFSET + 2U;
    bytes[VOICE_COUNT_OFFSET] = 4U;
    bytes[INTERVALS_OFFSET + 4U] = 12U;
    refreshChecksum(bytes);

    StepSequencerChordPreset decoded{};
    StepSequencerChordPresetCodecReport report{};
    TEST_ASSERT_FALSE(decodeChordPreset(
        bytes.data(),
        bytes.size(),
        decoded,
        &report
    ));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            StepSequencerChordPresetCodecStatus::INVALID_FORMAT
        ),
        static_cast<uint8_t>(report.status)
    );
}

void test_encode_rejects_follow_context_basis() {
    auto preset = eightVoicePreset();
    preset.formula.setIntervalBasis(
        StepSequencerChordIntervalBasis::FollowPitchContext
    );
    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    const auto encoded = encodeChordPreset(
        preset,
        bytes.data(),
        bytes.size()
    );
    TEST_ASSERT_FALSE(encoded.ok());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            StepSequencerChordPresetCodecStatus::INVALID_FORMAT
        ),
        static_cast<uint8_t>(encoded.status)
    );
}

void test_source_context_has_one_canonical_representation_per_basis() {
    auto preset = eightVoicePreset();
    TEST_ASSERT_FALSE(setChordPresetSourceContext(
        preset,
        {},
        0U
    ));

    preset.formula.setIntervalBasis(
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    TEST_ASSERT_TRUE(setChordPresetSourceContext(
        preset,
        {
            .root = 5,
            .type = StepSequencerScaleType::HarmonicMinor,
            .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
        },
        8U
    ));
    TEST_ASSERT_EQUAL_UINT8(0U, preset.sourceScale.root);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerScaleType::Chromatic),
        static_cast<uint8_t>(preset.sourceScale.type)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StepSequencerScaleConstraintMode::Free),
        static_cast<uint8_t>(preset.sourceScale.mode)
    );
    TEST_ASSERT_EQUAL_UINT8(0U, preset.sourceRootPitchClass);

    const auto canonicalChromatic = preset;
    TEST_ASSERT_FALSE(setChordPresetSourceContext(
        preset,
        {
            .root = 12U,
            .type = StepSequencerScaleType::HarmonicMinor,
            .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
        },
        8U
    ));
    TEST_ASSERT_TRUE(chordSpecsEqual(
        canonicalChromatic.formula,
        preset.formula
    ));
    TEST_ASSERT_EQUAL_UINT8(
        canonicalChromatic.sourceScale.root,
        preset.sourceScale.root
    );
    TEST_ASSERT_EQUAL_UINT8(
        canonicalChromatic.sourceRootPitchClass,
        preset.sourceRootPitchClass
    );

    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    TEST_ASSERT_TRUE(encodeChordPreset(
        preset,
        bytes.data(),
        bytes.size()
    ).ok());

    preset.sourceScale.root = 5U;
    TEST_ASSERT_FALSE(encodeChordPreset(
        preset,
        bytes.data(),
        bytes.size()
    ).ok());
}

void test_named_shape_flattens_to_explicit_formula() {
    auto source = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Minor7,
        4,
        StepSequencerChordVoicing::Wide,
        2,
        StepSequencerChordIntervalBasis::FollowPitchContext
    );
    source.strum = 31;
    source.velocityCurve = -22;

    StepSequencerChordSpec explicitFormula{};
    TEST_ASSERT_TRUE(makeExplicitChordPresetFormula(
        source,
        false,
        explicitFormula
    ));
    TEST_ASSERT_TRUE(explicitFormula.isCustom());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            StepSequencerChordIntervalBasis::ChromaticSemitones
        ),
        static_cast<uint8_t>(explicitFormula.intervalBasis())
    );
    TEST_ASSERT_EQUAL_UINT8(4, explicitFormula.voices());
    TEST_ASSERT_EQUAL_UINT8(0, explicitFormula.customInterval(0));
    TEST_ASSERT_EQUAL_UINT8(3, explicitFormula.customInterval(1));
    TEST_ASSERT_EQUAL_UINT8(7, explicitFormula.customInterval(2));
    TEST_ASSERT_EQUAL_UINT8(10, explicitFormula.customInterval(3));
    TEST_ASSERT_EQUAL_UINT8(2, explicitFormula.inversion());
    TEST_ASSERT_EQUAL_INT8(31, explicitFormula.strum);
    TEST_ASSERT_EQUAL_INT8(-22, explicitFormula.velocityCurve);
}

void test_decode_rejects_unknown_version_before_checksum() {
    const auto source = eightVoicePreset();
    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    TEST_ASSERT_TRUE(encodeChordPreset(
        source,
        bytes.data(),
        bytes.size()
    ).ok());
    bytes[4] = 2U;

    StepSequencerChordPreset decoded{};
    StepSequencerChordPresetCodecReport report{};
    TEST_ASSERT_FALSE(decodeChordPreset(
        bytes.data(),
        bytes.size(),
        decoded,
        &report
    ));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            StepSequencerChordPresetCodecStatus::UNSUPPORTED_VERSION
        ),
        static_cast<uint8_t>(report.status)
    );
}

void test_failed_decodes_leave_destinations_unchanged() {
    const auto source = eightVoicePreset();
    std::array<
        uint8_t,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    TEST_ASSERT_TRUE(encodeChordPreset(
        source,
        bytes.data(),
        bytes.size()
    ).ok());
    bytes[4] = static_cast<uint8_t>(
        StepSequencerChordPreset::CURRENT_FORMAT_VERSION + 1U
    );

    auto decoded = eightVoicePreset();
    TEST_ASSERT_TRUE(setChordPresetMetadata(
        decoded,
        "sentinel",
        "Sentinel"
    ));
    const auto decodedBefore = decoded;
    StepSequencerChordPresetCodecReport report{};
    TEST_ASSERT_FALSE(decodeChordPreset(
        bytes.data(),
        bytes.size(),
        decoded,
        &report
    ));
    TEST_ASSERT_EQUAL_MEMORY(
        &decodedBefore,
        &decoded,
        sizeof(decoded)
    );

    StepSequencerChordPresetMetadataView metadata{};
    metadata.formatVersion = 77U;
    std::strcpy(metadata.technicalId, "sentinel");
    std::strcpy(metadata.semanticName, "Sentinel");
    const auto metadataBefore = metadata;
    TEST_ASSERT_FALSE(decodeChordPresetMetadata(
        bytes.data(),
        bytes.size(),
        metadata,
        &report
    ));
    TEST_ASSERT_EQUAL_MEMORY(
        &metadataBefore,
        &metadata,
        sizeof(metadata)
    );
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_chord_preset_round_trip_preserves_explicit_formula);
    RUN_TEST(test_metadata_decodes_from_header_without_payload);
    RUN_TEST(test_full_decode_rejects_checksum_corruption);
    RUN_TEST(test_full_decode_rejects_noncanonical_formula_with_valid_checksum);
    RUN_TEST(test_encode_rejects_follow_context_basis);
    RUN_TEST(test_source_context_has_one_canonical_representation_per_basis);
    RUN_TEST(test_named_shape_flattens_to_explicit_formula);
    RUN_TEST(test_decode_rejects_unknown_version_before_checksum);
    RUN_TEST(test_failed_decodes_leave_destinations_unchanged);
    return UNITY_END();
}
