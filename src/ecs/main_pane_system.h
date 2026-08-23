#pragma once

// Renders the main pane (right of the sidebar, below the tab strip). Dispatches
// on AppComponent::view: the smart views (Home / Blocked / Review / Starred)
// are digest lists over the thread set; Chat renders the active tab's
// transcript as message bubbles.

#include <chrono>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "../test_hooks.h"
#include "../util/capture_clock.h"
#include "../util/diff.h"
#include "../util/format.h"
#include "../util/textscan.h"
#include "keyboard_focus.h"
#include "thread_model.h"
#include "transcript_render_cache.h"
#include "../ui/find_highlight.h"
#include "../ui/find_nav.h"
#include "../ui/find_operators.h"
#include "../ui/text_select.h"
#include "../ui/inline_image.h"
#include "../ui/slash_commands.h"
#include "../ui/model_menu.h"
#include "../ui/effort_menu.h"
#include "../ui/fold_menu.h"
#include "../keys.h"
#include "../settings.h"
#include "ui_imports.h"

#include "../../vendor/afterhours/src/plugins/clipboard.h"

namespace ecs {

namespace find_ops = hanabi::find_ops;

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
        // conversation. It ALWAYS renders (even on a backend that can't send —
        // it shows a disabled input with a reason, so it never silently
        // vanishes: the whole 'no chat input' saga was a send-capability check
        // hiding it entirely).
        // Reserve the composer strip UNCONDITIONALLY so the input is always on
        // screen (render_composer disables it + shows a reason when the backend
        // can't send).
        // Cmd+F opens find-in-conversation; Esc and Cmd+F close it. Only
        // meaningful with a thread open, so it is a no-op elsewhere.
        // Shift excluded: Cmd+Shift+F is search-across-threads
        // (session_search_system.h), and without this it would open the find
        // bar on the way past.
        if (hanabi::keys::cmd_down() && !hanabi::keys::shift_down() &&
            hanabi::keys::pressed(hanabi::keys::kF)) {
            app->findOpen = !app->findOpen;
            if (!app->findOpen) app->findQuery.clear();
        }
        if (app->findOpen && app->escape == EscapeIntent::CloseFind) {
            app->findOpen = false;
            app->findQuery.clear();
            app->refocusComposer = true;
        }
        // Cmd+G steps to the next match, Cmd+Shift+G to the previous — the
        // same move the find bar's chevrons make, through the same find_nav
        // step, so the chord and the buttons cannot land on different matches.
        // Only while the bar is open: with it closed there is no tally for a
        // step to mean anything against.
        //
        // findCount is the count the LAST rendered frame painted. That is the
        // only count that exists at the top of a frame, and it is the honest
        // one to step over: it is the number of bands currently on screen.
        if (app->findOpen) {
            const hanabi::find_nav::Step step = hanabi::find_nav::chord(
                hanabi::keys::cmd_down(), hanabi::keys::shift_down(),
                hanabi::keys::pressed(hanabi::keys::kG));
            apply_find_step(*app, step);
        }
        // Test-only (HANABI_FIND_STEP=<±n>): the harness cannot press a Cmd
        // chord (afterhours_gaps.md #49), so this feeds the same step the
        // chord feeds, |n| times, on the first frame that has a tally to move
        // over. Everything below the two key reads is then under test.
        if (app->findOpen && !findStepApplied_) {
            const int n = hanabi::test_hooks::find_step();
            if (n != 0 && app->findCount > 0) {
                const hanabi::find_nav::Step s =
                    n < 0 ? hanabi::find_nav::Step::Prev
                          : hanabi::find_nav::Step::Next;
                for (int i = 0; i < (n < 0 ? -n : n); ++i)
                    apply_find_step(*app, s);
                findStepApplied_ = true;
            }
        }
        // Cmd+C copies selected transcript text. Checked before the composer
        // sees the key: with a selection up, Cmd+C means the selection.
        if (hanabi::keys::cmd_down() &&
            hanabi::keys::pressed(hanabi::keys::kC))
            hanabi::text_select::copy();
        if (app->escape == EscapeIntent::ClearTranscript)
            hanabi::text_select::clear();

        layout->composerHeight = 92.0f + attachments_h(*app);
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

