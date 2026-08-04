#include "StepSequencerChordPreset.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace oc::note::sequencer {
namespace {

constexpr uint32_t CHORD_PRESET_MAGIC = 0x5043534DU;  // "MSCP"
constexpr uint8_t CHORD_PRESET_FLAGS = 0;
constexpr uint8_t CHORD_PRESET_RESERVED = 0;
constexpr uint16_t CHECKSUM_OFFSET = 8;
constexpr uint16_t CHECKSUM_PAYLOAD_OFFSET =
    STEP_SEQUENCER_CHORD_PRESET_PREFIX_SIZE;

class Writer {
public:
    Writer(uint8_t* data, uint16_t capacity)
        : data_(data), capacity_(capacity) {}

    bool writeU8(uint8_t value) {
        if (!reserve_(1)) return false;
        data_[position_++] = value;
        return true;
    }

    bool writeI8(int8_t value) {
        return writeU8(static_cast<uint8_t>(value));
    }

    bool writeU16(uint16_t value) {
        return writeU8(static_cast<uint8_t>(value & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    }

    bool writeU32(uint32_t value) {
        return writeU8(static_cast<uint8_t>(value & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 8U) & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 16U) & 0xFFU)) &&
               writeU8(static_cast<uint8_t>((value >> 24U) & 0xFFU));
    }

    bool writeBytes(const void* source, uint16_t size) {
        if (source == nullptr || !reserve_(size)) return false;
        std::memcpy(data_ + position_, source, size);
        position_ = static_cast<uint16_t>(position_ + size);
        return true;
    }

    [[nodiscard]] uint16_t position() const { return position_; }

private:
    bool reserve_(uint16_t size) const {
        return data_ != nullptr &&
               position_ <= capacity_ &&
               size <= static_cast<uint16_t>(capacity_ - position_);
    }

    uint8_t* data_ = nullptr;
    uint16_t capacity_ = 0;
    uint16_t position_ = 0;
};

class Reader {
public:
    Reader(const uint8_t* data, uint16_t size)
        : data_(data), size_(size) {}

    bool readU8(uint8_t& value) {
        if (!reserve_(1)) return false;
        value = data_[position_++];
        return true;
    }

    bool readI8(int8_t& value) {
        uint8_t raw = 0;
        if (!readU8(raw)) return false;
        value = static_cast<int8_t>(raw);
        return true;
    }

    bool readU16(uint16_t& value) {
        uint8_t low = 0;
        uint8_t high = 0;
        if (!readU8(low) || !readU8(high)) return false;
        value = static_cast<uint16_t>(
            low | (static_cast<uint16_t>(high) << 8U)
        );
        return true;
    }

    bool readU32(uint32_t& value) {
        uint8_t bytes[4] = {};
        if (!readU8(bytes[0]) || !readU8(bytes[1]) ||
            !readU8(bytes[2]) || !readU8(bytes[3])) {
            return false;
        }
        value = static_cast<uint32_t>(
            bytes[0] |
            (static_cast<uint32_t>(bytes[1]) << 8U) |
            (static_cast<uint32_t>(bytes[2]) << 16U) |
            (static_cast<uint32_t>(bytes[3]) << 24U)
        );
        return true;
    }

    bool readBytes(void* target, uint16_t size) {
        if (target == nullptr || !reserve_(size)) return false;
        std::memcpy(target, data_ + position_, size);
        position_ = static_cast<uint16_t>(position_ + size);
        return true;
    }

    [[nodiscard]] uint16_t position() const { return position_; }

private:
    bool reserve_(uint16_t size) const {
        return data_ != nullptr &&
               position_ <= size_ &&
               size <= static_cast<uint16_t>(size_ - position_);
    }

