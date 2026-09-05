#pragma once
#include <branding.h>

// Renders the main pane (right of the sidebar, below the tab strip). Dispatches
// on AppComponent::view: the smart views (Home / Blocked / Review / Starred)
// are digest lists over the thread set; Chat renders the active tab's
// transcript as message bubbles.

#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../api/disk_cache.h"
#include "../test_hooks.h"
#include "../util/capture_clock.h"
#include "../util/diff.h"
#include "../util/format.h"
#include "../util/textscan.h"
#include "keyboard_focus.h"
#include "digest_layout.h"
#include "home_buckets.h"
#include "pane_state.h"
#include "thread_model.h"
#include "../util/prof.h"
#include "../util/text_cache.h"
#include "../util/wrap_count.h"
#include "transcript_render_cache.h"
#include "../ui/field_chrome.h"
#include "../ui/find_highlight.h"
#include "../ui/find_nav.h"
#include "../ui/link_detect.h"
#include "../ui/md_spans.h"
#include "../ui/minimap.h"
#include "../ui/find_operators.h"
#include "../ui/text_select.h"
#include "../ui/inline_image.h"
#include "../ui/slash_commands.h"
#include "../ui/syntax_highlighter.h"
#include "../ui/model_menu.h"
#include "../ui/effort_menu.h"
#include "../ui/fold_menu.h"
#include "../ui/measure_probe.h"
#include "../ui/secondary_surface.h"
#include "../keys.h"
#include "../settings.h"
#include "line_draw_state.h"
#include "ui_imports.h"

#include "../../vendor/afterhours/src/plugins/clipboard.h"

namespace ecs {

namespace find_ops = hanabi::find_ops;

struct MainPaneSystem : afterhours::System<UIContext<InputAction>> {
    // The most cards any ONE Home section renders. See the note at the
    // sections themselves; the value is Recent's original cap, shared so the
    // four sections cannot drift apart again.
    static constexpr size_t kMaxSection = 20;
    static constexpr float kPageHeaderH = 46.0f;

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto& r = layout->main;
        const bool splitView = app->view == SmartView::Chat &&
                               app->splitOpen &&
                               split_fits(r.width - kDividerW);
        const float contentH =
            r.height + (splitView ? layout->composer.height : 0.0f);

