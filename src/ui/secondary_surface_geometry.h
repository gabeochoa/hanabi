#pragma once

#include <algorithm>

namespace hanabi::surface {

inline constexpr float kWindowMargin = 24.0f;
inline constexpr float kSheetCorner = 10.0f;
inline constexpr float kMenuCorner = 8.0f;
inline constexpr float kControlCorner = 7.0f;
inline constexpr float kSheetPadH = 24.0f;
inline constexpr float kSheetPadV = 20.0f;
inline constexpr float kFieldH = 38.0f;
inline constexpr float kMenuRowH = 36.0f;
inline constexpr float kButtonH = 34.0f;
inline constexpr float kTitleH = 24.0f;
inline constexpr float kSubtitleH = 20.0f;
inline constexpr float kHeaderH = 52.0f;

struct Rect {
    float x;
    float y;
    float width;
    float height;
};

inline Rect centered(float screenW, float screenH, float wantedW,
                     float wantedH, float margin = kWindowMargin) {
    const float width = std::max(0.0f, std::min(wantedW, screenW - margin * 2.0f));
    const float height = std::max(0.0f, std::min(wantedH, screenH - margin * 2.0f));
    return Rect{(screenW - width) * 0.5f, (screenH - height) * 0.5f,
                width, height};
}

inline Rect top_centered(float screenW, float screenH, float wantedW,
                         float wantedH, float top = 88.0f,
                         float margin = kWindowMargin) {
    Rect out = centered(screenW, screenH, wantedW, wantedH, margin);
    out.y = std::clamp(top, margin, std::max(margin, screenH - margin - out.height));
    return out;
}

}  // namespace hanabi::surface
