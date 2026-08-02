#pragma once

// Renders the main pane (right of the sidebar, below the tab strip). Dispatches
// on AppComponent::view: the smart views (Home / Blocked / Review / Starred)
// are digest lists over the thread set; Chat renders the active tab's
// transcript as message bubbles.

#include <cstdlib>
#include <map>
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
                              r.width, r.height, ecs::model::in_blocked_view,
                              "Nothing is waiting on you. \xf0\x9f\x8e\x89",
                              /*singleState=*/true);
                break;
            case SmartView::Review:
                render_digest(ctx, panel.ent(), *app, "Ready for review",
                              r.width, r.height, ecs::model::in_review_view,
                              "No threads are ready for review yet.",
                              /*singleState=*/true);
                break;
            case SmartView::Starred:
                render_digest(ctx, panel.ent(), *app, "Starred", r.width,
                              r.height, ecs::model::in_starred_view,
                              "No starred conversations. Star a thread to pin "
                              "it here.");
                break;
        }
    }

  private:
    // The one AppComponent (transcript render needs it for expand/fold state).
    static AppComponent* app_singleton() {
        auto q = afterhours::EntityQuery({.force_merge = true})
                     .whereHasComponent<AppComponent>()
                     .gen();
        return q.empty() ? nullptr : &q[0].get().get<AppComponent>();
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
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_debug_name("main_note"));
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
    static float wrap_width(float paneW) {
        float innerW = paneW - 48.0f;
        return innerW < kWrapCap ? innerW : kWrapCap;
    }

    static Entity& centered_wrap(UIContext<InputAction>& ctx, Entity& scroll,
                                 int id, float innerW) {
        constexpr float kCap = kWrapCap;
        float wrapW = innerW < kCap ? innerW : kCap;
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
            note(ctx, parent, emptyMsg);
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
        std::string out;
        out.reserve(in.size());
        auto strip_paired = [](std::string s, const std::string& d) {
            std::string r;
            r.reserve(s.size());
            size_t i = 0;
            while (i < s.size()) {
                if (s.compare(i, d.size(), d) == 0) {
                    size_t close = s.find(d, i + d.size());
                    // require a non-empty, single-line span between delimiters
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
        out = strip_paired(in, "**");   // bold first (before single *)
        out = strip_paired(out, "__");
        out = strip_paired(out, "`");    // inline code
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
    // primitive — gap #11); the muted static bars are enough to signal pending.
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
                          first, theme::tag_blocked_fg());
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
                          first, theme::tag_ready_fg());
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
        for (char& c : s)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        return s;
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
                                    .left = pixels(2)})
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
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
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
            std::string t = normalize_title(app.openSession->summary.title);
            // The per-session detail response often omits the title (only the
            // LIST carries it), so a real transcript's own summary.title is
            // frequently empty → the header showed a bare "(untitled)" even
            // though the sidebar/tab knew the name. Fall back to the list
            // summary's title, then the id, before giving up.
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

        // The transcript pane splits into a scrolling message column (grows to
        // fill) and a persistent composer row pinned to the bottom. But a
        // READ-ONLY backend (no send/stream capability) should NOT show a dead,
        // disabled Send box — a greyed input the user can't use is worse than
        // none (critique #25/#98). So when the backend can't reply we HIDE the
        // composer entirely and give the whole pane to the transcript. Header
        // is 46; the composer (when shown) is a fixed strip.
        const bool canReply =
            app.client &&
            (app.client->supports_send() || app.client->supports_stream());
        const float kComposerH = canReply ? 74.0f : 0.0f;
        float listH = paneH - 46.0f - kComposerH;
        if (listH < 20.0f) listH = 20.0f;

        auto scroll = div(ctx, mk(parent, 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(listH)})
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(6), .left = pixels(18)})
                .with_debug_name("transcript_scroll"));

        // Empty-thread state: an open thread with no messages shows a tasteful
        // empty state inside the scroll region (mirrors the mock's empty
        // screen) — but the composer still renders below, so the pane never
        // shows a blank void.
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
        } else {
            // Capped, CENTERED reading column (critique #5/#6): a real transcript
            // ran text edge-to-edge across the wide pane (~110ch/line) which is
            // brutal to read — cap the message column to ~720px (~68ch, the
            // comfortable-reading measure ChatGPT/Claude use) and center it.
            constexpr float kMsgCol = 720.0f;
            float innerW = paneW - 36.0f;
            float colW = innerW < kMsgCol ? innerW : kMsgCol;
            Entity& col = centered_wrap(ctx, scroll.ent(), 7777, colW);

            // Sub-agent panel sits above the messages when the thread has steps.
            sub_agent_panel(ctx, col, app);

            // Is a live stream filling one of these bubbles right now? If so,
            // that message index gets the "thinking…" / caret affordance while
            // the phase isn't Done. Only applies to the OPEN session's stream.
            const bool streamingHere =
                app.streamActive &&
                app.streamSessionId == app.openSession->summary.id &&
                app.streamPhase != AppComponent::StreamPhase::Done;
            const size_t liveIdx = app.streamMsgIndex;

            int i = 0;
            const auto& msgs = app.openSession->messages;
            const int n = static_cast<int>(msgs.size());
            while (i < n) {
                const auto& m = msgs[i];
                // Collapse a RUN of >=2 consecutive tool-role messages into one
                // "N tool calls" pile (navi-website pattern). A lone tool call
                // stays a normal block. Never pile the one that's live-streaming
                // (rare, but keep it visible). This also bounds render cost on a
                // tool-heavy thread — a collapsed pile is one row, not N blocks.
                if (m.role == api::Role::Tool) {
                    int j = i;
                    while (j < n && msgs[j].role == api::Role::Tool) ++j;
                    const int runLen = j - i;
                    if (runLen >= 2) {
                        tool_pile(ctx, col, i, msgs, i, j, colW);
                        i = j;
                        continue;
                    }
                }
                const bool isLive =
                    streamingHere && static_cast<size_t>(i) == liveIdx;
                render_bubble(ctx, col, i, m, colW, isLive, app.streamPhase);
                ++i;
            }

            // Auto-stick-to-bottom WHILE STREAMING (critique #94/#71): a live
            // reply grows the content, so pin the scroll to the bottom each
            // frame so the caret/newest text stays visible (ChatGPT/Claude
            // behavior). We only force it during an active stream — otherwise
            // the user's own scroll position is respected. clamp_scroll() uses
            // last frame's content/viewport, so setting a large offset lands at
            // the true bottom.
            if (streamingHere &&
                scroll.ent().has<afterhours::ui::HasScrollView>()) {
                auto& sv = scroll.ent().get<afterhours::ui::HasScrollView>();
                sv.scroll_offset.y = 1e9f;
                sv.clamp_scroll();
            }
        }

        // Composer only when the backend can actually reply (see canReply).
        if (canReply) render_composer(ctx, parent, app, paneW, kComposerH);
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
                         AppComponent& app, float paneW, float composerH) {
        // PER-THREAD persistent drafts. One composer instance renders whichever
        // thread is active, so a single shared draft would leak thread A's
        // half-typed reply into thread B on a tab switch. Key the draft by
        // session id and bind a reference to THIS thread's slot, so each thread
        // keeps its own in-progress reply (kept function-local to avoid growing
        // AppComponent; the map is small — one short string per opened thread).
        static std::map<std::string, std::string> replyDrafts;
        const std::string draftKey =
            app.openSession ? app.openSession->summary.id : std::string();
        std::string& replyDraft = replyDrafts[draftKey];

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
        // stream draining into this thread — either disables the composer.
        const bool sending =
            (app.sendPending && app.sendSessionId == openId) ||
            (app.streamActive && app.streamSessionId == openId);
        const bool hasText = !replyDraft.empty();
        const bool sendEnabled = canSend && hasText && !sending;

        auto bar = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(composerH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(8), .right = pixels(18),
                                      .bottom = pixels(8), .left = pixels(18)})
                .with_custom_background(theme::panel_bg())
                .with_roundness(0.0f)
                .with_debug_name("composer_bar"));

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
        float inputW = paneW - 36.0f - sendW - 8.0f;
        if (inputW < 120.0f) inputW = 120.0f;

        auto inputWrap = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(inputW), pixels(34)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.right = pixels(8)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_input_wrap"));

        afterhours::ui::imm::text_input(ctx, mk(inputWrap.ent(), 1), replyDraft,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(34)})
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_debug_name("composer_reply_input"));

        // Send affordance. Enabled (primary-styled, clickable) when the backend
        // supports replies and the draft has text; otherwise disabled-styled.
        auto send = button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(sending ? "\xe2\x80\xa6" : "Send")
                .with_size(ComponentSize{pixels(sendW), pixels(32)})
                .with_custom_background(sendEnabled ? theme::button_primary()
                                                    : theme::disabled_bg())
                .with_custom_hover_bg(sendEnabled ? theme::button_primary()
                                                  : theme::disabled_bg())
                .with_custom_text_color(sendEnabled ? theme::window_bg()
                                                    : theme::disabled_text())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("composer_send"));
        if (send && sendEnabled) {
            // Route through the STREAMING path when the backend supports it
            // (the mock does), so the reply fills in token-by-token; otherwise
            // fall back to the synchronous one-shot path (no regression). Both
            // are one-shot flags serviced by LoaderSystem; setting only one per
            // turn keeps them mutually exclusive.
            if (canStream)
                app.requestStreamPrompt = replyDraft;
            else
                app.requestSendPrompt = replyDraft;
            replyDraft.clear();
        }

        // Status caption (text_input has no placeholder support, gap #17). When
        // sending IS wired we drop the read-only note and either stay quiet
        // (draft empty -> a faint "Reply…" label) or show a "sending…" state.
        // The honest disabled caption only appears when the backend genuinely
        // can't reply.
        const char* caption = nullptr;
        if (!canSend) {
            caption =
                "read-only \xe2\x80\x94 this backend doesn't support replies";
        } else if (sending) {
            caption = "sending\xe2\x80\xa6";
        } else if (!hasText) {
            caption = "Reply\xe2\x80\xa6";
        }
        if (caption) {
            div(ctx, mk(bar.ent(), 3),
                ComponentConfig{}
                    .with_label(caption)
                    .with_size(ComponentSize{percent(1.0f), pixels(14)})
                    .with_margin(Margin{.top = pixels(4)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("composer_status"));
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
    static constexpr float kGlyphW = 6.2f;   // avg px per glyph @ Medium
    static constexpr float kLinePitch = 15.0f;  // px per wrapped line
    static int wrap_perline(float widthPx) {
        int p = static_cast<int>((widthPx - 10.0f) / kGlyphW);
        return p < 8 ? 8 : p;
    }

    static float estimate_height(const std::string& text, float widthPx) {
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
        if (lines < 1) lines = 1;
        return 20.0f + static_cast<float>(lines) * kLinePitch;
    }

    // Estimated WRAPPED line count of `text` at `widthPx` (same model as
    // estimate_height). Used to decide whether a body is long enough to fold.
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

    // Max bubble content width — caps the reading column so a conversational
    // message doesn't run edge-to-edge across the wide pane (v3 #8). ~620px is
    // roughly 70 characters at this font, the comfortable-reading target.
    static constexpr float kBubbleCap = 620.0f;

    // A conversational message (User / Assistant) as a CONTAINED bubble inside a
    // full-width row. The row aligns the bubble by role (user right, assistant
    // left) so the transcript reads as a two-sided conversation, not a stack of
    // full-width tinted bands. System / Tool messages take the quieter
    // metadata treatment (see render_meta_line / render_tool_block).
    void render_bubble(UIContext<InputAction>& ctx, Entity& parent, int index,
                       const api::Message& m, float paneWidth,
                       bool isLive = false,
                       AppComponent::StreamPhase streamPhase =
                           AppComponent::StreamPhase::Idle) {
        // System messages are conversation METADATA, not dialogue: render them
        // as a quiet centered caption, never a bubble.
        if (m.role == api::Role::System) {
            render_meta_line(ctx, parent, index, m);
            return;
        }
        // Tool messages are work-steps: a distinct, subtler contained block
        // (left-aligned, quieter than a conversational bubble).
        if (m.role == api::Role::Tool) {
            render_tool_block(ctx, parent, index, m, paneWidth);
            return;
        }

        const bool isUser = (m.role == api::Role::User);

        // The visible body text. While a reply is streaming into THIS assistant
        // turn, show a subtle live affordance: a "thinking…" placeholder before
        // the first token lands, then the partial text followed by a block caret
        // so it reads as an active, filling reply. The underlying api::Message is
        // untouched — this is a display-only decoration. Secrets are redacted at
        // render time (never shoulder-surf a pasted key/JWT).
        std::string body = strip_inline_md(redact_secrets(m.text));
        if (isLive) {
            if (body.empty() ||
                streamPhase == AppComponent::StreamPhase::Thinking) {
                body = "thinking\xe2\x80\xa6";
            } else {
                body += " \xe2\x96\x8b";  // trailing block caret (U+258B).
            }
        }

        // ---- DOC-FEED layout (critique verdict) -------------------------------
        // The assistant answer is long-form (prose + code + tool traces) so it
        // renders as a FULL-COLUMN document turn — an author row (colored name +
        // timestamp) then the body flowing the full reading column, NOT trapped
        // in a bubble (a bubble balloons to ~85% and floats text in dead padding
        // — critique #7/#15/#16). The short USER prompt keeps a compact
        // right-aligned tinted bubble (iMessage-style) so authorship is instantly
        // clear without fragmenting the turn.
        std::string label = isUser ? "You" : "hanabi";
        if (!m.subtitle.empty()) label += "  \xc2\xb7  " + m.subtitle;
        if (isLive) {
            label += "  \xc2\xb7  streaming\xe2\x80\xa6";
        } else {
            std::string age = fmtutil::relative_time(m.created_at);
            if (!age.empty()) label += "   " + age;
        }

        if (isUser) {
            // Right-aligned compact bubble. Cap width so a short prompt doesn't
            // stretch; size height to content.
            float bubbleW = paneWidth * 0.82f;
            if (bubbleW > 520.0f) bubbleW = 520.0f;
            float h = estimate_height(body, bubbleW - 28.0f);
            auto row = div(ctx, mk(parent, 200 + index * 10),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_justify_content(JustifyContent::FlexEnd)
                    .with_margin(Margin{.top = pixels(10), .right = pixels(0),
                                        .bottom = pixels(6), .left = pixels(0)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("user_row"));
            auto bub = div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(bubbleW), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_custom_background(bubble_bg(m.role))
                    .with_border(theme::border(), pixels(1.0f))
                    .with_padding(Padding{.top = pixels(7), .right = pixels(14),
                                          .bottom = pixels(8), .left = pixels(14)})
                    .with_roundness(0.42f)
                    .with_debug_name("user_bubble"));
            div(ctx, mk(bub.ent(), 1),
                ComponentConfig{}
                    .with_label(label)
                    .with_size(ComponentSize{percent(1.0f), pixels(15)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::role_user())
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("user_who"));
            div(ctx, mk(bub.ent(), 2),
                ComponentConfig{}
                    .with_label(body)
                    .with_size(ComponentSize{percent(1.0f), pixels(h - 22.0f)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(FontSize::Medium)
                    .with_text_overflow(TextOverflow::Wrap)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("user_text"));
            return;
        }

        // Assistant: full-column document turn (no bubble container).
        float textW = paneWidth - 34.0f;  // author-column inset both sides
        // FOLD a very long body: cap the rendered text at kFoldLines and offer a
        // "Show N more lines" toggle (critique #58 + Gabe's "long messages blow
        // up RAM" — a folded body renders ~kFoldLines of glyphs instead of
        // thousands, so a giant pasted log/diff can't balloon the vertex count).
        // Never fold the live-streaming message (it's actively growing). Keyed
        // by message id in AppComponent::expandedMsgs (default folded).
        constexpr int kFoldLines = 40;
        const int lineCount = count_lines(body, textW);
        AppComponent* app = app_singleton();
        const std::string mkey = m.id.empty()
                                     ? ("msg" + std::to_string(index))
                                     : m.id;
        const bool expanded = app && app->expandedMsgs.count(mkey) != 0;
        const bool foldable = !isLive && lineCount > kFoldLines && !expanded;
        std::string shown = foldable
                                ? first_n_lines(body, textW, kFoldLines)
                                : body;
        float h = estimate_height(shown, textW);
        auto turn = div(ctx, mk(parent, 200 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(12), .right = pixels(0),
                                    .bottom = pixels(6), .left = pixels(0)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("asst_turn"));
        // Author row: colored "hanabi" name + timestamp, ranked above the body.
        div(ctx, mk(turn.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{percent(1.0f), pixels(16)})
                .with_margin(Margin{.bottom = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::role_assistant())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("asst_who"));
        div(ctx, mk(turn.ent(), 2),
            ComponentConfig{}
                .with_label(shown)
                .with_size(ComponentSize{percent(1.0f), pixels(h - 18.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("asst_text"));
        // Fold toggle: shown when the body is long. Collapsed = "Show N more
        // lines"; expanded = "Show less". Clicking toggles expandedMsgs[mkey].
        if (app && !isLive && (foldable || (expanded && lineCount > kFoldLines))) {
            const int hidden = lineCount - kFoldLines;
            std::string flabel = expanded
                                     ? "Show less"
                                     : ("Show " + std::to_string(hidden) +
                                        " more lines");
            auto fbtn = div(ctx, mk(turn.ent(), 3),
                ComponentConfig{}
                    .with_label(flabel)
                    .with_size(ComponentSize{children(), pixels(24)})
                    .with_margin(Margin{.top = pixels(4)})
                    .with_padding(Padding{.top = pixels(3), .right = pixels(11),
                                          .bottom = pixels(3), .left = pixels(11)})
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

    // A collapsed PILE of >=2 consecutive tool-role messages: one summary row
    // ("N tool calls · first, second…") that expands to the individual tool
    // blocks on click (navi-website pattern). Default collapsed — keeps a
    // tool-heavy transcript scannable and bounds render cost. `keyIndex` is the
    // pile's stable id source; [lo,hi) is the run within `msgs`.
    void tool_pile(UIContext<InputAction>& ctx, Entity& parent, int keyIndex,
                   const std::vector<api::Message>& msgs, int lo, int hi,
                   float paneWidth) {
        const int count = hi - lo;
        const std::string key = msgs[lo].id.empty()
                                    ? ("pile" + std::to_string(keyIndex))
                                    : msgs[lo].id;
        AppComponent* app = app_singleton();
        const bool open = app && app->expandedPiles.count(key) != 0;

        // A short summary from the tool subtitles (or a generic "step").
        auto short_of = [](const api::Message& m) {
            std::string s = m.subtitle.empty() ? std::string("step") : m.subtitle;
            return s;
        };
        std::string names = short_of(msgs[lo]);
        if (count >= 2) names += ", " + short_of(msgs[lo + 1]);
        if (count > 2) names += "\xe2\x80\xa6";  // ellipsis

        float blockW = paneWidth - 60.0f;
        if (blockW < 160.0f) blockW = 160.0f;
        if (blockW > kBubbleCap) blockW = kBubbleCap;

        auto wrap = div(ctx, mk(parent, 260 + keyIndex * 10),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(blockW), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(6), .right = pixels(0),
                                    .bottom = pixels(6), .left = pixels(0)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("tool_pile"));

        // Summary row (the interactive header).
        auto head = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(30)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(0), .right = pixels(11),
                                      .bottom = pixels(0), .left = pixels(11)})
                .with_custom_background(
                    theme::over(theme::accent_soft(), theme::panel_bg()))
                .with_custom_hover_bg(theme::hover_over(
                    theme::over(theme::accent_soft(), theme::panel_bg())))
                .with_border(theme::border(), pixels(1.0f))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("pile_head"));
        head.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (app && head.ent().get<afterhours::ui::HasClickListener>().down) {
            if (open) app->expandedPiles.erase(key);
            else app->expandedPiles.insert(key);
        }
        // chevron (rotates via the glyph choice) + label + count badge
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(open ? "\xe2\x96\xbe" : "\xe2\x96\xb8")  // ▾ / ▸
                .with_size(ComponentSize{pixels(14), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::role_tool())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("pile_chev"));
        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(std::to_string(count) + " tool calls  \xc2\xb7  " +
                            names)
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_margin(Margin{.left = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("pile_label"));

        // Expanded: render each tool block below the summary.
        if (open) {
            for (int k = lo; k < hi; ++k)
                render_tool_block(ctx, wrap.ent(), 1000 + k, msgs[k], paneWidth);
        }
    }

    // A Tool message: a distinct, subtler contained block (left-aligned, a
    // faint accent-over-panel tint, muted body) so tool activity stays visually
    // separate from the conversational bubbles but doesn't shout like one.
    void render_tool_block(UIContext<InputAction>& ctx, Entity& parent,
                           int index, const api::Message& m, float paneWidth) {
        float avail = paneWidth - 60.0f;
        if (avail < 160.0f) avail = 160.0f;
        float blockW = avail < kBubbleCap ? avail : kBubbleCap;
        float textW = blockW - 28.0f;
        float h = estimate_height(m.text, textW);

        auto row = div(ctx, mk(parent, 200 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_justify_content(JustifyContent::FlexStart)
                .with_margin(Margin{.top = pixels(3), .right = pixels(0),
                                    .bottom = pixels(5), .left = pixels(0)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("tool_row"));

        auto block = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(blockW), pixels(h)})
                .with_custom_background(
                    theme::over(theme::accent_soft(), theme::panel_bg()))
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(6), .right = pixels(14),
                                      .bottom = pixels(8), .left = pixels(14)})
                .with_roundness(0.3f)
                .with_debug_name("tool_block"));

        std::string label = role_label(m.role);
        if (!m.subtitle.empty()) label += "  \xc2\xb7  " + m.subtitle;
        std::string age = fmtutil::relative_time(m.created_at);
        if (!age.empty()) label += "   " + age;
        div(ctx, mk(block.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{percent(1.0f), pixels(15)})
                .with_transparent_bg()
                .with_custom_text_color(theme::role_tool())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("tool_role"));
        div(ctx, mk(block.ent(), 2),
            ComponentConfig{}
                .with_label(strip_inline_md(redact_secrets(m.text)))
                .with_size(ComponentSize{percent(1.0f), pixels(h - 26.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(FontSize::Medium)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("tool_text"));
    }
};

}  // namespace ecs
