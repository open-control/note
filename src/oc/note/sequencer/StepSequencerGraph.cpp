#include "StepSequencerGraph.hpp"

#include <config/PlatformCompat.hpp>

namespace oc::note::sequencer {

FLASHMEM StepSequencerGraph::StepSequencerGraph() {
    reset();
}

FLASHMEM void StepSequencerGraph::reset() {
    enabled = false;
    rootSequenceId = StepSequencerGraphLimits::INVALID_ID;
    stepNodeCount = 0;
    sequenceCount = 0;
    cycleSetCount = 0;

    for (auto& node : stepNodes) {
        node = StepSequencerStepNode{};
    }
    for (auto& sequence : sequences) {
        sequence = StepSequencerSequence{};
    }
    for (auto& cycleSet : cycleSets) {
        cycleSet = StepSequencerCycleStateSet{};
    }
}

}  // namespace oc::note::sequencer
