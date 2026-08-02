#pragma once

// TEMPORARY app-side scroll indicator (afterhours gap #26).
//
// afterhours' HasScrollView tracks scroll_offset / content_size / viewport_size
// but never RENDERS a visible bar, so the user has no indication of scroll
// position or that more content exists below the fold. Until afterhours grows a
// built-in scrollbar (see afterhours_gaps.md #26), we paint a thin, muted,
// macOS-overlay-style indicator ourselves.
//
// Approach (temporary, no vendor edits): attach this as the scroll panel's
// on_draw_fg. The fg callback runs AFTER the panel's children are laid out and
// receives the panel's exact on-screen viewport rect, and we read the LIVE
// HasScrollView metrics off the entity by id at draw time (freshest values,
// post-layout). We paint a subtle track + a rounded thumb pinned to the INSIDE
// of the viewport's right edge. Callers reserve ~kBarReserve px of right
// padding on the content so text never runs under the bar; because content
// never reaches into that right strip, painting the bar in the panel's own fg
// (same layer as children) never overlaps text.
//
// v1 is an INDICATOR ONLY — it accurately reflects scroll position + content
// ratio and updates every frame as the wheel scrolls, but the thumb is not
// draggable. Auto-hides when content fits (nothing to scroll).

#include <algorithm>
#include <functional>

#include "../rl.h"
#include "theme.h"

namespace hanabi {

// Width of the reserved right strip / bar column, in px. Callers should add
// this much right padding to scroll content so text doesn't run under the bar.
inline constexpr float kBarReserve = 8.0f;

// Attach the returned callback via .with_on_draw_fg(...) on a scroll panel
// entity (one carrying afterhours::ui::HasScrollView). `scrollId` is that
// entity's id (scroll.ent().id) — we look the entity up at draw time so the
// metrics are the current frame's post-layout values.
inline std::function<void(RectangleType)>
scroll_indicator(afterhours::EntityID scrollId) {
    return [scrollId](RectangleType viewport) {
        using afterhours::ui::HasScrollView;
        auto opt = afterhours::EntityHelper::getEntityForID(scrollId);
        if (!opt.valid()) return;
        auto& ent = opt.asE();
        if (!ent.template has<HasScrollView>()) return;
        const auto& sv = ent.template get<HasScrollView>();

        const float content = sv.content_size.y;
        const float view = sv.viewport_size.y > 1.0f ? sv.viewport_size.y
                                                      : viewport.height;
        // Nothing to scroll → no bar (macOS overlay behavior).
        if (content <= view + 0.5f || view <= 1.0f) return;

        // Geometry (see brief):
        //   track height = viewport height
        //   thumb height = max(minThumb, view * view / content)
        //   thumb y      = (offset / (content - view)) * (track - thumb)
        constexpr float kMinThumb = 28.0f;
        constexpr float kBarW = 6.0f;   // visible bar width (< kBarReserve)
        constexpr float kInset = 2.0f;  // gap from the hard right edge
        constexpr float kPadTB = 3.0f;  // top/bottom breathing room on track

        const float trackX = viewport.x + viewport.width - kBarW - kInset;
        const float trackY = viewport.y + kPadTB;
        const float trackH = view - 2.0f * kPadTB;
        if (trackH <= kMinThumb) return;  // viewport too short to bother

        float thumbH = std::max(kMinThumb, trackH * (view / content));
        if (thumbH > trackH) thumbH = trackH;

        const float maxScroll = content - view;
        float frac = maxScroll > 0.5f
                         ? std::clamp(sv.scroll_offset.y / maxScroll, 0.0f, 1.0f)
                         : 0.0f;
        const float thumbY = trackY + frac * (trackH - thumbH);

        // Track: a barely-there groove (text_faint @ very low alpha over the
        // viewport's own backdrop). theme::over() is required because the UI
        // fill pipeline can't alpha-blend (afterhours gaps #13/#15) — a raw
        // low-alpha color would render as a harsh opaque block.
        const theme::Color faint = theme::text_faint();
        const theme::Color backdrop = theme::panel_bg();
        const theme::Color trackCol =
            theme::over(theme::Color{faint.r, faint.g, faint.b, 18}, backdrop);
        const theme::Color thumbCol =
            theme::over(theme::Color{faint.r, faint.g, faint.b, 150}, backdrop);

        afterhours::draw_rectangle_rounded(
            RectangleType{trackX, trackY, kBarW, trackH}, 1.0f, 8, trackCol);
        afterhours::draw_rectangle_rounded(
            RectangleType{trackX, thumbY, kBarW, thumbH}, 1.0f, 8, thumbCol);
    };
}

// Convenience: attach the scroll indicator as the entity's fg custom draw,
// AFTER the immediate-mode div() has been built (we need the entity id). Sets
// HasOnDraw.fg directly without disturbing any existing bg draw. Safe to call
// every frame — the callback is rebuilt with the (stable) entity id.
inline void attach_scroll_indicator(afterhours::Entity& e) {
    auto& hod = e.template addComponentIfMissing<afterhours::ui::HasOnDraw>();
    hod.fg = scroll_indicator(e.id);
}

} // namespace hanabi
