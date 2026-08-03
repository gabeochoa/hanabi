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

#include "../ui/icons.h"

// Clipboard seam (afterhours plugin, sokol-backed under AFTER_HOURS_USE_METAL
// which the app build defines). Used by the tab context menu's "Copy Navi
// URL". graphics.h (already transitively included via ui_imports->rl->ah) pulls
// in sokol_app.h, so sapp_set_clipboard_string is declared here. This header is
// app-only (tests include tab_model.h, never this), so no headless-test impact.
#include "../../vendor/afterhours/src/plugins/clipboard.h"

namespace ecs {

namespace tab_colors {
// Three-level elevation ladder so tabs read as physical tabs, not a segmented
// toolbar. The review flagged the old two-token scheme (strip==inactive, only a
// ~15L active/inactive delta) as "near-invisible". We now use a monotonic
// triple that keeps its ordering in BOTH dark and light:
//
//              dark L   light L
//   strip_bg   panel_bg     34      255   the content plane (no dark void)
//   inactive   window_bg    24      238   recessed WELL, distinct from strip
//   active     panel_bg     34      255   RAISED surface == the content pane
//
// The ACTIVE tab fills with panel_bg — the exact color of the transcript/main
// pane directly below it (main_pane_system) — so it reads as one continuous
// raised surface "lifted out of" the strip (we also bridge the strip's bottom
// hairline under it, below). The STRIP now ALSO uses panel_bg so the empty area
// to the RIGHT of the last tab reads as the same continuous chrome plane as the
// content below — not a darker "unfinished" black well (the old sidebar_bg
// strip left a hard dark void beside a single tab). INACTIVE tabs sit one step
// DOWN (window_bg) as their own recessed wells, so each still reads as a
// discrete tab. On hover an inactive tab gets a faint additive wash over its
// own fill (theme::over, gap #13 — a subtle tint, never a solid block).
inline afterhours::Color strip_bg() { return theme::panel_bg(); }
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
        // Chrome-style overflow ladder:
        //   * maxWCap: a comfortable full width; a few tabs never stretch huge.
        //   * minW:    the hard FLOOR. Tabs shrink to share the strip down to
        //              here (still showing icon+ellipsized title+×); below this
        //              they DON'T keep shrinking into illegibility — instead
        //              the strip SCROLLS (scrollX) and tabs keep minW.
        // (The old code used minW=90/cap=200 and simply stopped drawing past
        //  the right edge, which is what made the many-tabs state look broken.)
        const float minW = 40.0f;
        const float maxWCap = 240.0f;
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
        // the available strip, shrink the per-tab width so they share the space
        // evenly — but never below minW. Below minW the strip SCROLLS instead
        // of shrinking further (Chrome model), keeping every tab legible.
        // This uniform width keeps the drag-reorder slot math (slotStride)
        // valid. All computed by the pure, unit-tested model.
        const size_t nTabs = strip.tabOrder.size();
        const float uniformW =
            model::compute_tab_width(r.width, nTabs, minW, maxWCap, gap);

        // The per-tab horizontal advance (a full slot: tab width + the gap).
        const float slotStride = uniformW + gap;

        // How far the strip can scroll (content beyond the visible strip).
        const float maxScroll =
            model::compute_max_scroll(r.width, nTabs, uniformW, gap);

