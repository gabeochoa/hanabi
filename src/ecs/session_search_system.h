#pragma once

// ---------------------------------------------------------------------------
// Search across threads (Cmd+Shift+F).
//
// The find bar searches the thread you are reading; the sidebar's box filters
// the list. Neither answers "which conversation was that in" — that is this.
//
// It searches a LOCAL corpus (src/search/session_index.h says why, at length:
// there is no server verb to ask, and inventing an endpoint is worse than not
// having one). The corpus is every session's title and preview, plus the
// transcripts this machine holds — the in-memory LRU, and the on-disk cache on
// a real backend. Which means the answer is usually PARTIAL, and the panel says
// so under the results rather than letting a count imply it read everything.
//
// It owns no behaviour of its own: a result row raises the same
// requestOpenTab the sidebar raises, and hands the query to the existing find
// bar so the opened thread highlights and scrolls to the term through the
// machinery that already counts matches honestly.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "../api/disk_cache.h"
#include "../keys.h"
#include "../search/session_index.h"
#include "components.h"
#include "keyboard_focus.h"
#include "ui_imports.h"

namespace ecs {

struct SessionSearchSystem : afterhours::System<UIContext<InputAction>> {
    static constexpr float kPanelW = 560.0f;
    // Explicit, because afterhours has no flex-grow: a percent child of a
    // NoWrap column resolves against the whole panel and overflows its
    // padding (afterhours_gaps.md #18).
    static constexpr float kRowW = 500.0f;
    static constexpr float kTextW = kRowW - 26.0f;
    static constexpr float kFieldH = 34.0f;
    static constexpr float kRowH = 46.0f;
    static constexpr float kNoteH = 18.0f;
    static constexpr size_t kMaxRows = 6;

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Cmd+Shift+F. Cmd+F (find in this conversation) is the same key with
        // the Shift off, and main_pane_system.h refuses it while Shift is held
        // so one keystroke opens one thing.
        if (hanabi::keys::cmd_down() && hanabi::keys::shift_down() &&
            hanabi::keys::pressed(hanabi::keys::kF)) {
            app->sessionSearchOpen = !app->sessionSearchOpen;
            app->sessionSearchQuery.clear();
            app->sessionSearchIndex = 0;
            indexed_ = false;
        }
        if (!app->sessionSearchOpen) {
            wasOpen_ = false;
            return;
        }
        // Opening a search panel and finding the caret somewhere else is a
        // panel that does not work: the chord's whole promise is "type now".
        // A field has to have been rendered once before it can be grabbed, and
        // the grab has to be re-asserted for a couple of frames, hence a short
        // window rather than one set (afterhours_gaps.md #56/#57).
        if (!wasOpen_) focusFrames_ = 3;
        wasOpen_ = true;

        if (app->escape == EscapeIntent::CloseSessionSearch) {
            close(*app);
            return;
        }

        // Built once per opening: reading the disk cache is file I/O, and the
        // corpus cannot change while a modal is up (nothing behind it can open
        // a thread). Typing re-queries the built index, which is in memory.
        if (!indexed_) {
            index_ = build_index(*app);
            indexed_ = true;
        }

        const std::vector<hanabi::search::Hit> hits =
            index_.query(app->sessionSearchQuery, kMaxRows);

        if (app->arrow == ArrowIntent::SessionSearch && !hits.empty()) {
            app->sessionSearchIndex += app->arrowDelta;
            if (app->sessionSearchIndex < 0) app->sessionSearchIndex = 0;
            if (app->sessionSearchIndex >= static_cast<int>(hits.size()))
                app->sessionSearchIndex = static_cast<int>(hits.size()) - 1;
        }
        if (hanabi::keys::pressed(hanabi::keys::kEnter) && !hits.empty()) {
            const size_t i = static_cast<size_t>(
                std::clamp(app->sessionSearchIndex, 0,
                           static_cast<int>(hits.size()) - 1));
            open(hits[i], *app);
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            hanabi::viewport::width();
        const float sh =
            hanabi::viewport::height();

        auto backdrop = button(ctx, mk(uiRoot, 8400),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(sw), pixels(sh)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_custom_background(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_custom_hover_bg(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_render_layer(10)
                .with_debug_name("xsearch_backdrop"));
        if (backdrop) {
            close(*app);
            return;
        }

        const float rowsH =
            kRowH * static_cast<float>(std::min(hits.size(), kMaxRows));
        // Slack on purpose: afterhours has no flex-grow, and a column whose
        // children add up to exactly its height gets silently re-solved (and
        // warned about every frame — gap #53).
        const float ph = kFieldH + 12.0f + rowsH + 12.0f + kNoteH + 24.0f;
        const float px = (sw - kPanelW) * 0.5f;
        const float py = std::min(120.0f, sh * 0.15f);

        auto panel = div(ctx, mk(uiRoot, 8410),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(kPanelW), pixels(ph)})
                .with_absolute_position()
                .with_translate(px, py)
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(8), .right = pixels(8),
                                      .bottom = pixels(8), .left = pixels(8)})
                .with_roundness(0.35f)
                .with_render_layer(11)
                .with_debug_name("xsearch_panel"));

