#pragma once

// The rename modal: a centered sheet over a dimmed backdrop with the session's
// current title in a focused, selected field.
//
// Return (or the Rename button) hands the title to the loader and the modal
// STAYS OPEN with a spinner until the server's durable `session_renamed` echo
// lands — nothing in the app shows the new title before then. A refusal comes
// back on AppComponent::renameError and is shown under the field with the
// user's text still there to fix. Escape / Cancel / the backdrop close it.

#include <string>

#include "../keys.h"
#include "keyboard_focus.h"
#include "ui_imports.h"
#include "../../vendor/afterhours/src/plugins/ui/text_input/text_input.h"

namespace ecs {

struct RenameModalSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        const bool justOpened = app->renameOpen && !wasOpen_;
        wasOpen_ = app->renameOpen;
        if (!app->renameOpen) return;

        if (app->escape == EscapeIntent::CloseRename) {
            close(*app);
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            hanabi::viewport::width();
        const float sh =
            hanabi::viewport::height();

        auto backdrop = button(ctx, mk(uiRoot, 8200),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(sw), pixels(sh)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_custom_background(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_custom_hover_bg(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_render_layer(12)
                .with_debug_name("rename_backdrop"));
        if (backdrop && !app->renamePending) {
            close(*app);
            return;
        }

        const float pw = 420.0f;
        const float ph = 186.0f;
        auto panel = div(ctx, mk(uiRoot, 8210),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(pw), pixels(ph)})
                .with_absolute_position()
                .with_translate((sw - pw) * 0.5f, (sh - ph) * 0.5f)
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(18), .right = pixels(20),
                                      .bottom = pixels(18), .left = pixels(20)})
                .with_roundness(0.35f)
                .with_render_layer(13)
                .with_debug_name("rename_panel"));

        div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_label("Rename session")
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("rename_title"));

        auto field = afterhours::ui::imm::text_input(
            ctx, mk(panel.ent(), 2), app->renameDraft,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(34)})
                .with_margin(Margin{.top = pixels(10)})
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_render_layer(13)
                .with_debug_name("rename_input"));

        // Focusing the field without a mouse press is also what selects the
        // whole title, so the first keystroke replaces it (text_input.h).
        //
        // It is the text_input's inner FIELD that can hold focus — the outer
        // entity carries no click listener, so focus set on it is dropped at
        // the end of the frame (can_be_focused / EndUIContextManager). The
        // field also has to have been rendered once before it can be grabbed,
        // hence a short window rather than a single set.
        if (justOpened) focusFrames_ = 3;
        if (focusFrames_ > 0) {
            --focusFrames_;
            ctx.set_focus(focusable_field(field.ent()));
        }

        field.ent().addComponentIfMissing<
            afterhours::text_input::HasTextInputListener>(
            nullptr, [appPtr = app](Entity&) { appPtr->renameSubmit = true; });

        div(ctx, mk(panel.ent(), 3),
            ComponentConfig{}
                .with_label(app->renameError)
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_margin(Margin{.top = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(theme::destructive())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("rename_error"));

        render_actions(ctx, panel.ent(), *app);

        if (app->renameSubmit) {
            app->renameSubmit = false;
            confirm(*app);
        }
    }

  private:
    void render_actions(UIContext<InputAction>& ctx, Entity& parent,
                        AppComponent& app) {
        auto row = div(ctx, mk(parent, 4),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(38)})
                .with_padding(Padding{.top = pixels(10)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("rename_actions"));

        if (app.renamePending) {
            div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_label("Renaming\xe2\x80\xa6")
                    .with_size(ComponentSize{pixels(120), pixels(32)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(FontSize::Medium)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("rename_spinner"));
            return;
        }

        auto cancel = button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label("Cancel")
                .with_size(ComponentSize{pixels(92), pixels(32)})
                .with_margin(Margin{.right = pixels(8)})
                .with_custom_background(theme::button_secondary())
                .with_custom_hover_bg(
                    theme::hover_over(theme::button_secondary()))
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("rename_cancel"));
        if (cancel) {
            close(app);
            return;
        }

        auto confirmBtn = button(ctx, mk(row.ent(), 3),
            ComponentConfig{}
                .with_label("Rename")
                .with_size(ComponentSize{pixels(92), pixels(32)})
                .with_custom_background(theme::button_primary())
                .with_custom_hover_bg(theme::button_primary())
                .with_custom_text_color(theme::window_bg())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("rename_confirm"));
        if (confirmBtn) confirm(app);
    }

    // Hand the title to the loader. The modal keeps the text and stays up: the
    // sidebar row and the tab change only once the echo comes back.
    static void confirm(AppComponent& app) {
        if (app.renamePending) return;
        app.renameError.clear();
        app.requestRenameId = app.renameSessionId;
        app.requestRenameTitle = app.renameDraft;
        app.renamePending = true;
    }

    static void close(AppComponent& app) {
        app.renameOpen = false;
        app.renameSessionId.clear();
        app.renameDraft.clear();
        app.renameError.clear();
        app.renameSubmit = false;
    }

    bool wasOpen_ = false;
    int focusFrames_ = 0;
};

}  // namespace ecs
