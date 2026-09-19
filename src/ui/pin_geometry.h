#pragma once

#include <array>

namespace hanabi::pin_geometry {

struct Box {
    float x;
    float y;
    float w;
    float h;
};

inline constexpr float kGlyphW = 6.0f;
inline constexpr float kGlyphH = 10.0f;

inline std::array<Box, 5> rects(Box slot, float u, float scale) {
    const float cx = slot.x + u + kGlyphW * u * 0.5f;
    const float cy = slot.y + slot.h * 0.5f;
    const float s = u * scale;
    const float x = cx - kGlyphW * s * 0.5f;
    const float y = cy - kGlyphH * s * 0.5f;
    return {Box{x, y, 6 * s, 2 * s}, Box{x + s, y + 2 * s, 4 * s, 3 * s}, Box{x, y + 5 * s, 6 * s, 2 * s},
            Box{x + s, y + 7 * s, 4 * s, s}, Box{x + 2 * s, y + 8 * s, 2 * s, 2 * s}};
}

}
