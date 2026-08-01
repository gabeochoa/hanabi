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
                render_home(ctx, panel.ent(), *app, r.width, r.height);
                break;
            case SmartView::Blocked:
                render_digest(ctx, panel.ent(), *app, "Blocked on you",
                              r.width, r.height, ecs::model::in_blocked_view);
                break;
            case SmartView::Review:
                render_digest(ctx, panel.ent(), *app, "Ready for review",
                              r.width, r.height, ecs::model::in_review_view);
                break;
            case SmartView::Starred:
                render_digest(ctx, panel.ent(), *app, "Starred", r.width,
                              r.height, ecs::model::in_starred_view);
                break;
        }
    }

  private:
    static void header(UIContext<InputAction>& ctx, Entity& parent,
                       const std::string& title, const std::string& sub,
                       float titlePx = 14.0f) {
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
                .with_size(ComponentSize{percent(0.7f), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(titlePx)
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
                    .with_font_size(theme::type::SM)
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

    // Content wrapper capped at 720px wide (mirrors the mock's `.sv-wrap`
    // max-width). Left-aligned so it lines up with the header title's left
    // inset (the header is a separate fixed row, so left-align keeps the h1
    // and the card column on the same left edge rather than drifting apart).
    static Entity& centered_wrap(UIContext<InputAction>& ctx, Entity& scroll,
                                 int id, float innerW) {
        float wrapW = innerW < 720.0f ? innerW : 720.0f;
        auto row = div(ctx, mk(scroll, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_justify_content(JustifyContent::FlexStart)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sv_center"));
        auto wrap = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(wrapW), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sv_wrap"));
        return wrap.ent();
    }

    // ---------------- Digest views (Blocked / Review / Starred) ------------
    template <typename Pred>
    void render_digest(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, const std::string& title,
                       float paneW, float paneH, Pred pred) {
        std::vector<const api::SessionSummary*> rows;
        for (const auto& s : app.sessions)
            if (pred(s)) rows.push_back(&s);

        header(ctx, parent, title, std::to_string(rows.size()), 20.0f);

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
        Entity& wrap = centered_wrap(ctx, scroll.ent(), 9000, paneW - 48.0f);
        for (const auto* s : rows) digest_card(ctx, wrap, ++i, *s, app);
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
        // The tag_*_bg tokens are intentionally low-alpha "soft tints". The UI
        // rect fill can't alpha-blend (afterhours gap #13), so pre-composite the
        // tint OVER the card surface (panel_bg_2) into an opaque color — giving
        // the intended subtle pill instead of a saturated solid block.
        const theme::Color surface = theme::panel_bg_2();
        switch (t) {
            case api::ThreadTag::Blocked:
                return theme::over(theme::tag_blocked_bg(), surface);
            case api::ThreadTag::Review:
                return theme::over(theme::tag_ready_bg(), surface);
            case api::ThreadTag::Done:
                return theme::over(theme::tag_done_bg(), surface);
            default: return surface;
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
                .with_font_size(theme::type::TITLE)
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
                    .with_font_size(theme::type::CHIP)
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
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("dc_sub"));
    }

    // ---------------- Home digest ------------------------------------------
    void render_home(UIContext<InputAction>& ctx, Entity& parent,
                     AppComponent& app, float paneW, float paneH) {
        header(ctx, parent, "Home", "", 20.0f);

        float listH = paneH - 46.0f;
        if (listH < 40.0f) listH = 40.0f;
        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(24),
                                      .bottom = pixels(6), .left = pixels(24)})
                .with_debug_name("home_scroll"));

        Entity& wrap = centered_wrap(ctx, scroll.ent(), 9000, paneW - 48.0f);

        // Ordered: (a) waiting on you, (b) finished, (c) running (count only).
        int shown = 0, running = 0;
        section_label(ctx, wrap, 1, "WAITING ON YOU");
        for (const auto& s : app.sessions)
            if (s.state == api::ThreadState::Attention &&
                s.tag == api::ThreadTag::Blocked)
                digest_card(ctx, wrap, ++shown, s, app);

        section_label(ctx, wrap, 900, "FINISHED SINCE YOU LOOKED");
        for (const auto& s : app.sessions)
            if (s.state == api::ThreadState::Attention &&
                s.tag != api::ThreadTag::Blocked)
                digest_card(ctx, wrap, ++shown, s, app);

        for (const auto& s : app.sessions)
            if (s.state == api::ThreadState::Running) ++running;
        section_label(ctx, wrap, 1800,
                      "SELF-RUNNING (" + std::to_string(running) + ")");

        // Recent / all conversations. A calm backend (e.g. the generic http
        // adapter, which leaves every thread's high-signal state at its default
        // and files nothing into a folder) produces NO attention/finished/
        // running rows — so the three sections above are all empty and Home
        // would otherwise look blank even with a fully-loaded list. Show the
        // most recent conversations here so Home is always useful and the
        // loaded threads are reachable straight from the landing view. Capped
        // so a huge list doesn't build hundreds of cards on the home pane
        // (the sidebar's Recent folder holds the full set). Skip archived.
        std::vector<const api::SessionSummary*> recent;
        for (const auto& s : app.sessions)
            if (s.state != api::ThreadState::Archived) recent.push_back(&s);
        std::sort(recent.begin(), recent.end(),
                  [](const api::SessionSummary* a, const api::SessionSummary* b) {
                      return a->updated_at > b->updated_at;
                  });
        if (!recent.empty()) {
            section_label(ctx, wrap, 2600, "RECENT");
            constexpr size_t kMaxRecent = 20;
            for (size_t k = 0; k < recent.size() && k < kMaxRecent; ++k)
                digest_card(ctx, wrap, ++shown, *recent[k], app);
        }
    }

    static void section_label(UIContext<InputAction>& ctx, Entity& parent,
                              int id, const std::string& text) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_margin(Margin{.top = pixels(16), .right = pixels(0),
                                    .bottom = pixels(4), .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("home_section"));
    }

    // ---------------- Sub-agent panel (transcript-only) --------------------
    //
    // The mock (mock/assets/ui.js `subItemHtml` / `renderTranscript`) renders a
    // panel at the TOP of the transcript listing each sub-agent as a row with a
    // status shape (working ring / done dot / blocked triangle), a title, and a
    // status note. Per docs/decisions.md this visualization lives ONLY here in
    // the transcript, never in the sidebar.
    //
    // IMPORTANT — data source: the current api types (api::Session / api::Message
    // in src/api/types.h) carry NO sub-agent / sub-session / child field, and the
    // MockClient seeds none. The mock HTML's `subs:[{title,state,note}]` array has
    // no representation in the C++ model. So we drive this panel from the one real
    // per-step signal that DOES exist: Tool-role messages. Each tool message is a
    // discrete agent work-step (a shell/sql/etc run), which is exactly the "a
    // sub-task ran" notion the panel visualizes. We render them as "done" steps
    // (they carry a completed result), keyed for collapse by message id. See the
    // task report for the precise api-type addition needed to fully populate the
    // mock's richer running/blocked sub-agent rows.
    enum class SubGlyph { Working, Done, Blocked };

    static theme::Color sub_glyph_color(SubGlyph g) {
        switch (g) {
            case SubGlyph::Working: return theme::accent();
            case SubGlyph::Done: return theme::tag_done_fg();
            case SubGlyph::Blocked: return theme::tag_blocked_fg();
        }
        return theme::text_faint();
    }

    // Draw the sub-agent status shape centered in `rect`. Replicates the sidebar's
    // shape vocabulary locally (no dependency on sidebar_system.h): a hollow ring
    // for a running sub-agent, a filled dot for done, an up-triangle for blocked.
    static void draw_sub_glyph(RectangleType rect, SubGlyph g) {
        const theme::Color c = sub_glyph_color(g);
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        switch (g) {
            case SubGlyph::Working:
                // Hollow ring (outline, not filled) — mirrors the mock's
                // .glyph.g-working animated pulse ring, minus the animation.
                afterhours::draw_ring(cx, cy, 3.0f, 4.6f, 24, c);
                break;
            case SubGlyph::Done:
                afterhours::draw_circle_v(afterhours::vec2{cx, cy}, 4.0f, c);
                break;
            case SubGlyph::Blocked:
                afterhours::draw_triangle(
                    afterhours::vec2{cx, cy - 4.5f},
                    afterhours::vec2{cx - 5.0f, cy + 4.5f},
                    afterhours::vec2{cx + 5.0f, cy + 4.5f}, c);
                break;
        }
    }

    // One sub-agent row: [shape] title · status, click toggles the detail note.
    void sub_item(UIContext<InputAction>& ctx, Entity& parent, int id,
                  AppComponent& app, const std::string& key, SubGlyph g,
                  const std::string& title, const std::string& note) {
        bool open = app.expandedSubAgents.count(key) != 0;
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(6), .left = pixels(14)})
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_bg())
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.0f)
                .with_debug_name("sub_item"));
        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (row.ent().get<afterhours::ui::HasClickListener>().down) {
            if (open) app.expandedSubAgents.erase(key);
            else app.expandedSubAgents.insert(key);
        }

        // Header line: shape glyph slot + title + "·" + status.
        auto head = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("si_head"));
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(12), pixels(16)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([g](RectangleType rect) {
                    draw_sub_glyph(rect, g);
                })
                .with_debug_name("si_glyph"));
        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(title, 34))
                .with_size(ComponentSize{children(), pixels(16)})
                .with_margin(Margin{.top = pixels(0), .right = pixels(8),
                                    .bottom = pixels(0), .left = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("si_title"));
        if (!note.empty() && !open) {
            div(ctx, mk(head.ent(), 3),
                ComponentConfig{}
                    .with_label("\xc2\xb7  " + fmtutil::ellipsize(note, 40))
                    .with_size(ComponentSize{percent(0.6f), pixels(16)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("si_status"));
        }

        // Expanded detail: the full note, wrapped, indented under the title.
        if (open && !note.empty()) {
            float noteW = 700.0f;
            float nh = estimate_height(note, noteW - 24.0f);
            div(ctx, mk(row.ent(), 2),
                ComponentConfig{}
                    .with_label(note)
                    .with_size(ComponentSize{percent(1.0f), pixels(nh - 20.0f)})
                    .with_margin(Margin{.top = pixels(2), .right = pixels(0),
                                        .bottom = pixels(2), .left = pixels(18)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(FontSize::Medium)
                    .with_text_overflow(TextOverflow::Wrap)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("si_detail"));
        }
    }

    // Panel at the top of the transcript listing the thread's sub-agent steps.
    // Returns true if a panel was rendered.
    static SubGlyph sub_glyph_for(api::SubAgentState st) {
        switch (st) {
            case api::SubAgentState::Running: return SubGlyph::Working;
            case api::SubAgentState::Done: return SubGlyph::Done;
            case api::SubAgentState::Blocked: return SubGlyph::Blocked;
        }
        return SubGlyph::Working;
    }

    static const char* sub_state_note(api::SubAgentState st) {
        switch (st) {
            case api::SubAgentState::Running: return "running";
            case api::SubAgentState::Done: return "done";
            case api::SubAgentState::Blocked: return "blocked";
        }
        return "";
    }

    bool sub_agent_panel(UIContext<InputAction>& ctx, Entity& scroll,
                         AppComponent& app) {
        // Prefer real sub-agents when the session carries them; otherwise fall
        // back to deriving steps from Tool-role messages (the one per-step
        // signal that always exists).
        const auto& subs = app.openSession->sub_agents;
        std::vector<const api::Message*> steps;
        if (subs.empty()) {
            for (const auto& m : app.openSession->messages)
                if (m.role == api::Role::Tool) steps.push_back(&m);
            if (steps.empty()) return false;
        }

        const size_t count = subs.empty() ? steps.size() : subs.size();

        auto panel = div(ctx, mk(scroll, 8000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(700), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(2), .right = pixels(0),
                                    .bottom = pixels(12), .left = pixels(0)})
                .with_custom_background(theme::panel_bg_2())
                .with_roundness(0.28f)
                .with_debug_name("subpanel"));

        // Head: "SUB-AGENTS (n)".
        div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_label("SUB-AGENTS (" + std::to_string(count) + ")")
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_padding(Padding{.top = pixels(8), .right = pixels(12),
                                      .bottom = pixels(6), .left = pixels(14)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("subpanel_head"));

        int i = 0;
        if (!subs.empty()) {
            for (const auto& sa : subs) {
                std::string note =
                    std::string(sub_state_note(sa.state)) +
                    (sa.note.empty() ? "" : " \xc2\xb7 " + sa.note);
                sub_item(ctx, panel.ent(), 10 + i, app, sa.id,
                         sub_glyph_for(sa.state), sa.title, note);
                ++i;
            }
        } else {
            for (const auto* m : steps) {
                std::string title = m->subtitle.empty() ? "step" : m->subtitle;
                sub_item(ctx, panel.ent(), 10 + i, app, m->id, SubGlyph::Done,
                         title, m->text);
                ++i;
            }
        }
        return true;
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
        // Empty-thread state: an open thread with no messages shows the empty
        // state rather than a blank pane (mirrors the mock's empty screen).
        if (app.openSession->messages.empty()) {
            note(ctx, parent, "Nothing here yet");
            return;
        }

        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(6), .left = pixels(18)})
                .with_debug_name("transcript_scroll"));

        // Sub-agent panel sits above the messages when the thread has steps.
        sub_agent_panel(ctx, scroll.ent(), app);

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
            // Tool activity reads as a distinct, subtler surface than the
            // conversational bubbles: a faint accent-over-panel tint (mirrors
            // the mock's monospace-ish "Tool · shell … · lint clean" block).
            // theme::over pre-blends the low-alpha tint over the pane surface
            // into an opaque color (afterhours gap #13 workaround).
            case api::Role::Tool:
                return theme::over(theme::accent_soft(), theme::panel_bg());
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
                // Tool bodies read subtler than conversational text (mock's
                // muted "Tool · …" block); other roles keep primary text.
                .with_custom_text_color(m.role == api::Role::Tool
                                            ? theme::text_secondary()
                                            : theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("bubble_text"));
    }
};

}  // namespace ecs
