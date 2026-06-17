#include "StepSequencerExpander.hpp"

#include <algorithm>

namespace oc::note::sequencer {

namespace {

struct ResolvedStep {
    uint16_t nodeId = StepSequencerGraphLimits::INVALID_ID;
    bool enabled = true;
    StepSequencerStepValues values{};
    uint8_t probability = StepSequencerRuntimeState::DEFAULT_PROBABILITY;
    StepSequencerVariationRanges localVariation{};
    uint16_t childSequenceId = StepSequencerGraphLimits::INVALID_ID;
    uint16_t cycleSetId = StepSequencerGraphLimits::INVALID_ID;
};

struct CycleResolution {
    ResolvedStep step{};
    uint32_t childLocalCycleIndex = 0;
    uint32_t variationIdentity = 0;
};

uint8_t clampMidi7Offset(uint8_t base, int16_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

uint8_t applyNoteOffset(uint8_t base, int8_t offset, StepSequencerScaleSettings scaleSettings) {
    if (offset == 0) return base;
    scaleSettings.clamp();
    if (scaleSettings.isConstrained()) {
        return moveByScaleDegrees(base, offset, scaleSettings);
    }
    return clampMidi7Offset(base, offset);
}

uint16_t clampGateOffset(uint16_t base, int16_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    if (value < 0) return 0;
    if (value > static_cast<int>(StepSequencerRuntimeState::MAX_GATE_PERCENT)) {
        return StepSequencerRuntimeState::MAX_GATE_PERCENT;
    }
    return static_cast<uint16_t>(value);
}

int8_t clampNudgeOffset(int8_t base, int8_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    if (value < -50) return -50;
    if (value > 50) return 50;
    return static_cast<int8_t>(value);
}

uint8_t clampProbabilityOffset(uint8_t base, int16_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    if (value < 0) return 0;
    if (value > 100) return 100;
    return static_cast<uint8_t>(value);
}

uint8_t normalizeSequenceIndex(uint8_t playIndex, int8_t offset, uint8_t length) {
    if (length == 0) return 0;
    int value = static_cast<int>(playIndex) - static_cast<int>(offset);
    const int len = static_cast<int>(length);
    value %= len;
    if (value < 0) value += len;
    return static_cast<uint8_t>(value);
}

uint32_t boundaryTick(uint8_t playIndex, uint16_t spanTicks, uint8_t length) {
    if (length == 0) return 0;
    return (static_cast<uint32_t>(playIndex) * static_cast<uint32_t>(spanTicks)) /
           static_cast<uint32_t>(length);
}

uint16_t effectiveGateSpan(uint16_t spanTicks, uint16_t gatePercent) {
    const uint32_t scaled =
        (static_cast<uint32_t>(spanTicks) * static_cast<uint32_t>(gatePercent)) / 100U;
    if (scaled == 0) return 1;
    if (scaled > UINT16_MAX) return UINT16_MAX;
    return static_cast<uint16_t>(scaled);
}

StepSequencerVariationRanges combineVariationRanges(StepSequencerVariationRanges global,
                                                    StepSequencerVariationRanges local) {
    StepSequencerVariationRanges out{
        .pitchSemitones = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.pitchSemitones) + local.pitchSemitones,
                StepSequencerVariationRanges::MAX_PITCH_SEMITONES
            )
        ),
        .velocity = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.velocity) + local.velocity,
                StepSequencerVariationRanges::MAX_VELOCITY
            )
        ),
        .gatePercent = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.gatePercent) + local.gatePercent,
                StepSequencerVariationRanges::MAX_GATE_PERCENT
            )
        ),
        .nudge = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.nudge) + local.nudge,
                StepSequencerVariationRanges::MAX_NUDGE
            )
        ),
    };
    return out;
}

bool hasAnyVariationRange(const StepSequencerVariationRanges& ranges) {
    return ranges.pitchSemitones > 0 ||
           ranges.velocity > 0 ||
           ranges.gatePercent > 0 ||
           ranges.nudge > 0;
}

