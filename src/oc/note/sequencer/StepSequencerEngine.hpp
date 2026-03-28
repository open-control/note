#pragma once

#include <array>
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
    static constexpr size_t CYCLE_MASK_CACHE_SIZE = 4;

    void start_();
    void stop_();
    void advanceToTick_(uint32_t tick);
    void primeSchedule_();
    void scheduleStep_(uint32_t stepNumber, uint8_t ticksPerStep);
    void publishCycleMask_(uint32_t cycleIndex, uint8_t len);
    void clearCycleMaskCache_();

    uint8_t ticksPerStep_() const;
    uint8_t patternLength_() const;
    static uint8_t clampChannel_(uint8_t ch);
    static int32_t nudgeTickOffset_(int8_t nudge, uint8_t ticksPerStep);
    uint64_t resolveCycleMask_(uint32_t cycleIndex, uint8_t len) const;
    uint64_t maskForCycle_(uint32_t cycleIndex, uint8_t len);
    bool shouldTriggerStep_(uint8_t stepIndex, uint32_t stepNumber, uint8_t len);
    static uint32_t probabilityHash_(uint32_t runSeed, uint32_t cycleIndex, uint8_t stepIndex);

    StepSequencerState& state_;
    ISequencerOutput& output_;
    NoteScheduler scheduler_;

    bool playing_ = false;
    uint32_t last_tick_ = 0;
    uint32_t next_step_tick_ = 0;
    uint32_t next_scheduled_step_number_ = 0;
    uint32_t run_seed_ = 0;
    uint32_t published_cycle_index_ = UINT32_MAX;
    std::array<uint32_t, CYCLE_MASK_CACHE_SIZE> cached_cycle_indices_{};
    std::array<uint64_t, CYCLE_MASK_CACHE_SIZE> cached_cycle_masks_{};
    size_t next_cycle_cache_slot_ = 0;
    uint64_t last_enabled_mask_ = 0;
};

}  // namespace oc::note::sequencer