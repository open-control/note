#pragma once

#include <array>
#include <cstdint>

#include "StepSequencerGraph.hpp"
#include "StepSequencerRuntimeState.hpp"
#include "StepSequencerVariation.hpp"

namespace oc::note::sequencer {

struct StepSequencerExpandedNote {
    uint32_t localTick = 0;
    uint16_t spanTicks = 1;
    StepSequencerResolvedVariation variation{};
};

struct StepSequencerExpansion {
    std::array<
        StepSequencerExpandedNote,
        StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP
    > notes{};
    uint8_t count = 0;
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
};

}  // namespace oc::note::sequencer
