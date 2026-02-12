#include "InternalClock.hpp"

namespace oc::note::clock {

uint32_t InternalClock::tickPeriodUs_() const {
    const float bpm = bpm_;
    if (!(bpm > 0.0f)) return 0;

    // period_us = 60s / (BPM * PPQN)
    const double denom = static_cast<double>(bpm) * static_cast<double>(PPQN);
    if (!(denom > 0.0)) return 0;

    const double us = 60'000'000.0 / denom;
    uint32_t period = static_cast<uint32_t>(us + 0.5);
    if (period == 0) period = 1;
    return period;
}

void InternalClock::update(uint32_t nowMs) {
    if (!initialized_) {
        initialized_ = true;
        last_ms_ = nowMs;
        was_playing_ = playing_;
        if (playing_) {
            tick_ = 0;
            accum_us_ = 0;
        }
        return;
    }

    // Detect play start and reset tick domain.
    if (playing_ && !was_playing_) {
        tick_ = 0;
        accum_us_ = 0;
        last_ms_ = nowMs;
        was_playing_ = true;
        return;
    }
    was_playing_ = playing_;

    const uint32_t deltaMs = nowMs - last_ms_;
    last_ms_ = nowMs;

    if (!playing_) return;

    const uint32_t periodUs = tickPeriodUs_();
    if (periodUs == 0) return;

    accum_us_ += static_cast<uint64_t>(deltaMs) * 1000ULL;
    if (accum_us_ < periodUs) return;

    const uint64_t inc = accum_us_ / periodUs;
    tick_ += static_cast<uint32_t>(inc);
    accum_us_ -= inc * periodUs;
}

}  // namespace oc::note::clock
