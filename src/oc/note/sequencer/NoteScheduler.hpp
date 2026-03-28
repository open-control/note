#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ISequencerOutput.hpp"

namespace oc::note::sequencer {

/**
 * @brief Minimal fixed-capacity scheduler for note events (v0)
 */
class NoteScheduler {
public:
    static constexpr size_t MAX_EVENTS = 128;

    enum class EventType : uint8_t {
        NoteOn,
        NoteOff,
    };

    struct Event {
        uint32_t tick;
        EventType type;
        uint8_t channel;
        uint8_t note;
        uint8_t velocity;
    };

    void clear() { count_ = 0; }

    size_t size() const { return count_; }

    bool scheduleNoteOn(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity) {
        return schedule_(tick, EventType::NoteOn, channel, note, velocity);
    }

    bool scheduleNoteOff(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity = 0) {
        return schedule_(tick, EventType::NoteOff, channel, note, velocity);
    }

    void processUntil(uint32_t tick, ISequencerOutput& out) {
        if (count_ == 0) return;

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

            dispatch_(events_[dueIndex], out);

            --count_;
            if (dueIndex != count_) {
                events_[dueIndex] = events_[count_];
            }
        }
    }

private:
    static bool comesBefore_(const Event& lhs, const Event& rhs) {
        if (lhs.tick != rhs.tick) return lhs.tick < rhs.tick;
        if (lhs.type != rhs.type) return priority_(lhs.type) < priority_(rhs.type);
        return false;
    }

    static uint8_t priority_(EventType type) {
        return (type == EventType::NoteOff) ? 0U : 1U;
    }

    static void dispatch_(const Event& event, ISequencerOutput& out) {
        if (event.type == EventType::NoteOn) {
            out.sendNoteOn(event.channel, event.note, event.velocity);
            return;
        }

        out.sendNoteOff(event.channel, event.note, event.velocity);
    }

    bool schedule_(uint32_t tick, EventType type, uint8_t channel, uint8_t note, uint8_t velocity) {
        if (count_ >= MAX_EVENTS) return false;
        events_[count_++] = {tick, type, channel, note, velocity};
        return true;
    }

    std::array<Event, MAX_EVENTS> events_{};
    size_t count_ = 0;
};

}  // namespace oc::note::sequencer
