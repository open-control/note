#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "SequencerEvent.hpp"

namespace oc::note::sequencer {

class NoteScheduler {
public:
    static constexpr size_t MAX_EVENTS = 256;
    static constexpr size_t MAX_ACTIVE_TOKENS = 128;

    void clear() {
        count_ = 0;
        active_tokens_.fill({});
    }

    size_t size() const { return count_; }

    bool scheduleNoteOn(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity) {
        return schedule_(tick, SequencerEventType::NoteOn, channel, note, velocity, nextToken_());
    }

    bool scheduleNoteOff(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity = 0) {
        return schedule_(tick, SequencerEventType::NoteOff, channel, note, velocity, 0);
    }

    bool scheduleNote(uint32_t onTick,
                      uint32_t offTick,
                      uint8_t channel,
                      uint8_t note,
                      uint8_t velocity) {
        const uint16_t token = nextToken_();
        return schedule_(onTick, SequencerEventType::NoteOn, channel, note, velocity, token) &&
               schedule_(offTick, SequencerEventType::NoteOff, channel, note, 0, token);
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

            const SequencerEvent event = events_[dueIndex];
            if (!emitDueEvent_(event, sink)) {
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

    struct ActiveToken {
        uint8_t channel = 0;
        uint8_t note = 0;
        uint16_t token = 0;
    };

    ActiveToken* findActiveToken_(uint8_t channel, uint8_t note) {
        const uint8_t safeChannel = static_cast<uint8_t>(channel & 0x0FU);
        const uint8_t safeNote = static_cast<uint8_t>(note & 0x7FU);
        for (auto& active : active_tokens_) {
            if (active.token != 0 &&
                active.channel == safeChannel &&
                active.note == safeNote) {
                return &active;
            }
        }
        return nullptr;
    }

    ActiveToken* firstFreeActiveToken_() {
        for (auto& active : active_tokens_) {
            if (active.token == 0) return &active;
        }
        return nullptr;
    }

    bool rememberActiveToken_(const SequencerEvent& event) {
        ActiveToken* active = findActiveToken_(event.channel, event.note);
        if (active == nullptr) {
            active = firstFreeActiveToken_();
        }
        if (active == nullptr) {
            return false;
        }
        *active = ActiveToken{
            .channel = static_cast<uint8_t>(event.channel & 0x0FU),
            .note = static_cast<uint8_t>(event.note & 0x7FU),
            .token = event.token,
        };
        return true;
    }

    uint16_t nextToken_() {
        const uint16_t token = next_token_;
        ++next_token_;
        if (next_token_ == 0) {
            next_token_ = 1;
        }
        return token;
    }

    bool emitDueEvent_(const SequencerEvent& event, ISequencerEventSink& sink) {
        if (event.type == SequencerEventType::NoteOn) {
            const ActiveToken* active = findActiveToken_(event.channel, event.note);
            if (active != nullptr) {
                SequencerEvent off = event;
                off.type = SequencerEventType::NoteOff;
                off.velocity = 0;
                off.token = active->token;
                if (!sink.emitSequencerEvent(off)) {
                    return false;
                }
            }
            if (!rememberActiveToken_(event)) {
                SequencerEvent panic = event;
                panic.type = SequencerEventType::AllNotesOff;
                panic.velocity = 0;
                panic.token = 0;
                if (!sink.emitSequencerEvent(panic)) {
                    return false;
                }
                clear();
                return true;
            }
            return sink.emitSequencerEvent(event);
        }

        if (event.type == SequencerEventType::NoteOff) {
            ActiveToken* active = findActiveToken_(event.channel, event.note);
            if (event.token != 0 && (active == nullptr || active->token != event.token)) {
                return true;
            }
            if (active != nullptr) {
                *active = {};
            }
            return sink.emitSequencerEvent(event);
        }

        return sink.emitSequencerEvent(event);
    }

    bool schedule_(uint32_t tick,
                   SequencerEventType type,
                   uint8_t channel,
                   uint8_t note,
                   uint8_t velocity,
                   uint16_t token) {
        if (count_ >= MAX_EVENTS) return false;
        events_[count_++] = {tick, type, channel, note, velocity, token};
        return true;
    }

    std::array<SequencerEvent, MAX_EVENTS> events_{};
    std::array<ActiveToken, MAX_ACTIVE_TOKENS> active_tokens_{};
    size_t count_ = 0;
    uint16_t next_token_ = 1;
};

}  // namespace oc::note::sequencer
