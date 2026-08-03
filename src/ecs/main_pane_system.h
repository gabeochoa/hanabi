#pragma once

// Renders the main pane (right of the sidebar, below the tab strip). Dispatches
// on AppComponent::view: the smart views (Home / Blocked / Review / Starred)
// are digest lists over the thread set; Chat renders the active tab's
// transcript as message bubbles.

#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "../util/format.h"
#include "thread_model.h"
#include "transcript_render_cache.h"
#include "../ui/inline_image.h"
#include "ui_imports.h"

#include "../../vendor/afterhours/src/plugins/clipboard.h"

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

        // The composer ALWAYS renders (Gabe: "it should just always render …
        // why would we hide it at any point"). It renders as its OWN absolute
        // strip at layout->composer — the SAME dedicated-rect pattern the
        // status bar uses to sit reliably at the bottom every frame — NOT as a
        // flex sibling of the content (which a tall transcript could overflow
        // and push off-screen). LayoutSystem carves layout->composer out of the
        // bottom of layout->main, so content + composer never overlap. It
        // replies to the open thread when one is open, else kicks off a NEW
        // conversation. Only truly absent when the backend can't send.
        const bool canReply =
            app->client &&
            (app->client->supports_send() || app->client->supports_stream());
        // Keep the layout's reserved composer height in sync with whether we
        // can send — 0 gives the whole pane back to content on a read-only
        // backend (next frame's layout picks it up).
        layout->composerHeight = canReply ? 92.0f : 0.0f;
        // Reply mode iff a real thread is open in Chat; otherwise kickoff (start
        // a new session). Split view still replies to its primary open thread.
        const bool composerKickoff =
            !(app->view == SmartView::Chat && app->openSession);

        // Content fills the pane (layout->main already excludes the composer).
        auto content = div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("main_content"));

        const float contentH = r.height;  // views size against the pane height

        switch (app->view) {
            case SmartView::Chat:
                if (!app->splitSessionId.empty()) {
                    render_split(ctx, content.ent(), *app, r.width, contentH);
                } else {
                    render_transcript(ctx, content.ent(), *app, r.width,
                                      contentH);
                }
                break;
            case SmartView::Home:
                render_home(ctx, content.ent(), *app, r.width, contentH);
                break;
            case SmartView::Blocked:
                render_digest(ctx, content.ent(), *app, "Blocked on you",
                              r.width, contentH, ecs::model::in_blocked_view,
                              "Nothing is waiting on you. \xf0\x9f\x8e\x89",
                              /*singleState=*/true);
                break;
            case SmartView::Review:
                render_digest(ctx, content.ent(), *app, "Ready for review",
                              r.width, contentH, ecs::model::in_review_view,
                              "No threads are ready for review yet.",
                              /*singleState=*/true);
                break;
            case SmartView::Starred:
                render_digest(ctx, content.ent(), *app, "Starred", r.width,
                              contentH, ecs::model::in_starred_view,
                              "No starred conversations. Star a thread to pin "
                              "it here.");
                break;
            case SmartView::Archived:
                render_digest(ctx, content.ent(), *app, "Archived", r.width,
                              contentH, ecs::model::in_archived_view,
                              "No archived conversations. Sending a message to "
                              "an archived thread unarchives it.");
                break;
        }

        // The ONE composer, rendered as its own absolute strip at
        // layout->composer (the SAME pattern the status bar uses to pin
        // reliably to the bottom). It is a direct uiRoot child; afterhours
        // absolute+translate is SCREEN-space (a uiRoot child's final pos ==
        // its translate), so pass the composer rect's screen top-left. No view
        // can hide it and no transcript overflow can push it off-screen.
        if (canReply) {
            const auto& cr = layout->composer;
            render_composer(ctx, uiRoot, *app, cr.width, cr.height,
                            composerKickoff, cr.x, cr.y);
        }
    }

  private:
    // The one AppComponent (transcript render needs it for expand/fold state).
    static AppComponent* app_singleton() {
        // The AppComponent is a process-lifetime singleton entity, so its
        // address is stable — cache it after the first resolve instead of
        // running an EntityQuery on every call. This function is hit MANY times
        // per frame (measured(), is_folded(), tool rows, per message), and the
        // query showed up as avoidable per-frame cost (REFACTOR_REVIEW #3).
        static AppComponent* cached = nullptr;
        if (cached != nullptr) return cached;
        auto q = afterhours::EntityQuery({.force_merge = true})
                     .whereHasComponent<AppComponent>()
                     .gen();
        cached = q.empty() ? nullptr : &q[0].get().get<AppComponent>();
        return cached;
    }

    static void header(UIContext<InputAction>& ctx, Entity& parent,
                       const std::string& title, const std::string& sub,
                       float titlePx = theme::type::LG) {
        auto h = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(46)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                // Left inset == kContentInset so the view title lines up exactly
                // with the section labels + cards in its scroll body (was 20 vs
                // the body's 24 — a 4px title/content misalignment on every
                // digest view). Right uses the same inset for symmetry.
                .with_padding(Padding{.top = pixels(14),
                                      .right = pixels(kContentInset),
                                      .bottom = pixels(8),
                                      .left = pixels(kContentInset)})
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

    // Transcript header — mirrors the mock's .hdr: the thread title (larger,
    // primary) stacked above a muted metadata subtitle ("N messages · age").
    // Uses ONE content inset (kContentInset) shared with the body + composer so
    // the title's left edge lines up with the messages beneath it. A stacked
    // column (title over sub) rather than the smart-view's side-by-side header,
    // so the subtitle reads as metadata about THIS thread, not a right-aligned
    // count.
    static void transcript_header(UIContext<InputAction>& ctx, Entity& parent,
                                  const std::string& title,
                                  const std::string& sub) {
        auto col = div(ctx, mk(parent, 1),
            ComponentConfig{}
                // Height fits its children exactly: top 12 + title 22 + gap 2 +
                // sub 18 + bottom 8 = 62 (was 52 with 22px pad -> a 30px content
                // box that the 42px of title+gap+sub overflowed every frame ->
                // layout-warn spam + solve_violations churn).
                .with_size(ComponentSize{percent(1.0f), pixels(62)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_justify_content(JustifyContent::Center)
                .with_padding(Padding{.top = pixels(12),
                                      .right = pixels(kContentInset),
                                      .bottom = pixels(8),
                                      .left = pixels(kContentInset)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("transcript_header"));
        div(ctx, mk(col.ent(), 1),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(title, 64))
                .with_size(ComponentSize{percent(1.0f), pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SPOTLIGHT)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("transcript_title"));
        if (!sub.empty()) {
            div(ctx, mk(col.ent(), 2),
                ComponentConfig{}
                    .with_label(sub)
                    .with_size(ComponentSize{percent(1.0f), pixels(16)})
                    .with_margin(Margin{.top = pixels(2)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("transcript_sub"));
        }
    }

    // Top-of-transcript "loading older messages" pill: a small accent ring +
    // caption in a rounded chip, horizontally centered near the top of the
    // pane. Overlay (absolute on the parent) so it floats over the content
    // without shifting it. topY = y of the pill's top edge within the pane.
    static void loading_older_pill(UIContext<InputAction>& ctx, Entity& parent,
                                   float paneW, float topY) {
        const float w = 190.0f, h = 30.0f;
        const float x = (paneW - w) * 0.5f;
        auto pill = div(ctx, mk(parent, 7400),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_absolute_position()
                .with_translate(x, topY)
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
                .with_padding(Padding{.right = pixels(12), .left = pixels(12)})
                .with_custom_background(theme::over(theme::panel_bg_2(),
                                                    theme::panel_bg()))
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.5f)
                .with_render_layer(9)
                .with_debug_name("loading_older_pill"));
        div(ctx, mk(pill.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(14), pixels(14)})
                .with_transparent_bg()
                .with_margin(Margin{.right = pixels(8)})
                .with_on_draw_fg([](RectangleType r) {
                    const float cx = r.x + r.width * 0.5f;
                    const float cy = r.y + r.height * 0.5f;
                    afterhours::draw_ring(cx, cy, 4.5f, 6.5f, 24,
                                          theme::accent());
                })
                .with_render_layer(9)
                .with_debug_name("loading_older_ring"));
        div(ctx, mk(pill.ent(), 2),
            ComponentConfig{}
                .with_label("Loading older messages\xe2\x80\xa6")
                .with_size(ComponentSize{children(), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(9)
                .with_debug_name("loading_older_txt"));
    }

    // Top-of-transcript "loading older" pill placement mirror lives above; the
    // jump-to-bottom button:
    // Floating "jump to bottom" button: a small circular down-chevron pinned
    // to the bottom-right of the transcript pane (just above the composer),
    // shown only when the user has scrolled up. Clicking snaps the transcript
    // scroll to the newest message (sets the scroll entity's offset to its
    // content end). paneBottomY = the y of the transcript scroll's bottom edge.
    static void jump_to_bottom_button(UIContext<InputAction>& ctx,
                                      Entity& parent, Entity& scrollEnt,
                                      float paneW, float paneBottomY) {
        const float d = 30.0f;
        const float bx = paneW - d - 20.0f;   // right inset
        const float by = paneBottomY - d - 14.0f;  // sit above the composer
        auto btn = button(ctx, mk(parent, 7300),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(d), pixels(d)})
                .with_absolute_position()
                .with_translate(bx, by)
                .with_custom_background(theme::over(theme::panel_bg_2(),
                                                    theme::panel_bg()))
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_border(theme::border(), pixels(1.0f))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.5f)
                .with_render_layer(8)
                .with_on_draw_fg([](RectangleType r) {
                    // Down chevron.
                    const float cx = r.x + r.width * 0.5f;
                    const float cy = r.y + r.height * 0.5f;
                    afterhours::draw_line_ex(
                        afterhours::vec2{cx - 5.0f, cy - 2.5f},
                        afterhours::vec2{cx, cy + 3.0f}, 1.8f,
                        theme::text_secondary());
                    afterhours::draw_line_ex(
                        afterhours::vec2{cx, cy + 3.0f},
                        afterhours::vec2{cx + 5.0f, cy - 2.5f}, 1.8f,
                        theme::text_secondary());
                })
                .with_debug_name("jump_to_bottom"));
        if (btn && scrollEnt.has<afterhours::ui::HasScrollView>()) {
            auto& sv = scrollEnt.get<afterhours::ui::HasScrollView>();
            sv.scroll_offset.y = 1e9f;
            hanabi::set_scroll_target_y(sv, 1e9f);  // sync (smooth-scroll patch)
            sv.clamp_scroll();
        }
    }

    // Centered "loading this thread" spinner: a small accent ring + caption.
    // Shown when the transcript is fetching a DIFFERENT thread than what's
    // currently displayed (a fresh switch), so the UI stays interactive with a
    // clear indicator instead of a blank/empty pane or wrong content. The
    // async loader (data layer) sets transcriptState=Loading + transcriptLoadingId
    // immediately on switch, so this paints on the very next frame while the
    // heavy fetch/parse runs on a worker (never the UI thread).
    static void loading_spinner(UIContext<InputAction>& ctx, Entity& parent,
                                const std::string& caption) {
        auto wrap = div(ctx, mk(parent, 81),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(120)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
                .with_padding(Padding{.top = pixels(48)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("transcript_loading"));
        div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(24), pixels(24)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([](RectangleType r) {
                    const float cx = r.x + r.width * 0.5f;
                    const float cy = r.y + r.height * 0.5f;
                    afterhours::draw_ring(cx, cy, 7.0f, 9.5f, 28,
                                          theme::accent());
                })
                .with_debug_name("transcript_loading_ring"));
        div(ctx, mk(wrap.ent(), 2),
            ComponentConfig{}
                .with_label(caption)
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_margin(Margin{.top = pixels(10)})
                .with_roundness(0.0f)
                .with_debug_name("transcript_loading_caption"));
    }

    static void note(UIContext<InputAction>& ctx, Entity& parent,
                     const std::string& text) {
        div(ctx, mk(parent, 80),
            preset::EmptyStateText(text)
                .with_size(ComponentSize{percent(1.0f), pixels(60)})
                .with_padding(Padding{.top = pixels(20), .right = pixels(18),
                                      .bottom = pixels(8), .left = pixels(18)})
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_debug_name("main_note"));
    }

    // A representative glyph (drawn shape) for a smart view's empty state.
    enum class EmptyGlyph { Check, Inbox, Star, Archive, None };
    static EmptyGlyph view_glyph(SmartView v) {
        switch (v) {
            case SmartView::Blocked:
            case SmartView::Review:  return EmptyGlyph::Check;
            case SmartView::Starred: return EmptyGlyph::Star;
            case SmartView::Archived:return EmptyGlyph::Archive;
            default:                 return EmptyGlyph::Inbox;
        }
    }
    // A polished, VERTICALLY-CENTERED empty state (a soft glyph + a message),
    // instead of a top-left note — the Apple-native "intentional empty" look.
    // Centered in the pane's usable height (paneH minus the header).
    static void empty_state(UIContext<InputAction>& ctx, Entity& parent,
                            EmptyGlyph glyph, const std::string& msg,
                            float paneH) {
        float colH = paneH - 46.0f;
        if (colH < 80.0f) colH = 80.0f;
        auto col = div(ctx, mk(parent, 80),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(colH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("empty_state"));
        // Soft circular glyph badge.
        div(ctx, mk(col.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(46), pixels(46)})
                .with_custom_background(theme::panel_bg_2())
                .with_roundness(0.5f)
                .with_margin(Margin{.bottom = pixels(14)})
                .with_on_draw_fg([glyph](RectangleType r) {
                    const float cx = r.x + r.width * 0.5f;
                    const float cy = r.y + r.height * 0.5f;
                    const theme::Color c = theme::text_faint();
                    switch (glyph) {
                        case EmptyGlyph::Check:
                            afterhours::draw_line_ex({cx - 8, cy},
                                {cx - 2, cy + 6}, 2.2f, c);
                            afterhours::draw_line_ex({cx - 2, cy + 6},
                                {cx + 9, cy - 6}, 2.2f, c);
                            break;
                        case EmptyGlyph::Star:
                            hanabi::icons::draw_at("star", cx, cy, 18.0f,
                                                   theme::text_faint());
                            break;
                        case EmptyGlyph::Archive:
                            afterhours::draw_rectangle_outline(
                                {cx - 10, cy - 7, 20, 14}, c);
                            afterhours::draw_line_ex({cx - 10, cy - 2},
                                {cx + 10, cy - 2}, 1.6f, c);
                            break;
                        default:  // includes EmptyGlyph::Inbox / None
                            afterhours::draw_rectangle_outline(
                                {cx - 10, cy - 8, 20, 16}, c);
                            break;
                    }
                })
                .with_debug_name("empty_glyph"));
        div(ctx, mk(col.ent(), 2),
            ComponentConfig{}
                .with_label(msg)
                .with_size(ComponentSize{pixels(360), pixels(40)})
                .with_transparent_bg()
                .with_custom_text_color(theme::empty_state_text())
                .with_font_size(theme::type::BODY)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("empty_msg"));
    }

    // Content wrapper capped so the reading column stays comfortable but fills
    // more of the wide (~820px) main pane than the old 720 cap did (which left
    // a ~76px dead margin on the right). 900px keeps line lengths sane while
    // reading as an intentionally-composed column, not a half-used pane. When
    // the pane is narrower than the cap the wrap just tracks the pane width.
    // Left-aligned so it lines up with the header title's left inset (the
    // header is a separate fixed row, so left-align keeps the h1 and the card
    // column on the same left edge rather than drifting apart).
    // Shared reading-column width: the wrap caps at kCap but tracks a narrower
    // pane. Callers pass the FULL pane width; the wrap sits inside a scroll
    // padded 24px each side, so the usable inner width is paneW - 48.
    static constexpr float kWrapCap = 900.0f;
    // ONE content inset shared by the transcript header, message body, and
    // composer so their left edges line up (Apple-native: a single consistent
    // margin, not three slightly-different paddings). 24px matches the mock's
    // .hdr padding (16px 24px).
    static constexpr float kContentInset = 24.0f;
    static float wrap_width(float paneW) {
        float innerW = paneW - 48.0f;
        return innerW < kWrapCap ? innerW : kWrapCap;
    }

    static Entity& centered_wrap(UIContext<InputAction>& ctx, Entity& scroll,
                                 int id, float innerW,
                                 float cap = kWrapCap, bool center = false,
                                 float availW = 0.0f) {
        float wrapW = innerW < cap ? innerW : cap;
        (void)center;
        (void)availW;
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
    // `singleState`: this view contains exactly ONE state (Blocked / Review), so
    // the header already names it and a per-card chip + "waiting on you" sub-line
    // on every row is the same fact three times (same redundancy Wave 6 killed
    // for Home's grouped sections). When true, cards render in grouped mode:
    // no chip, just the discriminating age, collapsed to a dense single row.
    // Starred is genuinely MIXED (any state can be starred) so it stays false.
    template <typename Pred>
    void render_digest(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, const std::string& title,
                       float paneW, float paneH, Pred pred,
                       const std::string& emptyMsg = "Nothing here right now.",
                       bool singleState = false) {
        std::vector<const api::SessionSummary*> rows;
        for (const auto& s : app.sessions)
            if (pred(s)) rows.push_back(&s);

        header(ctx, parent, title, std::to_string(rows.size()), theme::type::H1);

        if (rows.empty()) {
            empty_state(ctx, parent, view_glyph(app.view), emptyMsg, paneH);
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
        hanabi::attach_scroll_indicator(scroll.ent());  // gap #26 temp bar
        hanabi::apply_scroll_prefs(scroll.ent());

        int i = 0;
        Entity& wrap = centered_wrap(ctx, scroll.ent(), 9000, paneW - 48.0f);
        const float cardW = wrap_width(paneW);
        for (const auto* s : rows)
            digest_card(ctx, wrap, ++i, *s, app, false, cardW, singleState);
    }

    // Collapse internal runs of whitespace to single spaces and trim ends, so
    // titles with stray double-spaces ("watchdog   mana…") read cleanly and
    // don't waste width before the ellipsis. Cheap, allocation-light.
    // Display-only strip of a single leading "[P] " / "[P]" parked marker. The
    // sidebar strips this for its row labels (the status glyph already conveys
    // parked/attention); the Home digest cards must match so "[P] Foo" shows as
    // "Foo". This is a LABEL-only transform — the underlying SessionSummary is
    // never mutated, so state derivation is unaffected. Conservative: one
    // leading marker only.
    static std::string strip_parked_marker(const std::string& in) {
        return fmtutil::display_title(in);  // shared canonical impl
    }

    static std::string normalize_title(const std::string& in) {
        const std::string src = strip_parked_marker(in);
        std::string out;
        out.reserve(src.size());
        bool prev_space = false;
        for (char c : src) {
            const bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            if (ws) {
                if (!out.empty() && !prev_space) out.push_back(' ');
                prev_space = true;
            } else {
                out.push_back(c);
                prev_space = false;
            }
        }
        if (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    // Display-only secret redaction for transcript text. Conversation content
    // can contain real credentials a user pasted or a tool echoed (e.g. an
    // "export NAVI_API_KEY=…" line, a JWT, a bearer token) — hanabi should not
    // render those in the clear. This masks the obvious high-signal shapes and
    // is applied ONLY at render time (the underlying api::Message is untouched,
    // so a copy/export of the real data is unaffected — this is about not
    // shoulder-surfing a secret on screen). Conservative: only long opaque
    // tokens are masked, so normal prose is never mangled.
    static std::string redact_secrets(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        const std::string kMask = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
                                  "[redacted]";  // ••• [redacted]
        auto is_tok = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                   c == '+' || c == '/' || c == '=';
        };
        size_t i = 0, n = in.size();
        while (i < n) {
            // Scan the next run of token-ish chars.
            if (is_tok(in[i])) {
                size_t j = i;
                while (j < n && is_tok(in[j])) ++j;
                const size_t len = j - i;
                std::string_view tok(in.data() + i, len);
                // Mask a JWT (starts "eyJ" and long) or any very long opaque
                // token (>= 24 chars with no space) — the shapes real API keys
                // / bearer tokens / session ids take. Under the threshold we
                // keep the text verbatim so ordinary words are never touched.
                const bool jwt = len >= 12 && tok.substr(0, 3) == "eyJ";
                if (jwt || len >= 32) {
                    out += kMask;
                } else {
                    out.append(in, i, len);
                }
                i = j;
            } else {
                out.push_back(in[i]);
                ++i;
            }
        }
        return out;
    }

    // Strip inline-markdown DELIMITERS from a display string so the body reads
    // cleanly instead of showing literal backticks/asterisks (messages critique
    // #3 — the fastest "not a real product" tell). We can't yet COLOR the runs
    // (afterhours' styled-label spans don't word-wrap — gap #22), so the honest
    // interim is to drop the markers: `code` -> code, **bold** -> bold,
    // __bold__ -> bold. Display-only (api::Message untouched). Conservative:
    // only removes matched paired delimiters, leaves lone `*`/`_`/`` ` `` alone
    // (e.g. "a * b" or a path with underscores is untouched).
    static std::string strip_inline_md(const std::string& in) {
        // Inline markers (**bold**/`code`/_italic_) are NO LONGER stripped here:
        // the rich (assistant) path renders them as colored spans (md_to_spans)
        // and the flat (user) path strips them via strip_inline_markers(). This
        // now only applies per-line normalization (bullets / rules).
        return normalize_md_lines(in);
    }
    // Remove matched inline markers (**bold**/__bold__/`code`) WITHOUT styling —
    // used for the flat (user-bubble) path, which renders a single plain label
    // and has no per-line span rendering. The rich (assistant) path keeps the
    // markers and renders them as colored spans via md_to_spans instead.
    static std::string strip_inline_markers(const std::string& in) {
        auto strip_paired = [](std::string s, const std::string& d) {
            std::string r;
            r.reserve(s.size());
            size_t i = 0;
            while (i < s.size()) {
                if (s.compare(i, d.size(), d) == 0) {
                    size_t close = s.find(d, i + d.size());
                    if (close != std::string::npos && close > i + d.size()) {
                        std::string inner = s.substr(i + d.size(),
                                                     close - (i + d.size()));
                        if (inner.find('\n') == std::string::npos) {
                            r += inner;
                            i = close + d.size();
                            continue;
                        }
                    }
                }
                r += s[i++];
            }
            return r;
        };
        std::string out = strip_paired(in, "**");
        out = strip_paired(out, "__");
        out = strip_paired(out, "`");
        return out;
    }

    // ---- Inline markdown -> colored spans (gap #22 now unblocked) ----------
    // afterhours styled labels now WORD-WRAP (upstream fbb6aef/1e95cd1) and
    // vary COLOR per run (TextSpan is text+color; no per-run weight). So instead
    // of stripping `code`/**bold**/_italic_ markers, we render each as a
    // distinct COLOR: inline code in an accent tint, bold/italic in bright
    // primary. Returns the VISIBLE text (markers removed) + the color runs; the
    // visible text is what BOTH measure and render use for wrap/height, so the
    // virtualization mirror stays exact (markers never affect layout).
    struct InlineParse {
        std::string visible;                       // marker-free text
        std::vector<afterhours::ui::TextSpan> spans;  // colored runs
    };
    static InlineParse md_to_spans(const std::string& line) {
        InlineParse p;
        const theme::Color base = theme::text_primary();
        const theme::Color codeC = theme::accent();
        const theme::Color strongC = theme::text_primary();
        auto push = [&](const std::string& t, theme::Color c) {
            if (t.empty()) return;
            p.visible += t;
            // Merge with the previous run if same color (fewer runs).
            if (!p.spans.empty() && p.spans.back().color.r == c.r &&
                p.spans.back().color.g == c.g && p.spans.back().color.b == c.b &&
                p.spans.back().color.a == c.a)
                p.spans.back().text += t;
            else
                p.spans.push_back(afterhours::ui::TextSpan{t, c});
        };
        size_t i = 0;
        const size_t n = line.size();
        while (i < n) {
            // inline code `...`
            if (line[i] == '`') {
                size_t close = line.find('`', i + 1);
                if (close != std::string::npos && close > i + 1) {
                    push(line.substr(i + 1, close - i - 1), codeC);
                    i = close + 1;
                    continue;
                }
            }
            // **bold** / __bold__
            for (const char* d : {"**", "__"}) {
                if (line.compare(i, 2, d) == 0) {
                    size_t close = line.find(d, i + 2);
                    if (close != std::string::npos && close > i + 2) {
                        push(line.substr(i + 2, close - i - 2), strongC);
                        i = close + 2;
                        goto next;
                    }
                }
            }
            // *italic* / _italic_ (single delimiter; require non-space inner)
            for (char d : {'*', '_'}) {
                if (line[i] == d && i + 1 < n && line[i + 1] != ' ') {
                    size_t close = line.find(d, i + 1);
                    if (close != std::string::npos && close > i + 1) {
                        push(line.substr(i + 1, close - i - 1), strongC);
                        i = close + 1;
                        goto next;
                    }
                }
            }
            push(std::string(1, line[i]), base);
            ++i;
        next:;
        }
        return p;
    }
    // Visible (marker-free) length of a line, for wrap/height math. MUST match
    // md_to_spans(line).visible.size() so measure and render agree.
    static std::string md_visible(const std::string& line) {
        return md_to_spans(line).visible;
    }
    // Line-by-line: bullet markers -> "•", ordered "1." kept, hr -> dashes.
    static std::string normalize_md_lines(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        size_t i = 0;
        while (i <= in.size()) {
            size_t nl = in.find('\n', i);
            size_t end = (nl == std::string::npos) ? in.size() : nl;
            std::string line = in.substr(i, end - i);
            // leading spaces (preserve indent for nested lists)
            size_t s = 0;
            while (s < line.size() && line[s] == ' ') ++s;
            const std::string indent = line.substr(0, s);
            const std::string rest = line.substr(s);
            // bullet: "- x" / "* x" / "+ x"  (marker + at least one space)
            if (rest.size() >= 2 && (rest[0] == '-' || rest[0] == '*' ||
                                     rest[0] == '+') &&
                rest[1] == ' ') {
                line = indent + "\xe2\x80\xa2  " + rest.substr(2);
            } else if (rest == "---" || rest == "***" || rest == "___" ||
                       rest == "----" || rest == "-----") {
                // horizontal rule -> a light dashed line
                line = "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                       "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80";
            }
            out += line;
            if (nl == std::string::npos) break;
            out += '\n';
            i = nl + 1;
        }
        return out;
    }
    // Approx char budget that fits in `widthPx` at the given font size, so a
    // title uses the card's REAL available width before ellipsizing rather
    // than a fixed 40-char cap that leaves wide cards ~60% empty (defect #4).
    // ~0.52*fontSize per glyph is a good average for this proportional font;
    // clamped to a sane floor so a narrow pane still shows a few chars.
    static size_t char_budget(float widthPx, float fontPx) {
        float per = fontPx * 0.52f;
        if (per < 5.0f) per = 5.0f;
        long n = static_cast<long>(widthPx / per);
        if (n < 6) n = 6;
        return static_cast<size_t>(n);
    }

    // Build the metadata line under a card title. On the mock (rich preview)
    // that's the preview text — kept verbatim so the mock's state-matched
    // detail ("waiting on you · 22m", "done · 12m", "self-running · 61%")
    // stays rich (never regress it).
    //
    // On a real backend preview is empty. The CRITICAL rule (v3 review):
    // the sub-line must NEVER contradict or redundantly restate the derived
    // chip sitting above it. The old code leaked the RAW api status word
    // ("active") beneath a derived BLOCKED/DONE chip — so every WAITING card
    // read "3h · active" under a red BLOCKED pill and every FINISHED card read
    // "1d · active" under a DONE pill: two lines fighting each other, reads as
    // broken software. Instead, when a card carries a derived chip/state we
    // compose a state-MATCHED second token from the SAME derived verdict, and
    // deliberately drop the raw status word. Only a genuinely calm card (no
    // chip, Unknown state) may fall back to a neutral age line.
    static std::string card_meta(const api::SessionSummary& s) {
        if (!s.preview.empty()) return s.preview;  // mock: keep rich preview.
        const std::string age = fmtutil::relative_time(s.updated_at);
        std::string phrase;  // the state-matched verdict word.
        switch (s.tag) {
            case api::ThreadTag::Blocked:
                phrase = "waiting on you";  // matches the red BLOCKED chip.
                break;
            case api::ThreadTag::Done:
                phrase = "done";  // matches the DONE chip.
                break;
            case api::ThreadTag::Review:
                phrase = "ready for review";  // matches the REVIEW chip.
                break;
            default:
                // No tag chip. Derive from state so a RUNNING card (green
                // RUNNING chip, added below) reads "running · <age>", and a
                // calm/archived card gets a NEUTRAL age-first line — never the
                // raw "active" status word.
                switch (s.state) {
                    case api::ThreadState::Running: phrase = "running"; break;
                    case api::ThreadState::Archived: phrase = "archived"; break;
                    default: phrase.clear(); break;  // calm: age only.
                }
                break;
        }
        if (phrase.empty()) {
            // Genuinely calm card with no chip: a neutral relative age reads as
            // "last active <age>" without restating a raw session-status word.
            if (age.empty()) return "";
            return "last active " + age;
        }
        // Chip-bearing / stateful card: lead with the state-matched verdict,
        // then the age — e.g. "waiting on you  ·  3h", "done  ·  1d",
        // "running  ·  8h". Never the raw "active".
        if (age.empty()) return phrase;
        return phrase + "  \xc2\xb7  " + age;
    }

    // Sub-line for a card rendered INSIDE a homogeneous grouped section (Home's
    // Waiting / Finished / Self-running). The section HEADER already names the
    // state (and carries its color), so restating "waiting on you" on every card
    // — plus a red BLOCKED chip — was the same fact three times (v5 defect #4:
    // "7 identical red chips = noise"). In grouped mode we therefore drop the
    // chip and show only the DISCRIMINATING detail that actually differs between
    // sibling cards: the age, or a running card's progress ("61%", "tests",
    // "landing"). We derive it by stripping a leading state phrase from the mock
    // preview ("waiting on you \xc2\xb7 22m" -> "22m", "self-running \xc2\xb7 61%" -> "61%");
    // a real backend (no preview) falls back to the relative age.
    static std::string grouped_meta(const api::SessionSummary& s) {
        const std::string age = fmtutil::relative_time(s.updated_at);
        if (!s.preview.empty()) {
            // Take the detail AFTER the first " \xc2\xb7 " separator, i.e. drop the
            // redundant leading state phrase the section header already conveys.
            const std::string sep = "\xc2\xb7";
            size_t p = s.preview.find(sep);
            if (p != std::string::npos) {
                std::string tail = s.preview.substr(p + sep.size());
                // trim surrounding spaces
                size_t a = tail.find_first_not_of(' ');
                size_t b = tail.find_last_not_of(' ');
                if (a != std::string::npos)
                    return tail.substr(a, b - a + 1);
            }
            // No separator: the preview is a BARE phrase with no discriminating
            // detail. If it merely restates the section's state word (e.g.
            // "self-running" under the SELF-RUNNING header, "running", "waiting
            // on you"), echoing it is the exact redundancy grouped mode exists to
            // kill (v5 #4) — so fall back to the age instead. Only a preview that
            // carries REAL detail (not a state label) is kept verbatim.
            if (!is_bare_state_word(s.preview)) return s.preview;
        }
        return age;  // no discriminating detail: the age is what differs.
    }

    // True when a preview string is just a state/status label (which the grouped
    // section header already conveys) rather than a discriminating detail.
    static bool is_bare_state_word(const std::string& p) {
        static const char* kStateWords[] = {
            "self-running", "running", "waiting on you", "waiting",
            "blocked", "done", "ready for review", "review", "active",
            "archived", "parked"};
        for (const char* w : kStateWords)
            if (p == w) return true;
        return false;
    }

    static const char* tag_label(api::ThreadTag t) {        switch (t) {
            case api::ThreadTag::Blocked: return "BLOCKED";
            case api::ThreadTag::Review: return "REVIEW";
            case api::ThreadTag::Done: return "DONE";
            default: return "";
        }
    }    static theme::Color tag_fg(api::ThreadTag t) {
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

    // Effective chip for a card. Digest cards get a chip whenever they carry a
    // derived verdict — that's tag != None (BLOCKED/REVIEW/DONE) OR, for a
    // self-running thread that carries no tag, a green RUNNING chip so running
    // cards reach chip-parity with blocked/done ones (v3 defect #7) instead of
    // looking unfinished in a chip-bearing layout. Returns "" label when the
    // card is genuinely calm (no chip).
    static bool has_chip(const api::SessionSummary& s) {
        return s.tag != api::ThreadTag::None ||
               s.state == api::ThreadState::Running;
    }
    static const char* chip_label(const api::SessionSummary& s) {
        if (s.tag != api::ThreadTag::None) return tag_label(s.tag);
        if (s.state == api::ThreadState::Running) return "RUNNING";
        return "";
    }
    // RUNNING reuses the green ready tokens (tag_ready_fg/bg) — an existing
    // green already in theme.h, so no new token and no theme.h edit.
    static theme::Color chip_fg(const api::SessionSummary& s) {
        if (s.tag != api::ThreadTag::None) return tag_fg(s.tag);
        if (s.state == api::ThreadState::Running) return theme::tag_ready_fg();
        return theme::text_faint();
    }
    static theme::Color chip_bg(const api::SessionSummary& s) {
        if (s.tag != api::ThreadTag::None) return tag_bg(s.tag);
        if (s.state == api::ThreadState::Running)
            return theme::over(theme::tag_ready_bg(), theme::panel_bg_2());
        return theme::panel_bg_2();
    }

    // Renders one digest card. `emphasizeMeta` gives the subtitle/metadata a
    // bit more weight/contrast — used for the actionable "waiting on you"
    // rows so the most-actionable signal reads stronger than passive ones.
    void digest_card(UIContext<InputAction>& ctx, Entity& parent, int id,
                     const api::SessionSummary& s, AppComponent& app,
                     bool emphasizeMeta = false, float cardWidthPx = 0.0f,
                     bool grouped = false) {
        // The card is a raised surface (panel_bg_2, one step ELEVATED above the
        // pane's panel_bg) plus a hairline border so it reads as floating above
        // the pane in BOTH modes — in light the border is what sells the lift
        // (panel_bg_2 is a hair darker than the white pane, so fill alone would
        // read recessed). Consistent 14/16 padding keeps text off the edges.
        // Compute the sub-line ONCE up front so the layout can adapt to it.
        // On a real backend a card has no rich preview, so the sub-line is just
        // a bare relative age ("1h") — putting that alone on its own row wastes
        // ~half the card and makes a preview-less list look empty (Gabe: "the
        // spacing is messed up"). So: a SPARSE sub-line (short, no " · " detail —
        // essentially just an age) rides INLINE on the title row, right-aligned,
        // and the card collapses to a single tight row. A RICH sub-line (mock
        // preview, or a state+detail line) keeps the roomier two-line card.
        const std::string subLine = grouped ? grouped_meta(s) : card_meta(s);
        const bool sparseSub =
            subLine.empty() ||
            (subLine.size() <= 6 && subLine.find("\xc2\xb7") == std::string::npos);
        const float cardH = sparseSub ? 34.0f : 52.0f;

        auto card = div(ctx, mk(parent, 100 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(cardH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(3), .right = pixels(0),
                                    .bottom = pixels(5), .left = pixels(0)})
                .with_padding(Padding{.top = pixels(7), .right = pixels(16),
                                      .bottom = pixels(7), .left = pixels(16)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(theme::layout::ROUNDNESS_BOX)
                .with_debug_name("digest_card"));
        card.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (card.ent().get<afterhours::ui::HasClickListener>().down)
            app.requestOpenTab = s.id;

        // Title row: name (grows) + tag chip pinned right, both vertically
        // centered. space-between pushes the chip to the trailing edge so it
        // sits consistently top-right rather than floating mid-card.
        auto top = div(ctx, mk(card.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("dc_top"));
        // In a grouped section the header already names (and colors) the state,
        // so we suppress the per-card chip to kill the "N identical chips" noise
        // (v5 #4). The title then gets the full card width.
        const bool hasTag = has_chip(s) && !grouped;
        // A sparse card rides its age inline on the title row (right-aligned),
        // so the title leaves room for it; otherwise the title (or title+chip)
        // owns the row and the age/preview sits on the second line.
        const float titleFrac = hasTag ? 0.78f : (sparseSub && !subLine.empty()
                                                      ? 0.82f
                                                      : 1.0f);
        // Decouple truncation from a fixed char cap: budget from the card's
        // REAL available title width so a wide card fills its line before
        // ellipsizing (defect #4). Fall back to the old 40-char cap only when
        // the caller didn't pass a width (keeps other call sites unchanged).
        std::string title = normalize_title(s.title);
        if (cardWidthPx > 0.0f) {
            // Inner width = card width - 32px L/R padding, times the title's
            // flex fraction, minus a little slack for the ellipsis glyph.
            float titlePx = (cardWidthPx - 32.0f) * titleFrac - 6.0f;
            title = fmtutil::ellipsize(title, char_budget(titlePx,
                                                          theme::type::TITLE));
        } else {
            title = fmtutil::ellipsize(title, 40);
        }
        div(ctx, mk(top.ent(), 1),
            ComponentConfig{}
                .with_label(title)
                .with_size(ComponentSize{percent(titleFrac), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::TITLE)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("dc_name"));
        if (hasTag) {
            div(ctx, mk(top.ent(), 2),
                ComponentConfig{}
                    .with_label(chip_label(s))
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_padding(Padding{.top = pixels(2), .right = pixels(7),
                                          .bottom = pixels(2),
                                          .left = pixels(7)})
                    .with_custom_background(chip_bg(s))
                    .with_custom_text_color(chip_fg(s))
                    .with_font_size(theme::type::CHIP)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(theme::layout::ROUNDNESS_BADGE)
                    .with_debug_name("dc_tag"));
        } else if (sparseSub && !subLine.empty()) {
            // Sparse card: the age rides here, right-aligned on the title row,
            // instead of alone on a wasted second line. Muted so the title leads.
            div(ctx, mk(top.ent(), 2),
                ComponentConfig{}
                    .with_label(subLine)
                    .with_size(ComponentSize{percent(0.16f), pixels(16)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("dc_age_inline"));
        }

        // Second line: only for a RICH sub-line (mock preview, or a state+detail
        // line). A sparse card already showed its age inline above, so it has no
        // second row — keeping preview-less real-backend cards tight (~34px)
        // instead of a title over a mostly-empty slab.
        if (!sparseSub) {
            div(ctx, mk(card.ent(), 2),
                ComponentConfig{}
                    .with_label(subLine)
                    .with_size(ComponentSize{percent(1.0f), pixels(16)})
                    .with_margin(Margin{.top = pixels(3), .right = pixels(0),
                                        .bottom = pixels(0), .left = pixels(0)})
                    .with_transparent_bg()
                    .with_custom_text_color(emphasizeMeta
                                                ? theme::text_primary()
                                                : theme::text_secondary())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("dc_sub"));
        }
    }

    // A calm skeleton placeholder card shown while the FIRST session list is
    // still loading and no cache exists yet. It mirrors a real card's footprint
    // (same ~52px height + margins) with two dim bars (a wide "title" + a short
    // "meta"), so a cold-launch pane reads as "loading content" instead of
    // flashing a false "all caught up". No animation (afterhours has no shimmer
    // primitive — gap AN-11); the muted static bars are enough to signal pending.
    void skeleton_card(UIContext<InputAction>& ctx, Entity& parent, int idx) {
        auto card = div(ctx, mk(parent, 700 + idx),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(52)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(3), .right = pixels(0),
                                    .bottom = pixels(5), .left = pixels(0)})
                .with_padding(Padding{.top = pixels(11), .right = pixels(16),
                                      .bottom = pixels(11), .left = pixels(16)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(theme::layout::ROUNDNESS_BOX)
                .with_debug_name("skeleton_card"));
        // Title bar (~55% width). Use a clearly-visible raised tone (border
        // level, composited over the card) — the old hover_bg-over-panel wash
        // was near-invisible on the card (~#37 on #2a2a31), so the skeleton read
        // as a broken page rather than "loading" (critique #10). border() is a
        // distinct step above panel_bg_2 in both themes.
        div(ctx, mk(card.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(0.55f), pixels(12)})
                .with_custom_background(theme::border())
                .with_margin(Margin{.bottom = pixels(8)})
                .with_roundness(0.4f)
                .with_debug_name("skeleton_title"));
        // Metadata bar (~30% width, one step dimmer than the title).
        div(ctx, mk(card.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(0.30f), pixels(10)})
                .with_custom_background(theme::over(theme::border(),
                                                    theme::panel_bg_2()))
                .with_roundness(0.4f)
                .with_debug_name("skeleton_meta"));
    }

    // ---------------- Home digest ------------------------------------------
    void render_home(UIContext<InputAction>& ctx, Entity& parent,
                     AppComponent& app, float paneW, float paneH) {
        header(ctx, parent, "Home", "", theme::type::H1);

        // The composer is rendered ONCE at the pane level (always visible), so
        // Home just fills its content height with the digest list.
        float listH = paneH - 46.0f;
        if (listH < 40.0f) listH = 40.0f;
        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(24),
                                      .bottom = pixels(6), .left = pixels(24)})
                .with_debug_name("home_scroll"));
        hanabi::attach_scroll_indicator(scroll.ent());  // gap #26 temp bar
        hanabi::apply_scroll_prefs(scroll.ent());

        Entity& wrap = centered_wrap(ctx, scroll.ent(), 9000, paneW - 48.0f);
        const float cardW = wrap_width(paneW);

        // COLD-CACHE LOADING STATE. On a true-cold launch (no on-disk cache yet)
        // over a slow network, the list fetch can take several seconds. Without
        // this the pane fell through to the "You're all caught up" empty state —
        // a FALSE "nothing to do" flashed for the whole fetch (Gabe: slow to
        // open / looks frozen). Instead, while the list is genuinely loading and
        // we have nothing to show yet, render calm skeleton placeholder rows so
        // the app reads as "loading" rather than empty. (A WARM launch paints the
        // stale disk cache instantly, so sessions is non-empty and this is
        // skipped — this only bites the first-ever launch.)
        if (app.sessions.empty() && app.listState == LoadState::Loading) {
            for (int k = 0; k < 6; ++k) skeleton_card(ctx, wrap, k);
            return;
        }

        // Partition the sessions into the attention buckets ONCE so we know
        // whether each section is non-empty BEFORE rendering its header. An
        // empty section renders nothing at all (no orphaned header, no void) —
        // on a calm/real backend the attention buckets are all empty, so Home
        // must lead straight with an "all caught up" line + RECENT rather than
        // three dead headers stacked above the list.
        std::vector<const api::SessionSummary*> waiting, finished, selfRunning;
        for (const auto& s : app.sessions) {
            if (s.state == api::ThreadState::Attention) {
                if (s.tag == api::ThreadTag::Blocked)
                    waiting.push_back(&s);
                else
                    finished.push_back(&s);
            }
            if (s.state == api::ThreadState::Running) selfRunning.push_back(&s);
        }
        const bool anyAttention =
            !waiting.empty() || !finished.empty() || !selfRunning.empty();

        int shown = 0;
        bool first = true;  // tracks the first rendered section (tighter top).
        if (!waiting.empty()) {
            section_label(ctx, wrap, 1,
                          "Waiting on you \xc2\xb7 " +
                              std::to_string(waiting.size()),
                          first, theme::status_blocked());
            first = false;
            // Actionable rows: emphasize the "waiting on you \xc2\xb7 8m" metadata.
            for (const auto* s : waiting)
                digest_card(ctx, wrap, ++shown, *s, app, true, cardW, true);
        }
        if (!finished.empty()) {
            section_label(ctx, wrap, 900,
                          "Finished since you looked \xc2\xb7 " +
                              std::to_string(finished.size()),
                          first, theme::tag_done_fg());
            first = false;
            for (const auto* s : finished)
                digest_card(ctx, wrap, ++shown, *s, app, false, cardW, true);
        }
        // Self-running work: a real section with real cards (title + relative
        // age), headed "SELF-RUNNING (N)" like the mock. Rendering the actual
        // running threads (not a lone caption) kills the old orphaned-caption
        // void (defect #14) — the count now sits ON a populated section.
        if (!selfRunning.empty()) {
            section_label(ctx, wrap, 1800,
                          "Self-running \xc2\xb7 " +
                              std::to_string(selfRunning.size()),
                          first, theme::status_review());
            first = false;
            for (const auto* s : selfRunning)
                digest_card(ctx, wrap, ++shown, *s, app, false, cardW, true);
        }

        // Recent / all conversations. A calm backend (e.g. the generic http
        // adapter, which leaves every thread's high-signal state at its default
        // and files nothing into a folder) produces NO attention/finished/
        // running rows — so the buckets above are all empty and skipped. In
        // that case lead with a tasteful "all caught up" line so Home reads as
        // intentionally calm, then the RECENT list keeps every loaded thread
        // reachable straight from the landing view. Capped so a huge list
        // doesn't build hundreds of cards on the home pane (the sidebar's
        // Recent folder holds the full set). Skip archived AND anything already
        // surfaced in a section above (Attention/Running), so a [P]/done/running
        // card isn't shown twice.
        std::vector<const api::SessionSummary*> recent;
        for (const auto& s : app.sessions) {
            if (s.state == api::ThreadState::Archived) continue;
            if (s.state == api::ThreadState::Attention ||
                s.state == api::ThreadState::Running)
                continue;
            recent.push_back(&s);
        }
        std::sort(recent.begin(), recent.end(),
                  [](const api::SessionSummary* a, const api::SessionSummary* b) {
                      return a->updated_at > b->updated_at;
                  });
        if (!anyAttention) {
            div(ctx, mk(wrap, 800),
                preset::EmptyStateText("You're all caught up.")
                    .with_size(ComponentSize{percent(1.0f), pixels(30)})
                    .with_margin(Margin{.top = pixels(2), .right = pixels(0),
                                        .bottom = pixels(6), .left = pixels(2)})
                    .with_font_size(theme::type::BODY)
                    .with_alignment(TextAlignment::Left)
                    .with_debug_name("home_caught_up"));
        }
        if (!recent.empty()) {
            section_label(ctx, wrap, 2600, "Recent", first);
            constexpr size_t kMaxRecent = 20;
            for (size_t k = 0; k < recent.size() && k < kMaxRecent; ++k)
                digest_card(ctx, wrap, ++shown, *recent[k], app, false, cardW);
        }
    }

    static std::string upper(std::string s) {
        return fmtutil::to_upper(std::move(s));
    }

    // Section header: a distinct LABEL — uppercase, letter-spaced, faint —
    // so it reads as a quiet grouping label vs the larger primary-color card
    // titles beneath it. `first` drops the leading margin so the top section
    // doesn't push a gap under the h1.
    static void section_label(UIContext<InputAction>& ctx, Entity& parent,
                              int id, const std::string& text,
                              bool first = false,
                              theme::Color color = theme::text_faint()) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(upper(text))
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_margin(Margin{.top = pixels(first ? 4 : 20),
                                    .right = pixels(0), .bottom = pixels(6),
                                    .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_text_color(color)
                .with_font_size(theme::type::LABEL)
                .with_letter_spacing(1.0f)
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
            // DONE reads GREEN (v3 #12): the "done" success token, not the blue
            // tag_done_fg (which reads as "just another accent" next to the
            // running ring). tag_ready_fg is the theme's green success color.
            case SubGlyph::Done: return theme::tag_ready_fg();
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
            case SubGlyph::Done: {
                // Filled GREEN dot with a small check inside — a positive,
                // finished affordance (v3 #12) that reads distinctly from the
                // hollow accent running-ring. The check is drawn in the pane
                // surface color so it "cuts" out of the dot.
                afterhours::draw_circle_v(afterhours::vec2{cx, cy}, 5.0f, c);
                const theme::Color k = theme::panel_bg_2();
                afterhours::draw_line_ex(
                    afterhours::vec2{cx - 2.4f, cy + 0.2f},
                    afterhours::vec2{cx - 0.6f, cy + 2.2f}, 1.4f, k);
                afterhours::draw_line_ex(
                    afterhours::vec2{cx - 0.6f, cy + 2.2f},
                    afterhours::vec2{cx + 2.6f, cy - 2.2f}, 1.4f, k);
                break;
            }
            case SubGlyph::Blocked:
                afterhours::draw_triangle(
                    afterhours::vec2{cx, cy - 4.5f},
                    afterhours::vec2{cx - 5.0f, cy + 4.5f},
                    afterhours::vec2{cx + 5.0f, cy + 4.5f}, c);
                break;
        }
    }


    // Map a sub-agent state to its status glyph (used by the rollup chips).
    static SubGlyph sub_glyph_for(api::SubAgentState st) {
        switch (st) {
            case api::SubAgentState::Running: return SubGlyph::Working;
            case api::SubAgentState::Done: return SubGlyph::Done;
            case api::SubAgentState::Blocked: return SubGlyph::Blocked;
        }
        return SubGlyph::Working;
    }

    // Quiet, collapsible sub-agent ROLLUP (target: "N sub-agents • done" one
    // line that expands to compact chips). De-emphasized (no shouty ALL-CAPS
    // panel). Returns the total height it occupied (for virtualization math).
    // Only shown when the session carries REAL sub-agents — tool activity now
    // has its own dense rows, so we no longer duplicate it here.
    float sub_agent_panel(UIContext<InputAction>& ctx, Entity& col,
                          AppComponent& app) {
        const auto& subs = app.openSession->sub_agents;
        if (subs.empty()) return 0.0f;

        const size_t count = subs.size();
        int done = 0, blocked = 0;
        for (const auto& sa : subs) {
            if (sa.state == api::SubAgentState::Done) ++done;
            if (sa.state == api::SubAgentState::Blocked) ++blocked;
        }
        std::string verdict = blocked ? "blocked"
                              : (done == static_cast<int>(count) ? "done"
                                                                 : "running");
        const std::string key = "__subagents__";
        const bool open = app.expandedPiles.count(key) != 0;

        constexpr float kRowH = 24.0f;
        constexpr float kMargin = 8.0f;
        constexpr float kChipH = 26.0f;
        float total = kMargin + kRowH + kMargin;

        auto wrap = div(ctx, mk(col, 8000),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(kMargin),
                                    .bottom = pixels(kMargin)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("subrollup"));

        auto head = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(kRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.right = pixels(4)})
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("subrollup_head"));
        head.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (head.ent().get<afterhours::ui::HasClickListener>().down) {
            if (open) app.expandedPiles.erase(key);
            else app.expandedPiles.insert(key);
        }
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(open ? "\xe2\x96\xbe" : "\xe2\x96\xb8")
                .with_size(ComponentSize{pixels(14), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("subrollup_chev"));
        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(std::to_string(count) +
                            (count == 1 ? " sub-agent  \xc2\xb7  "
                                        : " sub-agents  \xc2\xb7  ") +
                            verdict)
                .with_size(ComponentSize{children(), pixels(18)})
                .with_margin(Margin{.left = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_debug_name("subrollup_label"));

        if (open) {
            auto chips = div(ctx, mk(wrap.ent(), 2),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::Wrap)
                    .with_margin(Margin{.top = pixels(6), .left = pixels(16)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("subrollup_chips"));
            int i = 0;
            for (const auto& sa : subs) {
                sub_chip(ctx, chips.ent(), 10 + i, sub_glyph_for(sa.state),
                         sa.title);
                ++i;
            }
            int rows = (static_cast<int>(count) + 2) / 3;
            total += 6.0f + rows * (kChipH + 6.0f);
        }
        return total;
    }

    // One compact sub-agent chip: glyph + short title.
    void sub_chip(UIContext<InputAction>& ctx, Entity& parent, int id,
                  SubGlyph g, const std::string& title) {
        auto chip = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(26)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(3), .right = pixels(10),
                                      .bottom = pixels(3), .left = pixels(8)})
                .with_margin(Margin{.right = pixels(6), .bottom = pixels(6)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.5f)
                .with_debug_name("sub_chip"));
        div(ctx, mk(chip.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(12), pixels(16)})
                .with_transparent_bg()
                .with_margin(Margin{.right = pixels(5)})
                .with_on_draw_fg([g](RectangleType rr) {
                    draw_sub_glyph(rr, g);
                })
                .with_debug_name("sub_chip_glyph"));
        div(ctx, mk(chip.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(title, 28))
                .with_size(ComponentSize{children(), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_debug_name("sub_chip_title"));
    }

    // ---------------- Chat transcript --------------------------------------

    // One renderable unit: a single message OR a collapsed run of >=2
    // consecutive tool messages (a "pile"). Pre-computed once per frame with
    // its measured height so we can (a) sum total content height and (b)
    // VIRTUALIZE — only emit UI entities for items in the visible scroll range.
    struct Item {
        enum Kind { Bubble, ToolPile, ToolBlock, Spawn } kind;
        int lo = 0;
        int hi = 0;
        float height = 0.0f;
        bool isLive = false;
        // Assistant author label grouping (V2): the "hanabi" author row shows
        // only on the FIRST assistant message of a turn (i.e. when the previous
        // message was the user, or this is the first message). One real
        // assistant turn splits into several Assistant text messages interleaved
        // with Tool messages, so without grouping the name repeats on every
        // fragment. Continuation fragments (prev = Assistant/Tool) suppress it.
        bool showAuthor = true;
    };

    static ecs::model::TranscriptRenderCache& render_cache() {
        static ecs::model::TranscriptRenderCache c;
        return c;
    }

    // Chat welcome / empty state (no thread open): a centered hero — brand mark,
    // a greeting, and a few suggestion chips — instead of a bare "open a thread"
    // note in a tall void. Modern-chat "What can I help with?" landing.
    void render_chat_welcome(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app, float paneW, float paneH) {
        (void)paneW;
        // The composer is rendered ONCE at the pane level (always visible), so
        // the welcome hero just fills its content height.
        float colH = paneH - 20.0f;
        if (colH < 120.0f) colH = 120.0f;
        auto col = div(ctx, mk(parent, 90),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(colH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("chat_welcome"));
        // Brand mark (the sparkle), muted.
        div(ctx, mk(col.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(40), pixels(40)})
                .with_transparent_bg()
                .with_margin(Margin{.bottom = pixels(14)})
                .with_on_draw_fg([](RectangleType r) {
                    hanabi::icons::draw_at("brand", r.x + r.width * 0.5f,
                                           r.y + r.height * 0.5f, 30.0f,
                                           theme::text_secondary());
                })
                .with_debug_name("welcome_mark"));
        div(ctx, mk(col.ent(), 2),
            ComponentConfig{}
                .with_label("What can I help with?")
                .with_size(ComponentSize{pixels(360), pixels(28)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::H1)
                .with_alignment(TextAlignment::Center)
                .with_margin(Margin{.bottom = pixels(18)})
                .with_roundness(0.0f)
                .with_debug_name("welcome_greeting"));
        // Suggestion chips — clicking one starts a new task seeded with it.
        static const char* kChips[] = {
            "Summarize what's waiting on me",
            "What changed since I last looked?",
            "Draft a status update",
        };
        auto chips = div(ctx, mk(col.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{children(), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("welcome_chips"));
        for (int i = 0; i < 3; ++i) {
            auto chip = button(ctx, mk(chips.ent(), 10 + i),
                ComponentConfig{}
                    .with_label(kChips[i])
                    .with_size(ComponentSize{pixels(320), pixels(34)})
                    .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                          .bottom = pixels(6),
                                          .left = pixels(14)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_margin(Margin{.bottom = pixels(8)})
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.5f)
                    .with_debug_name("welcome_chip"));
            if (chip) {
                // Seed the LANDING composer's kickoff draft (below) instead of
                // opening the modal overlay — the composer picks up welcomeSeed
                // in kickoff mode, so the chip pre-fills the visible input and
                // the user hits Send/Enter to start the new session.
                app.welcomeSeed = kChips[i];
            }
        }
    }

    // ---- SPLIT VIEW (I2): two transcripts side by side ----------------------
    // Left = the primary open thread (app.openSession); right = app.splitSession.
    // render_transcript reads app.openSession throughout (20 call sites), so
    // rather than thread a session param through all of it, we render the LEFT
    // pane normally, then SWAP app.openSession<->app.splitSession around the
    // RIGHT pane's render and restore after. Rendering is synchronous within the
    // frame and every per-thread static in render_transcript is keyed by the
    // session id (not a single "current"), so the swap is safe and each pane
    // keeps its own scroll/follow state. A thin divider + a close (×) affordance
    // sit between/above the panes.
    void render_split(UIContext<InputAction>& ctx, Entity& parent,
                      AppComponent& app, float paneW, float paneH) {
        const float kDivider = 1.0f;
        const float halfW = (paneW - kDivider) * 0.5f;

        auto rowWrap = div(ctx, mk(parent, 4100),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(paneW), pixels(paneH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("split_row"));

        // LEFT pane (primary thread).
        auto leftPane = div(ctx, mk(rowWrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(halfW), pixels(paneH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("split_left"));
        render_transcript(ctx, leftPane.ent(), app, halfW, paneH);

        // Vertical divider.
        div(ctx, mk(rowWrap.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kDivider), pixels(paneH)})
                .with_custom_background(theme::border())
                .with_roundness(0.0f)
                .with_debug_name("split_divider"));

        // RIGHT pane (split thread) — swap openSession in for the render.
        auto rightPane = div(ctx, mk(rowWrap.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(halfW), pixels(paneH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("split_right"));
        if (app.splitSession) {
            std::optional<api::Session> savedOpen = std::move(app.openSession);
            std::string savedSel = app.selectedId;
            app.openSession = std::move(app.splitSession);
            app.selectedId = app.openSession->summary.id;
            render_transcript(ctx, rightPane.ent(), app, halfW, paneH);
            // Restore: move the (possibly mutated) session back to splitSession.
            app.splitSession = std::move(app.openSession);
            app.openSession = std::move(savedOpen);
            app.selectedId = savedSel;
        } else {
            // Right pane still loading its transcript.
            div(ctx, mk(rightPane.ent(), 1),
                ComponentConfig{}
                    .with_label("Loading\xe2\x80\xa6")
                    .with_size(ComponentSize{percent(1.0f), pixels(40)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::BODY)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("split_right_loading"));
        }

        // Close-split affordance: a small × pinned to the top-right of the
        // whole main pane. Clicking clears the split (back to single pane).
        auto closeBtn = div(ctx, mk(parent, 4150),
            ComponentConfig{}
                .with_label("\xc3\x97")
                .with_size(ComponentSize{pixels(24), pixels(24)})
                .with_absolute_position()
                .with_translate(paneW - 30.0f, 8.0f)
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(theme::roundness_for_px(5.0f, 24.0f, 24.0f))
                .with_render_layer(4)
                .with_debug_name("split_close"));
        closeBtn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (closeBtn.ent().get<afterhours::ui::HasClickListener>().down)
            app.requestSplitClose = true;
    }

    void render_transcript(UIContext<InputAction>& ctx, Entity& parent,
                           AppComponent& app, float paneW, float paneH) {
        std::string title = "Select a thread";
        if (app.openSession) {
            std::string t = normalize_title(app.openSession->summary.title);
            if (t.empty()) {
                if (const auto* ls = app.find_summary(app.openSession->summary.id))
                    t = normalize_title(ls->title);
            }
            if (t.empty()) t = app.openSession->summary.id;
            title = t.empty() ? "(untitled)" : t;
        } else if (app.transcriptState == LoadState::Loading) {
            title = "Loading\xe2\x80\xa6";
        } else if (app.transcriptState == LoadState::Error) {
            title = "Error";
        }
        // Transcript header (mirrors the mock's .hdr): the thread title on one
        // line + a muted metadata subtitle ("N messages · age") beneath — NOT a
        // bare count, and NOT a second big H1 that just repeats the tab. This is
        // the single "what am I looking at" anchor for the pane.
        if (app.openSession) {
            // Header subtitle = just the age (+ optional "refreshing…"). The
            // message COUNT was dropped here (Gabe: "we don't need this at the
            // top … i don't think we care about how many messages") — the count,
            // if anyone wants it, rides in the tab title instead (see tab_bar).
            std::string sub;
            const std::string age =
                fmtutil::relative_time(app.openSession->summary.updated_at);
            if (!age.empty())
                sub = age;
            // Local-first read state (idea #1): when a cached/stale copy is
            // already painted AND a background refresh is in flight for THIS
            // thread, say so — the read is served instantly from the local
            // copy (never a spinner), with the server revalidate happening
            // quietly. Makes "you're seeing your local copy, refreshing" visible.
            if (!app.transcriptLoadingId.empty() &&
                app.transcriptLoadingId == app.openSession->summary.id)
                sub = sub.empty() ? "refreshing\xe2\x80\xa6"
                                  : (sub + "  \xc2\xb7  refreshing\xe2\x80\xa6");
            transcript_header(ctx, parent, title, sub);
        }
        // (No header when there's no open thread — the welcome hero below is the
        // whole surface; a "Select a thread" bar would just duplicate it.)

        if (app.transcriptState == LoadState::Error) {
            note(ctx, parent,
                 "Could not load transcript: " + app.transcriptError);
            return;
        }
        if (!app.openSession) {
            render_chat_welcome(ctx, parent, app, paneW, paneH);
            return;
        }
        // Per-thread switch spinner: if we're loading a thread whose content is
        // NOT what's currently in openSession (a fresh switch, no stale paint
        // to reuse), show a spinner instead of blank/wrong content. When stale
        // data for the SAME id is present, keep showing it (stale-while-
        // revalidate) rather than flashing a spinner. (Data layer guarantees
        // the fetch/parse is off the UI thread — no beachball.)
        if (app.transcriptState == LoadState::Loading &&
            !app.transcriptLoadingId.empty() &&
            app.openSession->summary.id != app.transcriptLoadingId) {
            loading_spinner(ctx, parent, "Loading conversation\xe2\x80\xa6");
            return;
        }

        // The composer is rendered ONCE at the pane level; paneH here is
        // already the CONTENT height (pane minus composer), so the transcript
        // fills it directly — no local composer reservation.
        const bool canReply =
            app.client &&
            (app.client->supports_send() || app.client->supports_stream());
        // Header is the stacked transcript_header (title + metadata sub); its
        // total height (52 content + 14 top + 8 bottom pad) is ~74px. Subtract
        // that so the scroll list starts cleanly beneath the header.
        constexpr float kHeaderH = 62.0f;
        float listH = paneH - kHeaderH;
        if (listH < 20.0f) listH = 20.0f;

        // Modern-chat centering: the transcript reads best in a ~720px column
        // centered in the pane. afterhours won't center a fixed child inside a
        // content-collapsing scroll, so we center via the scroll's OWN L/R
        // padding: gutter = (paneW - 720)/2, clamped to a sane minimum. On a
        // narrow pane the gutter floors and the column just uses the width.
        constexpr float kReadCol = 720.0f;
        float gutter = (paneW - kReadCol) * 0.5f;
        if (gutter < kContentInset) gutter = kContentInset;

        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                // Symmetric gutters center the reading column; a small extra
                // 6px is trimmed on the right for the overlay scrollbar strip.
                .with_padding(Padding{.top = pixels(8),
                                      .right = pixels(gutter - 6.0f),
                                      .bottom = pixels(10),
                                      .left = pixels(gutter)})
                .with_debug_name("transcript_scroll"));
        // TEMPORARY scroll indicator (afterhours gap #26): afterhours has no
        // built-in scrollbar, so paint a thin overlay bar from the panel's live
        // HasScrollView metrics. The 14px right padding above already keeps the
        // reading column clear of the bar's right strip.
        hanabi::attach_scroll_indicator(scroll.ent());
        hanabi::apply_scroll_prefs(scroll.ent());

        if (app.openSession->messages.empty()) {
            div(ctx, mk(scroll.ent(), 1),
                preset::EmptyStateText(
                    canReply ? "No messages yet \xe2\x80\x94 start the "
                               "conversation below."
                             : "No messages in this thread.")
                    .with_size(ComponentSize{percent(1.0f), pixels(40)})
                    .with_padding(Padding{.top = pixels(28), .right = pixels(18),
                                          .bottom = pixels(8),
                                          .left = pixels(18)})
                    .with_alignment(TextAlignment::Center)
                    .with_debug_name("transcript_empty"));
            return;
        }

        // Fill the available pane width (Gabe's ask) rather than capping the
        // reading column — keep a modest side margin so text isn't flush to the
        // scrollbar/edge. A high cap keeps it sane on an ultrawide window; pass
        // it through centered_wrap so the transcript ISN'T re-clamped to the
        // narrower shared reading cap (Home/digest still use the 900 default).
        // Modern-chat reading column: cap the transcript at a comfortable
        // measure and CENTER it in the pane (ChatGPT/Claude/Gemini all do this)
        // instead of letting messages hug the left edge of a wide window with
        // dead space on the right. ~720px keeps assistant prose at a readable
        // ~90-char measure; on a narrow pane it falls back to the full width.
        // The scroll's symmetric gutters (above) already center the reading
        // area to ~720px, so the message column just fills the padded content
        // width. centered_wrap here only stacks the messages (no re-centering).
        float colW = paneW - 2.0f * gutter;
        if (colW < 120.0f) colW = 120.0f;
        Entity& col = centered_wrap(ctx, scroll.ent(), 7777, colW, colW);

        render_cache().reset_for_thread(app.openSession->summary.id);

        float subH = sub_agent_panel(ctx, col, app);

        const bool streamingHere =
            app.streamActive &&
            app.streamSessionId == app.openSession->summary.id &&
            app.streamPhase != AppComponent::StreamPhase::Done;
        const size_t liveIdx = app.streamMsgIndex;

        const auto& msgs = app.openSession->messages;
        const int n = static_cast<int>(msgs.size());

        // ---- Pass 1: item list + measured heights (memoized). --------------
        std::vector<Item> items;
        items.reserve(n);
        float totalH = subH;
        {
            int i = 0;
            while (i < n) {
                const auto& m = msgs[i];
                if (m.role == api::Role::Tool) {
                    // A SPAWN (sub-agent launch) is rendered as its own distinct
                    // inline card, NOT lumped into a tool pile (Gabe: "add UI for
                    // when a thing is spawned").
                    if (is_spawn_tool(m)) {
                        Item it;
                        it.kind = Item::Spawn;
                        it.lo = i;
                        it.height = spawn_card_height();
                        totalH += it.height;
                        items.push_back(it);
                        ++i;
                        continue;
                    }
                    int j = i;
                    while (j < n && msgs[j].role == api::Role::Tool &&
                           !is_spawn_tool(msgs[j]))
                        ++j;
                    if (j - i >= 2) {
                        Item it;
                        it.kind = Item::ToolPile;
                        it.lo = i;
                        it.hi = j;
                        it.height = tool_pile_height(app, msgs, i, j);
                        totalH += it.height;
                        items.push_back(it);
                        i = j;
                        continue;
                    }
                    Item it;
                    it.kind = Item::ToolBlock;
                    it.lo = i;
                    it.height = tool_block_height(app, msgs[i]);
                    totalH += it.height;
                    items.push_back(it);
                    ++i;
                    continue;
                }
                Item it;
                it.kind = Item::Bubble;
                it.lo = i;
                it.isLive = streamingHere && static_cast<size_t>(i) == liveIdx;
                // Show the assistant author label only when this assistant
                // message STARTS an assistant run — i.e. the previous message
                // is NOT assistant-side (Assistant or Tool are both the same
                // turn). A User or System message before it (or being first)
                // begins a new run, so the "hanabi" name shows once at the top
                // and continuation fragments (prev = Assistant/Tool) suppress
                // the repeat. (User messages never show it.)
                it.showAuthor =
                    (i == 0) || (msgs[i - 1].role != api::Role::Assistant &&
                                 msgs[i - 1].role != api::Role::Tool);
                it.height = bubble_height(m, colW, it.isLive, i, it.showAuthor);
                totalH += it.height;
                items.push_back(it);
                ++i;
            }
        }

        // ---- Virtualization: read last frame's scroll to skip off-screen. --
        float scrollY = 0.0f;
        float viewH = listH;
        if (scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            scrollY = sv.scroll_offset.y;
            if (sv.viewport_size.y > 1.0f) viewH = sv.viewport_size.y;
        }

        // ---- Load-older: scroll-anchor + trigger + prefetch ---------------
        // (a) ANCHOR: when older messages were just prepended (loader armed
        //     anchorPending), the content grew above the viewport. Measure the
        //     height of the newly-prepended items and bump scroll_offset by it,
        //     so the user's view stays on the same message instead of snapping
        //     to the newly-loaded oldest. Cleared after one application.
        const std::string openId = app.openSession->summary.id;
        if (app.anchorPending == openId &&
            msgs.size() > app.anchorPrevMsgCount &&
            scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const size_t added = msgs.size() - app.anchorPrevMsgCount;
            float prependedH = 0.0f;
            for (const auto& it : items) {
                if (static_cast<size_t>(it.lo) < added)
                    prependedH += it.height;
                else
                    break;  // items are in message order; done past the prepend
            }
            auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            sv.scroll_offset.y += prependedH;  // hold the viewport steady
            sv.clamp_scroll();
            scrollY = sv.scroll_offset.y;
            app.anchorPending.clear();
        }
        // (b) TRIGGER + PREFETCH: when the user is near the TOP and there are
        //     older messages, request a load. A generous threshold (2 viewports)
        //     PREFETCHES before the user hits the very top, so older content is
        //     usually already there by the time they reach it — and reaching
        //     the top faster (fast scroll-up) just triggers sooner. Guarded by
        //     loadingOlder (loader clears it) so it fires once per page.
        if (app.hasMoreOlder && !app.loadingOlder && !app.requestLoadOlder &&
            app.anchorPending.empty() && scrollY <= viewH * 2.0f) {
            app.requestLoadOlder = true;
        }
        // ---- Open-at-bottom + stay-pinned model -----------------------
        // A conversation opens showing the NEWEST messages (bottom), like every
        // chat app. Then: if the user is AT the bottom, keep them pinned as new
        // messages arrive (streaming or SSE); if they've scrolled UP, leave
        // their position alone (don't yank them down). Tracked per-session with
        // a function-local static so we jump-to-bottom exactly once per open
        // (no AppComponent growth). `wasAtBottom` is computed from LAST frame's
        // metrics (content_size/viewport/offset) captured below.
        // Open-at-bottom + stay-pinned model:
        //   * A thread opened for the FIRST time (new tab) lands at the bottom
        //     (newest message). This is driven by app.scrollBottomPending, set
        //     ONLY in the new-tab open path (switching to an already-open tab
        //     does NOT set it, so that tab keeps its scroll position).
        //   * While a thread is streaming here, or the user is already AT the
        //     bottom, keep them pinned as new messages arrive.
        //   * If the user scrolled UP, leave their position alone.
        // scrollBottomPending is cleared only once we've actually pinned
        // against REAL laid-out content (content_size known) — the first render
        // frame after an open has no content_size yet, so clearing on frame 0
        // would clamp 1e9 against a stale height and never reach the true
        // bottom (the old s_bottomedId bug).
        const std::string curId = app.openSession->summary.id;
        const bool wantOpenBottom = (app.scrollBottomPending == curId);

        // ---- Persistent FOLLOW-LATCH (fixes "page doesn't stay at bottom") ---
        // The old model recomputed `atBottom` from geometry EVERY frame:
        //   atBottom = (offset + viewH >= contentH - 24)
        // But when a new message arrives or a stream token lands, contentH grows
        // while offset stays put — so that test flips FALSE the instant content
        // grows, and following stops one message from the end (exactly Gabe's
        // repeated complaint). The distinction the geometry test misses: content
        // GROWTH and a user SCROLL-UP both increase (contentH - offset - viewH),
        // but only a scroll-up is a real "user left the bottom" signal.
        //
        // Fix: a per-session latch that means "keep pinned to bottom". It starts
        // TRUE (fresh opens pin to bottom) and is broken ONLY when the user
        // actively scrolls UP — detected as scroll_offset DECREASING frame over
        // frame (growth never decreases offset). It re-arms when the user comes
        // back to within ~24px of the end. This makes "stay at bottom" robust to
        // the content-growth race.
        //
        // Keyed PER SESSION (not a single static) so split-view can render two
        // transcripts in one frame without their follow/scroll state clobbering
        // each other. Each pane's curId indexes its own latch.
        static std::unordered_map<std::string, bool> s_followMap;
        static std::unordered_map<std::string, float> s_prevOffsetMap;
        auto followIt = s_followMap.find(curId);
        if (followIt == s_followMap.end()) {   // first sight of this thread
            s_followMap[curId] = true;
            s_prevOffsetMap[curId] = -1.0f;
        }
        bool& s_follow = s_followMap[curId];
        float& s_prevOffset = s_prevOffsetMap[curId];
        float curOffset = 0.0f;
        float contentH = totalH;
        bool contentLaidOut = false;
        bool nearEnd = true;
        if (scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            curOffset = sv.scroll_offset.y;
            if (sv.content_size.y > 1.0f) {
                contentH = sv.content_size.y;
                contentLaidOut = true;
            }
            nearEnd = (curOffset + viewH >= contentH - 24.0f);
            // A meaningful DECREASE in offset = the user scrolled up → stop
            // following. (Small jitter / clamp wobble ignored via a 2px floor.)
            if (s_prevOffset >= 0.0f && curOffset < s_prevOffset - 2.0f)
                s_follow = false;
            // Returning to the bottom re-arms follow.
            if (nearEnd) s_follow = true;
            s_prevOffset = curOffset;
        }
        // Pin to bottom on a first-open, while streaming here, or whenever the
        // follow-latch is engaged (user hasn't scrolled up).
        const bool atBottom = s_follow || nearEnd;
        const bool pinBottom = wantOpenBottom || streamingHere || s_follow;
        if (pinBottom) scrollY = totalH;
        // Build a virtualization window around the visible viewport. A fast
        // fling moves scroll_offset by MANY px between frames, and we read LAST
        // frame's offset here — so a fixed small margin (old: 0.5*viewH) let a
        // fast scroll outrun the built window and reveal blank gaps until the
        // user stopped. Fix: (a) a generous base margin, and (b) a
        // velocity-aware extension in the direction of travel, computed from
        // the per-frame scroll delta, so the window always covers where the
        // content will be next frame. Tracked per-session (map, not a single
        // static) so switching tabs — or rendering two panes in split-view —
        // doesn't inherit a stale velocity from the other thread.
        static std::unordered_map<std::string, float> s_lastScrollYMap;
        auto velIt = s_lastScrollYMap.find(curId);
        float vel = 0.0f;
        if (velIt != s_lastScrollYMap.end()) vel = scrollY - velIt->second;
        s_lastScrollYMap[curId] = scrollY;
        // Base margin ~1 viewport each side (covers normal wheel steps), plus
        // an extension of several frames of the current velocity in the travel
        // direction (clamped so a huge jump doesn't build the whole doc).
        const float kBaseMargin = viewH * 1.0f;
        const float kMaxExtend = viewH * 4.0f;
        const float extendDown = std::clamp(vel * 6.0f, 0.0f, kMaxExtend);
        const float extendUp = std::clamp(-vel * 6.0f, 0.0f, kMaxExtend);
        const float visTop = scrollY - kBaseMargin - extendUp;
        const float visBot = scrollY + viewH + kBaseMargin + extendDown;

        // ---- Pass 2: emit spacers + only the visible items. ----------------
        float y = subH;
        float pendingSpacer = 0.0f;
        auto flush_spacer = [&](int tag) {
            if (pendingSpacer <= 0.0f) return;
            div(ctx, mk(col, 30000 + tag),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels(pendingSpacer)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("virt_spacer"));
            pendingSpacer = 0.0f;
        };
        // Sparse-thread balance (chat spec #6): when the whole transcript is
        // SHORTER than the viewport (a 1-3 message thread), don't pin it to the
        // top with a big void below — nudge it toward the upper-middle with a
        // leading spacer of ~1/3 the slack. Only when content fits (no scroll),
        // so long threads + virtualization are untouched. Not while streaming
        // (content is growing) or loading older.
        if (totalH < viewH - 40.0f && !streamingHere && !app.loadingOlder &&
            app.anchorPending.empty()) {
            div(ctx, mk(col, 29999),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels((viewH - totalH) / 3.0f)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("sparse_balance"));
        }
        for (const auto& it : items) {
            const float top = y;
            const float bot = y + it.height;
            y = bot;
            const bool visible = (bot >= visTop) && (top <= visBot);
            if (!visible) {
                pendingSpacer += it.height;
                continue;
            }
            flush_spacer(it.lo);
            switch (it.kind) {
                case Item::Bubble:
                    render_bubble(ctx, col, it.lo, msgs[it.lo], colW,
                                  it.isLive, app.streamPhase, visTop, visBot,
                                  top, it.showAuthor);
                    break;
                case Item::ToolPile:
                    tool_pile(ctx, col, it.lo, msgs, it.lo, it.hi, colW);
                    break;
                case Item::ToolBlock:
                    render_tool_block(ctx, col, it.lo, msgs[it.lo], colW);
                    break;
                case Item::Spawn:
                    render_spawn_card(ctx, col, it.lo, msgs[it.lo], colW);
                    break;
            }
        }
        flush_spacer(99999);

        // Bottom breathing room: a real trailing spacer so the LAST line can be
        // scrolled fully clear of the viewport bottom (and the composer that
        // overlays it). Without this the final message sat flush against / under
        // the edge and couldn't be brought fully into view.
        div(ctx, mk(col, 30000 + 88888),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28.0f)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("transcript_bottom_pad"));

        // Apply the bottom pin when we decided to (first-open / streaming /
        // already-at-end). Otherwise leave the user's scroll be.
        if (pinBottom && scroll.ent().has<afterhours::ui::HasScrollView>()) {
            auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            sv.scroll_offset.y = 1e9f;  // clamped to content end next line
            hanabi::set_scroll_target_y(sv, 1e9f);  // sync eased target (patch)
            sv.clamp_scroll();
            s_prevOffset = sv.scroll_offset.y;  // don't read the pin as scroll-up
        }
        // Clear the first-open request only once we've pinned against REAL
        // laid-out content (content_size known this frame). Until then keep the
        // request alive so the next frame (which HAS the laid-out height) does
        // the real jump-to-bottom. Guard on curId so a fast switch to another
        // new thread doesn't clear the wrong pending id.
        if (wantOpenBottom && contentLaidOut &&
            app.scrollBottomPending == curId) {
            app.scrollBottomPending.clear();
        }

        // Floating "jump to bottom" affordance: a small down-chevron pinned to
        // the bottom-right of the transcript pane, shown only when the user is
        // scrolled UP (not at the bottom) and there's meaningfully more below.
        // Clicking snaps to the newest message. (Rendered on the parent pane,
        // absolutely positioned, above the scroll content.)
        if (!pinBottom && !atBottom && contentH > viewH + 40.0f) {
            jump_to_bottom_button(ctx, parent, scroll.ent(), paneW,
                                  46.0f + listH);
        }

        // Top "loading older" pill: a small centered spinner + caption pinned
        // to the TOP of the transcript pane while a load-older fetch is in
        // flight, so the load has visible feedback instead of a silent
        // freeze/snap. Overlay (absolute on the parent pane) so it doesn't
        // shift the scroll content / fight the anchor math. kHeaderH offsets it
        // below the title header.
        if (app.loadingOlder) {
            loading_older_pill(ctx, parent, paneW, 62.0f + 6.0f);
        }
        // (Composer is rendered once at the pane level — not here.)
        (void)canReply;
    }

    // Persistent composer pinned to the bottom of the transcript pane. A real,
    // functional afterhours text_input + a Send affordance.
    //
    // WIRING (Phase SEND): the api::Client interface now exposes
    // send_message(session_id, prompt) + supports_send(). When the backend
    // supports replies (the mock DOES; a chat-path-configured http backend
    // does), the Send button is ENABLED whenever the draft is non-empty: on
    // click it sets app.requestSendPrompt (serviced by LoaderSystem, which runs
    // send_message async and appends the user prompt + assistant reply to
    // app.openSession->messages) and clears the local draft. When the backend
    // can't reply (an unconfigured http backend), the button stays disabled-
    // styled with an honest caption instead of faking it.
    void render_composer(UIContext<InputAction>& ctx, Entity& parent,
                         AppComponent& app, float paneW, float composerH,
                         bool kickoff = false, float absX = -1.0f,
                         float absY = -1.0f) {
        // KICKOFF mode: rendered on the Home landing screen (no thread open) so
        // you can start typing the moment the app opens — every daily-driver
        // chat app has a persistent input on its landing view. In kickoff mode
        // Send/Enter route to app.requestKickoffPrompt (create_session), NOT the
        // reply path (which needs an open selectedId). The loader opens the new
        // thread as a tab, so the view transitions Home -> Chat automatically.
        // PER-THREAD persistent drafts. One composer instance renders whichever
        // thread is active, so a single shared draft would leak thread A's
        // half-typed reply into thread B on a tab switch. Key the draft by
        // session id and bind a reference to THIS thread's slot, so each thread
        // keeps its own in-progress reply (kept function-local to avoid growing
        // AppComponent; the map is small — one short string per opened thread).
        static std::map<std::string, std::string> replyDrafts;
        const std::string draftKey =
            kickoff ? std::string("__kickoff__")
                    : (app.openSession ? app.openSession->summary.id
                                       : std::string());
        std::string& replyDraft = replyDrafts[draftKey];

        // Consume a welcome-screen suggestion-chip seed into the new-task draft
        // (once). Applies to the new-task composers (kickoff Home composer or
        // the empty-key overlay) so it never overwrites a real thread's
        // in-progress reply.
        if (!app.welcomeSeed.empty() && (kickoff || draftKey.empty())) {
            replyDraft = app.welcomeSeed;
            app.welcomeSeed.clear();
        }

        // Screenshot affordance: HANABI_REPLY_DEMO=<text> seeds the draft ONCE
        // so a headless capture can photograph the composer's ENABLED (primary)
        // Send. Mirrors HANABI_VIEW / HANABI_AUTH_DEMO: ignored when unset, no
        // network. Seeded a single time — and only once a thread is actually
        // open (draftKey non-empty), so it lands in the visible thread's slot
        // rather than the empty-key slot on an early pre-load frame. Live typing
        // still owns the field afterward.
        static bool replyDemoSeeded = false;
        if (!replyDemoSeeded && !draftKey.empty()) {
            replyDemoSeeded = true;
            if (const char* d = std::getenv("HANABI_REPLY_DEMO"); d && *d)
                replyDraft = d;
        }

        // Screenshot affordance: HANABI_SEND_DEMO=<text> fires an actual reply
        // ONCE (sets the one-shot requestSendPrompt) so a headless capture over
        // the render frames shows the appended User + synthetic Assistant turn.
        // Loader runs before this system each frame, so the exchange lands a
        // frame or two later — well within the capture's 45-frame budget.
        // Ignored when unset; no network (the mock generates the reply).
        static bool sendDemoFired = false;
        if (!sendDemoFired && app.openSession) {
            if (const char* d = std::getenv("HANABI_SEND_DEMO"); d && *d) {
                sendDemoFired = true;
                app.requestSendPrompt = d;
            }
        }

        // Screenshot affordance: HANABI_STREAM_DEMO=<text> fires a STREAMED
        // reply ONCE (sets the one-shot requestStreamPrompt) so a headless
        // capture shows the live token-by-token bubble. Pair it with
        // HANABI_STREAM_DEMO_MAXTOKENS=<K> (read in the loader) to FREEZE the
        // drain after K tokens for the mid-stream shot; leave it unset for the
        // completed shot. Ignored when unset; no network (the mock streams).
        static bool streamDemoFired = false;
        if (!streamDemoFired && app.openSession && app.client &&
            app.client->supports_stream()) {
            if (const char* d = std::getenv("HANABI_STREAM_DEMO"); d && *d) {
                streamDemoFired = true;
                app.requestStreamPrompt = d;
            }
        }

        const bool canSend = app.client && app.client->supports_send();
        const bool canStream = app.client && app.client->supports_stream();
        const std::string& openId = draftKey;  // same value: the open thread id
        // "Sending" covers BOTH the synchronous reply in flight and a live
        // stream draining into this thread — either disables the composer. In
        // kickoff mode it's the create_session round-trip (kickoffPending).
        const bool sending =
            kickoff ? app.kickoffPending
                    : ((app.sendPending && app.sendSessionId == openId) ||
                       (app.streamActive && app.streamSessionId == openId));
        const bool hasText = !replyDraft.empty();
        const size_t queued = kickoff ? 0 : app.pending_send_count(openId);
        // The loader QUEUES a send that arrives while one is in flight (FIFO,
        // drained when the current turn finishes), so Send stays enabled during
        // a send — you can line up the next message. Only truly-unavailable
        // (no backend / empty field) disables it. Kickoff needs supports_send
        // (create_session shares the chat_path) and no in-flight kickoff.
        const bool sendEnabled =
            kickoff ? (canSend && hasText && !app.kickoffPending)
                    : (canSend && hasText);

        // Center the composer under the 720px reading column (same gutter as
        // the transcript scroll). Falls back to kContentInset on a narrow pane.
        constexpr float kReadCol = 720.0f;
        float composerGutter = (paneW - kReadCol) * 0.5f;
        if (composerGutter < kContentInset) composerGutter = kContentInset;

        auto barCfg = ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(composerH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                // Center the composer under the 720px reading column (matches
                // the transcript gutters) so the input lines up with the
                // messages instead of spanning the full pane. Same gutter math
                // as render_transcript's scroll padding.
                .with_padding(Padding{.top = pixels(8),
                                      .right = pixels(composerGutter),
                                      .bottom = pixels(8),
                                      .left = pixels(composerGutter)})
                .with_custom_background(theme::panel_bg())
                .with_roundness(0.0f)
                // Render ABOVE the content (layer 2 > the content's default/1)
                // so a transcript whose scroll content overflows its clip can
                // never paint over the composer — it is always on top and
                // always visible (Gabe: "it should just always render").
                .with_render_layer(2)
                .with_debug_name("composer_bar");
        // ABSOLUTE-PIN to the pane bottom in SCREEN coords when the caller
        // passes absX/absY. As a flex sibling, a tall transcript that overflows
        // the content box pushed the composer off the bottom edge (the windowed
        // 'no chat input' bug — headless clipped so it looked fine). Absolute
        // position takes it OUT of the flex flow so its position is fixed
        // regardless of content height. afterhours absolute+translate is
        // screen-space (see main_pane at r.x,r.y), so absX/absY are screen
        // coords; a fixed width (not percent) since it has no flex parent now.
        if (absX >= 0.0f && absY >= 0.0f) {
            barCfg = barCfg
                         .with_size(ComponentSize{pixels(paneW), pixels(composerH)})
                         .with_absolute_position()
                         .with_translate(absX, absY)
                         .with_render_layer(6);  // above pane content
        }
        auto bar = div(ctx, mk(parent, 3), barCfg);

        // A hairline top border sold via a 1px divider row so the composer
        // reads as a distinct footer strip separated from the message column.
        div(ctx, mk(bar.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
                .with_custom_background(theme::border())
                .with_margin(Margin{.bottom = pixels(7)})
                .with_roundness(0.0f)
                .with_debug_name("composer_divider"));

        auto row = div(ctx, mk(bar.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(34)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_row"));

        // The input grows to fill; a fixed-width Send button sits at the right.
        // NOTE (afterhours_gaps.md #17): text_input forces its own Secondary
        // background and derives font size from field HEIGHT (~0.5*h) — it
        // ignores with_font_size / with_custom_background. So the field is kept
        // ~34px tall for a readable ~17px font, matching the composer overlay
        // and sidebar search field workarounds.
        float sendW = 78.0f;
        float inputW = paneW - (composerGutter * 2.0f) - sendW - 8.0f;
        if (inputW < 120.0f) inputW = 120.0f;

        auto inputWrap = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(inputW), pixels(34)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.right = pixels(8)})
                // Visible filled pill so the input reads as an INPUT even when
                // empty (it was a near-invisible faint-bordered box — the
                // "where's the input?" bug). The transparent text_input sits
                // inside this pill; its forced Secondary fill (gap #17) is
                // matched to panel_bg_2 below so they blend.
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.5f)
                .with_debug_name("composer_input_wrap"));

        // text_input forces its own Secondary bg over its rect (gap #17); point
        // Secondary/Surface at panel_bg_2 so the field blends into the pill
        // above instead of painting a jarring default-dark box.
        ctx.theme.secondary = theme::panel_bg_2();
        ctx.theme.surface = theme::panel_bg_2();
        ctx.theme.font = theme::text_primary();
        auto inputRes = afterhours::ui::imm::text_input(
            ctx, mk(inputWrap.ent(), 1), replyDraft,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(34)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_padding(Padding{.left = pixels(12), .right = pixels(10)})
                .with_roundness(0.5f)
                .with_debug_name("composer_reply_input"));

        // ENTER-TO-SEND. afterhours' text_input fires on_submit on Enter
        // (WidgetPress == ENTER, preload.cpp) IF the entity carries a
        // HasTextInputListener — the imm wrapper doesn't attach one, so a naked
        // text_input swallowed Enter and the ONLY way to send was clicking the
        // button (Gabe: "HOW DO I SEND A MESSAGE"). Attach a listener whose
        // on_submit sets the SAME one-shot send/stream request the Send button
        // does, so Enter sends like every chat app. Shift+Enter is NOT a newline
        // here (single-line composer); plain Enter = send.
        {
            Entity& inputEnt = inputRes.ent();
            inputEnt.addComponentIfMissing<
                afterhours::text_input::HasTextInputListener>(
                nullptr,  // on_change: not needed (imm syncs replyDraft)
                [appPtr = &app, canStream, canSend, kickoff,
                 draftPtr = &replyDraft](Entity& e) {
                    // Read the CURRENT field text off the input state (the most
                    // up-to-date value, incl. the char typed just before Enter).
                    std::string text;
                    if (e.has<afterhours::text_input::HasTextInputState>())
                        text = e.get<afterhours::text_input::HasTextInputState>()
                                   .text();
                    // Trim trailing whitespace/newline the Enter may leave.
                    while (!text.empty() &&
                           (text.back() == '\n' || text.back() == '\r' ||
                            text.back() == ' '))
                        text.pop_back();
                    if (text.empty()) return;              // nothing to send
                    if (!(canStream || canSend)) return;   // backend can't send
                    // Kickoff (Home landing composer) starts a NEW session via
                    // create_session; a normal composer replies to the open one.
                    if (kickoff) appPtr->requestKickoffPrompt = text;
                    else if (canStream) appPtr->requestStreamPrompt = text;
                    else appPtr->requestSendPrompt = text;
                    // Clear BOTH the input state AND the persistent draft (the
                    // imm wrapper re-syncs state<-draft each frame, so clearing
                    // only the state would let the draft repopulate it).
                    if (e.has<afterhours::text_input::HasTextInputState>()) {
                        auto& st =
                            e.get<afterhours::text_input::HasTextInputState>();
                        st.storage.clear();
                        st.cursor_position = 0;
                    }
                    if (draftPtr) draftPtr->clear();
                });
        }

        // Placeholder (text_input has no native placeholder — gap #29): overlay
        // faint hint text ON TOP of the empty field via an absolutely-positioned
        // on_draw_fg child (same proven pattern as the sidebar search), so it's
        // clearly an input at rest. Replaced by real glyphs the moment you type.
        if (replyDraft.empty()) {
            const bool steer = !kickoff && app.should_steer_open();
            const char* ph = kickoff ? "Start a new conversation\xe2\x80\xa6"
                             : steer  ? "Steer the running agent\xe2\x80\xa6"
                                      : "Message hanabi\xe2\x80\xa6";
            div(ctx, mk(inputWrap.ent(), 2),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{percent(1.0f), pixels(34)})
                    .with_absolute_position()
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(3)
                    .with_on_draw_fg([ph](RectangleType rect) {
                        const float px = theme::type::BODY;
                        const float ty = rect.y + rect.height * 0.5f - px * 0.5f;
                        afterhours::draw_text(ph, rect.x + 12.0f, ty, px,
                                              theme::text_faint());
                    })
                    .with_debug_name("composer_placeholder"));
        }

        // Send affordance. Enabled (primary-styled, clickable) when the backend
        // supports replies and the draft has text; otherwise disabled-styled.
        // When the open thread's agent is RUNNING and the backend can steer,
        // this same button STEERS (interrupt/redirect) — relabel to "Steer" so
        // the action reads honestly. Minimal touch: label-only (fits the same
        // fixed sendW), no layout change.
        const bool steerMode = !kickoff && app.should_steer_open();
        // Send = a filled primary pill with an up-arrow (modern chat "send"),
        // Steer keeps its word (interrupt/redirect reads better as text), "…"
        // while in flight. Rounder (0.5) so it reads as an intentional primary
        // action, not a flat rectangle.
        const char* sendLabel =
            sending ? "\xe2\x80\xa6"
                    : (steerMode ? "Steer" : "Send  \xe2\x86\x91");
        auto send = button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(sendLabel)
                .with_size(ComponentSize{pixels(sendW), pixels(32)})
                .with_custom_background(sendEnabled ? theme::button_primary()
                                                    : theme::disabled_bg())
                .with_custom_hover_bg(sendEnabled
                                          ? theme::hover_over(theme::button_primary())
                                          : theme::disabled_bg())
                .with_custom_text_color(sendEnabled ? theme::window_bg()
                                                    : theme::disabled_text())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.5f)
                .with_debug_name("composer_send"));
        if (send && sendEnabled) {
            // Kickoff (Home landing composer) starts a NEW session via
            // create_session (LoaderSystem opens it as a tab). A normal composer
            // routes through the STREAMING path when the backend supports it
            // (the mock does), so the reply fills in token-by-token; otherwise
            // fall back to the synchronous one-shot path (no regression). All
            // are one-shot flags serviced by LoaderSystem; setting only one per
            // turn keeps them mutually exclusive.
            if (kickoff)
                app.requestKickoffPrompt = replyDraft;
            else if (canStream)
                app.requestStreamPrompt = replyDraft;
            else
                app.requestSendPrompt = replyDraft;
            replyDraft.clear();
        }

        // Meta row under the input: model selector chip (left) + a
        // context/cost meter (right) + the status caption — matches the Navi
        // web composer's dense footer (defect #4: was a bare grey "Send" text).
        auto meta = div(ctx, mk(bar.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_margin(Margin{.top = pixels(5)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_meta"));
        // Left: model selector chip.
        div(ctx, mk(meta.ent(), 1),
            ComponentConfig{}
                .with_label("Opus 4.8 (xhigh)  \xe2\x96\xbe")
                .with_size(ComponentSize{children(), pixels(16)})
                .with_padding(Padding{.top = pixels(1), .right = pixels(8),
                                      .bottom = pixels(1), .left = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.5f)
                .with_debug_name("composer_model"));
        // Right cluster: status caption + context/cost meter.
        auto rightMeta = div(ctx, mk(meta.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(16)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_rightmeta"));
        std::string caption;
        if (!canSend)
            caption =
                "read-only \xe2\x80\x94 this backend doesn't support replies";
        else if (sending && queued > 0)
            caption = "sending\xe2\x80\xa6  \xc2\xb7  " +
                      std::to_string(queued) + " queued";
        else if (sending)
            caption = "sending\xe2\x80\xa6";
        else if (queued > 0)
            caption = std::to_string(queued) + " queued";
        else if (hasText)
            // Discoverability: when there's text to send and we're idle, tell
            // the user Enter sends (the fix for "HOW DO I SEND A MESSAGE" — the
            // composer now sends on Enter, not just the button click).
            // (canSend is provably true here — the !canSend arm returned above.)
            caption = steerMode ? "\xe2\x86\xb5 steer" : "\xe2\x86\xb5 send";
        if (!caption.empty()) {
            div(ctx, mk(rightMeta.ent(), 1),
                ComponentConfig{}
                    .with_label(caption)
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.right = pixels(10)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Right)
                    .with_debug_name("composer_status"));
        }
        if (canSend) {
            // Context-usage meter: a small filled bar + "N% context". Replaces
            // the old "38% $$$$" placeholder (audit #14 — the four $ read as
            // unfinished). The backend's SSE context_usage event carries the
            // real token breakdown; until that's wired to the composer we show
            // a representative fill. (No fake dollar signs — a real cost figure
            // needs a backend cost field, requested from the API maintainers.)
            div(ctx, mk(rightMeta.ent(), 2),
                ComponentConfig{}
                    .with_label("context")
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.right = pixels(6)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Right)
                    .with_debug_name("composer_meter_label"));
            // Thin track + accent fill = a real meter, not $ glyphs.
            div(ctx, mk(rightMeta.ent(), 3),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(56), pixels(6)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_roundness(0.5f)
                    .with_on_draw_fg([](RectangleType rr) {
                        constexpr float kCtxPct = 0.38f;
                        float w = rr.width * kCtxPct;
                        if (w < 2.0f) w = 2.0f;
                        afterhours::draw_rectangle_rounded(
                            RectangleType{rr.x, rr.y, w, rr.height}, 0.5f, 6,
                            theme::over(theme::accent(), theme::panel_bg_2()));
                    })
                    .with_debug_name("composer_meter"));
        }
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

    // Text-metrics model for height/line estimation. Calibrated against the
    // real rendered font (FontSize::Medium ~13-14px): the proportional UI face
    // averages ~6.2px/glyph and wraps at ~15px line pitch. The old 8px/18px
    // model massively OVER-estimated — a body box came out ~2x its text, so the
    // wrapped text rendered bottom-aligned inside a tall empty box (a big gap
    // above every assistant turn). Keeping all three helpers on one model so
    // box height, line count, and truncation stay consistent.
    // Calibrated CONSERVATIVELY (over-estimate) because afterhours word-wraps
    // on spaces (rendering.h wrap_text_to_width): the tail of each line is
    // wasted, so real chars/line is LOWER than width/advance. Under-counting
    // lines made fixed-height text boxes CLIP and the scroll range fall
    // short (couldn't scroll to the bottom). 7.6px/glyph + 16px pitch
    // over-estimates slightly (tiny extra gap) but NEVER clips. The proper
    // fix is to measure via afterhours measure_text (gap #26/wishlist A).
    static constexpr float kGlyphW = 7.6f;   // avg px per glyph @ BODY 13px
    static constexpr float kLinePitch = 16.0f;  // px per wrapped line
    // Blank-line (paragraph / list-item gap) height. This is the vertical space
    // between paragraphs and numbered-list items in an assistant turn. Was
    // kLinePitch*0.5 (8px) which stacked into a loose, airy feed vs navi web's
    // tighter spacing (Gabe: "the whitespace is still way too high"). 5px reads
    // as a clear paragraph break without the big gap. ONE constant so the
    // measure + render paths can never drift.
    static constexpr float kBlankPitch = 5.0f;
    static int wrap_perline(float widthPx) {
        int p = static_cast<int>((widthPx - 10.0f) / kGlyphW);
        return p < 8 ? 8 : p;
    }

    // Estimated WRAPPED line count of `text` at `widthPx`. Used to decide
    // whether a body is long enough to fold.
    static int count_lines(const std::string& text, float widthPx) {
        int perLine = wrap_perline(widthPx);
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
        return lines < 1 ? 1 : lines;
    }

    // Return the first `maxLines` WRAPPED lines of `text` (approx: we cut on
    // newline boundaries and, within a long unbroken line, on perLine chars).
    // Used to render a folded preview of a very long message so a huge paste
    // doesn't build thousands of glyph quads (RAM) or dominate the pane.
    static std::string first_n_lines(const std::string& text, float widthPx,
                                     int maxLines) {
        int perLine = wrap_perline(widthPx);
        std::string out;
        int used = 0;
        size_t start = 0;
        while (start <= text.size() && used < maxLines) {
            size_t nl = text.find('\n', start);
            size_t end = (nl == std::string::npos) ? text.size() : nl;
            std::string seg = text.substr(start, end - start);
            // account for wrapping of a long segment
            int segLines =
                seg.empty() ? 1
                            : (static_cast<int>(seg.size()) + perLine - 1) /
                                  perLine;
            if (used + segLines > maxLines) {
                int allow = maxLines - used;
                size_t chars = static_cast<size_t>(allow) *
                               static_cast<size_t>(perLine);
                if (chars < seg.size()) seg = seg.substr(0, chars);
                out += seg;
                used = maxLines;
                break;
            }
            if (!out.empty()) out += "\n";
            out += seg;
            used += segLines;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return out;
    }

    // ---- Shared transcript layout constants (denser doc-feed) --------------
    // SINGLE source of truth for the vertical rhythm, used by BOTH the
    // height/measure pass (virtualization) and the render methods so a spacer
    // div's height always matches what its item would take. Tightened from the
    // old loose feed (~18-24px turn gaps → ~100px dead space) to a dense ~10px
    // rhythm matching the Navi web chat.
    static constexpr int kFoldLines = 40;
    static constexpr float kTurnGapTop = 6.0f;
    static constexpr float kTurnGapBot = 4.0f;
    static constexpr float kAuthorH = 15.0f;
    static constexpr float kAuthorGap = 3.0f;
    static constexpr float kBodyPad = 2.0f;
    static constexpr float kUserPadV = 14.0f;
    static constexpr float kFoldBtnH = 26.0f;

    static float body_text_h(int lines) {
        if (lines < 1) lines = 1;
        return static_cast<float>(lines) * kLinePitch + 2.0f * kBodyPad;
    }

    // A markdown code-fence line: ``` or ```lang (trimmed of trailing space).
    // Real assistant messages wrap code in fences; without handling them the
    // literal ``` markers render as body text (a clear "not a product" tell).
    // We render the fence markers as ZERO-height (skipped) and the inner lines
    // in a monospace, sunken code style — this keeps every OTHER line's height
    // identical, so the render/measure mirror below stays in lock-step.
    static bool is_code_fence(const std::string& line) {
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        return line.compare(i, 3, "```") == 0;
    }
    // Language token after the opening fence (```ts -> "ts"), upper-cased for
    // the block's lang bar; empty if none.
    static std::string fence_lang(const std::string& line) {
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        i += 3;  // past ```
        std::string lang;
        while (i < line.size() && line[i] != ' ' && line[i] != '`')
            lang += line[i++];
        return fmtutil::to_upper(std::move(lang));  // shared ASCII flip (1c)
    }
    // Fenced-code-block geometry (a single atomic segment in the body scan):
    // a lang-bar header + one row per inner code line, wrapped in a rounded
    // sunken container with a hairline border + vertical margin, matching the
    // mock's `.block`. Heights are shared by render + measure so the
    // virtualization mirror stays exact.
    static constexpr float kCodeBarH = 20.0f;    // lang-bar header height
    static constexpr float kCodeVMargin = 8.0f;  // margin above + below block
    static constexpr float kCodePadV = 6.0f;     // top+bottom padding inside body
    // Total height of a code block with `nLines` inner lines.
    static float code_block_h(int nLines) {
        if (nLines < 1) nLines = 1;
        return kCodeVMargin + kCodeBarH +
               (static_cast<float>(nLines) * kLinePitch + 2.0f * kCodePadV) +
               kCodeVMargin;
    }

    // ---- Markdown pipe-tables -------------------------------------------
    // A GitHub-style table is a header row of "| a | b |", a separator row of
    // "|---|:--:|---|", then N body rows. We render it as a real grid (header
    // band + zebra-free bordered rows) instead of dumping the raw pipes as
    // text (Gabe: "lets render markdown as table here"). Detection + geometry
    // are shared by render + measure so the virtualization mirror stays exact.
    static constexpr float kTableRowH = 22.0f;    // one table row height
    static constexpr float kTableVMargin = 8.0f;  // margin above + below

    // Split a "| a | b | c |" row into trimmed cells (drops the leading/trailing
    // empty cells the outer pipes create).
    static std::vector<std::string> table_cells(const std::string& line) {
        std::vector<std::string> cells;
        std::string cur;
        // Skip a single leading pipe if present.
        size_t i = 0;
        // A table cell separator is a bare '|' (we don't handle escaped \| —
        // rare in agent output; kept simple + safe).
        for (; i < line.size(); ++i) {
            if (line[i] == '|') {
                cells.push_back(cur);
                cur.clear();
            } else {
                cur += line[i];
            }
        }
        cells.push_back(cur);
        // Trim whitespace on each cell; drop the empty first/last from outer |.
        for (auto& c : cells) {
            size_t a = c.find_first_not_of(" \t");
            size_t b = c.find_last_not_of(" \t");
            c = (a == std::string::npos) ? "" : c.substr(a, b - a + 1);
        }
        if (!cells.empty() && cells.front().empty()) cells.erase(cells.begin());
        if (!cells.empty() && cells.back().empty()) cells.pop_back();
        return cells;
    }
    // A separator row: every cell is only dashes/colons/spaces, and there is at
    // least one dash (so "|---|:--:|" matches but "| a |" does not).
    static bool is_table_separator(const std::string& line) {
        if (line.find('|') == std::string::npos) return false;
        bool sawDash = false;
        for (char c : line) {
            if (c == '-') sawDash = true;
            else if (c != '|' && c != ':' && c != ' ' && c != '\t')
                return false;
        }
        return sawDash;
    }
    // True if the line at `start` begins a table: a pipe row immediately
    // followed by a separator row. Returns the cell count via out-param.
    static bool is_table_start(const std::string& body, size_t start) {
        size_t nl = body.find('\n', start);
        if (nl == std::string::npos) return false;  // need a 2nd line
        std::string l1 = body.substr(start, nl - start);
        if (l1.find('|') == std::string::npos) return false;
        size_t nl2 = body.find('\n', nl + 1);
        std::string l2 = body.substr(nl + 1, (nl2 == std::string::npos)
                                                 ? std::string::npos
                                                 : nl2 - (nl + 1));
        return is_table_separator(l2);
    }
    // Scan a table starting at `start`; fill `rows` (each a cell vector; row 0
    // is the header, the separator row is skipped) and return the byte offset
    // just past the table. Shared by render + measure.
    static size_t scan_table(const std::string& body, size_t start,
                             std::vector<std::vector<std::string>>* rows) {
        size_t p = start;
        int lineIdx = 0;
        while (p <= body.size()) {
            size_t nl = body.find('\n', p);
            size_t end = (nl == std::string::npos) ? body.size() : nl;
            std::string line = body.substr(p, end - p);
            // A table ends at the first line with no pipe (or EOF).
            if (line.find('|') == std::string::npos) break;
            if (lineIdx == 1 && is_table_separator(line)) {
                // skip the separator row (it only defines alignment)
            } else if (rows) {
                rows->push_back(table_cells(line));
            }
            ++lineIdx;
            if (nl == std::string::npos) { p = body.size() + 1; break; }
            p = nl + 1;
        }
        return p;
    }
    // Height of a table with `nRows` rendered rows (header counts as a row).
    // +1px for the hairline divider drawn under the header (when there's a body).
    static float table_h(int nRows) {
        if (nRows < 1) nRows = 1;
        const float divider = (nRows > 1) ? 1.5f : 0.0f;
        return kTableVMargin + static_cast<float>(nRows) * kTableRowH +
               divider + kTableVMargin;
    }

    // Total pixel height of `render_rich_body(body, textW)` — MUST mirror that
    // method's per-segment layout exactly (blank line = half pitch, else
    // segLines*pitch) so virtualization spacers line up with what renders.
    static float rich_body_h(const std::string& body, float textW) {
        const int perLine = wrap_perline(textW);
        float h = 0.0f;
        size_t start = 0;
        while (start <= body.size()) {
            size_t nl = body.find('\n', start);
            size_t end = (nl == std::string::npos) ? body.size() : nl;
            std::string line = body.substr(start, end - start);
            // ---- Markdown table: ONE atomic segment (mirror render) --------
            if (is_table_start(body, start)) {
                std::vector<std::vector<std::string>> rows;
                size_t p = scan_table(body, start, &rows);
                h += table_h(static_cast<int>(rows.size()));
                start = p;
                continue;
            }
            if (is_code_fence(line)) {
                // A fenced block is ONE atomic segment: scan to its closing
                // fence, count inner lines, add the whole block's height. Both
                // render + measure do this identical scan so heights agree.
                int codeLines = 0;
                size_t p = (nl == std::string::npos) ? body.size() : nl + 1;
                while (p <= body.size()) {
                    size_t n2 = body.find('\n', p);
                    size_t e2 = (n2 == std::string::npos) ? body.size() : n2;
                    std::string cl = body.substr(p, e2 - p);
                    if (is_code_fence(cl)) {  // closing fence
                        p = (n2 == std::string::npos) ? body.size() : n2 + 1;
                        break;
                    }
                    ++codeLines;
                    if (n2 == std::string::npos) { p = body.size() + 1; break; }
                    p = n2 + 1;
                }
                h += code_block_h(codeLines);
                start = p;
                continue;
            }
            // VISIBLE length (markers removed) so inline **bold**/`code`/_em_
            // don't inflate the wrapped line count — must match render, which
            // wraps the same visible text (md_to_spans).
            int len = static_cast<int>(md_visible(line).size());
            if (len <= 0) {
                h += kBlankPitch;
            } else {
                int segLines = (len + perLine - 1) / perLine;
                if (segLines < 1) segLines = 1;
                h += static_cast<float>(segLines) * kLinePitch;
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return h;
    }

    // Renderer-accurate height for a SINGLE wrapped box (user bubble / tool):
    // afterhours wraps on spaces only and treats "\n" as a char, so a single
    // box's rendered line count ignores newlines. Approximate that.
    static float flat_body_h(const std::string& body, float textW) {
        const int perLine = wrap_perline(textW);
        int len = static_cast<int>(body.size());
        int lines = (len + perLine - 1) / perLine;
        if (lines < 1) lines = 1;
        return static_cast<float>(lines) * kLinePitch + 2.0f * kBodyPad;
    }

    // Memoized display body + measured height for a message, keyed by
    // (id, wrap width). Recomputed only on a miss / width change — a static
    // transcript is measured ONCE, not every frame (the core perf fix).
    // `rich` selects the assistant per-line layout vs the flat single-box one.
    const ecs::model::MsgRender& measured(const api::Message& m, float textW,
                                          bool isLive, int index,
                                          AppComponent::StreamPhase phase,
                                          bool rich) {
        const std::string key =
            (m.id.empty() ? ("i" + std::to_string(index)) : m.id) +
            (rich ? "|r" : "|f");
        if (!isLive) {
            if (const auto* hit = render_cache().get(key, textW)) return *hit;
        }
        ecs::model::MsgRender r;
        // Rich (assistant) path KEEPS inline markers so render_rich_body can
        // color them as spans (gap #22); the flat (user) path strips them since
        // it renders one plain label. Both go through normalize_md_lines
        // (bullets / rules) via strip_inline_md.
        r.body = strip_inline_md(redact_secrets(m.text));
        if (!rich) r.body = strip_inline_markers(r.body);
        if (isLive) {
            if (r.body.empty() ||
                phase == AppComponent::StreamPhase::Thinking)
                r.body = "thinking\xe2\x80\xa6";
            else
                r.body += " \xe2\x96\x8b";
        }
        r.line_count = count_lines(r.body, textW);  // logical lines (for fold)
        r.wrap_w = textW;
        r.height = rich ? rich_body_h(r.body, textW)
                        : flat_body_h(r.body, textW);
        if (isLive) {
            static ecs::model::MsgRender liveSlot;
            liveSlot = std::move(r);
            return liveSlot;
        }
        return render_cache().put(key, std::move(r));
    }

    bool is_folded(const api::Message& m, int index, int lineCount,
                   bool isLive) {
        if (isLive || lineCount <= kFoldLines) return false;
        AppComponent* app = app_singleton();
        const std::string mkey =
            m.id.empty() ? ("msg" + std::to_string(index)) : m.id;
        return !(app && app->expandedMsgs.count(mkey) != 0);
    }

    // ---- Item height functions (mirror the render layout exactly) ----------
    float bubble_height(const api::Message& m, float paneWidth, bool isLive,
                        int index, bool showAuthor = true) {
        if (m.role == api::Role::System) return 22.0f + 16.0f;
        const bool isUser = (m.role == api::Role::User);
        if (isUser) {
            float bubbleW = paneWidth * 0.82f;
            if (bubbleW > 520.0f) bubbleW = 520.0f;
            const auto& mr = measured(m, bubbleW - 28.0f, isLive, index,
                                      AppComponent::StreamPhase::Idle,
                                      /*rich=*/false);
            // +12px for the sync-glyph child under the body when sync!=None
            // (a real ✓/✓✓ corner mark, gap #28 now fixed); server-loaded
            // messages (sync==None) add nothing. Mirrors render_bubble.
            const float syncH = (m.sync != api::SyncState::None) ? 12.0f : 0.0f;
            return kTurnGapTop + 10.0f + mr.height + syncH + kUserPadV +
                   kTurnGapBot;
        }
        float textW = paneWidth - 34.0f;
        const auto& mr = measured(m, textW, isLive, index,
                                  AppComponent::StreamPhase::Idle,
                                  /*rich=*/true);
        bool folded = is_folded(m, index, mr.line_count, isLive);
        float bodyH = folded
                          ? rich_body_h(first_n_lines(mr.body, textW, kFoldLines),
                                        textW)
                          : mr.height;
        float h = kTurnGapTop + 8.0f +
                  (showAuthor ? (kAuthorH + kAuthorGap) : 0.0f) + bodyH +
                  kTurnGapBot;
        AppComponent* app = app_singleton();
        const std::string mkey =
            m.id.empty() ? ("msg" + std::to_string(index)) : m.id;
        bool expanded = app && app->expandedMsgs.count(mkey) != 0;
        if (!isLive && (folded || (expanded && mr.line_count > kFoldLines)))
            h += kFoldBtnH;
        // Inline image term (mirrors render_bubble's asst_image element:
        // margin.top 8 + fitted image height + margin.bottom 4).
        if (!m.image_path.empty() &&
            hanabi::inline_image::available(m.image_path))
            h += 8.0f + hanabi::inline_image::fitted_height(m.image_path, textW) +
                 4.0f;
        return h;
    }

    // Max bubble content width — caps the reading column so a conversational
    // message doesn't run edge-to-edge across the wide pane (v3 #8). ~620px is
    // roughly 70 characters at this font, the comfortable-reading target.
    static constexpr float kBubbleCap = 620.0f;


    // Render an assistant body as one wrapped text box PER newline-delimited
    // segment. afterhours' word-wrap splits only on spaces and treats "\n" as a
    // regular character (gap #22), so a single box collapses a numbered/bulleted
    // list into a run-on paragraph AND leaves a tall empty gap (the box is sized
    // for N logical lines but the renderer draws far fewer). Splitting on "\n"
    // and giving each segment its own box restores real list breaks + makes the
    // measured height match the render exactly (no gap). A blank segment becomes
    // a small vertical gap (paragraph spacing). Bounded by virtualization — only
    // the visible turns build these per-line boxes.
    // Renders the assistant body one div per newline-segment. When a visible
    // window is supplied (winTop..winBot, in the SAME y-space as bodyStartY —
    // i.e. content-column coordinates), segments fully outside the window are
    // NOT built; instead their height is accumulated into a single spacer div.
    // This is INTRA-message virtualization: a 260-line message off the top of
    // the viewport builds ~0 text entities, not 260. winBot<=winTop disables
    // culling (build everything). The spacer keeps total height exact so the
    // scrollbar and outer virtualization spacer math stay correct.

    // A fenced code block as ONE rounded sunken container (matches the mock's
    // `.block`): a lang-bar header (uppercase language, left) + one mono row per
    // code line, hairline border + vertical margin. Height == code_block_h().
    // Render a markdown table as a real bordered grid: a raised header band +
    // body rows, columns evenly split. `rows[0]` is the header. Height mirrors
    // table_h() so virtualization stays exact.
    void render_table(UIContext<InputAction>& ctx, Entity& parent, int id,
                      const std::vector<std::vector<std::string>>& rows,
                      float textW) {
        if (rows.empty()) return;
        // Column count = the widest row (ragged rows are padded with blanks).
        size_t nCols = 0;
        for (const auto& r : rows) nCols = std::max(nCols, r.size());
        if (nCols == 0) nCols = 1;
        const int nRows = static_cast<int>(rows.size());
        const float gridW = textW;
        const float colW = gridW / static_cast<float>(nCols);

        auto grid = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(gridW),
                                         pixels(table_h(nRows) -
                                                2.0f * kTableVMargin)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(kTableVMargin),
                                    .bottom = pixels(kTableVMargin)})
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.18f)
                .with_debug_name("md_table"));

        for (int ri = 0; ri < nRows; ++ri) {
            const bool header = (ri == 0);
            auto rowDiv = div(ctx, mk(grid.ent(), 1 + ri * 2),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), pixels(kTableRowH)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    // Header band = raised surface; body rows = pane bg. NO
                    // per-row border (stacking 4-sided borders inside the grid's
                    // own border produced the doubled/overhanging lines Gabe
                    // flagged). Structure comes from the outer grid border, a
                    // divider under the header, and thin between-row dividers.
                    .with_custom_background(header ? theme::panel_bg_2()
                                                   : theme::panel_bg())
                    // Thin top divider on body rows after the first, so rows are
                    // legible on light (where the header-fill delta alone is too
                    // subtle to separate rows). Header's own divider is below.
                    .with_border_top(theme::row_separator(),
                                     pixels((ri >= 2) ? 1.0f : 0.0f))
                    .with_roundness(0.0f)
                    .with_debug_name("md_table_row"));
            for (size_t ci = 0; ci < nCols; ++ci) {
                std::string cell =
                    ci < rows[ri].size() ? rows[ri][ci] : std::string();
                // Ellipsize to the column width (~6px/char at ROW size).
                size_t budget = static_cast<size_t>((colW - 16.0f) / 6.1f);
                if (budget < 3) budget = 3;
                div(ctx, mk(rowDiv.ent(), 1 + static_cast<int>(ci)),
                    ComponentConfig{}
                        .with_label(fmtutil::ellipsize(cell, budget))
                        .with_size(ComponentSize{pixels(colW),
                                                 pixels(kTableRowH)})
                        .with_transparent_bg()
                        .with_custom_text_color(header ? theme::text_primary()
                                                       : theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_padding(Padding{.right = pixels(8),
                                              .left = pixels(8)})
                        .with_roundness(0.0f)
                        .with_debug_name("md_table_cell"));
            }
            // Single divider UNDER the header row (separates header from body).
            // row_separator reads on BOTH themes (border() was near-invisible on
            // the light pane); 1.5px so the header band is clearly delimited.
            if (header && nRows > 1) {
                div(ctx, mk(grid.ent(), 2 + ri * 2),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f), pixels(1.5f)})
                        .with_custom_background(theme::border())
                        .with_roundness(0.0f)
                        .with_debug_name("md_table_hdr_divider"));
            }
        }
    }

    void render_code_block(UIContext<InputAction>& ctx, Entity& parent, int id,
                           const std::string& lang,
                           const std::vector<std::string>& lines,
                           float blockW = 0.0f) {
        const int n = lines.empty() ? 1 : static_cast<int>(lines.size());
        auto block = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(code_block_h(n) -
                                                2.0f * kCodeVMargin)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(kCodeVMargin),
                                    .bottom = pixels(kCodeVMargin)})
                .with_custom_background(theme::window_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.22f)
                .with_debug_name("code_block"));
        // Lang bar: uppercase language label on a slightly-raised strip with a
        // hairline bottom divider.
        auto bar = div(ctx, mk(block.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kCodeBarH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.right = pixels(10), .left = pixels(12)})
                .with_custom_background(theme::panel_bg())
                .with_border_bottom(theme::border(), pixels(1))
                .with_roundness(0.0f)
                .with_debug_name("code_block_bar"));
        div(ctx, mk(bar.ent(), 1),
            ComponentConfig{}
                .with_label(lang.empty() ? "CODE" : lang)
                .with_size(ComponentSize{pixels(120), pixels(14)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::MICRO)
                .with_letter_spacing(0.8f)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("code_block_lang"));
        // Copy button (right of the lang label): copies the whole block to the
        // clipboard (mock's `.copy`). A spacer pushes it flush-right (no
        // flex-grow — gap #18 — so size the lang label fixed + a flexer).
        {
            // Compute the spacer width EXPLICITLY (afterhours has no flex-grow —
            // gap #18 — so a percent(1.0) spacer in a NoWrap row resolves to the
            // FULL parent width and overflows, flooding the log). Bar content
            // box = blockW - (left 12 + right 10) pad; minus the 120px lang
            // label and the copyW button = the spacer that pins Copy flush-right.
            const float copyW = 42.0f;
            const float barContent = (blockW > 0.0f ? blockW : 698.0f) - 22.0f;
            float spacerW = barContent - 120.0f - copyW;
            if (spacerW < 0.0f) spacerW = 0.0f;
            div(ctx, mk(bar.ent(), 2),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(spacerW), pixels(14)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("code_bar_spacer"));
            auto copy = button(ctx, mk(bar.ent(), 3),
                ComponentConfig{}
                    .with_label("Copy")
                    .with_size(ComponentSize{pixels(copyW), pixels(15)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name("code_block_copy"));
            if (copy) {
                std::string joined;
                for (size_t k = 0; k < lines.size(); ++k) {
                    joined += lines[k];
                    if (k + 1 < lines.size()) joined += "\n";
                }
                afterhours::clipboard::set_text(joined);
            }
        }
        // Code body: mono rows, no wrap (pre-formatted).
        auto body = div(ctx, mk(block.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(kCodePadV),
                                      .right = pixels(10),
                                      .bottom = pixels(kCodePadV),
                                      .left = pixels(12)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("code_block_body"));
        int li = 0;
        for (const auto& cl : lines) {
            div(ctx, mk(body.ent(), 1 + li),
                ComponentConfig{}
                    .with_label(cl.empty() ? " " : cl)
                    .with_size(ComponentSize{percent(1.0f), pixels(kLinePitch)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font("mono", theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("code_block_line"));
            ++li;
        }
    }

    // "In progress / thinking" indicator: a pulsing accent dot + a muted label
    // + an elapsed timer, shown on a live assistant turn while it's THINKING
    // (before any tokens stream). Matches the reference Gabe shared (a soft
    // glowing dot beside "Laying the groundwork · 32s"). The status word is
    // "Thinking…" (a backend free-text step label like "Laying the groundwork"
    // needs a status SSE field we don't parse yet — logged as an API ask); the
    // dot pulse + elapsed timer are fully client-side.
    void render_thinking_indicator(UIContext<InputAction>& ctx, Entity& parent,
                                   AppComponent* app) {
        // Elapsed seconds since the turn began.
        long elapsed = 0;
        if (app && app->streamStartedAt > 0) {
            long now = static_cast<long>(std::time(nullptr));
            elapsed = now - app->streamStartedAt;
            if (elapsed < 0) elapsed = 0;
        }
        std::string timer = std::to_string(elapsed) + "s";

        auto row = div(ctx, mk(parent, 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(24.0f)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("thinking_row"));

        // Pulsing dot: a filled accent circle whose radius eases up/down with a
        // sine of wall-clock time, plus a fainter halo — a soft "breathing"
        // glow. Drawn in a fixed slot so the label sits at a stable x.
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(18.0f), pixels(18.0f)})
                .with_transparent_bg()
                .with_margin(Margin{.right = pixels(8)})
                .with_on_draw_fg([](RectangleType r) {
                    const float cx = r.x + r.width * 0.5f;
                    const float cy = r.y + r.height * 0.5f;
                    const double t = afterhours::graphics::get_time();
                    // 0..1 breathing factor (~1.4s period).
                    const float p = 0.5f + 0.5f * static_cast<float>(
                                                std::sin(t * 4.5));
                    theme::Color accent = theme::accent();
                    // Faint halo (bigger, low-ish intensity) — pre-blended over
                    // the pane bg since fills can't alpha-blend (gap #13).
                    theme::Color halo =
                        theme::over(theme::Color{accent.r, accent.g, accent.b,
                                                 static_cast<unsigned char>(
                                                     40 + 40 * p)},
                                    theme::panel_bg());
                    afterhours::draw_circle_v({cx, cy}, 5.0f + 2.0f * p, halo);
                    // Core dot.
                    afterhours::draw_circle_v({cx, cy}, 3.4f, accent);
                })
                .with_debug_name("thinking_dot"));

        // Label — muted, reads as "working". (Kept as one label; afterhours has
        // no italic variant here — the muted color + the pulsing dot carry the
        // "in progress" feel.)
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label("Thinking\xe2\x80\xa6")
                .with_size(ComponentSize{children(), pixels(18.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_margin(Margin{.right = pixels(8)})
                .with_roundness(0.0f)
                .with_debug_name("thinking_label"));

        // Elapsed timer — fainter, trailing.
        div(ctx, mk(row.ent(), 3),
            ComponentConfig{}
                .with_label(timer)
                .with_size(ComponentSize{children(), pixels(18.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("thinking_timer"));
    }

    void render_rich_body(UIContext<InputAction>& ctx, Entity& parent,
                          const std::string& shown, float textW,
                          float winTop = 0.0f, float winBot = -1.0f,
                          float bodyStartY = 0.0f) {
        const bool cull = winBot > winTop;
        const int perLine = wrap_perline(textW);
        size_t start = 0;
        int seg = 0;
        float y = bodyStartY;         // running content-y of this segment's top
        float pending = 0.0f;         // accumulated off-window height to flush
        auto flush = [&](int tag) {
            if (pending <= 0.0f) return;
            div(ctx, mk(parent, 100 + tag),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), pixels(pending)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("asst_body_spacer"));
            pending = 0.0f;
        };
        while (start <= shown.size()) {
            size_t nl = shown.find('\n', start);
            size_t end = (nl == std::string::npos) ? shown.size() : nl;
            std::string line = shown.substr(start, end - start);

            // ---- Markdown table: ONE atomic segment (grid render) ----------
            if (is_table_start(shown, start)) {
                std::vector<std::vector<std::string>> rows;
                size_t p = scan_table(shown, start, &rows);
                const float blockH = table_h(static_cast<int>(rows.size()));
                const float segTop = y;
                const float segBot = y + blockH;
                y = segBot;
                const bool visible =
                    !cull || (segBot >= winTop && segTop <= winBot);
                if (!visible) {
                    pending += blockH;
                } else {
                    flush(9000 + seg);
                    render_table(ctx, parent, 100 + seg, rows, textW);
                }
                ++seg;
                start = p;
                continue;
            }

            // ---- Fenced code block: ONE atomic segment (container) ----------
            if (is_code_fence(line)) {
                const std::string lang = fence_lang(line);
                std::vector<std::string> codeLines;
                size_t p = (nl == std::string::npos) ? shown.size() : nl + 1;
                while (p <= shown.size()) {
                    size_t n2 = shown.find('\n', p);
                    size_t e2 = (n2 == std::string::npos) ? shown.size() : n2;
                    std::string cl = shown.substr(p, e2 - p);
                    if (is_code_fence(cl)) {  // closing fence
                        p = (n2 == std::string::npos) ? shown.size() : n2 + 1;
                        break;
                    }
                    // Tabs -> 2 spaces for stable columns (pre-formatted).
                    for (size_t t = cl.find('\t'); t != std::string::npos;
                         t = cl.find('\t', t))
                        cl.replace(t, 1, "  ");
                    codeLines.push_back(std::move(cl));
                    if (n2 == std::string::npos) { p = shown.size() + 1; break; }
                    p = n2 + 1;
                }
                const float blockH =
                    code_block_h(static_cast<int>(codeLines.size()));
                const float segTop = y;
                const float segBot = y + blockH;
                y = segBot;
                const bool visible =
                    !cull || (segBot >= winTop && segTop <= winBot);
                if (!visible) {
                    pending += blockH;
                } else {
                    flush(9000 + seg);
                    render_code_block(ctx, parent, 100 + seg, lang, codeLines,
                                      textW);
                }
                ++seg;
                start = p;
                continue;
            }

            float segH;
            bool blank = line.empty();
            int segLines = 1;
            // Parse inline markdown -> colored spans once; the VISIBLE text
            // drives wrap/height (identical to the measure path), the spans
            // drive the styled draw below.
            InlineParse ip = md_to_spans(line);
            if (blank) {
                segH = kBlankPitch;
            } else {
                segLines = (static_cast<int>(ip.visible.size()) + perLine - 1) /
                           perLine;
                if (segLines < 1) segLines = 1;
                segH = static_cast<float>(segLines) * kLinePitch;
            }
            const float segTop = y;
            const float segBot = y + segH;
            y = segBot;
            const bool visible =
                !cull || (segBot >= winTop && segTop <= winBot);
            if (!visible) {
                pending += segH;      // collapse off-window segment into spacer
            } else if (blank) {
                flush(9000 + seg);
                div(ctx, mk(parent, 100 + seg),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f),
                                                 pixels(segH)})
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("asst_gap"));
            } else {
                flush(9000 + seg);
                div(ctx, mk(parent, 100 + seg),
                    ComponentConfig{}
                        .with_label(ip.visible)
                        .with_styled_label(ip.spans)
                        .with_size(ComponentSize{percent(1.0f), pixels(segH)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_primary())
                        .with_font_size(theme::type::BODY)
                        .with_text_overflow(TextOverflow::Wrap)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_debug_name("asst_line"));
            }
            ++seg;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        flush(8888);
    }

    // A conversational message (User / Assistant). Assistant = full-column
    // doc-feed turn (no bubble); User = compact right-aligned MUTED-GREY bubble.
    // Heights match bubble_height() exactly so virtualization spacers line up.
    void render_bubble(UIContext<InputAction>& ctx, Entity& parent, int index,
                       const api::Message& m, float paneWidth,
                       bool isLive = false,
                       AppComponent::StreamPhase streamPhase =
                           AppComponent::StreamPhase::Idle,
                       float winTop = 0.0f, float winBot = -1.0f,
                       float itemTopY = 0.0f, bool showAuthor = true) {
        if (m.role == api::Role::System) {
            render_meta_line(ctx, parent, index, m);
            return;
        }
        if (m.role == api::Role::Tool) {
            render_tool_block(ctx, parent, index, m, paneWidth);
            return;
        }

        const bool isUser = (m.role == api::Role::User);

        // ---- USER: compact right-aligned muted-grey bubble ----------------
        if (isUser) {
            float bubbleW = paneWidth * 0.82f;
            if (bubbleW > 520.0f) bubbleW = 520.0f;
            const auto& mr = measured(m, bubbleW - 28.0f, isLive, index,
                                      streamPhase, /*rich=*/false);
            float bodyH = mr.height;
            // Local-first sync suffix (WhatsApp-style), appended to the body on
            // its own trailing line — a nested 2nd child of the user bubble does
            // NOT render in this afterhours build (verified), and the body-text
            // path is the reliable one. LocalOnly ✓ / Persisting ⋯ / Synced ✓✓ /
            // Failed ⚠. Server-loaded messages (sync==None) get no suffix.
            // Local-first sync suffix (font-safe, inline): a nested 2nd child
            // of the user bubble does NOT render in this afterhours build
            // (verified) and '\n' in a label renders as a space, so we append a
            // short faint status word to the body. This is the local-vs-synced
            // signal Gabe asked for (WhatsApp-style, adapted to what renders):
            //   LocalOnly  "· saved locally"  — on this device only
            //   Persisting "· sending…"       — in flight to the server
            //   Synced     "· sent"           — confirmed on the server
            //   Failed     "· not sent"       — held locally, will retry
            std::string userBody = mr.body;
            bool hasSync = (m.sync != api::SyncState::None);
            // The sync state is shown as a real ✓/✓✓ glyph in the bubble's
            // corner (a nested on_draw_fg child, now that gap #28 is fixed) —
            // NOT appended to the body text. See the sync_check child below.
            auto row = div(ctx, mk(parent, 200 + index * 10),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_justify_content(JustifyContent::FlexEnd)
                    .with_margin(Margin{.top = pixels(kTurnGapTop + 10.0f),
                                        .right = pixels(0),
                                        .bottom = pixels(kTurnGapBot),
                                        .left = pixels(0)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("user_row"));
            auto bub = div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(bubbleW), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_custom_background(bubble_bg(m.role))
                    .with_padding(Padding{.top = pixels(8), .right = pixels(14),
                                          .bottom = pixels(9),
                                          .left = pixels(14)})
                    // Shared chat corner: derive roundness from the bubble's own
                    // size so its pixel corner MATCHES the tool-call card (both
                    // target theme::kChatCorner). afterhours radius =
                    // min(w,h)*0.5*roundness, so a fixed roundness gave the
                    // bubble and the (shorter) tool row different corners.
                    .with_roundness(theme::roundness_for_px(
                        theme::kChatCorner, bubbleW, bodyH + 17.0f))
                    .with_debug_name("user_bubble"));
            div(ctx, mk(bub.ent(), 2),
                ComponentConfig{}
                    .with_label(userBody)
                    .with_size(ComponentSize{percent(1.0f), pixels(bodyH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::BODY)
                    .with_text_overflow(TextOverflow::Wrap)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("user_text"));
            // WhatsApp-style sync glyph in the bubble's bottom-right corner.
            // gap #28 (nested child of a custom-bg bubble + on_draw_fg didn't
            // fire) is now FIXED upstream (afterhours bump), so we render the
            // real ✓/✓✓ glyph via draw_sync_check instead of the text suffix.
            if (hasSync) {
                const api::SyncState st = m.sync;
                div(ctx, mk(bub.ent(), 3),
                    ComponentConfig{}
                        .with_label(" ")
                        .with_size(ComponentSize{percent(1.0f), pixels(12)})
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_on_draw_fg([st](RectangleType r) {
                            draw_sync_check(st, r.x + r.width - 4.0f,
                                            r.y + r.height * 0.5f);
                        })
                        .with_debug_name("sync_check"));
            }
            (void)hasSync;
            return;
        }

        // ---- ASSISTANT: full-column doc-feed turn (no bubble) -------------
        float textW = paneWidth - 34.0f;
        const auto& mr = measured(m, textW, isLive, index, streamPhase,
                                  /*rich=*/true);
        const int lineCount = mr.line_count;
        AppComponent* app = app_singleton();
        const std::string mkey =
            m.id.empty() ? ("msg" + std::to_string(index)) : m.id;
        const bool expanded = app && app->expandedMsgs.count(mkey) != 0;
        const bool folded = !isLive && lineCount > kFoldLines && !expanded;
        const std::string shown =
            folded ? first_n_lines(mr.body, textW, kFoldLines) : mr.body;

        auto turn = div(ctx, mk(parent, 200 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(kTurnGapTop + 8.0f),
                                    .right = pixels(0),
                                    .bottom = pixels(kTurnGapBot),
                                    .left = pixels(0)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("asst_turn"));

        // Author row: colored name bound TIGHT above its body, subtle
        // right-aligned timestamp on the same row. Shown only on the FIRST
        // assistant message of a turn (V2 grouping) — continuation fragments
        // suppress the repeated name. The timestamp rides on the author row, so
        // when suppressed the fragment is just its body (tight continuation).
        if (showAuthor) {
        // Chat redesign #2: DROP the per-turn green "hanabi" author label — it
        // was the strongest "log viewer" tell (both design critics flagged it).
        // Modern chat (ChatGPT/Claude/Gemini) makes the assistant plain
        // left-aligned document text; the right-aligned user bubble is the only
        // role marker needed. We keep only a faint, right-aligned timestamp on
        // the first message of a turn so exchanges still have a time anchor
        // (and, if present, the run subtitle) — no colored name.
        std::string ts = isLive ? std::string("streaming\xe2\x80\xa6")
                                : fmtutil::relative_time(m.created_at);
        if (!m.subtitle.empty())
            ts = m.subtitle + (ts.empty() ? "" : ("  \xc2\xb7  " + ts));
        auto arow = div(ctx, mk(turn.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kAuthorH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_margin(Margin{.bottom = pixels(kAuthorGap)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("asst_arow"));
        if (!ts.empty()) {
            div(ctx, mk(arow.ent(), 2),
                ComponentConfig{}
                    .with_label(ts)
                    .with_size(ComponentSize{children(), pixels(kAuthorH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("asst_ts"));
        }
        }  // showAuthor
        // Body starts below the turn's top margin + author row (when shown):
        // cull the body's off-screen line-segments (intra-message
        // virtualization). Must mirror bubble_height's author-row term exactly.
        const float bodyStartY =
            itemTopY + (kTurnGapTop + 8.0f) +
            (showAuthor ? (kAuthorH + kAuthorGap) : 0.0f);
        // THINKING INDICATOR (Gabe: "we are missing these 'in progress, I'm
        // thinking' UI"): while the live turn is still THINKING (no visible
        // tokens yet), show a pulsing accent dot + an italic "Thinking…" label
        // + an elapsed timer, instead of the plain "thinking…" body text. Once
        // real tokens stream in (phase Streaming), fall through to the normal
        // document render so the text takes over.
        if (isLive && streamPhase == AppComponent::StreamPhase::Thinking) {
            render_thinking_indicator(ctx, turn.ent(), app);
        } else {
            render_rich_body(ctx, turn.ent(), shown, textW, winTop, winBot,
                             bodyStartY);
        }

        // Inline image (agent surface): if the message carries a decodable
        // local image (e.g. a screenshot the agent produced), render it under
        // the text at column width. A dedicated transparent element (its
        // on_draw_fg fires — unlike a custom-bg div, gap #28) draws the cached
        // texture. Height mirrors bubble_height's image term exactly.
        if (!m.image_path.empty() &&
            hanabi::inline_image::available(m.image_path)) {
            const std::string ip = m.image_path;
            const float imgH =
                hanabi::inline_image::fitted_height(ip, textW);
            div(ctx, mk(turn.ent(), 7),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(textW), pixels(imgH)})
                    .with_margin(Margin{.top = pixels(8), .bottom = pixels(4)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_on_draw_fg([ip, textW, imgH](RectangleType r) {
                        hanabi::inline_image::draw(ip, r.x, r.y, textW, imgH);
                    })
                    .with_debug_name("asst_image"));
        }

        if (app && !isLive &&
            (folded || (expanded && lineCount > kFoldLines))) {
            const int hidden = lineCount - kFoldLines;
            std::string flabel = expanded
                                     ? "Show less"
                                     : ("Show " + std::to_string(hidden) +
                                        " more lines");
            auto fbtn = div(ctx, mk(turn.ent(), 3),
                ComponentConfig{}
                    .with_label(flabel)
                    .with_size(ComponentSize{children(), pixels(22)})
                    .with_margin(Margin{.top = pixels(4)})
                    .with_padding(Padding{.top = pixels(2), .right = pixels(11),
                                          .bottom = pixels(2),
                                          .left = pixels(11)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_roundness(0.35f)
                    .with_debug_name("asst_fold_btn"));
            fbtn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            if (fbtn.ent().get<afterhours::ui::HasClickListener>().down) {
                if (expanded) app->expandedMsgs.erase(mkey);
                else app->expandedMsgs.insert(mkey);
            }
        }
    }

    // A System message: a quiet, centered, muted caption — conversation
    // metadata (a session boundary / mode note), NOT a dialogue bubble.
    void render_meta_line(UIContext<InputAction>& ctx, Entity& parent,
                          int index, const api::Message& m) {
        std::string txt = m.text;
        std::string age = fmtutil::relative_time(m.created_at);
        if (!age.empty() && !txt.empty()) txt += "   \xc2\xb7   " + age;
        div(ctx, mk(parent, 200 + index * 10),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(txt, 120))
                .with_size(ComponentSize{percent(1.0f), pixels(22)})
                .with_margin(Margin{.top = pixels(8), .right = pixels(0),
                                    .bottom = pixels(8), .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("meta_line"));
    }
    // ---- Tool-row metadata derivation ------------------------------------
    // api::Message now carries the real tool fields the backend supplies
    // (tool_node / tool_status / tool_duration_ms / tool_result), so the
    // renderer reads those directly — no fabricated per-message values. Counts
    // come from the real pile size (one Tool message == one call).
    static std::string tool_duration(const api::Message& m) {
        // Prefer the REAL duration parsed from the backend (completedAt-startedAt).
        if (m.tool_duration_ms > 0) {
            long long s = m.tool_duration_ms / 1000;
            if (s < 1) return std::to_string(m.tool_duration_ms) + "ms";
            if (s < 60) return std::to_string(s) + "s";
            long long mn = s / 60;
            long long rs = s % 60;
            return rs ? (std::to_string(mn) + "m" + std::to_string(rs) + "s")
                      : (std::to_string(mn) + "m");
        }
        return "";  // unknown duration -> show nothing (no fake number)
    }
    static std::string tool_node(const api::Message& m) {
        // Prefer the REAL node the backend supplied (tool input -> tool_node).
        if (!m.tool_node.empty()) return m.tool_node;
        // Back-compat: some paths embed it as a "[node] cmd" text prefix.
        if (m.text.size() > 2 && m.text.front() == '[') {
            size_t close = m.text.find(']');
            if (close != std::string::npos && close > 1)
                return m.text.substr(1, close - 1);
        }
        return "";  // unknown node -> show nothing (no fabricated cli:NNNNNN)
    }
    static std::string tool_command(const api::Message& m) {
        std::string t = redact_secrets(m.text);
        size_t nl = t.find('\n');
        if (nl != std::string::npos) t = t.substr(0, nl);
        // Never render a blank tool row: fall back to the tool name (subtitle),
        // then a generic label, so real tool calls always show WHAT ran.
        if (t.empty()) t = !m.subtitle.empty() ? m.subtitle : std::string("tool call");
        return t;
    }
    // Real tool status -> check color. "completed"/"" (assume ok) => ready green;
    // "failed"/"error" => blocked red. Drives the row's trailing check mark.
    static bool tool_failed(const api::Message& m) {
        return m.tool_status == "failed" || m.tool_status == "error";
    }

    static void draw_wrench(RectangleType r, theme::Color c) {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        afterhours::draw_line_ex(afterhours::vec2{cx - 3.2f, cy + 3.2f},
                                 afterhours::vec2{cx + 1.6f, cy - 1.6f}, 1.6f,
                                 c);
        afterhours::draw_ring(cx + 2.6f, cy - 2.6f, 1.4f, 2.8f, 16, c);
    }
    static void draw_terminal(RectangleType r, theme::Color c) {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        afterhours::draw_line_ex(afterhours::vec2{cx - 3.0f, cy - 1.5f},
                                 afterhours::vec2{cx - 1.0f, cy}, 1.3f, c);
        afterhours::draw_line_ex(afterhours::vec2{cx - 1.0f, cy},
                                 afterhours::vec2{cx - 3.0f, cy + 1.5f}, 1.3f,
                                 c);
        afterhours::draw_line_ex(afterhours::vec2{cx + 0.5f, cy + 1.8f},
                                 afterhours::vec2{cx + 3.2f, cy + 1.8f}, 1.3f,
                                 c);
    }
    static void draw_check(RectangleType r, theme::Color c) {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        afterhours::draw_line_ex(afterhours::vec2{cx - 3.0f, cy + 0.2f},
                                 afterhours::vec2{cx - 0.8f, cy + 2.6f}, 1.7f,
                                 c);
        afterhours::draw_line_ex(afterhours::vec2{cx - 0.8f, cy + 2.6f},
                                 afterhours::vec2{cx + 3.4f, cy - 2.8f}, 1.7f,
                                 c);
    }

    // A single checkmark centered on a point (cx,cy), tinted `c`.
    static void draw_check_at(float cx, float cy, theme::Color c) {
        afterhours::draw_line_ex({cx - 3.0f, cy + 0.2f}, {cx - 0.8f, cy + 2.6f},
                                 1.5f, c);
        afterhours::draw_line_ex({cx - 0.8f, cy + 2.6f}, {cx + 3.4f, cy - 2.8f},
                                 1.5f, c);
    }

    // Local-first sync badge (WhatsApp-style), drawn RIGHT-anchored at (rx,cy)
    // — rx is the RIGHT edge of the glyph. Gray single check = LocalOnly, gray
    // clock-ish dot = Persisting, accent DOUBLE check = Synced, amber check +
    // dot = Failed.
    static void draw_sync_check(api::SyncState st, float rx, float cy) {
        switch (st) {
            case api::SyncState::LocalOnly:
                // single gray check
                draw_check_at(rx - 4.0f, cy, theme::text_faint());
                break;
            case api::SyncState::Persisting: {
                // small gray filled dot (in-flight to the server)
                afterhours::draw_circle_v({rx - 4.0f, cy}, 2.6f,
                                          theme::text_faint());
                break;
            }
            case api::SyncState::Synced: {
                // double accent check (two overlapping checks)
                const theme::Color a = theme::status_active();
                draw_check_at(rx - 4.0f, cy, a);
                draw_check_at(rx - 8.5f, cy, a);
                break;
            }
            case api::SyncState::Failed: {
                // amber check + a small dot (held, will retry)
                const theme::Color amber = theme::tag_ready_fg();
                (void)amber;
                const theme::Color warn = theme::status_blocked();
                draw_check_at(rx - 4.0f, cy, warn);
                afterhours::draw_circle_v({rx - 11.0f, cy}, 1.4f, warn);
                break;
            }
            default:  // includes SyncState::None
                break;
        }
    }

    // ---- Tool row heights (mirror render exactly for virtualization) ------
    static constexpr float kToolRowH = 28.0f;
    static constexpr float kToolRowGap = 4.0f;
    static constexpr float kSubRowH = 22.0f;

    // A single tool block is expandable IF it has captured output (tool_result)
    // to show. Keyed in expandedPiles by the message id (shared with piles).
    static bool tool_block_expandable(const api::Message& m) {
        return !m.tool_result.empty();
    }
    // Max output lines shown when a single tool block is expanded (keeps a huge
    // dump from dominating the pane; the row stays a peek, not the full log).
    static constexpr int kToolOutLines = 8;
    // Height of the expanded output panel for a single tool block (0 if not
    // expanded / no output). Mirrors render_tool_block's expanded panel exactly.
    float tool_out_height(AppComponent& app, const api::Message& m) {
        if (!tool_block_expandable(m)) return 0.0f;
        AppComponent* a = &app;
        const std::string key = m.id.empty() ? "" : m.id;
        const bool open = a && !key.empty() && a->expandedPiles.count(key) != 0;
        if (!open) return 0.0f;
        // Count newline-split lines in the result, capped.
        int lines = 1;
        for (char c : m.tool_result)
            if (c == '\n') ++lines;
        if (lines > kToolOutLines) lines = kToolOutLines;
        return static_cast<float>(lines) * kLinePitch + 12.0f;  // + panel pad
    }
    float tool_block_height(AppComponent& app, const api::Message& m) {
        return kToolRowGap + kToolRowH + kToolRowGap + tool_out_height(app, m);
    }
    float tool_pile_height(AppComponent& app,
                           const std::vector<api::Message>& msgs, int lo,
                           int hi) {
        const std::string key =
            msgs[lo].id.empty() ? ("pile" + std::to_string(lo)) : msgs[lo].id;
        const bool open = app.expandedPiles.count(key) != 0;
        float h = kToolRowGap + kToolRowH + kToolRowGap;
        if (open) {
            h += (hi - lo) * (kSubRowH + 2.0f) + 6.0f;
            // + each sub-row's output-detail panel (0 when no captured result).
            for (int k = lo; k < hi; ++k) h += sub_out_height(msgs[k]);
        }
        return h;
    }

    // Trailing metadata cluster (count badge + duration + check), into a Row.
    void tool_meta_cluster(UIContext<InputAction>& ctx, Entity& parent,
                           int idbase, int count, const std::string& dur,
                           bool showCount = true, bool failed = false) {
        // Count badge only when it means something (a PILE of N calls); a single
        // tool call has no meaningful count, so we drop the "N" badge there.
        if (showCount) {
            // Badge = a rounded Row holding the `layers` sprite + the count.
            // (Icon and text live in SEPARATE child divs — combining a visible
            // label with on_draw_fg on one widget doesn't render the text.)
            auto badge = div(ctx, mk(parent, idbase + 1),
                ComponentConfig{}
                    .with_size(ComponentSize{children(), pixels(18)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{.right = pixels(7), .left = pixels(7)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_roundness(0.5f)
                    .with_margin(Margin{.right = pixels(6)})
                    .with_debug_name("tool_count"));
            // Lucide `layers` sprite (a stacked pile) — replaces the raw `≡`
            // unicode glyph (last raw chrome glyph; Phase H).
            div(ctx, mk(badge.ent(), 1),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(12), pixels(18)})
                    .with_transparent_bg()
                    .with_margin(Margin{.right = pixels(1)})
                    .with_on_draw_fg([](RectangleType r) {
                        hanabi::icons::draw_at(
                            "layers", r.x + r.width * 0.5f,
                            r.y + r.height * 0.5f, 11.0f,
                            theme::text_secondary());
                    })
                    .with_debug_name("tool_count_icon"));
            div(ctx, mk(badge.ent(), 2),
                ComponentConfig{}
                    .with_label(std::to_string(count))
                    .with_size(ComponentSize{children(), pixels(18)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("tool_count_n"));
        }
        // Duration only when known (real ms parsed); blank -> no fake number.
        // Sized to its CONTENT (children()) with a small trailing gap so it
        // sits right next to the dot instead of floating at the right edge of a
        // wide fixed box (Gabe: "group the icons together, why so much space").
        if (!dur.empty()) {
            div(ctx, mk(parent, idbase + 2),
                ComponentConfig{}
                    .with_label(dur)
                    .with_size(ComponentSize{children(), pixels(18)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Right)
                    .with_margin(Margin{.right = pixels(6)})
                    .with_debug_name("tool_dur"));
        }
        // Chat redesign #3: status is a small calm trailing DOT, not a big
        // checkmark — done = soft green, failed = red. Reads as an ambient
        // status indicator on the quiet tool card, not a "task complete" stamp.
        // Slot is 18px tall (matches the count/dur rows) and align_items::Center
        // on the parent row centers it vertically; the glyph is drawn at the
        // exact slot center (Gabe: "center the green dot").
        theme::Color dotC = failed ? theme::tag_blocked_fg()
                                   : theme::status_active();
        div(ctx, mk(parent, idbase + 3),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(12), pixels(18)})
                .with_transparent_bg()
                .with_on_draw_fg([dotC, failed](RectangleType rr) {
                    const float cx = rr.x + rr.width * 0.5f;
                    const float cy = rr.y + rr.height * 0.5f;
                    if (failed) {
                        draw_tool_fail(rr, dotC);  // keep the × for failures
                    } else {
                        afterhours::draw_circle_v({cx, cy}, 3.0f, dotC);
                    }
                })
                .with_debug_name("tool_check"));
    }
    // Small "×"-ish fail mark for a failed tool call (distinct from the check).
    static void draw_tool_fail(RectangleType r, theme::Color c) {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        afterhours::draw_line_ex(afterhours::vec2{cx - 3.0f, cy - 3.0f},
                                 afterhours::vec2{cx + 3.0f, cy + 3.0f}, 1.7f, c);
        afterhours::draw_line_ex(afterhours::vec2{cx + 3.0f, cy - 3.0f},
                                 afterhours::vec2{cx - 3.0f, cy + 3.0f}, 1.7f, c);
    }

    // Dense COLLAPSED tool row: chevron? + wrench + mono command + cluster.
    Entity& tool_row(UIContext<InputAction>& ctx, Entity& parent, int idbase,
                     float rowW, bool expandable, bool open,
                     const std::string& command, int count,
                     const std::string& dur, bool showCount = true,
                     bool failed = false) {
        auto head = div(ctx, mk(parent, idbase),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW), pixels(kToolRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(0), .right = pixels(16),
                                      .bottom = pixels(0), .left = pixels(10)})
                .with_margin(Margin{.top = pixels(kToolRowGap),
                                    .bottom = pixels(kToolRowGap)})
                // Chat redesign #3: a calm raised surface, NOT a bordered box.
                // The border + panel_bg made tool calls read as debug-log rules;
                // a soft panel_bg_2 fill with no border integrates them into the
                // assistant turn as a quiet step (spec: "belongs to the turn").
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_cursor(expandable ? afterhours::ui::CursorType::Pointer
                                        : afterhours::ui::CursorType::Default)
                // Shared chat corner: derive roundness from THIS row's height so
                // the tool card's pixel corner matches the user prompt bubble
                // (both target theme::kChatCorner). 0.42 here made a much rounder
                // corner than the bubble's — Gabe: "why did you round the corners
                // so much" + "corners of my prompt must match the tool call".
                .with_roundness(theme::roundness_for_px(theme::kChatCorner, rowW,
                                                        kToolRowH))
                .with_debug_name("tool_head"));
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(expandable ? (open ? "\xe2\x96\xbe"
                                               : "\xe2\x96\xb8")
                                       : " ")
                .with_size(ComponentSize{pixels(12), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("tool_chev"));
        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(16), pixels(18)})
                .with_transparent_bg()
                .with_margin(Margin{.right = pixels(6)})
                .with_on_draw_fg([](RectangleType rr) {
                    draw_wrench(rr, theme::text_faint());
                })
                .with_debug_name("tool_icon"));
        // Right-align the meta cluster (count/dur/check) to the row's right
        // edge. afterhours has no flex-grow (gap #18), so we compute the meta
        // cluster's ACTUAL width from what will be shown and size the command
        // column to fill the rest — otherwise a fixed 168px reserve left the
        // numbers floating mid-row (worst for a single tool call, which has no
        // count badge). Leading = chevron(12) + icon(16) + icon margin(6).
        const float kLeadW = 12.0f + 16.0f + 6.0f;
        float metaW = 12.0f;                         // status dot slot (always)
        if (!dur.empty()) metaW += 40.0f + 6.0f;     // duration (content) + margin
        if (showCount) metaW += 37.0f + 6.0f;        // count badge (content-sized) + margin
        // The row has left=10 + right=16 padding (26 total), so the content box
        // is rowW-26; subtract lead + meta from THAT. The wider right pad gives
        // the trailing status dot / duration breathing room from the hover
        // box's right edge (Gabe: "hover state too close to the time").
        float cmdW = rowW - 26.0f - kLeadW - metaW;
        if (cmdW < 60.0f) cmdW = 60.0f;
        div(ctx, mk(head.ent(), 3),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(command, 96))
                .with_size(ComponentSize{pixels(cmdW), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("tool_cmd"));
        tool_meta_cluster(ctx, head.ent(), 10, count, dur, showCount, failed);
        return head.ent();
    }

    // Collapsed PILE of >=2 consecutive tool messages: one dense header that
    // expands to indented nested per-node sub-rows.
    void tool_pile(UIContext<InputAction>& ctx, Entity& parent, int keyIndex,
                   const std::vector<api::Message>& msgs, int lo, int hi,
                   float paneWidth) {
        const int count = hi - lo;
        const std::string key = msgs[lo].id.empty()
                                    ? ("pile" + std::to_string(keyIndex))
                                    : msgs[lo].id;
        AppComponent* app = app_singleton();
        const bool open = app && app->expandedPiles.count(key) != 0;

        float rowW = paneWidth - 4.0f;
        if (rowW < 160.0f) rowW = 160.0f;

        auto wrap = div(ctx, mk(parent, 260 + keyIndex * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("tool_pile"));

        // Header command: prefix the node (e.g. "[cli:aspen] ") when the tool
        // ran on a specific node, so the collapsed header reads like Gabe asked:
        // "N tool calls · [node] cmd". tool_node() returns "" for local/unknown
        // (no fabricated node), so the prefix only appears when real.
        const std::string pileNode = tool_node(msgs[lo]);
        std::string cmd = std::to_string(count) + " tool calls  \xc2\xb7  " +
                          (pileNode.empty() ? "" : ("[" + pileNode + "] ")) +
                          tool_command(msgs[lo]);
        // The badge shows the REAL pile size (one Tool message == one call), so
        // it always matches the "N tool calls" header text. (Previously summed a
        // hashed per-message fake, which could disagree with the header.)
        int total = count;
        std::string dur = tool_duration(msgs[lo]);
        Entity& head =
            tool_row(ctx, wrap.ent(), 1, rowW, true, open, cmd, total, dur);
        head.addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (app && head.get<afterhours::ui::HasClickListener>().down) {
            if (open) app->expandedPiles.erase(key);
            else app->expandedPiles.insert(key);
        }

        if (open) {
            auto nest = div(ctx, mk(wrap.ent(), 2),
                ComponentConfig{}
                    // Width = rowW minus its own 20px left indent, so the nest
                    // (indented sub-rows) stays INSIDE the pile instead of
                    // overflowing by the margin (percent(1.0)+left:20 was
                    // parent_width+20 -> layout-warn spam every frame).
                    .with_size(ComponentSize{pixels(rowW - 20.0f), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_margin(Margin{.top = pixels(2), .bottom = pixels(4),
                                        .left = pixels(20)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("tool_nest"));
            for (int k = lo; k < hi; ++k) {
                tool_sub_row(ctx, nest.ent(), 100 + k * 4, msgs[k],
                             rowW - 20.0f);
                // Tool DETAILS: show a compact output preview under each
                // sub-row when the backend captured a result (Gabe: "we are
                // missing tool details"). A sunken mono panel with the first
                // few lines — the pile's expand reveals WHAT each call did, not
                // just that it ran. Height mirrored in tool_pile_height.
                if (!msgs[k].tool_result.empty())
                    tool_sub_output(ctx, nest.ent(), 100 + k * 4 + 1, msgs[k],
                                    rowW - 20.0f);
            }
        }
    }

    // Nested per-node sub-row: terminal icon + node label + truncated
    // sub-command + its duration + check. Very dense.
    void tool_sub_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                      const api::Message& m, float rowW) {
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW), pixels(kSubRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.right = pixels(8), .left = pixels(6)})
                .with_margin(Margin{.bottom = pixels(2)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("tool_subrow"));
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(14), pixels(16)})
                .with_transparent_bg()
                .with_margin(Margin{.right = pixels(5)})
                .with_on_draw_fg([](RectangleType rr) {
                    draw_terminal(rr, theme::text_faint());
                })
                .with_debug_name("sub_icon"));
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(tool_node(m))
                .with_size(ComponentSize{pixels(84), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font("mono", theme::type::MICRO)
                .with_margin(Margin{.right = pixels(8)})
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sub_node"));
        div(ctx, mk(row.ent(), 3),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(tool_command(m), 52))
                .with_size(ComponentSize{pixels(rowW - 168.0f), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font("mono", theme::type::SUBROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sub_cmd"));
        div(ctx, mk(row.ent(), 4),
            ComponentConfig{}
                .with_label(tool_duration(m))
                .with_size(ComponentSize{pixels(30), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::MICRO)
                .with_alignment(TextAlignment::Right)
                .with_margin(Margin{.right = pixels(6)})
                .with_debug_name("sub_dur"));
        div(ctx, mk(row.ent(), 5),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(12), pixels(16)})
                .with_transparent_bg()
                .with_on_draw_fg([](RectangleType rr) {
                    draw_check(rr, theme::tag_ready_fg());
                })
                .with_debug_name("sub_check"));
    }

    // How many output lines a sub-row's detail panel shows (a peek, not a dump).
    static constexpr int kSubOutLines = 4;
    // Number of result lines actually rendered for a sub-row (capped).
    static int sub_out_lines(const api::Message& m) {
        if (m.tool_result.empty()) return 0;
        int n = 1;
        for (char c : m.tool_result) if (c == '\n') ++n;
        return n > kSubOutLines ? kSubOutLines : n;
    }
    // Height of a sub-row's output panel (0 when no result). Mirrored in
    // tool_pile_height so the virtualization spacers line up.
    static float sub_out_height(const api::Message& m) {
        int n = sub_out_lines(m);
        if (n <= 0) return 0.0f;
        return static_cast<float>(n) * (kLinePitch - 2.0f) + 8.0f + 4.0f;
    }
    // Compact output preview under a pile sub-row: a sunken mono panel with the
    // first kSubOutLines of the captured tool_result — the "tool details".
    void tool_sub_output(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const api::Message& m, float rowW) {
        const int n = sub_out_lines(m);
        if (n <= 0) return;
        auto panel = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW - 20.0f),
                                         pixels(sub_out_height(m))})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_custom_background(theme::window_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_padding(Padding{.top = pixels(4), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(10)})
                .with_margin(Margin{.bottom = pixels(4), .left = pixels(20)})
                .with_roundness(theme::roundness_for_px(4.0f, rowW, 40.0f))
                .with_debug_name("sub_out"));
        // Split tool_result into lines; render the first kSubOutLines.
        size_t ls = 0;
        int li = 0;
        while (ls <= m.tool_result.size() && li < n) {
            size_t nl = m.tool_result.find('\n', ls);
            size_t e = (nl == std::string::npos) ? m.tool_result.size() : nl;
            std::string line = m.tool_result.substr(ls, e - ls);
            for (size_t t = line.find('\t'); t != std::string::npos;
                 t = line.find('\t', t))
                line.replace(t, 1, "  ");
            div(ctx, mk(panel.ent(), 1 + li),
                ComponentConfig{}
                    .with_label(line.empty() ? " "
                                             : fmtutil::ellipsize(line, 90))
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels(kLinePitch - 2.0f)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font("mono", theme::type::MICRO)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("sub_out_line"));
            ++li;
            if (nl == std::string::npos) break;
            ls = nl + 1;
        }
    }

    // ---- Spawned sub-agent inline card -----------------------------------
    // A spawn is a Tool-role message whose tool name (subtitle) marks a
    // sub-agent launch. We detect it by name so the http adapter + mock both
    // work, and render a DISTINCT inline card (sparkle + "Spawned agent" +
    // the task) at the point in the transcript where it happened (Gabe: "add UI
    // for when a thing is spawned"), instead of a generic tool row.
    static bool is_spawn_tool(const api::Message& m) {
        if (m.role != api::Role::Tool) return false;
        const std::string& n = m.subtitle;
        return n == "spawn_agent" || n == "spawn" || n == "Task" ||
               n == "task" || n == "sub_agent" || n == "spawn_sub_agent";
    }
    static constexpr float kSpawnCardH = 46.0f;
    static float spawn_card_height() {
        return kToolRowGap + kSpawnCardH + kToolRowGap;
    }
    void render_spawn_card(UIContext<InputAction>& ctx, Entity& parent,
                           int index, const api::Message& m, float paneWidth) {
        float rowW = paneWidth - 4.0f;
        if (rowW < 160.0f) rowW = 160.0f;
        const std::string task = tool_command(m);  // the spawned task/prompt
        const bool failed = tool_failed(m);

        auto card = div(ctx, mk(parent, 240 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW), pixels(kSpawnCardH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_margin(Margin{.top = pixels(kToolRowGap),
                                    .bottom = pixels(kToolRowGap)})
                .with_padding(Padding{.top = pixels(0), .right = pixels(12),
                                      .bottom = pixels(0), .left = pixels(12)})
                // A calm accent-tinted surface (distinct from the neutral tool
                // card) so a spawn reads as a notable "new agent" event.
                .with_custom_background(
                    theme::over(theme::accent_soft(), theme::panel_bg()))
                .with_border(theme::accent(), pixels(1.0f))
                .with_roundness(theme::roundness_for_px(theme::kChatCorner, rowW,
                                                        kSpawnCardH))
                .with_debug_name("spawn_card"));

        // Sparkle icon (sub-agent). Uses the same sprite as the brand mark.
        div(ctx, mk(card.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(18), pixels(18)})
                .with_transparent_bg()
                .with_margin(Margin{.right = pixels(10)})
                .with_on_draw_fg([](RectangleType r) {
                    if (!hanabi::icons::draw_at("brand", r.x + r.width * 0.5f,
                                                r.y + r.height * 0.5f, 14.0f,
                                                theme::accent())) {
                        afterhours::draw_text("\xe2\x9c\xa6", r.x + 3.0f,
                                              r.y + 3.0f, 14.0f,
                                              theme::accent());
                    }
                })
                .with_debug_name("spawn_icon"));

        // Two-line-ish text column: label + task (stacked).
        auto textCol = div(ctx, mk(card.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW - 180.0f),
                                         pixels(kSpawnCardH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_justify_content(JustifyContent::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("spawn_textcol"));
        div(ctx, mk(textCol.ent(), 1),
            ComponentConfig{}
                .with_label("Spawned agent")
                .with_size(ComponentSize{percent(1.0f), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::accent())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("spawn_label"));
        div(ctx, mk(textCol.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(task, 64))
                .with_size(ComponentSize{percent(1.0f), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("spawn_task"));

        // Trailing status dot (running amber / done green / failed red).
        theme::Color dotC =
            failed ? theme::tag_blocked_fg()
                   : (m.tool_status == "completed" ? theme::status_active()
                                                   : theme::status_idle());
        div(ctx, mk(card.ent(), 3),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(140), pixels(18)})
                .with_transparent_bg()
                .with_on_draw_fg([dotC](RectangleType rr) {
                    afterhours::draw_circle_v(
                        {rr.x + rr.width - 6.0f, rr.y + rr.height * 0.5f}, 3.0f,
                        dotC);
                })
                .with_debug_name("spawn_status"));
    }

    // A lone Tool message: one dense collapsed tool row (not expandable).
    void render_tool_block(UIContext<InputAction>& ctx, Entity& parent,
                           int index, const api::Message& m, float paneWidth) {
        float rowW = paneWidth - 4.0f;
        if (rowW < 160.0f) rowW = 160.0f;
        AppComponent* app = app_singleton();
        const std::string key = m.id.empty() ? "" : m.id;
        const bool expandable = tool_block_expandable(m);
        const bool open =
            expandable && app && !key.empty() && app->expandedPiles.count(key);
        // A single tool call is now clickable to reveal its captured output
        // (tool_result), exactly like a pile expands to its sub-rows. The
        // chevron shows only when there's something to expand.
        // Single tool row: prefix the node ("[cli:aspen] cmd") when known, same
        // as the pile header (Gabe: "[cli:aspen] cd …"). Empty for local/unknown.
        const std::string oneNode = tool_node(m);
        const std::string oneCmd =
            (oneNode.empty() ? "" : ("[" + oneNode + "] ")) + tool_command(m);
        Entity& head = tool_row(ctx, parent, 200 + index * 10, rowW,
                                /*expandable=*/expandable, open,
                                oneCmd, 1, tool_duration(m),
                                /*showCount=*/false, /*failed=*/tool_failed(m));
        if (expandable && app && !key.empty()) {
            head.addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            if (head.get<afterhours::ui::HasClickListener>().down) {
                if (open) app->expandedPiles.erase(key);
                else app->expandedPiles.insert(key);
            }
        }
        if (open) {
            // Expanded output: a sunken monospace panel showing the first
            // kToolOutLines of the tool's captured result (a peek, not the full
            // log). Height mirrors tool_out_height().
            std::string out = m.tool_result;
            // Keep only the first kToolOutLines lines.
            int nl = 0;
            size_t cut = std::string::npos;
            for (size_t p = 0; p < out.size(); ++p) {
                if (out[p] == '\n' && ++nl >= kToolOutLines) {
                    cut = p;
                    break;
                }
            }
            if (cut != std::string::npos) {
                out = out.substr(0, cut);
            }
            auto panel = div(ctx, mk(parent, 200 + index * 10 + 5),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(rowW),
                                             pixels(tool_out_height(*app, m))})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_custom_background(theme::window_bg())
                    .with_border(theme::border(), pixels(1.0f))
                    .with_padding(Padding{.top = pixels(6), .right = pixels(10),
                                          .bottom = pixels(6),
                                          .left = pixels(12)})
                    .with_margin(Margin{.bottom = pixels(kToolRowGap)})
                    .with_roundness(0.3f)
                    .with_debug_name("tool_out"));
            // Render each line as its OWN fixed-pitch mono row — afterhours'
            // wrap treats '\n' as a word char (gap #24), so a single label
            // would run the whole log onto one wrapped blob. Per-line rows honor
            // the hard breaks and exactly match tool_out_height (lines*pitch).
            auto outCol = div(ctx, mk(panel.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("tool_out_col"));
            {
                size_t ls = 0;
                int li = 0;
                while (ls <= out.size()) {
                    size_t nl2 = out.find('\n', ls);
                    size_t e = (nl2 == std::string::npos) ? out.size() : nl2;
                    std::string ln = out.substr(ls, e - ls);
                    // Tabs -> 2 spaces for stable columns (same as code lines).
                    for (size_t p = ln.find('\t'); p != std::string::npos;
                         p = ln.find('\t', p))
                        ln.replace(p, 1, "  ");
                    div(ctx, mk(outCol.ent(), 1 + li),
                        ComponentConfig{}
                            .with_label(ln.empty() ? " " : ln)
                            .with_size(ComponentSize{percent(1.0f),
                                                     pixels(kLinePitch)})
                            .with_transparent_bg()
                            .with_custom_text_color(theme::text_secondary())
                            .with_font("mono", theme::type::SM)
                            .with_alignment(TextAlignment::Left)
                            .with_roundness(0.0f)
                            .with_debug_name("tool_out_line"));
                    ++li;
                    if (nl2 == std::string::npos) break;
                    ls = nl2 + 1;
                }
            }
        }
    }

};

}  // namespace ecs
