#pragma once

// ---------------------------------------------------------------------------
// The minimap: a thin rail down the right edge of a long transcript, one mark
// per thing in it, click a mark to go there.
//
// It reads the item list the transcript has ALREADY built — the same vector of
// measured items that virtualization spends and that the render walks — rather
// than walking the messages a second time. A second walk is a second ordering,
// and the two would drift the first time either side learned a new row type:
// the rail would point at the wrong message and nothing would say so.
//
// This file holds the parts that are only about the rail — what colour a kind
// of item is, and how tall its slot is — so the transcript keeps only the
// wiring. The marks are DRAWN (on_draw_fg), not typed: the font has no dot
// glyph worth relying on (afterhours_gaps.md #48).
// ---------------------------------------------------------------------------

#include <algorithm>

#include "minimap_marks.h"
#include "minimap_scrub.h"
#include "theme.h"

namespace hanabi::minimap {

inline theme::Color colour_of(Mark m) {
    switch (m) {
        case Mark::Machinery: return theme::role_tool();
        case Mark::Reply: return theme::role_assistant();
        case Mark::Ask: return theme::role_user();
        case Mark::Notice: return theme::accent();
        case Mark::Note: return theme::text_faint();
    }
    return theme::text_faint();
}

// Draw one mark inside its slot: a small rounded dot, centred, never thinner
// than kMinDotH so a one-line item is still visible on the rail — and never
// taller than kMaxDotH, so a long answer next to a short one leaves a gap
// between them instead of the two merging into one bar.
inline void draw_mark(RectangleType slot, Mark m, bool wide) {
    const float h =
        std::clamp(slot.height - 1.5f, kMinDotH, kMaxDotH);
    const float w = wide ? kDotW + 3.0f : kDotW;
    const float x = slot.x + (slot.width - w) * 0.5f;
    const float y = slot.y + (slot.height - h) * 0.5f;
    afterhours::draw_rectangle_rounded(RectangleType{x, y, w, h}, 0.4f, 4,
                                       colour_of(m));
}

// The scrubber: where the viewport currently sits, as a band over the rail.
// Drawn from the scroll view's own numbers, so it tracks the real scroll
// rather than a copy of it.
// Drawn as a bracket rather than a filled band: the marks it sits over are
// solid, and a translucent fill behind them would say nothing (the UI fill
// pipeline does not alpha-blend — afterhours_gaps.md #13). Two rules and two
// edges read clearly against a dense rail.
//
// The band's height and position come from minimap_scrub.h rather than from
// arithmetic spelled out here, because dragging along the rail has to be the
// exact inverse of this drawing. Two copies of the mapping would agree at the
// top of a drag and disagree by the height of the band at the bottom.
inline void draw_scrubber(RectangleType rail, float scrollY, float viewH,
                          float contentH) {
    if (contentH <= 0.0f) return;
    const float h = scrubber_h(rail.height, viewH, contentH);
    const float y = rail.y + scrubber_top(rail.height, scrollY, viewH, contentH);
    const theme::Color c = theme::text_secondary();
    const auto line = [&](float x0, float y0, float x1, float y1) {
        afterhours::draw_line_ex(afterhours::vec2{x0, y0},
                                 afterhours::vec2{x1, y1}, 1.5f, c);
    };
    line(rail.x, y, rail.x + rail.width, y);
    line(rail.x, y + h, rail.x + rail.width, y + h);
    line(rail.x + 0.5f, y, rail.x + 0.5f, y + h);
    line(rail.x + rail.width - 0.5f, y, rail.x + rail.width - 0.5f, y + h);
}

}  // namespace hanabi::minimap
