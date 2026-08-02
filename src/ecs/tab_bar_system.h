#pragma once

// VS Code-style closable content tabs across the top of the main pane.
// Each open thread is a Tab entity; the focused one carries an ActiveTab
// marker. Clicking a session row (handled in AppFlowSystem) opens a new tab or
// focuses the existing one; the transcript shows whatever the active tab
// points at. Close (×) removes a tab; Cmd+W closes the active tab.
//
// Mirrors floatinghotel/src/ecs/tab_bar_system.h.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../test_hooks.h"
#include "../util/format.h"
#include "tab_model.h"
#include "ui_imports.h"

namespace ecs {

namespace tab_colors {
// Three-level elevation ladder so tabs read as physical tabs, not a segmented
// toolbar. The review flagged the old two-token scheme (strip==inactive, only a
// ~15L active/inactive delta) as "near-invisible". We now use a monotonic
// triple that keeps its ordering in BOTH dark and light:
//
//              dark L   light L
//   strip_bg   sidebar_bg   19      231   deepest recessed chrome plane (L0-)
//   inactive   window_bg    24      238   recessed WELL, distinct from strip
//   active     panel_bg     34      255   RAISED surface == the content pane
//
// The ACTIVE tab fills with panel_bg — the exact color of the transcript/main
// pane directly below it (main_pane_system) — so it reads as one continuous
// raised surface "lifted out of" the strip (we also bridge the strip's bottom
// hairline under it, below). INACTIVE tabs no longer share the strip color:
// they sit one elevation step up from the strip as their own recessed wells, so
// each reads as a discrete tab even before you notice which is active. On hover
// an inactive tab gets a faint additive wash over its own fill (theme::over,
// gap #13 — a subtle tint, never a solid block).
inline afterhours::Color strip_bg() { return theme::sidebar_bg(); }
inline afterhours::Color tab_active() { return theme::panel_bg(); }
inline afterhours::Color tab_inactive() { return theme::window_bg(); }
inline afterhours::Color tab_hover() {
    return theme::over(theme::hover_bg(), theme::window_bg());
}
inline afterhours::Color tab_text() { return theme::text_secondary(); }
inline afterhours::Color tab_text_act() { return theme::text_primary(); }
inline afterhours::Color close_hover() {
    return theme::over(theme::hover_bg(), theme::panel_bg());
}
inline afterhours::Color border() { return theme::border(); }
inline afterhours::Color accent() { return theme::accent(); }
}  // namespace tab_colors

struct TabBarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layoutP = find_singleton<LayoutComponent>();
        auto* appP = find_singleton<AppComponent>();
        auto* stripP = find_singleton<TabStripComponent>();
        if (!layoutP || !appP || !stripP) return;
        auto& layout = *layoutP;
        auto& app = *appP;
        auto& strip = *stripP;

        // Cmd+W closes the active tab.
        bool cmdDown = afterhours::graphics::is_key_down(343) ||
                       afterhours::graphics::is_key_down(347);
        if (cmdDown && afterhours::graphics::is_key_pressed(87)) {  // W
            for (size_t i = 0; i < strip.tabOrder.size(); ++i) {
                auto opt = EntityHelper::getEntityForID(strip.tabOrder[i]);
                if (opt.valid() && opt->has<ActiveTab>()) {
                    close_tab(strip, app, strip.tabOrder[i], i, true);
                    break;
                }
            }
        }

        const auto& r = layout.tabStrip;
        if (r.height <= 0.0f || r.width <= 0.0f) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();

        // Strip background.
        div(ctx, mk(uiRoot, 900),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(tab_colors::strip_bg())
                .with_border_bottom(tab_colors::border(), pixels(1))
                .with_roundness(0.0f)
                .with_render_layer(6)
                .with_debug_name("tab_strip"));

