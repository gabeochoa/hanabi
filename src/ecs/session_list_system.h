#pragma once

// Renders the left sidebar: a scrollable list of session summaries. Clicking a
// row requests that transcript be opened (handled by LoaderSystem).

#include <string>

#include "../util/format.h"
#include "ui_imports.h"

namespace ecs {

struct SessionListSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto& r = layout->sidebar;

        // Sidebar background panel.
        auto panel = div(ctx, mk(uiRoot, 1000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::SIDEBAR_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_roundness(0.0f)
                .with_render_layer(1)
                .with_debug_name("sidebar"));

        // Header.
        div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_label("Sessions")
                .with_size(ComponentSize{percent(1.0f), pixels(40)})
                .with_padding(Padding{.top = pixels(12), .right = pixels(14),
                                      .bottom = pixels(6), .left = pixels(14)})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_header"));

        // States.
        if (app->listState == LoadState::Loading && app->sessions.empty()) {
            simple_note(ctx, panel.ent(), "Loading\xe2\x80\xa6");
            return;
        }
        if (app->listState == LoadState::Error) {
            simple_note(ctx, panel.ent(), "Error: " + app->listError);
            return;
        }
        if (app->sessions.empty()) {
            simple_note(ctx, panel.ent(), "No sessions");
            return;
        }

        // Scrollable list.
        float listH = r.height - 40.0f;
        if (listH < 20.0f) listH = 20.0f;
        auto scroll = div(ctx, mk(panel.ent(), 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_debug_name("session_scroll"));

        int i = 0;
        for (const auto& s : app->sessions) {
            render_row(ctx, scroll.ent(), i, s, app->selectedId == s.id, *app);
            ++i;
        }
    }

  private:
    static void simple_note(UIContext<InputAction>& ctx, Entity& parent,
                            const std::string& text) {
        div(ctx, mk(parent, 90),
            preset::EmptyStateText(text)
                .with_size(ComponentSize{percent(1.0f), pixels(40)})
                .with_padding(Padding{.top = pixels(12), .right = pixels(14),
                                      .bottom = pixels(6), .left = pixels(14)})
                .with_alignment(TextAlignment::Left)
                .with_debug_name("sidebar_note"));
    }

    static theme::Color status_pip(const std::string& status) {
        if (status == "active") return theme::STATUS_ACTIVE;
        if (status == "archived") return theme::STATUS_ARCHIVED;
        return theme::STATUS_IDLE;
    }

    void render_row(UIContext<InputAction>& ctx, Entity& parent, int index,
                    const api::SessionSummary& s, bool selected,
                    AppComponent& app) {
        constexpr float ROW_H = 58.0f;
        auto row = div(ctx, mk(parent, 100 + index * 10),
            preset::SelectableRow(selected)
                .with_size(ComponentSize{percent(1.0f), pixels(ROW_H)})
                .with_flex_direction(FlexDirection::Column)
                .with_padding(Padding{.top = pixels(8), .right = pixels(12),
                                      .bottom = pixels(8), .left = pixels(14)})
                .with_roundness(0.0f)
                .with_debug_name("session_row"));

        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (row.ent().get<afterhours::ui::HasClickListener>().down) {
            app.requestOpenId = s.id;
        }

        // Title line.
        std::string title = s.title.empty() ? "(untitled)" : s.title;
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(title, 34))
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("row_title"));

        // Subtitle: relative time + status.
        std::string age = fmtutil::relative_time(s.updated_at);
        std::string sub = age;
        if (!s.status.empty()) sub += sub.empty() ? s.status : "  \xc2\xb7  " + s.status;
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(sub)
                .with_size(ComponentSize{percent(1.0f), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(status_pip(s.status))
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("row_sub"));
    }
};

}  // namespace ecs
