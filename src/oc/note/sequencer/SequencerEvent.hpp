#pragma once

#include <cstdint>

namespace oc::note::sequencer {

enum class SequencerEventType : uint8_t {
    NoteOn,
    NoteOff,
    AllNotesOff,
};

struct SequencerEvent {
    uint32_t tick = 0;
    SequencerEventType type = SequencerEventType::NoteOn;
    uint8_t channel = 0;
    uint8_t note = 0;
    uint8_t velocity = 0;
};

struct ISequencerEventSink {
    virtual ~ISequencerEventSink() = default;
    virtual bool emitSequencerEvent(const SequencerEvent& event) = 0;
};

}  // namespace oc::note::sequencer
