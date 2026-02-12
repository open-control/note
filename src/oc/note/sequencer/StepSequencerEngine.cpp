#include "StepSequencerEngine.hpp"

namespace oc::note::sequencer {

void StepSequencerEngine::reset() {
    stop_();
    scheduler_.clear();
    last_tick_ = 0;
    next_step_tick_ = 0;
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

void StepSequencerEngine::start_() {
    playing_ = true;
    scheduler_.clear();
    next_step_tick_ = 0;
    last_tick_ = 0;
}

void StepSequencerEngine::stop_() {
    if (!playing_) return;
    playing_ = false;
    scheduler_.clear();
    output_.allNotesOff();
    state_.playheadStep.set(-1);
}

void StepSequencerEngine::update(uint32_t tick, bool playing) {
    if (playing && !playing_) {
        start_();
    } else if (!playing && playing_) {
        stop_();
        return;
    }

    if (!playing_) return;

    // Handle tick resets defensively.
    if (tick < last_tick_) {
        scheduler_.clear();
        next_step_tick_ = 0;
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
        // Ensure note-offs at the boundary are processed before the new step note-on.
        scheduler_.processUntil(next_step_tick_, output_);

        const uint32_t stepNumber = next_step_tick_ / ticksPerStep;
        const uint8_t stepIndex = static_cast<uint8_t>(stepNumber % len);

        state_.playheadStep.set(stepIndex);
        triggerStep_(stepIndex, next_step_tick_, ticksPerStep);

        next_step_tick_ += ticksPerStep;
    }

    // Process note-offs between boundaries (gate shorter than step).
    scheduler_.processUntil(tick, output_);
}

void StepSequencerEngine::triggerStep_(uint8_t stepIndex, uint32_t stepStartTick, uint8_t ticksPerStep) {
    if (stepIndex >= StepSequencerState::MAX_STEPS) return;

    const uint64_t mask = state_.enabledMask.get();
    if ((mask & (1ULL << stepIndex)) == 0) return;

    const uint16_t gatePct = state_.gate[stepIndex];
    if (gatePct == 0) return;

    const uint8_t ch = clampChannel_(state_.midiChannel.get());
    const uint8_t note = state_.note[stepIndex];
    const uint8_t vel = state_.velocity[stepIndex];

    output_.sendNoteOn(ch, note, vel);

    uint32_t offTicks = (static_cast<uint32_t>(gatePct) * ticksPerStep) / 100U;
    if (offTicks == 0) offTicks = 1;

    const uint32_t offTick = stepStartTick + offTicks;
    if (!scheduler_.scheduleNoteOff(offTick, ch, note, 0)) {
        // Fail-safe: avoid hanging notes if scheduler saturates.
        output_.allNotesOff();
        scheduler_.clear();
    }
}

}  // namespace oc::note::sequencer