        float tabX = r.x;
        float tabH = r.height;
        const float minW = 90.0f;
        const float maxWCap = 200.0f;
        // A small gap BETWEEN tabs (not a hairline divider): with the rounded
        // tops + distinct inactive fill, a gap reads cleaner than a rule and
        // makes each tab an obviously separate object. The gap shows the strip
        // background through it, so tabs float as discrete recessed wells.
        const float gap = 3.0f;
        // Right edge the tabs must never cross (the main-pane width). A tab or
        // its × drawn past this would bleed off the window frame.
        const float stripRight = r.x + r.width;

        // Cap total tab area so N tabs (plus the gaps between them) never
        // overflow the strip. If the tabs at their natural width would exceed
        // the available strip, shrink the per-tab max width so they share the
        // space evenly. Never let a tab go below minW — if even minW-width tabs
        // won't all fit, we stop drawing once we hit the right edge (below), so
        // nothing renders off-frame.
        // UNIFORM tab width: every tab gets the SAME width (Gabe's ask),
        // sized to share the strip evenly up to a comfortable cap, instead of
        // each tab sizing to its own label (which gave ragged widths).
        const size_t nTabs = strip.tabOrder.size();
        float uniformW = maxWCap;
        if (nTabs > 0) {
            float totalGap = gap * static_cast<float>(nTabs - 1);
            float perTab =
                (r.width - totalGap) / static_cast<float>(nTabs);
            uniformW = std::clamp(perTab, minW, maxWCap);
        }

        // The per-tab horizontal advance (a full slot: tab width + the gap).
        // Slot i's non-drag left edge is r.x + slotStride*i; its center is
        // r.x + slotStride*i + uniformW/2. Used both to lay tabs out and (in
        // the pure model) to compute the drop index while dragging.
        const float slotStride = uniformW + gap;

