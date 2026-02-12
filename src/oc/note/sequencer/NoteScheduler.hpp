#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ISequencerOutput.hpp"

namespace oc::note::sequencer {

/**
 * @brief Minimal fixed-capacity scheduler for note-off events (v0)
 */
class NoteScheduler {
public:
    static constexpr size_t MAX_EVENTS = 64;

    struct NoteOff {
        uint32_t tick;
        uint8_t channel;
        uint8_t note;
        uint8_t velocity;
    };

    void clear() { count_ = 0; }

    size_t size() const { return count_; }

    bool scheduleNoteOff(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity = 0) {
        if (count_ >= MAX_EVENTS) return false;
        events_[count_++] = {tick, channel, note, velocity};
        return true;
    }

    void processUntil(uint32_t tick, ISequencerOutput& out) {
        if (count_ == 0) return;

        size_t write = 0;
        for (size_t i = 0; i < count_; ++i) {
            const auto& e = events_[i];
            if (e.tick <= tick) {
                out.sendNoteOff(e.channel, e.note, e.velocity);
            } else {
                events_[write++] = e;
            }
        }
        count_ = write;
    }

private:
    std::array<NoteOff, MAX_EVENTS> events_{};
    size_t count_ = 0;
};

}  // namespace oc::note::sequencer
