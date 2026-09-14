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
#include "../ui/overlay_lifecycle.h"
#include "../ui/secondary_surface.h"
#include "keyboard_focus.h"
#include "../ui/edged_field.h"
#include "ui_imports.h"
#include <afterhours/src/plugins/ui/text_input/text_input.h>

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

        const auto panelRect =
            hanabi::surface::centered(sw, sh, 440.0f, 230.0f);

        auto backdrop = button(
            ctx, mk(uiRoot, 8200),
            hanabi::surface::scrim(sw, sh, 12)
                .with_debug_name("rename_backdrop"));
        if (!app->renamePending &&
            hanabi::overlay::dismisses(static_cast<bool>(backdrop),
                                       ctx.mouse.pos.x, ctx.mouse.pos.y,
                                       panelRect)) {
            close(*app);
            return;
        }

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
                .with_label(saving_view(*app)    ? "Save this filter as a view"
                            : renaming_view(*app) ? "Rename this view"
                                                  : "Rename session")
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

        auto field = hanabi::ui::edged_text_input(
            ctx, mk(panel.ent(), 2), app->renameDraft,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kFieldH)})
                .with_margin(Margin{.top = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_render_layer(13),
            "rename_input",
            hanabi::surface::kFieldH * hanabi::surface::kFieldFontRatio);

        // Focusing the field without a mouse press is also what selects the
        // whole title, so the first keystroke replaces it (text_input.h).
        //
        // It is the text_input's inner FIELD that can hold focus — the outer
        // entity carries no click listener, so focus set on it is dropped at
        // the end of the frame (can_be_focused / EndUIContextManager). The
        // field also has to have been rendered once before it can be grabbed,
        // hence a short window rather than a single set.
        if (justOpened) focusFrames_ = 3;
        // A refused name hands the caret BACK to the field: the person's
        // next keystrokes are the correction, and Confirm had taken focus
        // when it was pressed. (Found by the shelf-menu rename script: the
        // retyped name went to the button.)
        // Every refusal, not only the first: confirm() bumps a counter
        // each time it refuses, and a change in the count re-arms the
        // grab even when the message is the same words as last time.
        if (app->renameRefusals != refusalsSeen_) {
            refusalsSeen_ = app->renameRefusals;
            if (!app->renameError.empty()) focusFrames_ = 2;
        }
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
                .with_margin(Margin{.top = pixels(6), .left = pixels(8)})
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
                .with_label(saving_view(app) ? "Save" : "Rename")
                .with_font_size(FontSize::Medium)
                .with_justify_content(JustifyContent::Center)
                .with_debug_name("rename_confirm"));
        if (confirmBtn) confirm(app);
    }

    // Hand the title to the loader. The modal keeps the text and stays up: the
    // sidebar row and the tab change only once the echo comes back.
    // The same prompt serves two asks: a conversation's new title, and the
    // name of a view saved from the current filter (the reference's "Save
    // this filter as a view", SidebarColumn.saveCurrentAsView). The second
    // is marked by a reserved id in the slot the first uses for its session.
    static bool saving_view(const AppComponent& app) {
        return app.renameSessionId == model::kSaveViewPrompt;
    }
    // ... and a third: a saved view's new name (the shelf row's Rename...).
    static bool renaming_view(const AppComponent& app) {
        return model::renaming_view(app.renameSessionId).has_value();
    }

    static void confirm(AppComponent& app) {
        if (app.renamePending) return;
        app.renameError.clear();
        if (const auto viewId = model::renaming_view(app.renameSessionId)) {
            // Local and immediate, like Save: the store validates (trimmed,
            // non-empty, not a built-in, actually different) and a refusal
            // keeps the prompt up with the text intact. Nothing else moves:
            // the view keeps its id, so the lit shelf and the filter cache
            // (keyed by the record) follow the rename on their own.
            auto& store = Settings::get().saved_views_mut();
            if (!store.rename(*viewId, app.renameDraft)) {
                app.renameError = "That name cannot be used.";
                ++app.renameRefusals;
                return;
            }
            Settings::get().save_views();
            close(app);
            return;
        }
        if (saving_view(app)) {
            // Build the view from what the reader is looking at NOW -- the
            // lit shelf, the workspace, the words in the search box -- add
            // it, select it, and clear the search: the query is in the
            // shelf, and leaving it in the box would narrow the new shelf a
            // second time by the same words. A refused name (empty, or a
            // duplicate id) keeps the prompt up with a reason.
            auto& store = Settings::get().saved_views_mut();
            const hanabi::views::SavedView& current =
                store.resolve(app.savedViewId.empty()
                                  ? std::optional<std::string>{}
                                  : std::optional<std::string>{app.savedViewId});
            hanabi::views::SavedView made = hanabi::views::make_from(
                current, app.currentWorkspace, app.searchQuery, app.renameDraft);
            if (!store.add(made)) {
                app.renameError = "That name cannot be used.";
                ++app.renameRefusals;
                return;
            }
            Settings::get().save_views();
            app.savedViewId = made.id;
            Settings::get().set_selected_view(made.id);
            app.searchQuery.clear();
            close(app);
            return;
        }
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
    std::uint32_t refusalsSeen_ = 0;
};

}  // namespace ecs
