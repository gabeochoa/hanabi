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
#include "../search/session_corpus.h"
#include "../search/session_index.h"
#include "../ui/secondary_surface.h"
#include "components.h"
#include "keyboard_focus.h"
#include "ui_imports.h"

namespace ecs {

struct SessionSearchSystem : afterhours::System<UIContext<InputAction>> {
    static constexpr float kPanelW = 560.0f;
    static constexpr float kFieldH = hanabi::surface::kFieldH;
    static constexpr float kRowH = 52.0f;
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

        // Seeded once per opening, then DEEPENED a few transcripts a frame.
        //
        // This used to be one line that parsed the entire on-disk cache,
        // synchronously, on the frame the panel opened — a full nlohmann parse
        // per thread not already in memory, with no cap and no budget, growing
        // with the user's history forever (docs/SEARCH.md S5). Seeding is now
        // free of disk I/O entirely, and each frame reads kDeepenPerFrame more
        // transcripts, so the reads are spread over the frames somebody spends
        // typing rather than stacked on the one that opens the panel. Coverage
        // climbs as they type, and coverage_note is already the sentence that
        // says how far it has got.
        if (!indexed_) {
            corpus_.begin(collect_rows(*app));
            indexed_ = true;
        }
        corpus_.deepen(hanabi::search::kDeepenPerFrame,
                       [](const std::string& id) {
                           return load_flattened(id);
                       });

        // One more than fits, on purpose. kMaxRows was both the query limit
        // and the row limit, so a query hitting two hundred threads showed six
        // rows and said nothing at all about the other 194 — the note under
        // the results reports DEPTH and has never reported breadth
        // (docs/SEARCH.md S9). Asking for seven costs one more document's
        // scan and is the difference between "six results" and "the first
        // six". A true total is not worth it: it would mean scanning every
        // body to the end, every frame, over a corpus that can be tens of
        // megabytes.
        const std::vector<hanabi::search::Hit> found =
            corpus_.index().query(app->sessionSearchQuery, kMaxRows + 1);
        const bool overflowed = found.size() > kMaxRows;
        const std::vector<hanabi::search::Hit> hits(
            found.begin(),
            found.begin() + static_cast<std::ptrdiff_t>(
                                std::min(found.size(), kMaxRows)));

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

        const float rowsH =
            kRowH * static_cast<float>(hits.size()) +
            (overflowed ? kNoteH : 0.0f);
        const float wantedH = hanabi::surface::kSheetPadV * 2.0f +
                              hanabi::surface::kHeaderH + kFieldH + 12.0f +
                              rowsH + 12.0f + kNoteH;
        const hanabi::surface::Rect panelRect =
            hanabi::surface::top_centered(sw, sh, kPanelW, wantedH);
        const float contentW =
            panelRect.width - hanabi::surface::kSheetPadH * 2.0f;

        auto backdrop = button(
            ctx, mk(uiRoot, 8400),
            hanabi::surface::scrim(sw, sh, 10)
                .with_debug_name("xsearch_backdrop"));
        if (backdrop &&
            !afterhours::ui::is_mouse_inside(
                ctx.mouse.pos,
                RectangleType{panelRect.x, panelRect.y, panelRect.width,
                              panelRect.height})) {
            close(*app);
            return;
        }

        auto panelConfig = hanabi::surface::sheet(panelRect, 11);
        panelConfig.with_overflow(Overflow::Scroll, Axis::Y)
            .with_debug_name("xsearch_panel");
        auto panel = div(ctx, mk(uiRoot, 8410), panelConfig);

