#pragma once

// Auth overlay (Phase AUTH). Renders a centered device-code login panel over a
// dimmed backdrop when AppComponent::showAuth is true. It is a pure RENDERER of
// the DeviceCodeFlow state (the flow's logic + polling live in main.cpp / the
// pure api::DeviceCodeFlow): a big user_code, the verification URL to open, a
// "waiting for approval…" line, and the success / failure / expired states. It
// offers a single escape button ("Use offline (mock)") that sets
// requestAuthCancel so main.cpp can dismiss the overlay and keep the mock
// client — so the user is never trapped.
//
// Only shown when auth is actually needed (main.cpp sets showAuth). With no
// auth configured, showAuth stays false and this system draws nothing, so the
// default experience is byte-for-byte unchanged.
//
// Mirrors settings_system.h: pre-blended scrim backdrop (the UI fill pipeline
// has alpha blending disabled — afterhours gap #13), centered panel, immediate
// mode. Owns this file only.

#include <string>

#include "ui_imports.h"

namespace ecs {

struct AuthSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;
        if (!app->showAuth) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            static_cast<float>(afterhours::graphics::get_screen_width());
        const float sh =
            static_cast<float>(afterhours::graphics::get_screen_height());

        // Dimmed backdrop. Not click-to-dismiss: auth is a deliberate decision,
        // the user leaves via the explicit escape button (mirrors a modal login).
        div(ctx, mk(uiRoot, 8100),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(sw), pixels(sh)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_custom_background(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_roundness(0.0f)
                .with_render_layer(12)
                .with_debug_name("auth_backdrop"));

        const float pw = 420.0f;
        const float ph = 300.0f;
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
                .with_padding(Padding{.top = pixels(22), .right = pixels(26),
                                      .bottom = pixels(22), .left = pixels(26)})
                .with_roundness(0.35f)
                .with_render_layer(13)
                .with_debug_name("auth_panel"));

        render_title(ctx, panel.ent());
        render_body(ctx, panel.ent(), *app);
        render_escape(ctx, panel.ent(), *app);
    }

  private:
    using State = api::DeviceCodeFlow::State;

    static State flow_state(AppComponent& app) {
        return app.authFlow ? app.authFlow->current_state()
                            : State::AwaitingUser;
    }

    void render_title(UIContext<InputAction>& ctx, Entity& parent) {
        div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_label("Sign in")
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("auth_title"));
    }

    void render_body(UIContext<InputAction>& ctx, Entity& parent,
                     AppComponent& app) {
        State st = flow_state(app);
        const std::string code =
            app.authFlow ? app.authFlow->user_code() : std::string();
        const std::string uri =
            app.authFlow ? app.authFlow->verification_uri() : std::string();

        if (st == State::Success) {
            label_row(ctx, parent, 2, "Signed in.", theme::text_primary(),
                      FontSize::Large, 40);
            label_row(ctx, parent, 3, "Loading your sessions\xe2\x80\xa6",
                      theme::text_secondary(), FontSize::Medium, 24);
            return;
        }
        if (st == State::Failed) {
            label_row(ctx, parent, 2, "Sign-in failed.", theme::text_primary(),
                      FontSize::Large, 40);
            const std::string reason =
                app.authFlow && !app.authFlow->error().empty()
                    ? app.authFlow->error()
                    : "Please try again.";
            label_row(ctx, parent, 3, reason, theme::text_secondary(),
                      FontSize::Small, 24);
            return;
        }
        if (st == State::Expired) {
            label_row(ctx, parent, 2, "Code expired.", theme::text_primary(),
                      FontSize::Large, 40);
            label_row(ctx, parent, 3,
                      "Restart hanabi to request a fresh code.",
                      theme::text_secondary(), FontSize::Small, 24);
            return;
        }

        // Idle / RequestingCode / AwaitingUser / Polling: show the code + URL.
        label_row(ctx, parent, 2, "Open this URL in your browser:",
                  theme::text_secondary(), FontSize::Small, 22);
        label_row(ctx, parent, 3, uri.empty() ? "\xe2\x80\xa6" : uri,
                  theme::accent(), FontSize::Medium, 26);
        label_row(ctx, parent, 4, "and enter this code:",
                  theme::text_secondary(), FontSize::Small, 24);
        // Big user code.
        label_row(ctx, parent, 5, code.empty() ? "\xe2\x80\xa6" : code,
                  theme::text_primary(), FontSize::Large, 44);

        const bool requesting = (st == State::RequestingCode);
        label_row(ctx, parent, 6,
                  requesting ? "Requesting a code\xe2\x80\xa6"
                             : "Waiting for approval\xe2\x80\xa6",
                  theme::text_faint(), FontSize::Small, 24);
    }

    void render_escape(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app) {
        State st = flow_state(app);
        // Success needs no escape (main.cpp dismisses on success); everything
        // else offers the offline fallback so the user is never trapped.
        if (st == State::Success) return;
        auto btn = button(ctx, mk(parent, 20),
            ComponentConfig{}
                .with_label("Use offline (mock)")
                .with_size(ComponentSize{pixels(180), pixels(34)})
                .with_margin(Margin{.top = pixels(10)})
                .with_custom_background(theme::button_secondary())
                .with_custom_hover_bg(theme::hover_over(theme::button_secondary()))
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("auth_use_offline"));
        if (btn) app.requestAuthCancel = true;
    }

    void label_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                   const std::string& text, theme::Color color, FontSize size,
                   float h) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(h)})
                .with_padding(Padding{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(color)
                .with_font_size(size)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("auth_label"));
    }
};

}  // namespace ecs