        const std::string before = app->sessionSearchQuery;
        auto inputRes = afterhours::ui::imm::text_input(
            ctx, mk(panel.ent(), 1), app->sessionSearchQuery,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kFieldH)})
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_render_layer(11)
                .with_debug_name("xsearch_input"));
        if (app->sessionSearchQuery != before) app->sessionSearchIndex = 0;
        if (focusFrames_ > 0) {
            --focusFrames_;
            ctx.set_focus(focusable_field(inputRes.ent()));
        }

        for (size_t i = 0; i < hits.size(); ++i) {
            const bool selected = static_cast<int>(i) == app->sessionSearchIndex;
            // A div with a click listener rather than a button: a two-line
            // row is a column of its own labels, and afterhours sizes a
            // BUTTON from its text — which collapsed the row to one line and
            // warned about the overflow every frame (gap #53). Same shape the
            // sidebar's chat rows use.
            auto row = div(ctx, mk(panel.ent(), 100 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(kRowW), pixels(kRowH)})
                    .with_margin(Margin{.top = pixels(i == 0 ? 8 : 2)})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_padding(Padding{.top = pixels(4), .right = pixels(8),
                                          .bottom = pixels(4),
                                          .left = pixels(10)})
                    .with_custom_background(selected ? theme::selected_bg()
                                                     : theme::panel_bg())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_roundness(0.3f)
                    .with_render_layer(11)
                    .with_debug_name("xsearch_row_" + std::to_string(i)));
            row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            const bool rowClicked =
                row.ent().get<afterhours::ui::HasClickListener>().down;

            // A thread this search could only read the outside of says so on
            // its own row: the reason it matched is not the same kind of fact
            // as a hit inside the conversation. And a thread it read only the
            // TAIL of says that too — an absent match in one of those is not
            // evidence of anything (docs/SEARCH.md S2).
            std::string title = hits[i].title;
            if (hits[i].partial) title += "  (title and preview only)";
            else if (hits[i].windowed) title += "  (recent messages only)";
            div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_label(title)
                    .with_size(ComponentSize{pixels(kTextW), pixels(18)})
                    .with_transparent_bg()
                    .with_custom_text_color(selected ? theme::text_primary()
                                                     : theme::text_secondary())
                    .with_font_size(theme::type::BODY)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name("xsearch_title_" + std::to_string(i)));

            if (!hits[i].snippet.empty())
                div(ctx, mk(row.ent(), 2),
                    ComponentConfig{}
                        .with_label(hits[i].snippet)
                        .with_size(ComponentSize{pixels(kTextW), pixels(16)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_faint())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_render_layer(11)
                        .with_debug_name("xsearch_snippet_" +
                                         std::to_string(i)));

            if (rowClicked) {
                open(hits[i], *app);
                return;
            }
        }

        // The admission. It renders whether or not anything matched, because
        // "no results" is the case where it matters most.
        div(ctx, mk(panel.ent(), 3),
            ComponentConfig{}
                .with_label(note_for(*app, index_, hits))
                .with_size(ComponentSize{pixels(kRowW), pixels(kNoteH)})
                .with_margin(Margin{.top = pixels(8), .left = pixels(10)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(11)
                .with_debug_name("xsearch_note"));
    }

  private:
    hanabi::search::Index index_;
    bool indexed_ = false;
    bool wasOpen_ = false;
    int focusFrames_ = 0;

    void close(AppComponent& app) {
        app.sessionSearchOpen = false;
        app.sessionSearchQuery.clear();
        app.sessionSearchIndex = 0;
        indexed_ = false;
        // And drop the corpus. The index holds every indexed body TWICE — the
        // doc and a pre-lowered copy (session_index.h, Index::add) — so a
        // panel that has been opened once over a large cache and closed was
        // holding the app's whole transcript history, doubled, for the life of
        // the process. It is rebuilt on the next open regardless (indexed_ is
        // false above), so nothing is lost by letting it go.
        index_ = hanabi::search::Index{};
    }

    // Open the thread and hand the query to find-in-conversation, so the match
    // is highlighted and scrolled to by the machinery that already does that —
    // and counted by the same tally, which counts every paintable match in the
    // thread rather than the ones the window happens to be showing.
    void open(const hanabi::search::Hit& hit, AppComponent& app) {
        app.requestOpenTab = hit.id;
        app.view = SmartView::Chat;
        if (!app.sessionSearchQuery.empty()) {
            app.findOpen = true;
            app.findQuery = app.sessionSearchQuery;
            app.findIndex = 0;
            app.findScrollPending = true;
        }
        close(app);
    }

    static std::string note_for(const AppComponent& app,
                                const hanabi::search::Index& ix,
                                const std::vector<hanabi::search::Hit>& hits) {
        // The rows are their own count; what the line is FOR is the part the
        // rows cannot say — how much of your history was actually read.
        const std::string cov = hanabi::search::coverage_note(ix.coverage());
        if (!app.sessionSearchQuery.empty() && hits.empty())
            return "No matches. " + cov;
        return cov;
    }

    // Everything readable about one thread, and how deep the reading went.
    //
    // The in-memory LRU used to be preferred over the disk copy unconditionally
    // — "it is the newer of the two whenever they differ" — and the result was
    // stamped Depth::Full. Both halves were wrong together: the LRU holds only
    // a thread's last 20 messages (ecs::model::kCacheMaxMessagesPerThread), so
    // the five threads you had just been reading were the SHALLOWEST entries in
    // the corpus, and the sentence under the results called them full text
    // (docs/SEARCH.md S2). Newer is not fuller.
    //
    // So: prefer the LRU only while it holds the whole thread. When it was cut
    // down, the disk copy is read and whichever holds more messages wins — and
    // if the winner is itself a window (cut on the way into the cache, or
    // fetched with has_more_older), it is indexed as Windowed and the note says
    // so instead of claiming a depth nobody has.
    static hanabi::search::Index build_index(AppComponent& app) {
        hanabi::search::Index ix;
        for (const auto& s : app.sessions) {
            hanabi::search::Doc d;
            d.id = s.id;
            d.title = s.title;
            d.preview = s.preview;
            const api::Session* held = app.transcriptCache.peek(s.id);
            const bool heldCut =
                held != nullptr && app.transcriptCache.truncated(s.id);
            std::optional<api::Session> disk;
            if (held == nullptr || heldCut)
                disk = api::disk_cache::load_transcript(s.id);
            const bool takeDisk =
                disk.has_value() &&
                (held == nullptr ||
                 disk->messages.size() > held->messages.size());
            if (takeDisk) {
                d.body = flatten(*disk);
                d.depth = disk->has_more_older
                              ? hanabi::search::Depth::Windowed
                              : hanabi::search::Depth::Full;
            } else if (held != nullptr) {
                d.body = flatten(*held);
                d.depth = (heldCut || held->has_more_older)
                              ? hanabi::search::Depth::Windowed
                              : hanabi::search::Depth::Full;
            }
            ix.add(std::move(d));
        }
        return ix;
    }

    // The same rows find-in-conversation can paint: user and assistant text.
    // A snippet is a promise that the words are in the thread you are about to
    // open AND that the find bar will show you where — tool output and the
    // System caption have no highlight path (find_counts_only_what_it_could
    // _paint.e2e is the rule), so indexing them would promise a match that
    // opens a thread with nothing lit up in it.
    static std::string flatten(const api::Session& s) {
        std::string out;
        for (const auto& m : s.messages) {
            if (m.role != api::Role::User && m.role != api::Role::Assistant)
                continue;
            if (m.text.empty()) continue;
            out += m.text;
            out.push_back('\n');
        }
        return out;
    }
};

}  // namespace ecs
