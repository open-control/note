#pragma once

#include <array>
#include <cstdint>

#include <oc/note/clock/ClockConstants.hpp>

#include "NoteScheduler.hpp"
#include "SequencerEvent.hpp"
#include "StepSequencerPlaybackRegion.hpp"
#include "StepSequencerRuntimeState.hpp"

namespace oc::note::sequencer {

struct StepSequencerExpandedNote;
struct StepSequencerGraph;

class StepSequencerEngine {
public:
    StepSequencerEngine(StepSequencerRuntimeState& state, ISequencerEventSink& eventSink)
        : state_(state)
        , event_sink_(eventSink) {}

    void reset();
    void resyncToTick(uint32_t tick);

    void update(uint32_t tick, bool playing);

    /**
     * Advance scheduling and event emission through `emissionHorizonTick` while
     * publishing playhead and variation telemetry strictly for `tick`.
     *
     * While playing, both ticks must be monotonic and the emission horizon must
     * not precede the musical tick. A timing-context change (grid, swing,
     * pattern nudge, or playback region) also requires an explicit
     * `resyncToTick(tick)` before this method can advance again. Rejected calls
     * have no observable effect.
     *
     * `update(tick, playing)` is the single-tick convenience API and is
     * equivalent to advancing with no future emission horizon. Clients must
     * not mix the two APIs within one playback session.
     */
    [[nodiscard]] bool updateWithEmissionHorizon(uint32_t tick,
                                                 uint32_t emissionHorizonTick,
                                                 bool playing);
    void setGraph(const StepSequencerGraph* graph) { graph_ = graph; }

    /**
     * Atomically select an explicit region for every playback projection.
     * Its Content Length becomes authoritative until the override is cleared.
     * Invalid regions are rejected without changing the active configuration.
     */
    [[nodiscard]] bool setPlaybackRegion(const StepSequencerPlaybackRegion& region);

    /** Return to the default full [0, state.length) region. */
    void useStateLengthPlaybackRegion();

    /** Return the exact region currently consumed by the engine. */
    StepSequencerPlaybackRegion playbackRegion() const;

    bool isPlaying() const { return playing_; }

private:
    static constexpr size_t CYCLE_MASK_CACHE_SIZE = 4;

    void start_();
    void stop_();
    void prepareFromTick_(uint32_t tick);
    bool update_(uint32_t tick,
                 uint32_t emissionHorizonTick,
                 bool playing,
                 bool requireExplicitResync);
    void advanceEmissionToTick_(uint32_t tick);
    void publishTelemetryAtTick_(uint32_t tick, bool force);
    void primeSchedule_();
    void scheduleStep_(uint32_t playbackOrdinal, uint8_t ticksPerStep);
    uint32_t timedStepStartTick_(uint32_t playbackOrdinal,
                                 uint8_t stepIndex,
                                 uint8_t ticksPerStep) const;
    void scheduleExpandedNote_(uint32_t stepStartTick,
                               const StepSequencerExpandedNote& note);
    StepSequencerResolvedVariation resolveVariation_(uint8_t stepIndex,
                                                     uint32_t cycleIndex,
                                                     bool triggered) const;
    void publishResolvedVariation_(uint8_t stepIndex, uint32_t cycleIndex, bool triggered);
    void publishExpandedVariationTelemetry_(uint8_t stepIndex,
                                            uint32_t cycleIndex,
                                            bool triggered);
    void publishPlayheadTickPosition_(uint32_t tick, uint8_t ticksPerStep);
    void publishPlayheadPosition_(const StepSequencerPlaybackTickPosition& position);
    void publishCycleMask_(uint32_t cycleIndex, uint8_t len);
    void publishCycleVariationTelemetry_(uint32_t cycleIndex,
                                         uint8_t len,
                                         const StepBitMask128& triggeredMask);
    bool timingContextChanged_(uint8_t ticksPerStep) const;
    void rememberTimingContext_(uint8_t ticksPerStep);
    void resyncTimingContext_(uint32_t tick);
    void clearCycleMaskCache_();
    bool emitAllNotesOff_(uint32_t tick);
    bool processDueEvents_(uint32_t tick);

    uint8_t ticksPerStep_() const;
    uint8_t patternLength_() const;
    StepSequencerPlaybackRegion activePlaybackRegion_() const;
    bool resolvePlaybackOrdinal_(uint32_t ordinal,
                                 StepSequencerPlaybackPosition& output) const;
    bool resolvePlaybackTick_(uint32_t tick,
                              uint16_t ticksPerStep,
                              StepSequencerPlaybackTickPosition& output) const;
    static bool samePlaybackRegion_(const StepSequencerPlaybackRegion& left,
                                    const StepSequencerPlaybackRegion& right);
    static uint8_t clampChannel_(uint8_t ch);
    static uint8_t clampSwingPercent_(uint8_t swingPercent);
    static uint32_t swingTickOffset_(uint8_t stepIndex,
                                     uint8_t ticksPerStep,
                                     uint8_t swingPercent);
    static int32_t nudgeTickOffset_(int8_t nudge, uint8_t ticksPerStep);
    StepBitMask128 resolveCycleMask_(uint32_t cycleIndex, uint8_t len) const;
    StepBitMask128 maskForCycle_(uint32_t cycleIndex, uint8_t len);
    bool shouldTriggerStep_(const StepSequencerPlaybackPosition& position, uint8_t len);
    static uint32_t probabilityHash_(uint32_t runSeed, uint32_t cycleIndex, uint8_t stepIndex);

    StepSequencerRuntimeState& state_;
    ISequencerEventSink& event_sink_;
    const StepSequencerGraph* graph_ = nullptr;
    NoteScheduler scheduler_;

    bool playing_ = false;
    uint32_t last_tick_ = 0;
    uint32_t emission_horizon_tick_ = 0;
    uint32_t next_step_tick_ = 0;
    uint32_t next_scheduled_playback_ordinal_ = 0;
    uint32_t run_seed_ = 0;
    uint32_t published_cycle_index_ = UINT32_MAX;
    uint32_t published_playback_ordinal_ = UINT32_MAX;
    bool emission_horizon_valid_ = false;
    bool cycle_variation_telemetry_published_ = false;
    bool timing_context_valid_ = false;
    uint8_t last_ticks_per_step_ = 0;
    uint8_t last_effective_swing_percent_ = 0;
    int8_t last_pattern_nudge_percent_ = 0;
    StepSequencerPlaybackRegion last_playback_region_{};
    StepSequencerPlaybackRegion explicit_playback_region_{0, 0, 0, 0};
    std::array<uint32_t, CYCLE_MASK_CACHE_SIZE> cached_cycle_indices_{};
    std::array<StepBitMask128, CYCLE_MASK_CACHE_SIZE> cached_cycle_masks_{};
    size_t next_cycle_cache_slot_ = 0;
    StepBitMask128 last_enabled_mask_{};
};

}  // namespace oc::note::sequencer