        // ---- Drag-to-reorder input (manual hit-testing, mirrors the strip's
        // own is_mouse_inside checks). We only ever *record* intent here; the
        // actual tabOrder mutation happens in model::reorder_tab on release. A
        // press becomes a real drag only after the cursor moves past
        // DRAG_THRESHOLD_PX, so a plain click still falls through to focus.
        auto index_of = [&](afterhours::EntityID id) -> size_t {
            for (size_t i = 0; i < strip.tabOrder.size(); ++i)
                if (strip.tabOrder[i] == id) return i;
            return strip.tabOrder.size();
        };
        // The close-button hit area for tab drawn at left edge x (matches the
        // × geometry below) — a press there must NOT start a drag.
        auto over_close = [&](float x, float tabW) {
            float closeW = 16.0f;
            float closeX = x + tabW - closeW - 5.0f;
            float closeY = r.y + (tabH - closeW) * 0.5f;
            return afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{closeX, closeY, closeW, closeW});
        };

        if (ctx.mouse.just_pressed && nTabs > 0) {
            // Which slot (non-drag layout) did we press over?
            float px = r.x;
            for (size_t i = 0; i < nTabs; ++i) {
                float w = uniformW;
                float room = stripRight - px;
                if (room < minW) break;
                if (px + w > stripRight) w = room;
                bool inside = afterhours::ui::is_mouse_inside(
                    ctx.mouse.pos, RectangleType{px, r.y, w, tabH});
                if (inside && !over_close(px, w)) {
                    strip.dragCandidate = strip.tabOrder[i];
                    strip.dragFromIndex = i;
                    strip.dragStartX = ctx.mouse.pos.x;
                    strip.dragCurX = ctx.mouse.pos.x;
                    strip.dragging = false;
                    break;
                }
                px += w + gap;
            }
        } else if (ctx.mouse.left_down && strip.has_drag_candidate()) {
            strip.dragCurX = ctx.mouse.pos.x;
            if (std::fabs(strip.dragCurX - strip.dragStartX) >
                TabStripComponent::DRAG_THRESHOLD_PX)
                strip.dragging = true;
        } else if (ctx.mouse.just_released && strip.has_drag_candidate()) {
            size_t from = index_of(strip.dragCandidate);
            if (strip.dragging && from < nTabs && nTabs > 1) {
                // Center-x of the dragged tab follows the cursor, clamped so it
                // never leaves the strip (matches the render-side clamp).
                float origCenter =
                    r.x + slotStride * static_cast<float>(from) + uniformW * 0.5f;
                float draggedLeftR =
                    (origCenter + (strip.dragCurX - strip.dragStartX)) -
                    uniformW * 0.5f;
                draggedLeftR = std::clamp(draggedLeftR, r.x,
                                          stripRight - uniformW);
                size_t to = model::compute_drop_index(
                    draggedLeftR + uniformW * 0.5f, r.x, slotStride, nTabs);
                model::reorder_tab(strip, from, to);
                // A drag never focuses/switches — consume the press so the
                // per-tab click branch below can't also fire.
                ctx.mouse.just_pressed = false;
            } else if (!strip.dragging && from < nTabs) {
                // Pure click (moved < threshold): preserve click-to-focus.
                auto o = EntityHelper::getEntityForID(strip.dragCandidate);
                if (o.valid() && o->has<Tab>() && !o->has<ActiveTab>())
                    switch_to_tab(app, o.asE());
            }
            strip.clear_drag();
        }

        // Recompute after a possible reorder (tabOrder / index may have moved).
        const bool dragging = strip.dragging && strip.has_drag_candidate();
        const size_t dragFrom =
            dragging ? index_of(strip.dragCandidate) : strip.tabOrder.size();
        // While dragging, the dragged tab's live center-x (clamped to strip).
        float draggedLeft = 0.0f;
        size_t dropIndex = dragFrom;
        if (dragging && dragFrom < strip.tabOrder.size()) {
            float origCenter = r.x + slotStride * static_cast<float>(dragFrom) +
                               uniformW * 0.5f;
            float draggedCenter =
                origCenter + (strip.dragCurX - strip.dragStartX);
            // Clamp so the dragged tab never leaves the strip.
            float minLeft = r.x;
            float maxLeft = stripRight - uniformW;
            draggedLeft = draggedCenter - uniformW * 0.5f;
            draggedLeft = std::clamp(draggedLeft, minLeft, maxLeft);
            dropIndex = model::compute_drop_index(draggedLeft + uniformW * 0.5f,
                                                  r.x, slotStride,
                                                  strip.tabOrder.size());
        }

        // Precompute each tab's render left-edge X. Non-dragging: the natural
        // left-to-right slots. Dragging: the OTHER tabs reflow to open a gap at
        // dropIndex (so you can see where the dragged tab will land), and the
        // dragged tab itself is positioned at draggedLeft (drawn last, raised).
        std::vector<float> renderX(strip.tabOrder.size(), 0.0f);
        {
            float x = r.x;
            for (size_t i = 0; i < strip.tabOrder.size(); ++i) {
                if (dragging && i == dragFrom) {
                    renderX[i] = draggedLeft;  // follows cursor (raised layer)
                    continue;
                }
                if (dragging) {
                    // Slot index among the NON-dragged tabs: skip the dragged
                    // slot, and leave dropIndex's slot empty for the drop.
                    size_t slot = i < dragFrom ? i : i - 1;  // compacted index
                    if (slot >= dropIndex) slot += 1;         // open the gap
                    renderX[i] = r.x + slotStride * static_cast<float>(slot);
                } else {
                    renderX[i] = x;
                }
                x += uniformW + gap;
            }
        }

        for (size_t i = 0; i < strip.tabOrder.size(); ++i) {
            auto tabId = strip.tabOrder[i];
            auto tabOpt = EntityHelper::getEntityForID(tabId);
            if (!tabOpt.valid() || !tabOpt->has<Tab>()) continue;
            auto& tabEntity = tabOpt.asE();
            auto& tab = tabEntity.get<Tab>();
            bool isActive = tabEntity.has<ActiveTab>();
            bool isDragged = dragging && i == dragFrom;

            float tabW = uniformW;
            tabX = renderX[i];

            // Overflow guard: if this tab would extend past the strip's right
            // edge, clamp its width to whatever room is left; if there's no
            // usable room (< minW), skip it — draw NOTHING past the edge so no
            // tab or × ever bleeds off the window frame. (The dragged tab is
            // already clamped inside the strip above, so it's never skipped.)
            float room = stripRight - tabX;
            if (!isDragged) {
                if (room < minW) continue;
                if (tabX + tabW > stripRight) tabW = room;
            }

            // The dragged tab lifts above the others (higher render layer +
            // its distinct fill), so it visually floats while being moved.
            int baseLayer = isDragged ? 9 : 6;

            bool hovered = (!dragging &&
                            afterhours::ui::is_mouse_inside(
                                ctx.mouse.pos,
                                RectangleType{tabX, r.y, tabW, tabH})) ||
                           isDragged ||
                           // Test-only: force one tab's hover branch (to
                           // capture the hovered-tab styling headlessly).
                           // No-op unless HANABI_TEST_HOVER=tab:<sessionId>.
                           hanabi::test_hooks::force_hover("tab:" +
                                                           tab.sessionId);

            afterhours::Color bg = isActive  ? tab_colors::tab_active()
                                   : hovered ? tab_colors::tab_hover()
                                             : tab_colors::tab_inactive();
            afterhours::Color txt =
                isActive ? tab_colors::tab_text_act() : tab_colors::tab_text();

            auto tabBtn = button(ctx, mk(uiRoot, 910 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(tabW), pixels(tabH)})
                    .with_absolute_position()
                    .with_translate(tabX, r.y)
                    .with_custom_background(bg)
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{.left = pixels(12),
                                          .right = pixels(24)})
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    // Round ALL corners: afterhours' sokol renderer emits a
                    // degenerate triangle for MIXED round/sharp corners (gap
                    // #25), which showed as a diagonal notch on the tab. All-
                    // round avoids the buggy path; the bottom corners sit on the
                    // strip/content bridge so their slight round is invisible.
                    .with_rounded_corners(
                        afterhours::ui::imm::RoundedCorners().all_round().get())
                    .with_render_layer(baseLayer)
                    .with_debug_name("tab_" + tab.sessionId));
            // Label as a centered child whose height EQUALS the strip content
            // box (tabH), so the label's own vertical-centering lands the text
            // on the tab's true center instead of fontstash ascent/descent
            // pushing the glyphs low (same recipe as the sidebar chat rows).
            div(ctx, mk(tabBtn.ent(), 1),
                ComponentConfig{}
                    .with_label(fmtutil::ellipsize(tab.label, 22))
                    .with_size(ComponentSize{percent(1.0f), pixels(tabH)})
                    .with_transparent_bg()
                    .with_custom_text_color(txt)
                    .with_font_size(theme::type::ROW)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(baseLayer)
                    .with_debug_name("tab_label"));

            // Active-tab cues (secondary to the fill delta):
            //   1. a top accent bar (3px) — the classic "current tab" marker,
            //      stronger and more tab-like than a thin bottom underline;
            //   2. a bottom BRIDGE of the active fill drawn over the strip's
            //      1px bottom hairline, so the active tab visually merges into
            //      the content pane directly below it (no seam between them).
            if (isActive) {
                // Bridge: cover the strip's bottom border under this tab with
                // the panel fill so tab + content read as one raised surface.
                // (Skip the bridge while THIS tab is being dragged — it's
                // lifted off the strip, so merging it with the content below
                // would look wrong.)
                if (!isDragged)
                    div(ctx, mk(uiRoot, 925 + static_cast<int>(i)),
                        ComponentConfig{}
                            .with_size(ComponentSize{pixels(tabW), pixels(2)})
                            .with_absolute_position()
                            .with_translate(tabX, r.y + tabH - 1.0f)
                            .with_custom_background(tab_colors::tab_active())
                            .with_roundness(0.0f)
                            .with_render_layer(baseLayer + 1)
                            .with_debug_name("tab_bridge"));
                // Top accent bar.
                div(ctx, mk(uiRoot, 930 + static_cast<int>(i)),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(tabW), pixels(3)})
                        .with_absolute_position()
                        .with_translate(tabX, r.y)
                        .with_custom_background(tab_colors::accent())
                        .with_rounded_corners(
                            afterhours::ui::imm::RoundedCorners().all_round().get())
                        .with_roundness(0.35f)
                        .with_render_layer(baseLayer + 1)
                        .with_debug_name("tab_accent_top"));
            }

            // Click-to-focus is now resolved on RELEASE (see the drag-input
            // block above) so a press that turns into a drag doesn't also
            // switch tabs. Nothing to do on press here.

            // Close button.
            float closeW = 16.0f;
            float closeX = tabX + tabW - closeW - 5.0f;
            float closeY = r.y + (tabH - closeW) * 0.5f;
            bool closeHovered =
                !dragging &&
                afterhours::ui::is_mouse_inside(
                    ctx.mouse.pos,
                    RectangleType{closeX, closeY, closeW, closeW});
            button(ctx, mk(uiRoot, 950 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_label("\xc3\x97")
                    .with_size(ComponentSize{pixels(closeW), pixels(closeW)})
                    .with_absolute_position()
                    .with_translate(closeX, closeY)
                    .with_custom_background(closeHovered
                                                ? tab_colors::close_hover()
                                                : bg)
                    .with_custom_text_color(closeHovered
                                                ? tab_colors::tab_text_act()
                                                : tab_colors::tab_text())
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.2f)
                    .with_render_layer(baseLayer + 1)
                    .with_debug_name("tab_close"));
            if (closeHovered && ctx.mouse.just_pressed) {
                // A press on × is a close, not a drag — drop any candidate.
                strip.clear_drag();
                close_tab(strip, app, tabId, i, isActive);
                ctx.mouse.just_pressed = false;
                return;
            }
        }
    }

    // Open `id` in a tab: focus if already open, else create a new tab.
    // Delegates to the graphics-free, headlessly-tested model tab logic.
    static void open_session_in_tab(TabStripComponent& strip, AppComponent& app,
                                    const std::string& id) {
        model::open_session_in_tab(strip, app, id);
    }

    static void switch_to_tab(AppComponent& app, Entity& newTab) {
        model::switch_to_tab(app, newTab);
    }

    static void close_tab(TabStripComponent& strip, AppComponent& app,
                          afterhours::EntityID tabId, size_t index,
                          bool wasActive) {
        model::close_tab(strip, app, tabId, index, wasActive);
    }
};