        // ---- Horizontal scroll input --------------------------------------
        // Chrome hscrolls the strip on horizontal wheel / shift+wheel when the
        // tabs overflow. afterhours' facade only exposes the (vertical) wheel
        // float, so we treat a wheel event while the cursor is over the strip
        // as a horizontal scroll (Chrome also does plain-wheel-over-tabstrip),
        // and shift+wheel works anywhere. A positive wheel scrolls left.
        {
            bool shiftDown = afterhours::graphics::is_key_down(340) ||
                             afterhours::graphics::is_key_down(344);
            bool overStrip = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{r.x, r.y, r.width, tabH});
            if ((overStrip || shiftDown) && maxScroll > 0.0f) {
                float wheel = afterhours::graphics::get_mouse_wheel_move();
                if (wheel != 0.0f) {
                    // ~1 slot per wheel notch feels right; wheel sign is
                    // "up/away == positive", which we map to scroll-left.
                    strip.scrollX = model::clamp_scroll(
                        strip.scrollX - wheel * slotStride, maxScroll);
                }
            }
        }
        // Keep the ACTIVE tab visible: if selecting a tab pushed it off-screen,
        // scroll it into view. Also re-clamp scrollX every frame (strip width
        // or tab count may have changed since last frame).
        {
            size_t activeIdx = strip.tabOrder.size();
            for (size_t i = 0; i < strip.tabOrder.size(); ++i) {
                auto o = EntityHelper::getEntityForID(strip.tabOrder[i]);
                if (o.valid() && o->has<ActiveTab>()) { activeIdx = i; break; }
            }
            strip.scrollX = model::clamp_scroll(strip.scrollX, maxScroll);
            if (activeIdx < strip.tabOrder.size())
                strip.scrollX = model::scroll_to_show(
                    activeIdx, strip.scrollX, r.width, uniformW, gap, nTabs);
        }
        // Every tab's non-drag left edge is shifted left by the scroll offset.
        const float scrollOff = strip.scrollX;
        // baseX is the (possibly off-screen-left) origin of slot 0. All slot
        // layout is relative to baseX; the visible viewport is still [r.x,
        // stripRight]. When scrollOff==0 (everything fits) baseX==r.x, so the
        // no-overflow path is byte-identical to before.
        const float baseX = r.x - scrollOff;

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
            // Which slot (scrolled layout) did we press over? Slots advance by
            // slotStride from baseX; only slots overlapping the visible strip
            // are pressable. (With scroll, tabs keep uniformW.)
            for (size_t i = 0; i < nTabs; ++i) {
                float px = baseX + slotStride * static_cast<float>(i);
                float w = uniformW;
                // Skip slots entirely outside the visible viewport.
                if (px + w <= r.x || px >= stripRight) continue;
                // Clamp the visible hit rect to the viewport so a press in the
                // sidebar/beyond-strip gutter doesn't count.
                float hitX = std::max(px, r.x);
                float hitR = std::min(px + w, stripRight);
                bool inside = afterhours::ui::is_mouse_inside(
                    ctx.mouse.pos,
                    RectangleType{hitX, r.y, hitR - hitX, tabH});
                if (inside && !over_close(px, w)) {
                    strip.dragCandidate = strip.tabOrder[i];
                    strip.dragFromIndex = i;
                    strip.dragStartX = ctx.mouse.pos.x;
                    strip.dragCurX = ctx.mouse.pos.x;
                    strip.dragging = false;
                    break;
                }
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
                    baseX + slotStride * static_cast<float>(from) + uniformW * 0.5f;
                float draggedLeftR =
                    (origCenter + (strip.dragCurX - strip.dragStartX)) -
                    uniformW * 0.5f;
                draggedLeftR = std::clamp(draggedLeftR, r.x,
                                          stripRight - uniformW);
                size_t to = model::compute_drop_index(
                    draggedLeftR + uniformW * 0.5f, baseX, slotStride, nTabs);
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

        // ---- Right-click context menu (open) ------------------------------
        // A right-press over a tab opens the per-tab context menu anchored at
        // the cursor. sokol maps mouse button 1 == right. (Rendered + acted on
        // below; dismissed on any left-click that isn't on a menu item.)
        if (afterhours::graphics::is_mouse_button_pressed(1) && nTabs > 0) {
            for (size_t i = 0; i < nTabs; ++i) {
                float px = baseX + slotStride * static_cast<float>(i);
                float w = uniformW;
                if (px + w <= r.x || px >= stripRight) continue;
                float hitX = std::max(px, r.x);
                float hitR = std::min(px + w, stripRight);
                if (afterhours::ui::is_mouse_inside(
                        ctx.mouse.pos,
                        RectangleType{hitX, r.y, hitR - hitX, tabH})) {
                    strip.menuOpen = true;
                    strip.menuTabId = strip.tabOrder[i];
                    strip.menuX = ctx.mouse.pos.x;
                    strip.menuY = ctx.mouse.pos.y;
                    // A right-press must not also start a drag.
                    strip.clear_drag();
                    break;
                }
            }
        }

        // ---- Middle-click closes a tab (browser convention) ---------------
        // sokol/raylib maps mouse button 2 == middle. A middle-press over any
        // tab closes THAT tab (not just the active one), like every browser.
        if (afterhours::graphics::is_mouse_button_pressed(2) && nTabs > 0) {
            for (size_t i = 0; i < nTabs; ++i) {
                float px = baseX + slotStride * static_cast<float>(i);
                float w = uniformW;
                if (px + w <= r.x || px >= stripRight) continue;
                float hitX = std::max(px, r.x);
                float hitR = std::min(px + w, stripRight);
                if (afterhours::ui::is_mouse_inside(
                        ctx.mouse.pos,
                        RectangleType{hitX, r.y, hitR - hitX, tabH})) {
                    const afterhours::EntityID tabId = strip.tabOrder[i];
                    auto o = EntityHelper::getEntityForID(tabId);
                    const bool wasActive = o.valid() && o->has<ActiveTab>();
                    close_tab(strip, app, tabId, i, wasActive);
                    strip.clear_drag();  // a middle-press must not start a drag
                    break;
                }
            }
        }

        // Recompute after a possible reorder (tabOrder / index may have moved).
        const bool dragging = strip.dragging && strip.has_drag_candidate();
        const size_t dragFrom =
            dragging ? index_of(strip.dragCandidate) : strip.tabOrder.size();
        // While dragging, the dragged tab's live center-x (clamped to strip).
        float draggedLeft = 0.0f;
        size_t dropIndex = dragFrom;
        if (dragging && dragFrom < strip.tabOrder.size()) {
            float origCenter = baseX + slotStride * static_cast<float>(dragFrom) +
                               uniformW * 0.5f;
            float draggedCenter =
                origCenter + (strip.dragCurX - strip.dragStartX);
            // Clamp so the dragged tab never leaves the VISIBLE strip.
            float minLeft = r.x;
            float maxLeft = stripRight - uniformW;
            draggedLeft = draggedCenter - uniformW * 0.5f;
            draggedLeft = std::clamp(draggedLeft, minLeft, maxLeft);
            dropIndex = model::compute_drop_index(draggedLeft + uniformW * 0.5f,
                                                  baseX, slotStride,
                                                  strip.tabOrder.size());
        }

        // Precompute each tab's render left-edge X. Non-dragging: the natural
        // left-to-right slots (shifted by the scroll offset via baseX).
        // Dragging: the OTHER tabs reflow to open a gap at dropIndex (so you
        // can see where the dragged tab will land), and the dragged tab itself
        // is positioned at draggedLeft (drawn last, raised).
        std::vector<float> renderX(strip.tabOrder.size(), 0.0f);
        {
            float x = baseX;
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
                    renderX[i] = baseX + slotStride * static_cast<float>(slot);
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

            // Overflow / scroll clipping: skip tabs that are ENTIRELY outside
            // the visible strip (scrolled off either edge). For a tab that
            // straddles the RIGHT edge, clamp its drawn width to the room left
            // so its × never bleeds past the window frame (the ugly old
            // behavior). A tab straddling the LEFT edge renders its right
            // portion (its left is under the sidebar boundary). The dragged tab
            // is already clamped fully inside the strip above, so it's never
            // skipped/clamped here.
            if (!isDragged) {
                if (tabX + tabW <= r.x || tabX >= stripRight) continue;
                if (tabX + tabW > stripRight) tabW = stripRight - tabX;
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

            // At narrow (scrolled) widths, Chrome hides the × on inactive,
            // non-hovered tabs so the (ellipsized) title keeps as much room as
            // possible; the active/hovered tab always keeps a reachable ×.
            const float kCloseShowMinW = 90.0f;
            bool showClose = isActive || hovered || tabW >= kCloseShowMinW;
            // Ellipsize the title to the room the tab actually has: ~7px/char
            // at ROW size, minus left pad + (× reserve when shown).
            float rightReserve = showClose ? 26.0f : 8.0f;
            float textRoom = tabW - 12.0f - rightReserve;
            size_t labelBudget = textRoom <= 0.0f
                                     ? 1
                                     : static_cast<size_t>(textRoom / 7.0f);
            if (labelBudget < 1) labelBudget = 1;

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
                                          .right = pixels(showClose ? 24 : 8)})
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    // Round ALL corners: afterhours' sokol renderer emits a
                    // degenerate triangle for MIXED round/sharp corners (gap
                    // #25). All-round avoids the buggy path; the bottom corners
                    // sit on the strip/content bridge so their round is hidden.
                    // NOTE: the vendor fix is PROVEN + captured in
                    // vendor_patches/25-*.patch — once it lands in afterhours
                    // remote + the submodule pointer is bumped, switch this to
                    // RoundedCorners().all_sharp().top_round() for the true tab
                    // shape (verified clean with the patch applied).
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
                    .with_label(fmtutil::ellipsize(tab.label, labelBudget))
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

            // Close button (hidden on narrow inactive/unhovered tabs — see
            // showClose above; clamped to the visible strip so the × of a
            // right-edge partial tab never bleeds off the window frame).
            if (showClose) {
            float closeW = 16.0f;
            float closeX = tabX + tabW - closeW - 5.0f;
            float closeY = r.y + (tabH - closeW) * 0.5f;
            // Don't draw the × past the visible strip right edge.
            if (closeX + closeW <= stripRight) {
            bool closeHovered =
                !dragging &&
                afterhours::ui::is_mouse_inside(
                    ctx.mouse.pos,
                    RectangleType{closeX, closeY, closeW, closeW});
            button(ctx, mk(uiRoot, 950 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(" ")
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
                    // Lucide "close" sprite (atlas); \xc3\x97 unicode fallback.
                    // Tint tracks hover the same as the text color did.
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        "close", "\xc3\x97",
                        closeHovered ? tab_colors::tab_text_act()
                                     : tab_colors::tab_text(),
                        11.0f))
                    .with_debug_name("tab_close"));
            if (closeHovered && ctx.mouse.just_pressed) {
                // A press on × is a close, not a drag — drop any candidate.
                strip.clear_drag();
                close_tab(strip, app, tabId, i, isActive);
                ctx.mouse.just_pressed = false;
                return;
            }
            }  // closeX in-bounds
            }  // showClose
        }

        // ---- Right-click context menu (render + act) ----------------------
        // A small overlay anchored at the right-click cursor with per-tab
        // actions. Drawn on a high render layer so it sits above the strip and
        // content. Dismissed on click-away or after an action.
        if (strip.menuOpen) {
            // Resolve the target tab (it may have been closed since opening).
            auto menuOpt = EntityHelper::getEntityForID(strip.menuTabId);
            if (!menuOpt.valid() || !menuOpt->has<Tab>()) {
                strip.close_menu();
            } else {
                render_tab_menu(ctx, uiRoot, strip, app, menuOpt.asE());
            }
        }
    }

    // Renders the tab context menu and handles its item clicks + click-away
    // dismissal. Kept as a member so for_each_with stays readable.
    void render_tab_menu(UIContext<InputAction>& ctx, Entity& uiRoot,
                         TabStripComponent& strip, AppComponent& app,
                         Entity& tabEntity) {
        const auto& tab = tabEntity.get<Tab>();
        struct Item {
            const char* label;
            int action;  // 0 = copy url, 1 = close others
        };
        static const Item kItems[] = {
            {"Copy URL", 0},
            {"Close others", 1},
        };
        const int nItems = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));

        const float menuW = 160.0f;
        const float itemH = 26.0f;
        const float menuH = itemH * static_cast<float>(nItems);
        // Keep the menu on-screen (nudge left/up if it would spill off).
        float mx = strip.menuX;
        float my = strip.menuY;
        if (mx + menuW > ctx.screen_width) mx = ctx.screen_width - menuW;
        if (my + menuH > ctx.screen_height) my = ctx.screen_height - menuH;
        if (mx < 0.0f) mx = 0.0f;
        if (my < 0.0f) my = 0.0f;

        const int kMenuLayer = 30;  // well above tabs (6-10) and content

        // Menu panel background + border.
        div(ctx, mk(uiRoot, 970),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(menuW), pixels(menuH)})
                .with_absolute_position()
                .with_translate(mx, my)
                .with_custom_background(tab_colors::tab_active())
                .with_border(tab_colors::border(), pixels(1))
                .with_roundness(0.2f)
                .with_render_layer(kMenuLayer)
                .with_debug_name("tab_menu"));

        bool clickedItem = false;
        for (int k = 0; k < nItems; ++k) {
            float iy = my + itemH * static_cast<float>(k);
            bool itemHovered = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{mx, iy, menuW, itemH});
            div(ctx, mk(uiRoot, 971 + k),
                ComponentConfig{}
                    .with_label(kItems[k].label)
                    .with_size(ComponentSize{pixels(menuW), pixels(itemH)})
                    .with_absolute_position()
                    .with_translate(mx, iy)
                    .with_custom_background(itemHovered
                                                ? tab_colors::tab_hover()
                                                : tab_colors::tab_active())
                    .with_custom_text_color(tab_colors::tab_text_act())
                    .with_font_size(theme::type::ROW)
                    .with_alignment(TextAlignment::Left)
                    .with_padding(Padding{.left = pixels(10)})
                    .with_roundness(0.0f)
                    .with_render_layer(kMenuLayer + 1)
                    .with_debug_name("tab_menu_item"));
            if (itemHovered && ctx.mouse.just_pressed) {
                std::string keepId = tab.sessionId;
                if (kItems[k].action == 0) {
                    // Copy URL — real clipboard write via the afterhours
                    // sokol-backed clipboard seam. Base is config-driven
                    // (host-neutral navi://session/<id> when unconfigured).
                    afterhours::clipboard::set_text(
                        model::navi_url_for(app.webBaseUrl, keepId));
                } else {
                    model::close_others(strip, app, keepId);
                }
                clickedItem = true;
                ctx.mouse.just_pressed = false;
            }
        }

        // Dismiss: an item click, or any left-press outside the menu rect.
        bool pressedOutside =
            ctx.mouse.just_pressed &&
            !afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{mx, my, menuW, menuH});
        if (clickedItem || pressedOutside) strip.close_menu();
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
