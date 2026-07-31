#pragma once

// Renders the main pane (right of the sidebar, below the tab strip). Dispatches
// on AppComponent::view: the smart views (Home / Blocked / Review / Starred)
// are digest lists over the thread set; Chat renders the active tab's
// transcript as message bubbles.

#include <string>
#include <vector>

#include "../util/format.h"
#include "thread_model.h"
#include "ui_imports.h"

namespace ecs {

struct MainPaneSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto& r = layout->main;

        auto panel = div(ctx, mk(uiRoot, 2000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::panel_bg())
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_roundness(0.0f)
                .with_render_layer(1)
                .with_debug_name("main_pane"));

        switch (app->view) {
            case SmartView::Chat:
                render_transcript(ctx, panel.ent(), *app, r.width, r.height);
                break;
            case SmartView::Home:
                render_home(ctx, panel.ent(), *app, r.height);
                break;
            case SmartView::Blocked:
                render_digest(ctx, panel.ent(), *app, "Blocked on you",
                              r.height, ecs::model::in_blocked_view);
                break;
            case SmartView::Review:
                render_digest(ctx, panel.ent(), *app, "Ready for review",
                              r.height, ecs::model::in_review_view);
                break;
            case SmartView::Starred:
                render_digest(ctx, panel.ent(), *app, "Starred", r.height,
                              ecs::model::in_starred_view);
                break;
        }
    }

  private:
    static void header(UIContext<InputAction>& ctx, Entity& parent,
                       const std::string& title, const std::string& sub) {
        auto h = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(46)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(14), .right = pixels(20),
                                      .bottom = pixels(8), .left = pixels(20)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("main_header"));
        div(ctx, mk(h.ent(), 1),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(title, 48))
                .with_size(ComponentSize{percent(0.7f), pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("main_title"));
        if (!sub.empty()) {
            div(ctx, mk(h.ent(), 2),
                ComponentConfig{}
                    .with_label(sub)
                    .with_size(ComponentSize{percent(0.3f), pixels(22)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("main_sub"));
        }
    }

    static void note(UIContext<InputAction>& ctx, Entity& parent,
                     const std::string& text) {
        div(ctx, mk(parent, 80),
            preset::EmptyStateText(text)
                .with_size(ComponentSize{percent(1.0f), pixels(60)})
                .with_padding(Padding{.top = pixels(20), .right = pixels(18),
                                      .bottom = pixels(8), .left = pixels(18)})
                .with_alignment(TextAlignment::Left)
                .with_debug_name("main_note"));
    }

    // ---------------- Digest views (Blocked / Review / Starred) ------------
    template <typename Pred>
    void render_digest(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, const std::string& title,
                       float paneH, Pred pred) {
        std::vector<const api::SessionSummary*> rows;
        for (const auto& s : app.sessions)
            if (pred(s)) rows.push_back(&s);

        header(ctx, parent, title, std::to_string(rows.size()));

        if (rows.empty()) {
            note(ctx, parent, "Nothing here right now.");
            return;
        }

        float listH = paneH - 46.0f;
        if (listH < 40.0f) listH = 40.0f;
        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(24),
                                      .bottom = pixels(6), .left = pixels(24)})
                .with_debug_name("digest_scroll"));

        int i = 0;
        for (const auto* s : rows) digest_card(ctx, scroll.ent(), ++i, *s, app);
    }

    static const char* tag_label(api::ThreadTag t) {
        switch (t) {
            case api::ThreadTag::Blocked: return "BLOCKED";
            case api::ThreadTag::Review: return "REVIEW";
            case api::ThreadTag::Done: return "DONE";
            default: return "";
        }
    }
    static theme::Color tag_fg(api::ThreadTag t) {
        switch (t) {
            case api::ThreadTag::Blocked: return theme::tag_blocked_fg();
            case api::ThreadTag::Review: return theme::tag_ready_fg();
            case api::ThreadTag::Done: return theme::tag_done_fg();
            default: return theme::text_faint();
        }
    }
    static theme::Color tag_bg(api::ThreadTag t) {
        switch (t) {
            case api::ThreadTag::Blocked: return theme::tag_blocked_bg();
            case api::ThreadTag::Review: return theme::tag_ready_bg();
            case api::ThreadTag::Done: return theme::tag_done_bg();
            default: return theme::panel_bg();
        }
    }

    void digest_card(UIContext<InputAction>& ctx, Entity& parent, int id,
                     const api::SessionSummary& s, AppComponent& app) {
        auto card = div(ctx, mk(parent, 100 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(56)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(4), .right = pixels(0),
                                    .bottom = pixels(6), .left = pixels(0)})
                .with_padding(Padding{.top = pixels(8), .right = pixels(14),
                                      .bottom = pixels(8), .left = pixels(14)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_bg())
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("digest_card"));
        card.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (card.ent().get<afterhours::ui::HasClickListener>().down)
            app.requestOpenTab = s.id;

        // Title row (name + tag).
        auto top = div(ctx, mk(card.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("dc_top"));
        div(ctx, mk(top.ent(), 1),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(s.title, 40))
                .with_size(ComponentSize{percent(0.74f), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("dc_name"));
        if (s.tag != api::ThreadTag::None) {
            div(ctx, mk(top.ent(), 2),
                ComponentConfig{}
                    .with_label(tag_label(s.tag))
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_padding(Padding{.top = pixels(1), .right = pixels(6),
                                          .bottom = pixels(1),
                                          .left = pixels(6)})
                    .with_custom_background(tag_bg(s.tag))
                    .with_custom_text_color(tag_fg(s.tag))
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.3f)
                    .with_debug_name("dc_tag"));
        }

        // Subtitle / preview.
        div(ctx, mk(card.ent(), 2),
            ComponentConfig{}
                .with_label(s.preview.empty() ? s.status : s.preview)
                .with_size(ComponentSize{percent(1.0f), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("dc_sub"));
    }

    // ---------------- Home digest ------------------------------------------
    void render_home(UIContext<InputAction>& ctx, Entity& parent,
                     AppComponent& app, float paneH) {
        header(ctx, parent, "Home", "");

        float listH = paneH - 46.0f;
        if (listH < 40.0f) listH = 40.0f;
        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(24),
                                      .bottom = pixels(6), .left = pixels(24)})
                .with_debug_name("home_scroll"));

        // Ordered: (a) waiting on you, (b) finished, (c) running (count only).
        int shown = 0, running = 0;
        section_label(ctx, scroll.ent(), 1, "Waiting on you");
        for (const auto& s : app.sessions)
            if (s.state == api::ThreadState::Attention &&
                s.tag == api::ThreadTag::Blocked)
                digest_card(ctx, scroll.ent(), ++shown, s, app);

        section_label(ctx, scroll.ent(), 900, "Finished since you looked");
        for (const auto& s : app.sessions)
            if (s.state == api::ThreadState::Attention &&
                s.tag != api::ThreadTag::Blocked)
                digest_card(ctx, scroll.ent(), ++shown, s, app);

        for (const auto& s : app.sessions)
            if (s.state == api::ThreadState::Running) ++running;
        section_label(ctx, scroll.ent(), 1800,
                      "Self-running (" + std::to_string(running) + ")");
    }

    static void section_label(UIContext<InputAction>& ctx, Entity& parent,
                              int id, const std::string& text) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_margin(Margin{.top = pixels(10), .right = pixels(0),
                                    .bottom = pixels(2), .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("home_section"));
    }

    // ---------------- Chat transcript --------------------------------------
    void render_transcript(UIContext<InputAction>& ctx, Entity& parent,
                           AppComponent& app, float paneW, float paneH) {
        std::string title = "Select a thread";
        if (app.openSession) {
            const auto& t = app.openSession->summary.title;
            title = t.empty() ? "(untitled)" : t;
        } else if (app.transcriptState == LoadState::Loading) {
            title = "Loading\xe2\x80\xa6";
        } else if (app.transcriptState == LoadState::Error) {
            title = "Error";
        }
        header(ctx, parent, title, "");

        if (app.transcriptState == LoadState::Error) {
            note(ctx, parent,
                 "Could not load transcript: " + app.transcriptError);
            return;
        }
        if (!app.openSession) {
            note(ctx, parent, "Open a thread to view its messages.");
            return;
        }

        float listH = paneH - 46.0f;
        if (listH < 20.0f) listH = 20.0f;
        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(6), .left = pixels(18)})
                .with_debug_name("transcript_scroll"));

        int i = 0;
        for (const auto& m : app.openSession->messages)
            render_bubble(ctx, scroll.ent(), i++, m, paneW);
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
            case api::Role::User: return theme::role_user();
            case api::Role::Assistant: return theme::role_assistant();
            case api::Role::System: return theme::role_system();
            case api::Role::Tool: return theme::role_tool();
        }
        return theme::text_secondary();
    }
    static theme::Color bubble_bg(api::Role r) {
        switch (r) {
            case api::Role::User: return theme::bubble_user_bg();
            case api::Role::Assistant: return theme::bubble_assistant_bg();
            default: return theme::bubble_other_bg();
        }
    }

    static float estimate_height(const std::string& text, float widthPx) {
        float charW = 8.0f;
        float wrapW = widthPx - 10.0f;
        int perLine = static_cast<int>(wrapW / charW);
        if (perLine < 8) perLine = 8;
        int lines = 0;
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
        return 24.0f + static_cast<float>(lines) * 18.0f;
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
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(4), .right = pixels(0),
                                    .bottom = pixels(6), .left = pixels(0)})
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(8), .left = pixels(14)})
                .with_roundness(0.35f)
                .with_debug_name("bubble"));

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

        div(ctx, mk(bubble.ent(), 2),
            ComponentConfig{}
                .with_label(m.text)
                .with_size(ComponentSize{percent(1.0f), pixels(h - 24.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("bubble_text"));
    }
};

}  // namespace ecs