        // ---- The list's keyboard cursor -----------------------------------
        // Moved over the rows the PREVIOUS frame drew. That order is the one
        // the reader is looking at — sections, folds and caps already applied
        // — so it cannot disagree with the screen the way a second copy of the
        // ordering rules eventually would.
        listRowsPrev_ = listRows_;
        listRows_.clear();
        listCursorY_ = -1.0f;
        listCursorH_ = 0.0f;
        listY_ = kListTopPad;
        if (move_list_cursor(*app)) {
            // The list has the keyboard now. afterhours keeps the last
            // clicked widget focused, and a focused button answers Enter —
            // so without this, Enter on a list row also re-fires whatever
            // was clicked to get here (the smart view in the sidebar, which
            // promptly switched back to the list).
            ctx.set_focus(ctx.ROOT);
        }
        // Enter opens the row the cursor is on. It cannot collide with the
        // composer's Enter: that one is the text field's own on_submit and
        // only fires while the field has focus, which is exactly when the
        // arrows were never ours either.
        if (!app->listCursorId.empty() && app->view != SmartView::Chat &&
            !any_text_field_focused() && !overlay_up(*app) &&
            hanabi::keys::pressed(hanabi::keys::kEnter))
            app->requestOpenTab = app->listCursorId;

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
        // ALWAYS rendered — render_composer disables the input + shows a reason
        // when the backend can't send, rather than the pane hiding it.
        {
            const auto& cr = layout->composer;
            // In split view the composer replies to the LEFT (primary) thread
            // only, so it takes the left pane's width. Full-width under two
            // panes gave no clue which conversation you were typing into.
            const bool split = app->view == SmartView::Chat &&
                               !app->splitSessionId.empty();
            const float cw = split ? (cr.width - 1.0f) * 0.5f : cr.width;
            render_composer(ctx, uiRoot, *app, cw, cr.height, composerKickoff,
                            cr.x, cr.y);
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
        // (scrollbar now drawn by afterhours)
        hanabi::apply_scroll_prefs(scroll.ent());

        int i = 0;
        Entity& wrap = centered_wrap(ctx, scroll.ent(), 9000, paneW - 48.0f);
        const float cardW = wrap_width(paneW);
        for (const auto* s : rows)
            digest_card(ctx, wrap, ++i, *s, app, false, cardW, singleState);
        scroll_cursor_into_view(scroll.ent(), listH);
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
                line = "\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94"
                       "\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94";
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
    // ---- Keyboard cursor over a digest list --------------------------------
    // The rows of the list drawn last frame, in draw order, and a running
    // content-y for the one being drawn now. Every element that takes vertical
    // space in a list adds its own extent as it renders (list_extent), so the
    // cursor's y is a sum of what actually got drawn rather than a mirror of
    // the layout kept in a second place.
    std::vector<std::string> listRows_, listRowsPrev_;
    float listY_ = 0.0f;
    float listCursorY_ = -1.0f;
    float listCursorH_ = 0.0f;
    bool listScrollPending_ = false;
    static constexpr float kListTopPad = 6.0f;  // the scroll panel's own pad

    // One-shot: HANABI_FIND_STEP is a stand-in for a keypress, so it fires
    // once and not every frame.
    bool findStepApplied_ = false;

    void list_extent(float h) { listY_ += h; }

    // True when this frame's arrow press moved the cursor.
    bool move_list_cursor(AppComponent& app) {
        const std::vector<std::string>& rows = listRowsPrev_;
        // A cursor whose row is gone — the view switched, a filter changed, a
        // section folded — is dropped rather than carried onto a list that
        // never had it.
        const auto at = std::find(rows.begin(), rows.end(), app.listCursorId);
        if (!app.listCursorId.empty() && at == rows.end())
            app.listCursorId.clear();
        if (app.arrow != ArrowIntent::List || rows.empty()) return false;

        size_t idx = 0;
        if (app.listCursorId.empty()) {
            // The first press lands on the near end, so Down enters the list
            // from the top and Up from the bottom.
            idx = (app.arrowDelta > 0) ? 0 : rows.size() - 1;
        } else {
            const size_t cur = static_cast<size_t>(at - rows.begin());
            // Clamped, not wrapped: jumping from the last row back to the
            // first reads as a bug in a list whose end you can see.
            idx = (app.arrowDelta > 0) ? std::min(cur + 1, rows.size() - 1)
                                       : (cur == 0 ? 0 : cur - 1);
        }
        app.listCursorId = rows[idx];
        listScrollPending_ = true;
        return true;
    }

    // Bring the cursor row into the viewport. Called after the list is built,
    // so the extents it reads are this frame's.
    void scroll_cursor_into_view(Entity& scrollEnt, float listH) {
        if (!listScrollPending_) return;
        listScrollPending_ = false;
        if (listCursorY_ < 0.0f) return;
        if (!scrollEnt.has<afterhours::ui::HasScrollView>()) return;
        auto& sv = scrollEnt.get<afterhours::ui::HasScrollView>();
        const float viewH =
            (sv.viewport_size.y > 1.0f) ? sv.viewport_size.y : listH;
        const float top = listCursorY_;
        const float bot = listCursorY_ + listCursorH_;
        float off = sv.scroll_offset.y;
        if (top < off) off = top;
        else if (bot > off + viewH) off = bot - viewH;
        else return;
        sv.scroll_offset.y = off;
        hanabi::set_scroll_target_y(sv, off);
        sv.clamp_scroll();
    }

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

        // Every card in every list comes through here, so this is where the
        // keyboard cursor is both drawn and counted: the order below IS the
        // order on screen. The cursor row wears the hover surface plus an
        // accent border — a keyboard hover, reading like the mouse one.
        const bool onCursor = !app.listCursorId.empty() && s.id == app.listCursorId;
        constexpr float kCardMarginTop = 3.0f;
        constexpr float kCardMarginBot = 5.0f;
        listRows_.push_back(s.id);
        if (onCursor) {
            listCursorY_ = listY_;
            listCursorH_ = kCardMarginTop + cardH + kCardMarginBot;
        }
        list_extent(kCardMarginTop + cardH + kCardMarginBot);

        auto card = div(ctx, mk(parent, 100 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(cardH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(kCardMarginTop),
                                    .right = pixels(0),
                                    .bottom = pixels(kCardMarginBot),
                                    .left = pixels(0)})
                .with_padding(Padding{.top = pixels(7), .right = pixels(16),
                                      .bottom = pixels(7), .left = pixels(16)})
                .with_custom_background(onCursor
                                            ? theme::hover_over(theme::panel_bg_2())
                                            : theme::panel_bg_2())
                .with_border(onCursor ? theme::accent() : theme::border(),
                             pixels(1.0f))
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(theme::layout::ROUNDNESS_BOX)
                .with_debug_name("digest_card"));
        card.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (card.ent().get<afterhours::ui::HasClickListener>().down)
            app.requestOpenTab = s.id;

        // The same context menu the sidebar row has, on the same state — a
        // session's menu should follow the session, and an archived thread has
        // no sidebar row left to right-click, so this is the only way back.
        // SidebarSystem draws it; it runs before this one, so the menu appears
        // on the next frame.
        if (ctx.is_right_click(card.ent().id)) {
            app.rowMenuOpen = true;
            app.rowMenuSessionId = s.id;
            app.rowMenuX = ctx.mouse.pos.x;
            app.rowMenuY = ctx.mouse.pos.y;
        }

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
        // (scrollbar now drawn by afterhours)
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
            const bool folded = section_label(
                ctx, wrap, 1,
                "Waiting on you \xc2\xb7 " + std::to_string(waiting.size()),
                first, theme::status_blocked(), app, "waiting");
            first = false;
            // Actionable rows: emphasize the "waiting on you \xc2\xb7 8m" metadata.
            if (!folded)
                for (const auto* s : waiting)
                    digest_card(ctx, wrap, ++shown, *s, app, true, cardW, true);
        }
        if (!finished.empty()) {
            const bool folded = section_label(
                ctx, wrap, 900,
                "Finished since you looked \xc2\xb7 " +
                    std::to_string(finished.size()),
                first, theme::tag_done_fg(), app, "finished");
            first = false;
            if (!folded)
                for (const auto* s : finished)
                    digest_card(ctx, wrap, ++shown, *s, app, false, cardW, true);
        }
        // Self-running work: a real section with real cards (title + relative
        // age), headed "SELF-RUNNING (N)" like the mock. Rendering the actual
        // running threads (not a lone caption) kills the old orphaned-caption
        // void (defect #14) — the count now sits ON a populated section.
        if (!selfRunning.empty()) {
            const bool folded = section_label(
                ctx, wrap, 1800,
                "Self-running \xc2\xb7 " + std::to_string(selfRunning.size()),
                first, theme::status_review(), app, "self_running");
            first = false;
            if (!folded)
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
            if (ecs::model::is_archived(s)) continue;
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
            list_extent(2.0f + 30.0f + 6.0f);
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
            const bool folded = section_label(ctx, wrap, 2600, "Recent", first,
                                              theme::text_faint(), app,
                                              "recent");
            constexpr size_t kMaxRecent = 20;
            if (!folded)
                for (size_t k = 0; k < recent.size() && k < kMaxRecent; ++k)
                    digest_card(ctx, wrap, ++shown, *recent[k], app, false,
                                cardW);
        }
        scroll_cursor_into_view(scroll.ent(), listH);
    }

    static std::string upper(std::string s) {
        return fmtutil::to_upper(std::move(s));
    }

    // Section header: a distinct LABEL — uppercase, letter-spaced, faint —
    // so it reads as a quiet grouping label vs the larger primary-color card
    // titles beneath it. `first` drops the leading margin so the top section
    // doesn't push a gap under the h1.
    //
    // Clicking one folds its shelf. Returns true when the shelf is folded, so
    // the caller skips its cards. The chevron is DRAWN, not typed: the font has
    // no triangles (gap #48) and a missing codepoint paints nothing at all.
    bool section_label(UIContext<InputAction>& ctx, Entity& parent,
                              int id, const std::string& text, bool first,
                              theme::Color color, AppComponent& app,
                              const std::string& shelfKey) {
        const bool collapsed = app.collapsedShelves.count(shelfKey) != 0;
        list_extent((first ? 4.0f : 20.0f) + 20.0f + 6.0f);

        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_margin(Margin{.top = pixels(first ? 4 : 20),
                                    .right = pixels(0), .bottom = pixels(6),
                                    .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("home_section"));

        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (row.ent().get<afterhours::ui::HasClickListener>().down) {
            if (collapsed) app.collapsedShelves.erase(shelfKey);
            else app.collapsedShelves.insert(shelfKey);
            Settings::get().set_shelf_collapsed(shelfKey, !collapsed);
        }
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(14), pixels(18)})
                .with_transparent_bg()
                .with_on_draw_fg([collapsed, color](RectangleType r) {
                    hanabi::glyph::chevron(r, collapsed, color, 3.2f);
                })
                .with_debug_name("home_section_chev"));

        // Content-sized, not percent(1.0): the chevron takes 14px off the row
        // and afterhours has no flex-grow (gap #18), so a full-width label
        // overflows its parent and floods the log with wrap/overflow warnings
        // — which also costs a solve_violations pass every frame. The row
        // itself still spans the pane, so the hover band does too.
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(upper(text))
                .with_size(ComponentSize{children(), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(color)
                .with_font_size(theme::type::LABEL)
                .with_letter_spacing(1.0f)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("home_section_text"));

        return collapsed;
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
    static constexpr float kSubAgentRowH = 24.0f;
    static constexpr float kSubAgentMargin = 8.0f;
    static constexpr float kSubAgentChipH = 26.0f;

    // Trailing spacer under the last message, so it can be scrolled clear of
    // the pane bottom instead of sitting flush against it.
    static constexpr float kTranscriptBottomPad = 28.0f;

    // Height of the sub-agent rollup, WITHOUT rendering it — the transcript
    // needs the number during its measure pass, before it knows where in the
    // column the rollup will actually sit.
    static float sub_agent_panel_height(AppComponent& app) {
        const auto& subs = app.openSession->sub_agents;
        if (subs.empty()) return 0.0f;
        float total = kSubAgentMargin + kSubAgentRowH + kSubAgentMargin;
        if (app.expandedPiles.count("__subagents__") != 0) {
            const int rows = (chip_count(subs) + 2) / 3;
            total += 6.0f + rows * (kSubAgentChipH + 6.0f);
        }
        return total;
    }

    // Does this sub-agent get a chip of its own? A finished one does not,
    // unless the reader has asked for finished work: on a thread that spawned
    // a dozen helpers it is the done ones that bury the two still running.
    // Global, so the answer is the same in every thread — Settings ->
    // Behavior -> Sub-agents.
    static bool sub_agent_listed(const api::SubAgent& sa) {
        return sa.state != api::SubAgentState::Done ||
               Settings::get().get_show_finished_subagents();
    }

    // How many chips the expanded rollup draws: the listed sub-agents, plus one
    // more saying how many were left out. The measure pass and the render have
    // to agree on this or the transcript reserves the wrong height.
    static int chip_count(const std::vector<api::SubAgent>& subs) {
        int listed = 0;
        for (const auto& sa : subs)
            if (sub_agent_listed(sa)) ++listed;
        const int hidden = static_cast<int>(subs.size()) - listed;
        return listed + (hidden > 0 ? 1 : 0);
    }

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

        constexpr float kRowH = kSubAgentRowH;
        constexpr float kMargin = kSubAgentMargin;
        const float total = sub_agent_panel_height(app);

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
                .with_label(" ")
                .with_size(ComponentSize{pixels(14), pixels(18)})
                .with_transparent_bg()
                .with_on_draw_fg([open](RectangleType r) {
                    hanabi::glyph::chevron(r, !open, theme::text_faint(), 3.2f);
                })
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
            int hidden = 0;
            for (const auto& sa : subs) {
                if (!sub_agent_listed(sa)) {
                    ++hidden;
                    continue;
                }
                sub_chip(ctx, chips.ent(), 10 + i, sub_glyph_for(sa.state),
                         sa.title);
                ++i;
            }
            // What was left out, said out loud. A list that quietly omits rows
            // is a list you cannot trust; this is also the way back — the
            // control it names is one global setting, not a per-thread one.
            if (hidden > 0)
                sub_chip(ctx, chips.ent(), 10 + i, SubGlyph::Done,
                         std::to_string(hidden) + " finished hidden");
        }
        return total;
    }

    // One compact sub-agent chip: glyph + short title.
    void sub_chip(UIContext<InputAction>& ctx, Entity& parent, int id,
                  SubGlyph g, const std::string& title) {
        auto chip = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(kSubAgentChipH)})
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
        enum Kind { Bubble, ToolPile, ToolBlock, Spawn, NewDivider,
                    DateDivider, Thinking } kind;
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
                .with_corner_radius(5.0f)
                .with_render_layer(4)
                .with_debug_name("split_close"));
        closeBtn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (closeBtn.ent().get<afterhours::ui::HasClickListener>().down)
            app.requestSplitClose = true;
    }

    // ---------------- "New since you last looked" --------------------------
    // A thread that gained twelve messages overnight looks exactly like one
    // that gained none: you reopen it, land at the bottom, and have no idea
    // where to start reading. Every mature client draws a line.
    //
    // The boundary is the newest message timestamp from the last time this
    // thread was open, persisted per thread. Messages after it are new.
    static constexpr float kNewDividerH = 26.0f;

    static void new_divider(UIContext<InputAction>& ctx, Entity& parent,
                            int id, int count, float rowW) {
        const std::string label =
            std::to_string(count) +
            (count == 1 ? " new message" : " new messages");
        // The label is a real text element, not something painted in
        // on_draw_fg: drawn text never reaches the visible-text registry, so a
        // divider drawn wholesale would be invisible to every assertion about
        // it. The rule is drawn, the words are a widget.
        float lw = 120.0f;
        if (auto* fm = afterhours::EntityHelper::get_singleton_cmp<
                afterhours::ui::FontManager>())
            lw = afterhours::measure_text(fm->get_active_font(), label.c_str(),
                                          theme::type::SM, 1.0f)
                     .x;
        constexpr float kGap = 10.0f;

        auto row = div(ctx, mk(parent, 8600 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kNewDividerH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("new_divider"));
        // The rule fills what the label leaves. afterhours has no flex-grow
        // (gap #18), so its width is computed rather than grown — and it is
        // sized off the COLUMN width the caller laid out, since percent() here
        // would resolve against the row and overflow it (gap #53).
        // Explicit width, NOT percent(1.0): a percent child in a NoWrap row
        // resolves against the whole row, so it would take all of it, push the
        // label out, and get silently shrunk to fit — the exact trap in gap
        // #53, which cost an hour here before the label reappeared.
        float ruleW = rowW - lw - kGap;
        if (ruleW < 8.0f) ruleW = 8.0f;
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(ruleW), pixels(kNewDividerH)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([](RectangleType r) {
                    const float cy = r.y + r.height * 0.5f;
                    afterhours::draw_line_ex(
                        afterhours::vec2{r.x, cy},
                        afterhours::vec2{r.x + r.width, cy}, 1.0f,
                        theme::status_blocked());
                })
                .with_debug_name("new_divider_rule"));
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(lw + 2.0f),
                                         pixels(kNewDividerH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::status_blocked())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("new_divider_label"));
    }

    // ---------------- Date dividers ----------------------------------------
    // Every row carries how long ago it was ("3h", "2d"), which answers
    // "recently?" and never answers "which day?". A thread worked over a week
    // reads as one undifferentiated column, and the moment a night passed
    // mid-conversation is invisible.
    //
    // So a row naming the day is drawn above the first message of each new
    // local calendar day. The day boundary is the rule rather than the
    // breakdown's "more than four hours": this row says a DAY's name, and a
    // gap rule would print "Tuesday, August 18" twice on a Tuesday and print
    // nothing at all when a conversation ran past midnight.
    //
    // Nothing above the first message: a thread's opening line does not need
    // to be told it is the first thing that happened.
    static constexpr float kDateDividerH = 26.0f;

    static bool starts_new_day(const api::Message& prev,
                               const api::Message& curr) {
        if (prev.created_at <= 0 || curr.created_at <= 0) return false;
        return !fmtutil::same_local_day(prev.created_at, curr.created_at);
    }

    // The rule is DRAWN, not typed: Roboto has no Box Drawing block and a
    // codepoint the font lacks paints nothing at all (afterhours_gaps.md #48).
    // The label is a real text element for the same reason as in new_divider —
    // drawn text never reaches the visible-text registry, so a divider painted
    // wholesale would be invisible to every assertion about it.
    static void date_divider(UIContext<InputAction>& ctx, Entity& parent,
                             int id, int64_t at, float rowW) {
        const std::string label = fmtutil::day_label(at);
        if (label.empty()) {
            // Unnameable day (a stamp the C library cannot convert). The row
            // still takes its measured height — the item list already counted
            // it, and a row that silently occupies nothing shifts every spacer
            // below it.
            div(ctx, mk(parent, 8700 + id),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels(kDateDividerH)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("date_divider"));
            return;
        }
        float lw = 90.0f;
        if (auto* fm = afterhours::EntityHelper::get_singleton_cmp<
                afterhours::ui::FontManager>())
            lw = afterhours::measure_text(fm->get_active_font(), label.c_str(),
                                          theme::type::SM, 1.0f)
                     .x;
        constexpr float kGap = 12.0f;

        auto row = div(ctx, mk(parent, 8700 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kDateDividerH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("date_divider"));
        // Explicit widths, never percent(1.0): a percent child in a NoWrap row
        // resolves against the whole row and shoves its siblings out (gap #53).
        // The label is centred, so each rule takes half of what it leaves.
        float ruleW = (rowW - lw - 2.0f * kGap) * 0.5f;
        if (ruleW < 8.0f) ruleW = 8.0f;
        const auto rule = [&](int childId) {
            div(ctx, mk(row.ent(), childId),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(ruleW),
                                             pixels(kDateDividerH)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_on_draw_fg([](RectangleType r) {
                        const float cy = r.y + r.height * 0.5f;
                        afterhours::draw_line_ex(
                            afterhours::vec2{r.x, cy},
                            afterhours::vec2{r.x + r.width, cy}, 1.0f,
                            theme::border());
                    })
                    .with_debug_name("date_divider_rule"));
        };
        rule(1);
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(lw + 2.0f * kGap),
                                         pixels(kDateDividerH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("date_divider_label"));
        rule(3);
    }

    static bool caret_on_first_line(const std::string& text, size_t caret) {
        const size_t firstBreak = text.find('\n');
        return firstBreak == std::string::npos || firstBreak >= caret;
    }

    static bool caret_on_last_line(const std::string& text, size_t caret) {
        return text.find('\n', caret) == std::string::npos;
    }

    // Is any text field holding the keyboard? While one is, the reading keys
    // belong to it: Home and End move the caret, and space is a character.
    // Make one text element selectable: hit-testable, tracked for press and
    // drag, and showing an I-beam. The listener is empty — an element with no
    // click or drag listener is never eligible to be the hot element
    // (gap #38), so it exists purely to make the pointer visible here.
    static void selectable_text(UIContext<InputAction>& ctx, Entity& el,
                                const std::string& text, float fontPx) {
        el.addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (el.has<afterhours::HasColor>())
            el.get<afterhours::HasColor>().skip_hover_override = true;
        el.addComponentIfMissing<afterhours::ui::HasCursor>(
            afterhours::ui::CursorType::Text);
        if (!el.has<afterhours::ui::UIComponent>()) return;
        const auto& cmp = el.get<afterhours::ui::UIComponent>();
        const RectangleType r = cmp.rect();
        hanabi::text_select::update(el.id, r, text, fontPx, ctx.mouse.pos.x,
                                    ctx.mouse.pos.y, ctx.mouse.left_down,
                                    ctx.mouse.just_pressed);
        // Test/screenshot only: adopt a pre-selected run the first time the
        // element that contains it renders. No-op unless HANABI_SELECT_DEMO is
        // set (see apply_test_knobs).
        AppComponent* sapp = app_singleton();
        if (sapp && !sapp->selectDemo.empty()) {
            const size_t at = text.find(sapp->selectDemo);
            if (at != std::string::npos) {
                auto& st = hanabi::text_select::state();
                st.owner = el.id;
                st.text = text;
                st.fontPx = fontPx;
                st.anchor = at;
                st.cursor = at + sapp->selectDemo.size();
                st.dragging = false;
                sapp->selectDemo.clear();
            }
        }
    }

    // ---------------- Find in conversation ---------------------------------
    // Case-insensitive substring search over the open thread. Matching runs on
    // the message text as stored; the highlight below re-searches each RENDERED
    // line, so a match split across a markdown marker is counted here and not
    // painted. Both are honest about the same text — see the note on
    // highlight_ranges.
    static std::vector<size_t> find_all(const std::string& hay,
                                        const std::string& needle) {
        return textscan::occurrences(hay, needle);
    }

    // ---- What find can actually paint --------------------------------------
    //
    // The count and the highlight must agree. The highlight is drawn per
    // RENDERED line — the string the renderer hands to draw_text_in_rect after
    // markdown normalization and inline-marker parsing — while the obvious
    // thing to count is the message's stored text. Those are different
    // strings: "**ledger**" stores ten characters and paints six, so a naive
    // scan finds matches that can never be highlighted, and the tally says 3
    // where the reader can see 2. A count that disagrees with the highlighting
    // is worse than a lower count, so counting is done over exactly the
    // strings that get painted, and nothing else is counted.
    //
    // Deliberately NOT included, because none of them are highlighted:
    // markdown tables and fenced code blocks (their own render paths), tool
    // rows, and the sub-agent chips. A match inside a code block is real and
    // this will not find it — that is a gap in the feature, not a lie about
    // it, and it is noted in the PR.
    static std::vector<std::string> paintable_lines(const api::Message& m,
                                                    bool rich) {
        std::vector<std::string> out;
        const std::string body = strip_inline_md(redact_secrets(m.text));
        if (!rich) {
            // The user path renders one plain label, markers stripped.
            out.push_back(strip_inline_markers(body));
            return out;
        }
        // The assistant path mirrors render_rich_body's walk exactly: skip the
        // atomic blocks it hands to other renderers, and emit the visible text
        // of every ordinary line.
        size_t start = 0;
        while (start <= body.size()) {
            const size_t nl = body.find('\n', start);
            const size_t end = (nl == std::string::npos) ? body.size() : nl;
            const std::string line = body.substr(start, end - start);

            if (is_table_start(body, start)) {
                std::vector<std::vector<std::string>> rows;
                start = scan_table(body, start, &rows);
                continue;
            }
            if (is_code_fence(line)) {
                size_t p = (nl == std::string::npos) ? body.size() : nl + 1;
                while (p <= body.size()) {
                    const size_t n2 = body.find('\n', p);
                    const size_t e2 = (n2 == std::string::npos) ? body.size()
                                                                : n2;
                    if (is_code_fence(body.substr(p, e2 - p))) {
                        p = (n2 == std::string::npos) ? body.size() : n2 + 1;
                        break;
                    }
                    if (n2 == std::string::npos) { p = body.size() + 1; break; }
                    p = n2 + 1;
                }
                start = p;
                continue;
            }
            // A heading is painted — with its own find band — so what it
            // paints, the text without the hashes, is what may be counted.
            if (md_heading_level(line) > 0) {
                out.push_back(md_heading_text(line));
                if (nl == std::string::npos) break;
                start = nl + 1;
                continue;
            }
            if (!line.empty()) out.push_back(md_to_spans(line).visible);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return out;
    }

    // Times on transcript rows are a preference: a reader who knows when the
    // conversation happened is only being crowded by four more stamps. Read
    // per use rather than cached — the toggle takes effect on the next frame,
    // with the settings sheet still open over the transcript.
    static bool show_times() { return Settings::get().get_show_timestamps(); }

    // One match: which message, and where in it.
    struct Match {
        int msg = 0;
        size_t off = 0;
    };
    static std::vector<Match> collect_matches(const api::Session& s,
                                              const find_ops::Query& q) {
        std::vector<Match> out;
        if (q.text.empty() || q.invalid) return out;
        for (size_t i = 0; i < s.messages.size(); ++i) {
            const auto& m = s.messages[i];
            // Tool rows and system captions are not highlighted, so they are
            // not counted. Same rule as everything else here: if it cannot be
            // painted, it does not exist to the tally.
            if (m.role != api::Role::User && m.role != api::Role::Assistant)
                continue;
            // An operator excludes the row from the tally and from the
            // painting through this one test, so the two cannot disagree.
            if (!find_ops::row_matches(s, i, q)) continue;
            const bool rich = (m.role != api::Role::User);
            for (const auto& line : paintable_lines(m, rich))
                for (size_t off : find_all(line, q.text))
                    out.push_back(Match{static_cast<int>(i), off});
        }
        return out;
    }

    // The query as it stands this frame, parsed. Cheap enough to redo per row
    // (a handful of tokens) and that keeps the parse next to its use instead
    // of in a cache that can go stale mid-frame.
    static find_ops::Query live_query() {
        AppComponent* app = app_singleton();
        if (app == nullptr || !app->findOpen) return find_ops::Query{};
        return find_ops::parse(app->findQuery);
    }

    // What find should paint inside message `index` — the query's text, or
    // nothing at all when an operator has excluded that row.
    static std::string paint_query_for(int index) {
        AppComponent* app = app_singleton();
        if (app == nullptr || !app->findOpen || !app->openSession)
            return std::string();
        const find_ops::Query q = live_query();
        if (q.invalid || q.text.empty()) return std::string();
        if (!find_ops::row_matches(*app->openSession,
                                   static_cast<size_t>(index), q))
            return std::string();
        return q.text;
    }

    // Move the current match one step and ask the transcript to scroll it into
    // view. The Cmd+G chord and the find bar's chevrons both come through
    // here, so they cannot drift onto different matches.
    static void apply_find_step(AppComponent& app, hanabi::find_nav::Step s) {
        if (s == hanabi::find_nav::Step::None || app.findCount <= 0) return;
        app.findIndex =
            hanabi::find_nav::advance(app.findIndex, app.findCount, s);
        app.findScrollPending = true;
    }

    // The find bar: an overlay pinned to the transcript's top-right, so it
    // never displaces the conversation under it.
    void find_bar(UIContext<InputAction>& ctx, Entity& parent,
                  AppComponent& app, float paneW, int matchCount,
                  const find_ops::Query& q) {
        // Content must FIT: afterhours has no flex-grow and warns (loudly, every
        // frame) when a NoWrap row's children exceed it. Sized from the parts:
        // pad 8+6, input 168, gap 6, tally 74, two 22px steppers, a 4px gap and
        // a 22px close.
        constexpr float kBarW = 8.0f + 168.0f + 6.0f + 74.0f + 22.0f + 22.0f +
                                4.0f + 22.0f + 6.0f;
        constexpr float kBarH = 34.0f;
        const float bx = paneW - kBarW - 18.0f;
        auto bar = div(ctx, mk(parent, 7500),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kBarW), pixels(kBarH)})
                .with_absolute_position()
                .with_translate(bx, 52.0f)
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.right = pixels(6), .left = pixels(8)})
                .with_custom_background(theme::over(theme::panel_bg_2(),
                                                    theme::panel_bg()))
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.25f)
                .with_render_layer(9)
                .with_debug_name("find_bar"));

        ctx.theme.secondary = theme::over(theme::panel_bg_2(),
                                          theme::panel_bg());
        ctx.theme.surface = ctx.theme.secondary;
        ctx.theme.font = theme::text_primary();
        ctx.theme.focus = theme::accent();
        // No padding on the field: text_input's own inner element is sized to
        // the element's OUTER width, so any padding here makes the child wider
        // than the content box it sits in and afterhours warns every frame
        // (and warns is the good case — it also re-solves the layout). The
        // inset comes from the bar's padding instead.
        afterhours::ui::imm::text_input(
            ctx, mk(bar.ent(), 1), app.findQuery,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(168), pixels(26)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.2f)
                .with_render_layer(9)
                .with_debug_name("find_input"));

        // "3 of 12", or "no matches" once something has been typed.
        std::string tally;
        if (!app.findQuery.empty())
            tally = matchCount == 0
                        ? std::string("no matches")
                        : (std::to_string(app.findIndex + 1) + " of " +
                           std::to_string(matchCount));        div(ctx, mk(bar.ent(), 2),
            ComponentConfig{}
                .with_label(tally)
                .with_size(ComponentSize{pixels(74), pixels(16)})
                .with_margin(Margin{.left = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(9)
                .with_debug_name("find_tally"));

        const bool navigable = matchCount > 0;
        auto step = [&](int id, bool up) {
            auto b = button(ctx, mk(bar.ent(), id),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(22), pixels(22)})
                    .with_custom_background(theme::panel_bg())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.3f)
                    .with_render_layer(9)
                    .with_on_draw_fg([up, navigable](RectangleType r) {
                        // A chevron, drawn rather than typed: the font has no
                        // triangles (gap #48). Up = previous match.
                        const theme::Color c = navigable
                                                   ? theme::text_secondary()
                                                   : theme::text_faint();
                        const float cx = r.x + r.width * 0.5f;
                        const float cy = r.y + r.height * 0.5f;
                        const float h = 3.2f;
                        if (up)
                            afterhours::draw_triangle(
                                afterhours::vec2{cx, cy - h},
                                afterhours::vec2{cx + h, cy + h},
                                afterhours::vec2{cx - h, cy + h}, c);
                        else
                            afterhours::draw_triangle(
                                afterhours::vec2{cx - h, cy - h},
                                afterhours::vec2{cx + h, cy - h},
                                afterhours::vec2{cx, cy + h}, c);
                    })
                    .with_debug_name(up ? "find_prev" : "find_next"));
            if (b && navigable) {
                apply_find_step(app, up ? hanabi::find_nav::Step::Prev
                                        : hanabi::find_nav::Step::Next);
            }
        };
        step(3, true);
        step(4, false);

        auto close = button(ctx, mk(bar.ent(), 5),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(22), pixels(22)})
                .with_margin(Margin{.left = pixels(4)})
                .with_custom_background(theme::panel_bg())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.3f)
                .with_render_layer(9)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "close", "\xc3\x97", theme::text_secondary(), 12.0f))
                .with_debug_name("find_close"));
        if (close) {
            app.findOpen = false;
            app.findQuery.clear();
            app.refocusComposer = true;
        }

        // An operator we do not have would otherwise vanish into the plain
        // text and quietly widen the search — the query would say one thing
        // and the tally another. Say so instead, under the bar so the row's
        // fixed width is untouched.
        if (q.invalid)
            div(ctx, mk(parent, 7501),
                ComponentConfig{}
                    .with_label(find_ops::kHint)
                    .with_size(ComponentSize{pixels(kBarW), pixels(16)})
                    .with_absolute_position()
                    .with_translate(bx, 52.0f + kBarH + 4.0f)
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_render_layer(9)
                    .with_debug_name("find_hint"));

        // Test-only (HANABI_FIND_AUDIT=1): the previous frame's painted bands,
        // so a script can assert the tally against the highlighting instead of
        // against a second reading of the same counting code.
        const int bands = hanabi::find_highlight::take_band_count();
        if (hanabi::test_hooks::find_audit())
            div(ctx, mk(parent, 7502),
                ComponentConfig{}
                    .with_label("bands " + std::to_string(bands))
                    .with_size(ComponentSize{pixels(kBarW), pixels(16)})
                    .with_absolute_position()
                    .with_translate(bx, 52.0f + kBarH + 22.0f)
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_render_layer(9)
                    .with_debug_name("find_audit"));
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
                show_times()
                    ? fmtutil::relative_time(app.openSession->summary.updated_at)
                    : std::string();
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
            // Still fetching: the welcome hero here reads as "nothing here"
            // rather than "not yet". selectedId covers the frames before the
            // loader flips transcriptState; Error already returned above.
            const bool opening = !app.selectedId.empty() ||
                                 app.transcriptState == LoadState::Loading ||
                                 !app.transcriptLoadingId.empty();
            if (opening) {
                loading_spinner(ctx, parent, "Loading conversation\xe2\x80\xa6");
                return;
            }
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
        // (scrollbar now drawn by afterhours)
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

        hanabi::text_select::begin_frame();
        render_cache().reset_for_thread(app.openSession->summary.id);

        // Measured here, RENDERED further down: the rollup is the first thing
        // in the column, but a short thread gets a leading spacer in front of
        // it, and the spacer's size isn't known until every item is measured.
        const float subH = sub_agent_panel_height(app);

        const bool streamingHere =
            app.streamActive &&
            app.streamSessionId == app.openSession->summary.id &&
            app.streamPhase != AppComponent::StreamPhase::Done;
        const size_t liveIdx = app.streamMsgIndex;

        const auto& msgs = app.openSession->messages;
        const int n = static_cast<int>(msgs.size());

        // ---- Where the reader left off -------------------------------------
        // The first message newer than the stamp saved when this thread was
        // last open. -1 when everything has been seen, or when it has never
        // been opened (a first read is all-new, and marking every message new
        // is noise, not information).
        // Decided ONCE per open and held. A thread opens at its bottom, so
        // arriving at the end is not evidence the reader has read anything —
        // recomputing every frame deleted the line while they were looking at
        // it. Reaching the end advances the persisted stamp for NEXT time; the
        // line on screen stays until the thread is closed and reopened.
        const std::string& openId0 = app.openSession->summary.id;
        struct UnreadMark {
            bool computed = false;
            int64_t stamp = 0;   // the persisted stamp it was computed from
            size_t seen = 0;     // message count when it was computed
            int first = -1;
            int count = 0;
        };
        static std::unordered_map<std::string, UnreadMark> s_unread;
        UnreadMark& mark = s_unread[openId0];
        const int64_t lastRead = Settings::get().get_last_read(openId0);
        // Recomputed when the thread is first seen, and again if messages were
        // PREPENDED (load-older shifts every index, so a held index would
        // point at the wrong message).
        // Also recomputed when the persisted stamp changes underneath us. Our
        // OWN advance (reaching the end, below) writes mark.stamp too, so it
        // does not look like an external change and does not delete the line
        // the reader is looking at. Anything else — another window, a test
        // placing the boundary — is a real re-mark and is honoured.
        if (!mark.computed || msgs.size() < mark.seen || lastRead != mark.stamp) {
            mark.computed = true;
            mark.stamp = lastRead;
            mark.seen = msgs.size();
            mark.first = -1;
            mark.count = 0;
            if (lastRead > 0) {
                for (int i = 0; i < n; ++i) {
                    if (msgs[static_cast<size_t>(i)].created_at > lastRead) {
                        if (mark.first < 0) mark.first = i;
                        ++mark.count;
                    }
                }
            }
            // Everything new, on a thread that has been read before, means the
            // stamp is stale rather than the whole thread being unread.
            if (mark.first == 0 && mark.count == n) mark.first = -1;
        } else if (msgs.size() > mark.seen && mark.first >= 0) {
            // Messages arrived while it was open — they are new too, and they
            // are appended, so the boundary index is unaffected.
            mark.count += static_cast<int>(msgs.size() - mark.seen);
            mark.seen = msgs.size();
        }
        const int firstUnread = mark.first;
        const int unreadCount = mark.count;

        // ---- Pass 1: item list + measured heights (memoized). --------------
        std::vector<Item> items;
        items.reserve(n);
        float totalH = subH;
        {
            int i = 0;
            while (i < n) {
                // The day row goes above whatever item starts this day —
                // measured here, drawn from the same height below. A boundary
                // INSIDE a tool pile is not marked: the pile is one visual
                // unit, and splitting it to date it would be a worse read
                // than a pile whose first tool carries the day.
                if (i > 0 && starts_new_day(msgs[i - 1], msgs[i])) {
                    Item d;
                    d.kind = Item::DateDivider;
                    d.lo = i;
                    d.height = kDateDividerH;
                    totalH += d.height;
                    items.push_back(d);
                }
                if (i == firstUnread) {
                    Item d;
                    d.kind = Item::NewDivider;
                    d.lo = i;
                    d.hi = unreadCount;
                    d.height = kNewDividerH;
                    totalH += d.height;
                    items.push_back(d);
                }
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
                if (is_thinking(m) &&
                    !(streamingHere && static_cast<size_t>(i) == liveIdx)) {
                    Item it;
                    it.kind = Item::Thinking;
                    it.lo = i;
                    it.height = thinking_height(app, m, i, colW);
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

        // ---- Find in conversation: matches + scroll-to-match ---------------
        // Recomputed each frame from the query. Cheap (a substring scan of the
        // loaded window) and it keeps the count honest as a stream appends.
        const find_ops::Query findQ =
            app.findOpen ? find_ops::parse(app.findQuery) : find_ops::Query{};
        std::vector<Match> matches;
        if (app.findOpen && !findQ.text.empty()) {
            matches = collect_matches(*app.openSession, findQ);
            if (app.findIndex >= static_cast<int>(matches.size()))
                app.findIndex = 0;
        }
        app.findCount = static_cast<int>(matches.size());

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
        // Jump the current match into view. The item list carries every
        // message's measured height, so the y of the message holding the match
        // is the sum of the heights before it; a third of a viewport of lead-in
        // puts it comfortably inside the pane rather than flush at the top.
        if (app.findScrollPending && !matches.empty() &&
            scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const int target = matches[static_cast<size_t>(app.findIndex)].msg;
            float y = subH;
            for (const auto& it : items) {
                if (it.lo == target ||
                    (it.kind == Item::ToolPile && it.lo <= target &&
                     target < it.hi))
                    break;
                y += it.height;
            }
            auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            sv.scroll_offset.y = std::max(0.0f, y - viewH / 3.0f);
            hanabi::set_scroll_target_y(sv, sv.scroll_offset.y);
            sv.clamp_scroll();
            scrollY = sv.scroll_offset.y;
            app.findScrollPending = false;
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

        // ---- Reading the transcript from the keyboard ----------------------
        // A long thread is a document, and a document scrolls without the
        // mouse. Home/End jump to the ends, Page moves a viewport less a
        // couple of lines of overlap (so the line you were reading is still on
        // screen), and the arrows move a few lines.
        //
        // Suppressed while a text field owns the keyboard: Home and End belong
        // to the caret then, and space belongs to the message being typed.
        // TextInputSystem sets a focused field's is_focused, so anything
        // focused means the composer or the find box is the target.
        if (scroll.ent().has<afterhours::ui::HasScrollView>() &&
            !any_text_field_focused()) {
            auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            const float page = std::max(40.0f, viewH - 2.0f * kLinePitch);
            const float step = 3.0f * kLinePitch;
            float delta = 0.0f;
            bool jumpTop = false, jumpEnd = false;
            if (hanabi::keys::pressed(hanabi::keys::kPageUp)) delta -= page;
            if (hanabi::keys::pressed(hanabi::keys::kPageDown)) delta += page;
            // Up/Down come from the one owner (arrow_system.h) — the same
            // press also means "walk the composer history" and "move the list
            // cursor", and only one of those may happen per keystroke.
            if (app.arrow == ArrowIntent::Transcript)
                delta += step * static_cast<float>(app.arrowDelta);
            if (hanabi::keys::pressed(hanabi::keys::kHome)) jumpTop = true;
            if (hanabi::keys::pressed(hanabi::keys::kEnd)) jumpEnd = true;

            if (jumpTop || jumpEnd || delta != 0.0f) {
                float want = jumpTop  ? 0.0f
                             : jumpEnd ? 1e9f
                                       : sv.scroll_offset.y + delta;
                sv.scroll_offset.y = want;
                hanabi::set_scroll_target_y(sv, want);
                sv.clamp_scroll();
                scrollY = sv.scroll_offset.y;
                // Scrolling up by hand means "stop following the bottom", the
                // same as a wheel scroll does; End means "follow again".
                s_follow = jumpEnd;
                s_prevOffset = scrollY;
            }
        }

        // Pin to bottom on a first-open, while streaming here, or whenever the
        // follow-latch is engaged (user hasn't scrolled up).
        const bool atBottom = s_follow || nearEnd;
        // Find owns the scroll while it is open with a query: the bottom-pin
        // would drag the view back off the match the moment it landed.
        const bool findDriving = app.findOpen && !findQ.text.empty();
        const bool pinBottom =
            !findDriving && (wantOpenBottom || streamingHere || s_follow);
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
        // Short-thread bottom anchor: when the whole transcript fits in the
        // viewport there is nothing to scroll, and afterhours stacks a column
        // from the top — so a two-message thread floated at the top of the pane
        // with a few hundred px of dead space above the composer. A chat log
        // reads bottom-up: the newest line sits just above the input and the
        // conversation grows upward off the top. A leading spacer of the whole
        // slack gives exactly that, and it keeps the transition into a
        // scrollable thread seamless (the last line stays where it was).
        // Skipped while content is growing (streaming) or shifting (load-older)
        // so the anchor math isn't fighting a spacer that resizes underneath it.
        if (totalH < viewH - 40.0f && !streamingHere && !app.loadingOlder &&
            app.anchorPending.empty()) {
            div(ctx, mk(col, 29999),
                ComponentConfig{}
                    .with_size(ComponentSize{
                        percent(1.0f),
                        pixels(viewH - totalH - kTranscriptBottomPad - 6.0f)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("sparse_balance"));
        }
        sub_agent_panel(ctx, col, app);
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
                case Item::NewDivider:
                    new_divider(ctx, col, it.lo, it.hi, colW);
                    break;
                case Item::DateDivider:
                    date_divider(ctx, col, it.lo, msgs[it.lo].created_at,
                                 colW);
                    break;
                case Item::Thinking:
                    render_thinking_block(ctx, col, it.lo, msgs[it.lo], app,
                                          colW);
                    break;
            }
        }
        flush_spacer(99999);

        // Mark the thread read once the newest message is actually on screen.
        // Not on open — that would clear the divider you opened the thread to
        // see — and not on a mere scroll-to-bottom either: the stamp only ever
        // advances (Settings::set_last_read enforces it), so the divider
        // survives until the reader has genuinely arrived at the end.
        if (atBottom && n > 0) {
            const int64_t newest = msgs[static_cast<size_t>(n - 1)].created_at;
            // Forward only — scrolling back through a thread must not un-read
            // what you have already seen.
            if (newest > 0 && newest > Settings::get().get_last_read(openId0)) {
                Settings::get().set_last_read(openId0, newest);
                mark.stamp = newest;  // our own write, not an external re-mark
            }
        }

        // Bottom breathing room: a real trailing spacer so the LAST line can be
        // scrolled fully clear of the viewport bottom (and the composer that
        // overlays it). Without this the final message sat flush against / under
        // the edge and couldn't be brought fully into view.
        div(ctx, mk(col, 30000 + 88888),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(kTranscriptBottomPad)})
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
        if (app.findOpen)
            find_bar(ctx, parent, app, paneW, app.findCount, findQ);
        hanabi::text_select::end_frame(ctx.mouse.just_pressed);
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
    // The model picker: a popover over the composer strip listing the models
    // this deployment serves (src/ui/model_menu.h says where the list comes
    // from), with the current one marked. Choosing writes the default-model
    // preference and nothing else — there is no in-flight state to show a
    // spinner for, because there is no per-session model verb in this client
    // to wait on. The settings sheet's Model row reads and writes the same
    // value, so the two can never disagree.
    void render_model_popover(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app, Entity& anchorEnt,
                              const std::string& currentModel) {
        constexpr float kRowH = 24.0f;
        constexpr float kPopW = 210.0f;
        const auto& models = hanabi::models::all();
        const float popH =
            kRowH * static_cast<float>(models.size()) + 8.0f;
        const RectangleType anchor =
            anchorEnt.get<afterhours::ui::UIComponent>().rect();
        auto pop = afterhours::ui::imm::popover(
            ctx, mk(parent, 3200), anchor, app.modelPopoverOpen,
            afterhours::ui::overlay::Placement::Above,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kPopW), pixels(popH)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_padding(Padding{.top = pixels(4), .bottom = pixels(4)})
                .with_roundness(0.25f)
                .with_render_layer(7)
                .with_debug_name("model_popover"));
        if (!pop) return;
        for (size_t i = 0; i < models.size(); ++i) {
            const auto& m = models[i];
            const bool selected = m.id == currentModel;
            auto row = button(ctx, mk(pop.ent(), static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(std::string(m.name))
                    .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                    .with_custom_background(selected ? theme::selected_bg()
                                                     : theme::panel_bg_2())
                    .with_custom_hover_bg(
                        theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(selected ? theme::text_primary()
                                                     : theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_padding(Padding{.left = pixels(26)})
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_render_layer(8)
                    .with_on_draw_fg([selected](RectangleType r) {
                        // The mark is drawn, not typed: Roboto has no
                        // geometric shapes and would paint nothing (gap #48).
                        hanabi::glyph::radio(
                            RectangleType{r.x + 8.0f, r.y, 12.0f, r.height},
                            selected, selected ? theme::accent()
                                               : theme::text_faint());
                    })
                    .with_debug_name("model_row_" + std::to_string(i)));
            if (row) {
                Settings::get().set_default_model(std::string(m.id));
                app.modelPopoverOpen = false;
            }
        }
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
    // The effort picker: a popover over the composer strip with the server's
    // own ladder (src/ui/effort_menu.h), the current level marked. Choosing
    // writes the local preference and closes — there is no request in flight,
    // so there is no spinner and no "patching…" state to show. A slider was
    // the sketch; five discrete named levels are not a continuum, and a row
    // per level says what each one means where a notch cannot.
    void render_effort_popover(UIContext<InputAction>& ctx, Entity& parent,
                               AppComponent& app, Entity& anchorEnt,
                               const std::string& currentEffort) {
        constexpr float kRowH = 30.0f;
        constexpr float kPopW = 230.0f;
        const auto& levels = hanabi::effort::all();
        const float popH = kRowH * static_cast<float>(levels.size()) + 8.0f;
        const RectangleType anchor =
            anchorEnt.get<afterhours::ui::UIComponent>().rect();
        auto pop = afterhours::ui::imm::popover(
            ctx, mk(parent, 3300), anchor, app.effortPopoverOpen,
            afterhours::ui::overlay::Placement::Above,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kPopW), pixels(popH)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_padding(Padding{.top = pixels(4), .bottom = pixels(4)})
                .with_roundness(0.25f)
                .with_render_layer(7)
                .with_debug_name("effort_popover"));
        if (!pop) return;
        for (size_t i = 0; i < levels.size(); ++i) {
            const auto& lv = levels[i];
            const bool selected = lv.id == currentEffort;
            auto row = button(ctx, mk(pop.ent(), static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(std::string(lv.name) + "   \xc2\xb7   " +
                                std::string(lv.note))
                    .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                    .with_custom_background(selected ? theme::selected_bg()
                                                     : theme::panel_bg_2())
                    .with_custom_hover_bg(
                        theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(selected ? theme::text_primary()
                                                     : theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_padding(Padding{.left = pixels(26)})
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_render_layer(8)
                    .with_on_draw_fg([selected](RectangleType r) {
                        // Drawn, not typed: Roboto has no geometric shapes and
                        // a codepoint it lacks paints nothing (gap #48).
                        hanabi::glyph::radio(
                            RectangleType{r.x + 8.0f, r.y, 12.0f, r.height},
                            selected,
                            selected ? theme::accent() : theme::text_faint());
                    })
                    .with_debug_name("effort_row_" + std::to_string(i)));
            if (row) {
                Settings::get().set_default_effort(std::string(lv.id));
                app.effortPopoverOpen = false;
            }
        }
    }

    // ---- Composer attachments (pasted / dropped images) -------------------
    // Height of the chips block, and the ONE place the number comes from: the
    // strip's total height is reserved a full system earlier (layout->
    // composerHeight, set in for_each_with), so a block that measured itself
    // differently from what it draws would leave the input hanging off the
    // bottom of the strip.
    static constexpr float kAttachChipH = 38.0f;
    static constexpr float kAttachNoteH = 16.0f;
    static float attachments_h(const AppComponent& app) {
        if (app.composerAttachments.empty()) return 0.0f;
        return kAttachChipH + kAttachNoteH + 6.0f;  // chips, note, gap under
    }

    // A chip per pasted/dropped image, and one line saying plainly that they
    // are not going anywhere.
    //
    // WHY THE CHIP SAYS SO. hanabi cannot send an image on ANY backend it
    // speaks today: api::Client's send seam is send_message(session_id,
    // prompt) — two strings — and all three adapters take it at its word (the
    // mock, the generic http adapter's {session_id, message} body, and the
    // agentcloud adapter's `{"cmd":"input","text":…,"apply":…}` frame). The
    // orchestrator itself is NOT the blocker: its HTTP message route accepts
    // inline `attachments[]` (base64, five per message) and uploads them
    // server-side, and its socket `input` command carries `files` — but those
    // are file-id HANDLES a client can only get by uploading first, and that
    // upload path is not something this client has. So the honest state is:
    // take the image in, show it, and say it stays here. A chip that looked
    // like an attachment and vanished on send would be the worst of the three
    // options.
    void render_attachments(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app) {
        if (app.composerAttachments.empty()) return;

        auto strip = div(ctx, mk(parent, 4),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(attachments_h(app))})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_attachments"));

        auto chips = div(ctx, mk(strip.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kAttachChipH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_attach_chips"));

        int removeAt = -1;
        for (size_t i = 0; i < app.composerAttachments.size(); ++i) {
            const auto& att = app.composerAttachments[i];
            const std::string idx = std::to_string(i);

            auto chip = div(ctx, mk(chips.ent(), static_cast<int>(i) + 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(196), pixels(30)})
                    .with_margin(Margin{.right = pixels(8)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{.right = pixels(4), .left = pixels(4)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_border(theme::border(), pixels(1.0f))
                    .with_roundness(0.3f)
                    .with_debug_name("attach_chip_" + idx));

            // The thumbnail, drawn through the same decode cache the
            // transcript's inline images use. A path that will not decode
            // draws nothing rather than a broken box — the name beside it
            // still says which file this is.
            const std::string path = att.path;
            div(ctx, mk(chip.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(22), pixels(22)})
                    .with_margin(Margin{.right = pixels(6)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_on_draw_bg([path](RectangleType r) {
                        hanabi::inline_image::draw(path, r.x, r.y, r.width,
                                                   r.height);
                    })
                    .with_debug_name("attach_thumb_" + idx));

            div(ctx, mk(chip.ent(), 2),
                ComponentConfig{}
                    .with_label(att.name)
                    .with_size(ComponentSize{pixels(140), pixels(20)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("attach_name_" + idx));

            auto x = button(ctx, mk(chip.ent(), 3),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(20), pixels(20)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.3f)
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        "close", "\xc3\x97", theme::text_secondary(), 12.0f))
                    .with_debug_name("attach_remove_" + idx));
            if (x) removeAt = static_cast<int>(i);
        }

        div(ctx, mk(strip.ent(), 2),
            ComponentConfig{}
                .with_label("hanabi can't send images yet \xe2\x80\x94 these "
                            "stay in the composer")
                .with_size(ComponentSize{percent(1.0f), pixels(kAttachNoteH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_attach_note"));

        if (removeAt >= 0)
            app.composerAttachments.erase(app.composerAttachments.begin() +
                                          removeAt);
    }


    // The tool-fold picker: three modes, per session, written straight to
    // settings. Picking one clears the per-row overrides, because a reader who
    // asks for "Expand all" means all of them and not "all except the four I
    // closed before I found this menu".
    void render_fold_popover(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app, Entity& anchorEnt,
                             hanabi::fold::Mode current) {
        constexpr float kRowH = 30.0f;
        constexpr float kPopW = 230.0f;
        const auto& choices = hanabi::fold::all();
        const float popH = kRowH * static_cast<float>(choices.size()) + 8.0f;
        const RectangleType anchor =
            anchorEnt.get<afterhours::ui::UIComponent>().rect();
        auto pop = afterhours::ui::imm::popover(
            ctx, mk(parent, 3500), anchor, app.foldPopoverOpen,
            afterhours::ui::overlay::Placement::Above,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kPopW), pixels(popH)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_padding(Padding{.top = pixels(4), .bottom = pixels(4)})
                .with_roundness(0.25f)
                .with_render_layer(7)
                .with_debug_name("fold_popover"));
        if (!pop) return;
        for (size_t i = 0; i < choices.size(); ++i) {
            const auto& c = choices[i];
            const bool selected = c.mode == current;
            auto row = button(ctx, mk(pop.ent(), static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(std::string(c.name) + "   \xc2\xb7   " +
                                std::string(c.note))
                    .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                    .with_custom_background(selected ? theme::selected_bg()
                                                     : theme::panel_bg_2())
                    .with_custom_hover_bg(
                        theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(selected ? theme::text_primary()
                                                     : theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_padding(Padding{.left = pixels(26)})
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_render_layer(8)
                    .with_on_draw_fg([selected](RectangleType r) {
                        // Drawn, not typed: Roboto has no geometric shapes and
                        // a codepoint it lacks paints nothing (gap #48).
                        hanabi::glyph::radio(
                            RectangleType{r.x + 8.0f, r.y, 12.0f, r.height},
                            selected,
                            selected ? theme::accent() : theme::text_faint());
                    })
                    .with_debug_name("fold_row_" + std::to_string(i)));
            if (row && app.openSession) {
                Settings::get().set_tool_fold(app.openSession->summary.id,
                                              hanabi::fold::to_int(c.mode));
                // The sub-agent rollup shares this set but is not a tool row,
                // so its own disclosure is left exactly as the reader left it.
                const bool subs = app.expandedPiles.count("__subagents__") != 0;
                app.expandedPiles.clear();
                app.collapsedPiles.clear();
                if (subs) app.expandedPiles.insert("__subagents__");
                app.foldPopoverOpen = false;
            }
        }
    }

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

        // This composer's sent-message history (Up/Down walk, below). Keyed the
        // same way the draft is, so the two never disagree about which thread
        // they belong to.
        AppComponent::ComposerHistory& history = app.composerHistory[draftKey];
        const auto remember_sent = [&history](const std::string& text) {
            history.sent.push_back(text);
            history.walkIndex = 0;
            history.stashedDraft.clear();
        };

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

        // Enter parked its text here (see the listener at the bottom of this
        // function). Route it the same way the Send button does, with the mode
        // recomputed for THIS frame.
        //
        // A slash draft is held back instead: it is a command to this client,
        // not a message to the agent, and it is carried out further down where
        // the field entity exists (a completion has to be written back into
        // it).
        std::string slashSubmit;
        // Enter is not always the send key. When it is not, the keystroke is
        // dropped here and the draft is left exactly as typed — which is why
        // the field is emptied HERE, on the decision, instead of by the
        // listener that merely saw the key.
        bool clearFieldAfterSubmit = false;
        if (!app.composerSubmit.empty()) {
            const std::string text = std::move(app.composerSubmit);
            const bool withCmd = app.composerSubmitWithCmd;
            app.composerSubmit.clear();
            app.composerSubmitWithCmd = false;
            if (hanabi::enter_sends(Settings::get().get_send_key(), withCmd)) {
                if (hanabi::slash::is_command_text(text)) {
                    slashSubmit = text;
                } else if (canStream || canSend) {
                    if (kickoff) app.requestKickoffPrompt = text;
                    else if (canStream) app.requestStreamPrompt = text;
                    else app.requestSendPrompt = text;
                    remember_sent(text);
                }
                replyDraft.clear();
                clearFieldAfterSubmit = true;
            }
        }
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

        render_attachments(ctx, bar.ent(), app);

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
                // Modest uniform roundness (all 4 corners). A high fractional
                // value (roundness_for_px(10) => ~0.59 on a 34px bar) drove the
                // arc-heavy path that renders jagged bracket end-caps; 0.25 is a
                // clean subtle corner. Uniform (not mixed) so it never hits the
                // gap #25 degenerate-corner path.
                .with_roundness(0.25f)
                .with_debug_name("composer_input_wrap"));

        // text_input forces its own Secondary bg over its rect (gap #17); point
        // Secondary/Surface at panel_bg_2 so the field blends into the pill
        // above instead of painting a jarring default-dark box.
        ctx.theme.secondary = theme::panel_bg_2();
        ctx.theme.surface = theme::panel_bg_2();
        ctx.theme.font = theme::text_primary();
        ctx.theme.focus = theme::accent();
        // What the empty field says it is for. text_input renders this itself
        // now; it used to be an absolutely-positioned on_draw_fg child laid
        // over the field, because the widget had no placeholder of its own.
        const bool phSteer = !kickoff && app.should_steer_open();
        const char* placeholder = kickoff ? "Start a new conversation\xe2\x80\xa6"
                                  : phSteer ? "Steer the running agent\xe2\x80\xa6"
                                            : "Message hanabi\xe2\x80\xa6";
        auto inputRes = afterhours::ui::imm::text_input(
            ctx, mk(inputWrap.ent(), 1), replyDraft,
            ComponentConfig{}
                .with_placeholder(placeholder)
                .with_size(ComponentSize{percent(1.0f), pixels(34)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_padding(Padding{.left = pixels(12), .right = pixels(10)})
                // Match the wrap's uniform 0.25 corner (gap #17: text_input
                // paints its own bg over its rect, so a mismatched roundness
                // double-draws an inner rounded box).
                .with_roundness(0.25f)
                .with_debug_name("composer_reply_input"));

        // Screenshot affordance: HANABI_TEST_FOCUS_COMPOSER=1 force-focuses the
        // composer field so a capture can photograph the caret WITH text in it
        // (verifying caret position). Test-only; ignored when unset.
        if (std::getenv("HANABI_TEST_FOCUS_COMPOSER"))
            ctx.set_focus(inputRes.ent().id);

        // Opt-in field diagnostics: dump the live text_input state so we can
        // see EXACTLY what the field receives (chars, cursor, h-scroll) —
        // pins down space/backspace/wrap issues instead of guessing across the
        // vendored widget. Gated on HANABI_DBG_INPUT; a no-op when unset.
        if (std::getenv("HANABI_DBG_INPUT") &&
            inputRes.ent().has<afterhours::text_input::HasTextInputState>()) {
            const auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextInputState>();
            static std::string s_last;
            std::string cur = st.text();
            if (cur != s_last) {  // only log on change (avoid per-frame spam)
                s_last = cur;
                fprintf(stderr,
                        "[DBG input] focused=%d text_len=%zu cursor=%zu "
                        "inputW=%.1f text=\"%s\"\n",
                        (int)st.is_focused, cur.size(),
                        (size_t)st.cursor_position, inputW, cur.c_str());
            }
        }

        // ESCAPE-TO-CLEAR (Gabe): when the composer field is focused and ESC is
        // pressed, clear the input. (The first-ESC-pauses-the-agent behavior is
        // deferred — for now ESC just clears, which is the common chat-app
        // behavior.) Only when Esc belongs to the transcript this frame: with
        // an overlay or the find bar up, Esc dismisses THAT and the draft you
        // typed survives (escape_system.h).
        if (inputRes.ent().has<afterhours::text_input::HasTextInputState>()) {
            auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextInputState>();
            if (st.is_focused && app.escape == EscapeIntent::ClearTranscript) {
                st.storage.clear();
                st.cursor_position = 0;
                replyDraft.clear();
            }
        }

        // SLASH COMMANDS. A draft that opens with "/" is addressed to this
        // client, not to the agent: the menu below offers the vocabulary, and
        // the router carries out what hanabi can actually do and says plainly
        // what it cannot (src/ui/slash_commands.h has the reasoning per verb).
        //
        // Typing anything at all retires the last command's notice; it is a
        // reply to one keystroke, not a state the strip should keep.
        static std::string lastSlashDraft;
        if (replyDraft != lastSlashDraft) app.slashNotice.clear();
        // Putting the caret back in the composer. The focusable element is
        // the text_input's inner FIELD, not the wrapper this call returns:
        // focus set on the wrapper is dropped at end of frame, because only
        // the field registers itself as focusable (afterhours_gaps.md #57).
        // was_focused rides along so the widget does not read the return as a
        // fresh focus and select-all — what follows is the argument, not a
        // replacement for the verb.
        const auto refocus_field = [&]() {
            const auto& kids =
                inputRes.ent().get<afterhours::ui::UIComponent>().children;
            if (kids.empty()) return;
            ctx.set_focus(kids[0]);
            if (inputRes.ent()
                    .has<afterhours::text_input::HasTextInputState>())
                inputRes.ent()
                    .get<afterhours::text_input::HasTextInputState>()
                    .was_focused = true;
        };

        // The find bar just closed and took the caret with it (it owns a field
        // of its own). Put it back where the user was typing.
        if (app.refocusComposer) {
            app.refocusComposer = false;
            refocus_field();
        }

        const std::vector<const hanabi::slash::Command*> slashRows =
            hanabi::slash::filter(replyDraft);
        bool slashOpen =
            !slashRows.empty() && replyDraft != app.slashDismissedFor;
        if (app.escape == EscapeIntent::CloseSlashMenu && slashOpen) {
            app.slashDismissedFor = replyDraft;
            slashOpen = false;
            // Esc blurs the field too — afterhours' text_input does that
            // itself, on its own read of the key, with no way to opt out
            // (afterhours_gaps.md #57). The keystroke that put the menu away
            // must not also take the caret out of the field.
            refocus_field();
        }
        if (!slashOpen) app.slashMenuIndex = 0;
        if (app.slashMenuIndex >= static_cast<int>(slashRows.size()))
            app.slashMenuIndex = 0;
        app.slashMenuOpen = slashOpen;

        // Writing to the field's own state as well as the draft: the widget
        // keeps its own storage, and a draft change alone leaves the visible
        // text (and the caret) where it was.
        const auto set_field = [&](const std::string& text) {
            replyDraft = text;
            if (!inputRes.ent()
                     .has<afterhours::text_input::HasTextInputState>())
                return;
            auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextInputState>();
            st.storage.clear();
            st.storage.insert(0, text);
            st.cursor_position = text.size();
            st.clear_selection();
        };

        // The send decided at the top of this frame: empty the field now that
        // there is something that can write to it. The widget keeps its own
        // storage, so clearing replyDraft up there is not enough.
        if (clearFieldAfterSubmit) set_field("");

        // Carry out a parsed command. Only /new has somewhere to go today; the
        // rest report what is missing rather than reaching the agent as text.
        // `typed` is put back in the field for anything that did not run, so
        // a refused command leaves the words you wrote where you can edit
        // them — Enter empties the field before this is reached.
        const auto run_slash = [&](const hanabi::slash::Parsed& p,
                                   const std::string& typed) {
            const hanabi::slash::Command* cmd = hanabi::slash::find(p.verb);
            if (cmd == nullptr) {
                app.slashNotice = "/" + p.verb + " is not a command";
                set_field(typed);
                return;
            }
            if (!cmd->runnable) {
                app.slashNotice =
                    "/" + std::string(cmd->name) + " \xe2\x80\x94 " +
                    std::string(cmd->unwired);
                set_field(typed);
                return;
            }
            if (cmd->name == "new") {
                // The same new-conversation sheet Cmd+N raises.
                app.composerOpen = true;
                app.slashNotice.clear();
                set_field("");
            }
        };

        // Choosing a row: a verb that wants an argument completes into the
        // field so it can be typed; one that does not runs immediately.
        const auto choose_slash = [&](int index) {
            if (index < 0 || index >= static_cast<int>(slashRows.size()))
                return;
            const hanabi::slash::Command& cmd = *slashRows[index];
            if (cmd.arg.empty()) {
                hanabi::slash::Parsed p;
                p.matched = true;
                p.verb = std::string(cmd.name);
                p.known = true;
                run_slash(p, hanabi::slash::completion(cmd));
            } else {
                set_field(hanabi::slash::completion(cmd));
            }
            app.slashDismissedFor = replyDraft;
            app.slashMenuOpen = false;
            slashOpen = false;
            // A click landed focus on the row; typing the argument needs
            // the field back.
            refocus_field();
        };

        // Enter with the menu up takes the highlighted row; with it down (the
        // draft has reached its argument) it runs what was typed.
        if (!slashSubmit.empty()) {
            if (slashOpen) choose_slash(app.slashMenuIndex);
            else run_slash(hanabi::slash::parse(slashSubmit), slashSubmit);
        }

        // Up/Down belong to the menu while it is up — the history walk below
        // stands down so one keystroke never does two things.
        if (slashOpen && !slashRows.empty() &&
            inputRes.ent().has<afterhours::text_input::HasTextInputState>() &&
            inputRes.ent()
                .get<afterhours::text_input::HasTextInputState>()
                .is_focused) {
            const int count = static_cast<int>(slashRows.size());
            if (hanabi::keys::pressed(hanabi::keys::kUp))
                app.slashMenuIndex = (app.slashMenuIndex + count - 1) % count;
            if (hanabi::keys::pressed(hanabi::keys::kDown))
                app.slashMenuIndex = (app.slashMenuIndex + 1) % count;
        }

        // HISTORY WALK. Up recalls the previous sent message, Down steps back
        // toward the draft you were writing. Only while THIS field has focus:
        // unfocused, the same arrows scroll the transcript (which is why that
        // block upstream skips itself when a text field is focused).
        //
        // The arrows are not bound to any InputAction (preload.cpp), and the
        // text_input widget itself does nothing with them, so nothing upstream
        // has already eaten the keystroke.
        if (inputRes.ent().has<afterhours::text_input::HasTextInputState>()) {
            auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextInputState>();
            const std::string typed = st.text();
            const size_t caret = std::min(st.cursor_position, typed.size());
            // A draft with line breaks in it wants Up/Down for the caret, so
            // the walk only claims the keystroke at the edges of the text.
            // The arrows have one owner (ecs/arrow_system.h); the slash menu
            // is the exception it does not know about yet, and it drives its
            // own selection off the same keys — so the walk stands aside
            // while that menu is up.
            const bool mine = st.is_focused && !slashOpen &&
                              app.arrow == ArrowIntent::TextField;
            const bool walkBack = mine && app.arrowDelta < 0 &&
                                  caret_on_first_line(typed, caret);
            const bool walkForward = mine && app.arrowDelta > 0 &&
                                     caret_on_last_line(typed, caret);
            const auto recall = [&](const std::string& text) {
                st.storage.clear();
                st.storage.insert(0, text);
                st.cursor_position = text.size();
                st.clear_selection();
                replyDraft = text;
            };
            const bool atOldest = history.walkIndex >= history.sent.size();
            const bool atDraft = history.walkIndex == 0;
            if (walkBack && !atOldest) {
                if (atDraft) history.stashedDraft = typed;
                history.walkIndex++;
                recall(history.sent[history.sent.size() - history.walkIndex]);
            } else if (walkForward && !atDraft) {
                history.walkIndex--;
                recall(history.walkIndex == 0
                           ? history.stashedDraft
                           : history.sent[history.sent.size() -
                                          history.walkIndex]);
            }
        }

        // ENTER-TO-SEND. afterhours' text_input fires on_submit on Enter
        // (WidgetPress == ENTER, preload.cpp) IF the entity carries a
        // HasTextInputListener — the imm wrapper doesn't attach one, so a naked
        // text_input swallowed Enter and the ONLY way to send was clicking the
        // button (Gabe: "HOW DO I SEND A MESSAGE"). Attach a listener whose
        // on_submit sets the SAME one-shot send/stream request the Send button
        // does, so Enter sends like every chat app. Shift+Enter is NOT a newline
        // here (single-line composer); plain Enter = send.
        //
        // addComponentIfMissing means the listener attached on some early frame
        // is the one that runs forever. Anything it captures is frozen at that
        // moment — and on the frames right after launch there is no open
        // session, so a captured `kickoff` was baked in as TRUE and Enter
        // started a new conversation for the rest of the process even with a
        // thread open (the Send button, recomputed every frame, replied
        // correctly — so the two did different things). The same freeze applied
        // to the &replyDraft pointer, which would clear whichever thread's
        // draft happened to be current when the field was born.
        //
        // So the listener decides nothing: it parks the text on the app and the
        // per-frame code above routes it.
        //
        // The send KEY is the same trap one turn further on. Which keystroke
        // sends is a setting the user can change between one Enter and the
        // next, so the listener must not test it either — nor may it empty the
        // field, because emptying it IS the decision that the text was sent.
        // It reports two facts, the text and whether Cmd was held, and the
        // router above applies hanabi::enter_sends to them.
        {
            Entity& inputEnt = inputRes.ent();
            inputEnt.addComponentIfMissing<
                afterhours::text_input::HasTextInputListener>(
                nullptr,  // on_change: not needed (imm syncs replyDraft)
                [appPtr = &app](Entity& e) {
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
                    if (text.empty()) return;  // nothing to send
                    appPtr->composerSubmit = text;
                    appPtr->composerSubmitWithCmd = hanabi::keys::cmd_down();
                });
        }


        // Send affordance. Enabled (primary-styled, clickable) when the backend
        // supports replies and the draft has text; otherwise disabled-styled.
        // When the open thread's agent is RUNNING and the backend can steer,
        // this same button STEERS (interrupt/redirect) — relabel to "Steer" so
        // the action reads honestly. Minimal touch: label-only (fits the same
        // fixed sendW), no layout change.
        const bool steerMode = !kickoff && app.should_steer_open();
        // Send = a filled primary button with an up-arrow (modern chat "send"),
        // Steer keeps its word (interrupt/redirect reads better as text), "…"
        // while in flight. ~10px corner to match the input pill (0.5 made a
        // fully-rounded lozenge that clashed with the field).
        const char* sendLabel =
            sending ? "\xe2\x80\xa6" : (steerMode ? "Steer" : "Send");
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
                .with_roundness(0.25f)
                .with_debug_name("composer_send"));
        if (send && sendEnabled) {
            // A slash draft is a command, so the button carries it out rather
            // than sending the words to the agent (the Enter path above does
            // the same).
            if (hanabi::slash::is_command_text(replyDraft)) {
                const std::string typed = replyDraft;
                if (slashOpen) choose_slash(app.slashMenuIndex);
                else run_slash(hanabi::slash::parse(typed), typed);
            } else {
                // Kickoff (Home landing composer) starts a NEW session via
                // create_session (LoaderSystem opens it as a tab). A normal
                // composer routes through the STREAMING path when the backend
                // supports it (the mock does), so the reply fills in
                // token-by-token; otherwise fall back to the synchronous
                // one-shot path (no regression). All are one-shot flags
                // serviced by LoaderSystem; setting only one per turn keeps
                // them mutually exclusive.
                if (kickoff)
                    app.requestKickoffPrompt = replyDraft;
                else if (canStream)
                    app.requestStreamPrompt = replyDraft;
                else
                    app.requestSendPrompt = replyDraft;
                remember_sent(replyDraft);
                replyDraft.clear();
            }
        }

        // The menu itself, drawn ABOVE the strip: the composer is pinned to
        // the bottom of the window, so a dropdown "below the input" would be
        // off-screen. A sibling of the composer bar under the same uiRoot
        // parent, in the same screen coordinates, on a layer over it.
        if (slashOpen && absX >= 0.0f && absY >= 0.0f) {
            constexpr float kRowH = 22.0f;
            const float menuH = kRowH * static_cast<float>(slashRows.size());
            auto menu = div(ctx, mk(parent, 3100),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(inputW), pixels(menuH)})
                    .with_absolute_position()
                    .with_translate(absX + composerGutter,
                                    absY - menuH - 6.0f)
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_custom_background(theme::panel_bg_2())
                    .with_border(theme::border(), pixels(1.0f))
                    .with_roundness(0.25f)
                    .with_render_layer(7)
                    .with_debug_name("slash_menu"));
            for (size_t i = 0; i < slashRows.size(); ++i) {
                const hanabi::slash::Command& c = *slashRows[i];
                std::string label = "/" + std::string(c.name);
                if (!c.arg.empty()) label += " " + std::string(c.arg);
                label += "   " + std::string(c.blurb);
                // An unwired verb says so in the row, so the menu is not a
                // list of promises.
                if (!c.runnable)
                    label += " \xc2\xb7 " + std::string(c.unwired);
                const bool selected = static_cast<int>(i) ==
                                      app.slashMenuIndex;
                auto row = button(ctx, mk(menu.ent(), static_cast<int>(i)),
                    ComponentConfig{}
                        .with_label(label)
                        .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                        .with_custom_background(selected
                                                    ? theme::selected_bg()
                                                    : theme::panel_bg_2())
                        .with_custom_hover_bg(
                            theme::hover_over(theme::panel_bg_2()))
                        .with_custom_text_color(c.runnable
                                                    ? theme::text_primary()
                                                    : theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_padding(Padding{.left = pixels(10)})
                        .with_click_activation(ClickActivationMode::Press)
                        .with_roundness(0.0f)
                        .with_render_layer(7)
                        .with_debug_name("slash_item_" + std::to_string(i)));
                if (row) choose_slash(static_cast<int>(i));
            }
        }
        lastSlashDraft = replyDraft;

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
        // Left: the model chip, which opens the picker. It used to read
        // "Opus 4.8 (xhigh)" whatever the app was set to — a label, not a
        // fact. It now says which model the next conversation will ask for
        // (Settings' defaultModelId, the same value the settings sheet's
        // Model row holds).
        // The chips share a cluster: the row is SpaceBetween, so three direct
        // children spread evenly and the effort chip drifts into the middle of
        // the strip instead of sitting beside the model it qualifies.
        auto leftMeta = div(ctx, mk(meta.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(16)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_leftmeta"));

        const std::string currentModel = Settings::get().get_default_model();
        auto modelChip = button(ctx, mk(leftMeta.ent(), 1),
            ComponentConfig{}
                .with_label(hanabi::models::display_name(currentModel))
                .with_size(ComponentSize{children(), pixels(16)})
                .with_padding(Padding{.top = pixels(1), .right = pixels(8),
                                      .bottom = pixels(1), .left = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_alignment(TextAlignment::Left)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.5f)
                .with_debug_name("composer_model"));
        if (modelChip) app.modelPopoverOpen = !app.modelPopoverOpen;
        if (app.escape == EscapeIntent::CloseModelPicker)
            app.modelPopoverOpen = false;
        render_model_popover(ctx, parent, app, modelChip.ent(), currentModel);

        // The effort chip, right of the model: how hard the model is asked to
        // think on the work you start next.
        const std::string currentEffort = Settings::get().get_default_effort();
        auto effortChip = button(ctx, mk(leftMeta.ent(), 2),
            ComponentConfig{}
                .with_label("Effort: " +
                            hanabi::effort::display_name(currentEffort))
                .with_size(ComponentSize{children(), pixels(16)})
                .with_margin(Margin{.left = pixels(6)})
                .with_padding(Padding{.top = pixels(1), .right = pixels(8),
                                      .bottom = pixels(1), .left = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_alignment(TextAlignment::Left)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.5f)
                .with_debug_name("composer_effort"));
        if (effortChip) app.effortPopoverOpen = !app.effortPopoverOpen;
        if (app.escape == EscapeIntent::CloseEffortPicker)
            app.effortPopoverOpen = false;
        render_effort_popover(ctx, parent, app, effortChip.ent(),
                              currentEffort);

        // The tool-fold chip, right of effort: how much of a tool call this
        // thread shows by default. Only where there is a thread to set it on —
        // on the welcome screen there is no session to key the mode to.
        if (app.openSession) {
            const hanabi::fold::Mode currentFold = fold_mode(app);
            auto foldChip = button(ctx, mk(leftMeta.ent(), 3),
                ComponentConfig{}
                    .with_label("Tools: " +
                                hanabi::fold::chip_label(currentFold))
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.left = pixels(6)})
                    .with_padding(Padding{.top = pixels(1),
                                          .right = pixels(8),
                                          .bottom = pixels(1),
                                          .left = pixels(8)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_alignment(TextAlignment::Left)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.5f)
                    .with_debug_name("composer_fold"));
            if (foldChip) app.foldPopoverOpen = !app.foldPopoverOpen;
            if (app.escape == EscapeIntent::CloseFoldPicker)
                app.foldPopoverOpen = false;
            render_fold_popover(ctx, parent, app, foldChip.ent(), currentFold);
        }
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
        if (!app.slashNotice.empty())
            caption = app.slashNotice;
        else if (!canSend)
            caption =
                "read-only \xe2\x80\x94 this backend doesn't support replies";
        else if (sending && queued > 0)
            caption = "sending\xe2\x80\xa6  \xc2\xb7  " +
                      std::to_string(queued) + " queued";
        else if (sending)
            caption = "sending\xe2\x80\xa6";
        else if (queued > 0)
            caption = std::to_string(queued) + " queued";
        else if (hasText) {
            // Discoverability: when there's text to send and we're idle, tell
            // the user which key sends (the fix for "HOW DO I SEND A MESSAGE" —
            // the composer now sends on a keystroke, not just the button
            // click). The hint names the key that is CONFIGURED: telling
            // someone who moved the setting to Cmd+Return that Enter sends is
            // worse than saying nothing, because they will believe it.
            // (canSend is provably true here — the !canSend arm returned above.)
            const char* key = Settings::get().get_send_key() ==
                                      hanabi::kSendKeyCmdReturn
                                  ? "Cmd+Return"
                                  : "Enter";
            caption = std::string(key) + (steerMode ? " to steer" : " to send");
        }
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
        // A live selection says how much is on the clipboard's doorstep. It
        // also confirms the selection exists at all: the band is drawn behind
        // text and easy to miss on a short run.
        if (const std::string sel = hanabi::text_select::selected_text();
            !sel.empty()) {
            div(ctx, mk(rightMeta.ent(), 4),
                ComponentConfig{}
                    .with_label(std::to_string(sel.size()) + " selected")
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.right = pixels(10)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Right)
                    .with_debug_name("composer_selected"));
        }
        // Conversation size against the budget that will compact it. The
        // numerator is the provider's own count when the backend reports one
        // and a chars/4 estimate otherwise, and only the estimate wears a "~".
        // The denominator is the session's compaction budget, or the declared
        // one for a backend that reports none; with neither, the bar is absent
        // rather than filled to something invented.
        if (canSend && app.openSession) {
            const api::ContextUsage& usage = app.openSession->context;
            const bool counted = usage.counted();
            const int64_t tok =
                counted ? usage.used_tokens : estimated_tokens(*app.openSession);
            const int64_t budget = usage.has_denominator()
                                       ? usage.budget_tokens
                                       : app.configuredContextBudget;
            if (tok > 0) {
                std::string label = counted
                                        ? fmtutil::compact_count(tok)
                                        : "~" + fmtutil::compact_count(tok);
                if (budget > 0)
                    label += " / " + fmtutil::compact_count(budget);
                label += " tokens";
                // A reading the server has not caught up with says so. Hiding
                // it would present a stale number as a live one.
                if (usage.stale) label += " \xc2\xb7 stale";
                div(ctx, mk(rightMeta.ent(), 2),
                    ComponentConfig{}
                        .with_label(label)
                        .with_size(ComponentSize{children(), pixels(16)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_faint())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Right)
                        .with_debug_name("composer_size"));
                if (budget > 0) {
                    const float frac =
                        std::min(1.0f, static_cast<float>(tok) /
                                           static_cast<float>(budget));
                    div(ctx, mk(rightMeta.ent(), 3),
                        ComponentConfig{}
                            .with_size(ComponentSize{pixels(56), pixels(6)})
                            .with_margin(Margin{.left = pixels(6)})
                            .with_custom_background(theme::panel_bg_2())
                            .with_roundness(0.5f)
                            .with_on_draw_fg([frac](RectangleType rr) {
                                float w = rr.width * frac;
                                if (w < 2.0f) w = 2.0f;
                                afterhours::draw_rectangle_rounded(
                                    RectangleType{rr.x, rr.y, w, rr.height},
                                    0.5f, 6,
                                    theme::over(theme::accent(),
                                                theme::panel_bg_2()));
                            })
                            .with_debug_name("composer_meter"));
                }
            }
        }
    }

    // A rough size for the open conversation, in tokens, for a backend that
    // reports no count of its own. Counts the characters the thread actually
    // carries — message bodies plus captured tool output — and divides by four,
    // the usual English rule of thumb. Deliberately approximate and labelled
    // "~": an exact count needs the tokenizer the model uses, which is the
    // backend's to know, and a wrong precise number is worse than an honest
    // estimate.
    static int64_t estimated_tokens(const api::Session& s) {
        size_t chars = 0;
        for (const auto& m : s.messages) {
            chars += m.text.size();
            chars += m.tool_result.size();
        }
        return static_cast<int64_t>(chars / 4);
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
    // Line counting used to divide by a fudged average glyph width, calibrated
    // to over-estimate so a box never clipped. Two things were wrong with it:
    // it guessed where afterhours would break a line, and the constant was
    // tuned for ONE font while hanabi lets the reader pick another.
    //
    // afterhours exposes its own wrapper now (ui::wrap_text), so the count is
    // measured with the same function and the same font the renderer draws
    // with -- they agree by construction rather than by calibration.
    static constexpr float kLinePitch = 16.0f;  // px per wrapped line
    // Blank-line (paragraph / list-item gap) height. This is the vertical space
    // between paragraphs and numbered-list items in an assistant turn. Was
    // kLinePitch*0.5 (8px) which stacked into a loose, airy feed vs navi web's
    // tighter spacing (Gabe: "the whitespace is still way too high"). 5px reads
    // as a clear paragraph break without the big gap. ONE constant so the
    // measure + render paths can never drift.
    static constexpr float kBlankPitch = 5.0f;
    // The width afterhours actually wraps within: the label rect less the
    // horizontal inset the renderer applies. (Not wrap_width() above -- that
    // one is the pane's width, a different question.)
    static float text_wrap_width(float widthPx) {
        const float w = widthPx - 10.0f;
        return w < 24.0f ? 24.0f : w;
    }

    // Real wrapped lines for `text` at `widthPx`, via afterhours' own wrapper.
    // `fontPx` is a parameter because a run set larger than the body — a
    // markdown heading — breaks at a different word than the same string at
    // body size, and measuring it at BODY would under-count its lines.
    static std::vector<std::string> wrapped_lines(
        const std::string& text, float widthPx,
        float fontPx = theme::type::BODY) {
        return afterhours::ui::wrap_text(text, text_wrap_width(widthPx),
                                         afterhours::ui::UIComponent::DEFAULT_FONT,
                                         fontPx);
    }

    // Estimated WRAPPED line count of `text` at `widthPx`. Used to decide
    // whether a body is long enough to fold.
    static int count_lines(const std::string& text, float widthPx,
                           float fontPx = theme::type::BODY) {
        // wrap_text honours hard newlines itself, so a blank line still counts
        // as a line the way the old hand-rolled split did.
        const int lines =
            static_cast<int>(wrapped_lines(text, widthPx, fontPx).size());
        return lines < 1 ? 1 : lines;
    }

    // Return the first `maxLines` WRAPPED lines of `text` (approx: we cut on
    // newline boundaries and, within a long unbroken line, on perLine chars).
    // Used to render a folded preview of a very long message so a huge paste
    // doesn't build thousands of glyph quads (RAM) or dominate the pane.
    static std::string first_n_lines(const std::string& text, float widthPx,
                                     int maxLines) {
        std::string out;
        int used = 0;
        size_t start = 0;
        while (start <= text.size() && used < maxLines) {
            size_t nl = text.find('\n', start);
            size_t end = (nl == std::string::npos) ? text.size() : nl;
            std::string seg = text.substr(start, end - start);
            // Real wrapped extent of this segment, not a length/width guess.
            int segLines = seg.empty() ? 1 : count_lines(seg, widthPx);
            if (used + segLines > maxLines) {
                int allow = maxLines - used;
                // Keep the first `allow` measured lines of this segment rather
                // than guessing a character count for them.
                const std::vector<std::string> wl = wrapped_lines(seg, widthPx);
                if (static_cast<int>(wl.size()) > allow) {
                    std::string cut;
                    for (int i = 0; i < allow; ++i) {
                        if (i) cut += " ";
                        cut += wl[static_cast<size_t>(i)];
                    }
                    seg = cut;
                }
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

    // ---- Markdown headings (H1-H4) --------------------------------------
    // An assistant that structures a long answer writes "## Findings", and the
    // transcript printed the hashes as if they were prose. Detection + geometry
    // live here so the measure pass (rich_body_h) and the draw (render_rich_body)
    // read the SAME numbers: a heading is taller than the body line it replaces,
    // and a height only one of them knows about desyncs every virtualization
    // spacer below it.
    //
    // ATX only ("## Text"), 1-4 hashes, at column 0, with a space after them.
    // Level 5 and 6 stay body text: four sizes already reach body size, and a
    // fifth step would be a heading nobody can see is one. Returns 0 for
    // "not a heading".
    static int md_heading_level(const std::string& line) {
        size_t i = 0;
        while (i < line.size() && line[i] == '#') ++i;
        if (i == 0 || i > 4) return 0;
        if (i >= line.size() || line[i] != ' ') return 0;
        if (line.find_first_not_of(' ', i) == std::string::npos) return 0;
        return static_cast<int>(i);
    }
    // The heading's own text: hashes, the space after them, and a closing run
    // of hashes ("## Text ##") removed. Inline markers are resolved the same
    // way a body line's are, so "## **Done**" is not drawn with its asterisks.
    static std::string md_heading_text(const std::string& line) {
        size_t b = line.find_first_not_of('#');
        if (b == std::string::npos) return "";
        b = line.find_first_not_of(' ', b);
        if (b == std::string::npos) return "";
        size_t e = line.find_last_not_of(" \t");
        while (e > b && line[e] == '#') --e;
        while (e > b && (line[e] == ' ' || line[e] == '\t')) --e;
        return md_visible(line.substr(b, e - b + 1));
    }
    // The heading type scale. Not theme::type tokens: those are named for the
    // places they came from (a smart-view h1, a spotlight input), and a scale
    // needs its four steps to be chosen against each other — each step has to
    // be far enough from the next to read as a level, and H4 has to stay
    // clearly above BODY (13) so the smallest heading is still a heading.
    static constexpr float kHeadingFont[4] = {20.0f, 17.0f, 15.0f, 13.5f};
    static float heading_font(int level) {
        if (level < 1) level = 1;
        if (level > 4) level = 4;
        return kHeadingFont[level - 1];
    }
    // Line pitch as arithmetic on the font size rather than a font measurement:
    // measure and render must agree even on the first frame, before the font
    // context exists, and a measured pitch would answer differently then.
    static float heading_line_h(int level) {
        return std::round(heading_font(level) * 1.35f);
    }
    // Space above a heading, drawn as its own leading gap. More above than
    // below so the heading binds to the section it opens instead of floating
    // between two of them.
    static constexpr float kHeadingGapTop = 8.0f;
    static constexpr float kHeadingPadV = 2.0f;  // inside the text row
    // Total height of a heading segment: the leading gap plus the text row.
    // The ONE place either path gets this number.
    static float heading_seg_h(int level, int lines) {
        if (lines < 1) lines = 1;
        return kHeadingGapTop + static_cast<float>(lines) *
                                    heading_line_h(level) +
               2.0f * kHeadingPadV;
    }
    // Measured height of the heading on `line` when wrapped to `textW`.
    static float heading_seg_h_for(const std::string& line, float textW) {
        const int level = md_heading_level(line);
        const std::string text = md_heading_text(line);
        return heading_seg_h(level,
                             count_lines(text, textW, heading_font(level)));
    }

    // Total pixel height of `render_rich_body(body, textW)` — MUST mirror that
    // method's per-segment layout exactly (blank line = half pitch, else
    // segLines*pitch) so virtualization spacers line up with what renders.
    static float rich_body_h(const std::string& body, float textW) {
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
            // ---- Heading: one segment, taller than a body line -------------
            if (md_heading_level(line) > 0) {
                h += heading_seg_h_for(line, textW);
                if (nl == std::string::npos) break;
                start = nl + 1;
                continue;
            }
            // VISIBLE length (markers removed) so inline **bold**/`code`/_em_
            // don't inflate the wrapped line count — must match render, which
            // wraps the same visible text (md_to_spans).
            const std::string vis = md_visible(line);
            if (vis.empty()) {
                h += kBlankPitch;
            } else {
                // Measured, not divided: must mirror render_rich_body below.
                h += static_cast<float>(count_lines(vis, textW)) * kLinePitch;
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return h;
    }

    // Renderer-accurate height for a SINGLE wrapped box (user bubble / tool).
    //
    // This used to say afterhours "wraps on spaces only and treats \n as a
    // char", and counted length/perLine accordingly. That stopped being true:
    // the wrapper honours hard newlines now, so a three-line message measured
    // as one line and the bubble clipped. Measuring with the wrapper itself
    // cannot drift from it again.
    static float flat_body_h(const std::string& body, float textW) {
        return static_cast<float>(count_lines(body, textW)) * kLinePitch +
               2.0f * kBodyPad;
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
                r.body += " |";
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
        // A folded message shows its first few lines, so a match below the
        // fold is counted and cannot be painted. Rather than exclude those
        // matches — they are real, and hiding them would make find useless on
        // exactly the long messages it is for — a live query unfolds any
        // message that contains one.
        // An operator that excludes this row excludes it here too: unfolding
        // a message find will not highlight would open it for nothing.
        if (app && app->findOpen &&
            message_has_match(m, paint_query_for(index)))
            return false;
        const std::string mkey =
            m.id.empty() ? ("msg" + std::to_string(index)) : m.id;
        return !(app && app->expandedMsgs.count(mkey) != 0);
    }

    static bool message_has_match(const api::Message& m,
                                  const std::string& q) {
        if (q.empty()) return false;
        if (m.role != api::Role::User && m.role != api::Role::Assistant)
            return false;
        for (const auto& line : paintable_lines(m, m.role != api::Role::User))
            if (!textscan::occurrences(line, q).empty()) return true;
        return false;
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
                   kTurnGapBot + kMsgActionsGap + kMsgActionsH;
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
                  kTurnGapBot + kMsgActionsGap + kMsgActionsH;
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
            // Same in-place confirmation as a message's Copy: a press that
            // changes nothing on screen reads as a press that did nothing.
            const std::string ckey = "code:" + std::to_string(id);
            const bool ccopied = recently_copied(ckey);
            auto copy = button(ctx, mk(bar.ent(), 3),
                ComponentConfig{}
                    .with_label(ccopied ? "Copied" : "Copy")
                    .with_size(ComponentSize{pixels(copyW), pixels(15)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(ccopied ? theme::status_active()
                                                    : theme::text_secondary())
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
                copied_key() = ckey;
                copied_at() = std::chrono::steady_clock::now();
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
        // Elapsed seconds since the turn began. During a headless capture the
        // clock is frozen, so this subtracts two readings of the same instant
        // and the photographed number is the one the demo asked for.
        long elapsed = 0;
        if (app && app->streamStartedAt > 0) {
            long now = static_cast<long>(capture_clock::now());
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
                    const double t = capture_clock::anim_time(
                        afterhours::graphics::get_time());
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

    // Draw one markdown heading: a leading gap, then the text row at its
    // level's size in the accent colour. Height comes from heading_seg_h_for
    // — the same call the measure pass makes — and is split between the two
    // elements here, so what this builds occupies exactly what was measured.
    void render_heading(UIContext<InputAction>& ctx, Entity& parent, int seg,
                        int level, const std::string& text, float blockH,
                        const std::string& findQuery) {
        div(ctx, mk(parent, 20000 + seg),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(kHeadingGapTop)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("md_heading_gap"));
        const float fontPx = heading_font(level);
        const float rowH = blockH - kHeadingGapTop;
        auto cfg = ComponentConfig{}
                       .with_label(text)
                       .with_size(ComponentSize{percent(1.0f), pixels(rowH)})
                       .with_transparent_bg()
                       .with_custom_text_color(theme::accent())
                       .with_font_size(fontPx)
                       .with_text_overflow(TextOverflow::Wrap)
                       .with_alignment(TextAlignment::Left)
                       .with_roundness(0.0f)
                       .with_debug_name("md_h" + std::to_string(level));
        // Same two bands a body line gets, at the heading's font size: a
        // heading the reader can see but not select, or that find skips over,
        // is a hole in features that already work everywhere else.
        auto idHolder = std::make_shared<afterhours::EntityID>(-1);
        cfg = cfg.with_on_draw_bg(
            [text, q = findQuery, idHolder, fontPx](RectangleType r) {
                hanabi::text_select::draw(*idHolder, r, text, fontPx);
                if (!q.empty())
                    hanabi::find_highlight::draw(r, text, q, fontPx);
            });
        auto el = div(ctx, mk(parent, 100 + seg), cfg);
        *idHolder = el.ent().id;
        selectable_text(ctx, el.ent(), text, fontPx);
    }

    void render_rich_body(UIContext<InputAction>& ctx, Entity& parent,
                          const std::string& shown, float textW,
                          float winTop = 0.0f, float winBot = -1.0f,
                          float bodyStartY = 0.0f,
                          const std::string& findQuery = std::string()) {
        // The find text arrives from the caller, which is the only level that
        // knows WHICH message this body belongs to — and therefore whether an
        // operator has excluded it from the search.
        const bool cull = winBot > winTop;
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

            // ---- Heading: ONE atomic segment (leading gap + text row) ------
            if (const int level = md_heading_level(line); level > 0) {
                const float blockH = heading_seg_h_for(line, textW);
                const float segTop = y;
                const float segBot = y + blockH;
                y = segBot;
                const bool visible =
                    !cull || (segBot >= winTop && segTop <= winBot);
                if (!visible) {
                    pending += blockH;
                } else {
                    flush(9000 + seg);
                    render_heading(ctx, parent, seg, level,
                                   md_heading_text(line), blockH, findQuery);
                }
                ++seg;
                if (nl == std::string::npos) break;
                start = nl + 1;
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
                // Mirrors rich_body_h's measure exactly -- same function,
                // same width, so the spacers cannot drift from the render.
                segLines = count_lines(ip.visible, textW);
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
                auto cfg = ComponentConfig{}
                        .with_label(ip.visible)
                        .with_styled_label(ip.spans)
                        .with_size(ComponentSize{percent(1.0f), pixels(segH)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_primary())
                        .with_font_size(theme::type::BODY)
                        .with_text_overflow(TextOverflow::Wrap)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_debug_name("asst_line");
                // Both bands go BEHIND the glyphs: on_draw_bg runs before the
                // widget's own fill, and this element's fill is transparent.
                // The element's id is not known until it exists, so the draw
                // captures it by reference through a small holder.
                auto idHolder = std::make_shared<afterhours::EntityID>(-1);
                cfg = cfg.with_on_draw_bg(
                    [line = ip.visible, q = findQuery, idHolder](
                        RectangleType r) {
                        hanabi::text_select::draw(*idHolder, r, line,
                                                  theme::type::BODY);
                        if (!q.empty())
                            hanabi::find_highlight::draw(r, line, q,
                                                         theme::type::BODY);
                    });
                auto lineEl = div(ctx, mk(parent, 100 + seg), cfg);
                *idHolder = lineEl.ent().id;
                selectable_text(ctx, lineEl.ent(), ip.visible,
                                theme::type::BODY);
            }
            ++seg;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        flush(8888);
    }

    // ---- Per-message actions (hover) --------------------------------------
    // afterhours has no text selection on read-only labels (see
    // afterhours_gaps.md #36), so there is no way to drag across an answer and
    // copy it. A per-message Copy is the affordance that replaces it: reserved
    // in the layout on every turn, painted only while the pointer is over that
    // turn, and confirmed in place for a moment after a click.
    static constexpr float kMsgActionsH = 22.0f;
    static constexpr float kMsgActionsGap = 2.0f;

    // Which message last had Copy pressed, and when — so the button can read
    // "Copied" for a beat instead of silently doing nothing visible.
    static std::string& copied_key() {
        static std::string k;
        return k;
    }
    static std::chrono::steady_clock::time_point& copied_at() {
        static std::chrono::steady_clock::time_point t{};
        return t;
    }
    static bool recently_copied(const std::string& key) {
        if (copied_key() != key) return false;
        const auto age = std::chrono::steady_clock::now() - copied_at();
        return age < std::chrono::milliseconds(1600);
    }

    void message_actions(UIContext<InputAction>& ctx, Entity& turn,
                         int index, const std::string& key,
                         const std::string& rawText, bool alignRight,
                         int64_t sentAt = 0) {
        // mouse_was_in_subtree answers "is the pointer on this turn or on
        // anything inside it" from LAST frame's hit test — the tree being built
        // right now hasn't been resolved yet. Without the subtree form, moving
        // onto the Copy button would make the turn itself stop being hot and
        // the button would vanish under the cursor.
        const bool copied = recently_copied(key);
        const bool show = copied || ctx.mouse_was_in_subtree(turn.id) ||
                          hanabi::test_hooks::force_hover("msg:" + key);

        auto bar = div(ctx, mk(turn, 8),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kMsgActionsH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(alignRight ? JustifyContent::FlexEnd
                                                 : JustifyContent::FlexStart)
                .with_margin(Margin{.top = pixels(kMsgActionsGap)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("msg_actions"));
        if (!show) return;

        // On a user bubble the row runs right-to-left, so the time is emitted
        // FIRST to end up left of the button; on an assistant turn it trails.
        const std::string stamp =
            show_times() ? fmtutil::clock_time(sentAt) : std::string();
        auto time_chip = [&](int id) {
            if (stamp.empty()) return;
            div(ctx, mk(bar.ent(), id),
                ComponentConfig{}
                    .with_label(stamp)
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.right = pixels(8), .left = pixels(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Left)
                    .with_debug_name("msg_time"));
        };
        if (alignRight) time_chip(2);

        auto btn = div(ctx, mk(bar.ent(), 1),
            ComponentConfig{}
                .with_label(copied ? "Copied" : "Copy")
                .with_size(ComponentSize{pixels(56), pixels(18)})
                .with_padding(Padding{.right = pixels(6), .left = pixels(6)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_custom_text_color(copied ? theme::status_active()
                                               : theme::text_faint())
                .with_font_size(theme::type::MICRO)
                .with_alignment(TextAlignment::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.35f)
                .with_debug_name("msg_copy_btn"));
        btn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (btn.ent().get<afterhours::ui::HasClickListener>().down) {
            afterhours::clipboard::set_text(rawText);
            copied_key() = key;
            copied_at() = std::chrono::steady_clock::now();
        }
        if (!alignRight) time_chip(2);
        (void)index;
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
            auto uturn = div(ctx, mk(parent, 200 + index * 10),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_margin(Margin{.top = pixels(kTurnGapTop + 10.0f),
                                        .right = pixels(0),
                                        .bottom = pixels(kTurnGapBot),
                                        .left = pixels(0)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("user_turn"));
            // A turn is only hit-tested (and so only reports hover) once it
            // carries a listener — see ResolveHitTarget::is_candidate. The
            // listener does nothing; it exists to make the turn hoverable.
            uturn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            uturn.ent().get<afterhours::HasColor>().skip_hover_override = true;
            auto row = div(ctx, mk(uturn.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_justify_content(JustifyContent::FlexEnd)
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
                    .with_corner_radius(theme::kChatCorner)
                    .with_debug_name("user_bubble"));
            const std::string uq = paint_query_for(index);
            auto ucfg = ComponentConfig{}
                    .with_label(userBody)
                    .with_size(ComponentSize{percent(1.0f), pixels(bodyH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::BODY)
                    .with_text_overflow(TextOverflow::Wrap)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("user_text");
            auto uidHolder = std::make_shared<afterhours::EntityID>(-1);
            ucfg = ucfg.with_on_draw_bg(
                [t = userBody, q = uq, uidHolder](RectangleType r) {
                    hanabi::text_select::draw(*uidHolder, r, t,
                                              theme::type::BODY);
                    if (!q.empty())
                        hanabi::find_highlight::draw(r, t, q,
                                                     theme::type::BODY);
                });
            auto uEl = div(ctx, mk(bub.ent(), 2), ucfg);
            *uidHolder = uEl.ent().id;
            selectable_text(ctx, uEl.ent(), userBody, theme::type::BODY);
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
            message_actions(ctx, uturn.ent(), index,
                            m.id.empty() ? ("msg" + std::to_string(index))
                                         : m.id,
                            m.text, /*alignRight=*/true, m.created_at);
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
        // Hoverable only because of this listener (ResolveHitTarget skips
        // anything without one); it deliberately does nothing.
        turn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        turn.ent().get<afterhours::HasColor>().skip_hover_override = true;

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
        // "streaming…" survives the timestamps preference: it says the turn
        // is still arriving, which is state, not a stamp.
        std::string ts = isLive ? std::string("streaming\xe2\x80\xa6")
                                : (show_times()
                                       ? fmtutil::relative_time(m.created_at)
                                       : std::string());
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
                             bodyStartY, paint_query_for(index));
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
        message_actions(ctx, turn.ent(), index, mkey, m.text,
                        /*alignRight=*/false, m.created_at);
    }

    // A System message: a quiet, centered, muted caption — conversation
    // metadata (a session boundary / mode note), NOT a dialogue bubble.
    void render_meta_line(UIContext<InputAction>& ctx, Entity& parent,
                          int index, const api::Message& m) {
        std::string txt = m.text;
        std::string age =
            show_times() ? fmtutil::relative_time(m.created_at) : std::string();
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

    // ---- Diff colouring (src/util/diff.h decides WHAT a line is) ---------
    //
    // The hues are the theme's existing on-background status pair — the red
    // and green already tuned to read as red and green on BOTH palettes (see
    // the status_blocked / status_review note in theme.h). A diff does not
    // need a second opinion about what red means, and inventing a token per
    // feature is how a palette stops being one.
    static theme::Color diff_line_fg(hanabi::diff::LineKind kind) {
        switch (kind) {
            case hanabi::diff::LineKind::Added: return theme::status_review();
            case hanabi::diff::LineKind::Removed: return theme::status_blocked();
            case hanabi::diff::LineKind::Hunk: return theme::accent();
            case hanabi::diff::LineKind::Meta: return theme::text_faint();
            case hanabi::diff::LineKind::Context: break;
        }
        return theme::text_secondary();
    }
    // The band behind a changed row. Pre-composited over the panel fill
    // because a UI rect cannot alpha-blend (afterhours gap #13): passing a
    // low-alpha token straight in paints a harsh near-opaque block.
    static std::optional<theme::Color> diff_line_bg(
        hanabi::diff::LineKind kind) {
        const auto wash = [](theme::Color c) {
            return theme::over(theme::Color{c.r, c.g, c.b, 28},
                               theme::window_bg());
        };
        switch (kind) {
            case hanabi::diff::LineKind::Added:
                return wash(theme::status_review());
            case hanabi::diff::LineKind::Removed:
                return wash(theme::status_blocked());
            default: break;
        }
        return std::nullopt;
    }
    // The row's debug name says what the line was read as. A test can assert a
    // name; it cannot assert a colour (the harness has no colour property), so
    // the name IS how the classification is checked from outside.
    static std::string diff_line_name(hanabi::diff::LineKind kind, size_t i,
                                      const std::string& prefix = "tool_out") {
        const std::string idx = "_" + std::to_string(i);
        switch (kind) {
            case hanabi::diff::LineKind::Added: return prefix + "_add" + idx;
            case hanabi::diff::LineKind::Removed: return prefix + "_del" + idx;
            case hanabi::diff::LineKind::Hunk: return prefix + "_hunk" + idx;
            case hanabi::diff::LineKind::Meta: return prefix + "_meta" + idx;
            case hanabi::diff::LineKind::Context: break;
        }
        return prefix + "_line" + idx;
    }

    // A single tool block is expandable IF it has captured output (tool_result)
    // to show. Keyed in expandedPiles by the message id (shared with piles).
    static bool tool_block_expandable(const api::Message& m) {
        return !m.tool_result.empty();
    }

    // ---- Tool fold defaults (src/ui/fold_menu.h) --------------------------
    // The mode belongs to the thread being rendered, and split view swaps
    // openSession per pane, so reading it off openSession answers per pane.
    static hanabi::fold::Mode fold_mode(AppComponent& app) {
        if (!app.openSession) return hanabi::fold::kDefault;
        return hanabi::fold::from_int(
            Settings::get().get_tool_fold(app.openSession->summary.id));
    }
    // Auto's rule: a short captured result is worth the space, a long one is a
    // log. No result at all means there is nothing to open.
    static bool auto_opens(const api::Message& m) {
        return !m.tool_result.empty() &&
               m.tool_result.size() < hanabi::fold::kAutoResultChars;
    }
    // The result Auto judges a PILE by: the first call in it that captured
    // one. `msgs[lo]` may have run silently while the call after it is the one
    // with output, and folding on that would be judging a row by a result it
    // does not have.
    static const api::Message& pile_result_row(
        const std::vector<api::Message>& msgs, int lo, int hi) {
        for (int k = lo; k < hi; ++k)
            if (!msgs[k].tool_result.empty()) return msgs[k];
        return msgs[lo];
    }
    // The ONE answer to "is this tool row open" — read by the measure pass and
    // by the draw. A second copy of this decision is precisely how a row
    // measures one height and paints another, which desyncs every
    // virtualization spacer below it.
    static bool tool_is_open(AppComponent& app, const std::string& key,
                             const api::Message& resultRow) {
        if (!key.empty() && app.collapsedPiles.count(key) != 0) return false;
        if (!key.empty() && app.expandedPiles.count(key) != 0) return true;
        switch (fold_mode(app)) {
            case hanabi::fold::Mode::Expand: return true;
            case hanabi::fold::Mode::Auto: return auto_opens(resultRow);
            case hanabi::fold::Mode::Fold: break;
        }
        return false;
    }
    // A click records what the reader wants for THAT row, as an override on
    // the mode — so closing one row under "Expand all" does not silently
    // demote the whole thread back to folded.
    static void tool_toggle(AppComponent& app, const std::string& key,
                            bool wasOpen) {
        if (key.empty()) return;
        app.expandedPiles.erase(key);
        app.collapsedPiles.erase(key);
        if (wasOpen) app.collapsedPiles.insert(key);
        else app.expandedPiles.insert(key);
    }

    // Max output lines shown when a single tool block is expanded (keeps a huge
    // dump from dominating the pane; the row stays a peek, not the full log).
    static constexpr int kToolOutLines = 8;

    // The lines the expanded panel shows — the ONE list, built once and read
    // by both the measure (tool_out_height, below) and the draw
    // (render_tool_block). It used to be two walks of the same string that
    // happened to agree: the measure counted newlines and capped the count,
    // the draw cut the string at the Nth newline and split what was left. Two
    // readings of one text is exactly how a panel ends up a row taller than
    // the space reserved for it, and every virtualization spacer below it
    // moves. Now there is nothing to disagree about — the height is this
    // vector's size, and the rows are its elements.
    static std::vector<std::string> tool_out_lines(const api::Message& m,
                                                   int cap) {
        std::vector<std::string> out;
        size_t start = 0;
        const std::string& text = m.tool_result;
        if (text.empty()) return out;
        while (start <= text.size() && out.size() < static_cast<size_t>(cap)) {
            const size_t nl = text.find('\n', start);
            std::string line =
                text.substr(start, (nl == std::string::npos ? text.size() : nl) -
                                       start);
            // Tabs -> 2 spaces for stable columns (same as code lines).
            for (size_t t = line.find('\t'); t != std::string::npos;
                 t = line.find('\t', t))
                line.replace(t, 1, "  ");
            out.push_back(std::move(line));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return out;
    }

    // Height of the expanded output panel for a single tool block (0 if not
    // The expanded output panel's own box height (0 if not expanded / no
    // output). It counts the SAME vector render_tool_block draws from, so the
    // two cannot disagree, and it asks tool_is_open so the session's fold mode
    // decides what counts as open rather than a second copy of that rule.
    float tool_out_box_h(AppComponent& app, const api::Message& m) {
        if (!tool_block_expandable(m)) return 0.0f;
        const std::string key = m.id.empty() ? "" : m.id;
        if (!tool_is_open(app, key, m)) return 0.0f;
        return static_cast<float>(tool_out_lines(m, kToolOutLines).size()) *
                   kLinePitch +
               12.0f;  // + panel pad
    }
    // The space that panel takes in the column: its box plus its own bottom
    // margin, which the measure used to leave out (same 4px omission as
    // sub_out_height, found the same way).
    float tool_out_height(AppComponent& app, const api::Message& m) {
        const float box = tool_out_box_h(app, m);
        return box <= 0.0f ? 0.0f : box + kToolRowGap;
    }
    float tool_block_height(AppComponent& app, const api::Message& m) {
        return kToolRowGap + kToolRowH + kToolRowGap + tool_out_height(app, m);
    }
    float tool_pile_height(AppComponent& app,
                           const std::vector<api::Message>& msgs, int lo,
                           int hi) {
        const std::string key =
            msgs[lo].id.empty() ? ("pile" + std::to_string(lo)) : msgs[lo].id;
        const bool open = tool_is_open(app, key, pile_result_row(msgs, lo, hi));
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
                .with_corner_radius(theme::kChatCorner)
                .with_debug_name("tool_head"));
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(12), pixels(18)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([expandable, open](RectangleType r) {
                    if (expandable)
                        hanabi::glyph::chevron(r, !open, theme::text_faint(),
                                               3.2f);
                })
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
        const bool open =
            app && tool_is_open(*app, key, pile_result_row(msgs, lo, hi));

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
            tool_toggle(*app, key, open);
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
    // The sub-row's lines — the same ONE list the single-tool panel uses, at
    // the sub-row's tighter cap. It was a second pair of walks over the same
    // string (count the newlines here, split them there), which is the same
    // way to be wrong the single panel had: this panel's height is mirrored in
    // tool_pile_height, so a row more than was measured moves every spacer
    // below the pile.
    static int sub_out_lines(const api::Message& m) {
        return static_cast<int>(tool_out_lines(m, kSubOutLines).size());
    }
    // The output panel's own box height (what the draw asks for).
    static float sub_out_box_h(const api::Message& m) {
        int n = sub_out_lines(m);
        if (n <= 0) return 0.0f;
        return static_cast<float>(n) * (kLinePitch - 2.0f) + 8.0f + 4.0f;
    }
    // The space that panel takes in the column: its box PLUS its own bottom
    // margin. Mirrored in tool_pile_height so the virtualization spacers line
    // up. The margin used to be left out, so every expanded sub-row measured
    // 4px shorter than it drew and a pile of them threw the column off by a
    // multiple of that. Found by the measure-vs-draw probe.
    static float sub_out_height(const api::Message& m) {
        const float box = sub_out_box_h(m);
        return box <= 0.0f ? 0.0f : box + kToolRowGap;
    }
    // Compact output preview under a pile sub-row: a sunken mono panel with the
    // first kSubOutLines of the captured tool_result — the "tool details".
    void tool_sub_output(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const api::Message& m, float rowW) {
        const std::vector<std::string> lines = tool_out_lines(m, kSubOutLines);
        if (lines.empty()) return;
        auto panel = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW - 20.0f),
                                         pixels(sub_out_box_h(m))})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_custom_background(theme::window_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_padding(Padding{.top = pixels(4), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(10)})
                .with_margin(Margin{.bottom = pixels(4), .left = pixels(20)})
                .with_corner_radius(4.0f)
                .with_debug_name("sub_out"));
        // The rows are the vector sub_out_lines() counted, so the panel is
        // exactly as tall as what goes in it. A patch is coloured here too:
        // the edit tool usually arrives in a PILE with the calls around it,
        // which makes this — not the single-tool panel — the place its diff is
        // actually read.
        const bool isDiff = hanabi::diff::looks_like_diff(m.tool_result);
        for (size_t li = 0; li < lines.size(); ++li) {
            const std::string& line = lines[li];
            const hanabi::diff::LineKind kind =
                isDiff ? hanabi::diff::classify(line)
                       : hanabi::diff::LineKind::Context;
            auto cfg = ComponentConfig{}
                    .with_label(line.empty() ? " "
                                             : fmtutil::ellipsize(line, 90))
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels(kLinePitch - 2.0f)})
                    .with_custom_text_color(
                        isDiff ? diff_line_fg(kind) : theme::text_faint())
                    .with_font("mono", theme::type::MICRO)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name(diff_line_name(kind, li, "sub_out"));
            const std::optional<theme::Color> band = diff_line_bg(kind);
            if (band.has_value()) cfg = cfg.with_custom_background(*band);
            else cfg = cfg.with_transparent_bg();
            div(ctx, mk(panel.ent(), 1 + static_cast<int>(li)), cfg);
        }
    }

    // ---- Spawned sub-agent inline card -----------------------------------
    // A spawn is a Tool-role message whose tool name (subtitle) marks a
    // sub-agent launch. We detect it by name so the http adapter + mock both
    // work, and render a DISTINCT inline card (sparkle + "Spawned agent" +
    // the task) at the point in the transcript where it happened (Gabe: "add UI
    // for when a thing is spawned"), instead of a generic tool row.
    // ---------------- Thinking disclosure ----------------------------------
    //
    // The adapter marks a reasoning block by subtitle (agentcloud_client.cpp:
    // "it is not the answer"), and until now the transcript rendered it as an
    // ordinary assistant bubble — so the model's private reasoning read as
    // something it said to you, at full length, above the actual reply.
    //
    // It renders folded: one quiet row saying how much reasoning there is.
    // Open it and the text appears in the same dimmed treatment. The chevron
    // is drawn rather than typed (the font has no triangles, gap #48).
    static constexpr float kThinkingRowH = 26.0f;
    static constexpr float kThinkingInset = 22.0f;
    static constexpr float kThinkingPadBot = 8.0f;

    static bool is_thinking(const api::Message& m) {
        return m.role == api::Role::Assistant && m.subtitle == "thinking";
    }

    static std::string thinking_key(const api::Message& m, int index) {
        return m.id.empty() ? ("think" + std::to_string(index)) : m.id;
    }

    // Measure and draw read this one function, so a fold cannot desync the
    // virtualization spacers from what is painted.
    static float thinking_height(AppComponent& app, const api::Message& m,
                                 int index, float colW) {
        if (app.expandedThinking.count(thinking_key(m, index)) == 0)
            return kThinkingRowH;
        return kThinkingRowH +
               rich_body_h(strip_inline_md(m.text), colW - kThinkingInset) +
               kThinkingPadBot;
    }

    void render_thinking_block(UIContext<InputAction>& ctx, Entity& parent,
                               int index, const api::Message& m,
                               AppComponent& app, float colW) {
        const std::string key = thinking_key(m, index);
        const bool open = app.expandedThinking.count(key) != 0;

        auto wrap = div(ctx, mk(parent, 3400 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("thinking_block"));

        auto head = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW), pixels(kThinkingRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("thinking_head"));
        head.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (head.ent().get<afterhours::ui::HasClickListener>().down) {
            if (open) app.expandedThinking.erase(key);
            else app.expandedThinking.insert(key);
        }

        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(14), pixels(18)})
                .with_transparent_bg()
                .with_on_draw_fg([open](RectangleType r) {
                    hanabi::glyph::chevron(r, !open, theme::text_faint(), 3.2f);
                })
                .with_debug_name("thinking_chev"));

        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label("Thought for a moment")
                .with_size(ComponentSize{children(), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("thinking_summary"));

        if (!open) return;

        auto body = div(ctx, mk(wrap.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW - kThinkingInset),
                                         children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.left = pixels(kThinkingInset),
                                    .bottom = pixels(kThinkingPadBot)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("thinking_body"));
        render_rich_body(ctx, body.ent(), strip_inline_md(m.text),
                         colW - kThinkingInset);
    }

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
                .with_corner_radius(theme::kChatCorner)
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
                // Fill the remainder: content box = rowW - 24 (L/R pad); minus
                // icon(18) + icon margin(10) + status dot slot(16) = rowW - 68.
                // (Was rowW-180 for a 140px status slot that overflowed the card
                // by 12px every frame — the spawn_status NoWrap overflow spam.)
                .with_size(ComponentSize{pixels(rowW - 68.0f),
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
                .with_size(ComponentSize{pixels(16), pixels(18)})
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
            expandable && app && !key.empty() && tool_is_open(*app, key, m);
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
                tool_toggle(*app, key, open);
            }
        }
        if (open) {
            // Expanded output: a sunken monospace panel showing the first
            // kToolOutLines of the tool's captured result (a peek, not the full
            // log). The rows are tool_out_lines(m) — the same vector
            // tool_out_height() measured, so the panel cannot be the wrong
            // size for what goes in it.
            const std::vector<std::string> outLines =
                tool_out_lines(m, kToolOutLines);
            // An edit tool reports what it changed as a patch, and a patch
            // read in one flat grey is just text with punctuation. When the
            // output announces itself as a diff (a hunk header, or the
            // ---/+++ pair — never a leading "-" alone, which is also every
            // bullet list a tool ever printed), each row is coloured by what
            // it IS. See src/util/diff.h.
            const bool isDiff = hanabi::diff::looks_like_diff(m.tool_result);
            auto panel = div(ctx, mk(parent, 200 + index * 10 + 5),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(rowW),
                                             pixels(tool_out_box_h(*app, m))})
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
            for (size_t li = 0; li < outLines.size(); ++li) {
                const std::string& ln = outLines[li];
                const hanabi::diff::LineKind kind =
                    isDiff ? hanabi::diff::classify(ln)
                           : hanabi::diff::LineKind::Context;
                auto cfg = ComponentConfig{}
                        .with_label(ln.empty() ? " " : ln)
                        .with_size(ComponentSize{percent(1.0f),
                                                 pixels(kLinePitch)})
                        .with_custom_text_color(diff_line_fg(kind))
                        .with_font("mono", theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        // Named by what the line IS, so a scripted test can
                        // address "the second added line" — the classification
                        // is not otherwise observable from outside (the
                        // harness can assert geometry and text, never colour).
                        .with_debug_name(diff_line_name(kind, li));
                // A wash behind the whole row, not just the glyphs: a changed
                // line should be findable by scanning the block, which is what
                // a coloured band does and a coloured word does not.
                const std::optional<theme::Color> band = diff_line_bg(kind);
                if (band.has_value()) cfg = cfg.with_custom_background(*band);
                else cfg = cfg.with_transparent_bg();
                div(ctx, mk(outCol.ent(), 1 + static_cast<int>(li)), cfg);
            }
        }
    }

};

}  // namespace ecs
