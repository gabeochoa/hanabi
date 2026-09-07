#pragma once

#include <algorithm>
#include <cstddef>

#include "secondary_surface_geometry.h"

namespace hanabi::surface {

inline constexpr float kEdgeInset = 4.0f;

struct MenuMetrics {
    float width = 176.0f;
    float header_h = 28.0f;
    float row_h = kMenuRowH;
    float pad = 4.0f;

    float height_for(std::size_t items) const {
        return header_h + row_h * static_cast<float>(items) + pad * 2.0f;
    }

    float row_width() const { return std::max(0.0f, width - pad * 2.0f); }

    float row_x(float menuX) const { return menuX + pad; }

    float row_y(float menuY, std::size_t index) const {
        return menuY + pad + header_h + row_h * static_cast<float>(index);
    }

};

struct Placed {
    float x = 0.0f;
    float y = 0.0f;
    bool moved_x = false;
    bool moved_y = false;
};

inline Placed place_at(float wantX, float wantY, float w, float h,
                       float screenW, float screenH,
                       float inset = kEdgeInset) {
    const float maxX = std::max(inset, screenW - w - inset);
    const float maxY = std::max(inset, screenH - h - inset);
    Placed out;
    out.x = std::clamp(wantX, inset, maxX);
    out.y = std::clamp(wantY, inset, maxY);
    out.moved_x = out.x != wantX;
    out.moved_y = out.y != wantY;
    return out;
}

inline Placed place_submenu(const Rect& parent, float rowY, float w, float h,
                            float screenW, float screenH, float overlap = 4.0f,
                            float inset = kEdgeInset) {
    const float toRight = parent.x + parent.width - overlap;
    const float toLeft = parent.x + overlap - w;
    const bool fitsRight = toRight + w <= screenW - inset;
    const bool fitsLeft = toLeft >= inset;
    const float wantX = (fitsRight || !fitsLeft) ? toRight : toLeft;
    Placed out = place_at(wantX, rowY, w, h, screenW, screenH, inset);
    out.moved_x = out.moved_x || (!fitsRight && fitsLeft);
    return out;
}

enum class TipSide { Below, Above };

struct TipPlacement {
    float x = 0.0f;
    float y = 0.0f;
    TipSide side = TipSide::Below;
};

inline TipPlacement place_tooltip(const Rect& control, float w, float h,
                                  float screenW, float screenH,
                                  float gap = 6.0f, float inset = kEdgeInset) {
    const float below = control.y + control.height + gap;
    const float above = control.y - gap - h;
    TipPlacement out;
    out.side = (below + h <= screenH - inset || above < inset) ? TipSide::Below
                                                               : TipSide::Above;
    const float wantY = out.side == TipSide::Below ? below : above;
    const float wantX = control.x + (control.width - w) * 0.5f;
    const Placed placed = place_at(wantX, wantY, w, h, screenW, screenH, inset);
    out.x = placed.x;
    out.y = placed.y;
    return out;
}

}  // namespace hanabi::surface
