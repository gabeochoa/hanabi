#pragma once

// The toast: one transient bar at the bottom of the window carrying a message
// and, optionally, a single action.
//
// Archive is what raises it today. Filing a thread away is easy to do by
// accident and the Archived view is not where the user is looking, so the
// action has to be takeable back without going to find it. The bar counts
// itself down and disappears; Undo re-runs the same toggle the menu ran, which
// is why undoing an undo is simply archiving again.

#include <string>

#include "ui_imports.h"

namespace ecs {

struct ToastSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float dt) override {
        auto* app = find_singleton<AppComponent>();
        if (!app || app->toastMessage.empty()) return;

        app->toastSecondsLeft -= dt;
        if (app->toastSecondsLeft <= 0.0f) {
            app->dismiss_toast();
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            static_cast<float>(afterhours::graphics::get_screen_width());
        const float sh =
            static_cast<float>(afterhours::graphics::get_screen_height());

        const bool undoable = !app->toastUndoSessionId.empty();
        const float barW = undoable ? 300.0f : 220.0f;
        const float barH = 38.0f;
        // Clear of the status bar, so the toast never covers the one strip
        // that is supposed to be readable at all times.
        const float y = sh - barH - 44.0f;

        auto bar = div(ctx, mk(uiRoot, 8300),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(barW), pixels(barH)})
                .with_absolute_position()
                .with_translate((sw - barW) * 0.5f, y)
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(6), .right = pixels(10),
                                      .bottom = pixels(6), .left = pixels(14)})
                .with_roundness(0.35f)
                .with_render_layer(20)
                .with_debug_name("toast"));

        div(ctx, mk(bar.ent(), 1),
            ComponentConfig{}
                .with_label(app->toastMessage)
                .with_size(ComponentSize{pixels(barW - 130.0f), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("toast_message"));

        if (undoable) {
            auto undo = button(ctx, mk(bar.ent(), 2),
                ComponentConfig{}
                    .with_label("Undo")
                    .with_size(ComponentSize{pixels(64), pixels(26)})
                    .with_custom_background(theme::button_secondary())
                    .with_custom_hover_bg(
                        theme::hover_over(theme::button_secondary()))
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::ROW)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.3f)
                    .with_debug_name("toast_undo"));
            if (undo) {
                app->requestToggleArchive = app->toastUndoSessionId;
                // The toggle raises its own toast next frame, saying what the
                // undo did; leaving this one up would stack two.
                app->dismiss_toast();
                return;
            }
        }

        auto close = button(ctx, mk(bar.ent(), 3),
            ComponentConfig{}
                .with_label("\xc3\x97")
                .with_size(ComponentSize{pixels(24), pixels(26)})
                .with_margin(Margin{.left = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_debug_name("toast_close"));
        if (close) app->dismiss_toast();
    }
};

}  // namespace ecs
