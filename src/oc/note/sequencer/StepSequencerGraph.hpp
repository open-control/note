#pragma once

#include <array>
#include <cstdint>

#include "StepSequencerChord.hpp"
#include "StepSequencerVariation.hpp"

namespace oc::note::sequencer {

struct StepSequencerGraphLimits {
    static constexpr uint8_t MAX_DEPTH = 4;
    static constexpr uint16_t MAX_STEP_NODES = 512;
    static constexpr uint8_t MAX_SEQUENCES = 32;
    static constexpr uint8_t MAX_CYCLE_SETS = 64;
    static constexpr uint8_t MAX_CYCLE_STATES_PER_SET = 16;
    static constexpr uint8_t MAX_EXPANDED_NOTES_PER_ROOT_STEP = 16;
    static constexpr uint16_t INVALID_ID = 0xFFFFU;
};

enum class StepSequencerSequenceKind : uint8_t {
    RootPattern = 0,
    MicroSequence,
};

struct StepSequencerSequence {
    StepSequencerSequenceKind kind = StepSequencerSequenceKind::MicroSequence;
    uint16_t firstStepNode = StepSequencerGraphLimits::INVALID_ID;
    uint8_t length = 0;
    int8_t offset = 0;
};

struct StepSequencerCycleStateSet {
    uint16_t firstStateNode = StepSequencerGraphLimits::INVALID_ID;
    uint8_t length = 0;
    int8_t offset = 0;
};

enum StepSequencerStepNodeFlags : uint16_t {
    STEP_NODE_ENABLED_OVERRIDE = 1U << 0,
    STEP_NODE_ENABLED_VALUE = 1U << 1,
    STEP_NODE_NOTE_OFFSET = 1U << 2,
    STEP_NODE_VELOCITY_OFFSET = 1U << 3,
    STEP_NODE_GATE_OFFSET = 1U << 4,
    STEP_NODE_NUDGE_OFFSET = 1U << 5,
    STEP_NODE_PROBABILITY_OFFSET = 1U << 6,
    STEP_NODE_CHILD_SEQUENCE = 1U << 7,
    STEP_NODE_CYCLE_SET = 1U << 8,
    STEP_NODE_CHORD_MODE = 1U << 9,
    STEP_NODE_CHORD_LOCAL = 1U << 10,
};

inline constexpr uint16_t STEP_NODE_ALL_FLAGS =
    STEP_NODE_ENABLED_OVERRIDE |
    STEP_NODE_ENABLED_VALUE |
    STEP_NODE_NOTE_OFFSET |
    STEP_NODE_VELOCITY_OFFSET |
    STEP_NODE_GATE_OFFSET |
    STEP_NODE_NUDGE_OFFSET |
    STEP_NODE_PROBABILITY_OFFSET |
    STEP_NODE_CHILD_SEQUENCE |
    STEP_NODE_CYCLE_SET |
    STEP_NODE_CHORD_MODE |
    STEP_NODE_CHORD_LOCAL;

struct StepSequencerStepNode {
    uint16_t flags = 0;
    int16_t velocityOffset = 0;
    int16_t gateOffset = 0;
    int16_t probabilityOffset = 0;
    uint16_t childSequenceId = StepSequencerGraphLimits::INVALID_ID;
    uint16_t cycleSetId = StepSequencerGraphLimits::INVALID_ID;
    StepSequencerVariationRanges localVariation{};
    StepSequencerChordSpec chordSpec{};
    StepSequencerChordMode chordMode = StepSequencerChordMode::Single;
    int8_t noteOffset = 0;
    int8_t nudgeOffset = 0;

    bool has(uint16_t flag) const {
        return (flags & flag) != 0;
    }
};

static_assert(sizeof(StepSequencerStepNode) == 28, "Step nodes must not grow");

struct StepSequencerGraph {
    bool enabled;
    uint16_t rootSequenceId;
    uint16_t stepNodeCount;
    uint8_t sequenceCount;
    uint8_t cycleSetCount;
    std::array<StepSequencerStepNode, StepSequencerGraphLimits::MAX_STEP_NODES> stepNodes;
    std::array<StepSequencerSequence, StepSequencerGraphLimits::MAX_SEQUENCES> sequences;
    std::array<StepSequencerCycleStateSet, StepSequencerGraphLimits::MAX_CYCLE_SETS> cycleSets;

    StepSequencerGraph();

    void reset();

    const StepSequencerSequence* sequence(uint16_t id) const {
        if (id >= sequenceCount || id >= sequences.size()) return nullptr;
        const auto& seq = sequences[id];
        if (seq.length == 0) return nullptr;
        if (seq.firstStepNode == StepSequencerGraphLimits::INVALID_ID) return nullptr;
        if (seq.firstStepNode >= stepNodeCount) return nullptr;
        if (static_cast<uint32_t>(seq.firstStepNode) + seq.length > stepNodeCount) return nullptr;
        return &seq;
    }

    const StepSequencerStepNode* stepNode(uint16_t id) const {
        if (id >= stepNodeCount || id >= stepNodes.size()) return nullptr;
        return &stepNodes[id];
    }

    const StepSequencerCycleStateSet* cycleSet(uint16_t id) const {
        if (id >= cycleSetCount || id >= cycleSets.size()) return nullptr;
        const auto& set = cycleSets[id];
        if (set.length == 0 || set.length > StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET) {
            return nullptr;
        }
        if (set.firstStateNode == StepSequencerGraphLimits::INVALID_ID) return nullptr;
        if (set.firstStateNode >= stepNodeCount) return nullptr;
        if (static_cast<uint32_t>(set.firstStateNode) + set.length > stepNodeCount) return nullptr;
        return &set;
    }
};

static_assert(sizeof(StepSequencerGraph) == 14792, "Sequencer graph footprint changed");

}  // namespace oc::note::sequencer
