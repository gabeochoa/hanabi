#pragma once

// ---------------------------------------------------------------------------
// The rail's coordinate system, on its own.
//
// The minimap draws a band over the rail showing where the viewport sits
// (minimap::draw_scrubber). Dragging along the rail is the INVERSE of that
// drawing: the reader grabs the band and the transcript has to end up wherever
// the band went. Those two mappings have to be exact inverses of each other or
// the band slides out from under the cursor over a long drag — the failure
// every hand-rolled scrollbar has, and the one that cannot be seen in a
// screenshot because it only appears while the button is held.
//
// So the forward map lives here, the drawing calls it, and the drag calls the
// inverse. One pair of functions, one place where the arithmetic can be wrong.
//
// Pure and graphics-free, like pane_state.h and sidebar_footer_geometry.h: no
// afterhours, no RectangleType, no theme — so a unit test can drive the whole
// gesture headlessly (tests/unit/test_minimap_scrub.cpp) instead of the
// property being reachable only through a screenshot of a held mouse button.
// ---------------------------------------------------------------------------

#include <algorithm>

namespace hanabi::minimap {

// The band never shrinks below this share of the rail, however long the
// thread: a two-pixel band is not a thing anyone can point at. It is the same
// clamp the drawing has always used, named so the inverse can honour it.
inline constexpr float kMinScrubberFrac = 0.04f;

// How tall the band is on a `railH` rail, for a `viewH` viewport over `contentH`
// of content.
inline float scrubber_h(float railH, float viewH, float contentH) {
    if (railH <= 0.0f || contentH <= 0.0f) return 0.0f;
    return railH * std::clamp(viewH / contentH, kMinScrubberFrac, 1.0f);
}

// How far the band can travel: the rail less the band itself. This — not the
// rail height — is the denominator the drag has to divide by, and getting it
// wrong is the drift described at the top of this file.
inline float scrubber_travel(float railH, float viewH, float contentH) {
    return std::max(0.0f, railH - scrubber_h(railH, viewH, contentH));
}

// FORWARD: a scroll offset -> the band's top, as an offset from the rail's top.
inline float scrubber_top(float railH, float scrollY, float viewH,
                          float contentH) {
    const float denom = contentH - viewH;
    if (denom <= 0.0f) return 0.0f;
    return scrubber_travel(railH, viewH, contentH) *
           std::clamp(scrollY / denom, 0.0f, 1.0f);
}

// Rail pixels -> content pixels. One pixel of cursor travel is worth this many
// pixels of scroll, which is what makes a drag down the whole rail cover the
// whole thread exactly (asserted in the unit test).
inline float scrub_scale(float railH, float viewH, float contentH) {
    const float travel = scrubber_travel(railH, viewH, contentH);
    const float range = contentH - viewH;
    if (travel <= 0.0f || range <= 0.0f) return 0.0f;
    return range / travel;
}

// INVERSE, and the one the gesture calls: where the transcript should be after
// the cursor has moved `dy` pixels down the rail from wherever the drag was
// anchored.
//
// Relative to the anchor rather than absolute from the cursor, on purpose. The
// press that starts a drag has ALREADY jumped the transcript to the item it
// landed on (that is the click, and it still works), so an absolute map would
// jump a second time the moment the cursor crossed the drag threshold — two
// jumps for one gesture. Anchoring means the first live frame moves nothing
// and the band keeps whatever grip on the cursor the press gave it, which is
// how every scrollbar thumb behaves.
//
// Not clamped at the top end here: the caller's clamp_scroll() owns the real
// content extent, which is the laid-out one and not necessarily `contentH`.
inline float scrub_offset(float anchorOffset, float dy, float railH,
                          float viewH, float contentH) {
    return std::max(0.0f,
                    anchorOffset + dy * scrub_scale(railH, viewH, contentH));
}

// The gesture itself, as data: what survives between frames while the button
// is held. Lives on the pane (ecs::model::PaneState) because the rail — like
// every other widget here — is rebuilt from scratch every frame and cannot
// hold anything.
//
// Three fields and no per-mark anything: this is the whole reason the drag
// costs the same in a thread of four hundred messages as in one of twelve.
// The marks are already one widget each and ~4.6 heap allocations apiece per
// frame (afterhours_gaps.md #138); a gesture that asked each of them whether
// it was being dragged would have made that worse in the one situation — a
// held button — where the app has the least room to spare.
struct DragState {
    bool armed = false;   // a press landed on the rail; may still be a click
    bool live = false;    // and it has moved far enough to be a drag
    float anchorY = 0.0f;       // cursor y when it became a drag
    float anchorOffset = 0.0f;  // scroll offset at that moment
};

}  // namespace hanabi::minimap
