#pragma once

// ---------------------------------------------------------------------------
// The command palette (Cmd+K).
//
// Everything this app can do is behind a chord nobody has memorised or a menu
// nobody opens — the shortcuts sheet exists precisely because the bindings are
// invisible. The palette is the other half of that answer: type what you want
// instead of remembering where it lives.
//
// It searches two things at once, because that is what the question "where is
// the thing I want" actually means: the app's own actions, and your threads.
// A thread row opens the thread; an action row does the action.
//
// It deliberately does NOT own any behaviour of its own. Every row sets the
// same request flag the button or the chord sets, so the palette can never
// drift from what the app does — a row that stopped working would be a row
// whose feature stopped working.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <string>
#include <vector>

#include "../keys.h"
#include "../util/format.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct PaletteSystem : afterhours::System<UIContext<InputAction>> {
    // What a row does when it is chosen. An enum rather than a std::function
    // so the list can be rebuilt every frame for free.
    enum class Act {
        NewTask,
        ToggleSidebar,
        OpenSettings,
        OpenShortcuts,
        FindInThread,
        SearchThreads,
        ToggleSplit,
        OpenThread,
    };

    struct Row {
        std::string label;
        std::string hint;   // the chord, or the thread's relative time
        Act act;
        std::string arg;    // the session id, for OpenThread
    };

    static constexpr float kPanelW = 520.0f;
    static constexpr float kFieldH = 34.0f;
    static constexpr float kRowH = 28.0f;
    static constexpr size_t kMaxRows = 8;

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        auto* layout = find_singleton<LayoutComponent>();
        if (!app) return;

        if (hanabi::keys::cmd_down() &&
            hanabi::keys::pressed(hanabi::keys::kK)) {
            app->paletteOpen = !app->paletteOpen;
            app->paletteQuery.clear();
            app->paletteIndex = 0;
        }
        if (!app->paletteOpen) return;

        if (app->escape == EscapeIntent::ClosePalette) {
            close(*app);
            return;
        }

        const std::vector<Row> rows = build_rows(*app);

        // The palette owns the arrows while it is up, even though its field
        // holds the caret — moving the selection is what Up/Down mean here
        // (arrow_system.h ranks it above the text field for that reason).
        if (app->arrow == ArrowIntent::Palette && !rows.empty()) {
            app->paletteIndex += app->arrowDelta;
            if (app->paletteIndex < 0) app->paletteIndex = 0;
            if (app->paletteIndex >= static_cast<int>(rows.size()))
                app->paletteIndex = static_cast<int>(rows.size()) - 1;
        }
        if (hanabi::keys::pressed(hanabi::keys::kEnter) && !rows.empty()) {
            const size_t i = static_cast<size_t>(std::clamp(
                app->paletteIndex, 0, static_cast<int>(rows.size()) - 1));
            run(rows[i], *app, layout);
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            hanabi::viewport::width();
        const float sh =
            hanabi::viewport::height();

        auto backdrop = button(ctx, mk(uiRoot, 8300),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(sw), pixels(sh)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_custom_background(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_custom_hover_bg(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_render_layer(10)
                .with_debug_name("palette_backdrop"));
        if (backdrop) {
            close(*app);
            return;
        }

        // Near the top rather than centred: the list grows downward as you
        // type, and a centred panel would walk up the screen while you did it.
        const float ph = kFieldH + 16.0f +
                         kRowH * static_cast<float>(
                                     std::min(rows.size(), kMaxRows)) +
                         16.0f;
        const float px = (sw - kPanelW) * 0.5f;
        const float py = std::min(120.0f, sh * 0.15f);

        auto panel = div(ctx, mk(uiRoot, 8310),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kPanelW), pixels(ph)})
                .with_absolute_position()
                .with_translate(px, py)
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(8), .right = pixels(8),
                                      .bottom = pixels(8), .left = pixels(8)})
                .with_roundness(0.35f)
                .with_render_layer(11)
                .with_debug_name("palette_panel"));

        const std::string before = app->paletteQuery;
        afterhours::ui::imm::text_input(ctx, mk(panel.ent(), 1),
            app->paletteQuery,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kFieldH)})
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_render_layer(11)
                .with_debug_name("palette_input"));
        // Typing re-ranks the list, so a selection held over from the old
        // ranking would point at something the user never looked at.
        if (app->paletteQuery != before) app->paletteIndex = 0;

        if (rows.empty()) {
            div(ctx, mk(panel.ent(), 2),
                ComponentConfig{}
                    .with_label("Nothing matches")
                    .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                    .with_margin(Margin{.top = pixels(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name("palette_empty"));
            return;
        }

        for (size_t i = 0; i < rows.size() && i < kMaxRows; ++i) {
            const bool selected = static_cast<int>(i) == app->paletteIndex;
            auto row = button(ctx, mk(panel.ent(), 100 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                    .with_margin(Margin{.top = pixels(i == 0 ? 8 : 0)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_custom_background(selected ? theme::selected_bg()
                                                     : theme::panel_bg())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.3f)
                    .with_render_layer(11)
                    .with_debug_name("palette_row_" + std::to_string(i)));

            div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_label(rows[i].label)
                    .with_size(ComponentSize{pixels(kPanelW - 150.0f),
                                             pixels(18)})
                    .with_margin(Margin{.left = pixels(10)})
                    .with_transparent_bg()
                    .with_custom_text_color(selected ? theme::text_primary()
                                                     : theme::text_secondary())
                    .with_font_size(theme::type::BODY)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name("palette_label_" + std::to_string(i)));

            if (!rows[i].hint.empty())
                div(ctx, mk(row.ent(), 2),
                    ComponentConfig{}
                        .with_label(rows[i].hint)
                        .with_size(ComponentSize{children(), pixels(16)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_faint())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_render_layer(11)
                        .with_debug_name("palette_hint_" +
                                         std::to_string(i)));

            if (row) {
                run(rows[i], *app, layout);
                return;
            }
        }
    }

  private:
    static void close(AppComponent& app) {
        app.paletteOpen = false;
        app.paletteQuery.clear();
        app.paletteIndex = 0;
    }

    static bool contains_ci(const std::string& haystack,
                            const std::string& needle) {
        if (needle.empty()) return true;
        const std::string h = fmtutil::to_lower(haystack);
        const std::string n = fmtutil::to_lower(needle);
        return h.find(n) != std::string::npos;
    }

    // Actions first, then threads: an action is a fixed short list the user
    // can learn, and burying it under 2000 thread titles would make the
    // palette a thread switcher with some commands hidden at the bottom.
    static std::vector<Row> build_rows(const AppComponent& app) {
        const std::string& q = app.paletteQuery;
        std::vector<Row> out;

        const Row actions[] = {
            {"New task", "Cmd N", Act::NewTask, ""},
            {"Show or hide the sidebar", "Cmd B", Act::ToggleSidebar, ""},
            {"Split the pane", "Cmd \\", Act::ToggleSplit, ""},
            {"Settings", "Cmd ,", Act::OpenSettings, ""},
            {"Keyboard shortcuts", "Cmd /", Act::OpenShortcuts, ""},
            {"Find in this conversation", "Cmd F", Act::FindInThread, ""},
            {"Search across your threads", "Cmd Shift F", Act::SearchThreads,
             ""},
        };
        for (const auto& a : actions)
            if (contains_ci(a.label, q)) out.push_back(a);

        for (const auto& s : app.sessions) {
            if (out.size() >= kMaxRows) break;
            if (s.title.empty() || !contains_ci(s.title, q)) continue;
            out.push_back({s.title, fmtutil::relative_time(s.updated_at),
                           Act::OpenThread, s.id});
        }
        return out;
    }

    // Every action here is the SAME request the button or the chord raises.
    // The palette adds a way in, never a second implementation.
    static void run(const Row& row, AppComponent& app,
                    LayoutComponent* layout) {
        switch (row.act) {
            case Act::NewTask: app.composerOpen = true; break;
            case Act::ToggleSidebar:
                if (layout) layout->sidebarCollapsed = !layout->sidebarCollapsed;
                break;
            case Act::OpenSettings: app.showSettings = true; break;
            case Act::OpenShortcuts: app.showShortcuts = true; break;
            case Act::FindInThread: app.pane().findOpen = true; break;
            case Act::SearchThreads: app.sessionSearchOpen = true; break;
            case Act::ToggleSplit: app.requestSplitToggle = true; break;
            case Act::OpenThread: app.requestOpenTab = row.arg; break;
        }
        close(app);
    }
};

}  // namespace ecs
