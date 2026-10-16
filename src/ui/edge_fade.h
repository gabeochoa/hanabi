#pragma once

#include <algorithm>

namespace hanabi::edge_fade {

inline constexpr float kFadeH = 18.0f;

struct Strength {
    float top = 0.0f;
    float bottom = 0.0f;
};

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

inline Strength strength(float offset, float content, float viewport,
                         float fadeH = kFadeH) {
    Strength s;
    if (viewport <= 0.0f || content <= viewport + 0.5f || fadeH <= 0.0f) return s;
    const float maxOffset = content - viewport;
    const float o = std::clamp(offset, 0.0f, maxOffset);
    s.top = clamp01(o / fadeH);
    s.bottom = clamp01((maxOffset - o) / fadeH);
    return s;
}

inline unsigned char alpha_for(float strength, unsigned char full = 255) {
    return static_cast<unsigned char>(clamp01(strength) * static_cast<float>(full) + 0.5f);
}

}
