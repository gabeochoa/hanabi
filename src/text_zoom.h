#pragma once

#include <cmath>
#include <functional>

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
    if (!std::isfinite(stored)) return kDefault;
    return clamp(stored);
}

inline double zoomed_in(double from) { return snap(sanitize(from) + kStep); }

inline double zoomed_out(double from) { return snap(sanitize(from) - kStep); }

class PointScaleScope {
   public:
    PointScaleScope(float current, double zoom, std::function<void(float)> set)
        : prev_(current), set_(std::move(set)) {
        if (zoom != 1.0) set_(prev_ * static_cast<float>(zoom));
    }
    ~PointScaleScope() { set_(prev_); }
    PointScaleScope(const PointScaleScope&) = delete;
    PointScaleScope& operator=(const PointScaleScope&) = delete;

   private:
    float prev_;
    std::function<void(float)> set_;
};

}
