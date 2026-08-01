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
    template <typename Pred>
    void render_digest(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, const std::string& title,
                       float paneW, float paneH, Pred pred) {
        std::vector<const api::SessionSummary*> rows;
        for (const auto& s : app.sessions)
            if (pred(s)) rows.push_back(&s);

        header(ctx, parent, title, std::to_string(rows.size()), theme::type::H1);

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
        const float cardW = wrap_width(paneW);
        for (const auto* s : rows) digest_card(ctx, wrap, ++i, *s, app, false, cardW);
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
        size_t i = 0;
        while (i < in.size() && (in[i] == ' ' || in[i] == '\t')) ++i;
        if (in.compare(i, 3, "[P]") == 0) {
            i += 3;
            while (i < in.size() && (in[i] == ' ' || in[i] == '\t')) ++i;
            return in.substr(i);
        }
        return in;
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
                     bool emphasizeMeta = false, float cardWidthPx = 0.0f) {
        // The card is a raised surface (panel_bg_2, one step ELEVATED above the
        // pane's panel_bg) plus a hairline border so it reads as floating above
        // the pane in BOTH modes — in light the border is what sells the lift
        // (panel_bg_2 is a hair darker than the white pane, so fill alone would
        // read recessed). Consistent 14/16 padding keeps text off the edges.
        auto card = div(ctx, mk(parent, 100 + id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(60)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_margin(Margin{.top = pixels(4), .right = pixels(0),
                                    .bottom = pixels(8), .left = pixels(0)})
                .with_padding(Padding{.top = pixels(11), .right = pixels(16),
                                      .bottom = pixels(11), .left = pixels(16)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_hover_bg(theme::hover_bg())
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
        const bool hasTag = has_chip(s);
        const float titleFrac = hasTag ? 0.78f : 1.0f;
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
        if (has_chip(s)) {
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
        }

        // Subtitle / preview. On the mock this is the rich preview snippet; on
        // a real backend (no preview) card_meta() composes a useful line —
        // relative age + a state/status hint — so real cards aren't identical
        // bare "active" slabs (defects #3/#16). Actionable rows get slightly
        // more contrast (text_primary vs the passive text_secondary).
        div(ctx, mk(card.ent(), 2),
            ComponentConfig{}
                .with_label(card_meta(s))
                .with_size(ComponentSize{percent(1.0f), pixels(16)})
                .with_margin(Margin{.top = pixels(3), .right = pixels(0),
                                    .bottom = pixels(0), .left = pixels(0)})
                .with_transparent_bg()
                .with_custom_text_color(emphasizeMeta ? theme::text_primary()
                                                      : theme::text_secondary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("dc_sub"));
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
                          first);
            first = false;
            // Actionable rows: emphasize the "waiting on you \xc2\xb7 8m" metadata.
            for (const auto* s : waiting)
                digest_card(ctx, wrap, ++shown, *s, app, true, cardW);
        }
        if (!finished.empty()) {
            section_label(ctx, wrap, 900,
                          "Finished since you looked \xc2\xb7 " +
                              std::to_string(finished.size()),
                          first);
            first = false;
            for (const auto* s : finished)
                digest_card(ctx, wrap, ++shown, *s, app, false, cardW);
        }
        // Self-running work: a real section with real cards (title + relative
        // age), headed "SELF-RUNNING (N)" like the mock. Rendering the actual
        // running threads (not a lone caption) kills the old orphaned-caption
        // void (defect #14) — the count now sits ON a populated section.
        if (!selfRunning.empty()) {
            section_label(ctx, wrap, 1800,
                          "Self-running \xc2\xb7 " +
                              std::to_string(selfRunning.size()),
                          first);
            first = false;
            for (const auto* s : selfRunning)
                digest_card(ctx, wrap, ++shown, *s, app, false, cardW);
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
                              bool first = false) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(upper(text))
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_margin(Margin{.top = pixels(first ? 4 : 20),
                                    .right = pixels(0), .bottom = pixels(6),
                                    .left = pixels(2)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
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
            std::string t = normalize_title(app.openSession->summary.title);
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
        // fill) and a persistent composer row pinned to the bottom (v3 #30 —
        // kills the empty dark band). Header is 46; the composer is a fixed
        // strip; the scroll takes whatever's left.
        constexpr float kComposerH = 74.0f;
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
                preset::EmptyStateText("No messages yet â "
                                       "start the conversation below.")
                    .with_size(ComponentSize{percent(1.0f), pixels(40)})
                    .with_padding(Padding{.top = pixels(28), .right = pixels(18),
                                          .bottom = pixels(8),
                                          .left = pixels(18)})
                    .with_alignment(TextAlignment::Center)
                    .with_debug_name("transcript_empty"));
        } else {
            // Sub-agent panel sits above the messages when the thread has steps.
            sub_agent_panel(ctx, scroll.ent(), app);

            int i = 0;
            for (const auto& m : app.openSession->messages)
                render_bubble(ctx, scroll.ent(), i++, m, paneW);
        }

        render_composer(ctx, parent, paneW, kComposerH);
    }

    // Persistent composer pinned to the bottom of the transcript pane. A real,
    // functional afterhours text_input + a Send affordance.
    //
    // WIRING (v3 #30 + REPORT): the api::Client interface (src/api/client.h)
    // exposes list_sessions / get_session / create_session(prompt) /
    // backend_label — there is NO method to CONTINUE an already-open thread
    // (no send_message(session_id, prompt) / reply). So Send on an open
    // transcript has no backend to call. Rather than fake it, the composer is
    // rendered as a REAL, VISUALLY-COMPLETE input that you can type into
    // (bound to a persistent draft), with a clear READ-ONLY affordance: the
    // Send button is disabled-styled and a caption notes replies aren't wired
    // yet. A real reply needs a Client::send_message(const std::string& id,
    // const std::string& prompt) method + a loader path that appends the
    // returned message(s) to app.openSession->messages (see REPORT).
    void render_composer(UIContext<InputAction>& ctx, Entity& parent,
                         float paneW, float composerH) {
        // Function-local persistent draft (kept out of components.h, which is
        // owned by another agent). One transcript pane -> one instance -> safe.
        static std::string replyDraft;

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

        // Send affordance. No continue-session backend method exists, so this
        // is disabled-styled (read-only). It still renders as a complete,
        // polished button so the composer looks real, not a placeholder.
        button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label("Send")
                .with_size(ComponentSize{pixels(sendW), pixels(32)})
                .with_custom_background(theme::disabled_bg())
                .with_custom_hover_bg(theme::disabled_bg())
                .with_custom_text_color(theme::disabled_text())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.35f)
                .with_debug_name("composer_send"));

        // Read-only status caption: the text_input has no placeholder support
        // (gap #17), so this faint line both labels the field ("Reply...") and
        // states the honest limitation (no continue-session backend yet).
        div(ctx, mk(bar.ent(), 3),
            ComponentConfig{}
                .with_label(replyDraft.empty()
                                ? "Reply\xe2\x80\xa6  \xc2\xb7  read-only preview "
                                  "(replies aren't wired to the backend yet)"
                                : "read-only preview \xe2\x80\x94 replies aren't "
                                  "wired to the backend yet")
                .with_size(ComponentSize{percent(1.0f), pixels(14)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_status"));
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
                       const api::Message& m, float paneWidth) {
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

        // Usable content column, capped at kBubbleCap for readability.
        float avail = paneWidth - 60.0f;
        if (avail < 160.0f) avail = 160.0f;
        float bubbleW = avail < kBubbleCap ? avail : kBubbleCap;
        float textW = bubbleW - 28.0f;
        float h = estimate_height(m.text, textW);

        // Full-width row: justify FlexEnd (user, right) or FlexStart
        // (assistant, left). This is what turns a stack of bands into a
        // two-sided conversation.
        auto row = div(ctx, mk(parent, 200 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_justify_content(isUser ? JustifyContent::FlexEnd
                                             : JustifyContent::FlexStart)
                .with_margin(Margin{.top = pixels(3), .right = pixels(0),
                                    .bottom = pixels(5), .left = pixels(0)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("bubble_row"));

        // The bubble: a contained card with padding, a subtle role-tinted
        // surface, and a hairline border so it reads as a floating chip in both
        // themes (the border sells the lift in light mode).
        auto bubble = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(bubbleW), pixels(h)})
                .with_custom_background(bubble_bg(m.role))
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(7), .right = pixels(14),
                                      .bottom = pixels(9), .left = pixels(14)})
                .with_roundness(0.4f)
                .with_debug_name("bubble"));

        // Sender line: role + optional subtitle + relative age. User rows put
        // the label right-aligned so it tracks the bubble's trailing edge.
        std::string label = role_label(m.role);
        if (!m.subtitle.empty()) label += "  \xc2\xb7  " + m.subtitle;
        std::string age = fmtutil::relative_time(m.created_at);
        if (!age.empty()) label += "   " + age;
        div(ctx, mk(bubble.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{percent(1.0f), pixels(15)})
                .with_transparent_bg()
                .with_custom_text_color(role_color(m.role))
                .with_font_size(FontSize::Small)
                .with_alignment(isUser ? TextAlignment::Right
                                       : TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("bubble_role"));

        div(ctx, mk(bubble.ent(), 2),
            ComponentConfig{}
                .with_label(m.text)
                .with_size(ComponentSize{percent(1.0f), pixels(h - 26.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_text_overflow(TextOverflow::Wrap)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("bubble_text"));
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
                .with_label(m.text)
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
