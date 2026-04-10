#include "StepSequencerState.hpp"

#include <config/PlatformCompat.hpp>

namespace oc::note::sequencer {

FLASHMEM StepSequencerState::StepSequencerState()
    : length{DEFAULT_LENGTH},
      playheadStep{-1},
      stepsPerBeat{DEFAULT_STEPS_PER_BEAT},
      midiChannel{DEFAULT_MIDI_CHANNEL_0BASED},
      enabledMask{},
      probabilityCycleRevision{0} {
    length.setDebugLabel("note.stepSequencer.length");
    playheadStep.setDebugLabel("note.stepSequencer.playheadStep");
    stepsPerBeat.setDebugLabel("note.stepSequencer.stepsPerBeat");
    midiChannel.setDebugLabel("note.stepSequencer.midiChannel");
    enabledMask.setDebugLabel("note.stepSequencer.enabledMask");
    probabilityCycleRevision.setDebugLabel("note.stepSequencer.probabilityCycleRevision");
    reset();
}

FLASHMEM void StepSequencerState::reset() {
    length.set(DEFAULT_LENGTH);
    playheadStep.set(-1);
    stepsPerBeat.set(DEFAULT_STEPS_PER_BEAT);
    midiChannel.set(DEFAULT_MIDI_CHANNEL_0BASED);
    enabledMask.set({});
    probabilityCycleMask = {};
    probabilityCycleIndex = 0;
    probabilityCycleRevision.set(0);

    for (uint8_t i = 0; i < MAX_STEPS; ++i) {
        note[i] = DEFAULT_NOTE;
        velocity[i] = DEFAULT_VELOCITY;
        gate[i] = DEFAULT_GATE_PERCENT;
        nudge[i] = 0;
        probability[i] = DEFAULT_PROBABILITY;
    }
}

}  // namespace oc::note::sequencer
