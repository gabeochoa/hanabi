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

// The pixels added BETWEEN two lines of a message at `percent`, for a face
// whose 100% line is `linePx` tall (the reference's MessageLineSpacing
// .points(percent:font:), D123401188). Applied as a gap between lines, never
// as a taller line: a taller line puts its extra room above the glyphs, so a
// one-line message would grow a gap over its text and a bubble would lose its
// 100% margins.
inline float gap_px(float linePx, int percent) {
    const int extra = clamp(percent) - kDefault;
    if (extra <= 0) return 0.0f;
    const float g = linePx * static_cast<float>(extra) / 100.0f;
    return static_cast<float>(static_cast<int>(g + 0.5f));
}

// The height of `lines` lines at `percent`: every line at its 100% pitch, the
// extra room only between them, so one line is as tall as it is at 100%.
inline float block_px(int lines, float linePx, int percent) {
    if (lines < 1) lines = 1;
    return static_cast<float>(lines) * linePx +
           static_cast<float>(lines - 1) * gap_px(linePx, percent);
}

}
