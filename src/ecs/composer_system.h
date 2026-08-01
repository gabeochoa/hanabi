#pragma once

// Composer (Phase K). Renders a centered "New task" compose sheet over a dimmed
// full-window backdrop when AppComponent::composerOpen is true. The sheet has a
// text input bound to AppComponent::composerDraft and a Start affordance.
//
// Cmd+N toggles it; Esc / the ✕ / clicking the backdrop closes it.
//
// KICKOFF (Phase SEND): on Start with text, the composer sets the one-shot
// app.requestKickoffPrompt flag, clears the draft, and closes. LoaderSystem
// services it (create_session async), then refreshes the list and opens the
// new thread. The mock creates an in-memory session; the http adapter POSTs to
// the configured chat path. (client.h/mock_client.h are shared — not edited
// here.)
//
// Owns this file only. The + button that would toggle composerOpen lives in
// sidebar_system.h (owned by another agent); until that one-line hook lands,
// Cmd+N opens/closes this overlay.

#include <string>

#include "ui_imports.h"

namespace ecs {

struct ComposerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Cmd+N toggles the composer (343/347 = super, 78 = KEY_N).
        bool cmdDown = afterhours::graphics::is_key_down(343) ||
                       afterhours::graphics::is_key_down(347);
        if (cmdDown && afterhours::graphics::is_key_pressed(78)) {
            app->composerOpen = !app->composerOpen;
        }

        if (!app->composerOpen) return;

        // Esc closes.
        if (afterhours::graphics::is_key_pressed(256)) {  // KEY_ESCAPE
            app->composerOpen = false;
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            static_cast<float>(afterhours::graphics::get_screen_width());
        const float sh =
            static_cast<float>(afterhours::graphics::get_screen_height());

        // Dimmed full-window backdrop (pre-blended, gap #13). Click = close.
        auto backdrop = button(ctx, mk(uiRoot, 8100),
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
                .with_render_layer(10)
                .with_debug_name("composer_backdrop"));
        if (backdrop) {
            app->composerOpen = false;
            return;
        }

        // Centered panel.
        const float pw = 420.0f;
        const float ph = 176.0f;
        const float px = (sw - pw) * 0.5f;
        const float py = (sh - ph) * 0.5f;

        auto panel = div(ctx, mk(uiRoot, 8110),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(pw), pixels(ph)})
                .with_absolute_position()
                .with_translate(px, py)
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(18), .right = pixels(20),
                                      .bottom = pixels(18), .left = pixels(20)})
                .with_roundness(0.35f)
                .with_render_layer(11)
                .with_debug_name("composer_panel"));

        render_header(ctx, panel.ent(), *app);
        render_input(ctx, panel.ent(), *app);
        render_actions(ctx, panel.ent(), *app);
    }

  private:
    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_header"));

        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label("New task")
                .with_size(ComponentSize{pixels(340), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_title"));

        auto closeBtn = button(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label("\xc3\x97")
                .with_size(ComponentSize{pixels(26), pixels(26)})
                .with_custom_background(theme::panel_bg())
                .with_custom_hover_bg(theme::hover_bg())
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.3f)
                .with_debug_name("composer_close"));
        if (closeBtn) app.composerOpen = false;
    }

    void render_input(UIContext<InputAction>& ctx, Entity& parent,
                      AppComponent& app) {
        // Bound text input for the new-task description. The imm text_input
        // widget writes edits straight into app.composerDraft.
        //
        // NOTE (afterhours_gaps.md #17): the text_input widget derives its font
        // size from the field HEIGHT (field_h * 0.5f) and forces its own
        // Theme::Usage::Secondary background — it ignores with_font_size /
        // with_custom_background. So the field is kept single-line-height (~34)
        // to yield a ~17px readable font, and its inner surface is themed by
        // ctx.theme (not our tokens). A caption above labels it since the
        // widget also ignores placeholder styling here.
        div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_label("Describe the task")
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_margin(Margin{.top = pixels(12)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_caption"));

        afterhours::ui::imm::text_input(ctx, mk(parent, 2), app.composerDraft,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(34)})
                .with_margin(Margin{.top = pixels(6)})
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_debug_name("composer_input"));
    }

    void render_actions(UIContext<InputAction>& ctx, Entity& parent,
                        AppComponent& app) {
        auto row = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(38)})
                .with_padding(Padding{.top = pixels(14)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_actions"));

        bool hasText = !app.composerDraft.empty();

        auto cancel = button(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label("Cancel")
                .with_size(ComponentSize{pixels(92), pixels(32)})
                .with_margin(Margin{.right = pixels(8)})
                .with_custom_background(theme::button_secondary())
                .with_custom_hover_bg(theme::hover_bg())
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("composer_cancel"));
        if (cancel) {
            app.composerOpen = false;
            return;
        }

        auto start = button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label("Start")
                .with_size(ComponentSize{pixels(92), pixels(32)})
                .with_custom_background(hasText ? theme::button_primary()
                                                : theme::disabled_bg())
                .with_custom_hover_bg(hasText ? theme::button_primary()
                                              : theme::disabled_bg())
                .with_custom_text_color(hasText ? theme::window_bg()
                                                : theme::disabled_text())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("composer_start"));
        if (start && hasText) {
            // Kick off a new session: hand the draft to the loader via the
            // one-shot requestKickoffPrompt flag (LoaderSystem runs
            // create_session async, then refreshes the list + opens the new
            // thread). Clear the draft and close the overlay.
            app.requestKickoffPrompt = app.composerDraft;
            app.composerDraft.clear();
            app.composerOpen = false;
        }
    }
};

}  // namespace ecs
