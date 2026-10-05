#pragma once

// Fit to pane: the reading column grows with its pane (the reference's
// ReadingColumnRule, D122632847, now its default Transcript width). The column
// is the whole pane up to 768, never narrower than Comfortable draws it, then
// takes three quarters of every point the pane gains, up to 1280:
//
//     column = min(pane, 0.75 * pane + 192, 1280)
//
// A 1000 pane gets 942 (29 either side), a 1280 pane 1152, and anything past
// about 1451 stops at 1280. Pure.

#include <algorithm>

namespace hanabi::reading_column {

inline constexpr float kFloor = 768.0f;
inline constexpr float kSlope = 0.75f;
inline constexpr float kCap = 1280.0f;

inline float fit(float pane) {
    if (pane <= 0.0f) return 0.0f;
    return std::min({pane, kSlope * pane + (1.0f - kSlope) * kFloor, kCap});
}

}  // namespace hanabi::reading_column
