#pragma once

// VS Code-style closable content tabs across the top of the main pane.
// Each open thread is a Tab entity; the focused one carries an ActiveTab
// marker. Clicking a session row (handled in AppFlowSystem) opens a new tab or
// focuses the existing one; the transcript shows whatever the active tab
// points at. Close (×) removes a tab; Cmd+W closes the active tab.
//
// Mirrors floatinghotel/src/ecs/tab_bar_system.h.

#include <algorithm>
#include <bitset>
#include <cmath>
#include <string>
#include <vector>

#include "../test_hooks.h"
#include "../util/format.h"
#include "tab_model.h"
#include "../keys.h"
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
// Two levels, not three. The review that produced the old ladder was right
// that a ~15L active/inactive delta is near-invisible, but it solved that by
// tinting three planes at different values, and the reference does the
// opposite: everything is one background and the ACTIVE tab is the only thing
// that is a different colour at all.
//
//              dark          hex        role
//   strip_bg   window_bg     #171723    the one background
//   inactive   window_bg     #171723    the same — hairline + text only
//   active     selected_bg   #2E3A58    the only filled surface in the strip
//
// The ACTIVE tab fills with selected_bg (#2E3A58 on the reference) — the SAME
// fill the selected sidebar row uses, which is what makes "this is the current
// thing" read the same in both places. The strip and the INACTIVE tabs are both
// the window colour, so an inactive tab is invisible except for its hairline
// and its text: on the reference there is no recessed well behind it. On hover
// an inactive tab gets a faint additive wash over its own fill (theme::over,
// gap #13 — a subtle tint, never a solid block).
inline afterhours::Color strip_bg() { return theme::window_bg(); }
inline afterhours::Color tab_active() { return theme::selected_bg(); }
inline afterhours::Color tab_inactive() { return theme::window_bg(); }
inline afterhours::Color tab_hover() {
    return theme::over(theme::hover_bg(), theme::window_bg());
}
inline afterhours::Color tab_text() { return theme::text_secondary(); }
// The reference's active-tab title is pure white on the selected fill, a step
// brighter than text_primary; light mode keeps its own ink.
inline afterhours::Color tab_text_act() {
    return theme::g_mode == theme::Mode::Dark
               ? afterhours::Color{255, 255, 255, 255}
               : theme::text_primary();
}
inline afterhours::Color close_hover() {
    return theme::over(theme::hover_bg(), theme::panel_bg());
}
inline afterhours::Color border() { return theme::border(); }
inline afterhours::Color tab_outline() { return theme::divider(); }
inline afterhours::Color accent() { return theme::accent(); }
inline constexpr float kTabCorner = 4.0f;
inline constexpr float kTabBorderPx = 1.0f;
// afterhours rasterizes a w*h box at (x,y) as (w+1)*(h+1) pixels anchored at
// (x-1,y-1), so a tab whose OUTER edge must land on the reference's measured
// 220x34 at (284,32) is asked for as 219x33 at (285,33) (gap #80).
inline constexpr float kRasterGrow = 1.0f;
// Gap #81. The corner bits are named for the OPPOSITE corner: the enum is
// TOP_LEFT=0..BOTTOM_RIGHT=3 and the sokol backend reads the same bitset as
// 3=TL 2=TR 1=BL 0=BR. Naming the bottom two is what rounds the top two on
// screen. Do not "fix" this to read top_left/top_right; it renders inverted.
inline std::bitset<4> tab_corners_top_round_bottom_square() {
    using afterhours::ui::imm::CornerState;
    return afterhours::ui::imm::RoundedCorners()
        .all_sharp()
        .bottom_left(CornerState::ROUND)
        .bottom_right(CornerState::ROUND)
        .get();
}
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
        bool cmdDown = hanabi::keys::cmd_down();
        if (cmdDown && hanabi::keys::pressed(hanabi::keys::kW)) {
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
        // The tabs occupy the BOTTOM band of the strip; the band above them is
        // left clear (frameless-window drag zone / traffic lights). The tab's
        // bottom edge sits ON the strip's hairline so the hairline reads as the
        // shelf the tabs stand on.
        const float tabH = std::min(layout.tabStripTabHeight, r.height);
        const float tabY = r.y + r.height - tabH - 1.0f;
        // Reference geometry: a tab is a fixed 220 wide with a 4px gap, and the
        // run starts 4px in from the pane's left edge. Tabs only shrink from
        // there when too many are open to fit.
        const float minW = 40.0f;
        const float maxWCap = 220.0f;
        const float gap = 4.0f;
        // The strip's own left inset: the first tab does not touch the divider.
        const float stripPadL = 4.0f;
        // Room reserved at the right end for the new-tab (+) button, so a full
        // strip of tabs never runs underneath it.
        const float kNewTabW = 34.0f;
        // Right edge the tabs must never cross.
        const float stripRight = r.x + r.width - kNewTabW;
        // Left edge of the tab run (the strip's inset).
        const float runLeft = r.x + stripPadL;
        // Chrome-style overflow ladder: tabs shrink to share the strip down to
        // minW, and below that the strip SCROLLS (scrollX) instead of shrinking
        // them into illegibility.

        // Cap total tab area so N tabs (plus the gaps between them) never
        // overflow the strip. If the tabs at their natural width would exceed
        // the available strip, shrink the per-tab width so they share the space
        // evenly — but never below minW. Below minW the strip SCROLLS instead
        // of shrinking further (Chrome model), keeping every tab legible.
        // This uniform width keeps the drag-reorder slot math (slotStride)
        // valid. All computed by the pure, unit-tested model.
        const size_t nTabs = strip.tabOrder.size();
        // The width the tab run may use: the strip less its left inset and the
        // + button's reserved slot.
        const float runW = std::max(0.0f, r.width - stripPadL - kNewTabW);
        const float uniformW =
            model::compute_tab_width(runW, nTabs, minW, maxWCap, gap);

        // The per-tab horizontal advance (a full slot: tab width + the gap).
        const float slotStride = uniformW + gap;

        // How far the strip can scroll (content beyond the visible strip).
        const float maxScroll =
            model::compute_max_scroll(runW, nTabs, uniformW, gap);

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
                ctx.mouse.pos, RectangleType{r.x, tabY, r.width, tabH});
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
                    activeIdx, strip.scrollX, runW, uniformW, gap, nTabs);
        }
        // Every tab's non-drag left edge is shifted left by the scroll offset.
        const float scrollOff = strip.scrollX;
        // baseX is the (possibly off-screen-left) origin of slot 0. All slot
        // layout is relative to baseX; the visible viewport is still [r.x,
        // stripRight]. When scrollOff==0 (everything fits) baseX==r.x, so the
        // no-overflow path is byte-identical to before.
        const float baseX = r.x + stripPadL - scrollOff;

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
            float closeY = tabY + (tabH - closeW) * 0.5f;
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
                if (px + w <= runLeft || px >= stripRight) continue;
                // Clamp the visible hit rect to the viewport so a press in the
                // sidebar/beyond-strip gutter doesn't count.
                float hitX = std::max(px, runLeft);
                float hitR = std::min(px + w, stripRight);
                bool inside = afterhours::ui::is_mouse_inside(
                    ctx.mouse.pos,
                    RectangleType{hitX, tabY, hitR - hitX, tabH});
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
                draggedLeftR = std::clamp(draggedLeftR, runLeft,
                                          stripRight - uniformW);
                size_t to = model::compute_drop_index(
                    draggedLeftR + uniformW * 0.5f, baseX, slotStride, nTabs);
                model::reorder_tab(strip, from, to);
                // A drag never focuses/switches — consume the press so the
                // per-tab click branch below can't also fire.
                ctx.mouse.just_pressed = false;
            } else if (!strip.dragging && from < nTabs) {
                // Pure click (moved < threshold): preserve click-to-focus, and
                // treat a click on the tab you are already reading as the
                // second look that keeps it.
                auto o = EntityHelper::getEntityForID(strip.dragCandidate);
                if (o.valid() && o->has<Tab>()) {
                    if (o->has<ActiveTab>()) model::keep_tab(o.asE());
                    else switch_to_tab(app, o.asE());
                }
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
                if (px + w <= runLeft || px >= stripRight) continue;
                float hitX = std::max(px, runLeft);
                float hitR = std::min(px + w, stripRight);
                if (afterhours::ui::is_mouse_inside(
                        ctx.mouse.pos,
                        RectangleType{hitX, tabY, hitR - hitX, tabH})) {
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
                if (px + w <= runLeft || px >= stripRight) continue;
                float hitX = std::max(px, runLeft);
                float hitR = std::min(px + w, stripRight);
                if (afterhours::ui::is_mouse_inside(
                        ctx.mouse.pos,
                        RectangleType{hitX, tabY, hitR - hitX, tabH})) {
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
            float minLeft = runLeft;
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
            // A preview tab keeps the recessed fill even while it is the one
            // you are reading: the accent bar above still says "this is the
            // current tab", and the un-raised surface says "and it is not one
            // you have kept". That is the whole visual difference — a glance
            // does not get to look like a commitment.
            const bool isPreview = !tab.keptOpen;

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
                if (tabX + tabW <= runLeft || tabX >= stripRight) continue;
                if (tabX + tabW > stripRight) tabW = stripRight - tabX;
            }

            // The dragged tab lifts above the others (higher render layer +
            // its distinct fill), so it visually floats while being moved.
            int baseLayer = isDragged ? 9 : 6;

            bool hovered = (!dragging &&
                            afterhours::ui::is_mouse_inside(
                                ctx.mouse.pos,
                                RectangleType{tabX, tabY, tabW, tabH})) ||
                           isDragged ||
                           // Test-only: force one tab's hover branch (to
                           // capture the hovered-tab styling headlessly).
                           // No-op unless HANABI_TEST_HOVER=tab:<sessionId>.
                           hanabi::test_hooks::force_hover("tab:" +
                                                           tab.sessionId);

            afterhours::Color bg = (isActive && !isPreview)
                                       ? tab_colors::tab_active()
                                   : hovered ? tab_colors::tab_hover()
                                             : tab_colors::tab_inactive();
            afterhours::Color txt = (isActive && !isPreview)
                                        ? tab_colors::tab_text_act()
                                        : tab_colors::tab_text();

            // The × appears on HOVER only. A row of tabs each carrying a
            // permanent × reads as a toolbar of buttons; the reference shows a
            // clean title on every tab, active one included, and the close
            // affordance only under the cursor.
            bool showClose = hovered;
            // A pinned tab spends its left gutter on the pin, so the title
            // starts further in. Both numbers are measured off the reference:
            // pin at left+12, title at left+26 (left+12 when unpinned).
            // afterhours' draw_text_in_rect insets every string by a
            // hardcoded 5px margin with no way to zero it (gap #75), so the
            // padding we author is the design inset minus that margin.
            const float kTextMarginPx = 5.0f;
            const float padL =
                (tab.pinned ? 26.0f : 12.0f) - kTextMarginPx;
            // Ellipsize the title to the room the tab actually has: ~7px/char
            // at ROW size, minus left pad + (× reserve when shown).
            float rightReserve = showClose ? 26.0f : 8.0f;
            float textRoom = tabW - padL - rightReserve;
            size_t labelBudget = textRoom <= 0.0f
                                     ? 1
                                     : static_cast<size_t>(textRoom / 7.0f);
            if (labelBudget < 1) labelBudget = 1;

            auto tabBtn = button(ctx, mk(uiRoot, 910 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{
                        pixels(tabW - tab_colors::kRasterGrow),
                        pixels(tabH - tab_colors::kRasterGrow)})
                    .with_absolute_position()
                    .with_translate(tabX + tab_colors::kRasterGrow,
                                    tabY + tab_colors::kRasterGrow)
                    .with_custom_background(bg)
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{.left = pixels(padL),
                                          .right = pixels(showClose ? 24 : 8)})
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(tab_colors::kTabCorner)
                    .with_rounded_corners(
                        tab_colors::tab_corners_top_round_bottom_square())
                    .with_border(isActive ? bg : tab_colors::tab_outline(),
                                 pixels(tab_colors::kTabBorderPx))
                    .with_render_layer(baseLayer)
                    // The ACTIVE tab carries a fixed name: "which tab is
                    // current" is a thing tests need to assert, and the accent
                    // bar they used to assert on is gone.
                    .with_debug_name(isActive ? std::string("tab_active")
                                              : ("tab_" + tab.sessionId)));
            // Label as a centered child whose height EQUALS the strip content
            // box (tabH), so the label's own vertical-centering lands the text
            // on the tab's true center instead of fontstash ascent/descent
            // pushing the glyphs low (same recipe as the sidebar chat rows).
            div(ctx, mk(tabBtn.ent(), 1),
                ComponentConfig{}
                    .with_label(fmtutil::ellipsize(
                        model::tab_label_for(app, tab.sessionId), labelBudget))
                    .with_size(ComponentSize{
                        percent(1.0f),
                        pixels(tabH - tab_colors::kRasterGrow)})
                    .with_transparent_bg()
                    // An unpadded child is NOT unpadded: afterhours applies
                    // Spacing::sm (a fraction of the SCREEN) when every side is
                    // Dim::None, which slid the title right and made the inset
                    // window-size dependent. Zero it explicitly (gap #76).
                    .with_padding(Padding{.top = pixels(0),
                                          .left = pixels(0),
                                          .bottom = pixels(0),
                                          .right = pixels(0)})
                    .with_custom_text_color(txt)
                    .with_font_size(theme::type::ROW)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(baseLayer)
                    .with_debug_name("tab_label"));
            // Pin glyph, DRAWN: the font silently drops a pushpin codepoint
            // (gap #48), so it is a shape in the tab's left gutter.
            if (tab.pinned)
                div(ctx, mk(uiRoot, 960 + static_cast<int>(i)),
                    ComponentConfig{}
                        .with_label(" ")
                        .with_size(ComponentSize{pixels(10), pixels(12)})
                        .with_absolute_position()
                        .with_translate(tabX + 11.0f,
                                        tabY + (tabH - 12.0f) * 0.5f)
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_render_layer(baseLayer + 1)
                        .with_on_draw_fg([txt](RectangleType rc) {
                            hanabi::glyph::pin(rc, txt);
                        })
                        .with_debug_name("tab_pin"));

            // No accent bar and no bridge: the fill delta IS the
            // active-tab cue (the reference has neither), and the strip's
            // hairline runs unbroken under every tab.

            // Preview marker: a short bar in the tab's left gutter, over the
            // padding the label never uses, so saying "this one is temporary"
            // costs the title no width. There is at most one on screen.
            if (isPreview)
                div(ctx, mk(uiRoot, 940 + static_cast<int>(i)),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(3),
                                                 pixels(tabH - 14.0f)})
                        .with_absolute_position()
                        .with_translate(tabX + 4.0f, tabY + 7.0f)
                        .with_custom_background(tab_colors::tab_text())
                        .with_roundness(0.5f)
                        .with_render_layer(baseLayer + 1)
                        .with_debug_name("tab_preview"));

            // Click-to-focus is now resolved on RELEASE (see the drag-input
            // block above) so a press that turns into a drag doesn't also
            // switch tabs. Nothing to do on press here.

            // Close button (hidden on narrow inactive/unhovered tabs — see
            // showClose above; clamped to the visible strip so the × of a
            // right-edge partial tab never bleeds off the window frame).
            if (showClose) {
            float closeW = 16.0f;
            float closeX = tabX + tabW - closeW - 5.0f;
            float closeY = tabY + (tabH - closeW) * 0.5f;
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

        // ---- New-tab (+) at the far right of the strip --------------------
        // Its own reserved slot (kNewTabW), so however many tabs are open the
        // + is always in the same place. The glyph is a sprite blit, not a
        // typed '+', for the same reason the pin is drawn.
        {
            const float plusD = 24.0f;
            const float plusX = r.x + r.width - 17.0f - plusD * 0.5f;
            const float plusY = tabY + (tabH - plusD) * 0.5f;
            bool plusHovered = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{plusX, plusY, plusD, plusD});
            auto plusBtn = button(ctx, mk(uiRoot, 968),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(plusD), pixels(plusD)})
                    .with_absolute_position()
                    .with_translate(plusX, plusY)
                    .with_custom_background(plusHovered
                                                ? tab_colors::tab_hover()
                                                : tab_colors::strip_bg())
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(4.0f)
                    .with_render_layer(7)
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        "plus", "+",
                        plusHovered ? tab_colors::tab_text_act()
                                    : tab_colors::tab_text(),
                        15.0f))
                    .with_debug_name("tab_new"));
            if (plusBtn) app.composerOpen = true;
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
            int action;  // 0 = copy url, 1 = close others, 2 = open in split,
                         // 3 = rename, 4 = pin/unpin
        };
        std::vector<Item> items;
        if (app.client && app.client->supports_rename())
            items.push_back({"Rename\xe2\x80\xa6", 3});
        items.push_back({tab.pinned ? "Unpin tab" : "Pin tab", 4});
        items.push_back({"Copy URL", 0});
        items.push_back({"Open in split", 2});
        items.push_back({"Close others", 1});
        const Item* kItems = items.data();
        const int nItems = static_cast<int>(items.size());

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
                } else if (kItems[k].action == 2) {
                    // Open in split (I2): show this thread in the RIGHT pane
                    // beside the active one. No-op if it's the active thread.
                    app.requestSplitOpen = keepId;
                    app.view = SmartView::Chat;
                } else if (kItems[k].action == 4) {
                    tabEntity.get<Tab>().pinned = !tab.pinned;
                } else if (kItems[k].action == 3) {
                    app.renameOpen = true;
                    app.renameSessionId = keepId;
                    const auto* sum = app.find_summary(keepId);
                    app.renameDraft = sum != nullptr ? sum->title : tab.label;
                    app.renameError.clear();
                    app.renameSubmit = false;
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
                                    const std::string& id, bool keep = true,
                                    bool pinned = false) {
        model::open_session_in_tab(strip, app, id, keep, pinned);
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
                        TabBarSystem::open_session_in_tab(
                            *strip, app, id, /*keep=*/true,
                            /*pinned=*/std::find(app.restorePinnedIds.begin(),
                                                 app.restorePinnedIds.end(),
                                                 id) !=
                                app.restorePinnedIds.end());
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
            app.restorePinnedIds.clear();
        }

        if (app.requestOpenTab.empty()) return;
        std::string id = app.requestOpenTab;
        app.requestOpenTab.clear();
        auto* strip = find_singleton<TabStripComponent>();
        if (!strip) return;
        // A sidebar row clicked once is a look, not a commitment: it opens as a
        // PREVIEW and reuses whatever preview tab is already there.
        TabBarSystem::open_session_in_tab(*strip, app, id, /*keep=*/false);
    }
};

}  // namespace ecs
