#pragma once

#include <cstdint>

namespace oc::note::sequencer {

struct StepBitMask128 {
    uint64_t low = 0;
    uint64_t high = 0;

    constexpr bool operator==(const StepBitMask128& other) const {
        return low == other.low && high == other.high;
    }

    constexpr bool operator!=(const StepBitMask128& other) const {
        return !(*this == other);
    }

    constexpr StepBitMask128 operator&(const StepBitMask128& other) const {
        return {.low = low & other.low, .high = high & other.high};
    }

    constexpr StepBitMask128 operator|(const StepBitMask128& other) const {
        return {.low = low | other.low, .high = high | other.high};
    }

    constexpr StepBitMask128 operator^(const StepBitMask128& other) const {
        return {.low = low ^ other.low, .high = high ^ other.high};
    }

    constexpr StepBitMask128 operator~() const {
        return {.low = ~low, .high = ~high};
    }

    constexpr StepBitMask128& operator&=(const StepBitMask128& other) {
        low &= other.low;
        high &= other.high;
        return *this;
    }

    constexpr StepBitMask128& operator|=(const StepBitMask128& other) {
        low |= other.low;
        high |= other.high;
        return *this;
    }

    constexpr StepBitMask128& operator^=(const StepBitMask128& other) {
        low ^= other.low;
        high ^= other.high;
        return *this;
    }

    static constexpr StepBitMask128 fromLower64(uint64_t value) {
        return {.low = value, .high = 0};
    }

    static constexpr StepBitMask128 prefixMask(uint8_t length) {
        if (length == 0) {
            return {};
        }
        if (length >= 128) {
            return {.low = ~uint64_t{0}, .high = ~uint64_t{0}};
        }
        if (length >= 64) {
            const uint8_t highBits = static_cast<uint8_t>(length - 64U);
            return {
                .low = ~uint64_t{0},
                .high = (highBits >= 64U) ? ~uint64_t{0} : ((uint64_t{1} << highBits) - 1ULL)
            };
        }
        return {
            .low = (uint64_t{1} << length) - 1ULL,
            .high = 0
        };
    }

    constexpr uint64_t lower64() const {
        return low;
    }

    constexpr bool any() const {
        return low != 0 || high != 0;
    }

    constexpr bool test(uint8_t index) const {
        if (index >= 128U) return false;
        if (index < 64U) return (low & (uint64_t{1} << index)) != 0;
        return (high & (uint64_t{1} << (index - 64U))) != 0;
    }

    constexpr void setBit(uint8_t index, bool enabled = true) {
        if (index >= 128U) return;
        if (index < 64U) {
            const uint64_t bit = uint64_t{1} << index;
            if (enabled) low |= bit;
            else low &= ~bit;
            return;
        }
        const uint64_t bit = uint64_t{1} << (index - 64U);
        if (enabled) high |= bit;
        else high &= ~bit;
    }

    constexpr void toggleBit(uint8_t index) {
        if (index >= 128U) return;
        if (index < 64U) {
            low ^= (uint64_t{1} << index);
            return;
        }
        high ^= (uint64_t{1} << (index - 64U));
    }
};

static_assert(sizeof(StepBitMask128) == 16, "StepBitMask128 must stay compact");

}  // namespace oc::note::sequencer