        auto panel = div(ctx, mk(uiRoot, 2000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(contentH)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::chrome::content())
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
        if (app->escape == EscapeIntent::CloseFind) {
            Pane& target =
                app->pane().findOpen ? app->pane() : app->other_pane();
            target.findOpen = false;
            target.findQuery.clear();
            app->refocusComposer = true;
        }
        if (app->pane().findOpen && app->requestFindStep != 0) {
            const hanabi::find_nav::Step step =
                app->requestFindStep < 0 ? hanabi::find_nav::Step::Prev
                                         : hanabi::find_nav::Step::Next;
            const int count = std::abs(app->requestFindStep);
            app->requestFindStep = 0;
            for (int i = 0; i < count; ++i)
                apply_find_step(app->pane(), step);
        }
        // Test-only (HANABI_FIND_STEP=<±n>): the harness cannot press a Cmd
        // chord (afterhours_gaps.md #49), so this feeds the same step the
        // chord feeds, |n| times, on the first frame that has a tally to move
        // over. Everything below the two key reads is then under test.
        if (app->pane().findOpen && !findStepApplied_) {
            const int n = hanabi::test_hooks::find_step();
            if (n != 0 && app->pane().findCount > 0) {
                const hanabi::find_nav::Step s =
                    n < 0 ? hanabi::find_nav::Step::Prev
                          : hanabi::find_nav::Step::Next;
                for (int i = 0; i < (n < 0 ? -n : n); ++i)
                    apply_find_step(app->pane(), s);
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

        // 98 is the one-row strip. A draft that has grown past one row makes
        // the whole strip taller, so the transcript above it gets shorter
        // rather than being painted over -- the bar renders on layer 2 and
        // would happily cover the last lines of the conversation otherwise.
        if (app->lastComposerPaneW <= 0.0f)
            app->lastComposerPaneW = layout->composer.width > 0.0f
                                         ? layout->composer.width
                                         : layout->main.width;
        app->lastPaneContentH = layout->main.height + layout->composer.height;
        app->lastComposerChromeH = kComposerBaseH + attachments_h(*app) +
                                   composer_extra_h(composerRows_);
        layout->composerHeight =
            app->lastComposerChromeH + ask_card_h(*app);
        // Reply mode iff a real thread is open in Chat; otherwise kickoff (start
        // a new session). Split view still replies to its primary open thread.
        const bool composerKickoff =
            !(app->view == SmartView::Chat && app->pane().openSession);

        // Content fills the pane (layout->main already excludes the composer).
        auto content = div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("main_content"));

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
                if (splitView) {
                    render_split(ctx, content.ent(), *app, r.width, contentH,
                                 layout->composer.height);
                } else {
                    render_transcript(ctx, content.ent(), *app, app->pane(),
                                      r.width, contentH);
                }
                break;
            case SmartView::Home:
                render_home(ctx, content.ent(), *app, r.width, contentH);
                break;
            case SmartView::Blocked:
                render_digest(ctx, content.ent(), *app, "Blocked on you",
                              r.width, contentH, ecs::model::in_blocked_view,
                              "Nothing is waiting on you. \xf0\x9f\x8e\x89");
                break;
            case SmartView::Review:
                render_digest(ctx, content.ent(), *app, "Ready for review",
                              r.width, contentH, ecs::model::in_review_view,
                              "No threads are ready for review yet.");
                break;
            case SmartView::Starred:
                render_digest(ctx, content.ent(), *app, "Pinned", r.width,
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
            // In split view the composer belongs to the FOCUSED pane, and it
            // sits UNDER that pane: full-width under two panes gave no clue
            // which conversation you were typing into, and a composer nailed
            // to the left pane meant the right one could never be replied to.
            float cw = cr.width;
            float cx = cr.x;
            if (splitView) {
                const RectangleType pr =
                    pane_screen_rect(*app, app->focusedPane);
                cw = pr.width;
                cx = pr.x;
            }
            render_composer(ctx, uiRoot, *app, cw, cr.height, composerKickoff,
                            cx, cr.y);
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

    // ---- Which pane is being built right now -------------------------------
    // Everything down to the per-message painters takes `Pane&` explicitly.
    // The five leaves below the message loop (live_query, paint_query_for,
    // fold_mode and the two that call them) do not: they are reached fifteen
    // signatures deep, and every one of them ALREADY reached for a global --
    // app_singleton() -- to answer the same question. So the global they reach
    // for is narrowed rather than added to: not "the app", but "the pane whose
    // transcript this build pass is painting".
    //
    // It is a CURSOR over the build, not state. It is set by a scoped guard at
    // the top of render_transcript and restored when that pane's build
    // returns, nothing written through it outlives the frame, and -- unlike
    // the openSession swap it replaces -- an early return in the middle of a
    // pane cannot leave one pane's session parked in the other pane's field.
    //
    // Null outside a transcript build (the composer strip, the popovers), and
    // there "the pane" means the focused one, which is what painting_pane
    // falls back to.
    static Pane*& building_pane() {
        static Pane* p = nullptr;
        return p;
    }
    struct PaneBuildScope {
        Pane* prev;
        explicit PaneBuildScope(Pane& p) : prev(building_pane()) {
            building_pane() = &p;
        }
        ~PaneBuildScope() { building_pane() = prev; }
        PaneBuildScope(const PaneBuildScope&) = delete;
        void operator=(const PaneBuildScope&) = delete;
    };
    // Which of the app's panes this is. The Pane is handed around by
    // reference, and the ONE thing a reference cannot answer is "which one of
    // the two are you" -- which the per-pane widget names and the per-pane
    // memory keys both have to know.
    static int pane_index(const AppComponent& app, const Pane& pane) {
        return &pane == &app.panes[1] ? 1 : 0;
    }

    // ---- Per-pane widget names (afterhours_gaps.md #147) -------------------
    // A scroll view is reachable from outside this app ONLY by its debug name
    // (src/util/soak.h, scroll_named), so with two panes on screen a name that
    // used to identify one widget identifies two and the driver gets whichever
    // the entity walk reaches first. Pane 0 keeps the name it has always had,
    // so every existing driver, gate and assertion addresses exactly what it
    // addressed before; pane 1 gets its own.
    //
    // A static table returned BY REFERENCE rather than two literals at the
    // call site. Both names are inside libc++'s 22-character small-string
    // buffer (measured: capacity 22, heap allocation starts at 23), so this
    // saves no allocation and is not claimed to -- it is here so the two
    // spellings sit next to each other, one line apart, instead of at two
    // ends of an 8,800-line file where they can drift.
    static const std::string& scroll_name(int paneIndex) {
        static const std::string kNames[2] = {"transcript_scroll",
                                              "transcript_scroll_2"};
        return kNames[paneIndex & 1];
    }

    static Pane* painting_pane() {
        if (Pane* p = building_pane()) return p;
        AppComponent* app = app_singleton();
        return app == nullptr ? nullptr : &app->pane();
    }

    static void header(UIContext<InputAction>& ctx, Entity& parent,
                       const std::string& title, const std::string& sub,
                       float titlePx = theme::type::LG) {
        auto h = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kPageHeaderH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                // Left inset == kContentInset so the view title lines up exactly
                // with the section labels + cards in its scroll body (was 20 vs
                // the body's 24 — a 4px title/content misalignment on every
                // digest view). Right uses the same inset for symmetry.
                .with_padding(Padding{.top = pixels(14),
                                      .right = pixels(kContentInset),
                                      .bottom = pixels(theme::chrome::SPACE_2),
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
                .with_font_weight(theme::type::EMPHASIS)
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
                                      AppComponent& app, Pane& pane,
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
            model::FollowMemory& latch = model::pane_states().touch(
                model::pane_key(pane_index(app, pane), pane.openSession->summary.id))
                                             .latch;
            model::note_follow_pinned(latch, sv.scroll_offset.y,
                                      sv.scroll_target.y);
            app.focusedPane = pane_index(app, pane);
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
                .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
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
                     const std::string& text, float paneW, Pane& pane) {
        const float cardW = std::max(200.0f, paneW - 48.0f);
        auto card = div(ctx, mk(parent, 80),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(cardW), pixels(104)})
                .with_margin(Margin{.top = pixels(24), .right = pixels(24),
                                    .left = pixels(24)})
                .with_padding(Padding{.top = pixels(14), .left = pixels(16),
                                      .bottom = pixels(14), .right = pixels(16)})
                .with_custom_background(hanabi::surface::destructive_surface())
                .with_border(theme::destructive(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_debug_name("main_error"));
        div(ctx, mk(card.ent(), 1),
            ComponentConfig{}
                .with_label("Couldn\xe2\x80\x99t load this conversation")
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::destructive())
                .with_font_size(theme::type::LG)
                .with_alignment(TextAlignment::Left)
                .with_debug_name("main_error_title"));
        auto details = div(ctx, mk(card.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(40)})
                .with_margin(Margin{.top = pixels(6)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_debug_name("main_error_details"));
        div(ctx, mk(details.ent(), 1),
            ComponentConfig{}
                .with_label(text.empty() ? "Try again in a moment." : text)
                .with_size(ComponentSize{pixels(std::max(60.0f, cardW - 126.0f)),
                                         pixels(40)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::BODY)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_debug_name("main_error_body"));
        auto retry = button(ctx, mk(details.ent(), 2),
            hanabi::surface::action_button(78.0f, false, 2)
                .with_label("Try again")
                .with_margin(Margin{.left = pixels(8)})
                .with_font_size(theme::type::SM)
                .with_justify_content(JustifyContent::Center)
                .with_debug_name("main_error_retry"));
        if (retry && !pane.selectedId.empty()) {
            pane.transcriptError.clear();
            pane.transcriptState = LoadState::Loading;
            pane.requestOpenId = pane.selectedId;
        }
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
        float colH = paneH - kPageHeaderH;
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
                .with_custom_background(theme::chrome::raised())
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
    // The composer's growth, in one place because three sizes are derived from
    // it: the strip's reserved height, the outlined box and the field.
    static constexpr float kComposerLineH = 21.0f;
    static constexpr size_t kComposerMaxRows = 6;
    // The outlined box at ONE row. Every reference measurement in the composer
    // band below is written against it (the box is y=884..930 on
    // ref/01_home.png), so it is the anchor and growth is added to it rather
    // than derived through the field.
    static constexpr float kComposerBoxH1 = 46.0f;

    static constexpr size_t composer_rows_clamped(size_t rows) {
        return rows < 1 ? 1 : rows > kComposerMaxRows ? kComposerMaxRows : rows;
    }
    static constexpr float composer_box_h(size_t rows) {
        return kComposerBoxH1 +
               static_cast<float>(composer_rows_clamped(rows) - 1) *
                   kComposerLineH;
    }
    static constexpr float composer_extra_h(size_t rows) {
        return composer_box_h(rows) - kComposerBoxH1;
    }

    // The FIELD's height, which is not the box's and is not a round number.
    //
    // text_area gives its field a fixed padding of h720(4) top and bottom --
    // a 720p-REFERENCE size, so it resolves to 4 * screenH/720 each, 5.27 at
    // this app's 949 and 4.22 at the scripted suite's 760. Its own auto-grow
    // then sizes the field `rows * line_height + kVerticalPadding` with
    // kVerticalPadding a raw `8.f` (text_area.h:21, :141). The two agree at
    // exactly one screen height, 720. Above it the field is SHORTER than the
    // padding it carries plus the lines it holds, and the last line div hangs
    // out of its own parent's content box -- 21.0 in an 18.5 box, over by 2.5,
    // which is what `make bounds-gate` caught the moment this composer became
    // a text_area (afterhours_gaps.md #309).
    //
    // So the field's height is stated here instead, with the padding resolved
    // the way the widget will actually resolve it, and with_auto_grow is not
    // used -- it would put `+ 8` back. The box above is unaffected: it stays
    // 46 at one row whatever the padding resolves to, so nothing in the
    // reference band moves.
    static float composer_field_pad() {
        float h = 720.0f;
        if (auto* pcr = afterhours::EntityHelper::get_singleton_cmp<
                afterhours::window_manager::ProvidesCurrentResolution>())
            h = static_cast<float>(pcr->current_resolution.height);
        return 8.0f * h / 720.0f;
    }
    static float composer_field_h(size_t rows) {
        return static_cast<float>(composer_rows_clamped(rows)) *
                   kComposerLineH +
               composer_field_pad();
    }
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
    template <typename Pred>
    void render_digest(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, const std::string& title,
                       float paneW, float paneH, Pred pred,
                       const std::string& emptyMsg = "Nothing here right now.") {
        // Reused across frames so the collection costs no allocation once the
        // catalog has been seen at its largest. clear() keeps the capacity;
        // the old local vector malloc'd a pointer per matching session on
        // EVERY frame -- the same round trip sidebar.collect was taught to
        // stop making, one pane over.
        std::vector<const api::SessionSummary*>& rows = digestRows_;
        rows.clear();
        {
            hanabi::prof::Scope _t("digest.collect");
            hanabi::prof::AllocScope _a("digest.collect.allocs");
            for (const auto& s : app.sessions)
                if (pred(s)) rows.push_back(&s);
        }

        header(ctx, parent, title, std::to_string(rows.size()), theme::type::H1);

        if (rows.empty()) {
            empty_state(ctx, parent, view_glyph(app.view), emptyMsg, paneH);
            return;
        }

        float listH = paneH - kPageHeaderH;
        // The audit is a real row when it is on, so the list gives it the
        // height rather than overflowing the pane by it.
        if (hanabi::test_hooks::card_audit()) listH -= 16.0f;
        if (listH < 40.0f) listH = 40.0f;
        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::chrome::content())
                .with_padding(Padding{.top = pixels(6), .right = pixels(24),
                                      .bottom = pixels(6), .left = pixels(24)})
                .with_debug_name("digest_scroll"));
        // (scrollbar now drawn by afterhours)
        hanabi::apply_scroll_prefs(scroll.ent());

        int i = 0;
        Entity& wrap = centered_wrap(ctx, scroll.ent(), 9000, paneW - 48.0f);
        const float cardW = wrap_width(paneW);

        // Which of the matched cards is on screen. The list is not shortened:
        // the two spacers below are the exact height of the cards that were
        // skipped, so the content size, the scrollbar thumb, the clamp and the
        // y of any given card are the numbers they would have been with all of
        // them built. Blocked's job is to show everything blocked on you and
        // it still does -- this is docs/perf/SCROLL.md's fix for the sidebar's
        // rows, which is why it is a window and not the cap Home uses.
        const int n = static_cast<int>(rows.size());
        pitches_.clear();
        pitches_.reserve(static_cast<size_t>(n));
        {
            hanabi::prof::Scope _t("digest.pitch");
            hanabi::prof::AllocScope _a("digest.pitch.allocs");
            for (const auto* s : rows)
                pitches_.push_back(
                    digest::card_pitch(*s, false, subScratch_));
        }

        float viewH = 0.0f, offsetY = 0.0f, targetY = 0.0f;
        if (scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            viewH = sv.viewport_or_zero().y;
            offsetY = sv.scroll_offset.y;
            targetY = sv.scroll_target.y;
        }
        // Frame one has measured nothing, and "build the lot until it has" is
        // not a harmless fallback here: nothing retires a widget (gap #115),
        // so one uncapped frame mints four entities per matched session and
        // the app carries all 2276 of them for the rest of the process. The
        // frame after it builds thirty and the census still reads 2276.
        //
        // listH is the height this pane just asked the scroll view to be, so
        // it is the viewport to within its own 12 px of padding -- a guess
        // that is one card out, on one frame, against a plateau that never
        // comes down. It is also why every measurement in this branch is an
        // entity count and not only a frame time: the frame time was already
        // right with the fallback in.
        if (viewH <= 0.0f) viewH = listH;
        const digest::CardWindow win = digest::card_window(
            n, [this](int k) { return pitches_[static_cast<size_t>(k)]; },
            viewH, offsetY, targetY);

        // The keyboard cursor walks EVERY row, built or not. Its order is the
        // list's order and its y is a sum over the list's heights, so arrowing
        // off the bottom of the window scrolls to a card the next frame builds
        // rather than stopping at the edge of what happens to be on screen.
        // This is why digest_card is told not to do its own bookkeeping below:
        // it would count the built cards a second time and put every skipped
        // one at the wrong y.
        for (int k = 0; k < n; ++k) {
            const float h = pitches_[static_cast<size_t>(k)];
            listRows_.push_back(rows[static_cast<size_t>(k)]->id);
            if (!app.listCursorId.empty() &&
                rows[static_cast<size_t>(k)]->id == app.listCursorId) {
                listCursorY_ = listY_;
                listCursorH_ = h;
            }
            list_extent(h);
        }

        hanabi::prof::Scope _tbuild("digest.build");
        hanabi::prof::AllocScope _abuild("digest.build.allocs");
        card_spacer(ctx, wrap, 90, win.above);
        // Keyed on the window SLOT, never the card index. mk() retains an
        // entity per distinct id forever and nothing retires one (gap #115),
        // so index keys would mint four entities for every card ever scrolled
        // past -- the virtualization would be perfect and the leak would be
        // exactly the one it was written to remove. SCROLL.md section 3
        // measured that at +180 live blocks per 1000 frames on the sidebar.
        for (int k = win.first; k < win.last; ++k)
            digest_card(ctx, wrap, ++i, *rows[static_cast<size_t>(k)], app,
                        false, cardW, false, false);
        card_spacer(ctx, wrap, 91, win.below);

        cardsBuilt_ = i;
        cardsMatched_ = n;
        cardsFirst_ = win.first;
        hanabi::test_hooks::card_audit_counts() = {i, n, win.first};
        digest_audit(ctx, parent);
        scroll_cursor_into_view(scroll.ent(), listH);
    }

    // The stand-in for cards that were not built. One entity, the exact height
    // of the cards it replaces, so every measurement downstream of it is the
    // number it would have been.
    void card_spacer(UIContext<InputAction>& ctx, Entity& parent, int id,
                     float h) {
        if (h <= 0.0f) return;
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(h)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("digest_spacer"));
    }

    void home_cards(UIContext<InputAction>& ctx, Entity& wrap,
                    AppComponent& app,
                    const std::vector<const api::SessionSummary*>& rows,
                    size_t cap, float cardW, bool emphasizeMeta, bool grouped,
                    int spacerBase, int& shown, float viewH, float offsetY,
                    float targetY) {
        const size_t n = std::min(rows.size(), cap);
        if (n == 0) return;
        pitches_.clear();
        pitches_.reserve(n);
        for (size_t k = 0; k < n; ++k)
            pitches_.push_back(
                digest::card_pitch(*rows[k], grouped, subScratch_));

        const float sectionY = listY_ - kListTopPad;
        for (size_t k = 0; k < n; ++k) {
            const float h = pitches_[k];
            listRows_.push_back(rows[k]->id);
            if (!app.listCursorId.empty() && rows[k]->id == app.listCursorId) {
                listCursorY_ = listY_;
                listCursorH_ = h;
            }
            list_extent(h);
        }

        const digest::CardWindow win = digest::section_window(
            static_cast<int>(n),
            [this](int k) { return pitches_[static_cast<size_t>(k)]; }, viewH,
            offsetY, targetY, sectionY);

        const int base = homeMatched_;
        homeMatched_ += static_cast<int>(n);
        if (win.built() > 0 && homeFirstBuilt_ < 0)
            homeFirstBuilt_ = base + win.first;

        card_spacer(ctx, wrap, spacerBase, win.above);
        for (int k = win.first; k < win.last; ++k)
            digest_card(ctx, wrap, ++shown, *rows[static_cast<size_t>(k)], app,
                        emphasizeMeta, cardW, grouped, false);
        card_spacer(ctx, wrap, spacerBase + 1, win.below);
    }

    // Reused across frames so the pitch pass costs no allocation once the
    // catalog has been seen at its largest. clear() keeps the capacity.
    std::vector<float> pitches_;
    int homeMatched_ = 0;
    int homeFirstBuilt_ = -1;
    std::vector<const api::SessionSummary*> digestRows_;
    model::HomeBuckets homeBuckets_{kMaxSection};
    std::string subScratch_;

    // The cards this frame BUILT, against the sessions that matched, and the
    // row the build started at. See test_hooks::card_audit.
    int cardsBuilt_ = 0;
    int cardsMatched_ = 0;
    int cardsFirst_ = 0;

    void digest_audit(UIContext<InputAction>& ctx, Entity& parent) {
        if (!hanabi::test_hooks::card_audit()) return;
        div(ctx, mk(parent, 8),
            ComponentConfig{}
                .with_label("digest cards " + std::to_string(cardsBuilt_) +
                            " of " + std::to_string(cardsMatched_) + " @ " +
                            std::to_string(cardsFirst_))
                .with_size(ComponentSize{percent(1.0f), pixels(14)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(2)
                .with_debug_name("digest_audit"));
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
        // The VIEW, not the owning form: strip_parked_marker's only job is to
        // drop a leading "[P] ", and copying the whole title to do it was one
        // malloc per card per frame for a transform that removes bytes.
        const std::string_view src = fmtutil::display_title_view(in);
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
    using InlineParse = hanabi::md::Spans;
    static InlineParse md_to_spans(const std::string& line) {
        hanabi::prof::tick("text.md_spans");
        hanabi::prof::tick("text.md_bytes", line.size());
        return hanabi::md::inline_spans(
            line, hanabi::md::Palette{theme::text_primary(), theme::accent(),
                                      theme::text_primary()});
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

    // The card's displayed title: whitespace-normalised, "[P] " stripped, and
    // ellipsized to the title column -- memoized on the whole argument tuple.
    //
    // Pure in (raw title, card width, title fraction): char_budget is pure in
    // (width, font px) and theme::type::TITLE is a compile-time constant, so
    // there is nothing here that can go stale behind the key. It was three
    // heap allocations per card per frame -- the strip's copy, the normalised
    // string, and the ellipsized result -- for an answer that is the same one
    // every frame until the pane is resized.
    //
    // 128 entries: 63 cards are on screen at 1180x949 and the Home view has no
    // animated width the way the sidebar's fold does, so the working set is
    // the cards themselves and the tail is short. LRU rather than clear-when-
    // full, for the reason src/util/text_cache.h gives.
    static const std::string& card_title(const std::string& raw,
                                         float cardWidthPx, float titleFrac) {
        static constexpr std::size_t kCardTitleEntries = 128;
        static hanabi::text::TextKeyCache<std::string> memo(kCardTitleEntries);
        if (const std::string* hit = memo.find(raw, cardWidthPx, titleFrac)) {
            hanabi::prof::tick("cache.cardtitle_hit");
            return *hit;
        }
        hanabi::prof::tick("cache.cardtitle_miss");
        const std::string norm = normalize_title(raw);
        std::string cut;
        if (cardWidthPx > 0.0f) {
            // Inner width = card width - 32px L/R padding, times the title's
            // flex fraction, minus slack for the ellipsis glyph.
            const float titlePx = (cardWidthPx - 32.0f) * titleFrac - 6.0f;
            cut = fmtutil::ellipsize(norm,
                                     char_budget(titlePx, theme::type::TITLE));
        } else {
            cut = fmtutil::ellipsize(norm, 40);
        }
        return memo.put(raw, cardWidthPx, titleFrac, std::move(cut));
    }

    // A digest card's sub-line and its height live in ecs/digest_layout.h.
    // They moved out of this file when the digest lists were windowed: the
    // window has to know a card's height WITHOUT building the card, so the
    // height stopped being something the card decides on the way past and
    // became arithmetic two callers share. It is also the only part of this
    // 8000-line UI header that a unit test can reach
    // (tests/unit/test_digest_layout.cpp).

    static const char* tag_label(api::ThreadTag t) {        switch (t) {
            case api::ThreadTag::Blocked: return "BLOCKED";
            case api::ThreadTag::Waiting: return "WAITING";
            case api::ThreadTag::Review: return "REVIEW";
            case api::ThreadTag::Done: return "DONE";
            case api::ThreadTag::Failed: return "FAILED";
            default: return "";
        }
    }    static theme::Color tag_fg(api::ThreadTag t) {
        switch (t) {
            case api::ThreadTag::Blocked: return theme::tag_blocked_fg();
            // The same green the row's Waiting mark uses, so the chip and the
            // mark agree about which kind of wanting this is.
            case api::ThreadTag::Waiting: return theme::tag_ready_fg();
            case api::ThreadTag::Review: return theme::tag_ready_fg();
            case api::ThreadTag::Done: return theme::tag_done_fg();
            case api::ThreadTag::Failed: return theme::destructive();
            default: return theme::text_faint();
        }
    }
    static theme::Color tag_bg(api::ThreadTag t) {
        // The tag_*_bg tokens are intentionally low-alpha "soft tints". The UI
        // rect fill can't alpha-blend (afterhours gap #13), so pre-composite the
        // tint OVER the card surface (panel_bg_2) into an opaque color — giving
        // the intended subtle pill instead of a saturated solid block.
        const theme::Color surface = theme::chrome::raised();
        switch (t) {
            case api::ThreadTag::Blocked:
                return theme::over(theme::tag_blocked_bg(), surface);
            case api::ThreadTag::Waiting:
                return theme::over(theme::tag_ready_bg(), surface);
            case api::ThreadTag::Review:
                return theme::over(theme::tag_ready_bg(), surface);
            case api::ThreadTag::Done:
                return theme::over(theme::tag_done_bg(), surface);
            case api::ThreadTag::Failed:
                return hanabi::surface::destructive_surface();
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
            return theme::over(theme::tag_ready_bg(), theme::chrome::raised());
        return theme::chrome::raised();
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
    // How many VISUAL rows the composer's draft occupied on the frame just
    // rendered, clamped to kComposerMaxRows.
    //
    // Read one frame late, and that is the point rather than a compromise:
    // text_area's own `with_auto_grow` sizes the FIELD from
    // `state.layout_cache.line_count()` at the top of its call, which is the
    // count the PREVIOUS frame's rebuild left there. Taking the same number
    // from the same place, one frame after it was written, is the only way the
    // box around the field and the field itself can be guaranteed to agree --
    // and they must, or a three-line draft either clips against a one-line box
    // or leaves a gap under a six-line one. It also costs nothing: the count
    // is already computed, so nothing here wraps the draft a second time.
    //
    // The alternative was hanabi::text::wrapped_line_count over the draft with
    // the composer's own width, memoized like the transcript's. That is a
    // second wrap of the same string against a width this side has to derive
    // from the widget's padding rules, and it can disagree by a line. This
    // cannot.
    size_t composerRows_ = 1;
    float listY_ = 0.0f;
    float listCursorY_ = -1.0f;
    float listCursorH_ = 0.0f;
    bool listScrollPending_ = false;
    static constexpr float kListTopPad = 6.0f;  // the scroll panel's own pad

    // One-shot: HANABI_FIND_STEP is a stand-in for a keypress, so it fires
    // once and not every frame.
    bool findStepApplied_ = false;
    bool modelPopoverWasOpen_ = false;
    bool effortPopoverWasOpen_ = false;
    bool planPopoverWasOpen_ = false;
    struct AskRowId {
        const std::string* question;
        const std::string* option;
        afterhours::EntityID id;
    };
    std::vector<AskRowId> askRowIds_;
    afterhours::EntityID askEnterRow_ = 0;
    afterhours::EntityID askFocusedRow_ = 0;

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
            (sv.viewport_or_zero().y > 1.0f) ? sv.viewport_or_zero().y : listH;
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
                     bool grouped = false, bool trackCursor = true) {
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
        std::string subScratch;
        const std::string_view subLine = digest::sub_line(s, grouped, subScratch);
        const bool sparseSub = digest::sub_is_sparse(subLine);
        const float cardH = digest::card_body_height(subLine);

        // Every card in every list comes through here, so this is where the
        // keyboard cursor is both drawn and counted: the order below IS the
        // order on screen. The cursor row wears the hover surface plus an
        // accent border — a keyboard hover, reading like the mouse one.
        const bool onCursor = !app.listCursorId.empty() && s.id == app.listCursorId;
        if (trackCursor) {
            listRows_.push_back(s.id);
            if (onCursor) {
                listCursorY_ = listY_;
                listCursorH_ = digest::card_pitch(subLine);
            }
            list_extent(digest::card_pitch(subLine));
        }

        auto card = div(ctx, mk(parent, 100 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(cardH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(digest::kCardMarginTop),
                                    .right = pixels(0),
                                    .bottom = pixels(digest::kCardMarginBot),
                                    .left = pixels(0)})
                .with_padding(Padding{.top = pixels(7), .right = pixels(16),
                                      .bottom = pixels(7), .left = pixels(16)})
                .with_custom_background(onCursor
                                            ? theme::hover_over(theme::chrome::raised())
                                            : theme::chrome::raised())
                .with_border(onCursor ? theme::accent() : theme::chrome::divider(),
                             pixels(1.0f))
                .with_custom_hover_bg(theme::hover_over(theme::chrome::raised()))
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
        const std::string& title = card_title(s.title, cardWidthPx, titleFrac);
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
                    .with_label(std::string(subLine))
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
                    .with_label(std::string(subLine))
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
        hanabi::prof::tick("home.frames");
        header(ctx, parent, "Home", "", theme::type::H1);

        // The composer is rendered ONCE at the pane level (always visible), so
        // Home just fills its content height with the digest list.
        float listH = paneH - kPageHeaderH;
        if (listH < 40.0f) listH = 40.0f;
        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::chrome::content())
                .with_padding(Padding{.top = pixels(6), .right = pixels(24),
                                      .bottom = pixels(6), .left = pixels(24)})
                .with_debug_name("home_scroll"));
        // (scrollbar now drawn by afterhours)
        hanabi::apply_scroll_prefs(scroll.ent());

        float homeViewH = 0.0f, homeOffsetY = 0.0f, homeTargetY = 0.0f;
        if (scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            homeViewH = sv.viewport_or_zero().y;
            homeOffsetY = sv.scroll_offset.y;
            homeTargetY = sv.scroll_target.y;
        }
        if (homeViewH <= 0.0f) homeViewH = listH;
        homeMatched_ = 0;
        homeFirstBuilt_ = -1;

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

        homeBuckets_.update(app.sessionCatalogRevision, app.sessions);
        const auto& waiting = homeBuckets_.waiting();
        const auto& finished = homeBuckets_.finished();
        const auto& selfRunning = homeBuckets_.running();
        const auto& recent = homeBuckets_.recent();
        const bool anyAttention =
            !waiting.empty() || !finished.empty() || !selfRunning.empty();

        int shown = 0;
        bool first = true;  // tracks the first rendered section (tighter top).
        // Every section on this pane renders at most kMaxSection cards.
        //
        // Recent has been capped since it was written, on the reasoning that a
        // huge list must not build hundreds of cards. The three attention
        // sections above it were left uncapped on the reasoning that they are
        // empty on a calm backend -- which is true of a calm backend and says
        // nothing about a busy one. At a 2020-session catalog they build 696
        // cards, four entities each, and afterhours walks all 2784 in the
        // layout and render passes of EVERY frame whether or not one of them
        // is on screen. That, and not the sidebar, is the whole of the idle
        // curve's slope (soak census, commit b39304b).
        //
        // The section HEADER still carries the true total ("Waiting on you
        // \xc2\xb7 333"), so a cap hides rows and never a number. The cap is the
        // one Recent already used, shared now so the four sections cannot
        // drift apart -- and it is provably a no-op on the twenty-session
        // fixture the suite renders, because twenty sessions cannot put
        // twenty-one rows in any one section.
        if (!waiting.empty()) {
            const bool folded = section_label(
                ctx, wrap, 1,
                "Waiting on you \xc2\xb7 " + std::to_string(waiting.size()),
                first, theme::status_blocked(), app, "waiting");
            first = false;
            // Actionable rows: emphasize the "waiting on you \xc2\xb7 8m" metadata.
            if (!folded)
                home_cards(ctx, wrap, app, waiting, kMaxSection, cardW, true,
                           true, 7100, shown, homeViewH, homeOffsetY,
                           homeTargetY);
        }
        if (!finished.empty()) {
            const bool folded = section_label(
                ctx, wrap, 900,
                "Finished since you looked \xc2\xb7 " +
                    std::to_string(finished.size()),
                first, theme::tag_done_fg(), app, "finished");
            first = false;
            if (!folded)
                home_cards(ctx, wrap, app, finished, kMaxSection, cardW, false,
                           true, 7102, shown, homeViewH, homeOffsetY,
                           homeTargetY);
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
                home_cards(ctx, wrap, app, selfRunning, kMaxSection, cardW,
                           false, true, 7104, shown, homeViewH, homeOffsetY,
                           homeTargetY);
        }

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
            constexpr size_t kMaxRecent = kMaxSection;
            if (!folded)
                home_cards(ctx, wrap, app, recent, kMaxRecent, cardW, false,
                           false, 7106, shown, homeViewH, homeOffsetY,
                           homeTargetY);
        }
        hanabi::test_hooks::card_audit_counts() = {
            shown, homeMatched_,
            homeFirstBuilt_ < 0 ? 0 : homeFirstBuilt_};
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
                              theme::Color glyphColor, AppComponent& app,
                              const std::string& shelfKey) {
        const bool collapsed = app.collapsedShelves.count(shelfKey) != 0;
        list_extent((first ? theme::chrome::SPACE_2 : theme::chrome::SPACE_6) +
                    20.0f + theme::chrome::SPACE_2);

        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_margin(Margin{.top = pixels(first ? theme::chrome::SPACE_2
                                                           : theme::chrome::SPACE_6),
                                    .right = pixels(0),
                                    .bottom = pixels(theme::chrome::SPACE_2),
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
                .with_on_draw_fg([collapsed, glyphColor](RectangleType r) {
                    hanabi::glyph::chevron(r, collapsed, glyphColor, 3.2f);
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
                .with_custom_text_color(theme::text_faint())
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
            case api::SubAgentState::Failed:
                return SubGlyph::Blocked;
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
    static float sub_agent_panel_height(AppComponent& app, const Pane& pane) {
        const auto& subs = pane.openSession->sub_agents;
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
                          AppComponent& app, const Pane& pane) {
        const auto& subs = pane.openSession->sub_agents;
        if (subs.empty()) return 0.0f;

        const size_t count = subs.size();
        int done = 0, blocked = 0;
        for (const auto& sa : subs) {
            if (sa.state == api::SubAgentState::Done) ++done;
            if (sa.state == api::SubAgentState::Blocked ||
                sa.state == api::SubAgentState::Failed)
                ++blocked;
        }
        std::string verdict = blocked ? "blocked"
                              : (done == static_cast<int>(count) ? "done"
                                                                 : "running");
        const std::string key = "__subagents__";
        const bool open = app.expandedPiles.count(key) != 0;

        constexpr float kRowH = kSubAgentRowH;
        constexpr float kMargin = kSubAgentMargin;
        const float total = sub_agent_panel_height(app, pane);

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
                    // The 16px indent is PADDING, not a left margin. A
                    // percent()-sized child resolves against its parent's
                    // content box and its own margin is never subtracted from
                    // that (autolayout.h, Dim::Percent: "margins are external
                    // spacing and not included in computed"), so `percent(1)`
                    // plus `margin.left = 16` is a row exactly as wide as the
                    // parent, shifted 16px right -- 16px of chips outside the
                    // rollup, and the last chip on a line wrapping 16px late.
                    // Padding indents the CHILDREN and leaves the row the size
                    // the parent gave it, which is the same indent and the
                    // right box. (Padding is inert on a label-only element,
                    // gap #85 -- these are child divs, so it applies.)
                    .with_margin(Margin{.top = pixels(6)})
                    .with_padding(Padding{.left = pixels(16)})
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

    using Item = model::TranscriptItem;

    // ---- Minimap rail -----------------------------------------------------
    void minimap_rail(UIContext<InputAction>& ctx, Entity& parent,
                      Entity& scrollEnt, const std::vector<Item>& items,
                      const std::vector<api::Message>& msgs, float subH,
                      float paneW, float railTopY, float listH, float totalH,
                      float viewH, float scrollY, bool& follow,
                      hanabi::minimap::DragState& drag, model::PaneState& state,
                      bool itemsChanged) {
        // A gesture outlives the frame that started it; a rail does not. When
        // the rail stops being drawn under a held button — the thread shrank
        // below worth_showing, the reader switched tabs, the pane got narrow —
        // the state has to go with it, or the next press inherits a drag
        // nobody started.
        if (items.empty() ||
            !hanabi::minimap::worth_showing(totalH, viewH) ||
            !scrollEnt.has<afterhours::ui::HasScrollView>()) {
            drag = {};
            return;
        }
        const float railH = listH - 12.0f;
        if (railH < 40.0f) {
            drag = {};
            return;
        }
        const float railX = paneW - hanabi::minimap::kRailW -
                            hanabi::minimap::kRailInset;
        const float railY = railTopY + 6.0f;

        auto rail = div(ctx, mk(parent, 7400),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(hanabi::minimap::kRailW),
                                         pixels(railH)})
                .with_absolute_position()
                .with_translate(railX, railY)
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(8)
                .with_debug_name("minimap_rail"));

        // ---- press, drag, release --------------------------------------
        // The whole gesture is arithmetic on ONE rectangle and the pointer.
        //
        // The rectangle is the rail's LAID-OUT rect, read back off the entity,
        // and not the railX/railY above. Those are what the config was given,
        // and a config's translate is in the PARENT's space while the pointer
        // is in the window's: hit-testing the pointer against them is off by
        // the pane's origin — 280px here — and reads as a gesture that simply
        // never starts (afterhours_gaps.md #286). The rect is one frame old,
        // which for a rail that has not moved is the same rect.
        const auto railRect = rail.ent().get<afterhours::ui::UIComponent>().rect();
        auto& sv = scrollEnt.get<afterhours::ui::HasScrollView>();
        const bool onRail = ctx.mouse.pos.x >= railRect.x &&
                            ctx.mouse.pos.x <= railRect.x + railRect.width &&
                            ctx.mouse.pos.y >= railRect.y &&
                            ctx.mouse.pos.y <= railRect.y + railRect.height;
        // Where the press LANDED decides whether this button-hold is ours.
        // Asked once, on the press frame, because the cursor leaves the rail
        // constantly during a real drag and the answer must not change when it
        // does.
        if (ctx.mouse.just_pressed) drag.armed = onRail;
        // Release ENDS it, wherever the pointer is — off the rail, over the
        // composer, outside the window. The gesture belongs to the button, not
        // to the rectangle it started in, so this is a plain "the button is
        // up" test and not a hit test that a cursor 400px away would fail.
        if (!ctx.mouse.left_down) drag = {};
        if (drag.armed && !drag.live && ctx.mouse.press_moved) {
            // afterhours' own 6px threshold (MousePointerState::
            // press_drag_threshold_px), which is also the one that withholds a
            // widget's click — so a press is a click or a drag and never both
            // halves of one, and the two answers cannot disagree.
            drag.live = true;
            drag.anchorY = ctx.mouse.pos.y;
            drag.anchorOffset = sv.scroll_offset.y;
        }
        if (drag.live) {
            const float want = hanabi::minimap::scrub_offset(
                drag.anchorOffset, ctx.mouse.pos.y - drag.anchorY, railH, viewH,
                totalH);
            sv.scroll_offset.y = want;
            hanabi::set_scroll_target_y(sv, want);
            sv.clamp_scroll();
            // Scrubbing is leaving the bottom, exactly as clicking a mark or
            // scrolling up by hand is: without this the follow-latch drags the
            // view back to the newest message between frames and the drag
            // fights it.
            follow = false;
            // Draw the band from THIS frame's offset rather than the one read
            // before the transcript was built, so it sits under the cursor on
            // the frame the cursor moved and not the frame after. The content
            // still lands a frame late (the virtualizer reads last frame's
            // scroll), which is what every wheel scroll here already does.
            scrollY = sv.scroll_offset.y;
        }

        const bool hot = drag.live || onRail;
        const bool rebuildSlots = itemsChanged || !state.minimapSlots ||
                                  state.minimapTotalH != totalH ||
                                  state.minimapRailH != railH ||
                                  state.minimapLeadH != subH;
        if (rebuildSlots) {
            std::vector<float> heights;
            std::vector<hanabi::minimap::Mark> kinds;
            heights.reserve(items.size());
            kinds.reserve(items.size());
            for (const Item& it : items) {
                hanabi::minimap::Mark mark = hanabi::minimap::Mark::Note;
                switch (it.kind) {
                    case Item::ToolPile:
                    case Item::ToolBlock:
                        mark = hanabi::minimap::Mark::Machinery;
                        break;
                    case Item::Spawn:
                    case Item::Delivery:
                    case Item::Event:
                        mark = hanabi::minimap::Mark::Notice;
                        break;
                    case Item::Bubble:
                        mark = (it.lo < static_cast<int>(msgs.size()) &&
                                msgs[static_cast<size_t>(it.lo)].role ==
                                    api::Role::User)
                                   ? hanabi::minimap::Mark::Ask
                                   : hanabi::minimap::Mark::Reply;
                        break;
                    default: break;
                }
                heights.push_back(it.height);
                kinds.push_back(mark);
            }
            state.minimapSlots =
                std::make_shared<const std::vector<hanabi::minimap::Slot>>(
                    hanabi::minimap::group_marks(heights, kinds, subH, totalH,
                                                 railH));
            hanabi::prof::tick("minimap.slot_rebuild");
            state.minimapTotalH = totalH;
            state.minimapRailH = railH;
            state.minimapLeadH = subH;
        } else {
            hanabi::prof::tick("minimap.slot_cache_hit");
        }
        const auto slots = state.minimapSlots;
        hanabi::prof::gauge("minimap.items", items.size());
        hanabi::prof::gauge("minimap.marks", slots->size());

        auto marks = button(ctx, mk(rail.ent(), 901),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{percent(1.0f), pixels(railH)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_transparent_bg()
                .with_custom_hover_bg(afterhours::Color{0, 0, 0, 0})
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_skip_tabbing(true)
                .with_on_draw_fg([slots, totalH, hot](RectangleType r) {
                    for (const auto& slot : *slots) {
                        const float h = hanabi::minimap::slot_h(
                            slot.height, totalH, r.height);
                        if (h <= 0.0f) continue;
                        const float y = r.y + hanabi::minimap::slot_h(
                                                  slot.topY, totalH, r.height);
                        hanabi::minimap::draw_mark(
                            RectangleType{r.x, y, r.width, h}, slot.mark, hot);
                    }
                })
                .with_debug_name("minimap_marks"));
        if (marks && !slots->empty() && railRect.height > 0.0f) {
            const float ratio = std::clamp(
                (ctx.mouse.pos.y - railRect.y) / railRect.height, 0.0f, 1.0f);
            const float contentY = ratio * totalH;
            if (contentY >= slots->front().topY) {
                const hanabi::minimap::Slot* selected = &slots->front();
                for (const auto& slot : *slots) {
                    if (slot.topY > contentY) break;
                    selected = &slot;
                }
                const float want = std::max(0.0f, selected->topY - 12.0f);
                sv.scroll_offset.y = want;
                hanabi::set_scroll_target_y(sv, want);
                sv.clamp_scroll();
                follow = false;
            }
        }

        // Where the viewport is, drawn LAST: the render buffer takes a
        // parent's own foreground before its children, so a scrubber painted
        // by the rail itself would sit under every mark.
        div(ctx, mk(rail.ent(), 950),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(hanabi::minimap::kRailW),
                                         pixels(railH)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([scrollY, viewH, totalH](RectangleType r) {
                    hanabi::minimap::draw_scrubber(r, scrollY, viewH, totalH);
                })
                .with_debug_name("minimap_scrubber"));
    }

    static ecs::model::TranscriptRenderCache& render_cache() {
        static ecs::model::TranscriptRenderCache c;
        // It holds line counts, measured heights and hugged widths -- all of
        // them measurements, none of them keyed by anything that moves when
        // the reader swaps the face behind DEFAULT_FONT from Settings. One
        // check here covers both the per-message memo and the hug memo, which
        // share this store on purpose (see transcript_render_cache.h).
        static unsigned epoch = hanabi::text::font_epoch();
        if (epoch != hanabi::text::font_epoch()) {
            epoch = hanabi::text::font_epoch();
            c.clear();
        }
        return c;
    }

    // Chat welcome / empty state (no thread open): a centered hero — brand mark,
    // a greeting, and a few suggestion chips — instead of a bare "open a thread"
    // note in a tall void. Modern-chat "What can I help with?" landing.
    void render_chat_welcome(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app, float paneW, float paneH) {
        const float chipW = std::max(180.0f, std::min(360.0f, paneW - 48.0f));
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
                .with_custom_background(theme::over(theme::accent_soft(),
                                                    theme::panel_bg_2()))
                .with_border(theme::border(), pixels(1.0f))
                .with_corner_radius(12.0f)
                .with_on_draw_fg([](RectangleType r) {
                    hanabi::icons::draw_at("brand", r.x + r.width * 0.5f,
                                           r.y + r.height * 0.5f, 30.0f,
                                           theme::text_secondary());
                })
                .with_debug_name("welcome_mark"));
        div(ctx, mk(col.ent(), 2),
            ComponentConfig{}
                .with_label("What can I help with?")
                .with_size(ComponentSize{pixels(chipW), pixels(28)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::H1)
                .with_alignment(TextAlignment::Center)
                .with_margin(Margin{.top = pixels(14), .bottom = pixels(4)})
                .with_roundness(0.0f)
                .with_debug_name("welcome_greeting"));
        div(ctx, mk(col.ent(), 3),
            ComponentConfig{}
                .with_label("Start with a prompt or write your own below")
                .with_size(ComponentSize{pixels(chipW), pixels(22)})
                .with_margin(Margin{.bottom = pixels(12)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("welcome_subtitle"));
        static const char* kChips[] = {
            "Summarize what's waiting on me",
            "What changed since I last looked?",
            "Draft a status update",
        };
        auto chips = div(ctx, mk(col.ent(), 4),
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
                hanabi::surface::option_row(chipW, 38.0f, false, 2,
                                            theme::panel_bg_2())
                    .with_label(kChips[i])
                    .with_padding(Padding{.top = pixels(6), .left = pixels(14),
                                          .bottom = pixels(6),
                                          .right = pixels(14)})
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_margin(Margin{.bottom = pixels(8)})
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_debug_name("welcome_chip_" + std::to_string(i)));
            if (chip) {
                // Seed the LANDING composer's kickoff draft (below) instead of
                // opening the modal overlay — the composer picks up welcomeSeed
                // in kickoff mode, so the chip pre-fills the visible input and
                // the user hits Send/Enter to start the new session.
                app.welcomeSeed = kChips[i];
            }
        }
    }

    // ---- SPLIT VIEW: two transcripts side by side ---------------------------
    // Left is panes[0], right is panes[1], and both go through the SAME
    // render_transcript with their own Pane. There is no swap: what used to
    // happen here was to move panes[0]'s session out to a local, move the
    // right-hand thread in, render, and move both back -- so for the duration
    // of the right pane's build every global that said "the open thread" was
    // lying, and an early return anywhere inside would have left one pane's
    // transcript parked in the other pane's field.
    //
    // The divider is draggable. Its own hit strip is wider than the line it
    // draws (a 1px target is not a target), and the drag lives on the app
    // rather than in a local static for the reason every gesture in this app
    // does: the widget is rebuilt every frame, so the gesture cannot be.
    // The divider's WIDTH is its hit target; the line it paints is 1px.
    static constexpr float kDividerW = 5.0f;
    static constexpr float kMinimumPaneW = 280.0f;

    void render_split(UIContext<InputAction>& ctx, Entity& parent,
                      AppComponent& app, float paneW, float paneH,
                      float composerH) {
        const float usable = paneW - kDividerW;
        const float leftW = split_left_width(app, paneW);
        const float rightW = usable - leftW;

        auto rowWrap = div(ctx, mk(parent, 4100),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(paneW), pixels(paneH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("split_row"));

        const float leftH = app.focusedPane == 0 ? paneH - composerH : paneH;
        const float rightH = app.focusedPane == 1 ? paneH - composerH : paneH;
        pane_column(ctx, rowWrap.ent(), app, 0, leftW, leftH);
        divider_bar(ctx, rowWrap.ent(), app, paneW);
        pane_column(ctx, rowWrap.ent(), app, 1, rightW, rightH);
        close_split_button(ctx, parent, app, paneW);
    }

    // Close-split affordance: a small x pinned to the top-right of the whole
    // main pane. Clicking it goes back to one pane.
    void close_split_button(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app, float paneW) {
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

    // The left pane's width in pixels, from the persisted ratio, clamped so a
    // drag can never reduce either side to nothing.
    static constexpr float kAskUsablePaneW = 240.0f;

    static bool split_fits(float usable) {
        return usable >= kAskUsablePaneW * 2.0f;
    }

    static float clamped_split_left(float desired, float usable) {
        if (usable <= 0.0f) return 0.0f;
        if (!split_fits(usable)) return usable;
        const float floorW =
            usable >= kMinimumPaneW * 2.0f ? kMinimumPaneW : kAskUsablePaneW;
        return std::round(std::clamp(desired, floorW, usable - floorW));
    }

    static float split_left_width(const AppComponent& app, float paneW) {
        const float usable = paneW - kDividerW;
        return clamped_split_left(usable * hanabi::clamp_split_ratio(app.splitRatio),
                                  usable);
    }

    // ONE pane of the split. The debug name carries the pane index, and that
    // is not cosmetic: a scroll view is reachable from outside this app only
    // by its debug name (afterhours_gaps.md #147), so with two of them on
    // screen a name that used to identify one widget now identifies two --
    // whichever the entity walk happens to reach first. Every named widget
    // inside a pane gets the same treatment (see pane_suffix).
    //
    // Pane 0 keeps the UNSUFFIXED names it has always had, so every scripted
    // assertion, every soak arm and every screenshot of the single-pane app
    // still addresses exactly what it addressed before.
    void pane_column(UIContext<InputAction>& ctx, Entity& parent,
                     AppComponent& app, int index, float w, float h) {
        Pane& pane = app.panes[static_cast<size_t>(index)];
        const bool focused = app.focusedPane == index;
        auto col = div(ctx, mk(parent, 10 + index),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(index == 0 ? "split_left" : "split_right"));

        // Clicking anywhere in a pane focuses it. A press rather than a
        // release, and read BEFORE the pane builds, so the click that focuses
        // a pane also lands on whatever it was aimed at inside it.
        if (ctx.mouse.just_pressed && !focused &&
            afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, pane_screen_rect(app, index)))
            app.focusedPane = index;

        render_transcript(ctx, col.ent(), app, pane, w, h);

        // Which pane the keyboard is in. A hairline down the pane's inside
        // edge rather than a full border: two boxed panes read as two windows,
        // and the reference has no boxes anywhere in the main pane.
        if (focused) focus_edge(ctx, col.ent(), h);
    }

    // The focused pane's marker: an accent hairline along the top of the pane.
    void focus_edge(UIContext<InputAction>& ctx, Entity& parent, float h) {
        (void)h;
        div(ctx, mk(parent, 4180),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(2.0f)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_custom_background(theme::accent())
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("split_focus_edge"));
    }

    // Where a pane sits on SCREEN, for the hit tests that cannot ask the
    // widget tree (the click-to-focus above runs before the pane is built, and
    // afterhours hands out no rect until layout has run).
    static RectangleType pane_screen_rect(const AppComponent& app, int index) {
        auto* layout = find_singleton<LayoutComponent>();
        if (layout == nullptr) return RectangleType{0, 0, 0, 0};
        const auto& r = layout->main;
        const float paneH =
            r.height + (index == app.focusedPane ? 0.0f : layout->composer.height);
        const float leftW = split_left_width(app, r.width);
        if (index == 0) return RectangleType{r.x, r.y, leftW, paneH};
        return RectangleType{r.x + leftW + kDividerW, r.y,
                             r.width - leftW - kDividerW, paneH};
    }

    // The divider, and the drag along it.
    //
    // imm::divider is the LIBRARY's separator, not a hand-rolled one, and the
    // first version of this was hand-rolled before that was checked. What it
    // gets right and the hand-rolled one did not: the movement it reports is a
    // DELTA, so grabbing the bar 3px off centre moves it by what the mouse
    // moved rather than snapping the bar to the cursor. The hand-rolled one
    // set the ratio from the absolute mouse x and jumped on every grab.
    //
    // The 5px width is the hit target -- a 1px bar is not something a person
    // can reliably grab -- and the line the reader sees is 1px, painted down
    // the middle. Puffin has no 5px rules anywhere and the parity captures
    // would have caught it; the widget being wider than its ink is the way to
    // have both.
    void divider_bar(UIContext<InputAction>& ctx, Entity& parent,
                     AppComponent& app, float paneW) {
        const bool dragging = app.splitDragging;
        auto d = divider(ctx, mk(parent, 2), Axis::X,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kDividerW), percent(1.0f)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([dragging](RectangleType r) {
                    const float x = std::round(r.x + (r.width - 1.0f) * 0.5f);
                    afterhours::draw_rectangle(
                        RectangleType{x, r.y, 1.0f, r.height},
                        dragging ? theme::accent() : theme::border());
                })
                .with_debug_name("split_divider"));
        // `down` on the element is the gesture; the app keeps the flag only so
        // the line can say it is being dragged. The library owns the gesture's
        // lifetime, which is what the old bool on the app was standing in for.
        app.splitDragging =
            d.ent().template has<afterhours::ui::HasDragListener>() &&
            d.ent().template get<afterhours::ui::HasDragListener>().down;
        if (!d) return;
        const float usable = paneW - kDividerW;
        if (usable <= 1.0f) return;
        const float delta = d.template as<float>();
        const float nextLeft = split_left_width(app, paneW) + delta;
        app.splitRatio = clamped_split_left(nextLeft, usable) / usable;
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
        // The label is centred, and each rule is capped so date metadata does
        // not visually divide the conversation as strongly as a run outcome.
        float ruleW = std::min(72.0f,
                               (rowW - lw - 2.0f * kGap) * 0.5f);
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
                            transcript_rule());
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

    // ---------------- Run-outcome divider ----------------------------------
    // A run ending is the one thing in a transcript that is not somebody
    // speaking, and Puffin draws it as such: a hairline across the column with
    // the server's own word for how the run ended centred in it
    // (`AgentcloudTranscriptView.runSeparator`, an `HStack(spacing: 8)` of
    // rule / 9pt text / rule with 2pt of vertical padding). Without it a run
    // that DIED and a run that is merely quiet look identical — the thread
    // just stops, and the reader has to infer from the sidebar's tag that
    // nothing is coming.
    //
    // Only a failure is red. Puffin's own comment records why: painting
    // everything that was not `completed` in the danger colour made the user's
    // own deliberate stop read as an error, and so would any outcome invented
    // after this build. Anything else prints in the faint text colour, and the
    // word is the backend's own string rather than an enum, so an outcome
    // hanabi has never heard of still reads.
    static constexpr float kRunOutcomeH = 22.0f;
    // Air above the divider row, on top of the turn's own kTurnGapBot.
    //
    // Measured, not derived: with the rule centred in a 22px row the reference
    // wants it at y299 and hanabi put it at y296. Puffin's separator is a
    // stack ITEM -- `bubbleBreathing = 9` under the bubble plus
    // `itemSpacing = 6` before the next row plus its own
    // `.padding(.vertical, 2)` -- where hanabi's turn gap is one number, so
    // there is no single Puffin constant this equals. The 3 is the difference
    // between the two arithmetics on the frozen frame.
    static constexpr float kRunOutcomeGapTop = 3.0f;

    // Every rule hanabi draws inside a transcript.
    //
    // Puffin draws all three of its own in one colour and one alpha --
    // `Color(PuffinTheme.Color.mutedText).opacity(0.25)`, at
    // AgentcloudTranscriptView.swift:919, :2245 and :2250 -- and hanabi was
    // using `theme::border()` (62,62,72) for them. Over the window ground that
    // quarter-opacity muted grey resolves to about (53,53,65), and the
    // reference's own rule peaks at (48,48,62) spread across three rows by the
    // 2x downsample, which is the same line: summed over the ground, the
    // reference's three rows carry 30 units of ink and a crisp 1px at (53,53,65)
    // carries 29. border() carries 39 and reads as a harder line than Puffin's.
    //
    // Only the run-outcome rule is scoreable -- it is the one rule in
    // `ref/02_thread.png` -- but the date and new-message dividers take it too,
    // because two greys of rule in one pane is a defect a reader sees and a
    // metric cannot.
    static theme::Color transcript_rule() {
        theme::Color c = theme::text_secondary();
        c.a = 64;  // Puffin's .opacity(0.25)
        return c;
    }

    // Puffin's `TranscriptGrouping.drawsOutcome`: a completed run is only
    // announced when it is the last thing in the thread. Mid-thread, a
    // successful run ending is noise — the next turn is the announcement.
    static bool draws_outcome(const std::string& outcome, bool isLast) {
        if (outcome.empty()) return false;
        return outcome != "completed" || isLast;
    }

    static bool outcome_is_failure(const std::string& outcome) {
        return outcome == "failed" || outcome == "error";
    }

    // The rule is DRAWN and the word is a real text element, for the reasons
    // date_divider gives: Roboto has no Box Drawing block (gaps #48), and text
    // painted in on_draw_fg never reaches the visible-text registry, so a
    // divider drawn wholesale is invisible to every assertion about it.
    static void run_outcome_divider(UIContext<InputAction>& ctx, Entity& parent,
                                    int id, const std::string& outcome,
                                    float rowW) {
        const theme::Color ink = outcome_is_failure(outcome)
                                     ? theme::status_blocked()
                                     : theme::text_faint();
        float lw = 30.0f;
        if (auto* fm = afterhours::EntityHelper::get_singleton_cmp<
                afterhours::ui::FontManager>())
            lw = afterhours::measure_text(fm->get_active_font(),
                                          outcome.c_str(), theme::type::MICRO,
                                          1.0f)
                     .x;
        constexpr float kGap = 8.0f;  // Puffin's HStack(spacing: 8)

        auto row = div(ctx, mk(parent, 8800 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kRunOutcomeH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
                .with_margin(Margin{.top = pixels(kRunOutcomeGapTop)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("run_outcome_divider"));
        // Explicit widths, never percent(1.0): a percent child in a NoWrap row
        // resolves against the whole ROW and shoves its siblings out (gap #53).
        // The word is centred, so each rule takes half of what it leaves.
        float ruleW = (rowW - lw - 2.0f * kGap) * 0.5f;
        if (ruleW < 8.0f) ruleW = 8.0f;
        const auto rule = [&](int childId) {
            div(ctx, mk(row.ent(), childId),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(ruleW),
                                             pixels(kRunOutcomeH)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_on_draw_fg([](RectangleType r) {
                        const float cy = r.y + r.height * 0.5f;
                        afterhours::draw_line_ex(
                            afterhours::vec2{r.x, cy},
                            afterhours::vec2{r.x + r.width, cy}, 1.0f,
                            transcript_rule());
                    })
                    .with_debug_name("run_outcome_rule"));
        };
        rule(1);
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(outcome)
                .with_size(ComponentSize{pixels(lw + 2.0f * kGap),
                                         pixels(kRunOutcomeH)})
                .with_transparent_bg()
                .with_custom_text_color(ink)
                .with_font_size(theme::type::MICRO)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("run_outcome_label"));
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
    // ---- Work-tracker ids -------------------------------------------------
    // The ids this deployment can actually open. Empty whenever no tracker
    // host is configured, which is what keeps an id prose instead of a link
    // that goes nowhere (src/ui/link_detect.h says why).
    static std::vector<hanabi::links::Link> links_in(const std::string& text) {
        AppComponent* app = app_singleton();
        if (app == nullptr || app->trackerBaseUrl.empty()) return {};
        return hanabi::links::find(text);
    }

    // Recolour the spans covering each link so an id reads as one. The runs
    // are split, never re-measured: the VISIBLE text is untouched, so wrap and
    // height are exactly what the measure pass computed.
    static void colour_links(InlineParse& ip,
                             const std::vector<hanabi::links::Link>& links) {
        if (links.empty() || ip.spans.empty()) return;
        const theme::Color c = theme::link();
        std::vector<afterhours::ui::TextSpan> out;
        size_t at = 0;  // byte offset of this span's start within ip.visible
        for (const auto& sp : ip.spans) {
            size_t cut = 0;  // how much of this span is already emitted
            for (const auto& l : links) {
                if (l.off + l.len <= at + cut || l.off >= at + sp.text.size())
                    continue;
                const size_t b = l.off > at + cut ? l.off - at : cut;
                const size_t e = std::min(l.off + l.len - at, sp.text.size());
                if (b > cut)
                    out.push_back(afterhours::ui::TextSpan{
                        sp.text.substr(cut, b - cut), sp.color});
                out.push_back(
                    afterhours::ui::TextSpan{sp.text.substr(b, e - b), c});
                cut = e;
            }
            if (cut < sp.text.size())
                out.push_back(afterhours::ui::TextSpan{sp.text.substr(cut),
                                                       sp.color});
            at += sp.text.size();
        }
        ip.spans = std::move(out);
    }

    // A press on an id opens it. The hit test re-derives where the run landed
    // (afterhours_gaps.md #51 — there is still no way to ask), and the pointer
    // cursor over an id comes from the same answer.
    static void link_hotspot(UIContext<InputAction>& ctx, Entity& el,
                             const std::string& text,
                             const std::vector<hanabi::links::Link>& links,
                             float fontPx) {
        if (links.empty() || !el.has<afterhours::ui::UIComponent>()) return;
        const RectangleType r = el.get<afterhours::ui::UIComponent>().rect();
        const std::string id = hanabi::links::hit(r, text, links, fontPx,
                                                  ctx.mouse.pos.x,
                                                  ctx.mouse.pos.y);
        if (id.empty()) return;
        if (el.has<afterhours::ui::HasCursor>())
            el.get<afterhours::ui::HasCursor>().cursor =
                afterhours::ui::CursorType::Pointer;
        if (!ctx.mouse.just_pressed) return;
        AppComponent* app = app_singleton();
        if (app == nullptr) return;
        hanabi::links::open(hanabi::links::url_for(app->trackerBaseUrl, id));
        // The browser is another app, and on a headless render there is no
        // browser at all — either way the transcript says what it just did.
        app->raise_toast("Opened " + id, "", AppComponent::ToastUndo::None);
    }

    static void selectable_text(UIContext<InputAction>& ctx, Entity& el,
                                const std::string& text, float fontPx) {
        el.addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        // The listener above is empty: it exists so the element gets hover and
        // press plumbing for the drag-select, not because a paragraph is a
        // thing you can activate. afterhours reads "has a click listener" as
        // "is a keyboard tab stop", so without this every line of every
        // rendered turn joined the tab order -- and, since the focusable set
        // is a std::set rebuilt from scratch each frame, cost a red-black-tree
        // node malloc per line per frame (afterhours_gaps.md #183). Tabbing
        // through forty paragraphs to reach the composer was not a feature.
        el.addComponentIfMissing<afterhours::ui::SkipWhenTabbing>();
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

    // The other three transcript preferences, read the same way and for the
    // same reason: each is answered per use so the sheet open over the
    // transcript shows the change on the very next frame.
    static bool show_date_dividers() {
        return Settings::get().get_show_date_dividers();
    }
    static bool show_reasoning() {
        return Settings::get().get_show_reasoning();
    }
    static bool fold_long_messages() {
        return Settings::get().get_fold_long_messages();
    }

    // One match: which message, and where in it.
    //
    // WHAT THE TALLY IS. Every paintable match in the loaded thread — not the
    // ones currently on screen. Bands are painted only for the messages the
    // virtualization window built, so `bands <= findCount`, and equality holds
    // only when the whole thread fits the window. That gap is the feature:
    // "3 of 47" answers "how many are in this thread", which is what it
    // answers in every other editor, and the chevrons are how you walk to the
    // 44 that are not in front of you.
    //
    // The rule that IS load-bearing runs the other way — nothing is counted
    // that find could not paint. Same rows (user and assistant, no tool rows,
    // no system captions, no thinking blocks), same normalization
    // (paintable_lines), same operator predicate on both sides, and the same
    // LOGICAL line on both sides: find_highlight::paint_bands matches over the
    // whole line and then places the hit on the wrapped ones, so a multi-word
    // query broken across a soft wrap is painted rather than counted-only
    // (docs/SEARCH.md S12). A match that survives all of that is one a scroll
    // can bring under a band.
    //
    // The remaining exception is a match in the folded tail of a long message,
    // which is at least reachable by a click on the fold rather than by
    // nothing at all.
    using Match = hanabi::find_memo::Match;

    static const std::vector<Match>& collect_matches(
        Pane& pane, const api::Session& s, const find_ops::Query& q,
        float wrapWidth, std::uint64_t foldRevision) {
        const auto before = pane.findMemo.stats();
        hanabi::find_memo::PaintPolicy policy;
        policy.wrap_width = wrapWidth;
        policy.fold_long_messages = fold_long_messages();
        policy.show_reasoning = show_reasoning();
        policy.fold_revision = foldRevision;
        const auto& out = pane.findMemo.collect(
            s, pane.transcriptVersion, q, policy,
            [](const api::Message& m, bool rich) {
                return paintable_lines(m, rich);
            });
        const auto after = pane.findMemo.stats();
        hanabi::prof::tick("find.memo_hit",
                           after.result_hits - before.result_hits);
        hanabi::prof::tick("find.memo_miss",
                           after.result_misses - before.result_misses);
        hanabi::prof::tick("find.rows_visited",
                           after.rows_visited - before.rows_visited);
        hanabi::prof::tick("find.message_work",
                           after.message_work - before.message_work);
        hanabi::prof::tick("find.normalize",
                           after.normalized - before.normalized);
        hanabi::prof::tick("find.normalize_reused",
                           after.normalized_reused - before.normalized_reused);
        hanabi::prof::gauge("find.memo_entries", pane.findMemo.entries());
        return out;
    }

    static const find_ops::Query& live_query() {
        static const find_ops::Query empty;
        const Pane* p = painting_pane();
        return p == nullptr ? empty : p->findMemo.query();
    }

    // What find should paint inside message `index` — the query's text, or
    // nothing at all when an operator has excluded that row.
    static std::string paint_query_for(int index) {
        const Pane* p = painting_pane();
        if (p == nullptr || !p->findOpen || p->findQuery.empty() ||
            !p->openSession)
            return std::string();
        const find_ops::Query& q = live_query();
        if (q.invalid || q.text.empty()) return std::string();
        if (!p->findMemo.row_is_paintable(static_cast<std::size_t>(index)))
            return std::string();
        return q.text;
    }

    static const std::vector<std::size_t>* paint_offsets_for(
        int messageIndex, std::size_t lineIndex) {
        const Pane* p = painting_pane();
        if (p == nullptr || messageIndex < 0) return nullptr;
        return p->findMemo.line_hits(static_cast<std::size_t>(messageIndex),
                                     lineIndex);
    }

    // Move the current match one step and ask the transcript to scroll it into
    // view. The Cmd+G chord and the find bar's chevrons both come through
    // here, so they cannot drift onto different matches.
    static void apply_find_step(Pane& pane, hanabi::find_nav::Step s) {
        if (s == hanabi::find_nav::Step::None || pane.findCount <= 0) return;
        pane.findIndex =
            hanabi::find_nav::advance(pane.findIndex, pane.findCount, s);
        pane.findScrollPending = true;
    }

    // The find bar: an overlay pinned to the transcript's top-right, so it
    // never displaces the conversation under it.
    void find_bar(UIContext<InputAction>& ctx, Entity& parent,
                  AppComponent& app, Pane& pane, float paneW, int matchCount,
                  const find_ops::Query& q) {
        constexpr float kInputW = 176.0f;
        constexpr float kTallyW = 76.0f;
        constexpr float kButtonW = 26.0f;
        constexpr float kBarW = 8.0f + kInputW + 6.0f + kTallyW +
                                kButtonW * 2.0f + 4.0f + kButtonW + 6.0f;
        constexpr float kBarH = 38.0f;
        const float bx = std::max(12.0f, paneW - kBarW - 18.0f);
        auto bar = div(ctx, mk(parent, 7500),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kBarW), pixels(kBarH)})
                .with_absolute_position()
                .with_translate(bx, 52.0f)
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.right = pixels(6), .left = pixels(8)})
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_corner_radius(hanabi::surface::kMenuCorner)
                .with_render_layer(9)
                .with_debug_name("find_bar"));

        ctx.theme.secondary = theme::over(theme::panel_bg_2(),
                                          theme::panel_bg());
        ctx.theme.surface = ctx.theme.secondary;
        ctx.theme.font = theme::text_primary();
        // Re-asserted, not inherited: ctx.theme is one global struct read at
        // render time, so whatever the sidebar left in font_muted is what this
        // pane's muted text gets (gap #90).
        ctx.theme.font_muted = theme::text_faint();
        // No padding on the field: text_input's own inner element is sized to
        // the element's OUTER width, so any padding here makes the child wider
        // than the content box it sits in and afterhours warns every frame
        // (and warns is the good case — it also re-solves the layout). The
        // inset comes from the bar's padding instead.
        afterhours::ui::imm::text_input(
            ctx, mk(bar.ent(), 1), pane.findQuery,
            hanabi::surface::field(kInputW, 9, 28.0f)
                .with_debug_name("find_input"));

        // "3 of 12", or "no matches" once something has been typed.
        std::string tally;
        if (!pane.findQuery.empty())
            tally = matchCount == 0
                        ? std::string("no matches")
                        : (std::to_string(pane.findIndex + 1) + " of " +
                           std::to_string(matchCount));
        div(ctx, mk(bar.ent(), 2),
            ComponentConfig{}
                .with_label(tally)
                .with_size(ComponentSize{pixels(kTallyW), pixels(16)})
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
                    .with_size(ComponentSize{pixels(kButtonW), pixels(kButtonW)})
                    .with_custom_background(theme::panel_bg_2())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(hanabi::surface::kControlCorner)
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
                apply_find_step(pane, up ? hanabi::find_nav::Step::Prev
                                         : hanabi::find_nav::Step::Next);
            }
        };
        step(3, true);
        step(4, false);

        auto close = button(ctx, mk(bar.ent(), 5),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(kButtonW), pixels(kButtonW)})
                .with_margin(Margin{.left = pixels(4)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_render_layer(9)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "close", "\xc3\x97", theme::text_secondary(), 12.0f))
                .with_debug_name("find_close"));
        if (close) {
            pane.findOpen = false;
            pane.findQuery.clear();
            app.refocusComposer = true;
        }

        // An operator we do not have would otherwise vanish into the plain
        // text and quietly widen the search — the query would say one thing
        // and the tally another. Say so instead, under the bar so the row's
        // fixed width is untouched.
        //
        // The same slot carries the other thing the bar cannot say in 74px:
        // that the thread is WINDOWED. Opening a thread fetches its newest 40
        // messages (LoaderSystem::kMessagesWindow) and older ones arrive on
        // demand, so "no matches" over a 480-message thread meant "not in the
        // 40 we have" and read as "not in this conversation" (docs/SEARCH.md
        // S9). The tally is honest about the thread it can see; this says how
        // much of the thread that is. The hint wins the slot when both apply —
        // a malformed operator makes the count meaningless, so it is the more
        // urgent of the two.
        const std::string_view note =
            find_ops::bar_note(q, !app.pane().findQuery.empty(), app.pane().hasMoreOlder);
        if (!note.empty())
            div(ctx, mk(parent, 7501),
                ComponentConfig{}
                    .with_label(std::string(note))
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
                           AppComponent& app, Pane& pane, float paneW,
                           float paneH) {
        const PaneBuildScope building(pane);
        // No transcript header. Puffin's pane begins at the first message: the
        // tab strip is the only thing above the transcript, and the thread's
        // identity lives in the tab caption. hanabi used to derive a display
        // title here and draw it over a muted age; both are gone for visual
        // parity (the commit message says what that costs a reader).

        if (pane.transcriptState == LoadState::Error) {
            note(ctx, parent, pane.transcriptError, paneW, pane);
            return;
        }
        if (!pane.openSession) {
            // Still fetching: the welcome hero here reads as "nothing here"
            // rather than "not yet". selectedId covers the frames before the
            // loader flips transcriptState; Error already returned above.
            const bool opening = !pane.selectedId.empty() ||
                                 pane.transcriptState == LoadState::Loading ||
                                 !pane.transcriptLoadingId.empty();
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
        if (pane.transcriptState == LoadState::Loading &&
            !pane.transcriptLoadingId.empty() &&
            pane.openSession->summary.id != pane.transcriptLoadingId) {
            loading_spinner(ctx, parent, "Loading conversation\xe2\x80\xa6");
            return;
        }

        // The composer is rendered ONCE at the pane level; paneH here is
        // already the CONTENT height (pane minus composer), so the transcript
        // fills it directly — no local composer reservation.
        const bool canReply =
            app.client &&
            (app.client->supports_send() || app.client->supports_stream());
        // No header above the scroll list any more (Puffin's pane starts at the
        // first message), so the list gets the whole pane height. Kept as a
        // named zero because three call sites below position overlays relative
        // to the top of the scroll list, and they should keep reading "below
        // whatever the header is" rather than hard-coding 0.
        constexpr float kHeaderH = 0.0f;
        float listH = paneH - kHeaderH;
        if (listH < 20.0f) listH = 20.0f;

        // Modern-chat centering: the transcript reads best in a ~720px column
        // centered in the pane. afterhours won't center a fixed child inside a
        // content-collapsing scroll, so we center via the scroll's OWN L/R
        // padding: gutter = (paneW - 720)/2, clamped to a sane minimum. On a
        // narrow pane the gutter floors and the column just uses the width.
        // 736 and symmetric: measured off the reference, where the transcript
        // content spans x=362..1097 in a 900px pane (82px of gutter a side).
        constexpr float kReadCol = 736.0f;
        float gutter = (paneW - kReadCol) * 0.5f;
        if (gutter < kContentInset) gutter = kContentInset;
        const float findClearance = pane.findOpen ? 33.0f : 0.0f;

        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                // Symmetric gutters center the reading column; a small extra
                // 6px is trimmed on the right for the overlay scrollbar strip.
                // The top inset is Puffin's: its transcript ScrollView pads
                // its content `EdgeInsets(top: 12, leading: 16, bottom: 12,
                // trailing: 16)`, and hanabi's 8 left the first turn sitting
                // 4px high against `ref/02_thread.png` — a constant offset
                // applied to every row in the pane, and worth 1.45 structural
                // points across the turns band on its own.
                .with_padding(Padding{.top = pixels(12.0f + findClearance),
                                      .right = pixels(gutter),
                                      .bottom = pixels(10),
                                      .left = pixels(gutter)})
                .with_debug_name(scroll_name(pane_index(app, pane))));
        // TEMPORARY scroll indicator (afterhours gap #26): afterhours has no
        // built-in scrollbar, so paint a thin overlay bar from the panel's live
        // HasScrollView metrics. The 14px right padding above already keeps the
        // reading column clear of the bar's right strip.
        // (scrollbar now drawn by afterhours)
        hanabi::apply_scroll_prefs(scroll.ent());

        static const bool emptyTranscriptDemo = [] {
            const char* v = std::getenv("HANABI_EMPTY_TRANSCRIPT_DEMO");
            return v != nullptr && *v != '\0' && std::string_view(v) != "0";
        }();
        if (pane.openSession->messages.empty() || emptyTranscriptDemo) {
            float emptyH = listH - 22.0f;
            if (emptyH < 96.0f) emptyH = 96.0f;
            auto empty = div(ctx, mk(scroll.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), pixels(emptyH)})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_padding(Padding{.right = pixels(18),
                                          .left = pixels(18)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("transcript_empty_anchor"));
            div(ctx, mk(empty.ent(), 1),
                ComponentConfig{}
                    .with_label(canReply ? "Start this conversation"
                                        : "No messages yet")
                    .with_size(ComponentSize{percent(1.0f), pixels(28)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::LG)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("transcript_empty_title"));
            div(ctx, mk(empty.ent(), 2),
                ComponentConfig{}
                    .with_label(canReply ? "Send a message below to begin."
                                        : "Replies are unavailable for this backend.")
                    .with_size(ComponentSize{percent(1.0f), pixels(24)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::BODY)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("transcript_empty_body"));
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

        probe_drawn_turns();
        hanabi::text_select::begin_frame();
        render_cache().reset_for_thread(
            model::pane_key(pane_index(app, pane), pane.openSession->summary.id));

        // Measured here, RENDERED further down: the rollup is the first thing
        // in the column, but a short thread gets a leading spacer in front of
        // it, and the spacer's size isn't known until every item is measured.
        const float subH = sub_agent_panel_height(app, pane);

        const bool streamingHere =
            app.streamActive &&
            app.streamSessionId == pane.openSession->summary.id &&
            app.streamPhase != AppComponent::StreamPhase::Done;
        const size_t liveIdx = app.streamMsgIndex;

        const auto& msgs = pane.openSession->messages;
        const int n = static_cast<int>(msgs.size());
        hanabi::prof::gauge("transcript.messages", msgs.size());
        const find_ops::Query findQ =
            pane.findOpen ? find_ops::parse(pane.findQuery) : find_ops::Query{};
        static const std::vector<Match> noMatches;
        const std::vector<Match>* matches = &noMatches;
        if (pane.findOpen) {
            hanabi::prof::Scope _pfind("find.collect");
            matches = &collect_matches(pane, *pane.openSession, findQ, colW,
                                       app.findFoldVersion);
            if (pane.findIndex >= static_cast<int>(matches->size()))
                pane.findIndex = 0;
        }
        pane.findCount = static_cast<int>(matches->size());

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
        const std::string& openId0 = pane.openSession->summary.id;
        // The mark lives in the ONE bounded per-thread store (ecs/pane_state.h)
        // rather than in a function-local map keyed by session id, which grew
        // an entry per thread ever opened, was never pruned, and could not be
        // counted from outside the function. The reference stays valid for the
        // rest of the frame: unordered_map does not move its elements, and the
        // entry we just touched is the most-recently-used one, so the two
        // touch() calls further down cannot evict it.
        model::PaneState& mark =
            model::pane_states().touch(model::pane_key(pane_index(app, pane), openId0));
        const int64_t lastRead = Settings::get().get_last_read(openId0);
        // Recomputed when the thread is first seen, and again if messages were
        // PREPENDED (load-older shifts every index, so a held index would
        // point at the wrong message).
        // Also recomputed when the persisted stamp changes underneath us. Our
        // OWN advance (reaching the end, below) writes mark.stamp too, so it
        // does not look like an external change and does not delete the line
        // the reader is looking at. Anything else — another window, a test
        // placing the boundary — is a real re-mark and is honoured.
        model::update_unread(mark, msgs, lastRead, pane.transcriptMutation);
        const int firstUnread = mark.unreadFirst;
        const int unreadCount = mark.unreadCount;

        model::TranscriptGeometryFacts geometry;
        geometry.pane_width = colW;
        geometry.show_date_dividers = show_date_dividers();
        geometry.show_reasoning = show_reasoning();
        geometry.fold_long_messages = fold_long_messages();
        geometry.tool_fold_mode = hanabi::fold::to_int(fold_mode());
        geometry.unread_first = firstUnread;
        geometry.unread_count = unreadCount;
        geometry.find_open = pane.findOpen;
        geometry.find_query = pane.findQuery;
        geometry.streaming = streamingHere;
        geometry.live_index = liveIdx;
        geometry.stream_phase = static_cast<int>(app.streamPhase);
        geometry.font_epoch = hanabi::text::font_epoch();

        model::TranscriptItemIndex::View itemView;
        {
            hanabi::prof::Scope _p("transcript.pass1_measure");
            itemView = model::transcript_item_index().update(
                model::pane_key(pane_index(app, pane),
                                pane.openSession->summary.id),
                msgs.data(), msgs.size(), pane.transcriptMutation, geometry,
                [&](std::size_t start, std::vector<Item>& built) {
                    int i = static_cast<int>(start);
                    while (i < n) {
                        if (geometry.show_date_dividers && i > 0 &&
                            starts_new_day(msgs[i - 1], msgs[i])) {
                            Item d;
                            d.kind = Item::DateDivider;
                            d.lo = i;
                            d.height = kDateDividerH;
                            built.push_back(d);
                        }
                        if (i == firstUnread) {
                            Item d;
                            d.kind = Item::NewDivider;
                            d.lo = i;
                            d.hi = unreadCount;
                            d.height = kNewDividerH;
                            built.push_back(d);
                        }
                        const auto& m = msgs[i];
                        if (is_spawn_tool(m)) {
                            Item it;
                            it.kind = Item::Spawn;
                            it.lo = i;
                            it.height = spawn_card_height();
                            built.push_back(it);
                            ++i;
                            continue;
                        }
                        if (is_delivery(m)) {
                            Item it;
                            it.kind = Item::Delivery;
                            it.lo = i;
                            it.height = delivery_height(app, m, i, colW);
                            built.push_back(it);
                            ++i;
                            continue;
                        }
                        if (is_one_line_event(m)) {
                            Item it;
                            it.kind = Item::Event;
                            it.lo = i;
                            it.height = event_row_height();
                            built.push_back(it);
                            ++i;
                            continue;
                        }
                        if (m.role == api::Role::Tool) {
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
                                built.push_back(it);
                                i = j;
                                continue;
                            }
                            Item it;
                            it.kind = Item::ToolBlock;
                            it.lo = i;
                            it.height = tool_block_height(app, msgs[i]);
                            built.push_back(it);
                            ++i;
                            continue;
                        }
                        if (is_thinking(m) &&
                            !(streamingHere && static_cast<size_t>(i) == liveIdx)) {
                            if (!geometry.show_reasoning) {
                                ++i;
                                continue;
                            }
                            Item it;
                            it.kind = Item::Thinking;
                            it.lo = i;
                            it.height = thinking_height(app, m, i, colW);
                            built.push_back(it);
                            ++i;
                            continue;
                        }
                        Item it;
                        it.kind = Item::Bubble;
                        it.lo = i;
                        it.isLive = streamingHere && static_cast<size_t>(i) == liveIdx;
                        it.showAuthor =
                            (i == 0) ||
                            (msgs[i - 1].role != api::Role::Assistant &&
                             msgs[i - 1].role != api::Role::Tool);
                        it.height = bubble_height(m, colW, it.isLive, i,
                                                  it.showAuthor);
                        built.push_back(it);
                        if (draws_outcome(m.run_outcome, i == n - 1)) {
                            Item ro;
                            ro.kind = Item::RunOutcome;
                            ro.lo = i;
                            ro.height = kRunOutcomeH + kRunOutcomeGapTop;
                            built.push_back(ro);
                        }
                        ++i;
                    }
                });
        }
        const std::vector<Item>& items = *itemView.items;
        float totalH = subH + itemView.height;
        hanabi::prof::tick("transcript.item_messages_visited",
                           itemView.messages_visited);
        hanabi::prof::tick(itemView.rebuilt ? "transcript.item_index_rebuild"
                                            : "transcript.item_index_hit");
        hanabi::prof::gauge("transcript.item_index_slots",
                            model::transcript_item_index().slots());
        hanabi::prof::gauge("transcript.item_index_items",
                            model::transcript_item_index().total_items());
        if (hanabi::mprobe::on())
            for (const Item& item : items)
                if (item.kind == Item::Bubble)
                    hanabi::mprobe::expect("turn#" + std::to_string(item.lo),
                                           item.height);

        // WHAT THE ITEM LIST ACTUALLY CONTAINS, as gauges. Not a perf number:
        // a gate's first job is to prove the scenario DROVE the thing it
        // claims to measure, and the whole reason scripts/events_gate.sh
        // exists is that every existing gate ran over a transcript with none
        // of feat/event-model's row kinds in it and read the same number
        // before and after the merge. The gate fails when these are zero,
        // which is the difference between "the event rows cost nothing" and
        // "no event row was drawn".
        if (hanabi::prof::enabled()) {
            unsigned long long nEvent = 0, nDeliv = 0, nSpawn = 0, nThink = 0;
            for (const Item& it : items) {
                switch (it.kind) {
                    case Item::Event: ++nEvent; break;
                    case Item::Delivery: ++nDeliv; break;
                    case Item::Spawn: ++nSpawn; break;
                    case Item::Thinking: ++nThink; break;
                    default: break;
                }
            }
            hanabi::prof::gauge("items.total", items.size());
            hanabi::prof::gauge("items.event", nEvent);
            hanabi::prof::gauge("items.delivery", nDeliv);
            hanabi::prof::gauge("items.spawn", nSpawn);
            hanabi::prof::gauge("items.thinking", nThink);
        }
        // ---- Virtualization: read last frame's scroll to skip off-screen. --
        float scrollY = 0.0f;
        float viewH = listH;
        if (scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            scrollY = sv.scroll_offset.y;
            if (sv.viewport_or_zero().y > 1.0f)
                viewH = sv.viewport_or_zero().y;
        }

        // ---- Load-older: scroll-anchor + trigger + prefetch ---------------
        // (a) ANCHOR: when older messages were just prepended (loader armed
        //     anchorPending), the content grew above the viewport. Measure the
        //     height of the newly-prepended items and bump scroll_offset by it,
        //     so the user's view stays on the same message instead of snapping
        //     to the newly-loaded oldest. Cleared after one application.
        const std::string openId = pane.openSession->summary.id;
        if (pane.anchorPending == openId &&
            msgs.size() > pane.anchorPrevMsgCount &&
            scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const float prependedH =
                std::max(0.0f, itemView.height - itemView.previous_height);
            auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
            sv.scroll_offset.y += prependedH;  // hold the viewport steady
            sv.clamp_scroll();
            scrollY = sv.scroll_offset.y;
            pane.anchorPending.clear();
        }
        // (b) TRIGGER + PREFETCH: when the user is near the TOP and there are
        //     older messages, request a load. A generous threshold (2 viewports)
        //     PREFETCHES before the user hits the very top, so older content is
        //     usually already there by the time they reach it — and reaching
        //     the top faster (fast scroll-up) just triggers sooner. Guarded by
        //     loadingOlder (loader clears it) so it fires once per page.
        if (pane.hasMoreOlder && !pane.loadingOlder && !pane.requestLoadOlder &&
            pane.anchorPending.empty() && scrollY <= viewH * 2.0f) {
            pane.requestLoadOlder = true;
        }
        // Jump the current match into view. The item list carries every
        // message's measured height, so the y of the message holding the match
        // is the sum of the heights before it; a third of a viewport of lead-in
        // puts it comfortably inside the pane rather than flush at the top.
        if (pane.findScrollPending && !matches->empty() &&
            scroll.ent().has<afterhours::ui::HasScrollView>()) {
            const int target =
                (*matches)[static_cast<size_t>(pane.findIndex)].msg;
            float y = subH + findClearance;
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
            pane.findScrollPending = false;
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
        //     (newest message). This is driven by pane.scrollBottomPending, set
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
        const std::string curId = pane.openSession->summary.id;
        const bool wantOpenBottom = (pane.scrollBottomPending == curId);

        // ---- Persistent FOLLOW-LATCH (fixes "page doesn't stay at bottom") ---
        // A per-session latch meaning "keep pinned to bottom". It starts TRUE
        // (fresh opens pin to bottom), is broken when the reader scrolls up,
        // and re-arms when they come back to the end — which is robust to the
        // content-growth race a per-frame geometry test loses (a token landing
        // grows contentH while the offset stays put, so `offset + viewH >=
        // contentH - 24` flips FALSE the instant the thing being followed
        // arrives, and following stops one message from the end).
        //
        // The arithmetic is model::step_follow_latch (ecs/follow_latch.h),
        // pulled out of this function because the version that lived here read
        // the reader's intent off `scroll_offset` — the field the EASING
        // writes — instead of `scroll_target`, the field the WHEEL writes, and
        // so re-armed itself on the same frame every wheel notch broke it. The
        // header has the full account; the short version is that the mouse
        // wheel moved a thread exactly zero pixels.
        //
        // Keyed PER SESSION (not a single static) so split-view can render two
        // transcripts in one frame without their follow/scroll state clobbering
        // each other. Each pane's curId indexes its own latch.
        model::PaneState& mem =
            model::pane_states().touch(model::pane_key(pane_index(app, pane), curId));
        model::FollowMemory& latch = mem.latch;
        // A reference, because the minimap rail below is handed the latch and
        // breaks it when a drag scrubs backwards.
        bool& s_follow = latch.follow;
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
            nearEnd = model::step_follow_latch(
                          latch, model::FollowInput{curOffset,
                                                    sv.scroll_target.y, viewH,
                                                    contentH})
                          .nearEnd;
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
                model::note_follow_pinned(latch, scrollY, sv.scroll_target.y);
            }
        }

        // Pin to bottom on a first-open, while streaming here, or whenever the
        // follow-latch is engaged (user hasn't scrolled up).
        const bool atBottom = s_follow || nearEnd;
        // Find owns the scroll while it is open with a query: the bottom-pin
        // would drag the view back off the match the moment it landed.
        const bool findDriving = pane.findOpen && !findQ.text.empty();
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
        // A first sight has velocity 0, not -scrollY: the flag is what the
        // map's find()-vs-end() used to say.
        float vel = mem.haveLastScrollY ? scrollY - mem.lastScrollY : 0.0f;
        mem.lastScrollY = scrollY;
        mem.haveLastScrollY = true;
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
        // NO short-thread bottom anchor. hanabi used to insert a leading spacer
        // of the whole slack so a two-message thread sat just above the
        // composer, chat-log style. Puffin does not: its two-message thread
        // starts directly under the tab strip with the dead space BELOW. The
        // spacer is gone so a short thread top-anchors the way the reference
        // does. (Long threads are unaffected — they never had one.)
        sub_agent_panel(ctx, col, app, pane);
        {
        hanabi::prof::Scope _p2("transcript.pass2_build");
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
                case Item::RunOutcome:
                    run_outcome_divider(ctx, col, it.lo,
                                        msgs[it.lo].run_outcome, colW);
                    break;
                case Item::Thinking:
                    render_thinking_block(ctx, col, it.lo, msgs[it.lo], app,
                                          colW);
                    break;
                case Item::Event:
                    render_event_row(ctx, col, it.lo, msgs[it.lo], colW);
                    break;
                case Item::Delivery:
                    render_delivery_row(ctx, col, it.lo, msgs[it.lo], app,
                                        colW);
                    break;
            }
        }
        flush_spacer(99999);
        }

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
                mark.unreadStamp = newest;  // our own write, not a re-mark
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
            // Both fields, so the next frame does not read our own pin back as
            // a gesture — on either of the two signals the latch watches.
            model::note_follow_pinned(latch, sv.scroll_offset.y,
                                      sv.scroll_target.y);
        }
        // Clear the first-open request only once we've pinned against REAL
        // laid-out content (content_size known this frame). Until then keep the
        // request alive so the next frame (which HAS the laid-out height) does
        // the real jump-to-bottom. Guard on curId so a fast switch to another
        // new thread doesn't clear the wrong pending id.
        if (wantOpenBottom && contentLaidOut &&
            pane.scrollBottomPending == curId) {
            pane.scrollBottomPending.clear();
        }

        // The rail. Rendered after the pin above so a click on it wins the
        // frame it happens in, exactly as the jump-to-bottom button does — and
        // so does a drag, which writes the scroll on every frame it is held.
        {
            hanabi::prof::Scope _pm("transcript.minimap");
            minimap_rail(ctx, parent, scroll.ent(), items, msgs, subH, paneW,
                         kHeaderH, listH, totalH, viewH, scrollY, s_follow,
                         mem.minimapDrag, mem, itemView.rebuilt);
        }

        // Floating "jump to bottom" affordance: a small down-chevron pinned to
        // the bottom-right of the transcript pane, shown only when the user is
        // scrolled UP (not at the bottom) and there's meaningfully more below.
        // Clicking snaps to the newest message. (Rendered on the parent pane,
        // absolutely positioned, above the scroll content.)
        if (!pinBottom && !atBottom && contentH > viewH + 40.0f) {
            jump_to_bottom_button(ctx, parent, scroll.ent(), app, pane, paneW,
                                  46.0f + listH);
        }

        // Top "loading older" pill: a small centered spinner + caption pinned
        // to the TOP of the transcript pane while a load-older fetch is in
        // flight, so the load has visible feedback instead of a silent
        // freeze/snap. Overlay (absolute on the parent pane) so it doesn't
        // shift the scroll content / fight the anchor math. kHeaderH offsets it
        // below the title header.
        if (pane.loadingOlder) {
            loading_older_pill(ctx, parent, paneW, kHeaderH + 6.0f);
        }
        if (pane.findOpen)
            find_bar(ctx, parent, app, pane, paneW, pane.findCount, findQ);
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
    // app.pane().openSession->messages) and clears the local draft. When the backend
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
        if (!app.modelPopoverOpen && !modelPopoverWasOpen_) return;
        auto popRoot = mk(parent, 3200);
        RectangleType anchor =
            anchorEnt.get<afterhours::ui::UIComponent>().rect();
        anchor.y -= 24.0f;
        if (!app.modelPopoverOpen) {
            afterhours::ui::imm::popover(
                ctx, popRoot, anchor, app.modelPopoverOpen,
                afterhours::ui::overlay::Placement::Above);
            modelPopoverWasOpen_ = false;
            return;
        }
        modelPopoverWasOpen_ = true;
        constexpr float kRowH = 28.0f;
        constexpr float kPopW = 252.0f;
        constexpr float kHeadH = 44.0f;
        const auto& models = hanabi::models::all();
        const auto rowMetrics = afterhours::ui::imm::measure_config(
            hanabi::surface::option_row(kPopW - 8.0f, kRowH, false, 8),
            kPopW - 8.0f, kRowH);
        const float popH =
            kHeadH + rowMetrics.pitch().y * static_cast<float>(models.size()) +
            8.0f;
        const auto previousSurface = ctx.theme.surface;
        ctx.theme.surface = theme::panel_bg_2();
        auto pop = afterhours::ui::imm::popover(
            ctx, popRoot, anchor, app.modelPopoverOpen,
            afterhours::ui::overlay::Placement::Above,
            hanabi::surface::menu(kPopW, popH, 7)
                .with_debug_name("model_popover"));
        ctx.theme.surface = previousSurface;
        if (!pop) return;
        div(ctx, mk(pop.ent(), 900),
            ComponentConfig{}
                .with_label("Model")
                .with_size(ComponentSize{pixels(kPopW - 8.0f), pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_margin(Margin{.left = pixels(8)})
                .with_debug_name("model_popover_title"));
        div(ctx, mk(pop.ent(), 901),
            ComponentConfig{}
                .with_label("Default for new tasks")
                .with_size(ComponentSize{pixels(kPopW - 8.0f), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_margin(Margin{.left = pixels(8)})
                .with_debug_name("model_popover_subtitle"));
        for (size_t i = 0; i < models.size(); ++i) {
            const auto& model = models[i];
            const bool selected = model.id == currentModel;
            auto row = button(
                ctx, mk(pop.ent(), static_cast<int>(i)),
                hanabi::surface::option_row(kPopW - 8.0f, kRowH,
                                            selected, 8,
                                            theme::panel_bg_2())
                    .with_margin(Margin{.left = pixels(4.0f),
                                        .right = pixels(4.0f)})
                    .with_label(std::string(model.name))
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_on_draw_fg([selected](RectangleType r) {
                        hanabi::glyph::radio(
                            RectangleType{r.x + 9.0f, r.y, 12.0f, r.height},
                            selected,
                            selected ? theme::accent() : theme::text_faint());
                    })
                    .with_debug_name("model_row_" + std::to_string(i)));
            row.ent().get<afterhours::ui::HasLabel>().set_text_inset(
                Vector2Type{28.0f, 0.0f});
            row.ent().get<afterhours::ui::HasLabel>().text_x_offset = 23.0f;
            if (row) {
                Settings::get().set_default_model(std::string(model.id));
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
    // app.pane().openSession->messages) and clears the local draft. When the backend
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
        if (!app.effortPopoverOpen && !effortPopoverWasOpen_) return;
        auto popRoot = mk(parent, 3300);
        RectangleType anchor =
            anchorEnt.get<afterhours::ui::UIComponent>().rect();
        anchor.y -= 24.0f;
        if (!app.effortPopoverOpen) {
            afterhours::ui::imm::popover(
                ctx, popRoot, anchor, app.effortPopoverOpen,
                afterhours::ui::overlay::Placement::Above);
            effortPopoverWasOpen_ = false;
            return;
        }
        effortPopoverWasOpen_ = true;
        constexpr float kRowH = 34.0f;
        constexpr float kPopW = 286.0f;
        constexpr float kHeadH = 44.0f;
        const auto& levels = hanabi::effort::all();
        const float popH = kHeadH +
                           kRowH * static_cast<float>(levels.size()) + 8.0f;
        const auto previousSurface = ctx.theme.surface;
        ctx.theme.surface = theme::panel_bg_2();
        auto pop = afterhours::ui::imm::popover(
            ctx, popRoot, anchor, app.effortPopoverOpen,
            afterhours::ui::overlay::Placement::Above,
            hanabi::surface::menu(kPopW, popH, 7)
                .with_debug_name("effort_popover"));
        ctx.theme.surface = previousSurface;
        if (!pop) return;
        div(ctx, mk(pop.ent(), 900),
            ComponentConfig{}
                .with_label("Thinking effort")
                .with_size(ComponentSize{pixels(kPopW - 8.0f), pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_margin(Margin{.left = pixels(8)})
                .with_debug_name("effort_popover_title"));
        div(ctx, mk(pop.ent(), 901),
            ComponentConfig{}
                .with_label("Default for new tasks")
                .with_size(ComponentSize{pixels(kPopW - 8.0f), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_margin(Margin{.left = pixels(8)})
                .with_debug_name("effort_popover_subtitle"));
        for (size_t i = 0; i < levels.size(); ++i) {
            const auto& level = levels[i];
            const bool selected = level.id == currentEffort;
            auto row = button(
                ctx, mk(pop.ent(), static_cast<int>(i)),
                hanabi::surface::option_row(kPopW - 8.0f, kRowH,
                                            selected, 8,
                                            theme::panel_bg_2())
                    .with_margin(Margin{.left = pixels(4.0f),
                                        .right = pixels(4.0f)})
                    .with_label(std::string(level.name) + "   \xc2\xb7   " +
                                std::string(level.note))
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_on_draw_fg([selected](RectangleType r) {
                        hanabi::glyph::radio(
                            RectangleType{r.x + 9.0f, r.y, 12.0f, r.height},
                            selected,
                            selected ? theme::accent()
                                     : theme::text_faint());
                    })
                    .with_debug_name("effort_row_" + std::to_string(i)));
            row.ent().get<afterhours::ui::HasLabel>().set_text_inset(
                Vector2Type{28.0f, 0.0f});
            row.ent().get<afterhours::ui::HasLabel>().text_x_offset = 23.0f;
            if (row) {
                Settings::get().set_default_effort(std::string(level.id));
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

    static std::size_t open_ask_index(const AppComponent& app) {
        const auto* asks = app.asks_for(app.pane().openSession->summary.id);
        if (asks == nullptr || asks->empty()) return 0;
        std::vector<hanabi::ask::AskShown> rows;
        rows.reserve(asks->size());
        for (const auto& a : *asks) {
            hanabi::ask::AskShown row;
            row.chosen = a.id() == app.askState.shownId;
            const auto at = app.askState.answers.find(a.id());
            row.has_draft = at != app.askState.answers.end() &&
                            hanabi::ask::has_draft(a, at->second);
            rows.push_back(row);
        }
        return hanabi::ask::shown_index(rows);
    }

    static const api::PendingAsk* open_ask(const AppComponent& app) {
        if (app.view != SmartView::Chat || !app.pane().openSession)
            return nullptr;
        const auto* asks = app.asks_for(app.pane().openSession->summary.id);
        if (asks == nullptr || asks->empty()) return nullptr;
        return &(*asks)[open_ask_index(app)];
    }

    static constexpr float kComposerBaseH = 98.0f;
    static constexpr float kAskMinTranscriptH = 120.0f;
    static constexpr float kAskMinCardW = 240.0f;
    static constexpr float kAskActionW = 96.0f;
    static constexpr float kAskActionGap = 8.0f;
    static constexpr float kAskScrollbarW = 10.0f;
    static constexpr float kAskArityW = 64.0f;

    static float ask_arity_w(float rowW) {
        const float half = rowW * 0.5f;
        if (half <= 0.0f) return 0.0f;
        return kAskArityW < half ? kAskArityW : half;
    }
    static constexpr float kAskOptionInset = 26.0f;

    static float ask_option_text_w(float bodyTextW) {
        return bodyTextW - kAskOptionInset * 2.0f;
    }
    static constexpr const char* kAskFileHint =
        "A file cannot be attached from hanabi.";
    static constexpr const char* kAskChildUnknownHint =
        "Couldn't reach the sub-agent — answer this in its own thread.";
    static constexpr const char* kAskChildFileHint =
        "This one may take a file — answer it in the sub-agent's thread.";
    static constexpr float kComposerReadCol = 768.0f;
    static constexpr float kComposerColInset = 12.0f;

    static std::string ask_retry_label(api::AskAction failed,
                                       const std::string& submitLabel,
                                       const std::string& declineLabel) {
        switch (failed) {
            case api::AskAction::Accept: return submitLabel;
            case api::AskAction::Decline: return declineLabel;
            case api::AskAction::Cancel: return "Escape";
        }
        return submitLabel;
    }

    static void ask_set_tab_stop(Entity& e, bool focusable) {
        const bool skipping = e.has<afterhours::ui::SkipWhenTabbing>();
        if (focusable && skipping)
            e.removeComponent<afterhours::ui::SkipWhenTabbing>();
        else if (!focusable && !skipping)
            e.addComponent<afterhours::ui::SkipWhenTabbing>();
    }

    static bool ask_theme_is_dark() { return theme::is_dark(); }

    static theme::Color ask_disabled_border() {
        const theme::Color c = theme::border();
        const theme::Color bg = theme::panel_bg_2();
        const auto mix = [](unsigned char a, unsigned char b) {
            return static_cast<unsigned char>((static_cast<int>(a) + b) / 2);
        };
        return theme::Color{mix(c.r, bg.r), mix(c.g, bg.g), mix(c.b, bg.b),
                            c.a};
    }

    static void draw_ask_action_label(RectangleType r, const std::string& text,
                                      theme::Color ink) {
        const float px = theme::type::SM;
        const float w = theme::text_px(text.c_str(), px);
        const float x = r.x + (r.width - w) * 0.5f;
        const float y = r.y + (r.height - px) * 0.5f;
        afterhours::draw_text(text.c_str(), x, y, px, ink);
    }

    static theme::Color ask_disabled_ink() {
        return theme::ask_action_disabled_ink();
    }

    static theme::Color ask_enabled_action_ink() {
        return theme::ask_action_enabled_ink();
    }

    static int ask_action_count(const AppComponent& app) {
        if (!app.pane().openSession) return 2;
        const auto* all = app.asks_for(app.pane().openSession->summary.id);
        return (all != nullptr && all->size() > 1) ? 3 : 2;
    }

    static float ask_action_w(const AppComponent& app) {
        const float n = static_cast<float>(ask_action_count(app));
        const float inner = ask_text_w(app);
        const float each = (inner - kAskActionGap * (n - 1.0f)) / n;
        if (each >= kAskActionW) return kAskActionW;
        return each < 1.0f ? 1.0f : each;
    }

    static float ask_card_w(const AppComponent& app) {
        const float paneW = app.lastComposerPaneW > 0.0f
                                ? app.lastComposerPaneW
                                : hanabi::viewport::width();
        float gutter = (paneW - kComposerReadCol) * 0.5f + kComposerColInset;
        if (gutter < kContentInset) gutter = kContentInset;
        const float w = paneW - gutter * 2.0f;
        const float widest = paneW - kContentInset * 2.0f;
        const float want = kAskMinCardW < widest ? kAskMinCardW : widest;
        return w < want ? want : w;
    }

    static float ask_text_w(const AppComponent& app) {
        return ask_card_w(app) - hanabi::ask::kPad * 2.0f;
    }

    static int ask_message_lines(const api::PendingAsk& ask, float textW) {
        if (ask.message.empty()) return 0;
        return hanabi::ask::clamp_message_lines(
            count_lines(ask.message, textW, theme::type::SM));
    }

    static std::string ask_input_text(const api::PendingAsk& ask) {
        return api::elicitation::capped_input(ask.input);
    }

    // The approval body reserves height for exactly the spans that will be
    // DRAWN, by asking the same wrapper the draw asks.
    //
    // count_lines() answers a different question: it is
    // wrapped_line_count(), which puts an over-long word on a line of its own
    // rather than splitting it (src/util/wrap_count.h says so -- "the first
    // word of an output line is accepted without being measured"). The draw
    // path goes through ask_wrap_spans(), which passes break_long_words=true.
    // A command line is nothing BUT over-long words, so the two disagreed by
    // a factor of two: at 340x620 the longapproval fixture reserved 8 lines
    // and the span list had 16, so render_ask_wrapped() ellipsised at line 8
    // and `--allow-outside-workspace` was never on screen -- with Approve
    // enabled, because the body reported itself as fitting.
    //
    // One wrapper, one answer. Height, scroll extent and tooShort all come
    // from this count, so none of them can disagree with the glyphs.
    static int ask_input_lines(const api::PendingAsk& ask, float textW) {
        if (ask.kind != api::AskKind::Approval || ask.input.empty()) return 0;
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        ask_wrap_spans(ask_input_text(ask), textW, spans);
        return spans.empty() ? 1 : static_cast<int>(spans.size());
    }

    static void ask_wrap_spans(
        const std::string& text, float textW,
        std::vector<std::pair<std::size_t, std::size_t>>& out) {
        using Spans = std::vector<std::pair<std::size_t, std::size_t>>;
        constexpr std::size_t kAskSpanEntries = 64;
        static hanabi::text::TextKeyCache<Spans> memo(kAskSpanEntries);
        if (const Spans* hit = memo.find(text, textW, theme::type::SM)) {
            hanabi::prof::tick("cache.ask_spans_hit");
            out = *hit;
            return;
        }
        hanabi::prof::tick("cache.ask_spans_miss");
        hanabi::text::wrapped_line_spans(
            text, text_wrap_width(textW),
            [](const std::string& s) {
                return afterhours::ui::measure_text_line(
                           s, afterhours::ui::UIComponent::DEFAULT_FONT,
                           theme::type::SM)
                    .x;
            },
            out, /*break_long_words=*/true);
        memo.put(text, textW, theme::type::SM, out);
        hanabi::prof::gauge("cache.ask_spans_entries", memo.size());
    }

    static int ask_note_lines(const std::string& note, float textW) {
        if (note.empty()) return 1;
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        ask_wrap_spans(note, textW, spans);
        return hanabi::ask::clamp_note_lines(
            spans.empty() ? 1 : static_cast<int>(spans.size()));
    }

    static std::string ask_wrapped_line(
        const std::string& text,
        const std::vector<std::pair<std::size_t, std::size_t>>& spans,
        std::size_t index) {
        if (index >= spans.size()) return std::string();
        return text.substr(spans[index].first,
                           spans[index].second - spans[index].first);
    }

    static const std::vector<hanabi::ask::QuestionMetrics>& ask_metrics(
        const api::PendingAsk& ask, float textW) {
        static std::string memoId;
        static float memoW = -1.0f;
        static unsigned memoEpoch = 0;
        static std::vector<hanabi::ask::QuestionMetrics> memo;
        const unsigned epoch = hanabi::text::font_epoch();
        const std::string key = hanabi::ask::metrics_key(ask);
        if (memoW == textW && memoEpoch == epoch && memoId == key) {
            hanabi::prof::tick("cache.ask_metrics_hit");
            return memo;
        }
        hanabi::prof::tick("cache.ask_metrics_miss");
        memoId = key;
        memoW = textW;
        memoEpoch = epoch;
        memo = build_ask_metrics(ask, textW);
        return memo;
    }

    static std::vector<hanabi::ask::QuestionMetrics> build_ask_metrics(
        const api::PendingAsk& ask, float textW) {
        std::vector<hanabi::ask::QuestionMetrics> out;
        if (ask.kind == api::AskKind::Approval) return out;
        out.reserve(ask.questions.size());
        for (const api::AskQuestion& q : ask.questions) {
            hanabi::ask::QuestionMetrics m;
            m.prompt_lines = hanabi::ask::clamp_prompt_lines(
                count_lines(q.prompt, textW - ask_arity_w(textW),
                            theme::type::SM));
            m.option_lines.reserve(q.options.size());
            for (const api::AskOption& o : q.options)
                m.option_lines.push_back(hanabi::ask::clamp_option_lines(
                    count_lines(ask_option_text(o), ask_option_text_w(textW),
                                theme::type::SM)));
            out.push_back(std::move(m));
        }
        return out;
    }

    static std::string ask_option_text(const api::AskOption& o) {
        return o.detail.empty() ? o.label : o.label + "  —  " + o.detail;
    }

    static bool ask_can_resolve(const AppComponent& app) {
        return app.client != nullptr && app.client->supports_resolve_ask();
    }

    static bool ask_expired(const AppComponent& app,
                            const api::PendingAsk& ask) {
        const auto at = app.askState.seenAt.find(ask.id());
        if (at == app.askState.seenAt.end()) return false;
        return hanabi::ask::expired_at(
            ask.timeout_ms, at->second,
            static_cast<int64_t>(std::time(nullptr)), ask.deadline_unix_ms);
    }

    static bool ask_note_shown(const AppComponent& app,
                               const api::PendingAsk& ask) {
        if (!ask_can_resolve(app)) return true;
        if (ask_expired(app, ask)) return true;
        if (!app.askState.errorText.empty() &&
            app.askState.errorId == ask.id())
            return true;
        if (app.askState.busyId == ask.id()) return true;
        if (ask.has_file_question()) return true;
        const auto known = app.askState.answers.find(ask.id());
        const api::AskAnswer empty;
        return hanabi::ask::submit_blocked(
            ask, known == app.askState.answers.end() ? empty : known->second);
    }

    static bool ask_tight_row(const AppComponent& app,
                              const api::PendingAsk& ask) {
        const float actionW = ask_action_w(app);
        const auto fits = [actionW](const char* a, const char* b) {
            const float pad = 12.0f;
            return theme::text_px(a, theme::type::SM) + pad <= actionW &&
                   theme::text_px(b, theme::type::SM) + pad <= actionW;
        };
        return ask.kind == api::AskKind::Approval ? !fits("Approve", "Deny")
                                                  : !fits("Submit", "Decline");
    }

    static std::string ask_submit_label(const AppComponent& app,
                                        const api::PendingAsk& ask) {
        const bool tight = ask_tight_row(app, ask);
        if (ask.kind == api::AskKind::Approval)
            return tight ? "OK" : "Approve";
        return tight ? "Send" : "Submit";
    }

    static std::string ask_decline_label(const AppComponent& app,
                                         const api::PendingAsk& ask) {
        const bool tight = ask_tight_row(app, ask);
        if (ask.kind == api::AskKind::Approval) return tight ? "No" : "Deny";
        return tight ? "Skip" : "Decline";
    }

    static std::string ask_note_text(const AppComponent& app,
                                     const api::PendingAsk& ask,
                                     bool tooShort) {
        const std::string askId = ask.id();
        if (app.askState.busyId == askId) return "Sending your answer…";
        if (!ask_can_resolve(app))
            return "This backend cannot answer an agent's question.";
        if (ask_expired(app, ask))
            return "This question timed out — the agent stopped waiting.";
        if (!app.askState.errorText.empty() && app.askState.errorId == askId)
            return app.askState.errorText + " — press " +
                   ask_retry_label(app.askState.errorAction,
                                   ask_submit_label(app, ask),
                                   ask_decline_label(app, ask)) +
                   " to try again.";
        if (tooShort)
            return "Too short to show the questions — make the window taller.";
        const auto known = app.askState.answers.find(askId);
        const api::AskAnswer none;
        const api::AskAnswer& answer =
            known == app.askState.answers.end() ? none : known->second;
        if (hanabi::ask::submit_blocked(ask, answer))
            return hanabi::ask::with_file_caveat(
                ask, hanabi::ask::blocked_reason(ask));
        return hanabi::ask::with_file_caveat(ask, std::string());
    }

    static theme::Color ask_note_ink(const AppComponent& app,
                                     const api::PendingAsk& ask,
                                     bool tooShort) {
        if (app.askState.busyId == ask.id()) return theme::text_secondary();
        if (!ask_can_resolve(app)) return theme::text_secondary();
        if (ask_expired(app, ask)) return theme::status_blocked();
        if (!app.askState.errorText.empty() &&
            app.askState.errorId == ask.id())
            return theme::status_blocked();
        if (tooShort) return theme::status_blocked();
        return theme::text_secondary();
    }

    static int ask_note_lines_for(const AppComponent& app,
                                  const api::PendingAsk& ask, bool tooShort) {
        if (!ask_note_shown(app, ask)) return 1;
        return ask_note_lines(ask_note_text(app, ask, tooShort),
                              ask_text_w(app));
    }

    static float ask_height_budget(const AppComponent& app) {
        const float contentH = app.lastPaneContentH > 0.0f
                                   ? app.lastPaneContentH
                                   : hanabi::viewport::height();
        const float chrome = app.lastComposerChromeH > 0.0f
                                 ? app.lastComposerChromeH
                                 : kComposerBaseH;
        float budget = contentH - chrome - kAskMinTranscriptH - 8.0f;
        const float withoutTranscript = contentH - chrome - 8.0f;
        if (budget < hanabi::ask::kMinBodyH)
            budget = withoutTranscript < hanabi::ask::kMinBodyH
                         ? withoutTranscript
                         : hanabi::ask::kMinBodyH;
        const api::PendingAsk* ask = open_ask(app);
        const float floor =
            ask == nullptr
                ? 0.0f
                : hanabi::ask::irreducible_h(
                      *ask, ask_note_lines_for(app, *ask, false));
        if (budget < floor) budget = floor;
        return budget < 0.0f ? 0.0f : budget;
    }

    static float ask_body_text_w(const AppComponent& app,
                                 const api::PendingAsk& ask) {
        static std::string memoId;
        static float memoTextW = -1.0f;
        static unsigned memoEpoch = 0;
        static float memo = 0.0f;
        const float textW = ask_text_w(app);
        const unsigned epoch = hanabi::text::font_epoch();
        if (memoTextW == textW && memoEpoch == epoch && memoId == ask.id())
            return memo;
        const float chrome = hanabi::ask::chrome_h(
            ask, ask_message_lines(ask, textW), ask_note_shown(app, ask),
            ask_note_lines_for(app, ask, false));
        const float budget = ask_height_budget(app) - chrome;
        const float narrow = textW - kAskScrollbarW;
        const float natural = hanabi::ask::body_h(
            ask, ask_input_lines(ask, narrow), ask_metrics(ask, narrow));
        memoId = ask.id();
        memoTextW = textW;
        memoEpoch = epoch;
        memo = natural > budget ? narrow : textW;
        return memo;
    }

    struct AskLayout {
        int noteLines = 1;
        int messageLines = 0;
        float chromeH = 0.0f;
        float bodyNaturalH = 0.0f;
        float bodyH = 0.0f;
        bool tooShort = false;
    };

    static AskLayout ask_layout(const AppComponent& app,
                                const api::PendingAsk& ask) {
        const float textW = ask_text_w(app);
        const float bodyTextW = ask_body_text_w(app, ask);
        const bool showNote = ask_note_shown(app, ask);
        const float budget = ask_height_budget(app);
        const int wantedMessageLines = ask_message_lines(ask, textW);
        AskLayout out;
        out.bodyNaturalH = hanabi::ask::body_h(
            ask, ask_input_lines(ask, bodyTextW), ask_metrics(ask, bodyTextW));
        for (int pass = 0; pass < 3; ++pass) {
            out.noteLines = ask_note_lines_for(app, ask, out.tooShort);
            out.messageLines = hanabi::ask::message_lines_for(
                ask, wantedMessageLines, showNote, out.noteLines, budget);
            out.chromeH = hanabi::ask::chrome_h(ask, out.messageLines, showNote,
                                                out.noteLines);
            out.bodyH = hanabi::ask::body_view_h(out.bodyNaturalH,
                                                 budget - out.chromeH);
            const bool next =
                hanabi::ask::body_too_short(out.bodyH, out.bodyNaturalH);
            if (next == out.tooShort || pass == 2) break;
            out.tooShort = next;
        }
        return out;
    }

    static float ask_card_h(const AppComponent& app) {
        const api::PendingAsk* ask = open_ask(app);
        if (ask == nullptr) return 0.0f;
        const float textW = ask_text_w(app);
        const float bodyW = ask_body_text_w(app, *ask);
        return hanabi::ask::card_h(*ask, ask_message_lines(*ask, textW),
                                   ask_note_shown(app, *ask),
                                   ask_layout(app, *ask).noteLines,
                                   ask_input_lines(*ask, bodyW),
                                   ask_metrics(*ask, bodyW),
                                   ask_height_budget(app)) +
               8.0f;
    }

    static void draw_ask_glyph(RectangleType r, api::AskControl control,
                               bool on) {
        const float cx = r.x + 12.0f;
        const float cy = r.y + r.height * 0.5f;
        const theme::Color ink = on ? theme::accent() : theme::text_secondary();
        if (control == api::AskControl::Multi) {
            afterhours::draw_rectangle_outline(
                RectangleType{cx - 5.5f, cy - 5.5f, 11.0f, 11.0f}, ink);
            if (on) {
                afterhours::draw_line_ex(afterhours::vec2{cx - 3.0f, cy},
                                         afterhours::vec2{cx - 1.0f, cy + 2.5f},
                                         1.6f, ink);
                afterhours::draw_line_ex(afterhours::vec2{cx - 1.0f, cy + 2.5f},
                                         afterhours::vec2{cx + 3.5f, cy - 3.0f},
                                         1.6f, ink);
            }
            return;
        }
        afterhours::draw_ring_segment(cx, cy, 4.6f, 5.6f, 0.0f, 360.0f, 24, ink);
        if (on) afterhours::draw_ring_segment(cx, cy, 0.0f, 2.8f, 0.0f, 360.0f,
                                              20, ink);
    }

    void submit_ask(AppComponent& app, const api::PendingAsk& ask,
                    api::AskAction action) {
        if (ask_expired(app, ask)) return;
        if (!app.askState.busyId.empty()) return;
        if (!app.pane().openSession) return;
        if (!app.client || !app.client->supports_resolve_ask()) return;
        if (action == api::AskAction::Accept &&
            hanabi::ask::submit_blocked(ask,
                                        app.askState.answer_for(ask.id())))
            return;
        // An approval the reader cannot SEE is not one they can give. The
        // measured condition for that is the layout's own tooShort -- the
        // body view is smaller than one option row, so the card is drawing
        // "Too short to show the questions" where the command should be.
        //
        // This used to ask input_unreadable(), which compared the widest
        // wrapped span against the column it was wrapped to. Spans wrapped to
        // a column always fit inside it, so the answer was false on every
        // input this app can build, and Approve stayed live behind it.
        if (action == api::AskAction::Accept && ask_layout(app, ask).tooShort)
            return;
        app.requestAskSessionId = app.pane().openSession->summary.id;
        app.requestAsk = ask;
        app.requestAskAction = action;
    }

    static void scroll_ask_cursor_into_view(Entity& body,
                                            const std::string& rowName) {
        if (!body.has<afterhours::ui::HasScrollView>()) return;
        auto& sv = body.get<afterhours::ui::HasScrollView>();
        const auto& bodyBox = body.get<afterhours::ui::UIComponent>();
        std::vector<afterhours::EntityID> pending(bodyBox.children.begin(),
                                                  bodyBox.children.end());
        while (!pending.empty()) {
            const auto childId = pending.back();
            pending.pop_back();
            auto opt = afterhours::ui::UICollectionHolder::getEntityForID(childId);
            if (!opt.valid() || !opt->has<afterhours::ui::UIComponent>())
                continue;
            const auto& box = opt->get<afterhours::ui::UIComponent>();
            if (!opt->has<afterhours::ui::UIComponentDebug>() ||
                opt->get<afterhours::ui::UIComponentDebug>().name_value !=
                    rowName) {
                pending.insert(pending.end(), box.children.begin(),
                               box.children.end());
                continue;
            }
            const auto row = box.rect();
            const auto view = bodyBox.rect();
            const float top = row.y - view.y + sv.scroll_offset.y;
            const float bottom = top + row.height;
            if (top < sv.scroll_target.y)
                sv.scroll_target.y = top;
            else if (bottom > sv.scroll_target.y + view.height)
                sv.scroll_target.y = bottom - view.height;
            return;
        }
    }

    afterhours::EntityID ask_row_id(const std::string& question,
                                    const std::string& option) const {
        for (const AskRowId& row : askRowIds_)
            if (*row.question == question && *row.option == option)
                return row.id;
        return 0;
    }

    void drive_ask_keyboard(UIContext<InputAction>& ctx, AppComponent& app,
                            const api::PendingAsk& ask, Entity* body,
                            bool widgetOwnsEnter, bool keysLive) {
        if (!keysLive) return;
        if (hanabi::keys::cmd_down() || hanabi::keys::shift_down() ||
            hanabi::keys::ctrl_down() || hanabi::keys::option_down())
            return;
        if (!app.askState.busyId.empty()) return;
        auto& cursor = app.askState.cursor_for(ask.id());
        auto& answer = app.askState.answer_for(ask.id());
        const bool editing = ecs::any_text_field_focused();
        if (!editing) {
            if (app.arrow == ArrowIntent::Ask) {
                const bool moved =
                    hanabi::keys::pressed(hanabi::keys::kDown) ||
                    hanabi::keys::pressed(hanabi::keys::kUp);
                if (hanabi::keys::pressed(hanabi::keys::kDown))
                    hanabi::ask::move_cursor(ask, &cursor, 1);
                if (hanabi::keys::pressed(hanabi::keys::kUp))
                    hanabi::ask::move_cursor(ask, &cursor, -1);
                if (moved && cursor.set()) {
                    if (body != nullptr)
                        scroll_ask_cursor_into_view(
                            *body, "ask_option_" + cursor.question + "_" +
                                       std::to_string(hanabi::ask::option_index(
                                           ask, cursor)));
                    if (const afterhours::EntityID rowId =
                            ask_row_id(cursor.question, cursor.option))
                        ctx.set_focus(rowId);
                }
            }
            if (hanabi::keys::pressed(hanabi::keys::kSpace))
                hanabi::ask::toggle_at_cursor(ask, cursor, &answer);
        }
        if (hanabi::keys::pressed(hanabi::keys::kEnter) && !widgetOwnsEnter) {
            askEnterRow_ = ask_row_id(cursor.question, cursor.option);
            switch (hanabi::ask::return_intent(
                ask, answer, editing ? hanabi::ask::Cursor{} : cursor)) {
                case hanabi::ask::ReturnIntent::Submit:
                    submit_ask(app, ask, api::AskAction::Accept);
                    break;
                case hanabi::ask::ReturnIntent::PickAtCursor:
                    if (!editing)
                        hanabi::ask::toggle_at_cursor(ask, cursor, &answer);
                    break;
                case hanabi::ask::ReturnIntent::Ignore:
                    break;
            }
        }
    }

    void render_ask_wrapped(UIContext<InputAction>& ctx, Entity& parent,
                            int keyBase, const std::string& text, int lines,
                            float textW, float lineH, theme::Color ink,
                            const std::string& name,
                            bool selectable = false) {
        if (lines <= 0) return;
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        ask_wrap_spans(text, textW, spans);
        for (int li = 0; li < lines; ++li) {
            const bool last = li == lines - 1;
            const bool cut = last && spans.size() > static_cast<size_t>(lines);
            std::string line =
                ask_wrapped_line(text, spans, static_cast<size_t>(li));
            if (cut) line = hanabi::ask::with_ellipsis(line);
            auto row = div(ctx, mk(parent, keyBase + li),
                ComponentConfig{}
                    .with_label(line)
                    .with_size(ComponentSize{percent(1.0f), pixels(lineH)})
                    .with_transparent_bg()
                    .with_custom_text_color(ink)
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(cut ? TextOverflow::Ellipsis
                                            : TextOverflow::Clip)
                    .with_debug_name(name + "_" + std::to_string(li)));
            if (selectable)
                selectable_text(ctx, row.ent(), line, theme::type::SM);
        }
    }

    void render_ask_card(UIContext<InputAction>& ctx,
                         afterhours::ui::imm::ElementResult& bar,
                         AppComponent& app, float gutter) {
        const api::PendingAsk* found = open_ask(app);
        if (found == nullptr) {
            app.askFocused = false;
            return;
        }
        const api::PendingAsk& ask = *found;
        const std::string askId = ask.id();
        const bool busy = app.askState.busyId == askId;
        const bool approval = ask.kind == api::AskKind::Approval;
        auto& answer = app.askState.answer_for(askId);
        auto& cursor = app.askState.cursor_for(askId);
        const bool blocked = hanabi::ask::submit_blocked(ask, answer);
        const bool answerable = ask_can_resolve(app);
        const bool showNote = ask_note_shown(app, ask);
        const float cardW = ask_card_w(app);
        const float textW = ask_text_w(app);
        const float bodyTextW = ask_body_text_w(app, ask);
        const int inputLines = ask_input_lines(ask, bodyTextW);
        const auto& metrics = ask_metrics(ask, bodyTextW);
        const AskLayout layout = ask_layout(app, ask);
        const int messageLines = layout.messageLines;
        askRowIds_.clear();
        const float chromeH = layout.chromeH;
        const float bodyNaturalH = layout.bodyNaturalH;
        const float bodyH = layout.bodyH;
        bool clicked = false;

        auto card = div(ctx, mk(bar.ent(), 7),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(cardW),
                                         pixels(chromeH + bodyH)})
                .with_margin(Margin{.top = pixels(8), .left = pixels(gutter)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(hanabi::ask::kPad),
                                      .right = pixels(hanabi::ask::kPad),
                                      .bottom = pixels(hanabi::ask::kPad),
                                      .left = pixels(hanabi::ask::kPad)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::accent(), pixels(1.0f))
                .with_corner_radius(8.0f)
                .with_debug_name("ask_card"));

        hanabi::prof::tick("ask.cards_drawn");
        if (bodyNaturalH > bodyH) hanabi::prof::tick("ask.cards_scrolling");

        const auto* asks = app.asks_for(app.pane().openSession->summary.id);
        std::string head = hanabi::ask::head_text(ask);
        if (asks != nullptr && asks->size() > 1)
            head += "  ·  " + std::to_string(open_ask_index(app) + 1) +
                    " of " + std::to_string(asks->size());
        if (!ask.tool.empty()) head += "  ·  " + ask.tool;
        div(ctx, mk(card.ent(), 1),
            ComponentConfig{}
                .with_label(head)
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::ask::kHeadH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::accent())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_debug_name("ask_head"));

        render_ask_wrapped(ctx, card.ent(), 2, ask.message, messageLines, textW,
                           hanabi::ask::kMessageH, theme::text_primary(),
                           "ask_message");

        const bool tooShort = layout.tooShort;

        const auto* tabStrip = find_singleton<TabStripComponent>();
        const bool tabMenu = tabStrip != nullptr && tabStrip->menuOpen;
        const bool keysLive = ecs::ask_keys_live(app, tabMenu);
        const bool inputLive = ecs::ask_input_live(app, tabMenu);

        auto body = div(ctx, mk(card.ent(), 10),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(bodyH)})
                .with_transparent_bg()
                .with_padding(Padding{.top = pixels(0),
                                      .right = pixels(textW - bodyTextW),
                                      .bottom = pixels(0),
                                      .left = pixels(0)})
                .with_debug_name("ask_body"));
        hanabi::apply_scroll_prefs(body.ent());

        auto content = div(ctx, mk(body.ent(), 11),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(bodyNaturalH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_debug_name("ask_body_content"));

        int key = 20;
        if (tooShort) {
            div(ctx, mk(content.ent(), key++),
                ComponentConfig{}
                    .with_label("Too short to show the questions — make the "
                                "window taller.")
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels(hanabi::ask::kNoteH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::status_blocked())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_debug_name("ask_too_short"));
        } else if (approval) {
            render_ask_wrapped(ctx, content.ent(), key, ask_input_text(ask),
                               inputLines, bodyTextW, hanabi::ask::kNoteH,
                               theme::text_secondary(), "ask_approval_input",
                               /*selectable=*/true);
            key += inputLines;
        } else if (ask.schema_unreadable) {
            div(ctx, mk(content.ent(), key++),
                ComponentConfig{}
                    .with_label("This build cannot draw the form this "
                                "question uses.")
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels(hanabi::ask::kNoteH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_debug_name("ask_unreadable"));
        } else if (ask.questions.empty()) {
            div(ctx, mk(content.ent(), key++),
                ComponentConfig{}
                    .with_label("This one asks for nothing but an answer.")
                    .with_size(ComponentSize{percent(1.0f),
                                             pixels(hanabi::ask::kNoteH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_debug_name("ask_no_questions"));
        }

        for (std::size_t qi = 0;
             qi < ask.questions.size() && !approval && !tooShort; ++qi) {
            const api::AskQuestion& q = ask.questions[qi];
            const char* arity = q.control == api::AskControl::Single
                                    ? "Pick one"
                                    : (q.control == api::AskControl::Multi
                                           ? "Pick any"
                                           : "");
            const hanabi::ask::QuestionMetrics& qm =
                metrics[static_cast<std::size_t>(qi)];
            auto promptRow = div(ctx, mk(content.ent(), key++),
                ComponentConfig{}
                    .with_size(ComponentSize{
                        percent(1.0f),
                        pixels(hanabi::ask::prompt_row_h(qm.prompt_lines))})
                    .with_margin(Margin{.top = pixels(hanabi::ask::kQuestionGap)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_transparent_bg()
                    .with_debug_name("ask_prompt_row_" + q.key));
            auto promptCol = div(ctx, mk(promptRow.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{
                        pixels(bodyTextW - ask_arity_w(bodyTextW)),
                        pixels(hanabi::ask::prompt_row_h(qm.prompt_lines))})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_transparent_bg()
                    .with_debug_name("ask_prompt_" + q.key));
            render_ask_wrapped(ctx, promptCol.ent(), 1, q.prompt,
                               qm.prompt_lines,
                               bodyTextW - ask_arity_w(bodyTextW),
                               hanabi::ask::kOptionLineH,
                               theme::text_secondary(),
                               "ask_prompt_line_" + q.key);
            if (*arity != '\0')
                div(ctx, mk(promptRow.ent(), 2),
                    ComponentConfig{}
                        .with_label(arity)
                        .with_size(ComponentSize{
                            pixels(ask_arity_w(bodyTextW)),
                            pixels(hanabi::ask::kOptionLineH)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Right)
                        .with_debug_name("ask_arity_" + q.key));

            if (q.control == api::AskControl::File) {
                div(ctx, mk(content.ent(), key++),
                    ComponentConfig{}
                        .with_label(ask.child_session.empty()
                                        ? kAskFileHint
                                        : (ask.child_keys_unknown
                                               ? kAskChildUnknownHint
                                               : kAskChildFileHint))
                        .with_size(ComponentSize{percent(1.0f),
                                                 pixels(hanabi::ask::kNoteH)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::ask_caveat_ink())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_debug_name("ask_file_" + q.key));
                continue;
            }

            if (q.control == api::AskControl::Text) {
                afterhours::ui::imm::text_input(
                    ctx, mk(content.ent(), key++), answer.text[q.key],
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f),
                                                 pixels(hanabi::ask::kFieldH)})
                        .with_border(theme::border(), pixels(1.0f))
                        .with_custom_text_color(theme::text_primary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_corner_radius(6.0f)
                        .with_disabled(!inputLive)
                        .with_debug_name("ask_text_" + q.key));
                continue;
            }

            int oi = 0;
            for (const api::AskOption& option : q.options) {
                const bool on = answer.picked(q.key, option.value);
                const bool atCursor = app.askFocused &&
                                      cursor.question == q.key &&
                                      cursor.option == option.value;
                const int optLines =
                    static_cast<std::size_t>(oi) < qm.option_lines.size()
                        ? qm.option_lines[static_cast<std::size_t>(oi)]
                        : 1;
                auto row = button(ctx, mk(content.ent(), key++),
                    ComponentConfig{}
                        .with_label(optLines > 1 ? std::string()
                                                 : ask_option_text(option))
                        .with_size(ComponentSize{
                            percent(1.0f),
                            pixels(hanabi::ask::option_row_h(optLines))})
                        .with_padding(Padding{.top = pixels(0),
                                              .right = pixels(0),
                                              .bottom = pixels(0),
                                              .left = pixels(0)})
                        .with_transparent_bg()
                        .with_custom_hover_bg(
                            theme::hover_over(theme::panel_bg_2()))
                        .with_custom_text_color(on ? theme::text_primary()
                                                   : theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_cursor(afterhours::ui::CursorType::Pointer)
                        .with_alignment(TextAlignment::Left)
                        .with_corner_radius(6.0f)
                        .with_click_activation(ClickActivationMode::Press)
                        .with_disabled(!inputLive)
                        .with_on_draw_fg([on, atCursor, optLines,
                                          control = q.control](RectangleType r) {
                            if (atCursor)
                                afterhours::draw_rectangle_outline(
                                    RectangleType{r.x, r.y, r.width, r.height},
                                    theme::accent());
                            RectangleType gr = r;
                            if (optLines > 1)
                                gr.height = hanabi::ask::kOptionH;
                            draw_ask_glyph(gr, control, on);
                        })
                        .with_debug_name("ask_option_" + q.key + "_" +
                                         std::to_string(oi)));
                if (optLines <= 1 && row.ent().has<afterhours::ui::HasLabel>()) {
                    auto& label = row.ent().get<afterhours::ui::HasLabel>();
                    label.set_text_inset(Vector2Type{26.0f, 0.0f});
                    label.text_x_offset = 21.0f;
                }
                if (optLines > 1) {
                    auto wrapCol = div(ctx, mk(row.ent(), 1),
                        ComponentConfig{}
                            .with_size(ComponentSize{
                                pixels(ask_option_text_w(bodyTextW)),
                                pixels(hanabi::ask::kOptionLineH *
                                       static_cast<float>(optLines))})
                            .with_margin(Margin{.left = pixels(kAskOptionInset)})
                            .with_flex_direction(FlexDirection::Column)
                            .with_flex_wrap(FlexWrap::NoWrap)
                            .with_transparent_bg()
                            .with_debug_name("ask_option_wrap_" + q.key + "_" +
                                             std::to_string(oi)));
                    render_ask_wrapped(ctx, wrapCol.ent(), 1,
                                       ask_option_text(option), optLines,
                                       ask_option_text_w(bodyTextW),
                                       hanabi::ask::kOptionLineH,
                                       on ? theme::text_primary()
                                          : theme::text_secondary(),
                                       "ask_option_line_" + q.key + "_" +
                                           std::to_string(oi));
                }
                askRowIds_.push_back({&q.key, &option.value, row.ent().id});
                ask_set_tab_stop(row.ent(), inputLive && !busy);
                if (row && !busy && inputLive &&
                    row.ent().id != askEnterRow_) {
                    hanabi::ask::toggle(ask, q.key, option.value, &answer);
                    cursor.question = q.key;
                    cursor.option = option.value;
                    clicked = true;
                }
                ++oi;
            }

            if (!q.free_text_key.empty()) {
                div(ctx, mk(content.ent(), key++),
                    ComponentConfig{}
                        .with_label("Or write your own answer  ·  " +
                                    q.free_text_label)
                        .with_size(ComponentSize{percent(1.0f),
                                                 pixels(hanabi::ask::kNoteH)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_debug_name("ask_other_label_" + q.key));
                afterhours::ui::imm::text_input(
                    ctx, mk(content.ent(), key++), answer.text[q.free_text_key],
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f),
                                                 pixels(hanabi::ask::kFieldH)})
                        .with_border(theme::border(), pixels(1.0f))
                        .with_custom_text_color(theme::text_primary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_corner_radius(6.0f)
                        .with_disabled(!inputLive)
                        .with_debug_name("ask_other_" + q.key));
            }
        }

        const bool tightRow = ask_tight_row(app, ask);
        const std::string submitLabel = ask_submit_label(app, ask);
        const std::string declineLabel = ask_decline_label(app, ask);

        if (showNote) {
            const std::string note = ask_note_text(app, ask, tooShort);
            if (!note.empty()) {
                const theme::Color ink = ask_note_ink(app, ask, tooShort);
                const float noteW = ask_text_w(app);
                const int rows = layout.noteLines;
                auto noteBox = div(ctx, mk(card.ent(), 900),
                    ComponentConfig{}
                        .with_size(ComponentSize{
                            percent(1.0f),
                            pixels(hanabi::ask::kNoteH *
                                   static_cast<float>(rows))})
                        .with_transparent_bg()
                        .with_flex_direction(FlexDirection::Column)
                        .with_debug_name("ask_note"));
                render_ask_wrapped(ctx, noteBox.ent(), 0, note, rows, noteW,
                                   hanabi::ask::kNoteH, ink, "ask_note_line");
            }
        }

        auto actions = div(ctx, mk(card.ent(), 901),
            ComponentConfig{}
                .with_size(ComponentSize{
                    percent(1.0f),
                    pixels(hanabi::ask::kButtonsH - hanabi::ask::kActionsGap)})
                .with_margin(Margin{.top = pixels(hanabi::ask::kActionsGap)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_debug_name("ask_actions"));

        const bool expired = ask_expired(app, ask);
        const bool submitOff = busy || blocked || !answerable || !inputLive ||
                               expired || tooShort;
        auto submit = button(ctx, mk(actions.ent(), 1),
            ComponentConfig{}
                .with_label(submitOff ? std::string() : submitLabel)
                .with_disabled(submitOff)
                .with_on_draw_fg([submitOff, submitLabel](RectangleType r) {
                    if (submitOff)
                        draw_ask_action_label(r, submitLabel,
                                              ask_disabled_ink());
                })
                .with_size(ComponentSize{pixels(ask_action_w(app)),
                                         pixels(28)})
                .with_custom_background(
                    submitOff ? theme::ask_action_disabled_fill()
                              : theme::accent())
                .with_border(submitOff ? ask_disabled_border()
                                       : theme::accent(),
                             pixels(1.0f))
                .with_custom_text_color(submitOff ? ask_disabled_ink()
                                                  : theme::window_bg())
                .with_font_size(theme::type::SM)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_corner_radius(6.0f)
                .with_click_activation(ClickActivationMode::Press)
                .with_debug_name("ask_submit"));
        ask_set_tab_stop(submit.ent(), !submitOff);
        if (submit && !submitOff && inputLive) {
            clicked = true;
            submit_ask(app, ask, api::AskAction::Accept);
        }

        const auto* allAsks =
            app.asks_for(app.pane().openSession->summary.id);
        if (allAsks != nullptr && allAsks->size() > 1) {
            auto next = button(ctx, mk(actions.ent(), 3),
                ComponentConfig{}
                    .with_label(tightRow ? "»" : "Next")
                    .with_size(ComponentSize{pixels(ask_action_w(app)),
                                             pixels(28)})
                    .with_margin(Margin{.left = pixels(kAskActionGap)})
                    .with_transparent_bg()
                    .with_border(theme::border(), pixels(1.0f))
                    .with_custom_hover_bg(
                        theme::hover_over(theme::panel_bg_2()))
                    .with_custom_text_color(ask_enabled_action_ink())
                    .with_font_size(theme::type::SM)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_corner_radius(6.0f)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_debug_name("ask_next"));
            if (next && inputLive) {
                const std::size_t at =
                    (open_ask_index(app) + 1) % allAsks->size();
                auto& leaving = app.askState.cursor_for(ask.id());
                leaving.question.clear();
                leaving.option.clear();
                app.askState.shownId = (*allAsks)[at].id();
                clicked = true;
            }
        }

        auto decline = button(ctx, mk(actions.ent(), 2),
            ComponentConfig{}
                .with_label((busy || !answerable || !inputLive || expired ||
                             tooShort)
                                ? std::string()
                                : declineLabel)
                .with_disabled(busy || !answerable || !inputLive ||
                               expired || tooShort)
                .with_on_draw_fg([off = (busy || !answerable || !inputLive ||
                                         expired || tooShort),
                                  declineLabel](RectangleType r) {
                    if (off)
                        draw_ask_action_label(r, declineLabel,
                                              ask_disabled_ink());
                })
                .with_size(ComponentSize{pixels(ask_action_w(app)),
                                         pixels(28)})
                .with_margin(Margin{.left = pixels(kAskActionGap)})
                .with_custom_background(
                    (busy || !answerable || !inputLive || expired || tooShort)
                        ? theme::ask_action_disabled_fill()
                        : theme::Color{0, 0, 0, 0})
                .with_border((busy || !answerable || !inputLive || expired ||
                              tooShort)
                                 ? ask_disabled_border()
                                 : theme::border(),
                             pixels(1.0f))
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_custom_text_color(
                    (busy || !answerable || !inputLive || expired)
                        ? ask_disabled_ink()
                        : ask_enabled_action_ink())
                .with_font_size(theme::type::SM)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_corner_radius(6.0f)
                .with_click_activation(ClickActivationMode::Press)
                .with_debug_name("ask_decline"));
        ask_set_tab_stop(decline.ent(),
                         !(busy || !answerable || !inputLive || expired));
        if (decline && !busy && answerable && inputLive && !expired &&
            !tooShort) {
            clicked = true;
            submit_ask(app, ask, api::AskAction::Decline);
        }

        app.askFocused = clicked || ctx.focus_in_subtree(card.ent().id);
        const bool actionFocused = ctx.focus_in_subtree(actions.ent().id);
        const bool widgetOwnsEnter = actionFocused;
        auto& cur = app.askState.cursor_for(ask.id());
        if (keysLive) {
            if (actionFocused) {
                cur.question.clear();
                cur.option.clear();
            }
            afterhours::EntityID focusedRow = 0;
            for (const AskRowId& row : askRowIds_)
                if (ctx.has_focus(row.id)) {
                    focusedRow = row.id;
                    cur.question = *row.question;
                    cur.option = *row.option;
                }
            if (focusedRow != askFocusedRow_) {
                if (focusedRow != 0)
                    scroll_ask_cursor_into_view(
                        body.ent(),
                        "ask_option_" + cur.question + "_" +
                            std::to_string(
                                hanabi::ask::option_index(ask, cur)));
                askFocusedRow_ = focusedRow;
            }
        }
        if (keysLive && app.escape == EscapeIntent::DeclineAsk && !busy &&
            answerable) {
            if (ask.kind == api::AskKind::Approval ||
                hanabi::ask::has_draft(ask, answer)) {
                app.askFocused = false;
                ctx.set_focus(ctx.ROOT);
            } else {
                submit_ask(app, ask, api::AskAction::Cancel);
                app.askFocused = false;
                ctx.set_focus(ctx.ROOT);
            }
        }
        askEnterRow_ = 0;
        drive_ask_keyboard(ctx, app, ask, &body.ent(), widgetOwnsEnter,
                           keysLive);
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
                            AppComponent& app, float gutter) {
        if (app.composerAttachments.empty()) return;

        auto strip = div(ctx, mk(parent, 4),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(attachments_h(app))})
                // The gutter is carried per-row now that the composer bar has
                // none of its own (it gave it up so the hairline could span
                // the pane), so this block asks for the same inset the meter
                // row and the input row do.
                .with_padding(Padding{.right = pixels(gutter),
                                      .left = pixels(gutter)})
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
                .with_label(std::string(product_branding::kAppName) +
                            " can't send images yet \xe2\x80\x94 these stay in the composer")
                .with_size(ComponentSize{percent(1.0f), pixels(kAttachNoteH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::ask_caveat_ink())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_attach_note"));

        if (removeAt >= 0)
            app.composerAttachments.erase(app.composerAttachments.begin() +
                                          removeAt);
    }
    static const char* goal_phase_label(api::GoalPhase phase) {
        switch (phase) {
            case api::GoalPhase::Active: return "ACTIVE";
            case api::GoalPhase::Paused: return "PAUSED";
            case api::GoalPhase::Blocked: return "BLOCKED";
            case api::GoalPhase::Completed: return "COMPLETED";
            case api::GoalPhase::Cleared: return "CLEARED";
            case api::GoalPhase::Unknown: return "UPDATED";
        }
        return "UPDATED";
    }

    static const char* plan_status_label(api::SessionPlanStep::Status status) {
        switch (status) {
            case api::SessionPlanStep::Status::Completed: return "complete";
            case api::SessionPlanStep::Status::InProgress: return "in progress";
            case api::SessionPlanStep::Status::Pending: return "pending";
            case api::SessionPlanStep::Status::Cancelled: return "cancelled";
            case api::SessionPlanStep::Status::Unknown: return "unknown";
        }
        return "unknown";
    }

    static theme::Color plan_status_color(api::SessionPlanStep::Status status) {
        switch (status) {
            case api::SessionPlanStep::Status::Completed:
                return theme::status_review();
            case api::SessionPlanStep::Status::InProgress:
                return theme::accent();
            case api::SessionPlanStep::Status::Pending:
                return theme::text_secondary();
            case api::SessionPlanStep::Status::Cancelled:
            case api::SessionPlanStep::Status::Unknown:
                return theme::text_faint();
        }
        return theme::text_faint();
    }

    static void draw_plan_status(RectangleType r,
                                 api::SessionPlanStep::Status status) {
        const float cx = r.x + 13.0f;
        const float cy = r.y + r.height * 0.5f;
        const theme::Color color = plan_status_color(status);
        switch (status) {
            case api::SessionPlanStep::Status::Completed:
                afterhours::draw_line_ex(afterhours::vec2{cx - 4.0f, cy},
                                         afterhours::vec2{cx - 1.0f, cy + 3.0f},
                                         1.6f, color);
                afterhours::draw_line_ex(afterhours::vec2{cx - 1.0f, cy + 3.0f},
                                         afterhours::vec2{cx + 5.0f, cy - 4.0f},
                                         1.6f, color);
                break;
            case api::SessionPlanStep::Status::InProgress:
                afterhours::draw_triangle(
                    afterhours::vec2{cx - 3.0f, cy - 5.0f},
                    afterhours::vec2{cx - 3.0f, cy + 5.0f},
                    afterhours::vec2{cx + 5.0f, cy}, color);
                break;
            case api::SessionPlanStep::Status::Pending:
                afterhours::draw_ring_segment(cx, cy, 3.0f, 4.0f, 0.0f,
                                              360.0f, 20, color);
                break;
            case api::SessionPlanStep::Status::Cancelled:
                afterhours::draw_line_ex(afterhours::vec2{cx - 4.0f, cy - 4.0f},
                                         afterhours::vec2{cx + 4.0f, cy + 4.0f},
                                         1.4f, color);
                afterhours::draw_line_ex(afterhours::vec2{cx + 4.0f, cy - 4.0f},
                                         afterhours::vec2{cx - 4.0f, cy + 4.0f},
                                         1.4f, color);
                break;
            case api::SessionPlanStep::Status::Unknown:
                afterhours::draw_ring_segment(cx, cy, 0.0f, 2.0f, 0.0f,
                                              360.0f, 16, color);
                break;
        }
    }

    void render_plan_popover(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app, Entity& anchorEnt,
                             const api::Session& session) {
        const bool hasPlan = session.plan && !session.plan->steps.empty();
        const bool hasGoal = session.goal &&
                             session.goal->phase != api::GoalPhase::Cleared;
        if (!hasPlan && !hasGoal) app.planPopoverOpen = false;
        if (!app.planPopoverOpen && !planPopoverWasOpen_) return;

        auto popRoot = mk(parent, 3400);
        RectangleType anchor =
            anchorEnt.get<afterhours::ui::UIComponent>().rect();
        anchor.y -= 24.0f;
        if (!app.planPopoverOpen) {
            afterhours::ui::imm::popover(
                ctx, popRoot, anchor, app.planPopoverOpen,
                afterhours::ui::overlay::Placement::Above);
            planPopoverWasOpen_ = false;
            return;
        }
        planPopoverWasOpen_ = true;

        constexpr float kPopW = 372.0f;
        constexpr float kRowH = 30.0f;
        float popH = 42.0f;
        if (hasGoal) {
            popH += 46.0f;
            if (!session.goal->done_when.empty()) popH += 20.0f;
            if (!session.goal->note.empty()) popH += 20.0f;
        }
        if (hasPlan)
            popH += 30.0f + kRowH * static_cast<float>(session.plan->steps.size());
        popH += 8.0f;

        const auto previousSurface = ctx.theme.surface;
        ctx.theme.surface = theme::panel_bg_2();
        auto pop = afterhours::ui::imm::popover(
            ctx, popRoot, anchor, app.planPopoverOpen,
            afterhours::ui::overlay::Placement::Above,
            hanabi::surface::menu(kPopW, popH, 7)
                .with_padding(Padding{.top = pixels(8),
                                      .right = pixels(10),
                                      .bottom = pixels(8),
                                      .left = pixels(10)})
                .with_debug_name("plan_popover"));
        ctx.theme.surface = previousSurface;
        if (!pop) return;

        const std::string title = hasGoal && hasPlan
                                      ? "Goal and plan"
                                      : (hasPlan ? "Plan" : "Goal");
        div(ctx, mk(pop.ent(), 1),
            ComponentConfig{}
                .with_label(title)
                .with_size(ComponentSize{percent(1.0f), pixels(26)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_debug_name("plan_popover_title"));

        int key = 10;
        if (hasGoal) {
            const api::SessionGoal& goal = *session.goal;
            const std::string heading =
                std::string("GOAL  ·  ") + goal_phase_label(goal.phase);
            div(ctx, mk(pop.ent(), key++),
                ComponentConfig{}
                    .with_label(heading)
                    .with_size(ComponentSize{percent(1.0f), pixels(18)})
                    .with_transparent_bg()
                    .with_custom_text_color(
                        goal.phase == api::GoalPhase::Blocked
                            ? theme::status_blocked()
                            : (goal.phase == api::GoalPhase::Completed
                                   ? theme::status_review()
                                   : theme::accent()))
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_debug_name("goal_phase"));
            div(ctx, mk(pop.ent(), key++),
                ComponentConfig{}
                    .with_label(goal.objective)
                    .with_size(ComponentSize{percent(1.0f), pixels(24)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_debug_name("goal_objective"));
            if (!goal.done_when.empty()) {
                div(ctx, mk(pop.ent(), key++),
                    ComponentConfig{}
                        .with_label("Done when: " + goal.done_when)
                        .with_size(ComponentSize{percent(1.0f), pixels(20)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_debug_name("goal_done_when"));
            }
            if (!goal.note.empty()) {
                div(ctx, mk(pop.ent(), key++),
                    ComponentConfig{}
                        .with_label(goal.note)
                        .with_size(ComponentSize{percent(1.0f), pixels(20)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_debug_name("goal_note"));
            }
        }

        if (hasPlan) {
            const api::SessionPlan& plan = *session.plan;
            std::string heading = plan.title.empty() ? "PLAN" : plan.title;
            heading += "  ·  " + std::to_string(plan.completed()) + " of " +
                       std::to_string(plan.steps.size());
            div(ctx, mk(pop.ent(), key++),
                ComponentConfig{}
                    .with_label(heading)
                    .with_size(ComponentSize{percent(1.0f), pixels(26)})
                    .with_margin(Margin{.top = pixels(hasGoal ? 4.0f : 0.0f)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_debug_name("plan_heading"));
            for (size_t i = 0; i < plan.steps.size(); ++i) {
                const auto& step = plan.steps[i];
                const std::string stepText =
                    step.text.empty() ? "(unnamed step)" : step.text;
                const std::string rowText =
                    stepText + "  · " + plan_status_label(step.status);
                auto row = div(ctx, mk(pop.ent(), key++),
                    ComponentConfig{}
                        .with_label(rowText)
                        .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                        .with_transparent_bg()
                        .with_custom_text_color(
                            step.status == api::SessionPlanStep::Status::Cancelled
                                ? theme::text_secondary()
                                : theme::text_primary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_on_draw_fg([status = step.status](RectangleType r) {
                            draw_plan_status(r, status);
                        })
                        .with_debug_name("plan_step_" + std::to_string(i)));
                row.ent().get<afterhours::ui::HasLabel>().set_text_inset(
                    Vector2Type{28.0f, 0.0f});
                row.ent().get<afterhours::ui::HasLabel>().text_x_offset = 23.0f;
            }
        }
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
            if (row && app.pane().openSession) {
                Settings::get().set_tool_fold(app.pane().openSession->summary.id,
                                              hanabi::fold::to_int(c.mode));
                // The sub-agent rollup shares this set but is not a tool row,
                // so its own disclosure is left exactly as the reader left it.
                const bool subs = app.expandedPiles.count("__subagents__") != 0;
                app.expandedPiles.clear();
                app.collapsedPiles.clear();
                if (subs) app.expandedPiles.insert("__subagents__");
                model::transcript_item_index().invalidate_all();
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
        const std::string draftKey =
            kickoff ? std::string("__kickoff__")
                    : (app.pane().openSession ? app.pane().openSession->summary.id
                                       : std::string());
        // Draft and sent-history are ONE entry in the bounded per-thread store
        // (ecs/pane_state.h), keyed the same way they always were, so the two
        // can never disagree about which thread they belong to -- and so the
        // pair is bounded instead of growing forever, one entry per thread the
        // composer ever rendered. Eviction refuses any entry holding an unsent
        // draft, so the bound costs typing nothing.
        const std::string composerStateKey =
            model::pane_key(app.focusedPane, draftKey);
        model::PaneState& composerState =
            model::pane_states().touch(composerStateKey);
        std::string& replyDraft = composerState.replyDraft;
        const std::string persistedDraftKey =
            model::persisted_reply_key(app.focusedPane, draftKey);
        if (!kickoff && !draftKey.empty() && !composerState.replyDraftLoaded) {
            if (replyDraft.empty())
                replyDraft = api::disk_cache::load_draft(persistedDraftKey);
            composerState.persistedReplyDraft = replyDraft;
            composerState.replyDraftLoaded = true;
        }
        if (!kickoff && !draftKey.empty()) {
            if (const std::string* rescued = app.askRescued.find(draftKey)) {
                replyDraft = replyDraft.empty()
                                 ? *rescued
                                 : replyDraft + "\n" + *rescued;
                api::disk_cache::save_draft(persistedDraftKey, replyDraft);
                composerState.persistedReplyDraft = replyDraft;
                app.askRescued.clear(draftKey);
            }
        }
        model::PaneState& history = composerState;
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
            if (const char* a = std::getenv("HANABI_ATTACH_DEMO"); a && *a)
                app.composerAttachments.push_back(
                    {std::string(a), "ledger-mismatch.png"});
        }

        // Screenshot affordance: HANABI_SEND_DEMO=<text> fires an actual reply
        // ONCE (sets the one-shot requestSendPrompt) so a headless capture over
        // the render frames shows the appended User + synthetic Assistant turn.
        // Loader runs before this system each frame, so the exchange lands a
        // frame or two later — well within the capture's 45-frame budget.
        // Ignored when unset; no network (the mock generates the reply).
        static bool sendDemoFired = false;
        if (!sendDemoFired && app.pane().openSession) {
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
        if (!streamDemoFired && app.pane().openSession && app.client &&
            app.client->supports_stream()) {
            if (const char* d = std::getenv("HANABI_STREAM_DEMO"); d && *d) {
                streamDemoFired = true;
                app.requestStreamPrompt = d;
            }
        }

        const bool canSend = app.client && app.client->supports_send();
        const bool canStream = app.client && app.client->supports_stream();
        const std::string& openId = draftKey;  // same value: the open thread id
        // A brake the SERVER holds. Frozen refuses input outright -- a message
        // typed into a frozen thread is never answered -- and halted only
        // warns, because input still queues against a resume.
        const ecs::model::Brake brake = ecs::model::brake_for(
            openId, app.find_summary(openId),
            app.pane().openSession ? &*app.pane().openSession : nullptr);

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
        if (!app.composerSubmit.empty() && brake.refuses_input) {
            // The keystroke path reaches the backend without consulting the
            // Send button's own enablement, so the brake has to be applied
            // here as well -- and the draft is LEFT WHERE IT IS. Clearing the
            // field for a send that was never made loses what the reader
            // typed, which is worse than the send they cannot make.
            app.composerSubmit.clear();
            app.composerSubmitWithCmd = false;
        } else if (!app.composerSubmit.empty()) {
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
                    : (canSend && hasText && !brake.refuses_input);

        // Center the composer's CONTENT under the reading column. Two numbers,
        // not one, because Puffin's composer is a column inside a column: the
        // pane centres a 768pt band (`AgentcloudTranscriptView.columnWidth`,
        // which the transcript uses too) and the composer then insets itself
        // 12pt inside that band (`.padding(.horizontal, 12)` on its VStack).
        // The earlier single 720 was a guess off the picture and put every
        // horizontal edge in this strip ~12px in from the reference: measured
        // on ref/01_home.png the input box is x=357..1072 and the pill row ends
        // at x=1099, which is 768/2 either side of the pane centre less the 12.
        // Written as the two constants it came from so the next person reading
        // it can check them against Puffin's source rather than against a
        // downsample.
        float composerGutter =
            (paneW - kComposerReadCol) * 0.5f + kComposerColInset;
        if (composerGutter < kContentInset) composerGutter = kContentInset;

        auto barCfg = ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(composerH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                // NO padding on the bar itself, and the gutter carried by each
                // content row instead. The bar used to own it, which made the
                // hairline below a child of the padded box and therefore only
                // as wide as the reading column — a 720px rule floating in a
                // 899px pane. Puffin's `Divider()` is a SIBLING of the padded
                // composer, so its rule runs the whole pane width and the
                // strip reads as a footer rather than as a card. Moving the
                // padding down one level is the only way to get both from one
                // flex column.
                //
                // No top padding either: Puffin's composer opens with that
                // rule flush to the top of the strip, and everything below is
                // measured from it (measured on the reference: rule y=851,
                // pill row y=861..878, input box y=884..930).
                .with_padding(Padding{.top = pixels(0),
                                      .right = pixels(0),
                                      .bottom = pixels(0),
                                      .left = pixels(0)})
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
        app.lastComposerPaneW = paneW;
        auto bar = div(ctx, mk(parent, 3), barCfg);

        // A hairline top border sold via a 1px divider row so the composer
        // reads as a distinct footer strip separated from the message column.
        // Flush to the top of the strip (the reference's rule is at y=851, the
        // strip's first pixel); the 9px breathing space belongs to the meter
        // row below. percent(1.0f) of an UNPADDED bar, so it spans the whole
        // pane the way Puffin's does — see the padding note above.
        div(ctx, mk(bar.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
                .with_custom_background(theme::border())
                .with_roundness(0.0f)
                .with_debug_name("composer_divider"));

        render_ask_card(ctx, bar, app, composerGutter);

        // Whether Send is really STEER: the agent is running and the backend
        // can interrupt it. Read here because both the meter row's caption
        // (below) and the send button (further down) turn on it.
        const bool steerMode = !kickoff && app.should_steer_open();

        // Meta row ABOVE the input: the model label and the capacity meter at
        // the left, the control pills at the right — Puffin's arrangement
        // (measured on the reference: the label starts at x=359, the meter
        // track is 48x5 at x=441, and the pill row ends at x=1099, all of them
        // centred on y=869).
        //
        // The gutter lives here rather than on the bar so the hairline above
        // can span the pane; the extra 2px is Puffin's own
        // `StripMetrics.horizontalPadding`, which insets the strip inside the
        // composer's 12 and is what puts the last pill's edge at 1099 instead
        // of at the content edge.
        const bool compactComposer = paneW < 560.0f;
        if (compactComposer) {
            app.foldPopoverOpen = false;
        }
        auto meta = div(ctx, mk(bar.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_margin(Margin{.top = pixels(9)})
                .with_padding(Padding{.right = pixels(composerGutter + 2.0f),
                                      .left = pixels(composerGutter + 2.0f)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_meta"));
        // LEFT cluster: the context meter and the status caption. Puffin puts
        // a track + a "0%" figure here; hanabi's figure is a token count over
        // the compaction budget, which three tests assert by text.
        auto leftMeta = div(ctx, mk(meta.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(18)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_leftmeta"));

        // RIGHT cluster: the live controls. Puffin's three pills are `Tools`,
        // `Thinking`, `Deliveries`; hanabi's are the model picker, the effort
        // picker and the tool-fold mode. Same place, same pill shape, and they
        // still open their popovers — the words differ because the controls
        // do (see the report).
        auto rightMeta = div(ctx, mk(meta.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(18)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_rightmeta"));

        // afterhours draws a widget's label at a HARD-CODED 5px inset from the
        // widget's own rect — `Vector2Type margin_px{5.f, 5.f}` inside
        // rendering.h's draw_text_in_rect — and `with_padding` never reaches
        // it: padding sizes and positions the BOX, the words stay at +5. A
        // children()-sized widget with a label and no child elements has
        // nothing to sum, either, so it cannot measure itself from its text.
        //
        // So every text run in this row is measured and sized by hand, and
        // every gap between runs is stated NET of the inset the library will
        // not let us set. kLabelInset is that constant, named so the arithmetic
        // below reads as arithmetic rather than as magic numbers.
        // See afterhours_gaps.md #91.
        constexpr float kLabelInset = 5.0f;
        const auto run_box = [](const std::string& s, float trailing) {
            return theme::text_px(s, theme::type::SM) + kLabelInset + trailing;
        };

        // The model, as PLAIN TEXT rather than a pill.
        //
        // This looked like losing an affordance and is not. Puffin's
        // `modelLabel` is a `Button` with `.buttonStyle(.plain)` around a bare
        // `Text` — clickable, opens the harness popover, and carries a comment
        // saying the chevron was REMOVED on purpose: "nothing else in this
        // strip carries one, so the one chevron read as the only control in a
        // row of labels rather than as an affordance". The pill treatment in
        // this strip belongs to the toggles on the right; a bordered capsule
        // here said "toggle" about a thing that opens a list. It still opens
        // the same popover on the same click and still answers to
        // `composer_model`, so every scripted test reaches it unchanged.
        //
        // No trailing room in the box: the effort run butts straight onto it,
        // and its own 5px inset is the word space between them.
        {
            const std::string currentModel = Settings::get().get_default_model();
            const std::string modelText = hanabi::models::display_name(currentModel);
            auto modelChip = button(ctx, mk(leftMeta.ent(), 1),
                ComponentConfig{}
                    .with_label(modelText)
                    .with_size(ComponentSize{pixels(run_box(modelText, 0.0f)),
                                             pixels(18)})
                    .with_transparent_bg()
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_alignment(TextAlignment::Left)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_debug_name("composer_model"));
            if (modelChip) app.modelPopoverOpen = !app.modelPopoverOpen;
            if (app.escape == EscapeIntent::CloseModelPicker)
                app.modelPopoverOpen = false;
            render_model_popover(ctx, parent, app, modelChip.ent(), currentModel);

            // The effort, parenthesised directly onto the model the way Puffin
            // renders it — its `ModelChipLabel` is one `HStack(spacing: 0)` of
            // `Text(name)` then `Text(" (\(effort))").opacity(0.7)`, which is why
            // the reference reads "Opus 5 (high)" as a single run of words.
            //
            // Two widgets rather than one, because hanabi's model and effort are
            // two SEPARATE pickers where Puffin's are one popover. Butted together
            // and drawn a shade fainter, they are the same run of text on screen;
            // each half still opens its own list, and `composer_effort` stays the
            // name the effort test clicks.
            const std::string currentEffort = Settings::get().get_default_effort();
            const std::string effortText =
                "(" + hanabi::effort::display_name(currentEffort) + ")";
            auto effortChip = button(ctx, mk(leftMeta.ent(), 2),
                ComponentConfig{}
                    .with_label(effortText)
                    .with_size(ComponentSize{
                        pixels(run_box(effortText, kLabelInset)), pixels(18)})
                    .with_transparent_bg()
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_alignment(TextAlignment::Left)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_debug_name("composer_effort"));
            if (effortChip) app.effortPopoverOpen = !app.effortPopoverOpen;
            if (app.escape == EscapeIntent::CloseEffortPicker)
                app.effortPopoverOpen = false;
            render_effort_popover(ctx, parent, app, effortChip.ent(),
                                  currentEffort);
        }

        const api::Session* stripSession =
            app.pane().openSession ? &*app.pane().openSession : nullptr;
        const bool hasPlan = stripSession && stripSession->plan &&
                             !stripSession->plan->steps.empty();
        const bool hasGoal = stripSession && stripSession->goal &&
                             stripSession->goal->phase != api::GoalPhase::Cleared;
        if (hasPlan || hasGoal) {
            constexpr float kPillPad = 6.0f;
            constexpr float kPillIcon = 10.0f;
            constexpr float kPillIconGap = 3.0f;
            constexpr float kPillTextX = kPillPad + kPillIcon + kPillIconGap;
            std::string planText;
            if (hasPlan) {
                planText = stripSession->plan->chip_label();
            } else {
                planText = "Goal";
            }
            auto planChip = button(ctx, mk(rightMeta.ent(), 2),
                ComponentConfig{}
                    .with_label(planText)
                    .with_size(ComponentSize{
                        pixels(kPillTextX +
                               theme::text_px(planText, theme::type::SM) +
                               kPillPad),
                        pixels(18)})
                    .with_margin(Margin{.left = pixels(4)})
                    .with_transparent_bg()
                    .with_border(theme::border(), pixels(1.0f))
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_alignment(TextAlignment::Left)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(9.0f)
                    .with_on_draw_fg([](RectangleType rr) {
                        hanabi::icons::draw_at(
                            "layers", rr.x + kPillPad + kPillIcon * 0.5f,
                            rr.y + rr.height * 0.5f, kPillIcon,
                            theme::text_secondary());
                    })
                    .with_debug_name("composer_plan"));
            if (planChip.ent().has<afterhours::ui::HasLabel>())
                planChip.ent().get<afterhours::ui::HasLabel>().text_x_offset =
                    kPillTextX - kLabelInset;
            if (planChip) app.planPopoverOpen = !app.planPopoverOpen;
            if (app.escape == EscapeIntent::ClosePlanPicker)
                app.planPopoverOpen = false;
            render_plan_popover(ctx, parent, app, planChip.ent(), *stripSession);
        } else {
            app.planPopoverOpen = false;
        }

        // The tool-fold chip, at the right: how much of a tool call this
        // thread shows by default. Only where there is a thread to set it on —
        // on the welcome screen there is no session to key the mode to.
        //
        // ICON IN THE PILL. Puffin's three pills each lead with a 9pt SF
        // Symbol (`wrench.and.screwdriver`, `brain`, `tray.and.arrow.down`) 3pt
        // before the label, inside 6pt of horizontal padding — see
        // `ToggleChip` and `StripMetrics`. hanabi has one pill where Puffin has
        // three, but the pill TREATMENT is the thing being matched, and a
        // bordered capsule of bare words was the visible difference.
        //
        // The icon is blitted rather than laid out, because a `button` holds
        // one string and no child elements. Reserving its space needs BOTH
        // halves of the workaround: the box is measured by hand, and the label
        // is pushed off the pill's left edge through `HasLabel::text_x_offset`
        // — a public field on the vendored component that `ComponentConfig`
        // exposes no setter for, so it is written after the widget exists. The
        // renderer adds its own 5px on top of the offset, which is why the
        // offset is the inset MINUS that. afterhours_gaps.md #91.
        if (app.pane().openSession && !compactComposer) {
            constexpr float kPillPad = 6.0f;
            constexpr float kPillIcon = 10.0f;
            constexpr float kPillIconGap = 3.0f;
            constexpr float kPillTextX = kPillPad + kPillIcon + kPillIconGap;
            const hanabi::fold::Mode currentFold = fold_mode();
            const std::string foldText =
                "Tools: " + hanabi::fold::chip_label(currentFold);
            auto foldChip = button(ctx, mk(rightMeta.ent(), 3),
                ComponentConfig{}
                    .with_label(foldText)
                    .with_size(ComponentSize{
                        pixels(kPillTextX +
                               theme::text_px(foldText, theme::type::SM) +
                               kPillPad),
                        pixels(18)})
                    .with_margin(Margin{.left = pixels(4)})
                    .with_transparent_bg()
                    .with_border(theme::border(), pixels(1.0f))
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_alignment(TextAlignment::Left)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(9.0f)
                    .with_on_draw_fg([](RectangleType rr) {
                        hanabi::icons::draw_at(
                            "layers", rr.x + kPillPad + kPillIcon * 0.5f,
                            rr.y + rr.height * 0.5f, kPillIcon,
                            theme::text_secondary());
                    })
                    .with_debug_name("composer_fold"));
            if (foldChip.ent().has<afterhours::ui::HasLabel>())
                foldChip.ent().get<afterhours::ui::HasLabel>().text_x_offset =
                    kPillTextX - kLabelInset;
            if (foldChip) app.foldPopoverOpen = !app.foldPopoverOpen;
            if (app.escape == EscapeIntent::CloseFoldPicker)
                app.foldPopoverOpen = false;
            render_fold_popover(ctx, parent, app, foldChip.ent(), currentFold);
        }
        std::string caption;
        // A brake outranks the slash notice: the notice is a transient answer
        // to something the reader just typed, and the brake is the reason the
        // thing they typed will not be answered at all.
        if (brake.engaged)
            caption = brake.caption;
        else if (!app.slashNotice.empty())
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
        // Conversation size against the budget that will compact it. The
        // numerator is the provider's own count when the backend reports one
        // and a chars/4 estimate otherwise, and only the estimate wears a "~".
        // The denominator is the session's compaction budget, or the declared
        // one for a backend that reports none; with neither, the bar is absent
        // rather than filled to something invented.
        if (!compactComposer && canSend && app.pane().openSession) {
            const api::ContextUsage& usage = app.pane().openSession->context;
            const bool counted = usage.counted();
            const int64_t tok =
                counted ? usage.used_tokens : estimated_tokens(*app.pane().openSession);
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
                // Puffin's order, left to right: the track first, then the
                // figure. Measured on the reference: the track is 48x5 at
                // x=441..489 — 10px after the model label — and the figure
                // starts 4px later at x=493. Its `contextMeter` is a 48x5
                // `Capsule` in an `HStack(spacing: 4)`, and the 10 is the
                // strip's own `StripMetrics.spacing`.
                if (budget > 0) {
                    const float frac =
                        std::min(1.0f, static_cast<float>(tok) /
                                           static_cast<float>(budget));
                    div(ctx, mk(leftMeta.ent(), 13),
                        ComponentConfig{}
                            .with_size(ComponentSize{pixels(48), pixels(5)})
                            // 10 from the text that precedes it — Puffin's
                            // `StripMetrics.spacing` — less the 5 the effort
                            // run's box already carries past its last glyph.
                            .with_margin(Margin{.left = pixels(5)})
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
                div(ctx, mk(leftMeta.ent(), 12),
                    ComponentConfig{}
                        .with_label(label)
                        // Measured, not children()-sized, for the reason in
                        // the kLabelInset note: a label is not a child and a
                        // children()-sized box cannot see it.
                        .with_size(ComponentSize{
                            pixels(run_box(label, kLabelInset)), pixels(16)})
                        // Puffin's figure sits 4 after the meter; 4 is inside
                        // the 5 the label's own inset already spends, so the
                        // margin is nothing. With no meter to follow, it takes
                        // the strip's 10 less that same inset.
                        .with_margin(Margin{.left = pixels(budget > 0 ? 0 : 5)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_debug_name("composer_size"));
            }
        }

        // The status caption rides at the end of the left cluster: what is
        // happening, or which key sends. Puffin has no equivalent — its strip
        // is meter and pills only — but the key hint is the fix for "HOW DO I
        // SEND A MESSAGE" and a scripted test asserts it.
        if (!caption.empty()) {
            div(ctx, mk(leftMeta.ent(), 11),
                ComponentConfig{}
                    .with_label(caption)
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.left = pixels(10)})
                    .with_transparent_bg()
                    // A brake reads at the secondary weight, not the faint
                    // one every other caption uses: the faint token is for
                    // hints the reader may ignore, and this is the reason
                    // their message will not be answered.
                    .with_custom_text_color(brake.engaged
                                                ? theme::text_secondary()
                                                : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_debug_name("composer_status"));
        }
        // A live selection says how much is on the clipboard's doorstep. It
        // also confirms the selection exists at all: the band is drawn behind
        // text and easy to miss on a short run.
        if (const std::string sel = hanabi::text_select::selected_text();
            !sel.empty()) {
            div(ctx, mk(leftMeta.ent(), 14),
                ComponentConfig{}
                    .with_label(std::to_string(sel.size()) + " selected")
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.left = pixels(10)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_debug_name("composer_selected"));
        }

        render_attachments(ctx, bar.ent(), app, composerGutter);

        // Puffin's input is an OUTLINED box on the window colour, with a 19px
        // circular send button 9px to its right. Measured on the reference: the
        // box is x=357..1072 (716 wide) and y=884..930, and the send disc is
        // x=1082..1100 centred on the box's mid-line. 716 is exactly what the
        // gutter above leaves once the disc and its gap are taken off, so the
        // width is arithmetic rather than a second constant to keep in step.
        //
        // NOTE (afterhours_gaps.md #17, and #64 below it): text_input STILL
        // owns two things this box wants. Its font size is derived from the
        // field height unless set explicitly (a 45px field would render 22.5px
        // text), and its inner padding is derived from the height with no
        // override at all — at 45px that is a 15.75px left inset the caller
        // cannot change. So the field is given an explicit font size, and the
        // padding is what the widget decides.
        constexpr float kSendDia = 19.0f;
        constexpr float kSendGap = 9.0f;
        const float sendW = kSendDia;
        const float sendH = kSendDia;
        // 46, not 45: a 1px border draws ON the box edge, so a 45px box paints
        // 46 rows and the reference's paints 47 (y=884 through y=930).
        //
        // Not a constant any more, because the box GROWS with the draft. At one
        // row it is 46 and every reference measurement above still holds; each
        // further row adds one line height, to a ceiling of six. The row count
        // is last frame's, from the field's own layout cache, for the reason
        // composerRows_ carries.
        const float kInputH = composer_box_h(composerRows_);

        auto row = div(ctx, mk(bar.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kInputH)})
                .with_margin(Margin{.top = pixels(6)})
                .with_padding(Padding{.right = pixels(composerGutter),
                                      .left = pixels(composerGutter)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_row"));

        float inputW = paneW - (composerGutter * 2.0f) - sendW - kSendGap;
        if (inputW < 120.0f) inputW = 120.0f;

        auto inputWrap = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(inputW), pixels(kInputH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_justify_content(JustifyContent::Center)
                .with_margin(Margin{.right = pixels(kSendGap)})
                // An OUTLINE on the strip colour, not a filled pill: Puffin's
                // input interior is the window colour and only the 1px border
                // says where the field is.
                //
                // border_soft, not border. Puffin draws this one outline with
                // `mutedText.opacity(0.25)` and its full-strength `hairline`
                // only on the rule above, and the two are far enough apart to
                // measure: on the reference the rule is (57,57,70) and the
                // field's edge is (45,45,59). hanabi's `border` matches the
                // first and is 17 levels too bright for the second, which put
                // ~710px of wrong-coloured edge on four rows — the largest
                // single term left in this strip once the geometry lined up.
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border_soft(), pixels(1.0f))
                .with_corner_radius(7.0f)
                .with_debug_name("composer_input_wrap"));

        // text_input forces its own Secondary bg over its rect (gap #17); point
        // Secondary/Surface at the strip colour so the field disappears into
        // the outlined box instead of painting a filled panel inside it.
        ctx.theme.secondary = theme::panel_bg();
        ctx.theme.surface = theme::panel_bg();
        ctx.theme.font = theme::text_primary();
        // Re-asserted, not inherited: ctx.theme is one global struct read at
        // render time, so whatever the sidebar left in font_muted is what this
        // pane's muted text gets (gap #90).
        ctx.theme.font_muted = theme::text_faint();
        // What the empty field says it is for, painted by the pane over the
        // top of the empty field.
        //
        // text_input renders a placeholder itself and this used to be its
        // config; text_area does not render one at all -- "placeholder" does
        // not appear anywhere in text_area.h, while component.h has had it
        // since gap #29 was closed. So moving the composer to multiline took
        // the hint away with it, and the overlay below is 982376a's code
        // brought back for the widget that still needs it
        // (afterhours_gaps.md #261).
        //
        // Puffin's reads "Message Agentcloud… (↵)". The key hint is dropped:
        // Roboto has no U+21B5, and a missing codepoint draws nothing at all
        // (gap #48) — a placeholder cannot carry a drawn glyph, because the
        // widget owns the string and its layout. The key that sends is named
        // in words in the meter row's caption instead.
        const bool phSteer = steerMode;
        // A refusing brake overrides both: "Steer the running agent" invites
        // the reader to do the one thing a frozen thread cannot be made to do,
        // and a frozen thread can be running, so that is the string it would
        // otherwise draw.
        const char* placeholder =
            brake.refuses_input  ? "This thread is frozen"
            : kickoff            ? "Start a new conversation\xe2\x80\xa6"
            : phSteer            ? "Steer the running agent\xe2\x80\xa6"
                                 : "Message hanabi\xe2\x80\xa6";
        // The FIELD inside the box is 29px, not the box's 45. Not a style
        // choice: text_input derives its inner padding from the field height
        // (pad_w = h*0.35) and overwrites whatever with_padding the caller
        // passed, so the only way to ask for Puffin's 10px text inset is to
        // pick the height that yields it — 10/0.35 = 28.6. The box still reads
        // as 45px tall because the wrap owns the border and centres the field
        // inside it. See afterhours_gaps.md #65.
        //
        // text_area derives neither -- its padding is a fixed 6/4 and its font
        // size is whatever the caller states -- so 29 survives the move as the
        // ONE-ROW height rather than as the rule that produced it, and the
        // glyphs land on the same pixel either way (measured: byte-identical).
        // Past one row it is the field's grown height, the same number
        // with_auto_grow computes from the same cache.
        const float kFieldH = composer_field_h(composerRows_);
        // The PLACEHOLDER's ink, scoped by save/restore. text_input hardcodes
        // `field_label.explicit_text_color = ctx.theme.font_muted` (vendor
        // text_input/component.h:194) and offers no with_placeholder_color, so
        // the hint wears whatever the pane last left in font_muted --
        // `text_faint` (100,100,112). Puffin's is `mutedText` (140,140,166):
        // measured over the two frames' hint rows, the reference's ink peaks at
        // (141,141,165) and hanabi's at (94,94,106), the largest colour gap
        // left in the composer band. text_secondary (142,142,154) is the
        // nearest token and it is two units off Puffin's.
        //
        // Save/restore works here even though gap #90 says a per-widget colour
        // is a frame-wide edit, and the distinction is worth keeping: #90 is
        // about `Theme::Usage::*` values RESOLVED at render time, and this one
        // line COPIES a concrete colour into the entity during the imm build.
        // So the window is the build call, and it is exactly one call wide.
        // (afterhours_gaps.md #105.)
        //
        // Worth 68 of the hint row's 962 differing pixels -- small, and the
        // reason it is small is that the two strings differ ("Message
        // Agentcloud... (^)" against "Message hanabi..."), so brighter ink
        // lands on the reference's glyphs for the shared prefix and on bare
        // background after it. It is kept because it is the colour Puffin's
        // source names, not because of the 68px.
        //
        // NOTE this is the same swap that made the SIDEBAR FOOTER worse
        // (REFERENCE.md, "The sidebar footer's three buttons"): there Puffin's
        // 9pt SF Symbols never reach their own colour and hanabi's sprite blits
        // do, so matching the token overshoots. Here both apps' 13px body text
        // reaches full coverage -- the reference's own peak is (141,141,165)
        // against a (140,140,166) token -- so matching the token is right. The
        // rule is not "never match Puffin's token"; it is "measure what lands".
        const auto savedMuted = ctx.theme.font_muted;
        ctx.theme.font_muted = theme::text_secondary();
        auto inputRes = afterhours::ui::imm::text_area(
            ctx, mk(inputWrap.ent(), 1), replyDraft,
            ComponentConfig{}
                .with_line_height(pixels(kComposerLineH))
                .with_max_lines(kComposerMaxRows)
                .with_submit_on_enter(hanabi::enter_sends(
                    Settings::get().get_send_key(), hanabi::keys::cmd_down()))
                .with_size(ComponentSize{percent(1.0f), pixels(kFieldH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_corner_radius(7.0f)
                .with_debug_name("composer_reply_input"));
        ctx.theme.font_muted = savedMuted;

        // The two things text_area does to this field that text_input did
        // not, undone: it paints an opaque Theme::Usage::Secondary fill over
        // the caller's with_transparent_bg (gap #262), and it draws no focused
        // edge at all (gap #263). src/ui/field_chrome.h carries the whole
        // argument, including why neither gap's own proposed workaround is the
        // one used and what the fill actually costs here -- which is not the
        // interior colour the gap describes but 42 pixels of the WRAP's
        // outline, painted over by a field that is percent(1.0f) of it.
        //
        // ctx.theme.accent, because that is the one text_input reads. NOT
        // theme::accent(), which is a different blue, and NOT ctx.theme.focus,
        // which belongs to the :focus-visible ring and is written every frame
        // by focus_visible_system.h -- taking that one would put this field's
        // edge under the ring policy's colour correction and quietly bypass
        // the policy at the same time. Both pinned by `make chrome-gate`.
        // Last frame's row count for next frame's box (composerRows_).
        if (inputRes.ent().has<afterhours::text_input::HasTextAreaState>()) {
            const size_t rows = inputRes.ent()
                                    .get<afterhours::text_input::HasTextAreaState>()
                                    .layout_cache.line_count();
            composerRows_ = rows < 1 ? 1
                            : rows > kComposerMaxRows ? kComposerMaxRows
                                                      : rows;
        }

        const auto composerFieldId = focusable_field(inputRes.ent());
        const bool composerFocused =
            inputRes.ent().has<afterhours::text_input::HasTextAreaState>() &&
            inputRes.ent()
                .get<afterhours::text_input::HasTextAreaState>()
                .is_focused;
        hanabi::ui::field_chrome::clear_forced_fill(composerFieldId);
        if (composerFocused) ctx.theme.focus_ring_thickness = 0.0f;
        hanabi::ui::field_chrome::apply_focus_edge(
            inputWrap.ent().id, composerFocused, ctx.theme.accent);

        // Faint hint text ON TOP of the empty field, via an absolutely
        // positioned on_draw_fg child -- the same pattern the sidebar search
        // uses, and the one this composer used before the widget grew a
        // placeholder of its own. Replaced by real glyphs the moment you type.
        //
        // kComposerHintH, not kFieldH, and the difference is 495 pixels of the
        // parity frame. The hint is centred in this box, so the box's height
        // is where its baseline comes from -- and kFieldH now carries the
        // widget's h720(4) padding, which resolves to 5.27 at the parity
        // window's 949 and 4.22 at the scripted suite's 760. Following it
        // would move the hint 1.3px down at one window size and not at
        // another, against a reference measured at neither. 29 is the one-row
        // field height in 720p reference units, which is the number this hint
        // has always been centred in and the only one that does not depend on
        // how tall the window is. The hint is only ever drawn on an EMPTY
        // draft, so one row is the only case.
        constexpr float kComposerHintH = 29.0f;
        if (replyDraft.empty()) {
            div(ctx, mk(inputWrap.ent(), 2),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(
                        ComponentSize{percent(1.0f), pixels(kComposerHintH)})
                    .with_absolute_position()
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(3)
                    .with_on_draw_fg([placeholder](RectangleType rect) {
                        const float px = theme::type::BODY;
                        const float ty = rect.y + rect.height * 0.5f - px * 0.5f;
                        afterhours::draw_text(placeholder, rect.x + 10.0f, ty,
                                              px, theme::text_secondary());
                    })
                    .with_debug_name("composer_placeholder"));
        }

        // A SHADOW HasTextInputState, for the scripted-UI harness only.
        //
        // text_area's state component is HasTextAreaState. It DERIVES from
        // HasTextInputState, but the ECS keys components by exact type id
        // (core/entity.h: componentSet[get_type_id<T>()]), so a query for the
        // base never matches the derived one. The harness knows this and says
        // so in its own source -- expect_selected_text and
        // expect_input_selection both test for either component. The third
        // one, expect_input_text, was left asserting HasTextInputState alone,
        // and it is the only assertion in the harness that reads a field's
        // TEXT. So moving this field to multiline made every composer script
        // fail with "Text input not found: composer_reply_input", and that was
        // the ONLY thing that broke -- not the send key, not the slash menu,
        // not the history walk, not the layout (afterhours_gaps.md #258).
        //
        // vendor/afterhours is read-only here, so the field carries a copy of
        // its text in the component the harness looks for. Nothing reads it
        // but the harness: hanabi's own lookups ask for the area state first
        // (keyboard_focus.h), and is_focused is deliberately left alone on
        // this one so it cannot answer "is a field focused" with a stale yes.
        if (inputRes.ent().has<afterhours::text_input::HasTextAreaState>()) {
            const auto& live =
                inputRes.ent()
                    .get<afterhours::text_input::HasTextAreaState>();
            auto& shadow =
                inputRes.ent()
                    .addComponentIfMissing<
                        afterhours::text_input::HasTextInputState>();
            if (shadow.text() != live.text()) {
                shadow.storage.clear();
                shadow.storage.insert(0, live.text());
            }
        }

        // Screenshot affordance: HANABI_TEST_FOCUS_COMPOSER=1 force-focuses the
        // composer field so a capture can photograph the caret WITH text in it
        // (verifying caret position). Test-only; ignored when unset.
        //
        // focusable_field, not the wrapper's own id, and that is a SECOND bug
        // on top of the one 8b0bc3d fixed. That commit armed focus-visible so
        // the app's :focus-visible RING would draw; this is the field's own
        // focused BORDER, which is a different indicator and was still
        // missing. focus_id survives a frame only if the widget put itself in
        // `focused_ids` via try_to_grab (systems.h:562), and the only entity
        // in this pair that does is the FIELD child carrying InFocusCluster --
        // so EndUIContextManager dropped the wrapper's id back to ROOT at the
        // end of every frame and text_input's `state.is_focused` was never
        // true. It is the same call session_search_system and
        // rename_modal_system already make.
        if (hanabi::test_hooks::focus_composer())
            ctx.set_focus(focusable_field(inputRes.ent()));

        // Opt-in field diagnostics: dump the live text_input state so we can
        // see EXACTLY what the field receives (chars, cursor, h-scroll) —
        // pins down space/backspace/wrap issues instead of guessing across the
        // vendored widget. Gated on HANABI_DBG_INPUT; a no-op when unset.
        if (std::getenv("HANABI_DBG_INPUT") &&
            inputRes.ent().has<afterhours::text_input::HasTextAreaState>()) {
            const auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextAreaState>();
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
        if (inputRes.ent().has<afterhours::text_input::HasTextAreaState>()) {
            auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextAreaState>();
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
                    .has<afterhours::text_input::HasTextAreaState>())
                inputRes.ent()
                    .get<afterhours::text_input::HasTextAreaState>()
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
                     .has<afterhours::text_input::HasTextAreaState>())
                return;
            auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextAreaState>();
            st.storage.clear();
            st.storage.insert(0, text);
            st.cursor_position = text.size();
            st.clear_selection();
            // NOT rebuild_line_index(). HasTextAreaState keeps a LineIndex of
            // where every '\n' is, the widget refreshes it at each of its own
            // edits, and an outside write like this one leaves it describing
            // the previous string -- which looks exactly like a bug worth
            // fixing until you check what reads it. In text_area, nothing
            // does: Home and End are move_to_visual_line_*, the vertical moves
            // are visual too, and both read layout_cache, which is rebuilt
            // from the text every frame. The line_index consumers in
            // text_input/utils.h (:334, :355, :403, :410) have no call site in
            // text_area.h at all. See afterhours_gaps.md #307 -- a stale index
            // here is unobservable, and a "fix" for it is a line of code that
            // cannot be tested.
        };

        // The send decided at the top of this frame: empty the field now that
        // there is something that can write to it. The widget keeps its own
        // storage, so clearing replyDraft up there is not enough.
        if (clearFieldAfterSubmit) set_field("");
        if (app.forkRestoreSessionId == openId && !app.forkError.empty()) {
            set_field(app.forkRestoreDraft);
            app.slashNotice = app.forkError;
            app.forkRestoreDraft.clear();
            app.forkRestoreSessionId.clear();
            app.forkError.clear();
        }

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
            if (cmd->name == "model") {
                app.modelPopoverOpen = true;
                app.effortPopoverOpen = false;
                app.slashNotice.clear();
                set_field("");
                return;
            }
            if (cmd->name == "effort") {
                app.effortPopoverOpen = true;
                app.modelPopoverOpen = false;
                app.slashNotice.clear();
                set_field("");
                return;
            }
            if (cmd->name == "btw") {
                if (p.args.empty()) {
                    app.slashNotice = "Type a question after /btw.";
                    set_field(typed);
                    return;
                }
                if (openId.empty()) {
                    app.slashNotice =
                        "Open a writable session before using /btw.";
                    set_field(typed);
                    return;
                }
                if (!app.client || !app.client->supports_fork()) {
                    app.slashNotice =
                        "This backend does not support BTW forks.";
                    set_field(typed);
                    return;
                }
                if (app.forkPending || !app.requestForkSourceId.empty()) {
                    app.slashNotice = "A fork is already being created.";
                    set_field(typed);
                    return;
                }
                app.requestForkSourceId = openId;
                app.requestForkPrompt = p.args;
                app.requestForkTitle = hanabi::slash::btw_title(p.args);
                app.requestForkPane = app.focusedPane;
                app.forkRestoreDraft = typed;
                app.forkRestoreSessionId = openId;
                app.slashNotice = "Creating BTW fork\xe2\x80\xa6";
                set_field("");
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
            inputRes.ent().has<afterhours::text_input::HasTextAreaState>() &&
            inputRes.ent()
                .get<afterhours::text_input::HasTextAreaState>()
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
        if (inputRes.ent().has<afterhours::text_input::HasTextAreaState>()) {
            auto& st =
                inputRes.ent().get<afterhours::text_input::HasTextAreaState>();
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
                    if (e.has<afterhours::text_input::HasTextAreaState>())
                        text = e.get<afterhours::text_input::HasTextAreaState>()
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


        // Send affordance. Enabled (clickable) when the backend supports
        // replies and the draft has text; otherwise disabled-styled.
        //
        // Puffin's is a 19px CIRCLE with an up-arrow and no word on it, so
        // that is the shape here: a square button with a 9.5px corner radius
        // and the arrow DRAWN (Roboto has no U+2191 and a missing codepoint
        // paints nothing — gap #48; see hanabi::glyph::arrow_up).
        //
        // STEER IS A MARK, NOT A WORD (Gabe: "the steer button should be an
        // icon"). It used to be the one state that widened this button into a
        // 78x32 pill reading "Steer", on the argument that a drawn arrow
        // cannot tell you the verb changed. The argument was right about the
        // verb and wrong about where the verb has to live: the composer's own
        // status caption already reads "Enter to steer" the moment there is
        // text to send, six lines above the button, in words -- and the button
        // is only pressable when there IS text, so the caption is present
        // exactly when the mark is live. The name is in the frame; it does not
        // also have to be in the control.
        //
        // Which matters because of gap #112: afterhours has no tooltip and no
        // accessible name, so an icon-only button is unlabelled in every
        // sense, and the honest way to add one is a neighbouring string rather
        // than a pill. tests/ui/steer_is_an_icon_with_a_name.e2e pins both
        // halves so a later change cannot quietly drop the caption and leave a
        // nameless glyph.
        //
        // In-flight keeps the pill. Its label is an ellipsis, which is not a
        // verb and not a name -- it is "wait" -- and a 78px box of nothing-
        // to-press reads as disabled in a way a small circle does not.
        const char* sendLabel = "";
        // A frozen thread is often a RUNNING one, so the button would draw the
        // steer mark: an invitation to steer an agent that cannot be reached.
        // The plain arrow, disabled, is the honest face.
        const bool steerIcon = steerMode && !brake.refuses_input;
        const theme::Color sendFill =
            sendEnabled ? theme::button_primary() : theme::disabled_bg();
        auto send = button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(sendLabel)
                .with_size(ComponentSize{pixels(sendW), pixels(sendH)})
                .with_custom_background(sendFill)
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
                .with_corner_radius(kSendDia * 0.5f)
                .with_on_draw_fg([steerIcon, sending,
                                  sendEnabled](RectangleType rr) {
                    const theme::Color ink = sendEnabled
                                                 ? theme::window_bg()
                                                 : theme::disabled_text();
                    if (sending && !sendEnabled) {
                        afterhours::draw_ring(
                            rr.x + rr.width * 0.5f, rr.y + rr.height * 0.5f,
                            2.5f, 4.5f, 18, ink);
                    } else if (steerIcon) {
                        hanabi::glyph::steer(rr, ink);
                    } else {
                        hanabi::glyph::arrow_up(rr, ink);
                    }
                })
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
            constexpr float kRowH = hanabi::surface::kMenuRowH;
            const float menuH =
                kRowH * static_cast<float>(slashRows.size()) + 8.0f;
            auto menuConfig = hanabi::surface::menu(inputW, menuH, 7);
            menuConfig.with_absolute_position()
                .with_translate(absX + composerGutter, absY - menuH - 6.0f)
                .with_debug_name("slash_menu");
            auto menu = div(ctx, mk(parent, 3100), menuConfig);
            for (size_t i = 0; i < slashRows.size(); ++i) {
                const hanabi::slash::Command& c = *slashRows[i];
                std::string command = "/" + std::string(c.name);
                if (!c.arg.empty()) command += " " + std::string(c.arg);
                const bool selected = static_cast<int>(i) ==
                                      app.slashMenuIndex;
                auto row = button(
                    ctx, mk(menu.ent(), static_cast<int>(i)),
                    hanabi::surface::option_row(inputW - 8.0f, kRowH,
                                                selected, 8)
                        .with_label(" ")
                        .with_flex_direction(FlexDirection::Row)
                        .with_flex_wrap(FlexWrap::NoWrap)
                        .with_align_items(AlignItems::Center)
                        .with_debug_name("slash_item_" + std::to_string(i)));
                div(ctx, mk(row.ent(), 1),
                    ComponentConfig{}
                        .with_label(command)
                        .with_size(ComponentSize{pixels(106), pixels(20)})
                        .with_margin(Margin{.left = pixels(10)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_primary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_debug_name("slash_command_" +
                                         std::to_string(i)));
                const float statusW = c.runnable ? 0.0f : 78.0f;
                div(ctx, mk(row.ent(), 2),
                    ComponentConfig{}
                        .with_label(std::string(c.blurb))
                        .with_size(ComponentSize{pixels(
                            std::max(40.0f, inputW - 132.0f - statusW)),
                                                 pixels(20)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_debug_name("slash_blurb_" +
                                         std::to_string(i)));
                if (!c.runnable)
                    div(ctx, mk(row.ent(), 3),
                        ComponentConfig{}
                            .with_label("Unavailable")
                            .with_size(ComponentSize{pixels(statusW), pixels(20)})
                            .with_transparent_bg()
                            .with_custom_text_color(theme::text_faint())
                            .with_font_size(theme::type::MICRO)
                            .with_alignment(TextAlignment::Right)
                            .with_debug_name("slash_unavailable_" +
                                             std::to_string(i)));
                if (row) choose_slash(static_cast<int>(i));
            }
        }
        if (inputRes.ent().has<afterhours::text_input::HasTextAreaState>())
            replyDraft = inputRes.ent()
                             .get<afterhours::text_input::HasTextAreaState>()
                             .text();
        if (!kickoff && !draftKey.empty() &&
            replyDraft != composerState.persistedReplyDraft) {
            api::disk_cache::save_draft(persistedDraftKey, replyDraft);
            composerState.persistedReplyDraft = replyDraft;
        }
        lastSlashDraft = replyDraft;

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
    // Bubble fills, measured off the reference. They live here rather than in
    // the palette because the palette's transcript tokens are a different
    // design (a neutral grey user bubble, a green-tinted assistant one) and
    // that file is being reworked elsewhere; on light they fall back to it.
    struct chat_colors {
        static bool dark() { return theme::window_bg().r < 128; }
        static theme::Color user_bubble() {
            return dark() ? theme::Color{62, 56, 111, 255}
                          : theme::bubble_user_bg();
        }
        static theme::Color asst_bubble() {
            return dark() ? theme::Color{33, 33, 54, 255}
                          : theme::bubble_assistant_bg();
        }
    };

    // The user's avatar: a filled circle with their initial. Drawn, not a
    // rounded box — see the note at the call site.
    static void draw_user_avatar(RectangleType rc) {
        const float cx = rc.x + rc.width * 0.5f;
        const float cy = rc.y + rc.height * 0.5f;
        afterhours::draw_circle(static_cast<int>(cx), static_cast<int>(cy),
                                rc.width * 0.5f, chat_colors::user_bubble());
        static const std::string initial = [] {
            const char* u = std::getenv("USER");
            char c = (u != nullptr && *u != '\0')
                         ? static_cast<char>(std::toupper(*u))
                         : 'Y';
            return std::string(1, c);
        }();
        const float fp = 11.0f;
        afterhours::draw_text(initial.c_str(),
                              cx - theme::text_px(initial, fp) * 0.5f,
                              cy - fp * 0.6f, fp, theme::text_primary());
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
        hanabi::prof::Scope _p("text.wrap_text");
        hanabi::prof::tick("text.wrap_bytes", text.size());
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
        //
        // This used to be `wrapped_lines(...).size()` -- build every line as a
        // std::string, put them in a vector, take the size, drop the lot. The
        // counter in src/util/wrap_count.h answers the same question over byte
        // offsets, through the SAME measure function afterhours' wrapper uses,
        // so the two cannot disagree about where a line breaks (and
        // tests/unit/test_wrap_count.cpp checks that differentially against
        // the vendored wrapper itself). afterhours_gaps.md #135.
        //
        // In front of it, a memo -- because the transcript's RENDER pass asks
        // this question about every visible paragraph on every frame, and the
        // answer cannot change unless the text or the width does. The measure
        // pass has had a memo since the transcript work; the render pass never
        // did, and it is where 61.7 of the 61.8 wraps per idle frame came
        // from. Keyed on the whole argument tuple of a pure function, so it
        // cannot go stale: a changed body is a different key, not an invalid
        // entry, and there is nothing to invalidate on a resize or a theme
        // change.
        //
        // kLineCountEntries is the bound, and it is an LRU eviction rather
        // than a clear (see src/util/text_cache.h). Measured with the
        // transcript being scrolled: 128 entries held at 120 messages, 488 at
        // 480, and at 1,200 messages the cap engages and holds -- 512 entries,
        // 99.2% hit, 1.4 recomputes a frame. So 512 is the number where a
        // thread longer than any real one starts paying, and what it pays is
        // one recompute per evicted paragraph rather than a cold screen.
        constexpr std::size_t kLineCountEntries = 512;
        static hanabi::text::TextKeyCache<int> memo(kLineCountEntries);
        if (const int* hit = memo.find(text, widthPx, fontPx)) {
            hanabi::prof::tick("cache.lines_hit");
            return *hit;
        }
        hanabi::prof::tick("cache.lines_miss");
        hanabi::prof::Scope _p("text.count_lines");
        hanabi::prof::tick("text.count_bytes", text.size());
        const int lines = hanabi::text::wrapped_line_count(
            text, text_wrap_width(widthPx), [fontPx](const std::string& s) {
                return afterhours::ui::measure_text_line(
                           s, afterhours::ui::UIComponent::DEFAULT_FONT, fontPx)
                    .x;
            });
        const int out = lines < 1 ? 1 : lines;
        memo.put(text, widthPx, fontPx, out);
        hanabi::prof::gauge("cache.lines_entries", memo.size());
        return out;
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
    // The air around a turn. Puffin's transcript spends 24pt between two
    // messages and says so twice: `bubbleBreathing = 9` padded above and below
    // every row, plus the `LazyVStack`'s `itemSpacing = 6` between them
    // (`AgentcloudTranscriptView`), and its own comment — "the air belongs to
    // the conversation, not to the stack: two messages sit 24pt apart while
    // the machinery between them stays tight". hanabi has no stack spacing, so
    // the 24 is carried by the two margins alone: 10 below a turn and 14 above
    // the next one (kTurnGapTop + the 8 an assistant turn adds; a user turn
    // adds 10 and so breathes 2px more, which is hanabi's own asymmetry and
    // predates this).
    //
    // It used to be 4 + 14, with a 24px hover row reserved under every message
    // making up the difference and then some — 43px of real air against
    // Puffin's 24. The row is an overlay now (see message_actions), so the air
    // has to be stated rather than left over.
    static constexpr float kTurnGapTop = 6.0f;
    static constexpr float kTurnGapBot = 10.0f;
    static constexpr float kAuthorH = 15.0f;
    static constexpr float kAuthorGap = 3.0f;

    // What the assistant turn's meta row has to say, if anything. The relative
    // time that used to lead this row is gone (Puffin stamps no turn), so the
    // row now carries only STATE — the run subtitle and, while a turn is
    // arriving, "streaming…". Empty means the row is not drawn at all.
    static std::string turn_meta_text(const api::Message& m, bool isLive) {
        std::string s = m.subtitle;
        const std::string live = isLive ? "streaming\xe2\x80\xa6" : std::string();
        if (live.empty()) return s;
        return s.empty() ? live : (s + "  \xc2\xb7  " + live);
    }

    // THE ONE predicate for "does this turn have an author row". bubble_height,
    // the body's start-Y and the draw all ask it, so the measure and the draw
    // cannot disagree about an 18px row (see src/ui/measure_probe.h).
    static bool has_author_row(const api::Message& m, bool isLive,
                               bool showAuthor) {
        return showAuthor && !turn_meta_text(m, isLive).empty();
    }
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
    // An UNLABELLED fence's header carries only the copy affordance, which is
    // invisible at rest, so it does not need a label's height. Puffin sizes
    // its header to its content the same way and tightens the body's padding
    // with it (`CodeBlockView`: `.padding(top: showsHeader ? 4 : 8)`), which
    // is why the reference's bare fence stands 41px for two lines where
    // hanabi's stood 63.
    static constexpr float kCodeBarBareH = 0.0f;
    // The per-line chip's horizontal padding. Measured off the reference's
    // short line: "exit 65" is 7 mono glyphs and its chip runs x374..435.
    static constexpr float kCodeChipPadX = 6.0f;
    // A code line's own row height, and it is NOT kLinePitch.
    //
    // The reference's two chips are 21px each and stack with no gap between
    // them (`ref/02_thread.png`: x384..1018 y204..224, then x374..435
    // y225..245). Prose in the same bubble is on hanabi's 16px kLinePitch, and
    // sharing one constant put the fence's ink five pixels a line tighter than
    // the frame it is being compared with. The two rhythms are separate in
    // Puffin too -- prose is `scaledFont(size: 13)` laid out by SwiftUI and
    // code is an `NSFont.monospacedSystemFont` inside an AttributedString --
    // so they have no reason to share a number here either.
    static constexpr float kCodeLinePitch = 21.0f;
    // Air ABOVE the block and INSIDE its top, and none at all below.
    //
    // Measured, not derived: prose ink ends at y176 in the reference, the
    // first chip starts at y204 and the last ends at y245, and the prose under
    // the fence starts inking at y254. That is 28px of air above the chips and
    // 9 below, and the checkout's own arithmetic does not reproduce it --
    // `CodeBlockView` spends 8 above and 8+4 below inside a `VStack(spacing:
    // 6)`, which would put the prose below the fence at y263. The frozen frame
    // is v0.5.5 and the checkout is v0.5.2 (REFERENCE.md), so the PNG wins on
    // pixels: hanabi spends its whole allowance above the block, where the
    // reference spends it.
    static constexpr float kCodeVMarginTop = 15.0f;
    static constexpr float kCodeVMarginBot = 2.0f;
    static constexpr float kCodePadVTop = 6.0f;
    static constexpr float kCodePadVBot = 0.0f;
    // Total height of a code block with `nLines` inner lines.
    static float code_block_h(int nLines, bool labelled = true) {
        if (nLines < 1) nLines = 1;
        return kCodeVMarginTop + (labelled ? kCodeBarH : kCodeBarBareH) +
               (static_cast<float>(nLines) * kCodeLinePitch + kCodePadVTop +
                kCodePadVBot) +
               kCodeVMarginBot;
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
        hanabi::prof::Scope _p("text.rich_body_h");
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
                h += code_block_h(codeLines, !fence_lang(line).empty());
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
        const std::size_t staleWas =
            hanabi::prof::enabled() ? render_cache().stale() : 0;
        if (!isLive) {
            if (const auto* hit = render_cache().get(key, textW, m.text)) {
                hanabi::prof::tick("cache.msgrender_hit");
                return *hit;
            }
            hanabi::prof::tick("cache.msgrender_miss");
            hanabi::prof::tick(render_cache().stale() > staleWas
                                   ? "cache.miss_widthstale"
                                   : "cache.miss_absent");
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
        const ecs::model::MsgRender& out = render_cache().put(key, m.text, std::move(r));
        hanabi::prof::gauge("cache.msgrender_entries",
                            render_cache().total_size());
        hanabi::prof::gauge("cache.msgrender_threads", render_cache().threads());
        return out;
    }

    static void invalidate_item_geometry(int messageIndex = -1) {
        const Pane* pane = painting_pane();
        if (pane == nullptr || !pane->openSession) return;
        const std::string& id = pane->openSession->summary.id;
        for (int paneIndex = 0; paneIndex < 2; ++paneIndex) {
            const std::string key = model::pane_key(paneIndex, id);
            if (messageIndex < 0)
                model::transcript_item_index().invalidate(key);
            else
                model::transcript_item_index().invalidate(
                    key, static_cast<std::size_t>(messageIndex));
        }
    }

    bool is_folded(const api::Message& m, int index, int lineCount,
                   bool isLive) {
        if (isLive || lineCount <= kFoldLines) return false;
        // Folding is a preference: a reader who would rather scroll than
        // click can turn it off, and then nothing is ever hidden behind a
        // button. Answered before the fold state so an off setting cannot
        // leave a stale "Show more" measured into the height.
        if (!fold_long_messages()) return false;
        AppComponent* app = app_singleton();
        // A folded message shows its first few lines, so a match below the
        // fold is counted and cannot be painted. Rather than exclude those
        // matches — they are real, and hiding them would make find useless on
        // exactly the long messages it is for — a live query unfolds any
        // message that contains one.
        // An operator that excludes this row excludes it here too: unfolding
        // a message find will not highlight would open it for nothing.
        Pane* pane = painting_pane();
        if (pane && pane->findOpen &&
            pane->findMemo.message_has_match(static_cast<std::size_t>(index)))
            return false;
        const std::string mkey =
            m.id.empty() ? ("msg" + std::to_string(index)) : m.id;
        return !(app && app->expandedMsgs.count(mkey) != 0);
    }

    // TEMPORARY (HANABI_PROBE_MEASURE=1): read back the height the LAYOUT
    // ENGINE resolved for each turn drawn last frame and hold it against what
    // bubble_height promised the virtualizer. afterhours offers no "how tall
    // did that element come out" call, so this walks the UI collection for the
    // turn elements by debug name.
    static void probe_drawn_turns() {
        if (!hanabi::mprobe::on()) return;
        auto q = afterhours::EntityQuery<>(
                     afterhours::ui::UICollectionHolder::get().collection,
                     {.ignore_temp_warning = true})
                     .whereHasComponent<afterhours::ui::UIComponent>()
                     .whereHasComponent<afterhours::ui::UIComponentDebug>()
                     .gen();
        for (afterhours::Entity& e : q) {
            const std::string& n =
                e.get<afterhours::ui::UIComponentDebug>().name();
            const bool user = n.rfind("user_turn#", 0) == 0;
            const bool asst = n.rfind("asst_turn#", 0) == 0;
            if (!user && !asst) continue;
            if (!e.get<afterhours::ui::UIComponent>().was_rendered_to_screen)
                continue;
            const std::string idx = n.substr(n.find('#') + 1);
            // The resolved rect is the turn's CONTENT box; its own margins are
            // outside it and are the same two constants the measure adds.
            const float margins =
                (user ? (kTurnGapTop + 10.0f) : (kTurnGapTop + 8.0f)) +
                kTurnGapBot;
            hanabi::mprobe::observe(
                "turn#" + idx,
                e.get<afterhours::ui::UIComponent>().rect().height + margins);
        }
    }

    // ---- Item height functions (mirror the render layout exactly) ----------
    float bubble_height(const api::Message& m, float paneWidth, bool isLive,
                        int index, bool showAuthor = true) {
        hanabi::prof::Scope _p("measure.bubble_h");
        if (m.role == api::Role::System) return 22.0f + 16.0f;
        const bool isUser = (m.role == api::Role::User);
        if (isUser) {
            const UserBox box = user_box(m, paneWidth, isLive, index,
                                         AppComponent::StreamPhase::Idle);
            const auto& mr = measured(m, box.textW, isLive, index,
                                      AppComponent::StreamPhase::Idle,
                                      /*rich=*/false);
            // +12px for the sync-glyph child under the body when sync!=None
            // (a real ✓/✓✓ corner mark, gap #28 now fixed); server-loaded
            // messages (sync==None) add nothing. Mirrors render_bubble.
            const float syncH = (m.sync != api::SyncState::None) ? 12.0f : 0.0f;
            // kBubblePadTop/Bot are the bubble's OWN padding, the same two
            // numbers render_bubble hands to with_padding. The old code used a
            // single kUserPadV=14 here while the draw padded 8+9=17, so every
            // user message measured 3px shorter than it drew.
            return kTurnGapTop + 10.0f + kBubblePadTop + mr.height + syncH +
                   kBubblePadBot + kTurnGapBot;
        }
        float textW = asst_text_w(paneWidth);
        const auto& mr = measured(m, textW, isLive, index,
                                  AppComponent::StreamPhase::Idle,
                                  /*rich=*/true);
        bool folded = is_folded(m, index, mr.line_count, isLive);
        float bodyH = folded
                          ? rich_body_h(first_n_lines(mr.body, textW, kFoldLines),
                                        textW)
                          : mr.height;
        // + the assistant bubble's own vertical padding, which the draw
        // applies as with_padding on the bubble the body now lives in.
        float h = kTurnGapTop + 8.0f +
                  (has_author_row(m, isLive, showAuthor)
                       ? (kAuthorH + kAuthorGap)
                       : 0.0f) +
                  kBubblePadTop + bodyH + kBubblePadBot +
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
    // message doesn't run edge-to-edge across the wide pane (v3 #8). Puffin
    // does not spell this cap as a number: its user row is
    // `HStack(spacing: 6) { Spacer(minLength: 60); avatar; bubble }` inside the
    // 736px content column, so the cap is whatever is left once the spacer,
    // the two 6pt gaps and the 20pt avatar have taken their share — 644. It
    // was 620 here, guessed as "~70 characters at this font"; the arithmetic
    // is the same shape and lands 24px wider.
    static constexpr float kBubbleCap = 644.0f;

    // ---- Bubble geometry --------------------------------------------------
    // A user message is a right-aligned indigo bubble that HUGS its text, with
    // a circular avatar to its left; an assistant message is a left-aligned
    // dark bubble of fixed width. Every number here is used by BOTH the
    // measure pass and the draw — see user_box() / asst_text_w(), which exist
    // so there is only one copy.
    //
    // These were read off the 1x reference PNG. They are now read off Puffin's
    // own source instead (`Sources/Views/AgentcloudTranscriptView.swift`,
    // `BubbleRowView` / `BubbleAvatar`), because a downsample cannot tell a
    // 20pt circle from a 22pt one and cannot tell a corner radius at all. Each
    // constant below names the Swift it came from; where the PNG and the
    // source disagree the source wins, EXCEPT for kBubblePadX — see below.
    //
    // afterhours insets a label's text horizontally inside its own rect (the
    // wrap width is the rect less ~10px, and the first glyph lands ~6px in), so
    // a 13px visual gap between the bubble edge and the first glyph is a 7px
    // padding plus that inset. kLabelInsetX is that fudge, named so the next
    // reader knows it is the library's, not a design choice.
    static constexpr float kLabelInsetX = 6.0f;
    // Puffin's `.padding(.horizontal, 12)` and this 13 are not in conflict:
    // 12 is text-origin to bubble edge, 13 is first INK to bubble edge, and
    // the leading side bearing of a lowercase glyph at 13px is the 1px between
    // them. Measured on the reference: bubble left 752, first lit pixel 765.
    static constexpr float kBubblePadX = 13.0f;   // bubble edge -> first glyph
    static constexpr float kBubbleCfgPadX = kBubblePadX - kLabelInsetX;
    static constexpr float kBubblePadTop = 8.0f;
    static constexpr float kBubblePadBot = 7.0f;
    // `RoundedRectangle(cornerRadius: 10)`. Confirmed on the reference: the
    // top row of the bubble is inset 6.9px from its left edge, which is the
    // chord of a radius-10 arc at half a pixel down and nothing else.
    static constexpr float kBubbleCorner = 10.0f;  // px, not a fraction
    // These three were taken from the Swift and then confirmed, to the pixel,
    // against a reference the source was not consulted for: in
    // ref/02_thread.png the avatar disc is x 791..811 (20 across), its left
    // edge is 6 clear of the bubble at x 817, and its top is 6 below the
    // bubble's at y 95. When the source and an independent measurement agree
    // exactly, the constant is settled and the next reader need not re-derive
    // it.
    static constexpr float kAvatarD = 20.0f;      // BubbleAvatar.diameter
    static constexpr float kAvatarGap = 6.0f;     // the user row's HStack spacing
    static constexpr float kAvatarTop = 6.0f;     // BubbleAvatar .padding(.top, 6)
    // The assistant bubble is NOT content-sized: in the reference a one-line
    // answer still fills a fixed 670 of the 736 column. That 66 is
    // `Spacer(minLength: 60)` plus the same 6pt HStack spacing, so the two
    // sides of the transcript are inset by the same rule.
    static constexpr float kAsstInsetR = 66.0f;

    // The wrap width inside an assistant bubble at this column width. ONE
    // function, called by the measure pass and the draw, so the two cannot
    // drift the way two copies of `paneWidth - 34.0f` did.
    static float asst_text_w(float paneWidth) {
        const float w = paneWidth - kAsstInsetR - 2.0f * kBubblePadX;
        return w < 40.0f ? 40.0f : w;
    }
    static float asst_bubble_w(float paneWidth) {
        const float w = paneWidth - kAsstInsetR;
        return w < 40.0f ? 40.0f : w;
    }

    // The user bubble's box: outer width and the wrap width inside it. Also
    // ONE function for both passes.
    struct UserBox {
        float bubbleW = 0.0f;
        float textW = 0.0f;
    };
    UserBox user_box(const api::Message& m, float paneWidth, bool isLive,
                     int index, AppComponent::StreamPhase phase) {
        float maxW = paneWidth - kAvatarD - kAvatarGap;
        if (maxW > kBubbleCap) maxW = kBubbleCap;
        if (maxW < 80.0f) maxW = 80.0f;
        const float maxTextW = maxW - 2.0f * kBubbleCfgPadX;
        // The hug is a pure function of (body, maxTextW), and the body is a
        // pure function of the message — so on a thread that is not streaming
        // it is the same answer every frame, for every message, on-screen or
        // not. Computing it cost a wrap of the whole body plus a text measure
        // per resulting line, and pass 1 asks for EVERY user message in the
        // thread. Live messages skip the memo: their text changes per frame,
        // which is the one case where recomputing is the correct answer.
        const std::string hugKey =
            (m.id.empty() ? ("i" + std::to_string(index)) : m.id) + "|hug";
        if (!isLive) {
            if (const float* w = render_cache().hug(hugKey, maxTextW, m.text)) {
                hanabi::prof::tick("cache.hug_hit");
                return box_from_text_w(*w);
            }
            hanabi::prof::tick("cache.hug_miss");
        }
        const auto& mr = measured(m, maxTextW, isLive, index, phase,
                                  /*rich=*/false);
        // Widest wrapped line, measured with the font that will draw it. The
        // +kLabelInsetX*2 puts back what the label takes off its own rect, so
        // the text cannot re-wrap inside the narrowed box.
        //
        // The lines are taken as BYTE RANGES rather than as strings
        // (src/util/wrap_count.h): this used to be
        // `for (const auto& ln : wrapped_lines(mr.body, maxTextW))`, which
        // built and destroyed one std::string per wrapped line, plus the
        // vector, to produce one float. The ranges are checked line for line
        // against the vendored wrapper in tests/unit/test_wrap_count.cpp, so
        // the widths measured here are the widths of the lines that will be
        // drawn -- including the trailing whitespace on the last line of a
        // paragraph, which has width and which a naive span would drop.
        static std::vector<std::pair<std::size_t, std::size_t>> spans;
        static std::string lineBuf;
        hanabi::text::wrapped_line_spans(
            mr.body, text_wrap_width(maxTextW),
            [](const std::string& s) {
                return afterhours::ui::measure_text_line(
                           s, afterhours::ui::UIComponent::DEFAULT_FONT,
                           theme::type::BODY)
                    .x;
            },
            spans);
        float widest = 0.0f;
        for (const auto& sp : spans) {
            lineBuf.assign(mr.body, sp.first, sp.second - sp.first);
            widest = std::max(widest,
                              theme::text_px(lineBuf, theme::type::BODY));
        }
        const float textW = std::min(maxTextW, widest + 2.0f * kLabelInsetX);
        if (!isLive) render_cache().put_hug(hugKey, m.text, maxTextW, textW);
        return box_from_text_w(textW);
    }

    // The box the hugged text width implies. One place, so the memoized path
    // and the computed path cannot produce different boxes from the same
    // number.
    static UserBox box_from_text_w(float textW) {
        UserBox box;
        box.textW = textW < 24.0f ? 24.0f : textW;
        box.bubbleW = box.textW + 2.0f * kBubbleCfgPadX;
        return box;
    }


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

    // A syntax token's colour. The one place the scanner's vocabulary meets
    // the palette.
    static theme::Color syntax_color(hanabi::syntax::Tok t) {
        switch (t) {
            case hanabi::syntax::Tok::Keyword: return theme::syntax_keyword();
            case hanabi::syntax::Tok::Type: return theme::syntax_type();
            case hanabi::syntax::Tok::String: return theme::syntax_string();
            case hanabi::syntax::Tok::Comment: return theme::syntax_comment();
            case hanabi::syntax::Tok::Number: return theme::syntax_number();
            case hanabi::syntax::Tok::Punct: return theme::syntax_punct();
            case hanabi::syntax::Tok::Plain: break;
        }
        return theme::text_secondary();
    }

    // Counts of what was coloured, for the HANABI_SYNTAX_AUDIT caption. Filled
    // by the span builder itself, so what the caption reports is what the draw
    // was handed rather than a second run of the scanner.
    struct SyntaxAudit {
        int kw = 0, ty = 0, str = 0, com = 0, num = 0;
        // No spaces: `assert_ui name text=<value>` splits its arguments on
        // whitespace and cannot be given a value that contains any
        // (afterhours_gaps.md #61), so a spaced caption is unassertable.
        std::string summary() const {
            return "kw" + std::to_string(kw) + "/ty" + std::to_string(ty) +
                   "/str" + std::to_string(str) + "/com" +
                   std::to_string(com) + "/num" + std::to_string(num);
        }
    };

    // Turn the scanner's coloured runs into the renderer's span list, filling
    // the gaps between them with plain code text.
    static std::vector<afterhours::ui::TextSpan> code_spans(
        const std::string& line, const std::vector<hanabi::syntax::Run>& runs,
        SyntaxAudit* audit) {
        std::vector<afterhours::ui::TextSpan> spans;
        size_t at = 0;
        const auto plain = [&](size_t from, size_t to) {
            if (to > from)
                spans.push_back(afterhours::ui::TextSpan{
                    line.substr(from, to - from), theme::text_primary()});
        };
        for (const hanabi::syntax::Run& r : runs) {
            if (r.off >= line.size() || r.len == 0) continue;
            plain(at, r.off);
            const size_t len = std::min(r.len, line.size() - r.off);
            spans.push_back(afterhours::ui::TextSpan{line.substr(r.off, len),
                                                     syntax_color(r.tok)});
            if (audit != nullptr) {
                switch (r.tok) {
                    case hanabi::syntax::Tok::Keyword: ++audit->kw; break;
                    case hanabi::syntax::Tok::Type: ++audit->ty; break;
                    case hanabi::syntax::Tok::String: ++audit->str; break;
                    case hanabi::syntax::Tok::Comment: ++audit->com; break;
                    case hanabi::syntax::Tok::Number: ++audit->num; break;
                    default: break;
                }
            }
            at = r.off + len;
        }
        plain(at, line.size());
        return spans;
    }

    void render_code_block(UIContext<InputAction>& ctx, Entity& parent, int id,
                           const std::string& lang,
                           const std::vector<std::string>& lines,
                           float blockW = 0.0f) {
        const int n = lines.empty() ? 1 : static_cast<int>(lines.size());
        auto block = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{
                    percent(1.0f),
                    pixels(code_block_h(n, !lang.empty()) - kCodeVMarginTop -
                           kCodeVMarginBot)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(kCodeVMarginTop),
                                    .bottom = pixels(kCodeVMarginBot)})
                // TRANSPARENT, and no border. The reference's fence is not a
                // panel: measured row by row, its dark surface is per LINE,
                // and the two lines are not the same width -- the long error
                // line's chip runs x384..1018 and the "exit 65" line's runs
                // x374..435, in the same block. 1018 is the bubble's own inner
                // edge, so the first chip is FULL WIDTH and only the last one
                // hugs its words. See kCodeChipFullWidth below for why that is
                // a rule and not an accident.
                //
                // A panel here was wrong twice over: it painted window_bg,
                // which punched a hole clean through the bubble behind it, and
                // it painted the full rectangle where the reference paints
                // only behind the words. The hole was invisible to the parity
                // metric -- window_bg (23,23,35) is within tolerance of the
                // bubble's (33,33,54) -- and plainly visible to a reader.
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("code_block"));
        // Lang bar: uppercase language label on a slightly-raised strip with a
        // hairline bottom divider.
        //
        // The label is drawn only when the fence DECLARED a language. A bare
        // ``` fence used to be captioned "CODE", which is the renderer telling
        // the reader what they can already see, in the one place a code block
        // has nothing to say: Puffin's `CodeBlockView.header` emits its label
        // `if !lang.isEmpty` and nothing otherwise, and `ref/02_thread.png`'s
        // fence — a bare one — carries no caption at all. The strip itself
        // stays, because the copy affordance lives in it and Puffin's header
        // holds its own height for the same reason.
        // The bar exists only for a LANGUAGE, and when there is none it is not
        // emitted AT ALL -- not even at zero height.
        //
        // A zero-height div still paints its border, and this one's was
        // `border_bottom(code_bg(), 1)`. That drew a 1px rule of the fence's
        // own dark colour clean across the bubble at y190, above a block whose
        // reference has nothing there: 656 pixels of surface the reference
        // does not draw. It survived a rebuild that set out to remove exactly
        // this strip, because that change removed the bar's CHILDREN and left
        // the bar. The structural metric could not see it either -- a single
        // row of (19,19,27) on (33,33,54) is 14/255, and the 0.8px blur takes
        // it under the 12/255 tolerance before the comparison happens. It is
        // visible to a reader and invisible to the score, which is the trap
        // REFERENCE.md names from the other side.
        Entity* barEnt = nullptr;
        if (!lang.empty()) {
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
        barEnt = &bar.ent();
        div(ctx, mk(bar.ent(), 1),
            ComponentConfig{}
                .with_label(lang)
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
            // The test-only audit caption sits after Copy, so the spacer has
            // to give up its width — a child that overflows a NoWrap row warns
            // every frame, and that spam is a real cost even in a test build.
            const float auditW =
                hanabi::test_hooks::syntax_audit() ? 110.0f : 0.0f;
            const float barContent = (blockW > 0.0f ? blockW : 698.0f) - 22.0f;
            float spacerW = barContent - 120.0f - copyW - auditW;
            if (spacerW < 0.0f) spacerW = 0.0f;
            div(ctx, mk(bar.ent(), 2),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(spacerW), pixels(14)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("code_bar_spacer"));
            // Same in-place confirmation as a message's Copy: a press that
            // changes nothing on screen reads as a press that did nothing.
            //
            // Quiet at rest. Puffin's is the same affordance under the same
            // rule — `CopyAffordance.opacity(hovering:focused:copied:)` is 0
            // until the pointer arrives — and a fence in `ref/02_thread.png`
            // shows nothing in that corner. The element is still emitted, so
            // the strip's width arithmetic below does not move when the
            // pointer does; only its label is.
            const std::string ckey = "code:" + std::to_string(id);
            const bool ccopied = recently_copied(ckey);
            const bool cshow = ccopied ||
                               ctx.mouse_was_in_subtree(block.ent().id);
            auto copy = button(ctx, mk(bar.ent(), 3),
                ComponentConfig{}
                    .with_label(cshow ? (ccopied ? "Copied" : "Copy") : " ")
                    .with_size(ComponentSize{pixels(copyW), pixels(15)})
                    .with_custom_background(cshow ? theme::panel_bg_2()
                                                  : theme::panel_bg())
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
                hanabi::test_hooks::record_clipboard_text(joined);
                record_copied(ckey);
            }
        }
        }  // if (!lang.empty()) -- the whole bar
        // Code body: mono rows, no wrap (pre-formatted).
        //
        // The right padding is kLabelInsetX, not a design number: the block's
        // own box overhangs the bubble's visual inner edge by exactly that,
        // because kBubbleCfgPadX gives back the inset afterhours takes off a
        // label's rect. Spending it here puts a full-width chip's right edge
        // on x1018, which is where the reference's is.
        auto body = div(ctx, mk(block.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(kCodePadVTop),
                                      .right = pixels(kLabelInsetX),
                                      .bottom = pixels(kCodePadVBot),
                                      .left = pixels(12)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("code_block_body"));
        int li = 0;
        // Colouring runs down the block, not per line: a block comment and a
        // Python docstring both carry across lines, so the scanner's state is
        // threaded through the loop and reset here, at the opening fence.
        const hanabi::syntax::Lang slang = hanabi::syntax::lang_from_tag(lang);
        hanabi::syntax::State sstate;
        SyntaxAudit audit;
        for (const auto& cl : lines) {
            const std::string shown = cl.empty() ? " " : cl;
            // 12px mono in the plain text colour, both Puffin's:
            // `SyntaxHighlighter.highlight` sets `monospacedSystemFont(ofSize:
            // (12.0 * scale))` and paints unmatched code in `palette.plain`,
            // which is #E6EDF3 on the dark theme — hanabi's text_primary is
            // (224,224,230), the same near-white. This used to be 11px in
            // text_secondary (142,142,154), a grey meant for captions: beside
            // the reference the block read as a footnote rather than as the
            // evidence the answer is quoting.
            //
            // Only the COLOUR of that pair is visible: 11 and 12 render
            // identically here, and so do 13 and 14 — the requested size
            // lands in buckets somewhere below this call, measured on this
            // very line and written up in docs/visual-parity/FRICTION_LOG.md
            // (Transcript fixture). The 12 is kept because it is Puffin's
            // number and the next person to widen this type needs to know
            // that 1px is not a step.
            // The chip's WIDTH is not "hug the words" -- that is only true of
            // the LAST line. In the reference the two lines of one fence carry
            // chips of x384..1018 and x374..435: the first runs to the
            // bubble's own inner edge and the second stops after "65". The
            // rule behind it is TextKit's, and it is visible in the fixture:
            // the fence's text is "error: ...\nexit 65", so line one is
            // terminated by a newline and line two is not, and a background
            // attribute drawn over a line fragment is stretched to the
            // container's trailing edge by that newline glyph. Every line but
            // the last is therefore full width.
            //
            // hanabi has no line fragments, so it states the rule directly.
            // This is worth 12,053 diff pixels on `main` -- a third of the
            // whole region -- because a chip that hugs a 53-character mono
            // line is 276px against the reference's 635 and gets the
            // difference wrong twenty-one rows deep.
            const bool lastLine = (li + 1 == static_cast<int>(lines.size()));
            const float chipW =
                std::ceil(theme::text_px(shown.c_str(), theme::type::MD)) +
                2.0f * kCodeChipPadX;
            auto cfg =
                ComponentConfig{}
                    .with_label(shown)
                    .with_size(ComponentSize{
                        lastLine ? pixels(chipW) : percent(1.0f),
                        pixels(kCodeLinePitch)})
                    .with_custom_background(theme::code_bg())
                    .with_custom_text_color(theme::text_primary())
                    .with_font("mono", theme::type::MD)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("code_block_line");
            // The spans re-say the whole line, plain parts included: the
            // renderer draws the runs INSTEAD of the label, so a gap between
            // two coloured runs would be a hole in the code.
            const std::vector<hanabi::syntax::Run> runs =
                cl.empty() ? std::vector<hanabi::syntax::Run>{}
                           : hanabi::syntax::scan(slang, cl, sstate);
            if (!runs.empty())
                cfg = cfg.with_styled_label(code_spans(shown, runs, &audit));
            div(ctx, mk(body.ent(), 1 + li), cfg);
            ++li;
        }
        // Test-only (HANABI_SYNTAX_AUDIT=1): what was actually coloured, in the
        // lang bar, because a script can read a label and never a colour. An
        // unlabelled fence has no bar to put it in and no test asks for one.
        if (barEnt != nullptr && hanabi::test_hooks::syntax_audit())
            div(ctx, mk(*barEnt, 4),
                ComponentConfig{}
                    .with_label(audit.summary())
                    .with_size(ComponentSize{children(), pixels(14)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("code_block_audit_" +
                                     (lang.empty() ? "CODE" : lang)));
    }

    // "In progress / thinking" indicator: a pulsing accent dot + a muted label
    // + an elapsed timer, shown on a live assistant turn while it's THINKING
    // (before any tokens stream). Matches the reference Gabe shared (a soft
    // glowing dot beside "Laying the groundwork · 32s"). The status word is
    // "Thinking…" (a backend free-text step label like "Laying the groundwork"
    // needs a status SSE field we don't parse yet — logged as an API ask); the
    // dot pulse + elapsed timer are fully client-side.
    //
    // WHAT IT IS ANCHORED TO, which is the whole of its geometry: this row is
    // the PLACEHOLDER for the assistant's first line of prose. render_rich_body
    // replaces it the instant a token arrives, so anything about it that does
    // not match that line is a jump the eye catches. Two consequences, and
    // neither is a tuned constant:
    //
    //   * its height is body_text_h(1) -- one wrapped line -- not a round 24.
    //   * its content starts on the TEXT column, which is kLabelInsetX inside
    //     the bubble's own content box and not on it. kBubbleCfgPadX is
    //     deliberately 6 short of the 13px the design wants (kBubblePadX)
    //     because afterhours adds that 6 back when it draws a LABEL into an
    //     element's rect and adds nothing at all for a custom draw. So a text
    //     child of this bubble lands at 13 and a drawn child lands at 7, and
    //     the dot was drawing into the bubble's left padding: measured on the
    //     parity capture its ink ran x=372..383 where every line of assistant
    //     prose above it began at x=374. Padding here (not a margin -- a
    //     percent-sized child plus a margin is the overflow this branch just
    //     fixed one pane over) puts the row's children on the text column.
    static constexpr float kDotInk = 14.0f;    // the halo's own max diameter
    static constexpr float kDotLabelGap = 8.0f;  // dot ink -> first glyph
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
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(body_text_h(1))})
                .with_padding(Padding{.left = pixels(kLabelInsetX)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("thinking_row"));

        // Pulsing dot: a filled accent circle whose radius eases up/down with a
        // sine of wall-clock time, plus a fainter halo — a soft "breathing"
        // glow.
        //
        // The slot is the INK and not a round number, so where the dot starts
        // is arithmetic rather than something to measure off a screenshot: the
        // halo's radius peaks at 5 + 2 = 7, so its widest is kDotInk across,
        // and a circle centred in a kDotInk box fills that box exactly. The
        // dot's leftmost lit pixel is therefore the row's content origin,
        // which is the text column. An 18px slot put it 2px inside instead --
        // gap #114's problem (a drawing's ink extent is not its box) solved
        // the only way it can be, by making the box the ink.
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(kDotInk), pixels(kDotInk)})
                .with_transparent_bg()
                // The gap the eye sees is dot-ink to first glyph; the label's
                // own rect already spends kLabelInsetX of it before its first
                // glyph, so the margin is what is left. Same idiom as the
                // composer meter's "10 ... less the 5 the effort run's box
                // already carries".
                .with_margin(Margin{
                    .right = pixels(kDotLabelGap - kLabelInsetX)})
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
                // kLinePitch, so the placeholder's text box is the box the
                // line it stands in for will have.
                .with_size(ComponentSize{children(), pixels(kLinePitch)})
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
                .with_size(ComponentSize{children(), pixels(kLinePitch)})
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
                        const std::string& findQuery, int messageIndex,
                        std::size_t findLine) {
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
        const auto ep = mk(parent, 100 + seg);
        Entity& headingEnt = ep.first.get();
        auto& ld = headingEnt.addComponentIfMissing<ecs::LineDrawState>();
        ld.text = text;
        ld.query = findQuery;
        if (const auto* hits = paint_offsets_for(messageIndex, findLine))
            ld.findOffsets = *hits;
        else
            ld.findOffsets.clear();
        ld.id = headingEnt.id;
        ecs::LineDrawState* ldp = &ld;
        cfg = cfg.with_on_draw_bg([ldp, fontPx](RectangleType r) {
            hanabi::text_select::draw(ldp->id, r, ldp->text, fontPx);
            if (!ldp->query.empty())
                hanabi::find_highlight::draw(r, ldp->text, ldp->query, fontPx,
                                             &ldp->findOffsets);
        });
        auto el = div(ctx, ep, cfg);
        selectable_text(ctx, el.ent(), text, fontPx);
    }

    void render_rich_body(UIContext<InputAction>& ctx, Entity& parent,
                          const std::string& shown, float textW,
                          float winTop = 0.0f, float winBot = -1.0f,
                          float bodyStartY = 0.0f,
                          const std::string& findQuery = std::string(),
                          int messageIndex = -1) {
        // The find text arrives from the caller, which is the only level that
        // knows WHICH message this body belongs to — and therefore whether an
        // operator has excluded it from the search.
        const bool cull = winBot > winTop;
        size_t start = 0;
        int seg = 0;
        std::size_t findLine = 0;
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
        const float probeStartY = bodyStartY;
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
                const float blockH = code_block_h(
                    static_cast<int>(codeLines.size()), !lang.empty());
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
                                   md_heading_text(line), blockH, findQuery,
                                   messageIndex, findLine);
                }
                ++seg;
                ++findLine;
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
            const std::size_t thisFindLine = findLine;
            if (!line.empty()) ++findLine;
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
                // Work-tracker ids in this line, coloured before the spans are
                // handed over. Detection reads the VISIBLE text, the same
                // string the measure pass wrapped, so a link can never change
                // where a line breaks.
                const std::vector<hanabi::links::Link> lnks =
                    links_in(ip.visible);
                colour_links(ip, lnks);
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
                // mk() hands back the entity BEFORE the widget is built, and
                // that entity is the same one every frame, so the draw state
                // lives on it and the callbacks capture one pointer to it
                // (ecs::LineDrawState). They used to capture the text, the
                // query and a shared_ptr for the id -- three heap objects,
                // cloned again by each of afterhours' three ComponentConfig
                // copies. Assigning into the component's existing strings
                // reuses their capacity, so a steady frame allocates none.
                const auto ep = mk(parent, 100 + seg);
                Entity& lineEnt = ep.first.get();
                auto& ld = lineEnt.addComponentIfMissing<ecs::LineDrawState>();
                ld.text = ip.visible;
                ld.query = findQuery;
                if (const auto* hits =
                        paint_offsets_for(messageIndex, thisFindLine))
                    ld.findOffsets = *hits;
                else
                    ld.findOffsets.clear();
                ld.links = lnks;
                ld.id = lineEnt.id;
                ecs::LineDrawState* ldp = &ld;
                cfg = cfg.with_on_draw_bg([ldp](RectangleType r) {
                    hanabi::text_select::draw(ldp->id, r, ldp->text,
                                              theme::type::BODY);
                    if (!ldp->query.empty())
                        hanabi::find_highlight::draw(r, ldp->text, ldp->query,
                                                     theme::type::BODY,
                                                     &ldp->findOffsets);
                });
                // The underline goes OVER the glyphs' own row, so it is drawn
                // in the foreground pass; the colour alone would leave an id
                // looking like emphasis rather than a link.
                if (!lnks.empty())
                    cfg = cfg.with_on_draw_fg([ldp](RectangleType r) {
                        hanabi::links::draw_underlines(r, ldp->text,
                                                       ldp->links,
                                                       theme::type::BODY);
                    });
                auto lineEl = div(ctx, ep, cfg);
                selectable_text(ctx, lineEl.ent(), ip.visible,
                                theme::type::BODY);
                link_hotspot(ctx, lineEl.ent(), ip.visible, lnks,
                             theme::type::BODY);
            }
            ++seg;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        flush(8888);
        // Guarded, because the ARGUMENTS are what cost: rich_body_h(shown,
        // textW) is a full second measure pass over the whole body -- an
        // md_to_spans and a wrap per line -- and C++ evaluates it whether or
        // not the probe is on. `compare` returning early was doing nothing
        // for the price of the work it was handed.
        if (hanabi::mprobe::on())
            hanabi::mprobe::compare("richbody", rich_body_h(shown, textW),
                                    y - probeStartY);
    }

    // ---- Per-message actions (hover) --------------------------------------
    // afterhours has no text selection on read-only labels (see
    // afterhours_gaps.md #36), so there is no way to drag across an answer and
    // copy it. A per-message Copy is the affordance that replaces it, painted
    // only while the pointer is over that turn and confirmed in place for a
    // moment after a click.
    //
    // It is an OVERLAY on the bubble's top-right corner, which is what Puffin
    // does (`.overlay(alignment: .topTrailing) { copyButton }`, and the button
    // carries the bubble's own fill so it masks the text it covers). It used
    // to be a reserved row UNDER every turn, and the air that row held was
    // never free: 22px plus a 2px gap on every message put 43px between two
    // turns where Puffin puts 24, so the whole transcript ran progressively
    // further down the pane than the reference with every exchange. On
    // `ref/01_home.png` that cost nothing measurable — the reference had no
    // transcript to disagree with — and on `ref/02_thread.png`, which does,
    // removing it is worth 0.62 structural points on `main`, the largest
    // single item in the region.
    //
    // Absolute, so it is skipped everywhere its size would feed into its
    // parent's and the bubble does not grow around it. Its width has to be
    // handed in as a PIXEL value: afterhours refuses percent() on an absolute
    // widget outright (`VALIDATE(false, "Absolute widgets should not use
    // Percent"`), so "as wide as the thing I am overlaying" is not sayable —
    // see afterhours_gaps.md #97.
    static constexpr float kMsgActionsH = 22.0f;

    static model::PaneState* message_action_state() {
        AppComponent* app = app_singleton();
        Pane* pane = painting_pane();
        if (app == nullptr || pane == nullptr || !pane->openSession) return nullptr;
        return &model::pane_states().touch(model::pane_key(
            pane_index(*app, *pane), pane->openSession->summary.id));
    }
    static bool recent_action(const std::string& key,
                              const std::string& recordedKey,
                              std::chrono::steady_clock::time_point at) {
        if (recordedKey != key) return false;
        return std::chrono::steady_clock::now() - at <
               std::chrono::milliseconds(1600);
    }
    static bool recently_copied(const std::string& key) {
        const model::PaneState* state = message_action_state();
        return state != nullptr &&
               recent_action(key, state->copiedMessageKey,
                             state->copiedMessageAt);
    }
    static bool recently_retried(const std::string& key) {
        const model::PaneState* state = message_action_state();
        return state != nullptr &&
               recent_action(key, state->retriedMessageKey,
                             state->retriedMessageAt);
    }
    static void record_copied(const std::string& key) {
        model::PaneState* state = message_action_state();
        if (state == nullptr) return;
        state->copiedMessageKey = key;
        state->copiedMessageAt = std::chrono::steady_clock::now();
    }
    static void record_retried(const std::string& key) {
        model::PaneState* state = message_action_state();
        if (state == nullptr) return;
        state->retriedMessageKey = key;
        state->retriedMessageAt = std::chrono::steady_clock::now();
    }
    static void draw_copy_action(RectangleType r, theme::Color c) {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        afterhours::draw_rectangle_outline({cx - 4.0f, cy - 4.0f, 7.0f, 8.0f}, c);
        afterhours::draw_rectangle_outline({cx - 2.0f, cy - 2.0f, 7.0f, 8.0f}, c);
    }
    static void draw_retry_action(RectangleType r, theme::Color c) {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        afterhours::draw_ring(cx, cy, 3.2f, 4.4f, 18, c);
        afterhours::draw_line_ex({cx + 2.0f, cy - 4.0f},
                                 {cx + 5.0f, cy - 4.0f}, 1.4f, c);
        afterhours::draw_line_ex({cx + 5.0f, cy - 4.0f},
                                 {cx + 4.0f, cy - 1.0f}, 1.4f, c);
    }

    void message_actions(UIContext<InputAction>& ctx, Entity& host,
                         Entity& turn, float hostW, theme::Color hostFill,
                         int index, const std::string& key,
                         const std::string& rawText, int64_t sentAt = 0,
                         bool retryable = false) {
        model::PaneState* state = message_action_state();
        AppComponent* app = app_singleton();
        Pane* pane = painting_pane();
        const int paneIndex =
            app != nullptr && pane != nullptr ? pane_index(*app, *pane) : 0;
        const std::string paneSuffix = paneIndex == 0 ? "" : "_2";
        const bool copied = recently_copied(key);
        const bool retried = recently_retried(key);
        const bool hovering = ctx.mouse_was_in_subtree(turn.id) ||
                              hanabi::test_hooks::force_hover("msg:" + key);
        const bool focused =
            state != nullptr && state->focusedMessageActionKey == key;
        if (!copied && !retried && !hovering && !focused) return;

        auto bar = div(ctx, mk(host, 8),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(hostW), pixels(kMsgActionsH)})
                .with_absolute_position(0.0f, 0.0f)
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("msg_actions" + paneSuffix));

        const std::string stamp =
            show_times() ? fmtutil::clock_time(sentAt) : std::string();
        if (!stamp.empty()) {
            div(ctx, mk(bar.ent(), 2),
                ComponentConfig{}
                    .with_label(stamp)
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_margin(Margin{.right = pixels(6)})
                    .with_padding(Padding{.right = pixels(4),
                                          .left = pixels(4)})
                    .with_custom_background(hostFill)
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.35f)
                    .with_debug_name("msg_time"));
        }

        const auto add_button = [&](int id, const std::string& debugName,
                                    const std::string& label, bool active,
                                    bool retryIcon) -> Entity& {
            auto button = div(ctx, mk(bar.ent(), id),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(retryIcon ? 56.0f : 52.0f),
                                             pixels(18)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{.right = pixels(4), .left = pixels(3)})
                    .with_margin(Margin{.right = pixels(2)})
                    .with_custom_background(hostFill)
                    .with_custom_hover_bg(theme::hover_over(hostFill))
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_roundness(0.35f)
                    .with_debug_name(debugName));
            const theme::Color color =
                active ? theme::status_active() : theme::text_faint();
            div(ctx, mk(button.ent(), 1),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(11), pixels(16)})
                    .with_transparent_bg()
                    .with_margin(Margin{.right = pixels(2)})
                    .with_on_draw_fg([retryIcon, color](RectangleType r) {
                        if (retryIcon) draw_retry_action(r, color);
                        else draw_copy_action(r, color);
                    })
                    .with_debug_name((retryIcon ? "msg_retry_icon"
                                                    : "msg_copy_icon") +
                                                   paneSuffix));
            div(ctx, mk(button.ent(), 2),
                ComponentConfig{}
                    .with_label(label)
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_transparent_bg()
                    .with_custom_text_color(color)
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name((retryIcon ? "msg_retry_label"
                                                    : "msg_copy_label") +
                                                   paneSuffix));
            button.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            return button.ent();
        };

        Entity& copyButton =
            add_button(10, "msg_copy_btn" + paneSuffix,
                       copied ? "Copied" : "Copy",
                       copied, false);
        if (copyButton.get<afterhours::ui::HasClickListener>().down) {
            afterhours::clipboard::set_text(rawText);
            hanabi::test_hooks::record_clipboard_text(rawText);
            record_copied(key);
        }

        Entity* retryButton = nullptr;
        if (retryable) {
            Entity& button = add_button(20, "msg_retry_btn" + paneSuffix,
                                        retried ? "Queued" : "Retry",
                                        retried, true);
            retryButton = &button;
            if (button.get<afterhours::ui::HasClickListener>().down) {
                if (app != nullptr && pane != nullptr && pane->openSession) {
                    app->requestRetrySessionId = pane->openSession->summary.id;
                    app->requestRetryPrompt = rawText;
                    record_retried(key);
                }
            }
        }

        if (state != nullptr) {
            const bool actionFocused = ctx.has_focus(copyButton.id) ||
                (retryButton != nullptr && ctx.has_focus(retryButton->id));
            if (actionFocused) state->focusedMessageActionKey = key;
            else if (!hovering && state->focusedMessageActionKey == key)
                state->focusedMessageActionKey.clear();
        }
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
            const UserBox box = user_box(m, paneWidth, isLive, index,
                                         streamPhase);
            const float bubbleW = box.bubbleW;
            const auto& mr = measured(m, box.textW, isLive, index,
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
                    .with_debug_name(hanabi::mprobe::on()
                                         ? ("user_turn#" +
                                            std::to_string(index))
                                         : std::string("user_turn")));
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
                    // Top, not centre: on a tall message the avatar belongs
                    // beside the FIRST line, not floating halfway down.
                    .with_align_items(AlignItems::FlexStart)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("user_row"));
            // Circular avatar to the LEFT of the bubble. A circle is drawn,
            // not approximated with a fully-rounded box: afterhours' roundness
            // is a fraction of the shorter side, and a 22px square at 0.5 still
            // renders as a squircle at this size.
            div(ctx, mk(row.ent(), 0),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(kAvatarD),
                                             pixels(kAvatarD)})
                    .with_margin(Margin{.top = pixels(kAvatarTop),
                                        .right = pixels(kAvatarGap)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_on_draw_fg([](RectangleType rc) {
                        draw_user_avatar(rc);
                    })
                    .with_debug_name("user_avatar"));
            auto bub = div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(bubbleW), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_custom_background(chat_colors::user_bubble())
                    .with_padding(Padding{.top = pixels(kBubblePadTop),
                                          .right = pixels(kBubbleCfgPadX),
                                          .bottom = pixels(kBubblePadBot),
                                          .left = pixels(kBubbleCfgPadX)})
                    .with_corner_radius(kBubbleCorner)
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
            const auto uep = mk(bub.ent(), 2);
            Entity& userEnt = uep.first.get();
            auto& uld = userEnt.addComponentIfMissing<ecs::LineDrawState>();
            uld.text = userBody;
            uld.query = uq;
            if (const auto* hits = paint_offsets_for(index, 0))
                uld.findOffsets = *hits;
            else
                uld.findOffsets.clear();
            uld.id = userEnt.id;
            ecs::LineDrawState* uldp = &uld;
            ucfg = ucfg.with_on_draw_bg([uldp](RectangleType r) {
                hanabi::text_select::draw(uldp->id, r, uldp->text,
                                          theme::type::BODY);
                if (!uldp->query.empty())
                    hanabi::find_highlight::draw(
                        r, uldp->text, uldp->query, theme::type::BODY,
                        &uldp->findOffsets);
            });
            auto uEl = div(ctx, uep, ucfg);
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
            message_actions(ctx, bub.ent(), uturn.ent(), bubbleW,
                            chat_colors::user_bubble(), index,
                            m.id.empty() ? ("msg" + std::to_string(index))
                                         : m.id,
                            m.text, m.created_at,
                            m.sync != api::SyncState::Persisting);
            return;
        }

        // ---- ASSISTANT: left-aligned bubble -------------------------------
        float textW = asst_text_w(paneWidth);
        const auto& mr = measured(m, textW, isLive, index, streamPhase,
                                  /*rich=*/true);
        const int lineCount = mr.line_count;
        AppComponent* app = app_singleton();
        const std::string mkey =
            m.id.empty() ? ("msg" + std::to_string(index)) : m.id;
        const bool expanded = app && app->expandedMsgs.count(mkey) != 0;
        // The SAME predicate the measure pass used (bubble_height ->
        // is_folded), not a second copy of the rule: the two expressions had
        // already drifted apart — is_folded unfolds a message find has a match
        // in, and this copy did not, so a search made the drawn turn shorter
        // than the height every spacer below it was placed from.
        const bool folded = is_folded(m, index, lineCount, isLive);
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
                .with_debug_name(hanabi::mprobe::on()
                                     ? ("asst_turn#" + std::to_string(index))
                                     : std::string("asst_turn")));
        // Hoverable only because of this listener (ResolveHitTarget skips
        // anything without one); it deliberately does nothing.
        turn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        turn.ent().get<afterhours::HasColor>().skip_hover_override = true;

        // Meta row above the body: STATE only, right-aligned, on the first
        // assistant message of a turn (V2 grouping) — continuation fragments
        // suppress it. It used to lead with a relative time; Puffin stamps no
        // turn, so the time is gone and the row exists only when the run has a
        // subtitle or is still arriving. When it has nothing to say it is not
        // emitted at all, and has_author_row is what both the measure and this
        // draw ask so the 18px cannot drift.
        const bool authorRow = has_author_row(m, isLive, showAuthor);
        if (authorRow) {
        const std::string ts = turn_meta_text(m, isLive);
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
        }  // authorRow
        // Body starts below the turn's top margin + meta row (when shown):
        // cull the body's off-screen line-segments (intra-message
        // virtualization). Must mirror bubble_height's author-row term exactly.
        const float bodyStartY =
            itemTopY + (kTurnGapTop + 8.0f) +
            (authorRow ? (kAuthorH + kAuthorGap) : 0.0f) + kBubblePadTop;
        // The body lives inside a left-aligned bubble now (the reference draws
        // the assistant's answer on its own dark surface, not as bare column
        // text). Its padding is the same two constants bubble_height adds.
        auto asstBubble = div(ctx, mk(turn.ent(), 5),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(asst_bubble_w(paneWidth)),
                                         children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_custom_background(chat_colors::asst_bubble())
                .with_padding(Padding{.top = pixels(kBubblePadTop),
                                      .right = pixels(kBubbleCfgPadX),
                                      .bottom = pixels(kBubblePadBot),
                                      .left = pixels(kBubbleCfgPadX)})
                .with_corner_radius(kBubbleCorner)
                .with_debug_name("asst_bubble"));
        // THINKING INDICATOR (Gabe: "we are missing these 'in progress, I'm
        // thinking' UI"): while the live turn is still THINKING (no visible
        // tokens yet), show a pulsing accent dot + an italic "Thinking…" label
        // + an elapsed timer, instead of the plain "thinking…" body text. Once
        // real tokens stream in (phase Streaming), fall through to the normal
        // document render so the text takes over.
        if (isLive && streamPhase == AppComponent::StreamPhase::Thinking) {
            render_thinking_indicator(ctx, asstBubble.ent(), app);
        } else {
            render_rich_body(ctx, asstBubble.ent(), shown, textW, winTop,
                             winBot, bodyStartY, paint_query_for(index), index);
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
                ++app->findFoldVersion;
                invalidate_item_geometry(index);
            }
        }
        message_actions(ctx, asstBubble.ent(), turn.ent(),
                        asst_bubble_w(paneWidth), chat_colors::asst_bubble(),
                        index, mkey, m.text, m.created_at);
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
    static std::string format_tool_duration(int64_t durationMs) {
        if (durationMs <= 0) return "";
        long long s = durationMs / 1000;
        if (s < 1) return std::to_string(durationMs) + "ms";
        if (s < 60) return std::to_string(s) + "s";
        long long mn = s / 60;
        long long rs = s % 60;
        return rs ? (std::to_string(mn) + "m" + std::to_string(rs) + "s")
                  : (std::to_string(mn) + "m");
    }
    static std::string tool_duration(const api::Message& m) {
        return format_tool_duration(m.tool_duration_ms);
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
        const std::string node = tool_node(m);
        const std::string prefix = node.empty() ? "" : ("[" + node + "] ");
        if (!prefix.empty() && t.starts_with(prefix)) t.erase(0, prefix.size());
        if (t.empty()) t = !m.subtitle.empty() ? m.subtitle : std::string("tool call");
        return t;
    }
    static std::string tool_label(const api::Message& m) {
        const std::string command = tool_command(m);
        std::string out;
        if (!m.subtitle.empty() && m.subtitle != command) out = m.subtitle;
        const std::string node = tool_node(m);
        if (!node.empty()) {
            if (!out.empty()) out += "  \xc2\xb7  ";
            out += "[" + node + "]";
        }
        if (!command.empty() && command != out) {
            if (!out.empty()) out += "  \xc2\xb7  ";
            out += command;
        }
        return out;
    }
    static bool tool_failed(const api::Message& m) {
        return m.tool_status == "failed" || m.tool_status == "error";
    }
    static std::string pile_duration(const std::vector<api::Message>& msgs,
                                     int lo, int hi) {
        int64_t total = 0;
        for (int i = lo; i < hi; ++i)
            if (msgs[i].tool_duration_ms > 0) total += msgs[i].tool_duration_ms;
        return format_tool_duration(total);
    }
    static std::string pile_status(const std::vector<api::Message>& msgs,
                                   int lo, int hi) {
        std::string status;
        for (int i = lo; i < hi; ++i) {
            if (tool_failed(msgs[i])) return "failed";
            if (msgs[i].tool_status == "running") status = "running";
            else if (status.empty() && !msgs[i].tool_status.empty())
                status = msgs[i].tool_status;
        }
        return status;
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
                               theme::code_bg());
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
    // The mode belongs to the THREAD being rendered, so it is read off the
    // pane that is being painted rather than off the app: two panes on two
    // threads have two answers, and the composer strip's own popover (built
    // outside any pane) means the focused one.
    static hanabi::fold::Mode fold_mode() {
        const Pane* p = painting_pane();
        if (p == nullptr || !p->openSession) return hanabi::fold::kDefault;
        return hanabi::fold::from_int(
            Settings::get().get_tool_fold(p->openSession->summary.id));
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
        switch (fold_mode()) {
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
                            bool wasOpen, int messageIndex) {
        if (key.empty()) return;
        app.expandedPiles.erase(key);
        app.collapsedPiles.erase(key);
        if (wasOpen) app.collapsedPiles.insert(key);
        else app.expandedPiles.insert(key);
        invalidate_item_geometry(messageIndex);
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
        hanabi::prof::Scope _p("text.tool_out_lines");
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
        hanabi::prof::Scope _p("measure.tool_block_h");
        return kToolRowGap + kToolRowH + kToolRowGap + tool_out_height(app, m);
    }
    float tool_pile_height(AppComponent& app,
                           const std::vector<api::Message>& msgs, int lo,
                           int hi) {
        hanabi::prof::Scope _p("measure.tool_pile_h");
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

    static void draw_tool_fail(RectangleType r, theme::Color c) {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        afterhours::draw_line_ex(afterhours::vec2{cx - 3.0f, cy - 3.0f},
                                 afterhours::vec2{cx + 3.0f, cy + 3.0f}, 1.7f, c);
        afterhours::draw_line_ex(afterhours::vec2{cx + 3.0f, cy - 3.0f},
                                 afterhours::vec2{cx - 3.0f, cy + 3.0f}, 1.7f, c);
    }

    void tool_count_badge(UIContext<InputAction>& ctx, Entity& parent,
                          int count, float rowW) {
        constexpr float badgeW = 82.0f;
        auto badge = div(ctx, mk(parent, 31),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(badgeW), pixels(18)})
                .with_absolute_position((rowW - badgeW) * 0.5f, 5.0f)
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::Center)
                .with_custom_background(theme::panel_bg())
                .with_roundness(0.5f)
                .with_debug_name("tool_count"));
        div(ctx, mk(badge.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(12), pixels(18)})
                .with_transparent_bg()
                .with_margin(Margin{.right = pixels(2)})
                .with_on_draw_fg([](RectangleType r) {
                    hanabi::icons::draw_at("layers", r.x + r.width * 0.5f,
                                           r.y + r.height * 0.5f, 11.0f,
                                           theme::text_secondary());
                })
                .with_debug_name("tool_count_icon"));
        div(ctx, mk(badge.ent(), 2),
            ComponentConfig{}
                .with_label(std::to_string(count) + " tool calls")
                .with_size(ComponentSize{children(), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::MICRO)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("tool_count_n"));
    }

    static float tool_status_width(const std::string& dur,
                                   const std::string& status) {
        float width = 0.0f;
        if (!dur.empty()) width += 42.0f;
        if (!status.empty()) width += std::min(78.0f, 12.0f + 6.0f * status.size());
        if (!status.empty()) width += 14.0f;
        return width;
    }

    void tool_status_cluster(UIContext<InputAction>& ctx, Entity& parent,
                             const std::string& dur,
                             const std::string& status, float rowW) {
        const float clusterW = tool_status_width(dur, status);
        if (clusterW <= 0.0f) return;
        const bool failed = status == "failed" || status == "error";
        auto cluster = div(ctx, mk(parent, 41),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(clusterW), pixels(18)})
                .with_absolute_position(rowW - 16.0f - clusterW, 5.0f)
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("tool_status_cluster"));
        if (!dur.empty()) {
            div(ctx, mk(cluster.ent(), 1),
                ComponentConfig{}
                    .with_label(dur)
                    .with_size(ComponentSize{pixels(38), pixels(18)})
                    .with_margin(Margin{.right = pixels(4)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("tool_dur"));
        }
        if (!status.empty()) {
            const float statusW = std::min(72.0f, 8.0f + 6.0f * status.size());
            div(ctx, mk(cluster.ent(), 2),
                ComponentConfig{}
                    .with_label(fmtutil::ellipsize(status, 12))
                    .with_size(ComponentSize{pixels(statusW), pixels(18)})
                    .with_margin(Margin{.right = pixels(2)})
                    .with_transparent_bg()
                    .with_custom_text_color(failed ? theme::tag_blocked_fg()
                                                   : theme::text_faint())
                    .with_font_size(theme::type::MICRO)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("tool_status"));
            const theme::Color dotC = failed ? theme::tag_blocked_fg()
                                             : theme::status_active();
            div(ctx, mk(cluster.ent(), 3),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(12), pixels(18)})
                    .with_transparent_bg()
                    .with_on_draw_fg([dotC, failed](RectangleType r) {
                        if (failed) draw_tool_fail(r, dotC);
                        else afterhours::draw_circle_v(
                            {r.x + r.width * 0.5f, r.y + r.height * 0.5f},
                            3.0f, dotC);
                    })
                    .with_debug_name("tool_check"));
        }
    }

    Entity& tool_row(UIContext<InputAction>& ctx, Entity& parent, int idbase,
                     float rowW, bool expandable, bool open,
                     const std::string& command, int count,
                     const std::string& dur, const std::string& status,
                     bool showCount = true) {
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
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_cursor(expandable ? afterhours::ui::CursorType::Pointer
                                        : afterhours::ui::CursorType::Default)
                .with_roundness(0.0f)
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
        const float leadW = 34.0f;
        const float rightW = tool_status_width(dur, status);
        const float commandRight = showCount
            ? (rowW * 0.5f - 45.0f)
            : (rowW - 16.0f - rightW - 8.0f);
        float commandW = commandRight - 10.0f - leadW;
        if (commandW < 44.0f) commandW = 44.0f;
        div(ctx, mk(head.ent(), 3),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(command, 120))
                .with_size(ComponentSize{pixels(commandW), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("tool_cmd"));
        if (showCount) tool_count_badge(ctx, head.ent(), count, rowW);
        tool_status_cluster(ctx, head.ent(), dur, status, rowW);
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

        float rowW = paneWidth;
        if (rowW < 160.0f) rowW = 160.0f;

        auto wrap = div(ctx, mk(parent, 260 + keyIndex * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(rowW), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("tool_pile"));

        std::string cmd = tool_label(msgs[lo]);
        int total = count;
        std::string dur = pile_duration(msgs, lo, hi);
        std::string status = pile_status(msgs, lo, hi);
        Entity& head = tool_row(ctx, wrap.ent(), 1, rowW, true, open, cmd,
                                total, dur, status);
        head.addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (app && head.get<afterhours::ui::HasClickListener>().down) {
            tool_toggle(*app, key, open, keyIndex);
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
                .with_label(fmtutil::ellipsize(m.subtitle, 10))
                .with_size(ComponentSize{pixels(64), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font("mono", theme::type::MICRO)
                .with_margin(Margin{.right = pixels(6)})
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sub_name"));
        const std::string node = tool_node(m);
        div(ctx, mk(row.ent(), 3),
            ComponentConfig{}
                .with_label(node.empty() ? "" : ("[" + node + "]"))
                .with_size(ComponentSize{pixels(92), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font("mono", theme::type::MICRO)
                .with_margin(Margin{.right = pixels(6)})
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sub_node"));
        float commandW = rowW - 14.0f - 19.0f - 70.0f - 98.0f - 116.0f;
        if (commandW < 40.0f) commandW = 40.0f;
        div(ctx, mk(row.ent(), 4),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(tool_command(m), 96))
                .with_size(ComponentSize{pixels(commandW), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font("mono", theme::type::SUBROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sub_cmd"));
        div(ctx, mk(row.ent(), 5),
            ComponentConfig{}
                .with_label(tool_duration(m))
                .with_size(ComponentSize{pixels(36), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::MICRO)
                .with_alignment(TextAlignment::Right)
                .with_margin(Margin{.right = pixels(4)})
                .with_debug_name("sub_dur"));
        const bool failed = tool_failed(m);
        div(ctx, mk(row.ent(), 6),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(m.tool_status, 10))
                .with_size(ComponentSize{pixels(58), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(failed ? theme::tag_blocked_fg()
                                               : theme::text_faint())
                .with_font_size(theme::type::MICRO)
                .with_alignment(TextAlignment::Right)
                .with_margin(Margin{.right = pixels(2)})
                .with_debug_name("sub_status"));
        if (!m.tool_status.empty()) {
            const theme::Color color = failed ? theme::tag_blocked_fg()
                                              : theme::status_active();
            div(ctx, mk(row.ent(), 7),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(12), pixels(16)})
                    .with_transparent_bg()
                    .with_on_draw_fg([color, failed](RectangleType rr) {
                        if (failed) draw_tool_fail(rr, color);
                        else afterhours::draw_circle_v(
                            {rr.x + rr.width * 0.5f, rr.y + rr.height * 0.5f},
                            2.5f, color);
                    })
                    .with_debug_name("sub_check"));
        }
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
                .with_custom_background(theme::code_bg())
                .with_border(theme::border_soft(), pixels(1.0f))
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
        // The kind is the answer; the subtitle is how the adapter used to say
        // it and how the http adapter and the mock's literals still do.
        return m.kind == api::EventKind::Thinking ||
               (m.role == api::Role::Assistant && m.subtitle == "thinking");
    }

    // ---- Event rows -------------------------------------------------------
    //
    // A session emits things that are not speech: a skill loading, a node
    // attaching, a delivery the platform made, the agent's own status. They
    // are NOT painted alike, deliberately — he named them separately because
    // he wants to tell them apart, and a row that says only "something
    // happened" is the same gap as no row at all. Each carries its own word,
    // its own colour and its own shape: a one-line stamp for the facts, a
    // disclosure for a delivery (which has a body worth reading), and the
    // spawn card for a sub-agent.
    static bool is_one_line_event(const api::Message& m) {
        switch (m.kind) {
            case api::EventKind::Node:
            case api::EventKind::Skill:
            case api::EventKind::Notice:
            case api::EventKind::Status:
            case api::EventKind::Plan:
            case api::EventKind::Goal: return true;
            default: return false;
        }
    }
    static bool is_delivery(const api::Message& m) {
        return m.kind == api::EventKind::Delivery;
    }

    struct EventStyle {
        const char* word;
        theme::Color tint;
    };
    static EventStyle event_style(const api::Message& m) {
        switch (m.kind) {
            case api::EventKind::Node:     return {"node", theme::status_active()};
            case api::EventKind::Skill:    return {"skill", theme::link()};
            case api::EventKind::Status:   return {"status", theme::status_review()};
            case api::EventKind::Plan:     return {"plan", theme::accent()};
            case api::EventKind::Goal:     return {"goal", theme::status_active()};
            case api::EventKind::Notice:   return {"notice", theme::destructive()};
            case api::EventKind::Delivery: return {"delivered", theme::accent()};
            default:                       return {"event", theme::text_faint()};
        }
    }

    // "node · attached mac-GRQ7Y259H4" — the verb and the subject, in that
    // order, because the verb is what changed and the subject is which one.
    static std::string event_line(const api::Message& m) {
        switch (m.kind) {
            case api::EventKind::Node:
                return m.text + "  " + m.subtitle;
            case api::EventKind::Skill:
                return m.subtitle;
            case api::EventKind::Status:
                return m.text.empty() ? m.subtitle : m.subtitle + "  " + m.text;
            default:
                return m.text.empty() ? m.subtitle : m.text;
        }
    }

    static constexpr float kEventRowH = 20.0f;
    static constexpr float kEventRowGap = 4.0f;
    static constexpr float kEventInset = 8.0f;

    static float event_row_height() {
        return kEventRowH + 2.0f * kEventRowGap;
    }

    // The class word in its own colour, then the fact in the body colour, as
    // ONE label. `with_styled_label` takes coloured runs and sets the label to
    // their concatenation, so the row measures, aligns and overflows like any
    // other label -- a two-child row with a fixed-width first column would
    // have to guess that width in pixels and would drift with the font.
    static std::vector<afterhours::ui::TextSpan> event_spans(
        const api::Message& m) {
        const EventStyle st = event_style(m);
        return {{std::string(st.word) + "   ", st.tint},
                {fmtutil::ellipsize(event_line(m), 110),
                 theme::text_secondary()}};
    }

    void render_event_row(UIContext<InputAction>& ctx, Entity& parent,
                          int index, const api::Message& m, float colW) {
        div(ctx, mk(parent, 3600 + index * 10),
            ComponentConfig{}
                .with_styled_label(event_spans(m))
                .with_size(ComponentSize{pixels(colW - kEventInset),
                                         pixels(kEventRowH)})
                .with_margin(Margin{.top = pixels(kEventRowGap),
                                    .bottom = pixels(kEventRowGap),
                                    .left = pixels(kEventInset)})
                .with_transparent_bg()
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("event_row"));
    }

    // A delivery has a BODY — a child's settlement, a peer session's message,
    // a subscription's payload — so it folds like reasoning does rather than
    // being ellipsized into a stamp. Its head names the source and, when the
    // wire said so, whether it actually landed.
    static std::string delivery_head(const api::Message& m) {
        std::string head = m.subtitle.empty() ? std::string("delivery")
                                              : m.subtitle;
        if (!m.tool_status.empty()) head += "  \xc2\xb7  " + m.tool_status;
        return head;
    }
    static std::string delivery_key(const api::Message& m, int index) {
        return m.id.empty() ? ("deliv" + std::to_string(index)) : ("d" + m.id);
    }
    static float delivery_height(AppComponent& app, const api::Message& m,
                                 int index, float colW) {
        if (app.expandedThinking.count(delivery_key(m, index)) == 0)
            return event_row_height();
        return event_row_height() +
               rich_body_h(strip_inline_md(m.text),
                           colW - kEventInset - kThinkingInset) +
               kThinkingPadBot;
    }

    void render_delivery_row(UIContext<InputAction>& ctx, Entity& parent,
                             int index, const api::Message& m,
                             AppComponent& app, float colW) {
        const std::string key = delivery_key(m, index);
        const bool open = app.expandedThinking.count(key) != 0;
        const EventStyle st = event_style(m);

        auto wrap = div(ctx, mk(parent, 3700 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW - kEventInset), children()})
                .with_margin(Margin{.left = pixels(kEventInset)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("delivery_block"));

        auto head = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW - kEventInset),
                                         pixels(event_row_height())})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("delivery_head"));
        head.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (head.ent().get<afterhours::ui::HasClickListener>().down) {
            if (open) app.expandedThinking.erase(key);
            else app.expandedThinking.insert(key);
            invalidate_item_geometry(index);
        }

        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(14), pixels(18)})
                .with_transparent_bg()
                .with_on_draw_fg([open](RectangleType r) {
                    hanabi::glyph::chevron(r, !open, theme::text_faint(), 3.2f);
                })
                .with_debug_name("delivery_chev"));

        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_styled_label(
                    {{std::string(st.word) + "   ", st.tint},
                     {fmtutil::ellipsize(delivery_head(m), 90),
                      theme::text_secondary()}})
                .with_size(ComponentSize{pixels(colW - kEventInset - 14.0f),
                                         pixels(18)})
                .with_transparent_bg()
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("delivery_summary"));

        if (!open) return;

        auto body = div(ctx, mk(wrap.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{
                    pixels(colW - kEventInset - kThinkingInset), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.left = pixels(kThinkingInset),
                                    .bottom = pixels(kThinkingPadBot)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("delivery_body"));
        render_rich_body(ctx, body.ent(), strip_inline_md(m.text),
                         colW - kEventInset - kThinkingInset);
    }

    // The fold's own comment has always said this row is "one quiet row
    // saying how much reasoning there is", and the label was the constant
    // "Thought for a moment" — the same six words above four lines of
    // reasoning and above four hundred. Six seconds of thought and six
    // minutes of it are different facts about the turn, and a reader deciding
    // whether to open the fold has nothing else to decide on.
    static std::string thinking_summary(const api::Message& m) {
        size_t words = 0;
        bool in_word = false;
        for (char c : m.text) {
            const bool space = (c == ' ' || c == '\n' || c == '\t');
            if (!space && !in_word) ++words;
            in_word = !space;
        }
        if (words == 0) return "Thought for a moment";
        return "Thought for a moment  \xc2\xb7  " + std::to_string(words) +
               (words == 1 ? " word" : " words");
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
               rich_body_h(strip_inline_md(m.text),
                           colW - kEventInset - kThinkingInset) +
               kThinkingPadBot;
    }

    void render_thinking_block(UIContext<InputAction>& ctx, Entity& parent,
                               int index, const api::Message& m,
                               AppComponent& app, float colW) {
        const std::string key = thinking_key(m, index);
        const bool open = app.expandedThinking.count(key) != 0;

        auto wrap = div(ctx, mk(parent, 3400 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW - kEventInset), children()})
                .with_margin(Margin{.left = pixels(kEventInset)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("thinking_block"));

        auto head = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW - kEventInset),
                                         pixels(kThinkingRowH)})
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
            invalidate_item_geometry(index);
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
                .with_label(thinking_summary(m))
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
                .with_size(ComponentSize{
                    pixels(colW - kEventInset - kThinkingInset), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.left = pixels(kThinkingInset),
                                    .bottom = pixels(kThinkingPadBot)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("thinking_body"));
        render_rich_body(ctx, body.ent(), strip_inline_md(m.text),
                         colW - kEventInset - kThinkingInset);
    }

    static bool is_spawn_tool(const api::Message& m) {
        // A spawn the FOLD recognized: three events (intent, spawned,
        // settled) already collapsed into one row carrying the child's title
        // and its outcome. That is the real thing; the tool-name list below
        // is the guess that came before it and only ever fired on the mock —
        // the tool a real session calls is `subagent__spawn`, which was not
        // in it, which is why "i dont see any subagents".
        if (m.kind == api::EventKind::SubAgent) return true;
        if (m.role != api::Role::Tool) return false;
        const std::string& n = m.subtitle;
        return n == "subagent__spawn" || n == "spawn_agent" || n == "spawn" ||
               n == "Task" || n == "task" || n == "sub_agent" ||
               n == "spawn_sub_agent";
    }
    static constexpr float kSpawnCardH = 38.0f;
    static float spawn_card_height() {
        return kToolRowGap + kSpawnCardH + kToolRowGap;
    }
    void render_spawn_card(UIContext<InputAction>& ctx, Entity& parent,
                           int index, const api::Message& m, float paneWidth) {
        float rowW = paneWidth;
        if (rowW < 160.0f) rowW = 160.0f;
        // A fold-recognized spawn (EventKind::SubAgent) knows the child's
        // TITLE — the name it was actually given, and the only thing that
        // tells one of six concurrent children from another. The prompt is
        // the fallback for a spawn recognized only by its tool name, which
        // has no title to show.
        const std::string task = (m.kind == api::EventKind::SubAgent &&
                                  !m.subtitle.empty())
                                     ? m.subtitle
                                     : tool_command(m);
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
                .with_transparent_bg()
                .with_roundness(0.0f)
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
        float rowW = paneWidth;
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
        const std::string oneCmd = tool_label(m);
        Entity& head = tool_row(ctx, parent, 200 + index * 10, rowW,
                                /*expandable=*/expandable, open,
                                oneCmd, 1, tool_duration(m), m.tool_status,
                                /*showCount=*/false);
        if (expandable && app && !key.empty()) {
            head.addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            if (head.get<afterhours::ui::HasClickListener>().down) {
                tool_toggle(*app, key, open, index);
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
