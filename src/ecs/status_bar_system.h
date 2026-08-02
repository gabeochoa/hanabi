#pragma once

// Bottom status bar.
//
// A considered, two-cluster status bar (not two strings flung at the far
// edges):
//   * LEFT cluster  — an attention/connection indicator: a small colored dot
//     plus a short status phrase. When threads are blocked on the user it
//     reads e.g. "2 blocked on you" in the attention color; otherwise it is a
//     calm "Ready" with a subtle live dot. It never surfaces the raw backend
//     mode ("backend: mock") in normal use — a shipped app shouldn't expose
//     its transport as chrome. The backend label is only appended when the
//     HANABI_DEBUG env var is set (dev affordance, invisible to real users).
//   * RIGHT cluster — the session count, right-aligned with a matching gutter.
//
// Both clusters use symmetric vertical padding so the renderer centers their
// text within the ~26px bar, a consistent ~12px left/right gutter, and legible
// theme tokens (contrast >= 4.5:1 in BOTH modes on sidebar_bg). A hairline top
// border separates the bar from the content above it.

#include <chrono>
#include <cstdlib>
#include <string>

#include "ui_imports.h"

namespace ecs {

struct StatusBarSystem : afterhours::System<UIContext<InputAction>> {
    // Dev-only: append the backend transport label to the status text. Read
    // once (cached); a hard no-op returning false in every normal run.
    static bool debug_backend() {
        static const bool on = [] {
            const char* v = std::getenv("HANABI_DEBUG");
            return v && *v;
        }();
        return on;
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto& r = layout->statusBar;

        constexpr float kGutter = 12.0f;   // left/right inset
        constexpr float kDot = 7.0f;       // attention/status dot diameter
        constexpr float kDotGap = 7.0f;    // dot -> text spacing
        // Symmetric vertical padding => renderer vertically centers the text
        // inside (height - 2*pad). ~7px top/bottom on a 26px bar.
        const float vpad = (r.height - 14.0f) * 0.5f;

        // --- bar background + hairline top divider ---------------------------
        div(ctx, mk(uiRoot, 3000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::sidebar_bg())
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_bar_bg"));
        // 1px top border so the bar reads as a distinct plane.
        div(ctx, mk(uiRoot, 3005),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(1)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::border())
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_bar_divider"));

        // --- left cluster: attention / connection ---------------------------
        int blocked = 0;
        for (const auto& s : app->sessions)
            if (s.tag == api::ThreadTag::Blocked) ++blocked;

        const bool attention = blocked > 0;
        std::string left = attention
                               ? std::to_string(blocked) + " blocked on you"
                               : "Ready";
        // Attention text uses the intentional "blocked" hue; the calm state
        // uses text_secondary (>=4.5:1 on sidebar_bg in both themes).
        const auto text_color =
            attention ? theme::tag_blocked_fg() : theme::text_secondary();
        // Dot: attention -> blocked hue; calm -> a subtle live (active) dot.
        const auto dot_color =
            attention ? theme::tag_blocked_fg() : theme::status_active();

        if (debug_backend()) left += "  \xc2\xb7  backend: " + app->backend_label;

        // The dot, drawn as a small circle (roundness 1.0) — the font has no
        // reliable filled-bullet glyph, so we render it as UI, vertically
        // centered against the text baseline row.
        const float dotY = r.y + (r.height - kDot) * 0.5f;
        div(ctx, mk(uiRoot, 3006),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kDot), pixels(kDot)})
                .with_absolute_position()
                .with_translate(r.x + kGutter, dotY)
                .with_custom_background(dot_color)
                .with_roundness(1.0f)
                .with_render_layer(5)
                .with_debug_name("status_dot"));

        const float leftTextX = r.x + kGutter + kDot + kDotGap;
        div(ctx, mk(uiRoot, 3001),
            ComponentConfig{}
                .with_label(left)
                .with_size(ComponentSize{pixels(r.width * 0.6f), pixels(r.height)})
                .with_absolute_position()
                .with_translate(leftTextX, r.y)
                .with_padding(Padding{.top = pixels(vpad), .right = pixels(0),
                                      .bottom = pixels(vpad), .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_text_color(text_color)
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_left"));

        // --- right cluster: session count -----------------------------------
        std::string right = std::to_string(app->sessions.size()) + " sessions";
        const float rw = r.width * 0.4f;
        div(ctx, mk(uiRoot, 3002),
            ComponentConfig{}
                .with_label(right)
                .with_size(ComponentSize{pixels(rw), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x + r.width - rw, r.y)
                .with_padding(Padding{.top = pixels(vpad), .right = pixels(kGutter),
                                      .bottom = pixels(vpad), .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_right"));

        // --- live (SSE) indicator: a "● live" cluster left of the session
        // count, shown only when a real live subscription is bound to the OPEN
        // thread (mock/unconfigured http never subscribe, so this stays hidden
        // there). The dot brightens for ~1.5s after each live event so activity
        // is visible, then settles to a calm steady "connected" tone.
        // HANABI_LIVE_DEMO=1 forces it on (with a fresh flash) for screenshots.
        const bool liveDemo = [] {
            const char* v = std::getenv("HANABI_LIVE_DEMO");
            return v && *v && std::string(v) != "0";
        }();
        const bool liveConnected = liveDemo || app->openThreadLive;
        if (liveConnected) {
            const long long nowMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            const long long last =
                liveDemo ? nowMs : app->lastEventMs.load();
            const bool fresh = last > 0 && (nowMs - last) < 1500;
            const auto liveColor =
                fresh ? theme::status_active() : theme::text_faint();

            // "live" label sits just left of the session count; the dot sits
            // left of the label. Reserve a small fixed cluster width.
            constexpr float kLiveW = 34.0f;   // "live" text
            constexpr float kLiveDot = 6.0f;
            const float liveTextRight = r.x + r.width - rw;  // = session cluster left
            const float liveTextX = liveTextRight - kLiveW - kGutter;
            const float liveDotX = liveTextX - kLiveDot - 5.0f;
            const float liveDotY = r.y + (r.height - kLiveDot) * 0.5f;
            div(ctx, mk(uiRoot, 3007),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(kLiveDot), pixels(kLiveDot)})
                    .with_absolute_position()
                    .with_translate(liveDotX, liveDotY)
                    .with_custom_background(liveColor)
                    .with_roundness(1.0f)
                    .with_render_layer(5)
                    .with_debug_name("status_live_dot"));
            div(ctx, mk(uiRoot, 3008),
                ComponentConfig{}
                    .with_label("live")
                    .with_size(ComponentSize{pixels(kLiveW), pixels(r.height)})
                    .with_absolute_position()
                    .with_translate(liveTextX, r.y)
                    .with_padding(Padding{.top = pixels(vpad), .right = pixels(0),
                                          .bottom = pixels(vpad),
                                          .left = pixels(0)})
                    .with_transparent_bg()
                    .with_custom_text_color(liveColor)
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(5)
                    .with_debug_name("status_live_label"));
        }
    }
};

}  // namespace ecs
