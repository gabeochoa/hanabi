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
#include "../ui/secondary_surface.h"
#include "keyboard_focus.h"
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

    static constexpr float kPanelW = 560.0f;
    static constexpr float kFieldH = hanabi::surface::kFieldH;
    static constexpr float kRowH = hanabi::surface::kMenuRowH;
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
        const bool justOpened = app->paletteOpen && !wasOpen_;
        wasOpen_ = app->paletteOpen;
        if (!app->paletteOpen) return;
        if (justOpened) focusFrames_ = 3;

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

        const size_t visibleRows = std::min(rows.size(), kMaxRows);
        const float wantedH =
            hanabi::surface::kSheetPadV * 2.0f +
            hanabi::surface::kHeaderH + kFieldH + 8.0f +
            kRowH * static_cast<float>(std::max<size_t>(visibleRows, 1));
        const hanabi::surface::Rect panelRect =
            hanabi::surface::top_centered(sw, sh, kPanelW, wantedH);
        const float contentW =
            panelRect.width - hanabi::surface::kSheetPadH * 2.0f;

        auto backdrop = button(
            ctx, mk(uiRoot, 8300),
            hanabi::surface::scrim(sw, sh, 10)
                .with_debug_name("palette_backdrop"));
        if (backdrop &&
            !afterhours::ui::is_mouse_inside(
                ctx.mouse.pos,
                RectangleType{panelRect.x, panelRect.y, panelRect.width,
                              panelRect.height})) {
            close(*app);
            return;
        }

        auto panel = div(
            ctx, mk(uiRoot, 8310),
            hanabi::surface::sheet(panelRect, 11)
                .with_debug_name("palette_panel"));

        auto header = div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kHeaderH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("palette_header"));
        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label("Command palette")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kTitleH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("palette_title"));
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label("Run an action or open a conversation")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kSubtitleH)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("palette_subtitle"));

        const std::string before = app->paletteQuery;
        auto input = afterhours::ui::imm::text_input(
            ctx, mk(panel.ent(), 2), app->paletteQuery,
            hanabi::surface::field(contentW, 11)
                .with_debug_name("palette_input"));
        if (focusFrames_ > 0) {
            --focusFrames_;
            ctx.set_focus(focusable_field(input.ent()));
        }
        if (app->paletteQuery != before) app->paletteIndex = 0;

        const float listH = std::max(
            kRowH, panelRect.height - hanabi::surface::kSheetPadV * 2.0f -
                       hanabi::surface::kHeaderH - kFieldH - 8.0f);
        auto list = div(ctx, mk(panel.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(contentW), pixels(listH)})
                .with_margin(Margin{.top = pixels(8)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_overflow(Overflow::Scroll, Axis::Y)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("palette_results"));

        if (rows.empty()) {
            div(ctx, mk(list.ent(), 2),
                ComponentConfig{}
                    .with_label("No matches. Try an action or conversation title.")
                    .with_size(ComponentSize{pixels(contentW), pixels(kRowH)})
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
            auto row = button(
                ctx, mk(list.ent(), 100 + static_cast<int>(i)),
                hanabi::surface::option_row(contentW, kRowH, selected, 11)
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_debug_name("palette_row_" + std::to_string(i)));

            div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_label(rows[i].label)
                    .with_size(ComponentSize{pixels(contentW - 126.0f),
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
                        .with_size(ComponentSize{pixels(100), pixels(16)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_faint())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Right)
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

    bool wasOpen_ = false;
    int focusFrames_ = 0;
};

}  // namespace ecs
