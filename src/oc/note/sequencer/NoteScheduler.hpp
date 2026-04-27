#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "SequencerEvent.hpp"

namespace oc::note::sequencer {

class NoteScheduler {
public:
    static constexpr size_t MAX_EVENTS = 128;

    void clear() { count_ = 0; }

    size_t size() const { return count_; }

    bool scheduleNoteOn(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity) {
        return schedule_(tick, SequencerEventType::NoteOn, channel, note, velocity);
    }

    bool scheduleNoteOff(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity = 0) {
        return schedule_(tick, SequencerEventType::NoteOff, channel, note, velocity);
    }

    bool processUntil(uint32_t tick, ISequencerEventSink& sink) {
        if (count_ == 0) return true;

        while (true) {
            size_t dueIndex = count_;

            for (size_t i = 0; i < count_; ++i) {
                if (events_[i].tick > tick) continue;

                if (dueIndex == count_ || comesBefore_(events_[i], events_[dueIndex])) {
                    dueIndex = i;
                }
            }

            if (dueIndex == count_) {
                break;
            }

            if (!sink.emitSequencerEvent(events_[dueIndex])) {
                return false;
            }

            --count_;
            if (dueIndex != count_) {
                events_[dueIndex] = events_[count_];
            }
        }

        return true;
    }

private:
    static bool comesBefore_(const SequencerEvent& lhs, const SequencerEvent& rhs) {
        if (lhs.tick != rhs.tick) return lhs.tick < rhs.tick;
        if (lhs.type != rhs.type) return priority_(lhs.type) < priority_(rhs.type);
        return false;
    }

    static uint8_t priority_(SequencerEventType type) {
        return (type == SequencerEventType::NoteOff) ? 0U : 1U;
    }

    bool schedule_(uint32_t tick,
                   SequencerEventType type,
                   uint8_t channel,
                   uint8_t note,
                   uint8_t velocity) {
        if (count_ >= MAX_EVENTS) return false;
        events_[count_++] = {tick, type, channel, note, velocity};
        return true;
    }

    std::array<SequencerEvent, MAX_EVENTS> events_{};
    size_t count_ = 0;
};

}  // namespace oc::note::sequencer
