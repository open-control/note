#pragma once

#include <cstdint>

#include <oc/note/clock/ClockConstants.hpp>

#include "ISequencerOutput.hpp"
#include "NoteScheduler.hpp"
#include "StepSequencerState.hpp"

namespace oc::note::sequencer {

/**
 * @brief Minimal mono-track step sequencer engine (v0)
 *
 * Contract:
 * - tick domain is PPQN=24 and should reset to 0 when playback starts.
 * - engine outputs MIDI while `playing=true`, independent of UI/view state.
 */
class StepSequencerEngine {
public:
    StepSequencerEngine(StepSequencerState& state, ISequencerOutput& output)
        : state_(state)
        , output_(output) {}

    void reset();

    void update(uint32_t tick, bool playing);

    bool isPlaying() const { return playing_; }

private:
    void start_();
    void stop_();
    void advanceToTick_(uint32_t tick);
    void triggerStep_(uint8_t stepIndex, uint32_t stepStartTick, uint8_t ticksPerStep);

    uint8_t ticksPerStep_() const;
    uint8_t patternLength_() const;
    static uint8_t clampChannel_(uint8_t ch);

    StepSequencerState& state_;
    ISequencerOutput& output_;
    NoteScheduler scheduler_;

    bool playing_ = false;
    uint32_t last_tick_ = 0;
    uint32_t next_step_tick_ = 0;
};

}  // namespace oc::note::sequencer
