#pragma once

namespace hanabi::line_spacing {

inline constexpr int kDefault = 100;
// The reference's range is 100-200% (D123401186): Line spacing only ever adds
// room between lines, and a stored value under 100% reads as 100%.
inline constexpr int kMin = 100;
inline constexpr int kMax = 200;
inline constexpr int kStep = 5;

inline int clamp(int percent) {
    if (percent < kMin) return kMin;
    if (percent > kMax) return kMax;
    return percent;
}

inline int stepped_up(int percent) { return clamp(clamp(percent) + kStep); }

inline int stepped_down(int percent) { return clamp(clamp(percent) - kStep); }

inline float factor(int percent) { return static_cast<float>(clamp(percent)) / 100.0f; }

}
