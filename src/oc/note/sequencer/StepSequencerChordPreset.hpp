#pragma once

#include <cstddef>
#include <cstdint>

#include "StepSequencerChord.hpp"

namespace oc::note::sequencer {

enum class StepSequencerChordPresetCodecStatus : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    INVALID_FORMAT,
    UNSUPPORTED_VERSION,
    BUFFER_TOO_SMALL,
    CHECKSUM_MISMATCH,
};

struct StepSequencerChordPresetCodecReport {
    StepSequencerChordPresetCodecStatus status =
        StepSequencerChordPresetCodecStatus::OK;
    uint16_t bytesProcessed = 0;

    void reset();
    [[nodiscard]] bool ok() const {
        return status == StepSequencerChordPresetCodecStatus::OK;
    }
};

/**
 * Portable, file-backed chord formula.
 *
 * `formula` is always a semantic Custom formula with an explicit DEG or ST
 * basis. Built-in shapes are flattened before capture so loading never
 * depends on a future shape catalog. `sourceShapeHint` is presentation-only.
 */
struct StepSequencerChordPreset {
    static constexpr uint8_t CURRENT_FORMAT_VERSION = 1;
    static constexpr size_t TECHNICAL_ID_SIZE = 55;
    static constexpr size_t SEMANTIC_NAME_SIZE = 32;

    bool valid = false;
    uint8_t formatVersion = CURRENT_FORMAT_VERSION;
    char technicalId[TECHNICAL_ID_SIZE] = {};
    char semanticName[SEMANTIC_NAME_SIZE] = {};
    StepSequencerChordSpec formula{};
    StepSequencerChordHarmony sourceShapeHint =
        StepSequencerChordHarmony::Custom;
    StepSequencerScaleSettings sourceScale{};
    uint8_t sourceRootPitchClass = 0;

    void reset();
};

struct StepSequencerChordPresetMetadataView {
    uint8_t formatVersion = 0;
    char technicalId[StepSequencerChordPreset::TECHNICAL_ID_SIZE] = {};
    char semanticName[StepSequencerChordPreset::SEMANTIC_NAME_SIZE] = {};
};

struct StepSequencerChordPresetEncodeResult {
    StepSequencerChordPresetCodecStatus status =
        StepSequencerChordPresetCodecStatus::OK;
    uint16_t bytesWritten = 0;

    [[nodiscard]] bool ok() const {
        return status == StepSequencerChordPresetCodecStatus::OK;
    }
};

inline constexpr uint16_t STEP_SEQUENCER_CHORD_PRESET_PREFIX_SIZE = 12;
inline constexpr uint16_t STEP_SEQUENCER_CHORD_PRESET_METADATA_SIZE =
    StepSequencerChordPreset::TECHNICAL_ID_SIZE +
    StepSequencerChordPreset::SEMANTIC_NAME_SIZE;
inline constexpr uint16_t STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE =
    STEP_SEQUENCER_CHORD_PRESET_PREFIX_SIZE +
    STEP_SEQUENCER_CHORD_PRESET_METADATA_SIZE;
inline constexpr uint16_t STEP_SEQUENCER_CHORD_PRESET_PAYLOAD_SIZE = 20;
inline constexpr uint16_t STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE =
    STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE +
    STEP_SEQUENCER_CHORD_PRESET_PAYLOAD_SIZE;

static_assert(STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE == 119);

/**
 * Flattens any semantic named shape into an explicit Custom formula while
 * retaining inversion, voicing, strum and velocity contour.
 */
bool makeExplicitChordPresetFormula(
    StepSequencerChordSpec source,
    bool intervalUsesScaleDegrees,
    StepSequencerChordSpec& out
);

bool validChordPresetTechnicalIdText(const char* technicalId);
bool validChordPresetSemanticName(const char* semanticName);

bool setChordPresetMetadata(
    StepSequencerChordPreset& preset,
    const char* technicalId,
    const char* semanticName
);

/**
 * Stores only the source context that can affect later projection.
 *
 * Scale-degree formulas require a constrained source scale and retain their
 * source root pitch class. Chromatic formulas canonicalize both fields to
 * their neutral values because neither participates in semitone projection.
 */
bool setChordPresetSourceContext(
    StepSequencerChordPreset& preset,
    StepSequencerScaleSettings sourceScale,
    uint8_t sourceRootPitchClass
);

StepSequencerChordPresetEncodeResult encodeChordPreset(
    const StepSequencerChordPreset& preset,
    uint8_t* out,
    uint16_t capacity
);

bool decodeChordPreset(
    const uint8_t* data,
    uint16_t size,
    StepSequencerChordPreset& out,
    StepSequencerChordPresetCodecReport* report = nullptr
);

/**
 * Reads only the bounded metadata prefix. Payload bytes and checksum
 * validation are intentionally deferred to the full decoder.
 */
bool decodeChordPresetMetadata(
    const uint8_t* data,
    uint16_t size,
    StepSequencerChordPresetMetadataView& out,
    StepSequencerChordPresetCodecReport* report = nullptr
);

}  // namespace oc::note::sequencer
