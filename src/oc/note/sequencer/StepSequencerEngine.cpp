#include "StepSequencerEngine.hpp"

namespace oc::note::sequencer {

void StepSequencerEngine::clearCycleMaskCache_() {
    cached_cycle_indices_.fill(UINT32_MAX);
    cached_cycle_masks_.fill(0);
    next_cycle_cache_slot_ = 0;
}

void StepSequencerEngine::reset() {
    stop_();
    scheduler_.clear();
    last_tick_ = 0;
    next_step_tick_ = 0;
    next_scheduled_step_number_ = 0;
    published_cycle_index_ = UINT32_MAX;
    clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask.get();
    state_.probabilityCycleMask = 0;
    state_.probabilityCycleIndex = 0;
    state_.probabilityCycleRevision.set(state_.probabilityCycleRevision.get() + 1U);
}

uint8_t StepSequencerEngine::clampChannel_(uint8_t ch) {
    return (ch > 15) ? 15 : ch;
}

uint8_t StepSequencerEngine::patternLength_() const {
    const uint8_t len = state_.patternLength();
    return len;
}

uint8_t StepSequencerEngine::ticksPerStep_() const {
    uint8_t spb = state_.stepsPerBeat.get();
    if (spb == 0) spb = StepSequencerState::DEFAULT_STEPS_PER_BEAT;
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

uint64_t StepSequencerEngine::resolveCycleMask_(uint32_t cycleIndex, uint8_t len) const {
    if (len == 0) return 0;

    const uint64_t enabledMask = state_.enabledMask.get();
    uint64_t resolvedMask = 0;

    for (uint8_t stepIndex = 0; stepIndex < len; ++stepIndex) {
        const uint64_t stepBit = (1ULL << stepIndex);
        if ((enabledMask & stepBit) == 0) continue;
        if (state_.gate[stepIndex] == 0) continue;

        const uint8_t probability = StepSequencerState::clampProbability(state_.probability[stepIndex]);
        if (probability >= 100U) {
            resolvedMask |= stepBit;
            continue;
        }
        if (probability == 0U) {
            continue;
        }

        if ((probabilityHash_(run_seed_, cycleIndex, stepIndex) % 100U) < probability) {
            resolvedMask |= stepBit;
        }
    }

    return resolvedMask;
}

uint64_t StepSequencerEngine::maskForCycle_(uint32_t cycleIndex, uint8_t len) {
    for (size_t i = 0; i < CYCLE_MASK_CACHE_SIZE; ++i) {
        if (cached_cycle_indices_[i] == cycleIndex) {
            return cached_cycle_masks_[i];
        }
    }

    const uint64_t mask = resolveCycleMask_(cycleIndex, len);
    cached_cycle_indices_[next_cycle_cache_slot_] = cycleIndex;
    cached_cycle_masks_[next_cycle_cache_slot_] = mask;
    next_cycle_cache_slot_ = (next_cycle_cache_slot_ + 1U) % CYCLE_MASK_CACHE_SIZE;
    return mask;
}

bool StepSequencerEngine::shouldTriggerStep_(uint8_t stepIndex, uint32_t stepNumber, uint8_t len) {
    if (len == 0 || stepIndex >= len) return false;
    const uint32_t cycleIndex = stepNumber / static_cast<uint32_t>(len);
    return (maskForCycle_(cycleIndex, len) & (1ULL << stepIndex)) != 0;
}

void StepSequencerEngine::publishCycleMask_(uint32_t cycleIndex, uint8_t len) {
    if (published_cycle_index_ == cycleIndex) return;

    published_cycle_index_ = cycleIndex;
    state_.probabilityCycleIndex = cycleIndex;
    state_.probabilityCycleMask = maskForCycle_(cycleIndex, len);
    state_.probabilityCycleRevision.set(state_.probabilityCycleRevision.get() + 1U);
}

void StepSequencerEngine::start_() {
    playing_ = true;
    scheduler_.clear();
    next_step_tick_ = 0;
    last_tick_ = 0;
    next_scheduled_step_number_ = 0;
    ++run_seed_;
    published_cycle_index_ = UINT32_MAX;
    clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask.get();

    const uint8_t len = patternLength_();
    if (len > 0) {
        publishCycleMask_(0, len);
    }

    primeSchedule_();
}

void StepSequencerEngine::stop_() {
    if (!playing_) return;
    playing_ = false;
    scheduler_.clear();
    output_.allNotesOff();
    state_.playheadStep.set(-1);
    published_cycle_index_ = UINT32_MAX;
    clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask.get();
    state_.probabilityCycleMask = 0;
    state_.probabilityCycleIndex = 0;
    state_.probabilityCycleRevision.set(state_.probabilityCycleRevision.get() + 1U);
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
    const uint64_t enabledMask = state_.enabledMask.get();
    if (enabledMask != last_enabled_mask_) {
        last_enabled_mask_ = enabledMask;
        clearCycleMaskCache_();
        published_cycle_index_ = UINT32_MAX;
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
        clearCycleMaskCache_();
    last_enabled_mask_ = state_.enabledMask.get();
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
        state_.playheadStep.set(-1);
        return;
    }

    const uint8_t ticksPerStep = ticksPerStep_();

    while (next_step_tick_ <= tick) {
        scheduler_.processUntil(next_step_tick_, output_);

        const uint32_t stepNumber = next_step_tick_ / ticksPerStep;
        const uint8_t stepIndex = static_cast<uint8_t>(stepNumber % len);
        const uint32_t cycleIndex = stepNumber / static_cast<uint32_t>(len);

        publishCycleMask_(cycleIndex, len);
        state_.playheadStep.set(stepIndex);

        while (next_scheduled_step_number_ < stepNumber + 3U) {
            scheduleStep_(next_scheduled_step_number_, ticksPerStep);
            ++next_scheduled_step_number_;
        }

        next_step_tick_ += ticksPerStep;
    }

    scheduler_.processUntil(tick, output_);
}

void StepSequencerEngine::primeSchedule_() {
    const uint8_t len = patternLength_();
    if (len == 0) return;

    const uint8_t ticksPerStep = ticksPerStep_();
    scheduleStep_(0, ticksPerStep);
    scheduleStep_(1, ticksPerStep);
    next_scheduled_step_number_ = 2;
}

void StepSequencerEngine::scheduleStep_(uint32_t stepNumber, uint8_t ticksPerStep) {
    const uint8_t len = patternLength_();
    if (len == 0) return;

    const uint8_t stepIndex = static_cast<uint8_t>(stepNumber % len);
    if (stepIndex >= StepSequencerState::MAX_STEPS) return;

    if (!shouldTriggerStep_(stepIndex, stepNumber, len)) return;

    const uint8_t ch = clampChannel_(state_.midiChannel.get());
    const uint8_t note = state_.note[stepIndex];
    const uint8_t vel = state_.velocity[stepIndex];

    const uint32_t stepStartTick = stepNumber * static_cast<uint32_t>(ticksPerStep);
    const int32_t startOffset = nudgeTickOffset_(state_.nudge[stepIndex], ticksPerStep);
    int64_t onTickSigned = static_cast<int64_t>(stepStartTick) + static_cast<int64_t>(startOffset);
    if (onTickSigned < 0) {
        onTickSigned = 0;
    }
    const uint32_t onTick = static_cast<uint32_t>(onTickSigned);

    if (!scheduler_.scheduleNoteOn(onTick, ch, note, vel)) {
        output_.allNotesOff();
        scheduler_.clear();
        return;
    }

    uint32_t offTicks = (static_cast<uint32_t>(state_.gate[stepIndex]) * ticksPerStep) / 100U;
    if (offTicks == 0) offTicks = 1;

    const uint32_t offTick = onTick + offTicks;
    if (!scheduler_.scheduleNoteOff(offTick, ch, note, 0)) {
        // Fail-safe: avoid hanging notes if scheduler saturates.
        output_.allNotesOff();
        scheduler_.clear();
    }
}

}  // namespace oc::note::sequencer