#pragma once

// Renders the right pane: the transcript of the selected session as a
// scrollable column of message bubbles.

#include <string>

#include "../util/format.h"
#include "ui_imports.h"

namespace ecs {

struct TranscriptSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto& r = layout->transcript;

        auto panel = div(ctx, mk(uiRoot, 2000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::PANEL_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_roundness(0.0f)
                .with_render_layer(1)
                .with_debug_name("transcript"));

        // Header: session title.
        std::string header = "Select a session";
        if (app->openSession) {
            const auto& t = app->openSession->summary.title;
            header = t.empty() ? "(untitled)" : t;
        } else if (app->transcriptState == LoadState::Loading) {
            header = "Loading\xe2\x80\xa6";
        } else if (app->transcriptState == LoadState::Error) {
            header = "Error";
        }
        div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(header, 60))
                .with_size(ComponentSize{percent(1.0f), pixels(44)})
                .with_padding(Padding{.top = pixels(14), .right = pixels(18),
                                      .bottom = pixels(8), .left = pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("transcript_header"));

        if (app->transcriptState == LoadState::Error) {
            note(ctx, panel.ent(), "Could not load transcript: " +
                                       app->transcriptError);
            return;
        }
        if (!app->openSession) {
            note(ctx, panel.ent(),
                 "Pick a session on the left to view its messages.");
            return;
        }

        float listH = r.height - 44.0f;
        if (listH < 20.0f) listH = 20.0f;
        auto scroll = div(ctx, mk(panel.ent(), 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(6), .left = pixels(18)})
                .with_debug_name("transcript_scroll"));

        int i = 0;
        for (const auto& m : app->openSession->messages) {
            render_bubble(ctx, scroll.ent(), i, m, r.width);
            ++i;
        }
    }

  private:
    static void note(UIContext<InputAction>& ctx, Entity& parent,
                     const std::string& text) {
        div(ctx, mk(parent, 80),
            preset::EmptyStateText(text)
                .with_size(ComponentSize{percent(1.0f), pixels(60)})
                .with_padding(Padding{.top = pixels(20), .right = pixels(18),
                                      .bottom = pixels(8), .left = pixels(18)})
                .with_alignment(TextAlignment::Left)
                .with_debug_name("transcript_note"));
    }

    static const char* role_label(api::Role r) {
        switch (r) {
            case api::Role::User: return "You";
            case api::Role::Assistant: return "Assistant";
            case api::Role::System: return "System";
            case api::Role::Tool: return "Tool";
        }
        return "";
    }

    static theme::Color role_color(api::Role r) {
        switch (r) {
            case api::Role::User: return theme::ROLE_USER;
            case api::Role::Assistant: return theme::ROLE_ASSISTANT;
            case api::Role::System: return theme::ROLE_SYSTEM;
            case api::Role::Tool: return theme::ROLE_TOOL;
        }
        return theme::TEXT_SECONDARY;
    }

    static theme::Color bubble_bg(api::Role r) {
        switch (r) {
            case api::Role::User: return theme::BUBBLE_USER_BG;
            case api::Role::Assistant: return theme::BUBBLE_ASSISTANT_BG;
            default: return theme::BUBBLE_OTHER_BG;
        }
    }

    // Estimate wrapped height from text length and available width. The imm UI
    // wraps text at (rect.width - 10px); we size the bubble to roughly fit so
    // the column scrolls. Uses a conservative average glyph advance.
    static float estimate_height(const std::string& text, float widthPx) {
        float charW = 8.0f;              // approx advance at Medium size
        float wrapW = widthPx - 10.0f;   // renderer's 5px margin each side
        int perLine = static_cast<int>(wrapW / charW);
        if (perLine < 8) perLine = 8;
        int lines = 0;
        // Count wrapped lines paragraph by paragraph (honor explicit newlines).
        size_t start = 0;
        while (start <= text.size()) {
            size_t nl = text.find('\n', start);
            size_t end = (nl == std::string::npos) ? text.size() : nl;
            int len = static_cast<int>(end - start);
            lines += (len <= 0) ? 1 : (len + perLine - 1) / perLine;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        if (lines < 1) lines = 1;
        float lineH = 18.0f;
        return 24.0f + static_cast<float>(lines) * lineH;
    }

    void render_bubble(UIContext<InputAction>& ctx, Entity& parent, int index,
                       const api::Message& m, float paneWidth) {
        float bubbleW = paneWidth - 60.0f;
        if (bubbleW < 120.0f) bubbleW = 120.0f;
        float textW = bubbleW - 28.0f;
        float h = estimate_height(m.text, textW);

        auto bubble = div(ctx, mk(parent, 200 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(bubbleW), pixels(h)})
                .with_custom_background(bubble_bg(m.role))
                .with_flex_direction(FlexDirection::Column)
                // Column defaults to FlexWrap::Wrap — without NoWrap the
                // second child (body text) wraps into a new column beside the
                // role label instead of stacking under it.
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(4), .right = pixels(0),
                                    .bottom = pixels(6), .left = pixels(0)})
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(8), .left = pixels(14)})
                .with_roundness(0.35f)
                .with_debug_name("bubble"));

        // Role label (+ optional subtitle for tool/system).
        std::string label = role_label(m.role);
        if (!m.subtitle.empty()) label += "  \xc2\xb7  " + m.subtitle;
        std::string age = fmtutil::relative_time(m.created_at);
        if (!age.empty()) label += "   " + age;
        div(ctx, mk(bubble.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{percent(1.0f), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(role_color(m.role))
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("bubble_role"));

        // Body text (word-wrapped). Wrapping needs TextOverflow::Wrap + an
        // explicit font size, which FontSize::Medium provides. Width is a
        // percent of the bubble's content box (mirrors the role label, which
        // renders correctly) — a fixed pixel width was being placed against
        // the right edge and clipped.
        div(ctx, mk(bubble.ent(), 2),
            ComponentConfig{}
                .with_label(m.text)
                .with_size(ComponentSize{percent(1.0f), pixels(h - 24.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(FontSize::Medium)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("bubble_text"));
    }
};

}  // namespace ecs
