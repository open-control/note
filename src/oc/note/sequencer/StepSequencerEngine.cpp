#include "StepSequencerEngine.hpp"

#include <algorithm>

#include "StepSequencerExpander.hpp"

namespace oc::note::sequencer {

void StepSequencerEngine::clearCycleMaskCache_() {
    cached_cycle_indices_.fill(UINT32_MAX);
    cached_cycle_masks_.fill({});
    next_cycle_cache_slot_ = 0;
}

void StepSequencerEngine::reset() {
    stop_();
    scheduler_.clear();
    last_tick_ = 0;
    next_step_tick_ = 0;
    next_scheduled_step_number_ = 0;
    published_cycle_index_ = UINT32_MAX;
    cycle_variation_telemetry_published_ = false;
    timing_context_valid_ = false;
    clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask;
    state_.probabilityCycleMask = {};
    state_.probabilityCycleIndex = 0;
    state_.probabilityCycleRevision += 1U;
    state_.playheadStepTickOffset = 0;
    state_.playheadStepTicks = ticksPerStep_();
    state_.lastResolvedVariation = {};
    state_.cycleVariationTelemetry.reset();
    state_.expandedVariationTelemetry.reset();
    state_.variationTelemetryRevision += 1U;
}

void StepSequencerEngine::resyncToTick(uint32_t tick) {
    scheduler_.clear();
    emitAllNotesOff_(tick);
    playing_ = true;
    prepareFromTick_(tick);
}

uint8_t StepSequencerEngine::clampChannel_(uint8_t ch) {
    return (ch > 15) ? 15 : ch;
}

uint8_t StepSequencerEngine::clampSwingPercent_(uint8_t swingPercent) {
    return StepSequencerRuntimeState::clampSwingPercent(swingPercent);
}

uint8_t StepSequencerEngine::patternLength_() const {
    const uint8_t len = state_.patternLength();
    return len;
}

uint8_t StepSequencerEngine::ticksPerStep_() const {
    uint8_t spb = state_.stepsPerBeat;
    if (spb == 0) spb = StepSequencerRuntimeState::DEFAULT_STEPS_PER_BEAT;
    if (spb > oc::note::clock::PPQN) spb = static_cast<uint8_t>(oc::note::clock::PPQN);

    uint8_t tps = static_cast<uint8_t>(oc::note::clock::PPQN / spb);
    if (tps == 0) tps = 1;
    return tps;
}

int32_t StepSequencerEngine::nudgeTickOffset_(int8_t nudge, uint8_t ticksPerStep) {
    const int32_t clamped = (nudge < -50) ? -50 : ((nudge > 50) ? 50 : nudge);
    const int32_t scaled = clamped * static_cast<int32_t>(ticksPerStep);

    if (scaled >= 0) {
        return (scaled + 50) / 100;
    }

    return -(((-scaled) + 50) / 100);
}

uint32_t StepSequencerEngine::swingTickOffset_(uint8_t stepIndex,
                                               uint8_t ticksPerStep,
                                               uint8_t swingPercent) {
    if ((stepIndex & 0x1U) == 0) return 0;
    const uint32_t scaled =
        static_cast<uint32_t>(ticksPerStep) * static_cast<uint32_t>(clampSwingPercent_(swingPercent));
    return (scaled + 100U) / 200U;
}

uint32_t StepSequencerEngine::timedStepStartTick_(uint32_t stepNumber,
                                                  uint8_t stepIndex,
                                                  uint8_t ticksPerStep) const {
    const uint32_t base = stepNumber * static_cast<uint32_t>(ticksPerStep);
    const uint32_t swing =
        swingTickOffset_(stepIndex, ticksPerStep, state_.effectiveSwingPercent);
    const int32_t patternNudge =
        nudgeTickOffset_(state_.patternNudgePercent, ticksPerStep);
    int64_t signedTick =
        static_cast<int64_t>(base) + static_cast<int64_t>(swing) + patternNudge;
    if (signedTick < 0) {
        signedTick = 0;
    }
    return static_cast<uint32_t>(signedTick);
}

uint32_t StepSequencerEngine::probabilityHash_(uint32_t runSeed, uint32_t cycleIndex, uint8_t stepIndex) {
    uint32_t x = runSeed * 747796405u;
    x ^= cycleIndex * 2891336453u;
    x ^= static_cast<uint32_t>(stepIndex) * 277803737u;
    x ^= 0x9E3779B9u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

StepBitMask128 StepSequencerEngine::resolveCycleMask_(uint32_t cycleIndex, uint8_t len) const {
    if (len == 0) return {};

    const StepBitMask128 enabledMask = state_.enabledMask;
    StepBitMask128 resolvedMask{};

    for (uint8_t stepIndex = 0; stepIndex < len; ++stepIndex) {
        if (!enabledMask.test(stepIndex)) continue;
        if (state_.gate[stepIndex] == 0) continue;

        const uint8_t probability =
            StepSequencerRuntimeState::clampProbability(state_.probability[stepIndex]);
        if (probability >= 100U) {
            resolvedMask.setBit(stepIndex, true);
            continue;
        }
        if (probability == 0U) {
            continue;
        }

        if ((probabilityHash_(run_seed_, cycleIndex, stepIndex) % 100U) < probability) {
            resolvedMask.setBit(stepIndex, true);
        }
    }

    return resolvedMask;
}

StepBitMask128 StepSequencerEngine::maskForCycle_(uint32_t cycleIndex, uint8_t len) {
    for (size_t i = 0; i < CYCLE_MASK_CACHE_SIZE; ++i) {
        if (cached_cycle_indices_[i] == cycleIndex) {
            return cached_cycle_masks_[i];
        }
    }

    const StepBitMask128 mask = resolveCycleMask_(cycleIndex, len);
    cached_cycle_indices_[next_cycle_cache_slot_] = cycleIndex;
    cached_cycle_masks_[next_cycle_cache_slot_] = mask;
    next_cycle_cache_slot_ = (next_cycle_cache_slot_ + 1U) % CYCLE_MASK_CACHE_SIZE;
    return mask;
}

bool StepSequencerEngine::shouldTriggerStep_(uint8_t stepIndex, uint32_t stepNumber, uint8_t len) {
    if (len == 0 || stepIndex >= len) return false;
    const uint32_t cycleIndex = stepNumber / static_cast<uint32_t>(len);
    return maskForCycle_(cycleIndex, len).test(stepIndex);
}

void StepSequencerEngine::publishCycleMask_(uint32_t cycleIndex, uint8_t len) {
    if (published_cycle_index_ == cycleIndex) {
        if (state_.variationTelemetryEnabled && !cycle_variation_telemetry_published_) {
            publishCycleVariationTelemetry_(cycleIndex, len, state_.probabilityCycleMask);
            cycle_variation_telemetry_published_ = true;
        }
        return;
    }

    published_cycle_index_ = cycleIndex;
    state_.probabilityCycleIndex = cycleIndex;
    state_.probabilityCycleMask = maskForCycle_(cycleIndex, len);
    state_.probabilityCycleRevision += 1U;
    cycle_variation_telemetry_published_ = false;
    if (state_.variationTelemetryEnabled) {
        publishCycleVariationTelemetry_(cycleIndex, len, state_.probabilityCycleMask);
        cycle_variation_telemetry_published_ = true;
    }
}

void StepSequencerEngine::start_() {
    playing_ = true;
    scheduler_.clear();
    next_step_tick_ = 0;
    last_tick_ = 0;
    next_scheduled_step_number_ = 0;
    ++run_seed_;
    published_cycle_index_ = UINT32_MAX;
    cycle_variation_telemetry_published_ = false;
    clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask;

    const uint8_t len = patternLength_();
    if (len > 0) {
        publishCycleMask_(0, len);
    }

    primeSchedule_();
}

void StepSequencerEngine::prepareFromTick_(uint32_t tick) {
    const uint8_t len = patternLength_();
    const uint8_t ticksPerStep = ticksPerStep_();
    rememberTimingContext_(ticksPerStep, len);

    last_tick_ = tick;
    published_cycle_index_ = UINT32_MAX;
    cycle_variation_telemetry_published_ = false;
    clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask;

    if (len == 0) {
        next_step_tick_ = 0;
        next_scheduled_step_number_ = 0;
        state_.playheadStep = -1;
        state_.playheadStepTickOffset = 0;
        state_.playheadStepTicks = ticksPerStep;
        state_.probabilityCycleMask = {};
        state_.probabilityCycleIndex = 0;
        state_.probabilityCycleRevision += 1U;
        state_.lastResolvedVariation = {};
        state_.cycleVariationTelemetry.reset();
        state_.expandedVariationTelemetry.reset();
        state_.variationTelemetryRevision += 1U;
        cycle_variation_telemetry_published_ = false;
        return;
    }

    const uint32_t stepNumber = tick / ticksPerStep;
    const uint8_t stepIndex = static_cast<uint8_t>(stepNumber % len);
    const uint32_t cycleIndex = stepNumber / static_cast<uint32_t>(len);

    publishCycleMask_(cycleIndex, len);
    publishPlayheadTickPosition_(tick, ticksPerStep, len);
    publishResolvedVariation_(stepIndex, cycleIndex, shouldTriggerStep_(stepIndex, stepNumber, len));

    next_step_tick_ = (stepNumber + 1U) * static_cast<uint32_t>(ticksPerStep);
    next_scheduled_step_number_ = stepNumber + 1U;
    while (next_scheduled_step_number_ < stepNumber + 4U) {
        scheduleStep_(next_scheduled_step_number_, ticksPerStep);
        ++next_scheduled_step_number_;
    }
}

void StepSequencerEngine::stop_() {
    if (!playing_) return;
    playing_ = false;
    scheduler_.clear();
    emitAllNotesOff_(last_tick_);
    state_.playheadStep = -1;
    state_.playheadStepTickOffset = 0;
    state_.playheadStepTicks = ticksPerStep_();
    published_cycle_index_ = UINT32_MAX;
    cycle_variation_telemetry_published_ = false;
    clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask;
    state_.probabilityCycleMask = {};
    state_.probabilityCycleIndex = 0;
    state_.probabilityCycleRevision += 1U;
    state_.lastResolvedVariation = {};
    state_.cycleVariationTelemetry.reset();
    state_.expandedVariationTelemetry.reset();
    state_.variationTelemetryRevision += 1U;
}

void StepSequencerEngine::update(uint32_t tick, bool playing) {
    if (playing && !playing_) {
        start_();
    } else if (!playing && playing_) {
        stop_();
        return;
    }

    if (!playing_) return;

    const uint8_t len = patternLength_();
    const uint8_t ticksPerStep = ticksPerStep_();
    if (timingContextChanged_(ticksPerStep, len)) {
        resyncTimingContext_(tick);
        last_tick_ = tick;
        return;
    }

    const StepBitMask128 enabledMask = state_.enabledMask;
    if (enabledMask != last_enabled_mask_) {
        last_enabled_mask_ = enabledMask;
        clearCycleMaskCache_();
        published_cycle_index_ = UINT32_MAX;
        cycle_variation_telemetry_published_ = false;
        if (len > 0) {
            const uint32_t currentStepNumber = next_step_tick_ / ticksPerStep;
            const uint32_t currentCycleIndex = currentStepNumber / static_cast<uint32_t>(len);
            publishCycleMask_(currentCycleIndex, len);
        }
    }

    // Handle tick resets defensively.
    if (tick < last_tick_) {
        scheduler_.clear();
        next_step_tick_ = 0;
        next_scheduled_step_number_ = 0;
        published_cycle_index_ = UINT32_MAX;
        cycle_variation_telemetry_published_ = false;
        clearCycleMaskCache_();
        last_enabled_mask_ = state_.enabledMask;
        const uint8_t len = patternLength_();
        if (len > 0) {
            publishCycleMask_(0, len);
        }
        primeSchedule_();
    }

    advanceToTick_(tick);
    last_tick_ = tick;
}

void StepSequencerEngine::advanceToTick_(uint32_t tick) {
    const uint8_t len = patternLength_();
    if (len == 0) {
        state_.playheadStep = -1;
        state_.playheadStepTickOffset = 0;
        state_.playheadStepTicks = ticksPerStep_();
        return;
    }

    const uint8_t ticksPerStep = ticksPerStep_();

    while (next_step_tick_ <= tick) {
        processDueEvents_(next_step_tick_);

        const uint32_t stepNumber = next_step_tick_ / ticksPerStep;
        const uint8_t stepIndex = static_cast<uint8_t>(stepNumber % len);
        const uint32_t cycleIndex = stepNumber / static_cast<uint32_t>(len);

        publishCycleMask_(cycleIndex, len);
        publishPlayheadTickPosition_(next_step_tick_, ticksPerStep, len);
        publishResolvedVariation_(stepIndex, cycleIndex, shouldTriggerStep_(stepIndex, stepNumber, len));

        while (next_scheduled_step_number_ < stepNumber + 3U) {
            scheduleStep_(next_scheduled_step_number_, ticksPerStep);
            ++next_scheduled_step_number_;
        }

        next_step_tick_ += ticksPerStep;
    }

    processDueEvents_(tick);
    publishPlayheadTickPosition_(tick, ticksPerStep, len);
}

void StepSequencerEngine::primeSchedule_() {
    const uint8_t len = patternLength_();
    if (len == 0) return;

    const uint8_t ticksPerStep = ticksPerStep_();
    rememberTimingContext_(ticksPerStep, len);
    scheduleStep_(0, ticksPerStep);
    scheduleStep_(1, ticksPerStep);
    next_scheduled_step_number_ = 2;
}

void StepSequencerEngine::scheduleStep_(uint32_t stepNumber, uint8_t ticksPerStep) {
    const uint8_t len = patternLength_();
    if (len == 0) return;

    const uint8_t stepIndex = static_cast<uint8_t>(stepNumber % len);
    if (stepIndex >= StepSequencerRuntimeState::MAX_STEPS) return;

    const uint32_t cycleIndex = stepNumber / static_cast<uint32_t>(len);
    const uint32_t stepStartTick = timedStepStartTick_(stepNumber, stepIndex, ticksPerStep);

    if (graph_ != nullptr && graph_->enabled &&
        graph_->sequence(graph_->rootSequenceId) != nullptr) {
        const auto expansion = StepSequencerExpander::expandRootStep(
            state_,
            *graph_,
            stepIndex,
            cycleIndex,
            ticksPerStep,
            run_seed_,
            true
        );
        for (uint8_t i = 0; i < expansion.count; ++i) {
            scheduleExpandedNote_(stepStartTick, expansion.notes[i]);
        }
        return;
    }

    if (!shouldTriggerStep_(stepIndex, stepNumber, len)) return;

    const auto variation = resolveVariation_(stepIndex, cycleIndex, true);

    const uint8_t ch = clampChannel_(state_.midiChannel);
    const uint8_t note = variation.resolved.note;
    const uint8_t vel = variation.resolved.velocity;

    const int32_t startOffset = nudgeTickOffset_(variation.resolved.nudge, ticksPerStep);
    int64_t onTickSigned = static_cast<int64_t>(stepStartTick) + static_cast<int64_t>(startOffset);
    if (onTickSigned < 0) {
        onTickSigned = 0;
    }
    const uint32_t onTick = static_cast<uint32_t>(onTickSigned);

    uint32_t offTicks = (static_cast<uint32_t>(variation.resolved.gate) * ticksPerStep) / 100U;
    if (offTicks == 0) offTicks = 1;

    const uint32_t offTick = onTick + offTicks;
    if (!scheduler_.scheduleNote(onTick, offTick, ch, note, vel)) {
        emitAllNotesOff_(offTick);
        scheduler_.clear();
    }
}

void StepSequencerEngine::scheduleExpandedNote_(uint32_t stepStartTick,
                                                const StepSequencerExpandedNote& note) {
    const auto& variation = note.variation;
    const uint8_t ch = clampChannel_(state_.midiChannel);
    const uint8_t midiNote = variation.resolved.note;
    const uint8_t vel = variation.resolved.velocity;
    const uint16_t spanTicks = (note.spanTicks == 0) ? 1U : note.spanTicks;
    const uint32_t noteStartTick = stepStartTick + note.localTick;
    const int32_t startOffset =
        nudgeTickOffset_(variation.resolved.nudge, static_cast<uint8_t>(spanTicks));
    int64_t onTickSigned = static_cast<int64_t>(noteStartTick) + static_cast<int64_t>(startOffset);
    if (onTickSigned < 0) {
        onTickSigned = 0;
    }
    const uint32_t onTick = static_cast<uint32_t>(onTickSigned);

    uint32_t offTicks = (static_cast<uint32_t>(variation.resolved.gate) * spanTicks) / 100U;
    if (offTicks == 0) offTicks = 1;

    const uint32_t offTick = onTick + offTicks;
    if (!scheduler_.scheduleNote(onTick, offTick, ch, midiNote, vel)) {
        emitAllNotesOff_(offTick);
        scheduler_.clear();
    }
}

StepSequencerResolvedVariation StepSequencerEngine::resolveVariation_(uint8_t stepIndex,
                                                                      uint32_t cycleIndex,
                                                                      bool triggered) const {
    if (stepIndex >= StepSequencerRuntimeState::MAX_STEPS) {
        return {};
    }

    return resolveStepVariation(
        StepSequencerStepValues{
            .note = state_.note[stepIndex],
            .velocity = state_.velocity[stepIndex],
            .gate = state_.gate[stepIndex],
            .nudge = state_.nudge[stepIndex],
        },
        state_.variationRanges,
        state_.scaleSettings,
        StepSequencerRuntimeState::MAX_GATE_PERCENT,
        run_seed_,
        cycleIndex,
        stepIndex,
        triggered
    );
}

void StepSequencerEngine::publishExpandedVariationTelemetry_(uint8_t stepIndex,
                                                            uint32_t cycleIndex,
                                                            bool triggered) {
    state_.expandedVariationTelemetry.reset();
    if (graph_ == nullptr ||
        !graph_->enabled ||
        graph_->sequence(graph_->rootSequenceId) == nullptr) {
        return;
    }

    auto& telemetry = state_.expandedVariationTelemetry;
    telemetry.valid = true;
    telemetry.rootStepIndex = stepIndex;
    telemetry.cycleIndex = cycleIndex;

    const auto expansion = StepSequencerExpander::expandRootStep(
        state_,
        *graph_,
        stepIndex,
        cycleIndex,
        ticksPerStep_(),
        run_seed_,
        triggered
    );
    const uint8_t count = std::min<uint8_t>(
        expansion.count,
        StepSequencerExpandedVariationTelemetry::MAX_NOTES
    );
    for (uint8_t i = 0; i < count; ++i) {
        telemetry.store(
            i,
            expansion.notes[i].nodeId,
            expansion.notes[i].localTick,
            expansion.notes[i].spanTicks,
            expansion.notes[i].variation
        );
    }
}

void StepSequencerEngine::publishResolvedVariation_(uint8_t stepIndex,
                                                    uint32_t cycleIndex,
                                                    bool triggered) {
    publishExpandedVariationTelemetry_(stepIndex, cycleIndex, triggered);
    if (state_.expandedVariationTelemetry.valid) {
        state_.lastResolvedVariation = state_.expandedVariationTelemetry.count > 0
            ? state_.expandedVariationTelemetry.variation[0]
            : resolveVariation_(stepIndex, cycleIndex, false);
        return;
    }

    state_.lastResolvedVariation = resolveVariation_(stepIndex, cycleIndex, triggered);
}

void StepSequencerEngine::publishPlayheadTickPosition_(uint32_t tick,
                                                       uint8_t ticksPerStep,
                                                       uint8_t len) {
    if (len == 0 || ticksPerStep == 0) {
        state_.playheadStep = -1;
        state_.playheadStepTickOffset = 0;
        state_.playheadStepTicks = ticksPerStep == 0 ? 1 : ticksPerStep;
        return;
    }

    const uint32_t stepNumber = tick / ticksPerStep;
    state_.playheadStep = static_cast<int16_t>(stepNumber % len);
    state_.playheadStepTickOffset =
        static_cast<uint16_t>(tick - (stepNumber * static_cast<uint32_t>(ticksPerStep)));
    state_.playheadStepTicks = ticksPerStep;
}

void StepSequencerEngine::publishCycleVariationTelemetry_(uint32_t cycleIndex,
                                                         uint8_t len,
                                                         const StepBitMask128& triggeredMask) {
    state_.cycleVariationTelemetry.reset();
    state_.cycleVariationTelemetry.cycleIndex = cycleIndex;
    state_.cycleVariationTelemetry.ranges = state_.variationRanges;
    state_.cycleVariationTelemetry.ranges.clamp();
    state_.cycleVariationTelemetry.scaleSettings = state_.scaleSettings;
    state_.cycleVariationTelemetry.scaleSettings.clamp();

    const uint8_t safeLen = std::min<uint8_t>(len, StepSequencerRuntimeState::MAX_STEPS);
    const bool graphActive =
        graph_ != nullptr &&
        graph_->enabled &&
        graph_->sequence(graph_->rootSequenceId) != nullptr;
    for (uint8_t stepIndex = 0; stepIndex < safeLen; ++stepIndex) {
        const bool enabled = state_.enabledMask.test(stepIndex);
        const bool triggered = enabled && triggeredMask.test(stepIndex);
        if (graphActive) {
            const auto expansion = StepSequencerExpander::expandRootStep(
                state_,
                *graph_,
                stepIndex,
                cycleIndex,
                ticksPerStep_(),
                run_seed_,
                triggered
            );
            state_.cycleVariationTelemetry.store(
                expansion.count > 0
                    ? expansion.notes[0].variation
                    : resolveVariation_(stepIndex, cycleIndex, false)
            );
        } else {
            state_.cycleVariationTelemetry.store(
                resolveVariation_(stepIndex, cycleIndex, triggered)
            );
        }
    }

    state_.variationTelemetryRevision += 1U;
}

bool StepSequencerEngine::timingContextChanged_(uint8_t ticksPerStep, uint8_t len) const {
    if (!timing_context_valid_) return false;
    return last_ticks_per_step_ != ticksPerStep ||
           last_pattern_length_ != len ||
           last_effective_swing_percent_ != state_.effectiveSwingPercent ||
           last_pattern_nudge_percent_ != state_.patternNudgePercent;
}

void StepSequencerEngine::rememberTimingContext_(uint8_t ticksPerStep, uint8_t len) {
    last_ticks_per_step_ = ticksPerStep;
    last_pattern_length_ = len;
    last_effective_swing_percent_ = state_.effectiveSwingPercent;
    last_pattern_nudge_percent_ = state_.patternNudgePercent;
    timing_context_valid_ = true;
}

void StepSequencerEngine::resyncTimingContext_(uint32_t tick) {
    scheduler_.clear();
    emitAllNotesOff_(tick);
    prepareFromTick_(tick);
}

bool StepSequencerEngine::emitAllNotesOff_(uint32_t tick) {
    SequencerEvent event{};
    event.tick = tick;
    event.type = SequencerEventType::AllNotesOff;
    return event_sink_.emitSequencerEvent(event);
}

bool StepSequencerEngine::processDueEvents_(uint32_t tick) {
    if (scheduler_.processUntil(tick, event_sink_)) {
        return true;
    }

    emitAllNotesOff_(tick);
    scheduler_.clear();
    return false;
}

}  // namespace oc::note::sequencer
