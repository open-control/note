#pragma once

#include <array>
#include <cstdint>

#include "StepSequencerGraph.hpp"
#include "StepSequencerRuntimeState.hpp"
#include "StepSequencerVariation.hpp"

namespace oc::note::sequencer {

struct StepSequencerExpandedNote {
    uint16_t nodeId = StepSequencerGraphLimits::INVALID_ID;
    uint32_t localTick = 0;
    uint16_t spanTicks = 1;
    StepSequencerResolvedVariation variation{};
    StepSequencerChordSource chordSource = StepSequencerChordSource::Single;
    uint8_t chordVoiceIndex = 0;
    uint8_t chordVoiceCount = 1;
    int16_t chordInterval = 0;
    bool chordIntervalUsesScaleDegrees = false;
};

struct StepSequencerExpansion {
    std::array<
        StepSequencerExpandedNote,
        StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP
    > notes{};
    uint8_t count = 0;
    uint8_t requestedNoteCount = 0;
    bool noteBudgetExceeded = false;
    bool depthLimitReached = false;
};

struct StepSequencerExpansionAnalysis {
    uint8_t emittedNoteCount = 0;
    // Saturates at MAX_EXPANDED_NOTES_PER_ROOT_STEP + 1 ("17+").
    uint8_t requestedNoteCount = 0;
    bool noteBudgetExceeded = false;
    bool depthLimitReached = false;
};

class StepSequencerExpander {
public:
    static StepSequencerExpansion expandRootStep(const StepSequencerRuntimeState& state,
                                                 const StepSequencerGraph& graph,
                                                 uint8_t rootStepIndex,
                                                 uint32_t cycleIndex,
                                                 uint8_t ticksPerStep,
                                                 uint32_t runSeed,
                                                 bool triggered);

    /**
     * Run the exact expansion traversal without retaining expanded notes.
     *
     * This is intended for authoring previews: it reports the same deterministic
     * 16-note budget decision as expandRootStep(). Its temporary storage remains
     * bounded to the engine's 16 retained notes; it never allocates per pattern
     * step or grows with graph size.
     */
    static StepSequencerExpansionAnalysis analyzeRootStep(
        const StepSequencerRuntimeState& state,
        const StepSequencerGraph& graph,
        uint8_t rootStepIndex,
        uint32_t cycleIndex,
        uint8_t ticksPerStep,
        uint32_t runSeed,
        bool triggered
    );
};

}  // namespace oc::note::sequencer
