#pragma once

#include <cstdint>

namespace oc::note::sequencer {

/**
 * @brief Persistent musical bounds of one root step-sequencer pattern.
 *
 * The interval before loopStart is played once, starting at playStart. The
 * half-open interval [loopStart, loopEnd) then repeats indefinitely.
 */
struct StepSequencerPlaybackRegion {
    static constexpr uint8_t MIN_CONTENT_LENGTH = 1;
    static constexpr uint8_t MAX_CONTENT_LENGTH = 128;
    static constexpr uint8_t DEFAULT_CONTENT_LENGTH = 8;

    uint8_t contentLength = DEFAULT_CONTENT_LENGTH;
    uint8_t playStart = 0;
    uint8_t loopStart = 0;
    uint8_t loopEnd = DEFAULT_CONTENT_LENGTH;

    static constexpr StepSequencerPlaybackRegion fullLength(uint8_t contentLength) {
        return StepSequencerPlaybackRegion{
            contentLength,
            0,
            0,
            contentLength,
        };
    }

    constexpr bool isValid() const {
        return contentLength >= MIN_CONTENT_LENGTH &&
               contentLength <= MAX_CONTENT_LENGTH &&
               playStart <= loopStart &&
               loopStart < loopEnd &&
               loopEnd <= contentLength;
    }

    constexpr uint8_t preludeLength() const {
        return isValid() ? static_cast<uint8_t>(loopStart - playStart) : 0;
    }

    constexpr uint8_t loopLength() const {
        return isValid() ? static_cast<uint8_t>(loopEnd - loopStart) : 0;
    }
};

/**
 * @brief Musical position resolved from an absolute playback ordinal.
 *
 * ordinal is the number of root steps elapsed since transport start. During
 * the one-shot prelude loopCycleIndex and loopOffset are zero. Once the loop
 * begins, loopCycleIndex is zero for its first traversal and increments only
 * when crossing Loop End back to Loop Start.
 */
struct StepSequencerPlaybackPosition {
    uint32_t ordinal = 0;
    uint32_t loopCycleIndex = 0;
    uint8_t stepIndex = 0;
    uint8_t loopOffset = 0;
    bool inPrelude = false;
    bool atLoopStart = false;
};

/**
 * @brief Tick-domain projection of a playback position for resynchronization.
 *
 * nextStepTick is 64-bit so the boundary immediately after UINT32_MAX remains
 * representable without wrapping. The engine can then choose its own transport
 * wrap policy explicitly.
 */
struct StepSequencerPlaybackTickPosition {
    uint64_t nextStepTick = 0;
    StepSequencerPlaybackPosition playback{};
    uint32_t stepStartTick = 0;
    uint16_t tickOffset = 0;
    uint16_t ticksPerStep = 1;
    bool atStepBoundary = true;
};

static_assert(sizeof(StepSequencerPlaybackPosition) <= 12,
              "Playback position must remain a small hot-path value");
static_assert(sizeof(StepSequencerPlaybackTickPosition) <= 32,
              "Tick projection must remain stack-bounded");

/**
 * @brief Resolve an absolute root-step ordinal into the musical region.
 *
 * Returns false for an invalid region and leaves output untouched.
 */
[[nodiscard]] bool tryResolvePlaybackOrdinal(
    const StepSequencerPlaybackRegion& region,
    uint32_t ordinal,
    StepSequencerPlaybackPosition& output);

/**
 * @brief Resolve an arbitrary transport tick, including mid-step resyncs.
 *
 * Returns false for an invalid region or a zero tick division and leaves
 * output untouched.
 */
[[nodiscard]] bool tryResolvePlaybackTick(
    const StepSequencerPlaybackRegion& region,
    uint32_t tick,
    uint16_t ticksPerStep,
    StepSequencerPlaybackTickPosition& output);

}  // namespace oc::note::sequencer
