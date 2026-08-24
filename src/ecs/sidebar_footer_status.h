#pragma once

// The live half of the sidebar footer: an activity light fused to the session
// count, right-aligned against the footer's action cluster.
//
// This is where hanabi's bottom-of-window status went. hanabi used to paint a
// full-width strip across the floor of the MAIN pane (`status_bar_system.h`,
// deleted) carrying "N blocked on you" left and "N sessions" right. Puffin has
// no such surface -- its root is an HStack of sidebar beside content
// (`MainWindowShell.swift:210`) and its only bottom-anchored chrome is
// `SidebarColumn.sidebarFooter`, a 28pt safeAreaInset on the sidebar column
// alone -- so the strip cost hanabi's composer 26px of height that Puffin's
// keeps, and put every row of the composer band against the wrong reference
// row.
//
// The count, the light and the HANABI_DEBUG transport label moved here. The
// "N blocked on you" phrase did not,
// because it was the second and third rendering of one fact: the sidebar
// already badges it on the Blocked row (as Puffin does -- `attentionCounts` in
// `HomeSessionList.swift`), and `menubar.mm`'s `status_for_blocked` states it
// in words on the macOS menu bar whether or not the window is on screen.
//
// And the duplicate had already drifted. The strip counted `tag == Blocked`
// and said 3; the badge counts `ecs::model::in_blocked_view` -- Blocked OR
// Failed, Puffin's own `case .blocked` rule, the one the reference's badge of
// six confirms -- and said 6. Two numbers for one fact, one frame, 800px
// apart. Full argument: docs/visual-parity/REFERENCE.md, "The status bar".

#include <cstdlib>
#include <string>

#include "../ui/theme.h"
#include "components.h"
#include "sidebar_footer_geometry.h"
#include "ui_imports.h"

namespace ecs::footer_status {

// True while ANY network fetch is in flight -- list / transcript / load-older
// / send / stream / live refetch, including background per-tab subscription
// refetches. Disk-cache reads are deliberately not counted: this is a
// hard-drive-style LED for NETWORK work. Lifted verbatim from the deleted
// strip so the light still means what it always meant.
inline bool network_active(const AppComponent& app) {
    static const bool liveDemo = [] {
        const char* v = std::getenv("HANABI_LIVE_DEMO");
        return v && *v && std::string(v) != "0";
    }();
    if (liveDemo) return true;
    for (const auto& kv : app.liveSubs)
        if (kv.second.pending) return true;
    return app.listPending || app.transcriptPending || app.loadingOlder ||
           app.livePending || app.sendPending || app.streamCollecting ||
           app.kickoffPending || app.settingsPending || app.authBeginPending;
}

// The band's geometry constants and its two snapped placements live in
// sidebar_footer_geometry.h, which has no graphics in it so a unit test can
// call them: `tests/unit/test_footer_geometry.cpp`.

// The dev-only transport label. The deleted strip appended "  ·  backend: X"
// to its left cluster under HANABI_DEBUG and nothing else in the UI prints
// `backend_label`, so it comes with the rest of the strip rather than being
// lost with it. Read once; a hard no-op returning false in every normal run,
// which is every capture and every user.
inline bool debug_backend() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_DEBUG");
        return v && *v;
    }();
    return on;
}

inline void render(UIContext<InputAction>& ctx, Entity& parent,
                   const AppComponent& app, float panelW, float top,
                   float bandH) {
    if (panelW < kMinPanelW) return;

    if (debug_backend()) {
        // Beside the version label, which is the other thing in this band that
        // is the app talking about itself, one 8px gap past the version's ink.
        const std::string be = "backend: " + app.backend_label;
        div(ctx, mk(parent, 18),
            ComponentConfig{}
                .with_label(be)
                .with_size(ComponentSize{
                    pixels(theme::text_px(be, theme::type::SM) + kAhTextInset),
                    pixels(bandH)})
                .with_absolute_position()
                .with_translate(label_box_x(
                                    kFooterPadX
                                    + theme::text_px("v0.0.0", theme::type::SM)
                                    + 8.0f),
                                top)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(2)
                .with_debug_name("sb_backend_label"));
    }

    const std::string count = std::to_string(app.sessions.size()) + " sessions";
    const float textW = theme::text_px(count, theme::type::SM);
    const float textRight = panelW - kActionsLeft - kActionGap;
    const float textLeft = textRight - textW;

    // LEFT-aligned in a slot sized to the text PLUS afterhours' hardcoded 5px
    // label inset, rather than right-aligned in a wide box: the inset applies
    // on EVERY alignment with no way off (gap #84), so a right-aligned label
    // lands 5px shy of its box and the light beside it ends up touching the
    // digits. Put the inset on the left, where nothing is flush with it, and
    // the ink's right edge is the slot's. Same trick the smart-view counts use.
    //
    // `text_faint` over `text_secondary` is a MEASURED decision for this band
    // -- see the note over render_footer in sidebar_system.h.
    div(ctx, mk(parent, 16),
        ComponentConfig{}
            .with_label(count)
            .with_size(ComponentSize{pixels(textW + kAhTextInset), pixels(bandH)})
            .with_absolute_position()
            .with_translate(label_box_x(textLeft), top)
            .with_transparent_bg()
            .with_custom_text_color(theme::text_faint())
            .with_font_size(theme::type::SM)
            .with_alignment(TextAlignment::Left)
            .with_roundness(0.0f)
            .with_render_layer(2)
            .with_debug_name("sb_session_count"));

    // The light hugs the count's MEASURED left edge rather than a fixed slot,
    // which is why the pair reads as one object instead of a dot adrift in the
    // band. Same correction the strip's right cluster already carried.
    //
    // Both of its coordinates are SNAPPED to whole pixels, and that is what
    // makes it round. afterhours never rounds a position -- grid snapping is
    // off in hanabi (`preload.cpp`) and only ever snapped SIZES anyway -- so a
    // fractional origin reaches the rasterizer, which has no antialiasing
    // (gap #92) and resolves a 6px box at y=932.5 into FIVE rows. The shipped
    // light was 6 wide and 5 tall: an ellipse, because x happened to land
    // whole and y was the band's odd 10.5px inset. Its x is worse than a
    // constant -- it is derived from `text_px` of a string whose length
    // changes with the catalog, so which axis loses a pixel depends on how
    // many sessions there are. Gap #130.
    //
    // floor, not round: the band's own centre is y935 and the count's ink
    // centre is y934, because a label's ascent is taller than its descent.
    // Flooring the half lands the light on the TEXT rather than on the box.
    const float dotY = dot_y(top, bandH);
    const float dotX = dot_x(textLeft);
    div(ctx, mk(parent, 17),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(kDot), pixels(kDot)})
            .with_absolute_position()
            .with_translate(dotX, dotY)
            .with_custom_background(network_active(app) ? theme::status_active()
                                                        : theme::text_faint())
            .with_roundness(1.0f)
            .with_render_layer(2)
            .with_debug_name("sb_activity_dot"));
}

}  // namespace ecs::footer_status