uint32_t mixVariationIdentity(uint32_t parent,
                              uint32_t kindSalt,
                              uint8_t index,
                              uint8_t depth) {
    uint32_t x = parent ^ (kindSalt * 2654435761u);
    x ^= static_cast<uint32_t>(index) * 2246822519u;
    x ^= static_cast<uint32_t>(depth) * 3266489917u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

uint32_t graphProbabilityHash(uint32_t runSeed,
                              uint32_t cycleIndex,
                              uint8_t rootStepIndex,
                              uint8_t depth,
                              uint8_t ordinal) {
    uint32_t x = runSeed * 747796405u;
    x ^= cycleIndex * 2891336453u;
    x ^= static_cast<uint32_t>(rootStepIndex) * 277803737u;
    x ^= static_cast<uint32_t>(depth) * 1597334677u;
    x ^= static_cast<uint32_t>(ordinal) * 2246822519u;
    x ^= 0x9E3779B9u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

bool probabilityAllows(const ResolvedStep& step,
                       uint32_t runSeed,
                       uint32_t cycleIndex,
                       uint8_t rootStepIndex,
                       uint8_t depth,
                       uint8_t ordinal) {
    if (step.probability >= 100U) return true;
    if (step.probability == 0U) return false;
    return (graphProbabilityHash(runSeed, cycleIndex, rootStepIndex, depth, ordinal) % 100U) <
           step.probability;
}

ResolvedStep applyNode(const ResolvedStep& parent,
                       const StepSequencerStepNode& node,
                       uint16_t nodeId,
                       StepSequencerScaleSettings scaleSettings) {
    ResolvedStep out = parent;
    out.nodeId = nodeId;

    if (node.has(STEP_NODE_ENABLED_OVERRIDE)) {
        out.enabled = node.has(STEP_NODE_ENABLED_VALUE);
    }
    if (node.has(STEP_NODE_NOTE_OFFSET)) {
        out.values.note = applyNoteOffset(out.values.note, node.noteOffset, scaleSettings);
    }
    if (node.has(STEP_NODE_VELOCITY_OFFSET)) {
        out.values.velocity = clampMidi7Offset(out.values.velocity, node.velocityOffset);
    }
    if (node.has(STEP_NODE_GATE_OFFSET)) {
        out.values.gate = clampGateOffset(out.values.gate, node.gateOffset);
    }
    if (node.has(STEP_NODE_NUDGE_OFFSET)) {
        out.values.nudge = clampNudgeOffset(out.values.nudge, node.nudgeOffset);
    }
    if (node.has(STEP_NODE_PROBABILITY_OFFSET)) {
        out.probability = clampProbabilityOffset(out.probability, node.probabilityOffset);
    }
    out.localVariation = node.localVariation;
    out.localVariation.clamp();
    if (node.has(STEP_NODE_CHILD_SEQUENCE)) {
        out.childSequenceId = node.childSequenceId;
    }
    if (node.has(STEP_NODE_CYCLE_SET)) {
        out.cycleSetId = node.cycleSetId;
    }

    return out;
}

bool ownsChildContent(const StepSequencerStepNode& node) {
    return node.has(STEP_NODE_CHILD_SEQUENCE) || node.has(STEP_NODE_CYCLE_SET);
}

ResolvedStep applyLocalVariationContext(const ResolvedStep& step,
                                        StepSequencerScaleSettings scaleSettings,
                                        uint32_t runSeed,
                                        uint32_t cycleIndex,
                                        uint8_t rootStepIndex,
                                        uint32_t variationIdentity) {
    if (!hasAnyVariationRange(step.localVariation)) return step;

    ResolvedStep out = step;
    const auto variation = resolveStepVariation(
        out.values,
        out.localVariation,
        scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        runSeed,
        cycleIndex,
        rootStepIndex,
        true,
        variationIdentity
    );
    out.values = variation.resolved;
    out.localVariation = {};
    return out;
}

CycleResolution applyCycleStates(const StepSequencerGraph& graph,
                                 const ResolvedStep& parent,
                                 uint32_t localCycleIndex,
                                 uint8_t depth,
                                 uint32_t variationIdentity,
                                 uint32_t runSeed,
                                 uint32_t cycleIndex,
                                 uint8_t rootStepIndex,
                                 StepSequencerScaleSettings scaleSettings,
                                 StepSequencerExpansion& out) {
    CycleResolution result{
        .step = parent,
        .childLocalCycleIndex = localCycleIndex,
        .variationIdentity = variationIdentity,
    };

    uint32_t cycleCursor = localCycleIndex;
    uint8_t appliedDepth = 0;
    while (result.step.cycleSetId != StepSequencerGraphLimits::INVALID_ID) {
        if (static_cast<uint16_t>(depth) + appliedDepth >= StepSequencerGraphLimits::MAX_DEPTH) {
            out.depthLimitReached = true;
            result.step.cycleSetId = StepSequencerGraphLimits::INVALID_ID;
            break;
        }

        const auto* set = graph.cycleSet(result.step.cycleSetId);
        if (set == nullptr || set->length == 0) {
            result.step.cycleSetId = StepSequencerGraphLimits::INVALID_ID;
            break;
        }

        const uint8_t stateOffset = normalizeSequenceIndex(
            static_cast<uint8_t>(cycleCursor % set->length),
            set->offset,
            set->length
        );
        const auto stateNodeId = static_cast<uint16_t>(set->firstStateNode + stateOffset);
        const auto* stateNode = graph.stepNode(stateNodeId);
        if (stateNode == nullptr) {
            result.step.cycleSetId = StepSequencerGraphLimits::INVALID_ID;
            break;
        }

        const uint32_t ownerActivationIndex = cycleCursor / set->length;
        ResolvedStep stateParent = result.step;
        stateParent.cycleSetId = StepSequencerGraphLimits::INVALID_ID;
        if (ownsChildContent(*stateNode)) {
            // State-owned child content is a local exception: it replaces same-level
            // default content and advances from this state's activation count.
            stateParent.childSequenceId = StepSequencerGraphLimits::INVALID_ID;
            result.childLocalCycleIndex = ownerActivationIndex;
        }

        const uint32_t stateVariationIdentity = mixVariationIdentity(
            result.variationIdentity,
            0x4359434Cu,
            stateOffset,
            static_cast<uint8_t>(depth + appliedDepth)
        );
        result.step = applyNode(stateParent, *stateNode, stateNodeId, scaleSettings);
        result.variationIdentity = stateVariationIdentity;
        if (result.step.cycleSetId != StepSequencerGraphLimits::INVALID_ID) {
            result.step = applyLocalVariationContext(
                result.step,
                scaleSettings,
                runSeed,
                cycleIndex,
                rootStepIndex,
                result.variationIdentity
            );
        }
        cycleCursor = ownerActivationIndex;
        ++appliedDepth;
    }

    return result;
}

bool appendNote(StepSequencerExpansion& out,
                const ResolvedStep& step,
                uint32_t localTick,
                uint16_t spanTicks,
                StepSequencerVariationRanges ranges,
                StepSequencerScaleSettings scaleSettings,
                uint32_t runSeed,
                uint32_t cycleIndex,
                uint8_t rootStepIndex,
                uint32_t variationIdentity,
                bool triggered) {
    if (out.count >= out.notes.size()) {
        out.noteBudgetExceeded = true;
        return false;
    }

    auto& note = out.notes[out.count++];
    note.nodeId = step.nodeId;
    note.localTick = localTick;
    note.spanTicks = std::max<uint16_t>(spanTicks, 1U);
    note.variation = resolveStepVariation(
        step.values,
        ranges,
        scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        runSeed,
        cycleIndex,
        rootStepIndex,
        triggered,
        variationIdentity
    );
    return true;
}

void expandStep(const StepSequencerGraph& graph,
                const ResolvedStep& input,
                uint32_t localTick,
                uint16_t spanTicks,
                uint8_t depth,
                StepSequencerVariationRanges ranges,
                StepSequencerScaleSettings scaleSettings,
                uint32_t runSeed,
                uint32_t cycleIndex,
                uint32_t localCycleIndex,
                uint8_t rootStepIndex,
                uint8_t ordinal,
                uint32_t variationIdentity,
                StepSequencerExpansion& out) {
    if (out.noteBudgetExceeded) return;

    ResolvedStep contextInput = input;
    if (contextInput.cycleSetId != StepSequencerGraphLimits::INVALID_ID) {
        contextInput = applyLocalVariationContext(
            contextInput,
            scaleSettings,
            runSeed,
            cycleIndex,
            rootStepIndex,
            variationIdentity
        );
    }

    const CycleResolution cycleResolution =
        applyCycleStates(
            graph,
            contextInput,
            localCycleIndex,
            depth,
            variationIdentity,
            runSeed,
            cycleIndex,
            rootStepIndex,
            scaleSettings,
            out
        );
    ResolvedStep step = cycleResolution.step;
    if (!step.enabled || step.values.gate == 0) {
        return;
    }

    if (!probabilityAllows(step, runSeed, cycleIndex, rootStepIndex, depth, ordinal)) {
        return;
    }

    const bool canDescend = depth + 1U < StepSequencerGraphLimits::MAX_DEPTH;
    const auto* child = graph.sequence(step.childSequenceId);
    if (child != nullptr && child->length > 0) {
        if (!canDescend) {
            out.depthLimitReached = true;
            appendNote(
                out,
                step,
                localTick,
                spanTicks,
                combineVariationRanges(ranges, step.localVariation),
                scaleSettings,
                runSeed,
                cycleIndex,
                rootStepIndex,
                cycleResolution.variationIdentity,
                true
            );
            return;
        }

        step = applyLocalVariationContext(
            step,
            scaleSettings,
            runSeed,
            cycleIndex,
            rootStepIndex,
            cycleResolution.variationIdentity
        );

        const uint16_t childTotalSpan = effectiveGateSpan(spanTicks, step.values.gate);
        for (uint8_t i = 0; i < child->length; ++i) {
            const uint8_t sourceIndex = normalizeSequenceIndex(i, child->offset, child->length);
            const auto* childNode = graph.stepNode(
                static_cast<uint16_t>(child->firstStepNode + sourceIndex)
            );
            if (childNode == nullptr) continue;
            ResolvedStep childBase = step;
            childBase.localVariation = {};
            childBase.childSequenceId = StepSequencerGraphLimits::INVALID_ID;
            childBase.cycleSetId = StepSequencerGraphLimits::INVALID_ID;
            childBase.values.gate = StepSequencerRuntimeState::DEFAULT_GATE_PERCENT;
            const auto childNodeId = static_cast<uint16_t>(child->firstStepNode + sourceIndex);
            const ResolvedStep childStep = applyNode(
                childBase,
                *childNode,
                childNodeId,
                scaleSettings
            );
            const uint32_t childStart = boundaryTick(i, childTotalSpan, child->length);
            const uint32_t childEnd = boundaryTick(
                static_cast<uint8_t>(i + 1U),
                childTotalSpan,
                child->length
            );
            const uint16_t childSpan =
                static_cast<uint16_t>(std::max<uint32_t>(childEnd - childStart, 1U));
            expandStep(
                graph,
                childStep,
                localTick + childStart,
                childSpan,
                static_cast<uint8_t>(depth + 1U),
                ranges,
                scaleSettings,
                runSeed,
                cycleIndex,
                cycleResolution.childLocalCycleIndex,
                rootStepIndex,
                i,
                mixVariationIdentity(
                    cycleResolution.variationIdentity,
                    0x4D494352u,
                    sourceIndex,
                    static_cast<uint8_t>(depth + 1U)
                ),
                out
            );
            if (out.noteBudgetExceeded) return;
        }
        return;
    }

    appendNote(
        out,
        step,
        localTick,
        spanTicks,
        combineVariationRanges(ranges, step.localVariation),
        scaleSettings,
        runSeed,
        cycleIndex,
        rootStepIndex,
        cycleResolution.variationIdentity,
        true
    );
}

}  // namespace

StepSequencerExpansion StepSequencerExpander::expandRootStep(const StepSequencerRuntimeState& state,
                                                             const StepSequencerGraph& graph,
                                                             uint8_t rootStepIndex,
                                                             uint32_t cycleIndex,
                                                             uint8_t ticksPerStep,
                                                             uint32_t runSeed,
                                                             bool triggered) {
    StepSequencerExpansion out{};
    if (!graph.enabled || !triggered || rootStepIndex >= StepSequencerRuntimeState::MAX_STEPS) {
        return out;
    }

    const auto* root = graph.sequence(graph.rootSequenceId);
    if (root == nullptr || rootStepIndex >= root->length) {
        return out;
    }

    const uint8_t sourceRootIndex =
        normalizeSequenceIndex(rootStepIndex, root->offset, root->length);
    if (sourceRootIndex >= StepSequencerRuntimeState::MAX_STEPS) {
        return out;
    }

    ResolvedStep base{
        .nodeId = static_cast<uint16_t>(root->firstStepNode + sourceRootIndex),
        .enabled = state.enabledMask.test(sourceRootIndex),
        .values = StepSequencerStepValues{
            .note = state.note[sourceRootIndex],
            .velocity = state.velocity[sourceRootIndex],
            .gate = state.gate[sourceRootIndex],
            .nudge = state.nudge[sourceRootIndex],
        },
        .probability = StepSequencerRuntimeState::clampProbability(state.probability[sourceRootIndex]),
        .localVariation = {},
        .childSequenceId = StepSequencerGraphLimits::INVALID_ID,
        .cycleSetId = StepSequencerGraphLimits::INVALID_ID,
    };

    const auto* rootNode = graph.stepNode(static_cast<uint16_t>(root->firstStepNode + sourceRootIndex));
    if (rootNode != nullptr) {
        base = applyNode(
            base,
            *rootNode,
            static_cast<uint16_t>(root->firstStepNode + sourceRootIndex),
            state.scaleSettings
        );
    }

    expandStep(
        graph,
        base,
        0,
        ticksPerStep,
        0,
        state.variationRanges,
        state.scaleSettings,
        runSeed,
        cycleIndex,
        cycleIndex,
        rootStepIndex,
        0,
        sourceRootIndex,
        out
    );
    return out;
}

}  // namespace oc::note::sequencer
