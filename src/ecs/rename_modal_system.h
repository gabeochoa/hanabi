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
#include "../ui/secondary_surface.h"
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

        auto backdrop = button(
            ctx, mk(uiRoot, 8200),
            hanabi::surface::scrim(sw, sh, 12)
                .with_debug_name("rename_backdrop"));
        if (backdrop && !app->renamePending) {
            close(*app);
            return;
        }

        const auto panelRect =
            hanabi::surface::centered(sw, sh, 440.0f, 230.0f);
        auto panel = div(
            ctx, mk(uiRoot, 8210),
            hanabi::surface::sheet(panelRect, 13)
                .with_debug_name("rename_panel"));

        auto header = div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kHeaderH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("rename_header"));
        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label("Rename session")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kTitleH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("rename_title"));
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label("Use a short title you can recognize later")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kSubtitleH)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("rename_subtitle"));

        auto field = afterhours::ui::imm::text_input(
            ctx, mk(panel.ent(), 2), app->renameDraft,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kFieldH)})
                .with_margin(Margin{.top = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_corner_radius(hanabi::surface::kControlCorner)
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
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_margin(Margin{.top = pixels(6)})
                .with_padding(Padding{.left = pixels(8)})
                .with_custom_background(app->renameError.empty()
                                            ? theme::panel_bg()
                                            : hanabi::surface::destructive_surface())
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
                .with_size(ComponentSize{percent(1.0f), pixels(48)})
                .with_padding(Padding{.top = pixels(14)})
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
            hanabi::surface::action_button(92.0f, false, 13)
                .with_label("Cancel")
                .with_margin(Margin{.right = pixels(8)})
                .with_font_size(FontSize::Medium)
                .with_justify_content(JustifyContent::Center)
                .with_debug_name("rename_cancel"));
        if (cancel) {
            close(app);
            return;
        }

        auto confirmBtn = button(ctx, mk(row.ent(), 3),
            hanabi::surface::action_button(92.0f, true, 13)
                .with_label("Rename")
                .with_font_size(FontSize::Medium)
                .with_justify_content(JustifyContent::Center)
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
