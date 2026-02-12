#pragma once

#include <cstdint>

#include "ClockConstants.hpp"

namespace oc::note::clock {

/**
 * @brief Internal clock (PPQN=24) for embedded-friendly timing
 *
 * - Uses ms timestamps (`nowMs`) provided by the host.
 * - Converts BPM + elapsed time into a monotonic tick counter.
 * - Resets tick to 0 on play start.
 */
class InternalClock {
public:
    void setPlaying(bool playing) { playing_ = playing; }
    void setBpm(float bpm) { bpm_ = bpm; }

    void reset() {
        playing_ = false;
        was_playing_ = false;
        initialized_ = false;
        bpm_ = 120.0f;
        tick_ = 0;
        last_ms_ = 0;
        accum_us_ = 0;
    }

    void update(uint32_t nowMs);

    uint32_t tick() const { return tick_; }
    float bpm() const { return bpm_; }
    bool isPlaying() const { return playing_; }

private:
    uint32_t tickPeriodUs_() const;

    bool playing_ = false;
    bool was_playing_ = false;
    bool initialized_ = false;
    float bpm_ = 120.0f;
    uint32_t tick_ = 0;
    uint32_t last_ms_ = 0;
    uint64_t accum_us_ = 0;
};

}  // namespace oc::note::clock
