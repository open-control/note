#pragma once

#include <cstddef>
#include <cstdint>

namespace oc::note::sequencer {

/**
 * @brief Abstraction for sequencer output (USB MIDI, protocol, etc.)
 */
struct ISequencerOutput {
    virtual ~ISequencerOutput() = default;

    virtual void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) = 0;
    virtual void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0) = 0;
    virtual void sendCC(uint8_t channel, uint8_t cc, uint8_t value) = 0;

    virtual void allNotesOff() {}
};

}  // namespace oc::note::sequencer
