#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "SequencerEvent.hpp"

namespace oc::note::sequencer {

template <size_t MaxEvents, size_t MaxActiveTokens>
class BoundedNoteScheduler {
public:
    static_assert(MaxEvents >= 2, "a note requires one on and one off edge");
    static_assert(MaxActiveTokens >= 1, "at least one sounding note is required");

    static constexpr size_t MAX_EVENTS = MaxEvents;
    static constexpr size_t MAX_ACTIVE_TOKENS = MaxActiveTokens;

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
        // Keep the pair transactional. A capacity failure must never retain a
        // NoteOn without its matching NoteOff.
        if (count_ > MAX_EVENTS - 2U) return false;
        const uint16_t token = nextToken_();
        return schedule_(onTick, SequencerEventType::NoteOn, channel, note, velocity, token) &&
               schedule_(offTick, SequencerEventType::NoteOff, channel, note, 0, token);
    }

    /**
     * Schedule a monophonic/retriggering voice without retaining dominated
     * future NoteOff edges for the same MIDI note. An old tokenized off is
     * redundant only when its paired NoteOn is already active or is queued no
     * later than the new onset. Keep manual safety offs and pairs whose onset
     * is still later: callers may submit bounded events out of tick order.
     */
    bool scheduleRetriggeringNote(uint32_t onTick,
                                  uint32_t offTick,
                                  uint8_t channel,
                                  uint8_t note,
                                  uint8_t velocity) {
        const uint8_t safeChannel = static_cast<uint8_t>(channel & 0x0FU);
        const uint8_t safeNote = static_cast<uint8_t>(note & 0x7FU);
        size_t removable = 0U;
        for (size_t i = 0U; i < count_; ++i) {
            const auto& event = events_[i];
            if (isDominatedRetriggerOff_(
                    event,
                    onTick,
                    safeChannel,
                    safeNote
                )) {
                ++removable;
            }
        }
        if (count_ - removable > MAX_EVENTS - 2U) return false;
        for (size_t remaining = count_; remaining > 0U; --remaining) {
            const size_t index = remaining - 1U;
            const auto& event = events_[index];
            if (!isDominatedRetriggerOff_(
                    event,
                    onTick,
                    safeChannel,
                    safeNote
                )) {
                continue;
            }
            --count_;
            if (index != count_) events_[index] = events_[count_];
        }

        const uint16_t token = nextToken_();
        return schedule_(
                   onTick,
                   SequencerEventType::NoteOn,
                   safeChannel,
                   safeNote,
                   velocity,
                   token
               ) &&
               schedule_(
                   offTick,
                   SequencerEventType::NoteOff,
                   safeChannel,
                   safeNote,
                   0U,
                   token
               );
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
    bool isDominatedRetriggerOff_(
        const SequencerEvent& event,
        uint32_t newOnTick,
        uint8_t channel,
        uint8_t note
    ) const {
        if (event.type != SequencerEventType::NoteOff || event.token == 0U ||
            event.channel != channel || event.note != note ||
            event.tick < newOnTick) {
            return false;
        }

        for (size_t i = 0U; i < count_; ++i) {
            const auto& paired = events_[i];
            if (paired.type == SequencerEventType::NoteOn &&
                paired.token == event.token) {
                return paired.tick <= newOnTick;
            }
        }

        // The paired onset has already left the queue. Its off is superseded
        // by the retrigger edge emitted for the currently active voice.
        return true;
    }

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

using NoteScheduler = BoundedNoteScheduler<256U, 128U>;

}  // namespace oc::note::sequencer
