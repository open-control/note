#include "StepSequencerPlaybackRegion.hpp"

namespace oc::note::sequencer {

bool tryResolvePlaybackOrdinal(const StepSequencerPlaybackRegion& region,
                               uint32_t ordinal,
                               StepSequencerPlaybackPosition& output) {
    if (!region.isValid()) {
        return false;
    }

    StepSequencerPlaybackPosition resolved{};
    resolved.ordinal = ordinal;

    const uint32_t preludeLength = region.preludeLength();
    if (ordinal < preludeLength) {
        resolved.stepIndex = static_cast<uint8_t>(region.playStart + ordinal);
        resolved.inPrelude = true;
        output = resolved;
        return true;
    }

    const uint32_t loopOrdinal = ordinal - preludeLength;
    const uint32_t loopLength = region.loopLength();
    resolved.loopCycleIndex = loopOrdinal / loopLength;
    resolved.loopOffset = static_cast<uint8_t>(loopOrdinal % loopLength);
    resolved.stepIndex = static_cast<uint8_t>(region.loopStart + resolved.loopOffset);
    resolved.atLoopStart = resolved.loopOffset == 0;
    output = resolved;
    return true;
}

bool tryResolvePlaybackTick(const StepSequencerPlaybackRegion& region,
                            uint32_t tick,
                            uint16_t ticksPerStep,
                            StepSequencerPlaybackTickPosition& output) {
    if (!region.isValid() || ticksPerStep == 0) {
        return false;
    }

    const uint32_t ordinal = tick / static_cast<uint32_t>(ticksPerStep);
    StepSequencerPlaybackPosition playback{};
    if (!tryResolvePlaybackOrdinal(region, ordinal, playback)) {
        return false;
    }

    StepSequencerPlaybackTickPosition resolved{};
    resolved.playback = playback;
    resolved.ticksPerStep = ticksPerStep;
    resolved.stepStartTick = ordinal * static_cast<uint32_t>(ticksPerStep);
    resolved.nextStepTick =
        static_cast<uint64_t>(resolved.stepStartTick) + ticksPerStep;
    resolved.tickOffset = static_cast<uint16_t>(tick - resolved.stepStartTick);
    resolved.atStepBoundary = resolved.tickOffset == 0;
    output = resolved;
    return true;
}

}  // namespace oc::note::sequencer