    const uint8_t* data_ = nullptr;
    uint16_t size_ = 0;
    uint16_t position_ = 0;
};

struct Prefix {
    uint32_t magic = 0;
    uint8_t version = 0;
    uint8_t flags = 0;
    uint16_t length = 0;
    uint32_t checksum = 0;
};

struct Metadata {
    char technicalId[StepSequencerChordPreset::TECHNICAL_ID_SIZE] = {};
    char semanticName[StepSequencerChordPreset::SEMANTIC_NAME_SIZE] = {};
};

FLASHMEM uint32_t crc32(const uint8_t* data, uint16_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint16_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

FLASHMEM void writeU32At(uint8_t* data, uint16_t offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>(value & 0xFFU);
    data[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    data[offset + 2U] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    data[offset + 3U] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

FLASHMEM bool writePrefix(Writer& writer, uint32_t checksum) {
    return writer.writeU32(CHORD_PRESET_MAGIC) &&
           writer.writeU8(StepSequencerChordPreset::CURRENT_FORMAT_VERSION) &&
           writer.writeU8(CHORD_PRESET_FLAGS) &&
           writer.writeU16(STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE) &&
           writer.writeU32(checksum);
}

FLASHMEM bool readPrefix(Reader& reader, Prefix& prefix) {
    return reader.readU32(prefix.magic) &&
           reader.readU8(prefix.version) &&
           reader.readU8(prefix.flags) &&
           reader.readU16(prefix.length) &&
           reader.readU32(prefix.checksum);
}

FLASHMEM bool prefixValid(
    const Prefix& prefix,
    StepSequencerChordPresetCodecReport* report
) {
    if (prefix.magic != CHORD_PRESET_MAGIC ||
        prefix.flags != CHORD_PRESET_FLAGS ||
        prefix.length != STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE) {
        if (report != nullptr) {
            report->status = StepSequencerChordPresetCodecStatus::INVALID_FORMAT;
        }
        return false;
    }
    if (prefix.version !=
        StepSequencerChordPreset::CURRENT_FORMAT_VERSION) {
        if (report != nullptr) {
            report->status =
                StepSequencerChordPresetCodecStatus::UNSUPPORTED_VERSION;
        }
        return false;
    }
    return true;
}

FLASHMEM bool boundedTextLength(
    const char* text,
    size_t capacity,
    size_t& length
) {
    length = 0;
    if (text == nullptr || capacity == 0) return false;
    while (length < capacity && text[length] != '\0') ++length;
    return length < capacity;
}

FLASHMEM bool validUtf8Text(const char* text, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const uint8_t first = static_cast<uint8_t>(text[offset]);
        uint32_t codePoint = 0;
        size_t width = 0;
        if (first < 0x80U) {
            codePoint = first;
            width = 1;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            codePoint = static_cast<uint32_t>(first & 0x1FU);
            width = 2;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codePoint = static_cast<uint32_t>(first & 0x0FU);
            width = 3;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codePoint = static_cast<uint32_t>(first & 0x07U);
            width = 4;
        } else {
            return false;
        }
        if (width > length - offset) return false;
        for (size_t index = 1; index < width; ++index) {
            const uint8_t continuation =
                static_cast<uint8_t>(text[offset + index]);
            if ((continuation & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) |
                        static_cast<uint32_t>(continuation & 0x3FU);
        }
        const bool overlong =
            (width == 2U && codePoint < 0x80U) ||
            (width == 3U && codePoint < 0x800U) ||
            (width == 4U && codePoint < 0x10000U);
        if (overlong || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint < 0x20U ||
            (codePoint >= 0x7FU && codePoint <= 0x9FU)) {
            return false;
        }
        offset += width;
    }
    return true;
}

FLASHMEM bool metadataValid(const Metadata& metadata) {
    return validChordPresetTechnicalIdText(metadata.technicalId) &&
           validChordPresetSemanticName(metadata.semanticName);
}

FLASHMEM bool chordSpecCanonical(const StepSequencerChordSpec& spec) {
    auto canonical = spec;
    canonical.clamp();
    return chordSpecsEqual(spec, canonical);
}

FLASHMEM bool formulaValid(const StepSequencerChordSpec& formula) {
    if (!formula.isCustom()) return false;
    const auto basis = formula.intervalBasis();
    if (basis != StepSequencerChordIntervalBasis::ScaleDegrees &&
        basis != StepSequencerChordIntervalBasis::ChromaticSemitones) {
        return false;
    }
    const uint8_t voices = formula.voices();
    if (voices < 2U || voices > StepSequencerChordSpec::MAX_CUSTOM_VOICES ||
        formula.inversion() >= voices) {
        return false;
    }
    if (!chordSpecCanonical(formula) ||
        formula.customInterval(0) != 0U) {
        return false;
    }
    uint8_t previous = 0;
    for (uint8_t voice = 1; voice < voices; ++voice) {
        const uint8_t interval = formula.customInterval(voice);
        if (interval <= previous ||
            interval > StepSequencerChordSpec::MAX_CUSTOM_INTERVAL) {
            return false;
        }
        previous = interval;
    }
    for (uint8_t voice = voices;
         voice < StepSequencerChordSpec::MAX_CUSTOM_VOICES;
         ++voice) {
        if (formula.customInterval(voice) != 0U) return false;
    }
    return true;
}

FLASHMEM bool sourceScaleValid(
    const StepSequencerScaleSettings& sourceScale
) {
    return sourceScale.root < 12U &&
           static_cast<uint8_t>(sourceScale.type) <=
               static_cast<uint8_t>(StepSequencerScaleType::WholeTone) &&
           static_cast<uint8_t>(sourceScale.mode) <=
               static_cast<uint8_t>(
                   StepSequencerScaleConstraintMode::ConstrainDown
               );
}

FLASHMEM bool sourceContextValid(
    const StepSequencerChordPreset& preset
) {
    if (!sourceScaleValid(preset.sourceScale) ||
        preset.sourceRootPitchClass >= 12U) {
        return false;
    }
    if (preset.formula.intervalBasis() ==
        StepSequencerChordIntervalBasis::ScaleDegrees) {
        return preset.sourceScale.isConstrained();
    }
    return preset.formula.intervalBasis() ==
               StepSequencerChordIntervalBasis::ChromaticSemitones &&
           preset.sourceScale.root == 0U &&
           preset.sourceScale.type == StepSequencerScaleType::Chromatic &&
           preset.sourceScale.mode ==
               StepSequencerScaleConstraintMode::Free &&
           preset.sourceRootPitchClass == 0U;
}

FLASHMEM bool encodedIntervalsValid(
    const std::array<
        uint8_t,
        StepSequencerChordSpec::MAX_CUSTOM_VOICES
    >& intervals,
    uint8_t voiceCount
) {
    if (voiceCount < 2U ||
        voiceCount > StepSequencerChordSpec::MAX_CUSTOM_VOICES ||
        intervals[0] != 0U) {
        return false;
    }
    uint8_t previous = 0;
    for (uint8_t voice = 1; voice < voiceCount; ++voice) {
        const uint8_t interval = intervals[voice];
        if (interval <= previous ||
            interval > StepSequencerChordSpec::MAX_CUSTOM_INTERVAL) {
            return false;
        }
        previous = interval;
    }
    for (uint8_t voice = voiceCount;
         voice < StepSequencerChordSpec::MAX_CUSTOM_VOICES;
         ++voice) {
        if (intervals[voice] != 0U) return false;
    }
    return true;
}

FLASHMEM bool presetValid(const StepSequencerChordPreset& preset) {
    return preset.valid &&
           preset.formatVersion ==
               StepSequencerChordPreset::CURRENT_FORMAT_VERSION &&
           validChordPresetTechnicalIdText(preset.technicalId) &&
           validChordPresetSemanticName(preset.semanticName) &&
           formulaValid(preset.formula) &&
           static_cast<uint8_t>(preset.sourceShapeHint) <
               static_cast<uint8_t>(StepSequencerChordHarmony::Count) &&
           sourceContextValid(preset);
}

FLASHMEM bool readMetadata(Reader& reader, Metadata& metadata) {
    return reader.readBytes(
               metadata.technicalId,
               sizeof(metadata.technicalId)
           ) &&
           reader.readBytes(
               metadata.semanticName,
               sizeof(metadata.semanticName)
           );
}

FLASHMEM bool readAndValidateHeader(
    const uint8_t* data,
    uint16_t size,
    Prefix& prefix,
    Metadata& metadata,
    StepSequencerChordPresetCodecReport* report
) {
    if (data == nullptr ||
        size < STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE) {
        if (report != nullptr) {
            report->status = data == nullptr
                ? StepSequencerChordPresetCodecStatus::INVALID_ARGUMENT
                : StepSequencerChordPresetCodecStatus::BUFFER_TOO_SMALL;
        }
        return false;
    }
    Reader reader(data, size);
    if (!readPrefix(reader, prefix) || !prefixValid(prefix, report) ||
        !readMetadata(reader, metadata) || !metadataValid(metadata) ||
        reader.position() != STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE) {
        if (report != nullptr && report->ok()) {
            report->status =
                StepSequencerChordPresetCodecStatus::INVALID_FORMAT;
        }
        return false;
    }
    if (report != nullptr) {
        report->bytesProcessed =
            STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE;
    }
    return true;
}

}  // namespace

FLASHMEM void StepSequencerChordPresetCodecReport::reset() {
    *this = {};
}

FLASHMEM void StepSequencerChordPreset::reset() {
    *this = {};
    formatVersion = CURRENT_FORMAT_VERSION;
    formula = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        3,
        StepSequencerChordVoicing::Close,
        0,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    sourceShapeHint = StepSequencerChordHarmony::Custom;
}

FLASHMEM bool makeExplicitChordPresetFormula(
    StepSequencerChordSpec source,
    bool intervalUsesScaleDegrees,
    StepSequencerChordSpec& out
) {
    if (!chordSpecCanonical(source)) return false;
    const auto resolved = resolveChordFormula(
        source,
        intervalUsesScaleDegrees
    );
    if (!resolved.valid || resolved.count < 2U ||
        resolved.count > StepSequencerChordSpec::MAX_CUSTOM_VOICES) {
        return false;
    }

    std::array<
        uint8_t,
        StepSequencerChordSpec::MAX_CUSTOM_VOICES
    > intervals{};
    int16_t previous = -1;
    for (uint8_t voice = 0; voice < resolved.count; ++voice) {
        const int16_t interval = resolved.intervals[voice];
        if (interval < 0 ||
            interval > StepSequencerChordSpec::MAX_CUSTOM_INTERVAL ||
            interval <= previous) {
            return false;
        }
        intervals[voice] = static_cast<uint8_t>(interval);
        previous = interval;
    }
    if (intervals[0] != 0U) return false;

    auto explicitFormula = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        resolved.count,
        source.voicing(),
        std::min<uint8_t>(
            source.inversion(),
            static_cast<uint8_t>(resolved.count - 1U)
        ),
        intervalUsesScaleDegrees
            ? StepSequencerChordIntervalBasis::ScaleDegrees
            : StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    explicitFormula.setCustomIntervals(intervals);
    explicitFormula.strum = source.strum;
    explicitFormula.velocityCurve = source.velocityCurve;
    explicitFormula.clamp();
    if (!formulaValid(explicitFormula)) return false;
    out = explicitFormula;
    return true;
}

FLASHMEM bool validChordPresetTechnicalIdText(const char* technicalId) {
    size_t length = 0;
    if (!boundedTextLength(
            technicalId,
            StepSequencerChordPreset::TECHNICAL_ID_SIZE,
            length
        ) ||
        length == 0U ||
        technicalId[0] == ' ' ||
        technicalId[length - 1U] == ' ') {
        return false;
    }
    return validUtf8Text(technicalId, length);
}

FLASHMEM bool validChordPresetSemanticName(const char* semanticName) {
    size_t length = 0;
    if (!boundedTextLength(
            semanticName,
            StepSequencerChordPreset::SEMANTIC_NAME_SIZE,
            length
        ) ||
        length == 0U ||
        semanticName[0] == ' ' ||
        semanticName[length - 1U] == ' ') {
        return false;
    }
    return validUtf8Text(semanticName, length);
}

FLASHMEM bool setChordPresetMetadata(
    StepSequencerChordPreset& preset,
    const char* technicalId,
    const char* semanticName
) {
    if (!validChordPresetTechnicalIdText(technicalId) ||
        !validChordPresetSemanticName(semanticName)) {
        return false;
    }
    std::memset(preset.technicalId, 0, sizeof(preset.technicalId));
    std::memset(preset.semanticName, 0, sizeof(preset.semanticName));
    std::strncpy(
        preset.technicalId,
        technicalId,
        sizeof(preset.technicalId) - 1U
    );
    std::strncpy(
        preset.semanticName,
        semanticName,
        sizeof(preset.semanticName) - 1U
    );
    preset.formatVersion = StepSequencerChordPreset::CURRENT_FORMAT_VERSION;
    return true;
}

FLASHMEM bool setChordPresetSourceContext(
    StepSequencerChordPreset& preset,
    StepSequencerScaleSettings sourceScale,
    uint8_t sourceRootPitchClass
) {
    const auto basis = preset.formula.intervalBasis();
    if (!sourceScaleValid(sourceScale) ||
        sourceRootPitchClass >= 12U) {
        return false;
    }
    if (basis == StepSequencerChordIntervalBasis::ScaleDegrees) {
        if (!sourceScale.isConstrained()) {
            return false;
        }
        preset.sourceScale = sourceScale;
        preset.sourceRootPitchClass = sourceRootPitchClass;
        return true;
    }
    if (basis != StepSequencerChordIntervalBasis::ChromaticSemitones) {
        return false;
    }
    preset.sourceScale = {};
    preset.sourceRootPitchClass = 0U;
    return true;
}

FLASHMEM StepSequencerChordPresetEncodeResult encodeChordPreset(
    const StepSequencerChordPreset& preset,
    uint8_t* out,
    uint16_t capacity
) {
    if (out == nullptr) {
        return {
            StepSequencerChordPresetCodecStatus::INVALID_ARGUMENT,
            0,
        };
    }
    if (capacity < STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE) {
        return {
            StepSequencerChordPresetCodecStatus::BUFFER_TOO_SMALL,
            0,
        };
    }
    if (!presetValid(preset)) {
        return {
            StepSequencerChordPresetCodecStatus::INVALID_FORMAT,
            0,
        };
    }

    std::memset(out, 0, STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE);
    Writer writer(out, capacity);
    const auto& formula = preset.formula;
    if (!writePrefix(writer, 0U) ||
        !writer.writeBytes(preset.technicalId, sizeof(preset.technicalId)) ||
        !writer.writeBytes(preset.semanticName, sizeof(preset.semanticName)) ||
        !writer.writeU8(formula.voices()) ||
        !writer.writeU8(static_cast<uint8_t>(formula.intervalBasis()))) {
        return {
            StepSequencerChordPresetCodecStatus::BUFFER_TOO_SMALL,
            0,
        };
    }
    for (uint8_t voice = 0;
         voice < StepSequencerChordSpec::MAX_CUSTOM_VOICES;
         ++voice) {
        if (!writer.writeU8(formula.customInterval(voice))) {
            return {
                StepSequencerChordPresetCodecStatus::BUFFER_TOO_SMALL,
                0,
            };
        }
    }
    if (!writer.writeU8(formula.inversion()) ||
        !writer.writeU8(static_cast<uint8_t>(formula.voicing())) ||
        !writer.writeI8(formula.strum) ||
        !writer.writeI8(formula.velocityCurve) ||
        !writer.writeU8(static_cast<uint8_t>(preset.sourceShapeHint)) ||
        !writer.writeU8(preset.sourceScale.root) ||
        !writer.writeU8(static_cast<uint8_t>(preset.sourceScale.type)) ||
        !writer.writeU8(static_cast<uint8_t>(preset.sourceScale.mode)) ||
        !writer.writeU8(preset.sourceRootPitchClass) ||
        !writer.writeU8(CHORD_PRESET_RESERVED) ||
        writer.position() != STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE) {
        return {
            StepSequencerChordPresetCodecStatus::BUFFER_TOO_SMALL,
            0,
        };
    }

    const uint32_t checksum = crc32(
        out + CHECKSUM_PAYLOAD_OFFSET,
        static_cast<uint16_t>(
            STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE -
            CHECKSUM_PAYLOAD_OFFSET
        )
    );
    writeU32At(out, CHECKSUM_OFFSET, checksum);
    return {
        StepSequencerChordPresetCodecStatus::OK,
        STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE,
    };
}

FLASHMEM bool decodeChordPresetMetadata(
    const uint8_t* data,
    uint16_t size,
    StepSequencerChordPresetMetadataView& out,
    StepSequencerChordPresetCodecReport* report
) {
    if (report != nullptr) report->reset();
    Prefix prefix{};
    Metadata metadata{};
    if (!readAndValidateHeader(data, size, prefix, metadata, report)) {
        return false;
    }
    StepSequencerChordPresetMetadataView decoded{};
    decoded.formatVersion = prefix.version;
    std::memcpy(
        decoded.technicalId,
        metadata.technicalId,
        sizeof(decoded.technicalId)
    );
    std::memcpy(
        decoded.semanticName,
        metadata.semanticName,
        sizeof(decoded.semanticName)
    );
    out = decoded;
    return true;
}

FLASHMEM bool decodeChordPreset(
    const uint8_t* data,
    uint16_t size,
    StepSequencerChordPreset& out,
    StepSequencerChordPresetCodecReport* report
) {
    if (report != nullptr) report->reset();
    Prefix prefix{};
    Metadata metadata{};
    if (!readAndValidateHeader(data, size, prefix, metadata, report)) {
        return false;
    }
    if (size != STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE) {
        if (report != nullptr) {
            report->status =
                StepSequencerChordPresetCodecStatus::INVALID_FORMAT;
        }
        return false;
    }
    const uint32_t checksum = crc32(
        data + CHECKSUM_PAYLOAD_OFFSET,
        static_cast<uint16_t>(size - CHECKSUM_PAYLOAD_OFFSET)
    );
    if (checksum != prefix.checksum) {
        if (report != nullptr) {
            report->status =
                StepSequencerChordPresetCodecStatus::CHECKSUM_MISMATCH;
        }
        return false;
    }

    Reader reader(data, size);
    Prefix ignoredPrefix{};
    Metadata ignoredMetadata{};
    uint8_t voiceCount = 0;
    uint8_t basis = 0;
    std::array<
        uint8_t,
        StepSequencerChordSpec::MAX_CUSTOM_VOICES
    > intervals{};
    uint8_t inversion = 0;
    uint8_t voicing = 0;
    int8_t strum = 0;
    int8_t velocityCurve = 0;
    uint8_t sourceShapeHint = 0;
    uint8_t sourceScaleRoot = 0;
    uint8_t sourceScaleType = 0;
    uint8_t sourceScaleMode = 0;
    uint8_t sourceRootPitchClass = 0;
    uint8_t reserved = 0;
    if (!readPrefix(reader, ignoredPrefix) ||
        !readMetadata(reader, ignoredMetadata) ||
        !reader.readU8(voiceCount) ||
        !reader.readU8(basis)) {
        if (report != nullptr) {
            report->status =
                StepSequencerChordPresetCodecStatus::INVALID_FORMAT;
        }
        return false;
    }
    for (auto& interval : intervals) {
        if (!reader.readU8(interval)) {
            if (report != nullptr) {
                report->status =
                    StepSequencerChordPresetCodecStatus::INVALID_FORMAT;
            }
            return false;
        }
    }
    if (!reader.readU8(inversion) ||
        !reader.readU8(voicing) ||
        !reader.readI8(strum) ||
        !reader.readI8(velocityCurve) ||
        !reader.readU8(sourceShapeHint) ||
        !reader.readU8(sourceScaleRoot) ||
        !reader.readU8(sourceScaleType) ||
        !reader.readU8(sourceScaleMode) ||
        !reader.readU8(sourceRootPitchClass) ||
        !reader.readU8(reserved) ||
        reader.position() != size ||
        reserved != CHORD_PRESET_RESERVED ||
        !encodedIntervalsValid(intervals, voiceCount) ||
        basis <= static_cast<uint8_t>(
            StepSequencerChordIntervalBasis::FollowPitchContext
        ) ||
        basis >= static_cast<uint8_t>(
            StepSequencerChordIntervalBasis::Count
        ) ||
        inversion >= voiceCount ||
        voicing >= static_cast<uint8_t>(StepSequencerChordVoicing::Count) ||
        strum < StepSequencerChordSpec::MIN_STRUM ||
        strum > StepSequencerChordSpec::MAX_STRUM ||
        velocityCurve < StepSequencerChordSpec::MIN_VELOCITY_CURVE ||
        velocityCurve > StepSequencerChordSpec::MAX_VELOCITY_CURVE ||
        sourceShapeHint >=
            static_cast<uint8_t>(StepSequencerChordHarmony::Count) ||
        sourceScaleRoot >= 12U ||
        sourceScaleType >
            static_cast<uint8_t>(StepSequencerScaleType::WholeTone) ||
        sourceScaleMode >
            static_cast<uint8_t>(
                StepSequencerScaleConstraintMode::ConstrainDown
            ) ||
        sourceRootPitchClass >= 12U) {
        if (report != nullptr) {
            report->status =
                StepSequencerChordPresetCodecStatus::INVALID_FORMAT;
        }
        return false;
    }

    auto formula = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        voiceCount,
        static_cast<StepSequencerChordVoicing>(voicing),
        inversion,
        static_cast<StepSequencerChordIntervalBasis>(basis)
    );
    formula.setCustomIntervals(intervals);
    formula.strum = strum;
    formula.velocityCurve = velocityCurve;

    StepSequencerChordPreset decoded{};
    decoded.valid = true;
    decoded.formatVersion = prefix.version;
    std::memcpy(
        decoded.technicalId,
        metadata.technicalId,
        sizeof(decoded.technicalId)
    );
    std::memcpy(
        decoded.semanticName,
        metadata.semanticName,
        sizeof(decoded.semanticName)
    );
    decoded.formula = formula;
    decoded.sourceShapeHint =
        static_cast<StepSequencerChordHarmony>(sourceShapeHint);
    decoded.sourceScale = {
        .root = sourceScaleRoot,
        .type = static_cast<StepSequencerScaleType>(sourceScaleType),
        .mode = static_cast<StepSequencerScaleConstraintMode>(
            sourceScaleMode
        ),
    };
    decoded.sourceRootPitchClass = sourceRootPitchClass;
    if (!presetValid(decoded)) {
        if (report != nullptr) {
            report->status =
                StepSequencerChordPresetCodecStatus::INVALID_FORMAT;
        }
        return false;
    }

    out = decoded;
    if (report != nullptr) {
        report->status = StepSequencerChordPresetCodecStatus::OK;
        report->bytesProcessed = size;
    }
    return true;
}

}  // namespace oc::note::sequencer
