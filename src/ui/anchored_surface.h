#pragma once

#include <algorithm>
#include <vector>
#include <cstddef>

#include "secondary_surface_geometry.h"

namespace hanabi::surface {

inline constexpr float kEdgeInset = 4.0f;

struct MenuMetrics {
    float width = 176.0f;
    float header_h = 28.0f;
    float row_h = kMenuRowH;
    // A group separator's slot: a hairline with breathing room, not a row.
    // The reference's menus draw ~9pt around a 1px rule; a full row per
    // separator made a five-group menu 504px tall and pinned every menu to
    // the top edge of a 760px window.
    float separator_h = 9.0f;
    float pad = 4.0f;

    // Heights by row, from which rows are separators. `separators[i]` true =
    // row i takes separator_h; absent or false = row_h.
    float height_for(std::size_t items, const std::vector<bool>& separators = {}) const {
        float h = header_h + pad * 2.0f;
        for (std::size_t i = 0; i < items; ++i)
            h += (i < separators.size() && separators[i]) ? separator_h : row_h;
        return h;
    }

    float row_width() const { return std::max(0.0f, width - pad * 2.0f); }

    float row_x(float menuX) const { return menuX + pad; }

    float row_y(float menuY, std::size_t index, const std::vector<bool>& separators = {}) const {
        float y = menuY + pad + header_h;
        for (std::size_t i = 0; i < index; ++i)
            y += (i < separators.size() && separators[i]) ? separator_h : row_h;
        return y;
    }

    float row_height(std::size_t index, const std::vector<bool>& separators = {}) const {
        return (index < separators.size() && separators[index]) ? separator_h : row_h;
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