// Consumes AppComponent::requestOpenTab (a sidebar/digest row click) and turns
// it into an open/focused tab. Runs before the loader so the resulting
// transcript fetch is picked up the same frame.
struct TabFlowSystem : afterhours::System<AppComponent> {
    void for_each_with(Entity&, AppComponent& app, float) override {
        // Restore persisted tabs once the session list is available.
        if (!app.restoreDone && app.listState == LoadState::Loaded) {
            app.restoreDone = true;
            auto* strip = find_singleton<TabStripComponent>();
            if (strip && !app.restoreTabIds.empty()) {
                for (const auto& id : app.restoreTabIds)
                    if (app.find_summary(id))
                        TabBarSystem::open_session_in_tab(*strip, app, id);
                // Focus the persisted active tab (falls back to last opened).
                if (!app.restoreActiveId.empty() &&
                    app.find_summary(app.restoreActiveId)) {
                    for (auto tabId : strip->tabOrder) {
                        auto o = EntityHelper::getEntityForID(tabId);
                        if (o.valid() && o->has<Tab>() &&
                            o->get<Tab>().sessionId == app.restoreActiveId) {
                            TabBarSystem::switch_to_tab(app, o.asE());
                            break;
                        }
                    }
                }
            }
            app.restoreTabIds.clear();
        }

        if (app.requestOpenTab.empty()) return;
        std::string id = app.requestOpenTab;
        app.requestOpenTab.clear();
        auto* strip = find_singleton<TabStripComponent>();
        if (!strip) return;
        TabBarSystem::open_session_in_tab(*strip, app, id);
    }
};

}  // namespace ecs
