#pragma once

#include <array>
#include <cstdint>

namespace oc::note::sequencer {

/** Ordinary per-step values shared by reactive state and detached documents.
 * No signals, owners or runtime state: copying these bytes never publishes an
 * edit. Callers retain their own validation and notification boundaries.
 * Value initialization stays zero-filled; resetStepData selects musical defaults.
 */
struct StepSequencerStepData {
    static constexpr uint8_t MAX_STEPS = 128;
    static constexpr uint16_t MAX_GATE_PERCENT = 1600;
    static constexpr uint8_t DEFAULT_NOTE = 48;
    static constexpr uint8_t DEFAULT_VELOCITY = 64;
    static constexpr uint16_t DEFAULT_GATE_PERCENT = 100;
    static constexpr uint8_t DEFAULT_PROBABILITY = 100;

    std::array<uint8_t, MAX_STEPS> note{};
    std::array<uint8_t, MAX_STEPS> velocity{};
    std::array<uint16_t, MAX_STEPS> gate{};
    std::array<int8_t, MAX_STEPS> nudge{};
    std::array<uint8_t, MAX_STEPS> probability{};

    void resetStepData() {
        for (uint8_t i = 0; i < MAX_STEPS; ++i) {
            note[i] = DEFAULT_NOTE;
            velocity[i] = DEFAULT_VELOCITY;
            gate[i] = DEFAULT_GATE_PERCENT;
            nudge[i] = 0;
            probability[i] = DEFAULT_PROBABILITY;
        }
    }
};

}  // namespace oc::note::sequencer