        auto header = div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kHeaderH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("xsearch_header"));
        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label("Search conversations")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kTitleH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("xsearch_heading"));
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label("Titles, previews, and locally cached messages")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kSubtitleH)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("xsearch_subtitle"));

        const std::string before = app->sessionSearchQuery;
        auto inputRes = afterhours::ui::imm::text_input(
            ctx, mk(panel.ent(), 2), app->sessionSearchQuery,
            hanabi::surface::field(contentW, 11)
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
            auto row = div(
                ctx, mk(panel.ent(), 100 + static_cast<int>(i)),
                hanabi::surface::option_row(contentW, kRowH, selected, 11)
                    .with_margin(Margin{.top = pixels(i == 0 ? 8 : 2)})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_padding(Padding{.top = pixels(4), .left = pixels(10),
                                          .bottom = pixels(4),
                                          .right = pixels(8)})
                    .with_cursor(afterhours::ui::CursorType::Pointer)
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
                    .with_size(ComponentSize{pixels(contentW - 26.0f), pixels(18)})
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
                        .with_size(ComponentSize{pixels(contentW - 26.0f), pixels(16)})
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

        // The other admission: there were more, and these are the first of
        // them. It sits with the rows rather than in the coverage note,
        // because it is a fact about the RESULTS and the note is a fact about
        // the corpus — and because the note is already three clauses long.
        if (overflowed)
            div(ctx, mk(panel.ent(), 4),
                ComponentConfig{}
                    .with_label("More matches \xe2\x80\x94 keep typing to "
                                "narrow")
                    .with_size(ComponentSize{pixels(contentW), pixels(kNoteH)})
                    .with_margin(Margin{.top = pixels(2), .left = pixels(10)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name("xsearch_more"));

        // The admission. It renders whether or not anything matched, because
        // "no results" is the case where it matters most.
        div(ctx, mk(panel.ent(), 3),
            ComponentConfig{}
                .with_label(note_for(*app, corpus_, hits))
                .with_size(ComponentSize{pixels(contentW), pixels(kNoteH)})
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
    hanabi::search::CorpusBuilder corpus_;
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
        corpus_ = hanabi::search::CorpusBuilder{};
    }

    // Open the thread and hand the query to find-in-conversation, so the match
    // is highlighted and scrolled to by the machinery that already does that —
    // and counted by the same tally, which counts every paintable match in the
    // thread rather than the ones the window happens to be showing.
    void open(const hanabi::search::Hit& hit, AppComponent& app) {
        app.requestOpenTab = hit.id;
        app.view = SmartView::Chat;
        if (!app.sessionSearchQuery.empty()) {
            app.pane().findOpen = true;
            app.pane().findQuery = app.sessionSearchQuery;
            app.pane().findIndex = 0;
            app.pane().findScrollPending = true;
        }
        close(app);
    }

    static std::string note_for(const AppComponent& app,
                                const hanabi::search::CorpusBuilder& c,
                                const std::vector<hanabi::search::Hit>& hits) {
        // The rows are their own count; what the line is FOR is the part the
        // rows cannot say — how much of your history was actually read.
        std::string cov = hanabi::search::coverage_note(c.index().coverage());
        // ...and while the corpus is still arriving, that it is still
        // arriving. A "no matches" over a corpus that is 40% read is a
        // different claim from one over a corpus that is finished, and the
        // ellipsis is the whole of the difference a reader needs.
        if (!c.complete()) cov += "\xe2\x80\xa6";
        if (!app.sessionSearchQuery.empty() && hits.empty())
            return "No matches. " + cov;
        return cov;
    }

    // Everything readable about one thread WITHOUT touching the disk: the list
    // row, plus the in-memory copy when there is one.
    //
    // The in-memory LRU used to be preferred over the disk copy unconditionally
    // — "it is the newer of the two whenever they differ" — and the result was
    // stamped Depth::Full. Both halves were wrong together: the LRU holds only
    // a thread's last 20 messages (ecs::model::kCacheMaxMessagesPerThread), so
    // the five threads you had just been reading were the SHALLOWEST entries in
    // the corpus, and the sentence under the results called them full text
    // (docs/SEARCH.md S2). Newer is not fuller.
    //
    // So a cut copy is marked as one, and the thread stays in the deepening
    // queue: what is on disk may hold more of it, and whichever holds more
    // wins.
    static std::vector<hanabi::search::Row> collect_rows(AppComponent& app) {
        std::vector<hanabi::search::Row> rows;
        rows.reserve(app.sessions.size());
        for (const auto& s : app.sessions) {
            hanabi::search::Row r;
            r.id = s.id;
            r.title = s.title;
            r.preview = s.preview;
            r.updated_at = s.updated_at;
            if (const api::Session* held = app.transcriptCache.peek(s.id)) {
                r.held = flatten(*held);
                r.has_held = true;
                r.held_is_tail = app.transcriptCache.truncated(s.id) ||
                                 held->has_more_older;
            }
            rows.push_back(std::move(r));
        }
        return rows;
    }

    // The disk half, called at most kDeepenPerFrame times a frame.
    static std::optional<hanabi::search::Loaded> load_flattened(
        const std::string& id) {
        auto disk = api::disk_cache::load_transcript(id);
        if (!disk) return std::nullopt;
        hanabi::search::Loaded out;
        out.body = flatten(*disk);
        out.windowed = disk->has_more_older;
        return out;
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
