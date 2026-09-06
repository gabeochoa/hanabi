#pragma once

#include <algorithm>
#include <cstddef>

namespace hanabi::surface {

inline constexpr float kWindowMargin = 24.0f;
inline constexpr float kSheetCorner = 10.0f;
inline constexpr float kMenuCorner = 8.0f;
inline constexpr float kControlCorner = 7.0f;
inline constexpr float kSheetPadH = 24.0f;
inline constexpr float kSheetPadV = 20.0f;
inline constexpr float kFieldH = 38.0f;
// afterhours' text_input sizes its text from the field HEIGHT (field_h * 0.5)
// only when the caller states no font size of its own. Hanabi's fields stated
// none, so 19px is what they have always drawn -- until the library's default
// changed under them (upstream f328d13 made ComponentConfig::font_size default
// to the type scale and auto-fit opt-in), which silently shrank every field's
// text from 12px of ink to 9px. State the ratio instead of inheriting it.
inline constexpr float kFieldFontRatio = 0.5f;
inline constexpr float kMenuRowH = 36.0f;
inline constexpr float kButtonH = 34.0f;
inline constexpr float kTitleH = 26.0f;
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

inline float toast_width(float screenW, std::size_t textLength,
                         bool hasAction) {
    const float desired = std::clamp(120.0f + static_cast<float>(textLength) * 6.0f +
                                         (hasAction ? 76.0f : 0.0f),
                                     280.0f, 460.0f);
    return std::max(0.0f,
                    std::min(desired, screenW - kWindowMargin * 2.0f));
}

}  // namespace hanabi::surface
