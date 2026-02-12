#pragma once

#include <cstdint>

namespace oc::note::clock {

/**
 * @brief Internal clock placeholder (v0)
 *
 * v0 goal: compile/test skeleton. Tick generation will be implemented in Phase 2.
 */
class InternalClock {
public:
    void setPlaying(bool playing) { playing_ = playing; }
    void setBpm(float bpm) { bpm_ = bpm; }

    void reset() {
        tick_ = 0;
        last_ms_ = 0;
    }

    void update(uint32_t nowMs) {
        // v0 skeleton: monotonic tick while playing
        if (!playing_) {
            last_ms_ = nowMs;
            return;
        }
        if (nowMs != last_ms_) {
            ++tick_;
            last_ms_ = nowMs;
        }
    }

    uint32_t tick() const { return tick_; }
    float bpm() const { return bpm_; }
    bool isPlaying() const { return playing_; }

private:
    bool playing_ = false;
    float bpm_ = 120.0f;
    uint32_t tick_ = 0;
    uint32_t last_ms_ = 0;
};

}  // namespace oc::note::clock
