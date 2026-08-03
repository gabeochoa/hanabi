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
//   * RIGHT cluster — the session count, right-aligned with a matching gutter,
//     prefixed by a small "activity light" dot. That dot behaves like a
//     hard-drive activity LED: it glows green ONLY while we are actively
//     fetching data over the network (a session-list / transcript / send /
//     stream / live refetch is in flight) and sits dim/gray when idle. It
//     replaces the old free-standing "● live" text label — the green light IS
//     the live indicator now, fused with the session count.
//
// Both clusters use symmetric vertical padding so the renderer centers their
// text within the ~26px bar, a consistent ~12px left/right gutter, and legible
// theme tokens (contrast >= 4.5:1 in BOTH modes on sidebar_bg). A hairline top
// border separates the bar from the content above it.

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

        // --- right cluster: activity light + session count ------------------
        // The dot is a hard-drive-style activity LED: green while ANY network
        // fetch is in flight (list / transcript / load-older / send / stream /
        // live refetch, incl. background per-tab live subscription refetches),
        // dim otherwise. HANABI_LIVE_DEMO=1 forces it lit for screenshots.
        // Disk-cache reads are deliberately NOT counted — this signals NETWORK
        // activity only, matching Gabe's "fetching data from the network" ask.
        const bool liveDemo = [] {
            const char* v = std::getenv("HANABI_LIVE_DEMO");
            return v && *v && std::string(v) != "0";
        }();
        bool bgLiveRefetch = false;
        for (const auto& kv : app->liveSubs)
            if (kv.second.pending) { bgLiveRefetch = true; break; }
        const bool fetching =
            liveDemo || app->listPending || app->transcriptPending ||
            app->loadingOlder || app->livePending || app->sendPending ||
            app->streamCollecting || app->kickoffPending ||
            app->settingsPending || app->authBeginPending || bgLiveRefetch;
        const auto activityColor =
            fetching ? theme::status_active() : theme::text_faint();

        std::string right = std::to_string(app->sessions.size()) + " sessions";
        const float rw = r.width * 0.4f;
        // Session count text, right-aligned within the right cluster.
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

        // Activity light: a small dot just left of the session-count text,
        // vertically centered. Sized off the MEASURED text width so the dot
        // hugs the left edge of "N sessions" instead of floating in the gutter
        // (Gabe: "the dot should be up against the session count"). The count
        // text is right-aligned against (r.x + r.width - kGutter), so its left
        // edge is that minus the real rendered width.
        constexpr float kActDot = 6.0f;
        constexpr float kActGap = 6.0f;   // dot -> text spacing
        const float rightTextW = theme::text_px(right, theme::type::MD);
        const float rightTextLeft = r.x + r.width - kGutter - rightTextW;
        const float actDotX = rightTextLeft - kActGap - kActDot;
        const float actDotY = r.y + (r.height - kActDot) * 0.5f;
        div(ctx, mk(uiRoot, 3007),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kActDot), pixels(kActDot)})
                .with_absolute_position()
                .with_translate(actDotX, actDotY)
                .with_custom_background(activityColor)
                .with_roundness(1.0f)
                .with_render_layer(5)
                .with_debug_name("status_activity_dot"));
    }
};

}  // namespace ecs
