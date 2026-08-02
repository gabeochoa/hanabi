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
// The thumb is now DRAGGABLE (app-side, still no vendor edits) — see the drag
// section below. Clicking + dragging the thumb scrolls the content; clicking
// the track above/below the thumb pages toward the click. It still accurately
// reflects scroll position + content ratio and updates every frame as the wheel
// scrolls. Auto-hides when content fits (nothing to scroll).

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>

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
        float thumbY = trackY + frac * (trackH - thumbH);

        // ---- Drag / paging (app-side interaction; afterhours has no built-in
        // draggable scrollbar — gap #26). on_draw_fg runs during RENDER, after
        // layout, so mutating scroll_offset here takes effect NEXT frame — the
        // same pattern main_pane_system.h uses for jump-to-bottom. We only ever
        // write scroll_offset while THIS bar is actively dragging, so we never
        // fight the wheel or other bars.
        //
        // Drag math (all in the viewport's screen-space y):
        //   The thumb's top can travel over [trackY, trackY + (trackH-thumbH)].
        //   That travel range maps linearly onto scroll_offset.y in
        //   [0, maxScroll]. On press we remember grabDY = mouseY - thumbY (where
        //   inside the thumb the user grabbed). While dragging, the thumb top
        //   should follow the cursor keeping that grab point fixed:
        //       desiredThumbTop = mouseY - grabDY
        //   Convert back to a scroll fraction, then to an offset:
        //       frac = (desiredThumbTop - trackY) / (trackH - thumbH)   [0..1]
        //       scroll_offset.y = frac * maxScroll
        //   clamp_scroll() re-clamps against the live content/viewport sizes.
        struct DragState {
            bool dragging = false;
            float grabDY = 0.0f;   // cursor y offset from thumb top at grab
            bool prevDown = false; // per-bar mouse-button edge detect
        };
        static std::unordered_map<afterhours::EntityID, DragState> s_drag;
        // Only ONE scrollbar may drag at a time.
        static afterhours::EntityID s_activeDrag = 0;

        const auto mp = afterhours::input::get_mouse_position();
        const float mx = mp.x;
        const float my = mp.y;
        const bool down = afterhours::input::is_mouse_button_down(0);

        const float travel = trackH - thumbH;  // > 0 (trackH > kMinThumb, and
                                                // thumbH clamped <= trackH)
        DragState& ds = s_drag[scrollId];
        // Edge-detect per-bar so multiple scroll panels in one frame each get a
        // correct just-pressed (a single shared static would only be right for
        // whichever bar rendered first).
        const bool justPressed = down && !ds.prevDown;
        ds.prevDown = down;

        const bool overThumb = mx >= trackX && mx <= trackX + kBarW &&
                               my >= thumbY && my <= thumbY + thumbH;
        const bool overTrack = mx >= trackX && mx <= trackX + kBarW &&
                               my >= trackY && my <= trackY + trackH;

        if (justPressed && overThumb && s_activeDrag == 0) {
            // Begin dragging this thumb.
            ds.dragging = true;
            ds.grabDY = my - thumbY;
            s_activeDrag = scrollId;
        } else if (justPressed && overTrack && !overThumb && s_activeDrag == 0) {
            // Page toward the click (click above thumb → up a page, below →
            // down a page). One-shot; does not start a drag.
            auto& msv = ent.template get<HasScrollView>();
            const float page = view;  // one viewport per click
            if (my < thumbY)
                msv.scroll_offset.y -= page;
            else
                msv.scroll_offset.y += page;
            msv.clamp_scroll();
        }

        if (ds.dragging && s_activeDrag == scrollId) {
            if (down && travel > 0.5f) {
                const float desiredThumbTop = my - ds.grabDY;
                float dfrac =
                    std::clamp((desiredThumbTop - trackY) / travel, 0.0f, 1.0f);
                auto& msv = ent.template get<HasScrollView>();
                msv.scroll_offset.y = dfrac * maxScroll;
                msv.clamp_scroll();
                // Reflect the drag in THIS frame's paint (no 1-frame lag on the
                // thumb visual) by recomputing frac/thumbY from the new offset.
                frac = dfrac;
                thumbY = trackY + frac * travel;
            }
            if (!down) {  // release
                ds.dragging = false;
                if (s_activeDrag == scrollId) s_activeDrag = 0;
            }
        }

        const bool active = ds.dragging && s_activeDrag == scrollId;
        const bool hot = active || overThumb;

        // Track: a barely-there groove (text_faint @ very low alpha over the
        // viewport's own backdrop). theme::over() is required because the UI
        // fill pipeline can't alpha-blend (afterhours gaps #13/#15) — a raw
        // low-alpha color would render as a harsh opaque block.
        const theme::Color faint = theme::text_faint();
        const theme::Color backdrop = theme::panel_bg();
        const theme::Color trackCol =
            theme::over(theme::Color{faint.r, faint.g, faint.b, 18}, backdrop);
        // Brighter thumb while hovered/dragging as an affordance.
        const std::uint8_t thumbA = hot ? 220 : 150;
        const theme::Color thumbCol = theme::over(
            theme::Color{faint.r, faint.g, faint.b, thumbA}, backdrop);

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
