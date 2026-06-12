#include "StepSequencerExpander.hpp"

#include <algorithm>

namespace oc::note::sequencer {

namespace {

struct ResolvedStep {
    bool enabled = true;
    StepSequencerStepValues values{};
    uint8_t probability = StepSequencerRuntimeState::DEFAULT_PROBABILITY;
    uint16_t childSequenceId = StepSequencerGraphLimits::INVALID_ID;
    uint16_t cycleSetId = StepSequencerGraphLimits::INVALID_ID;
};

struct CycleResolution {
    ResolvedStep step{};
    uint32_t childLocalCycleIndex = 0;
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
                       StepSequencerScaleSettings scaleSettings) {
    ResolvedStep out = parent;

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

CycleResolution applyCycleStates(const StepSequencerGraph& graph,
                                 const ResolvedStep& parent,
                                 uint32_t localCycleIndex,
                                 uint8_t depth,
                                 StepSequencerScaleSettings scaleSettings,
                                 StepSequencerExpansion& out) {
    CycleResolution result{
        .step = parent,
        .childLocalCycleIndex = localCycleIndex,
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
        const auto* stateNode =
            graph.stepNode(static_cast<uint16_t>(set->firstStateNode + stateOffset));
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

        result.step = applyNode(stateParent, *stateNode, scaleSettings);
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
                bool triggered) {
    if (out.count >= out.notes.size()) {
        out.noteBudgetExceeded = true;
        return false;
    }

    auto& note = out.notes[out.count++];
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
        triggered
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
                StepSequencerExpansion& out) {
    if (out.noteBudgetExceeded) return;

    const CycleResolution cycleResolution =
        applyCycleStates(graph, input, localCycleIndex, depth, scaleSettings, out);
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
                ranges,
                scaleSettings,
                runSeed,
                cycleIndex,
                rootStepIndex,
                true
            );
            return;
        }

        const uint16_t childTotalSpan = effectiveGateSpan(spanTicks, step.values.gate);
        for (uint8_t i = 0; i < child->length; ++i) {
            const uint8_t sourceIndex = normalizeSequenceIndex(i, child->offset, child->length);
            const auto* childNode = graph.stepNode(
                static_cast<uint16_t>(child->firstStepNode + sourceIndex)
            );
            if (childNode == nullptr) continue;
            ResolvedStep childBase = step;
            childBase.childSequenceId = StepSequencerGraphLimits::INVALID_ID;
            childBase.cycleSetId = StepSequencerGraphLimits::INVALID_ID;
            childBase.values.gate = StepSequencerRuntimeState::DEFAULT_GATE_PERCENT;
            const ResolvedStep childStep = applyNode(childBase, *childNode, scaleSettings);
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
        ranges,
        scaleSettings,
        runSeed,
        cycleIndex,
        rootStepIndex,
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
        .enabled = state.enabledMask.test(sourceRootIndex),
        .values = StepSequencerStepValues{
            .note = state.note[sourceRootIndex],
            .velocity = state.velocity[sourceRootIndex],
            .gate = state.gate[sourceRootIndex],
            .nudge = state.nudge[sourceRootIndex],
        },
        .probability = StepSequencerRuntimeState::clampProbability(state.probability[sourceRootIndex]),
        .childSequenceId = StepSequencerGraphLimits::INVALID_ID,
        .cycleSetId = StepSequencerGraphLimits::INVALID_ID,
    };

    const auto* rootNode = graph.stepNode(static_cast<uint16_t>(root->firstStepNode + sourceRootIndex));
    if (rootNode != nullptr) {
        base = applyNode(base, *rootNode, state.scaleSettings);
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
        out
    );
    return out;
}

}  // namespace oc::note::sequencer
