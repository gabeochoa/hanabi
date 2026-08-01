#pragma once

// VS Code-style closable content tabs across the top of the main pane.
// Each open thread is a Tab entity; the focused one carries an ActiveTab
// marker. Clicking a session row (handled in AppFlowSystem) opens a new tab or
// focuses the existing one; the transcript shows whatever the active tab
// points at. Close (×) removes a tab; Cmd+W closes the active tab.
//
// Mirrors floatinghotel/src/ecs/tab_bar_system.h.

#include <algorithm>
#include <string>

#include "../util/format.h"
#include "tab_model.h"
#include "ui_imports.h"

namespace ecs {

namespace tab_colors {
// The tab strip is the recessed chrome plane (sidebar_bg == L0). An ACTIVE tab
// is the RAISED surface (panel_bg == L1) that visually connects to the content
// panel directly below it, so the active tab reads as "lifted out of" the strip
// — the clear filled state the review asked for. Inactive tabs sit flush in the
// strip; on hover they get a faint overlay wash.
inline afterhours::Color strip_bg() { return theme::sidebar_bg(); }
inline afterhours::Color tab_active() { return theme::panel_bg(); }
inline afterhours::Color tab_hover() {
    return theme::over(theme::hover_bg(), theme::sidebar_bg());
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
        // Right edge the tabs must never cross (the main-pane width). A tab or
        // its × drawn past this would bleed off the window frame.
        const float stripRight = r.x + r.width;

        // Cap total tab area so N tabs never overflow the strip. If the tabs
        // at their natural width would exceed the available strip, shrink the
        // per-tab max width so they share the space evenly. Never let a tab go
        // below minW — if even minW-width tabs won't all fit, we stop drawing
        // once we hit the right edge (below), so nothing renders off-frame.
        const size_t nTabs = strip.tabOrder.size();
        float maxW = maxWCap;
        if (nTabs > 0) {
            float perTab = r.width / static_cast<float>(nTabs);
            maxW = std::clamp(perTab, minW, maxWCap);
        }

        for (size_t i = 0; i < strip.tabOrder.size(); ++i) {
            auto tabId = strip.tabOrder[i];
            auto tabOpt = EntityHelper::getEntityForID(tabId);
            if (!tabOpt.valid() || !tabOpt->has<Tab>()) continue;
            auto& tabEntity = tabOpt.asE();
            auto& tab = tabEntity.get<Tab>();
            bool isActive = tabEntity.has<ActiveTab>();

            float labelW =
                static_cast<float>(tab.label.size()) * 7.0f + 44.0f;
            float tabW = std::clamp(labelW, minW, maxW);

            // Overflow guard: if this tab would extend past the strip's right
            // edge, clamp its width to whatever room is left; if there's no
            // usable room (< minW), stop — draw NOTHING past the edge so no tab
            // or × ever bleeds off the window frame.
            float room = stripRight - tabX;
            if (room < minW) break;
            if (tabX + tabW > stripRight) tabW = room;

            bool hovered = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{tabX, r.y, tabW, tabH});

            afterhours::Color bg = isActive  ? tab_colors::tab_active()
                                   : hovered ? tab_colors::tab_hover()
                                             : tab_colors::strip_bg();
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
                    .with_rounded_corners(
                        afterhours::ui::imm::RoundedCorners().all_sharp().top_round().get())
                    .with_render_layer(6)
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
                    .with_render_layer(6)
                    .with_debug_name("tab_label"));

            // Active underline accent.
            if (isActive) {
                div(ctx, mk(uiRoot, 930 + static_cast<int>(i)),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(tabW), pixels(2)})
                        .with_absolute_position()
                        .with_translate(tabX, r.y + tabH - 2.0f)
                        .with_custom_background(tab_colors::accent())
                        .with_roundness(0.0f)
                        .with_render_layer(7)
                        .with_debug_name("tab_underline"));
            }

            bool clicked = hovered && ctx.mouse.just_pressed;
            if (clicked && !isActive) {
                switch_to_tab(app, tabEntity);
            }

            // Close button.
            float closeW = 16.0f;
            float closeX = tabX + tabW - closeW - 5.0f;
            float closeY = r.y + (tabH - closeW) * 0.5f;
            bool closeHovered = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{closeX, closeY, closeW, closeW});
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
                    .with_render_layer(7)
                    .with_debug_name("tab_close"));
            if (closeHovered && ctx.mouse.just_pressed) {
                close_tab(strip, app, tabId, i, isActive);
                ctx.mouse.just_pressed = false;
                return;
            }

            // Divider between tabs.
            if (i < strip.tabOrder.size() - 1) {
                div(ctx, mk(uiRoot, 970 + static_cast<int>(i)),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(1), pixels(tabH)})
                        .with_absolute_position()
                        .with_translate(tabX + tabW, r.y)
                        .with_custom_background(tab_colors::border())
                        .with_roundness(0.0f)
                        .with_render_layer(6)
                        .with_debug_name("tab_divider"));
            }

            tabX += tabW;
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
