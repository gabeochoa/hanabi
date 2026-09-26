#pragma once

namespace hanabi::line_spacing {

inline constexpr int kDefault = 100;
inline constexpr int kMin = 80;
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
