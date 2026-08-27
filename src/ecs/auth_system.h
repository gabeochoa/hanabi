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

#include "../ui/secondary_surface.h"
#include "ui_imports.h"

namespace ecs {

struct AuthSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;
        if (!app->showAuth) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            hanabi::viewport::width();
        const float sh =
            hanabi::viewport::height();

        div(ctx, mk(uiRoot, 8100),
            hanabi::surface::scrim(sw, sh, 12)
                .with_debug_name("auth_backdrop"));

        const auto panelRect =
            hanabi::surface::centered(sw, sh, 440.0f, 340.0f);
        auto panel = div(
            ctx, mk(uiRoot, 8110),
            hanabi::surface::sheet(panelRect, 13)
                .with_debug_name("auth_panel"));

        render_title(ctx, panel.ent());
        const float bodyH = std::max(
            60.0f, panelRect.height - hanabi::surface::kSheetPadV * 2.0f -
                       hanabi::surface::kHeaderH - 48.0f);
        auto body = div(ctx, mk(panel.ent(), 30),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(bodyH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_overflow(Overflow::Scroll, Axis::Y)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("auth_body"));
        render_body(ctx, body.ent(), *app);
        render_escape(ctx, panel.ent(), *app);
    }

  private:
    using State = api::DeviceCodeFlow::State;

    static State flow_state(AppComponent& app) {
        return app.authFlow ? app.authFlow->current_state()
                            : State::AwaitingUser;
    }

    void render_title(UIContext<InputAction>& ctx, Entity& parent) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kHeaderH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("auth_header"));
        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label("Sign in to Agentcloud")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kTitleH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("auth_title"));
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label("Approve this device to load your conversations")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kSubtitleH)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("auth_subtitle"));
    }

    void render_body(UIContext<InputAction>& ctx, Entity& parent,
                     AppComponent& app) {
        // Launch-perf: while the deferred begin() runs on a worker thread the
        // flow's fields are being written off-thread, so DON'T read them here.
        // Show a stable pre-code screen (reads only the app flag). Once begin()
        // resolves LoaderSystem clears authBeginPending and the normal
        // state-driven body below renders the real code/URL.
        if (app.authBeginPending) {
            label_row(ctx, parent, 2, "Requesting a code\xe2\x80\xa6",
                      theme::text_primary(), FontSize::Large, 40);
            label_row(ctx, parent, 3, "Contacting the sign-in server\xe2\x80\xa6",
                      theme::text_secondary(), FontSize::Medium, 24);
            return;
        }
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
            label_row(ctx, parent, 2, "Sign-in failed.", theme::destructive(),
                      FontSize::Large, 40);
            const std::string reason =
                app.authFlow && !app.authFlow->error().empty()
                    ? app.authFlow->error()
                    : "Please try again.";
            value_row(ctx, parent, 3, reason, theme::text_primary(), 48.0f,
                      false, true);
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

        label_row(ctx, parent, 2, "1. Open this URL", theme::text_secondary(),
                  FontSize::Small, 22);
        value_row(ctx, parent, 3, uri.empty() ? "\xe2\x80\xa6" : uri,
                  theme::accent(), 38.0f, false);
        label_row(ctx, parent, 4, "2. Enter this code", theme::text_secondary(),
                  FontSize::Small, 24);
        value_row(ctx, parent, 5, code.empty() ? "\xe2\x80\xa6" : code,
                  theme::text_primary(), 48.0f, true);

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
            hanabi::surface::action_button(180.0f, false, 13)
                .with_label("Continue offline")
                .with_margin(Margin{.top = pixels(10)})
                .with_font_size(FontSize::Medium)
                .with_justify_content(JustifyContent::Center)
                .with_debug_name("auth_use_offline"));
        if (btn) app.requestAuthCancel = true;
    }

    void value_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                   const std::string& text, theme::Color color, float h,
                   bool emphasized, bool danger = false) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(h)})
                .with_padding(Padding{.top = pixels(6), .left = pixels(12),
                                      .bottom = pixels(6), .right = pixels(12)})
                .with_custom_background(
                    danger ? hanabi::surface::destructive_surface()
                           : emphasized
                                 ? theme::over(theme::accent_soft(),
                                               theme::panel_bg_2())
                                 : theme::panel_bg_2())
                .with_border(danger ? theme::destructive()
                                    : emphasized ? theme::accent()
                                                 : theme::border(),
                             pixels(1.0f))
                .with_custom_text_color(color)
                .with_font_size(emphasized ? FontSize::Large : FontSize::Medium)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_debug_name(emphasized ? "auth_code" : "auth_url"));
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
