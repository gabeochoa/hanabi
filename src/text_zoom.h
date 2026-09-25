#pragma once

#include <cmath>

namespace hanabi::text_zoom {

inline constexpr double kDefault = 1.0;
inline constexpr double kMin = 0.7;
inline constexpr double kMax = 2.0;
inline constexpr double kStep = 0.1;

inline double clamp(double scale) {
    if (scale < kMin) return kMin;
    if (scale > kMax) return kMax;
    return scale;
}

inline double snap(double scale) { return clamp(std::round(scale / kStep) * kStep); }

inline double sanitize(double stored) {
    if (!std::isfinite(stored) || stored <= 0.0) return kDefault;
    return clamp(stored);
}

inline double zoomed_in(double from) { return snap(sanitize(from) + kStep); }

inline double zoomed_out(double from) { return snap(sanitize(from) - kStep); }

inline bool same(double a, double b) { return std::fabs(a - b) < kStep * 0.25; }

}
