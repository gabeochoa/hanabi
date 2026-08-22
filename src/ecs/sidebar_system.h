#pragma once

// Renders the single collapsible sidebar.
//
//   Unfolded (full): header (brand + New task + Settings + collapse) → search
//     → smart-view list with counts → folders → recent → a low-signal
//     collapsed Archived section.
//   Folded (thin rail): icon-only smart views + a collapse/expand toggle in
//     the header. A blocked-count badge sits on the rail.
//
// Clicking a smart view swaps the main pane (SmartView). Clicking a thread row
// requests that thread be opened in a tab (handled by TabBarSystem/Loader).
// The collapse toggle flips layout.sidebarCollapsed; Cmd+B does the same.

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include "../test_hooks.h"
#include "../settings.h"
#include "../util/format.h"
#include "../ui/icons.h"
#include "../../vendor/afterhours/src/plugins/ui/text_input/text_input.h"
#include "thread_model.h"
#include "ui_imports.h"

namespace ecs {

struct SidebarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

        // The immediate-mode text_input forces its field background to the UI
        // theme's Secondary color and its text to theme.font (gap #17), ignoring
        // per-widget colors. Point those at hanabi tokens so the search field's
        // inner surface blends into its pill (panel_bg_2) instead of rendering
        // as a jarring default dark-blue box, and its text uses our palette.
        ctx.theme.secondary = theme::panel_bg_2();
        ctx.theme.surface = theme::panel_bg_2();
        ctx.theme.font = theme::text_primary();
        ctx.theme.font_muted = theme::text_faint();
        ctx.theme.focus = theme::accent();

        // Apply a pending star-toggle request (set by a row's star affordance).
        // The mutation lives HERE so this owned system is the single writer of
        // the sessions vector's starred flag — flipping it updates the Starred
        // smart-view count + membership immediately, on the very next render.
        if (!app->requestToggleStar.empty()) {
            for (auto& s : app->sessions) {
                if (s.id == app->requestToggleStar) {
                    s.starred = !s.starred;
                    // Persist so the star survives relaunch (Settings is the
                    // durable source of truth; the loader re-applies it to
                    // freshly-fetched sessions on the next launch).
                    Settings::get().set_starred(s.id, s.starred);
                    break;
                }
            }
            app->requestToggleStar.clear();
        }

        // Cmd+B toggles the sidebar.
        bool cmdDown = afterhours::graphics::is_key_down(343) ||
                       afterhours::graphics::is_key_down(347);
        if (cmdDown && afterhours::graphics::is_key_pressed(66)) {  // B
            layout->sidebarCollapsed = !layout->sidebarCollapsed;
            Settings::get().set_sidebar_collapsed(layout->sidebarCollapsed);
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto& r = layout->sidebar;
        bool folded = layout->sidebarCollapsed;

        auto panel = div(ctx, mk(uiRoot, 1000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::sidebar_bg())
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_roundness(0.0f)
                .with_render_layer(1)
                .with_debug_name("sidebar"));

        render_header(ctx, panel.ent(), *layout, folded);
        if (!folded) render_search(ctx, panel.ent(), *app, r.x, r.y, r.width);
        render_smart_views(ctx, panel.ent(), *app, folded, r.width);

        if (folded) return;  // rail stops after icon views

        // Scrollable region: folders + recent + archived.
        // header(40) + search(40) + VIEWS label(25) + views block(~162, now
        // children()-sized). Keep in rough sync with the VIEWS block so the
        // scroll region is sized right; a few px off just changes scroll extent.
        float used = 40.0f + 40.0f + 25.0f + 162.0f;
        float scrollH = r.height - used;
        if (scrollH < 40.0f) scrollH = 40.0f;
        auto scroll = div(ctx, mk(panel.ent(), 5),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(scrollH)})
                // preset::ScrollPanel is a FlexDirection::Column but leaves
                // flex_wrap at its Wrap default. The sidebar puts MANY direct
                // children in this panel (folder heads + 90+ chat rows); once
                // their stacked height exceeds the viewport, a WRAPPING column
                // wraps the overflow into a SECOND column at x += column-width
                // — i.e. off the right edge of the sidebar, where the scroll
                // viewport's scissor clips it away. The visible result: only
                // the first viewport-height of rows ever draws; every row past
                // content-Y ≈ viewport height renders (and lays out) in a
                // clipped-out second column, so at a non-zero scroll offset the
                // list looks empty even though the rows exist + stay clickable
                // (their hit rects use the same wrapped positions). Forcing
                // NoWrap keeps every child stacked in ONE column so the scroll
                // offset simply slides the whole list. (The main-pane
                // ScrollPanels don't hit this because they hold a SINGLE
                // content child; only this many-direct-children panel wraps.)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_custom_background(theme::sidebar_bg())
                .with_debug_name("sidebar_scroll"));
        // Match the OS "natural scrolling" setting (see util/scroll_prefs.h).
        hanabi::apply_scroll_prefs(scroll.ent());
        // TEMPORARY scroll indicator (afterhours gap #26): thin overlay bar
        // computed from the panel's live HasScrollView metrics.
        hanabi::attach_scroll_indicator(scroll.ent());

        // "FOLDERS" section label + fold-all control (mirrors the mock's
        // second section header, which carries a fold-all affordance).
        folders_section_head(ctx, scroll.ent(), 4, *app, r.width);

        // Live search filter: when the query is non-empty, folders only render
        // matching rows and empty folders are hidden. Track whether ANY row
        // survived so we can show a no-results empty state.
        const std::string q = lower(app->searchQuery);
        int shown = 0;
        // Each folder is given a widely-spaced base id (1000 apart). The row
        // ids inside a folder are base + 1 + rowIndex, so a folder can hold up
        // to ~999 rows before it would collide with the next folder's base.
        // A real backend routes ~everything through the Recent catch-all, so
        // Recent alone can carry 100+ rows — the old 10/20/30/40/50 spacing
        // (room for only ~9 rows each) overflowed into the next folder's id
        // range and tripped afterhours' entity-id-conflict guard.
        // REAL folders from the API: group by the distinct, non-empty folder
        // values the backend actually returned (s.folder = the session's
        // workspace). No hardcoded/fake folders — a folder appears only if a
        // real session is filed under it. Sorted for stable ordering; each gets
        // a widely-spaced id base (1000 apart) so its rows never collide with
        // the next folder's id range.
        std::vector<std::string> folders = distinct_folders(*app);
        // Folders start COLLAPSED by default (Gabe: subthreads hidden until you
        // open a folder). Seed every folder key into collapsedFolders ONCE, the
        // first render that actually has folders; afterwards the user's own
        // expand/collapse choices stand for the session.
        if (!app->foldersDefaultCollapsedSeeded && !folders.empty()) {
            for (const auto& fname : folders) app->collapsedFolders.insert(fname);
            app->foldersDefaultCollapsedSeeded = true;
        }
        int fbase = 1000;
        for (const auto& fname : folders) {
            shown += render_folder(ctx, scroll.ent(), fbase,
                                   display_folder_name(fname), fname,
                                   *app, q, r.width);
            fbase += 1000;
        }
        // Unfoldered sessions (folder=="" — ~all of them on the real backend
        // today) render as a HEADERLESS flat list right below the real folders.
        // No invented "Recent" folder — per Gabe, only real API folders get a
        // header. (If the backend later files every session under a workspace,
        // this list is simply empty and only real folders show.)
        //
        // V6: fill the available vertical space instead of always capping at a
        // fixed ~12 rows (which left empty space below the "Show N more…"
        // button in a tall window). The scroll viewport is scrollH px tall and
        // each thread row is kRowHeight px. We want the list to reach the
        // bottom of the visible area AND leave the "Show N more…" button
        // VISIBLE at the bottom of the viewport — not pushed below the fold
        // (M2: filling the FULL viewport hid the show-more). So the cap targets
        // the viewport MINUS the space already taken by folders rendered above
        // (shown-so-far, in rows) MINUS one row reserved for the show-more
        // button itself. This keeps the panel filled without shoving the
        // show-more off-screen. Never drops below kBucketCap.
        int viewportRows = static_cast<int>(scrollH / kRowHeight);
        int rowsUsedAbove = static_cast<int>(shown / kRowHeight);
        int fillCap = viewportRows - rowsUsedAbove - 1;  // -1 = show-more row
        if (fillCap < kBucketCap) fillCap = kBucketCap;
        shown += render_folder(ctx, scroll.ent(), 900000, "", "recent",
                               *app, q, r.width, /*archived=*/false,
                               /*catchAll=*/true, /*headerless=*/true,
                               /*cap=*/fillCap);
        // (Archived is now a smart VIEW in the Views section above, not a
        // sidebar folder â per Gabe. Sending a message to an archived thread
        // unarchives it, same as the backend behavior.)

        // No-results empty state (only meaningful with a non-empty query).
        if (!q.empty() && shown == 0) {
            div(ctx, mk(scroll.ent(), 900),
                ComponentConfig{}
                    .with_label("No matches for \"" + app->searchQuery + "\"")
                    .with_size(ComponentSize{percent(1.0f), pixels(28)})
                    .with_padding(Padding{.top = pixels(8), .right = pixels(14),
                                          .bottom = pixels(4),
                                          .left = pixels(14)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("sb_no_results"));
        }
    }

  private:
    // ---- shared count-column geometry (gap #18: afterhours has no flex-grow)
    // The smart-view rows, folder headers, and time-group headers all show a
    // right-aligned count. To land every count at the SAME right-edge x, we
    // reserve one count-column width + one right inset consistently and size
    // the preceding label column in PIXELS (label = panelW − left − reserved),
    // so the count box always starts at the same x regardless of section. This
    // is the best we can do without flex-grow, and it makes the three count
    // families flush to a single edge.
    static constexpr float kCountColW = 30.0f;   // count box width
    static constexpr float kCountRightPad = 12.0f;  // inset from panel right
    // Folded (rail) icon column: one left inset + one slot width shared by the
    // header collapse toggle AND every smart-view icon, so glyphs (which
    // draw_fg centers in their slot) line up on ONE flush-left vertical line.
    // Inset ~ the unfolded content's left padding; slot = the toggle btn box.
    static constexpr float kRailIconInset = 14.0f;
    static constexpr float kRailIconSlot = 28.0f;
    // Defect #2: cap the rows shown in an expanded group at kBucketCap; beyond
    // that a "Show N more…" expander row is rendered instead of the full wall,
    // so no single time-bucket / folder dumps a 90-row scroll-pit on first
    // open. 12 fills a typical viewport-worth of rows while staying scannable.
    // The expander row is given a fixed id offset near the TOP of a group's
    // id slot so it never collides with a capped body row (base+1..base+12).
    static constexpr int kBucketCap = 12;
    static constexpr int kMoreRowIdOffset = 199;
    // The fixed on-screen height of one chat row (see render_chat_row's
    // .with_size height). Used by V6's fill-the-viewport cap computation so
    // the headerless catch-all shows enough rows to fill the scroll area.
    static constexpr float kRowHeight = 24.0f;
    // Per-row trailing relative-time column width ("2h" / "Jul 28"). Wide
    // enough for a short absolute date so old rows aren't clipped; kept small
    // so the title still gets most of the row.
    static constexpr float kRowTimeColW = 46.0f;
    // A thread row's left inset (padding-left) and its leading status-glyph
    // slot width. The title text therefore starts at kRowLeftInset + kGlyphW
    // from the row's left edge — the "Show N more…" expander matches that so
    // its label aligns with the thread titles (V7), not the row edge.
    // 16px = the VIEWS rows' icon left edge (container 8 + smart_item pad 8),
    // so FOLDER rows line up with the VIEWS rows above them (M4).
    static constexpr float kRowLeftInset = 16.0f;
    static constexpr float kGlyphW = 12.0f;   // leading status glyph slot
    // A count's LEFT edge (== its column start x) is the same for every
    // section: panelW − kCountRightPad − kCountColW. Given a section's own
    // left inset, the label column width is that start-x minus the left inset
    // (minus any fixed leading slot such as a chevron/icon).
    static float label_col_w(float panelW, float leftInset, float leadSlot) {
        float w = panelW - kCountRightPad - kCountColW - leftInset - leadSlot;
        if (w < 30.0f) w = 30.0f;
        return w;
    }

    // ---- text helpers ----


    // Compact right-aligned per-row timestamp derived from `updated_at`
    // (relative to `now`). Recent rows read as a relative age ("now","5m",
    // "3h","2d","4w"); anything older than the finest week bucket reads as an
    // absolute short date ("Jul 28") so a year-old row isn't a giant "58w".
    // Returns "" for an unknown (0) or future timestamp so the slot stays
    // blank rather than lying. Pure + now-injected for headless testing.
    static std::string row_time_label(int64_t updated_at, int64_t now) {
        if (updated_at <= 0 || updated_at > now) return "";
        // Sub-week ages use the ONE canonical relative-time ladder (shared with
        // the transcript header etc.) so they can never drift; only the
        // older-than-a-week ABSOLUTE-date tail is bespoke to the sidebar.
        if (now - updated_at < 7 * fmtutil::kDaySecs)
            return fmtutil::relative_time(updated_at, now);
        // Older than a week: absolute short date ("Jul 28"), and append the
        // year when it differs from now's year so an old row is unambiguous.
        std::time_t ut = static_cast<std::time_t>(updated_at);
        std::time_t nt = static_cast<std::time_t>(now);
        std::tm utm{};
        std::tm ntm{};
        localtime_r(&ut, &utm);
        localtime_r(&nt, &ntm);
        char buf[24];
        if (utm.tm_year == ntm.tm_year)
            std::strftime(buf, sizeof(buf), "%b %-d", &utm);
        else
            std::strftime(buf, sizeof(buf), "%b %-d %Y", &utm);
        return std::string(buf);
    }

    // ---- display-only title normalization (defect #10: "[P]" prefix leak) --
    // Real + mock rows use the app's title convention where a leading "[P] "
    // (parked / needs-you) drives the row's RED attention triangle via the
    // state model (http_client.cpp derive_state keys the parked state off this
    // exact prefix; the mock hands the same shape). Once that glyph is drawn,
    // the literal "[P] " in the title text is redundant noise sitting right
    // next to the shape that already means the same thing. So for DISPLAY only
    // we strip a single leading "[P] " (or bare "[P]"). This never mutates the
    // underlying SessionSummary — the state/tag/glyph derivation still sees the
    // original title, so the attention triangle is unchanged; we only clean the
    // string handed to the row's title label. Conservative: strips exactly one
    // leading occurrence, case-sensitive, so it can't chew into real titles.
    static std::string strip_parked_prefix(const std::string& title) {
        return fmtutil::display_title(title);  // shared canonical impl
    }

    // ---- automated / scheduled row detection (defect #5: cron noise) -------
    // A real backend mixes human conversations with scheduled/cron sessions
    // ("Schedule: nightly backfill", "kicker-tick", "continuous-triage-tick").
    // Those aren't real conversations but they inflate the buckets and read as
    // peers of human threads. We DON'T hide them (the user may want them) — we
    // just de-emphasize them (dimmer title + a small "gear-ish" automated glyph
    // in the status slot) so the eye skips them when scanning for real work.
    //
    // Heuristic (kept deliberately small + conservative so it can't over-match
    // a real conversation title): a session is "automated" if its title starts
    // with "Schedule:" OR ends with "-tick". These are structural naming
    // conventions of scheduled jobs, not content words, so a human thread is
    // very unlikely to trip them. No company/endpoint/product strings — purely
    // shape-based. If this ever over-matches, tighten (don't broaden) the list.
    static bool is_automated(const std::string& title) {
        if (title.rfind("Schedule:", 0) == 0) return true;  // "Schedule: ..."
        // ends-with "-tick"
        const std::string suf = "-tick";
        if (title.size() >= suf.size() &&
            title.compare(title.size() - suf.size(), suf.size(), suf) == 0)
            return true;
        return false;
    }

    // ASCII-lowercase a copy (search is case-insensitive; titles are UTF-8 but
    // case-folding only the ASCII range is sufficient for these labels).
    static std::string lower(const std::string& s) {
        return fmtutil::to_lower(s);
    }
    // Does `haystack` (already lowercased) contain the lowercased `needle`?
    static bool title_matches(const std::string& title, const std::string& q) {
        if (q.empty()) return true;
        return lower(title).find(q) != std::string::npos;
    }

    // Draw a small folder chevron: pointing DOWN when expanded, RIGHT when
    // collapsed (mirrors the mock, whose .chev rotates -90deg on .collapsed).
    // Drawn as a filled triangle so it stays crisp without needing a separate
    // rotated atlas cell (the atlas has no chevron-right).
    static void draw_chevron(RectangleType rect, bool collapsed,
                             theme::Color c) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        const float s = 3.6f;  // half-extent
        if (collapsed) {
            // Right-pointing: apex on the right, base on the left.
            afterhours::draw_triangle(afterhours::vec2{cx - s, cy - s},
                                      afterhours::vec2{cx - s, cy + s},
                                      afterhours::vec2{cx + s, cy}, c);
        } else {
            // Down-pointing: apex at bottom, base along the top.
            afterhours::draw_triangle(afterhours::vec2{cx - s, cy - s},
                                      afterhours::vec2{cx + s, cy - s},
                                      afterhours::vec2{cx, cy + s}, c);
        }
    }

    // ---- attention model helpers ----
    // Delegate the pure classification to the graphics-free, headlessly-tested
    // ecs::model layer so the tested logic IS the shipped logic.
    static bool is_attention(api::ThreadState s) {
        return ecs::model::is_attention(s);
    }
    static int blocked_count(const AppComponent& app) {
        int n = 0;
        for (const auto& s : app.sessions)
            if (s.tag == api::ThreadTag::Blocked) ++n;
        return n;
    }

    // ---- status glyph (shape-per-status) ----
    // The compact sidebar rows no longer carry a text tag chip. Instead EVERY
    // row gets a leading indicator slot of the SAME size, so no row looks
    // "unlabeled". Attention-worthy rows get a SHAPE-per-status glyph (status
    // readable by SHAPE, not color alone, mirroring the mock); calm rows get a
    // small neutral resting dot rather than a blank slot:
    //   Blocked / needs-you -> RED UP-TRIANGLE  (most urgent)
    //   Review (agent-verified) -> GREEN DIAMOND (square rotated 45 deg)
    //   Done -> BLUE DOT (filled circle)
    //   Running / in-progress -> HOLLOW RING (accent) — self-running, quiet
    //   parked / archived / calm -> small FAINT neutral dot (calm)
    //
    // NOTE (defect #9 — glyph vocabulary parity on real data): the pure
    // ecs::model::glyph_for maps only the tag/attention families (Blocked ->
    // Triangle, Review -> Diamond, Done -> Dot) and returns None for
    // everything else — including ThreadState::Running. On the MOCK, running
    // rows also carry a tag so they still glyph; but on the REAL backend the
    // digest-derivation (http_client.cpp derive_state) produces plain
    // Running/Attention/Done states with NO tag, so a real Running session
    // fell through to None and rendered as the same faint calm dot as an
    // Unknown row. That collapsed real data to ~2 visible glyphs (triangle +
    // grey square) while the mock showed 4. We fix the mapping HERE (in the
    // owned sidebar file) by widening the sidebar's own glyph vocabulary with a
    // Running ring, resolved AFTER the shared model so blocked/review/done
    // precedence is unchanged — the shared tested logic still owns the
    // tag/attention cases; the sidebar only ADDS the state-only Running case
    // that the pure model intentionally leaves neutral.
    enum class SbGlyph { None, Triangle, Diamond, Dot, Ring, Automated };

    // Precedence: the shared, headlessly-tested ecs::model::glyph_for owns the
    // tag/attention families (blocked -> triangle, review -> diamond, done ->
    // dot). If that yields a real glyph, use it. Only when the model says
    // "None" do we consult the sidebar-local state fallback so a self-running
    // thread (state=Running, no tag — the shape a real backend returns) gets
    // its own distinct RING instead of collapsing into the calm dot. Ready
    // (agent-verified, no tag) also earns the review diamond here so the real
    // backend's review-state rows read the same as the mock's tagged ones.
    static SbGlyph glyph_for(const api::SessionSummary& s) {
        switch (ecs::model::glyph_for(s)) {
            case ecs::model::Glyph::Triangle: return SbGlyph::Triangle;
            case ecs::model::Glyph::Diamond: return SbGlyph::Diamond;
            case ecs::model::Glyph::Dot: return SbGlyph::Dot;
            case ecs::model::Glyph::None: break;  // fall through to state map
        }
        // State-only fallback (no tag): give real-data states a distinct glyph
        // so the sidebar vocabulary is as rich on real data as on the mock.
        switch (s.state) {
            case api::ThreadState::Running: return SbGlyph::Ring;
            case api::ThreadState::Ready: return SbGlyph::Diamond;
            default: return SbGlyph::None;
        }
    }
    using Glyph = SbGlyph;

    static theme::Color glyph_color(Glyph g) {
        switch (g) {
            case Glyph::Triangle: return theme::tag_blocked_fg();  // red
            case Glyph::Diamond: return theme::tag_ready_fg();     // green
            case Glyph::Dot: return theme::tag_done_fg();          // blue
            case Glyph::Ring: return theme::accent();              // running
            default: return theme::text_faint();  // Automated/None/calm — quiet
        }
    }

    // Draw the status glyph centered inside `rect` (the on-screen rect of the
    // small glyph slot). Uses afterhours' real shape primitives — filled
    // triangle, a 4-sided poly rotated 45 deg for the diamond, and a circle —
    // so the three attention statuses are visually distinct by SHAPE, not just
    // color. EVERY row draws SOMETHING in this slot: an attention row gets its
    // shape-glyph, a calm row gets a small NEUTRAL dot (not blank), so no row
    // reads as "unlabeled / second-class". The calm dot is deliberately small
    // and faint (a resting bullet), distinct in both size and color from the
    // blue Done dot, so shape+color still separates the four states:
    //   Triangle (red)   blocked / waiting-on-you
    //   Diamond  (green) ready for review
    //   Dot 4px  (blue)  done
    //   Ring 4px (accent) running / in-progress (hollow, so it never reads as
    //                     the filled blue Done dot)
    //   Dot 2.4px(faint) calm  (parked / archived / no signal)
    static void draw_glyph(RectangleType rect, Glyph g) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        if (g == Glyph::None) {
            // Calm rows: a small, faint resting bullet so the row still reads
            // as intentionally-labeled (calm), not blank/broken. Smaller than
            // the 4px Done dot and drawn in the faint token so it never
            // competes with a real status.
            afterhours::draw_circle_v(afterhours::vec2{cx, cy}, 2.4f,
                                      theme::text_faint());
            return;
        }
        const theme::Color c = glyph_color(g);
        switch (g) {
            case Glyph::Triangle: {
                // Up-pointing equilateral-ish triangle, ~9px tall / 10px wide.
                const float half_w = 5.0f;
                const float half_h = 4.5f;
                afterhours::draw_triangle(
                    afterhours::vec2{cx, cy - half_h},          // apex (top)
                    afterhours::vec2{cx - half_w, cy + half_h}, // bottom-left
                    afterhours::vec2{cx + half_w, cy + half_h}, // bottom-right
                    c);
                break;
            }
            case Glyph::Diamond: {
                // Diamond = a 4-sided regular poly with vertices at
                // top/bottom/left/right. draw_poly's first vertex sits at
                // angle 0 (pointing right), so with rotation 0 the four
                // vertices already land at right/up/left/down -> a diamond.
                // (Rotating 45 deg would instead give an axis-aligned square.)
                // Circumradius ~5.6 gives an ~8px diamond, matching the mock.
                afterhours::draw_poly(afterhours::vec2{cx, cy}, 4, 5.6f, 0.0f,
                                      c);
                break;
            }
            case Glyph::Dot: {
                // 8px filled circle.
                afterhours::draw_circle_v(afterhours::vec2{cx, cy}, 4.0f, c);
                break;
            }
            case Glyph::Ring: {
                // Hollow ~8px ring (accent): a self-running / in-progress
                // thread. Hollow so it never reads as the filled blue Done
                // dot — same size, different fill, so real Running rows get a
                // distinct glyph instead of collapsing into the calm dot.
                afterhours::draw_ring(cx, cy, 2.4f, 4.0f, 24, c);
                break;
            }
            case Glyph::Automated: {
                // Cron / scheduled row: a "repeat" sprite from the Lucide atlas
                // (gap #20 — the atlas now carries an "automated" glyph). It
                // reads as "machine / recurring job" far more clearly than the
                // old drawn hollow square, while staying quiet in the faint
                // token. Falls back to the drawn ~6px hollow square if the atlas
                // can't load, so a missing texture never leaves the slot blank.
                if (hanabi::icons::draw_at("automated", cx, cy, 12.0f, c))
                    break;
                const float h = 2.6f;  // half-side (fallback)
                const afterhours::vec2 tl{cx - h, cy - h};
                const afterhours::vec2 tr{cx + h, cy - h};
                const afterhours::vec2 bl{cx - h, cy + h};
                const afterhours::vec2 br{cx + h, cy + h};
                const float t = 1.1f;
                afterhours::draw_line_ex(tl, tr, t, c);
                afterhours::draw_line_ex(tr, br, t, c);
                afterhours::draw_line_ex(br, bl, t, c);
                afterhours::draw_line_ex(bl, tl, t, c);
                break;
            }
            default:
                break;
        }
    }

    // ---- Blocked smart-view nav icon (defect #5) ----
    // A warning-triangle glyph (outlined up-triangle + centered "bang"),
    // centered in the icon slot at `px` size. Used for the Blocked smart view
    // instead of the Lucide "blocked" no-entry sprite so the nav reads as
    // "attention / waiting on you" rather than "forbidden". Drawn (not
    // atlased) because the atlas has no waiting/attention glyph; it reuses the
    // per-row Blocked triangle's shape so the view + its rows match.
    static void draw_attention_icon(RectangleType rect, theme::Color c,
                                    float px) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        const float hw = px * 0.5f;          // half-width of the triangle base
        const float hh = px * 0.44f;         // half-height (apex above center)
        const afterhours::vec2 apex{cx, cy - hh};
        const afterhours::vec2 bl{cx - hw, cy + hh};
        const afterhours::vec2 br{cx + hw, cy + hh};
        // Outlined triangle (3 stroked edges) so it reads as a warning sign,
        // not a solid alert. ~1.4px stroke matches the Lucide line weight.
        const float t = 1.4f;
        afterhours::draw_line_ex(apex, bl, t, c);
        afterhours::draw_line_ex(bl, br, t, c);
        afterhours::draw_line_ex(br, apex, t, c);
        // Exclamation "bang": a short vertical stroke + a dot below it,
        // centered in the triangle body.
        const float bangTop = cy - hh * 0.15f;
        const float bangBot = cy + hh * 0.42f;
        afterhours::draw_line_ex(afterhours::vec2{cx, bangTop},
                                 afterhours::vec2{cx, bangBot}, t, c);
        afterhours::draw_circle_v(afterhours::vec2{cx, cy + hh * 0.72f},
                                  t * 0.7f, c);
    }

    // ---- header / title bar ----
    // Layout intent (unfolded): a single row split into two anchored groups —
    //   [ ✦ hanabi ]  ................dead space collapses here............  [ + ⚙ ‹ ]
    // The brand block is left-aligned with proper padding; the three action
    // icons are grouped in a right-anchored cluster with consistent spacing
    // and equal 28x28 hit-boxes. JustifyContent::SpaceBetween pushes the two
    // groups to the row's edges so there is no floating icon band in the
    // middle (the old layout sized the brand at 62% and let the icons drift).
    //
    // NOTE (traffic lights): hanabi runs in a standard NSWindow, so real macOS
    // traffic-light dots live in the NATIVE title bar ABOVE this content view —
    // we deliberately do NOT draw faux dots here (that would duplicate them).
    // The brand keeps a comfortable left inset so it reads as an intentional
    // wordmark rather than crowding the window's top-left control zone.
    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       LayoutComponent& layout, bool folded) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(40)})
                .with_flex_direction(folded ? FlexDirection::Column
                                            : FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                // Folded rail: LEFT-align the collapse toggle (cross-axis =
                // horizontal for a Column) so it sits at the same left inset as
                // the smart-view icons below, forming one flush-left column.
                // Unfolded: Center keeps the row's brand/actions vertically
                // centered.
                .with_align_items(folded ? AlignItems::FlexStart
                                         : AlignItems::Center)
                .with_justify_content(folded ? JustifyContent::FlexStart
                                             : JustifyContent::SpaceBetween)
                .with_padding(Padding{.top = pixels(7), .right = pixels(8),
                                      .bottom = pixels(5),
                                      .left = pixels(kRailIconInset)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_header"));

        if (!folded) {
            // Header content box (panelW − left pad kRailIconInset − right 8).
            // The action cluster (+ / gear / collapse) is essential and fixed
            // at kActionsW; the brand block takes whatever's left so the two
            // never sum past the header and trip a NoWrap overflow (which would
            // warn + run solve_violations every frame on a narrow sidebar /
            // small window). At the tightest widths the wordmark is dropped and
            // only the ✦ mark shows; and if even the mark + full action cluster
            // won't fit (a sub-~120px sidebar / tiny window), the action
            // cluster's own width is clamped to the remaining space so the
            // header still never overflows (the icons right-justify within it).
            const float kBrandMin = 18.0f;  // just the ✦ mark
            float hdrContent = layout.sidebar.width - kRailIconInset - 8.0f;
            if (hdrContent < kBrandMin + 8.0f) hdrContent = kBrandMin + 8.0f;
            float actionsW = 92.0f;
            if (actionsW > hdrContent - kBrandMin)
                actionsW = hdrContent - kBrandMin;
            if (actionsW < 8.0f) actionsW = 8.0f;
            const float kActionsW = actionsW;
            float brandW = hdrContent - kActionsW;
            if (brandW < kBrandMin) brandW = kBrandMin;  // at least the mark
            if (brandW > 96.0f) brandW = 96.0f;   // original cap
            bool showWordmark = brandW >= 18.0f + 40.0f;  // mark + room for text
            // --- brand block (left-anchored): ✦ mark + "hanabi" wordmark ---
            // Fixed-width so SpaceBetween has real slack to distribute; the
            // ✦ mark is a Lucide "sparkle" sprite (on_draw_fg), the wordmark
            // is text. Sized to just hug its content so the right cluster
            // anchors to the sidebar's right edge.
            auto brand = div(ctx, mk(header.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(brandW), pixels(26)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("sb_brand"));
            div(ctx, mk(brand.ent(), 1),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(18), pixels(22)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        "brand", "\xe2\x9c\xa6", theme::accent(), 15.0f,
                        -1.0f))
                    .with_debug_name("sb_brand_mark"));
            if (showWordmark)
            div(ctx, mk(brand.ent(), 2),
                ComponentConfig{}
                    .with_label("hanabi")
                    .with_size(ComponentSize{pixels(brandW - 24.0f), pixels(24)})
                    .with_padding(Padding{.left = pixels(6)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("sb_brand_name"));

            // --- action cluster (right-anchored): + / gear / collapse ---
            // A NoWrap row that packs the three icons at the sidebar's right
            // edge with an even gap. Each icon shares the SAME 28x28 hit-box
            // (icon_btn_sprite) so they read as one consistent control group.
            auto actions = div(ctx, mk(header.ent(), 2),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(kActionsW), pixels(28)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_justify_content(JustifyContent::FlexEnd)
                    .with_gap(pixels(3))
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("sb_actions"));

            // New task → open the composer (Phase K composer system renders it).
            // Icons are 28px + 3px gap; at very narrow header widths only as
            // many as fit are shown (collapse is essential and kept last, then
            // settings, then new) so the cluster never overflows its clamped
            // width and churns solve_violations. At realistic widths (≥ ~140px
            // sidebar) all three fit.
            int iconsFit = static_cast<int>((kActionsW + 3.0f) / 31.0f);
            if (iconsFit < 1) iconsFit = 1;
            if (iconsFit > 3) iconsFit = 3;
            if (iconsFit >= 3) {
            auto newBtn = button(ctx, mk(actions.ent(), 1),
                icon_btn_sprite("plus", "+").with_debug_name("sb_new"));
            if (newBtn) {
                if (auto* app = find_singleton<AppComponent>())
                    app->composerOpen = true;
            }
            }

            // Settings → open the settings overlay (Phase K settings system).
            if (iconsFit >= 2) {
            auto setBtn = button(ctx, mk(actions.ent(), 2),
                icon_btn_sprite("gear", "\xe2\x9a\x99")
                    .with_debug_name("sb_settings"));
            if (setBtn) {
                if (auto* app = find_singleton<AppComponent>())
                    app->showSettings = true;
            }
            }

            // Collapse toggle joins the cluster (unfolded state).
            auto collapseBtn = button(ctx, mk(actions.ent(), 3),
                icon_btn_sprite("sidebar_close", "\xc2\xab")
                    .with_debug_name("sb_collapse"));
            if (collapseBtn) {
                layout.sidebarCollapsed = !layout.sidebarCollapsed;
                Settings::get().set_sidebar_collapsed(layout.sidebarCollapsed);
            }
            return;
        }

        // Folded rail: a single expand toggle in the header column.
        auto collapseBtn = button(ctx, mk(header.ent(), 4),
            icon_btn_sprite("sidebar_open", "\xc2\xbb")
                .with_debug_name("sb_collapse"));
        if (collapseBtn) {
            layout.sidebarCollapsed = !layout.sidebarCollapsed;
            Settings::get().set_sidebar_collapsed(layout.sidebarCollapsed);
        }
    }

    static ComponentConfig icon_btn(const std::string& label) {
        return ComponentConfig{}
            .with_label(label)
            .with_size(ComponentSize{pixels(26), pixels(26)})
            .with_custom_background(theme::sidebar_bg())
            .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
            .with_custom_text_color(theme::text_secondary())
            .with_font_size(FontSize::Medium)
            .with_alignment(TextAlignment::Center)
            .with_justify_content(JustifyContent::Center)
            .with_align_items(AlignItems::Center)
            .with_click_activation(ClickActivationMode::Press)
            .with_roundness(0.3f);
    }

    // Sprite-icon button: a consistent 28x28 chrome button (equal hit-box for
    // every title-bar action) that blits the Lucide atlas sprite `name`
    // (tinted) via on_draw_fg, keeping the widget label empty. `fallback_glyph`
    // is the legacy unicode text drawn only if the atlas fails to load. Routed
    // through icons::draw_fg so a future icon-source swap is localized.
    static ComponentConfig icon_btn_sprite(const std::string& name,
                                           const std::string& fallback_glyph) {
        return ComponentConfig{}
            .with_label(" ")
            .with_size(ComponentSize{pixels(28), pixels(28)})
            .with_custom_background(theme::sidebar_bg())
            .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
            .with_cursor(afterhours::ui::CursorType::Pointer)
            .with_click_activation(ClickActivationMode::Press)
            .with_roundness(0.3f)
            .with_on_draw_fg(hanabi::icons::draw_fg(
                name, fallback_glyph, theme::text_secondary(), 16.0f));
    }

    // ---- search (unfolded only) ----
    // `panelW` is the live sidebar width (LayoutComponent::sidebar.width). The
    // search text field shares a NoWrap row with fixed-width siblings (a 18px
    // magnifier slot, and an 18px clear-× slot when a query is present).
    // afterhours has NO flex-grow (afterhours_gaps.md #18): a percent(1.0f)
    // child means 100% of the PARENT width, so a percent text field next to
    // fixed siblings overflows the row every frame. So we size the text field
    // in PIXELS = the field's inner content width minus those reserved slots.
    void render_search(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, float panelX, float panelY,
                       float panelW) {
        // Wrap in a full-width padded row so the search field itself never
        // extends past the sidebar (margins on a percent(1.0) child overflow).
        auto wrap = div(ctx, mk(parent, 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(40)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(4), .right = pixels(10),
                                      .bottom = pixels(6), .left = pixels(10)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_search_wrap"));
        // Search field: a row-flex pill holding a magnifier sprite slot + an
        // editable text_input. HOVER + FOCUS states (Gabe: "you still didnt
        // address the hover state for the input for search"): the pill lifts on
        // hover (hover_over wash) and shows a focus RING (accent border) while
        // the text_input is focused. Both are read from the PREVIOUS frame's
        // context state (ids are stable across frames via mk()), since in
        // immediate mode the pill div is emitted before its input child.
        static afterhours::EntityID s_searchFieldId =
            std::numeric_limits<afterhours::EntityID>::max();
        static afterhours::EntityID s_searchInputId =
            std::numeric_limits<afterhours::EntityID>::max();
        const bool searchHot =
            s_searchFieldId !=
                std::numeric_limits<afterhours::EntityID>::max() &&
            (ctx.is_hot(s_searchFieldId) || ctx.was_hot(s_searchFieldId) ||
             (s_searchInputId !=
                  std::numeric_limits<afterhours::EntityID>::max() &&
              ctx.is_hot(s_searchInputId)));
        const bool searchFocused =
            s_searchInputId !=
                std::numeric_limits<afterhours::EntityID>::max() &&
            ctx.has_focus(s_searchInputId);
        // Fill lifts on hover; border is the accent focus-ring when focused,
        // a hairline otherwise (so the field always reads as an input, and
        // clearly as the ACTIVE input while typing).
        theme::Color fieldFill =
            searchHot ? theme::hover_over(theme::panel_bg_2())
                      : theme::panel_bg_2();
        theme::Color fieldBorder =
            searchFocused ? theme::focus_ring() : theme::border();
        auto field = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(30)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(5), .right = pixels(8),
                                      .bottom = pixels(5), .left = pixels(8)})
                .with_custom_background(fieldFill)
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_border(fieldBorder,
                             pixels(searchFocused ? 1.5f : 1.0f))
                .with_roundness(0.3f)
                .with_debug_name("sb_search"));
        s_searchFieldId = field.ent().id;
        div(ctx, mk(field.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(18), pixels(18)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "search", "\xf0\x9f\x94\x8d", theme::text_faint(), 13.0f,
                    -1.0f))
                .with_debug_name("sb_search_icon"));

        // Editable field bound to app.searchQuery. text_input() reads/writes
        // the std::string reference and drains typed chars while focused
        // (click to focus). It forces its own Secondary background (gap #17),
        // so we let it FILL the pill's remaining width — but in PIXELS, not
        // percent(1.0): afterhours has no flex-grow, so a percent child in a
        // NoWrap row overflows past its fixed siblings. Compute the width as
        // the field's inner content box minus the reserved icon (and clear-×,
        // when a query is present) slots. Content box = panelW − wrap pad
        // (10+10) − field pad (8+8) = panelW − 36. Reserve 20 per fixed slot
        // (18px glyph + ~2px the flex layout leaves before the next child) so
        // the field's right edge never crosses the pill. Clamp to a sane min
        // so a narrow sidebar never yields a negative/zero width.
        bool hasQuery = !app.searchQuery.empty();
        const float kSearchSlot = 20.0f;  // per fixed sibling (icon / clear-×)
        float searchInner = panelW - 36.0f;             // field content box
        float searchTextW = searchInner - kSearchSlot;  // minus magnifier slot
        if (hasQuery) searchTextW -= kSearchSlot;        // minus clear-× slot
        // Clamp so the text field never exceeds the pill's inner box (else the
        // NoWrap search row overflows + churns solve_violations). Prefer 40px,
        // but if the field is genuinely tiny (a sub-usable sidebar width), fall
        // back to the actual remaining width so it still can't overflow.
        if (searchTextW < 40.0f)
            searchTextW = std::max(12.0f, searchInner - kSearchSlot -
                                              (hasQuery ? kSearchSlot : 0.0f));
        auto searchRes = afterhours::text_input::text_input(
            ctx, mk(field.ent(), 2), app.searchQuery,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(searchTextW), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(hasQuery ? theme::text_primary()
                                                 : theme::text_faint())
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sb_search_text"));
        s_searchInputId = searchRes.ent().id;

        // Clear affordance (only when a query is present): an ✕ that empties
        // the query and restores the full tree.
        if (hasQuery) {
            auto clr = button(ctx, mk(field.ent(), 3),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(18), pixels(20)})
                    .with_transparent_bg()
                    .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::LG)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.3f)
                    // Lucide "close" sprite (atlas); \xc3\x97 unicode fallback.
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        "close", "\xc3\x97", theme::text_faint(), 12.0f))
                    .with_debug_name("sb_search_clear"));
            if (clr) app.searchQuery.clear();
        }

        // Placeholder (defect 13): afterhours' text_input has no native
        // placeholder AND it forces an opaque Secondary fill over its own rect
        // (gap #17), so a placeholder painted *behind* the input is covered.
        // We instead overlay a faint "Search conversations" ON TOP of the empty
        // input via an absolutely-positioned child (out of flex flow, so it
        // never shifts the field) whose on_draw_fg paints the hint text. Only
        // rendered while the query is empty; the moment the user types, the
        // real glyphs replace it. The field origin is derived from the sidebar
        // panel geometry: panel(panelX,panelY) → header 40 → search wrap
        // (top pad 4) → field (left pad 8 + magnifier slot 18). See the search
        // layout notes above for the reserved slots.
        if (app.searchQuery.empty()) {
            const float phX = panelX + 10.0f + 8.0f + 18.0f + 4.0f;  // text start
            const float phY = panelY + 40.0f + 4.0f;                 // field top
            const float phW = panelW - (phX - panelX) - 12.0f;
            div(ctx, mk(parent, 9),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(phW > 20.0f ? phW : 20.0f),
                                             pixels(30)})
                    .with_absolute_position()
                    .with_translate(phX, phY)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(3)
                    .with_on_draw_fg([](RectangleType rect) {
                        const float px = theme::type::ROW;
                        const float ty = rect.y + rect.height * 0.5f - px * 0.5f;
                        afterhours::draw_text("Search conversations", rect.x, ty,
                                              px, theme::text_faint());
                    })
                    .with_debug_name("sb_search_placeholder"));
        }
    }

    // ---- section label (mock .sb-section-label: 10.5px uppercase faint,
    // padding 10/14/5) ----
    void section_label(UIContext<InputAction>& ctx, Entity& parent, int id,
                       const std::string& text) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(25)})
                .with_padding(Padding{.top = pixels(10), .right = pixels(14),
                                      .bottom = pixels(5), .left = pixels(14)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::LABEL)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sb_section_label"));
    }

    // ---- FOLDERS section header: label + fold-all control ----
    // The mock's Folders section header carries a fold-all affordance that
    // collapses/expands EVERY folder at once. Clicking it toggles
    // app.foldAllFolders and applies that state to every folder key, so all
    // folders snap open or closed together.
    void folders_section_head(UIContext<InputAction>& ctx, Entity& parent,
                              int base, AppComponent& app, float panelW) {
        auto head = div(ctx, mk(parent, base),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                // SpaceBetween pins the label left and the fold-all button hard
                // to the RIGHT edge (afterhours has no flex-grow — gap #18 — so
                // a percent-width label left the button parked mid-row instead
                // of flush-right). Right inset = kCountRightPad so the fold-all
                // icon lands on the SAME right edge as the thread-row counts
                // (and clears the temp scroll bar, which sits ~8px in).
                .with_justify_content(JustifyContent::SpaceBetween)
                // Right inset clears the temporary scroll indicator (which sits
                // ~8px in from the panel's right edge) so the fold-all glyph is
                // never clipped by / overlapping the scrollbar corner.
                .with_padding(Padding{.top = pixels(6),
                                      .right = pixels(12),
                                      .bottom = pixels(4), .left = pixels(14)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_folders_head"));

        // FOLDERS label: intrinsic-ish fixed width so SpaceBetween pushes the
        // fold-all button to the far right edge. Clamp it so label + button +
        // pads never exceed the header content box on a narrow sidebar (else
        // the NoWrap head overflows every frame → warn + solve_violations
        // churn). Content = panelW − left 14 − right 4.
        float headContent = panelW - 14.0f - 4.0f;
        float foldLabelW = headContent - 28.0f;  // minus the fold-all button
        if (foldLabelW > 64.0f) foldLabelW = 64.0f;
        if (foldLabelW < 16.0f) foldLabelW = 16.0f;
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label("FOLDERS")
                .with_size(ComponentSize{pixels(foldLabelW), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::LABEL)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sb_folders_label"));

        // Fold-all button (chevrons-down-up sprite). Its tint brightens when
        // all folders are currently folded, echoing the mock's active state.
        // Wider tap target (28px) so it's easy to hit, a clearer hover surface
        // (panel_bg_2 wash reads as a real button, not just a tint), and a
        // smaller right inset than the row counts so it sits closer to the edge.
        theme::Color foldTint =
            app.foldAllFolders ? theme::text_secondary() : theme::text_faint();
        auto foldBtn = button(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(24), pixels(20)})
                .with_custom_background(theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                // Crisp custom double-chevron (the Lucide chevrons-down-up
                // sprite rasterized poorly at ~15px and read as a broken glyph).
                // Two chevrons meeting in the middle = the standard "collapse
                // all" affordance: top points UP, bottom points DOWN when
                // folders are expanded (i.e. "click to collapse"); it flips to
                // point apart when everything is already folded.
                .with_on_draw_fg([foldTint, allFolded = app.foldAllFolders]
                                 (RectangleType r) {
                    // Two stacked solid triangles (draw_triangle — crisp, no
                    // rounded-cap blobbing like draw_line_ex gave at this size).
                    const float cx = r.x + r.width * 0.5f;
                    const float cy = r.y + r.height * 0.5f;
                    const float s = 3.4f;   // triangle half-width
                    const float hh = 3.0f;  // triangle half-height
                    const float off = 3.4f; // vertical offset of each triangle
                    using afterhours::vec2;
                    if (!allFolded) {
                        // Expanded -> "collapse": top triangle points UP,
                        // bottom points DOWN (apexes away from center).
                        float ty = cy - off;
                        afterhours::draw_triangle(vec2{cx - s, ty + hh},
                            vec2{cx + s, ty + hh}, vec2{cx, ty - hh}, foldTint);
                        float by = cy + off;
                        afterhours::draw_triangle(vec2{cx - s, by - hh},
                            vec2{cx + s, by - hh}, vec2{cx, by + hh}, foldTint);
                    } else {
                        // All folded -> "expand": top points DOWN, bottom UP
                        // (apexes toward center).
                        float ty = cy - off;
                        afterhours::draw_triangle(vec2{cx - s, ty - hh},
                            vec2{cx + s, ty - hh}, vec2{cx, ty + hh}, foldTint);
                        float by = cy + off;
                        afterhours::draw_triangle(vec2{cx - s, by + hh},
                            vec2{cx + s, by + hh}, vec2{cx, by - hh}, foldTint);
                    }
                })
                .with_debug_name("sb_fold_all"));
        if (foldBtn) {
            app.foldAllFolders = !app.foldAllFolders;
            // Fold/unfold every REAL folder in lockstep. Folders are DYNAMIC
            // (derived from distinct_folders — the actual folder values on the
            // sessions), so build the key set from those, using the SAME key
            // render_folder collapses on (the raw folder value). The Recent
            // catch-all is a HEADERLESS flat list with no header to collapse,
            // so it's skipped. (The old hardcoded stars/oncall/experiments +
            // __t_*__ time-bucket + __archived__ keys are obsolete — folders
            // are dynamic, Recent is headerless, and Archived is a smart VIEW.)
            if (app.foldAllFolders) {
                for (const auto& k : distinct_folders(app))
                    app.collapsedFolders.insert(k);
            } else {
                app.collapsedFolders.clear();
            }
        }
    }

    // ---- smart views ----
    void render_smart_views(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app, bool folded, float panelW) {
        // "VIEWS" section label (unfolded only, per the mock).
        if (!folded) section_label(ctx, parent, 25, "VIEWS");

        auto container = div(ctx, mk(parent, 3),
            ComponentConfig{}
                // Height fits the 5 rows exactly (children()) — a fixed 178px
                // was taller than the rows (~160px), leaving dead space that
                // opened a large gap before the FOLDERS section (M3).
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                // Folded rail: left inset == the header toggle's, so the icon
                // column is flush-left in one vertical line. Unfolded: the
                // original 8px inset the label/count geometry is tuned around.
                .with_padding(Padding{.top = pixels(0), .right = pixels(4),
                                      .bottom = pixels(2),
                                      .left = pixels(folded ? kRailIconInset
                                                            : 8.0f)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("smart_views"));

        int review = 0, starred = 0, archived = 0, blocked = blocked_count(app);
        for (const auto& s : app.sessions) {
            if (s.state == api::ThreadState::Ready) ++review;
            if (s.starred) ++starred;
            if (s.state == api::ThreadState::Archived) ++archived;
        }

        smart_item(ctx, container.ent(), 1, "home", "\xe2\x8c\x82", "Home",
                   SmartView::Home, -1, app, folded, panelW);
        smart_item(ctx, container.ent(), 2, "blocked", "\xe2\x9b\x94",
                   "Blocked", SmartView::Blocked, blocked, app, folded, panelW);
        smart_item(ctx, container.ent(), 3, "review", "\xe2\x9c\x93", "Review",
                   SmartView::Review, review, app, folded, panelW);
        smart_item(ctx, container.ent(), 4, "star", "\xe2\x98\x85", "Starred",
                   SmartView::Starred, starred, app, folded, panelW);
        // "archive" now has a real Lucide sprite in the atlas; \xe2\x96\xa4 stays as fallback.
        smart_item(ctx, container.ent(), 5, "archive", "\xe2\x96\xa4",
                   "Archived", SmartView::Archived, archived, app, folded,
                   panelW);
    }

    void smart_item(UIContext<InputAction>& ctx, Entity& parent, int idx,
                    const std::string& icon_name,
                    const std::string& fallback_glyph,
                    const std::string& label, SmartView view, int count,
                    AppComponent& app, bool folded, float panelW) {
        bool active = app.view == view;
        auto row = div(ctx, mk(parent, 100 + idx),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(30)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                // Folded rail: no row left-pad — the container's kRailIconInset
                // already sets the icon column's left edge, and the icon slot
                // (kRailIconSlot, == the header toggle box) makes the centered
                // glyph land on the same vertical line as the toggle above.
                // Right pad == kCountRightPad so the smart-view count's right
                // edge lands on the SAME column as the folder-header + thread-
                // row counts (was 8 vs 12 — a 4px cross-section count misalign).
                .with_padding(Padding{.top = pixels(4),
                                      .right = pixels(kCountRightPad),
                                      .bottom = pixels(4),
                                      .left = pixels(folded ? 0.0f : 8.0f)})
                .with_margin(Margin{.top = pixels(1), .right = pixels(0),
                                    .bottom = pixels(1), .left = pixels(0)})
                .with_custom_background(active ? theme::selected_bg()
                                               : theme::sidebar_bg())
                // A SELECTED item does not react to hover (no double state):
                // its hover bg == its selected fill, so hovering it is a no-op.
                // Only an UNselected item gets the subtle hover wash.
                .with_custom_hover_bg(active ? theme::selected_bg()
                                             : theme::hover_over(
                                                   theme::sidebar_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("smart_item"));

        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (row.ent().get<afterhours::ui::HasClickListener>().down) {
            app.view = view;
        }

        theme::Color txt =
            active ? theme::text_primary() : theme::text_secondary();

        // Folded rail: a smart view whose count > 0 gets a small attention dot
        // at the icon's top-right corner (Blocked = red, others = accent), so
        // the thin rail still signals "something waits here" without labels —
        // matching the mock's rail badge.
        bool railDot = folded && count > 0;
        theme::Color dotColor = (view == SmartView::Blocked)
                                    ? theme::tag_blocked_fg()
                                    : theme::accent();
        // Defect #5: the Blocked smart-view nav icon was the Lucide "blocked"
        // atlas sprite — a no-entry / prohibition circle-slash that reads as
        // "forbidden / banned", not "waiting on you / needs attention". The
        // atlas (src/ui/icons_atlas.h) has NO better-fitting glyph: it carries
        // only brand/gear/plus/search/sidebar/chevron/home/blocked/review(check)
        // /star/folder_grid/fold_all — nothing that reads as
        // waiting/attention (no clock, hourglass, inbox, bell, or hand). So we
        // draw the Blocked view's icon as a WARNING TRIANGLE (an outlined
        // up-triangle with a bang), which (a) reads as "attention", and (b)
        // reuses the SAME up-triangle shape the per-row Blocked/attention glyph
        // already uses, so the smart view and its rows share one visual
        // vocabulary. Ideally the atlas would gain a Lucide "clock" (or
        // "bell"/"hourglass"/"inbox") sprite for this — see report / gen_icons
        // note; that's owned elsewhere, so we draw the triangle in-app rather
        // than regenerate the atlas.
        const bool useAttentionIcon = (view == SmartView::Blocked);
        const float iconPx = folded ? 18.0f : 16.0f;
        auto attnColor = txt;
        auto iconDraw = hanabi::icons::draw_fg(icon_name, fallback_glyph, txt,
                                               16.0f, -1.0f);
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{
                    pixels(folded ? kRailIconSlot : 18.0f), pixels(22)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([iconDraw, useAttentionIcon, attnColor, iconPx,
                                  railDot, dotColor](RectangleType rect) {
                    if (useAttentionIcon)
                        draw_attention_icon(rect, attnColor, iconPx);
                    else
                        iconDraw(rect);
                    if (railDot) {
                        const float cx = rect.x + rect.width * 0.5f + 8.0f;
                        const float cy = rect.y + rect.height * 0.5f - 7.0f;
                        afterhours::draw_circle_v(afterhours::vec2{cx, cy}, 3.2f,
                                                  dotColor);
                    }
                })
                .with_debug_name("sv_icon"));

        if (folded) return;

        // Label column: explicit pixel width so the count box that follows is
        // pushed flush to the row's right edge (afterhours has no flex-grow, so
        // a percent label would leave the count packed mid-row, not aligned).
        // Row content = panelW − container pad (l8 + r4) − row pad (l8 + r8)
        //             = panelW − 28. label = content − icon(18) − count(kCol).
        // With the count box right edge == panelW − kCountRightPad, the smart
        // counts line up with the folder / time-group counts.
        //
        // No-overflow (defect: layout-warn spam): if icon+label+count exceed
        // the row content the NoWrap row overflows every frame (warn +
        // solve_violations churn). At narrow widths DROP the count rather than
        // clamp the label into an overflow; the label then takes the full
        // remaining width. Uses a small label floor so we only keep the count
        // while it genuinely fits.
        const float kSvContent = panelW - 28.0f;
        const float kSvLabelMin = 30.0f;
        bool svShowCount =
            count > 0 && (kSvContent - 18.0f - kSvLabelMin) >= kCountColW;
        float svLabelW = kSvContent - 18.0f - (svShowCount ? kCountColW : 0.0f);
        if (svLabelW < 16.0f) svLabelW = 16.0f;
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(svLabelW), pixels(22)})
                .with_padding(Padding{.left = pixels(10)})
                .with_transparent_bg()
                .with_custom_text_color(txt)
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sv_label"));

        if (svShowCount) {
            // Count column: fixed width, right-aligned. Its right edge lands at
            // panelW − kCountRightPad because the label above is sized to push
            // it flush to the row edge (unified column; see the count geometry
            // note). This makes all three count families flush to one edge.
            div(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(std::to_string(count))
                    .with_size(ComponentSize{pixels(kCountColW), pixels(22)})
                    .with_transparent_bg()
                    .with_custom_text_color(active ? theme::text_primary()
                                                   : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("sv_count"));
        }
    }

    // Is `key` one of the explicitly-named folders (not the Recent catch-all)?
    // A session whose folder matches one of these belongs to that named
    // folder; anything else falls through to the Recent catch-all.
    // A folder is "named" (rendered as its own section, excluded from the
    // Recent catch-all) iff it's a real, non-empty folder value from the API.
    // Unfoldered sessions (folder=="") fall through to the catch-all.
    static bool is_named_folder(const std::string& folder) {
        return !folder.empty() && folder != "recent";
    }

    // Title-case a folder key for display ("stars"->"Stars",
    // "whole foods"->"Whole Foods"). The raw key is still used for matching;
    // this only affects the header label. A folder that already has caps
    // (a real workspace name) is shown as-is except for a lowercase leading
    // char, so "Whole foods" stays "Whole foods".
    static std::string display_folder_name(const std::string& key) {
        if (key.empty()) return key;
        // If it already contains an uppercase letter, treat it as a real name
        // and only capitalize the first char.
        bool hasUpper = false;
        for (char c : key) if (c >= 'A' && c <= 'Z') { hasUpper = true; break; }
        std::string out = key;
        if (hasUpper) {
            if (out[0] >= 'a' && out[0] <= 'z') out[0] = out[0] - 32;
            return out;
        }
        // All-lowercase key: Title Case each word (shared ASCII helper, 1c).
        return fmtutil::ascii_title(std::move(out));
    }

    // Distinct non-empty folder names present across the loaded sessions
    // (excluding archived-only), sorted for stable display order. These are
    // the REAL folders the backend returned — no hardcoded set.
    static std::vector<std::string> distinct_folders(const AppComponent& app) {
        std::vector<std::string> out;
        for (const auto& s : app.sessions) {
            if (s.state == api::ThreadState::Archived) continue;
            if (!is_named_folder(s.folder)) continue;
            if (std::find(out.begin(), out.end(), s.folder) == out.end())
                out.push_back(s.folder);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // ---- folder group ----
    // Renders a collapsible folder. Returns the number of chat rows actually
    // rendered (used by the caller to drive the search no-results state).
    // `q` is the already-lowercased search query ("" = no filter).
    // `catchAll`: when true (the Recent folder), also gathers any non-archived
    // session whose folder is NOT a named folder (folder="" or unknown) — so a
    // real backend's unfoldered sessions are browsable instead of hidden.
    int render_folder(UIContext<InputAction>& ctx, Entity& parent, int base,
                      const std::string& name, const std::string& key,
                      AppComponent& app, const std::string& q, float panelW,
                      bool archived = false, bool catchAll = false,
                      bool headerless = false, int cap = kBucketCap) {
        // Collect member threads, honoring the live search filter.
        std::vector<const api::SessionSummary*> members;
        for (const auto& s : app.sessions) {
            bool match;
            if (archived) {
                match = (s.state == api::ThreadState::Archived);
            } else if (catchAll) {
                // Recent = its own key OR any unfoldered/unknown-folder session
                // that isn't archived. Named-folder sessions are excluded so a
                // session shows in exactly one place.
                match = (s.state != api::ThreadState::Archived) &&
                        (s.folder == key || !is_named_folder(s.folder));
            } else {
                match = (s.folder == key &&
                         s.state != api::ThreadState::Archived);
            }
            // Match on TITLE or, failing that, on cached CONVERSATION CONTENT
            // (local-first idea #3): the sidebar search now finds threads by
            // what was SAID in them, not just their title — using only the
            // local transcript cache (instant, offline). Content is only
            // checked when there's a query and the title didn't already match.
            if (match &&
                (title_matches(s.title, q) ||
                 (!q.empty() && api::disk_cache::content_matches(s.id, q))))
                members.push_back(&s);
        }
        // Hide a folder with no (matching) members. With an active query this
        // is what drops non-matching folders out of the tree.
        if (members.empty()) return 0;

        // Per Gabe: do NOT day-bucket. Both named folders and the Recent
        // catch-all render as a single FLAT list, newest-first. (The old
        // Today/Yesterday/Prev-week time grouping + render_time_groups were
        // removed — dead per this decision, ponytail types pass 2026-08-03.)
        if (catchAll) {
            std::sort(members.begin(), members.end(),
                      [](const api::SessionSummary* a,
                         const api::SessionSummary* b) {
                          return a->updated_at > b->updated_at;
                      });
        }
        return render_group(ctx, parent, base, name, key, members, app, q,
                            panelW, archived, headerless, cap);
    }

    // ---- collapsible group header (shared by folders + time-groups) ----
    // Renders the chevron + name + right-aligned count header for a group and
    // wires its click-to-collapse. Returns the resolved `collapsed` state so
    // the caller knows whether to emit body rows. `count` is shown in the
    // unified count column so folder heads + time-group heads land flush with
    // the smart-view counts.
    bool render_group_header(UIContext<InputAction>& ctx, Entity& parent,
                             int base, const std::string& name,
                             const std::string& key, int count,
                             theme::Color headColor, AppComponent& app,
                             const std::string& q, float panelW) {
        bool collapsed = app.collapsedFolders.count(key) > 0;
        // A live search overrides collapse: matches must be visible, so a
        // matching group auto-expands while filtering (mirrors the mock).
        if (!q.empty()) collapsed = false;

        // Header row. Right pad is kCountRightPad so the count's right edge
        // lines up with the smart-view / other-group counts (unified column).
        auto head = div(ctx, mk(parent, base),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(26)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(4),
                                      .right = pixels(kCountRightPad),
                                      .bottom = pixels(4), .left = pixels(10)})
                .with_custom_background(theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("folder_head"));

        // Clicking the header toggles collapse for this key. Disabled while a
        // query is active (results stay pinned open).
        head.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (q.empty() &&
            head.ent().get<afterhours::ui::HasClickListener>().down) {
            if (app.collapsedFolders.count(key)) {
                app.collapsedFolders.erase(key);
                // Re-expanding a bucket returns it to the CAPPED view: clear
                // any prior "show all" opt-in so re-opening never dumps the
                // full wall (defect #2 — progressive disclosure resets on
                // collapse). The user must click "Show N more…" again to see
                // the long list. This guarantees no bucket renders its full
                // body (e.g. 84 rows) just from a header expand — only an
                // explicit "Show more" does.
                app.collapsedFolders.erase("__more_" + key + "__");
            } else {
                app.collapsedFolders.insert(key);
            }
            // Any explicit per-group toggle drops out of "fold all" mode.
            app.foldAllFolders = false;
        }

        // Chevron: down = expanded, right = collapsed (drawn triangle; the
        // atlas has no chevron-right).
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(16), pixels(18)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([collapsed, headColor](RectangleType rect) {
                    draw_chevron(rect, collapsed, headColor);
                })
                .with_debug_name("folder_chevron"));
        // Name column: explicit pixel width so the count column starts at the
        // same x for every group. leftInset = head left pad (10); leadSlot =
        // the 16px chevron. At narrow widths the trailing count is dropped
        // (not clamped into an overflow) so the NoWrap header never overflows
        // + churns solve_violations — see label_col_w / the row width note.
        const float kHeadContent = panelW - 10.0f - kCountRightPad;
        bool headShowCount = (kHeadContent - 16.0f - 30.0f) >= kCountColW;
        float headNameW = headShowCount
                              ? label_col_w(panelW, 10.0f, 16.0f)
                              : (kHeadContent - 16.0f);
        if (headNameW < 16.0f) headNameW = 16.0f;
        div(ctx, mk(head.ent(), 4),
            ComponentConfig{}
                .with_label(name)
                .with_size(ComponentSize{pixels(headNameW), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(headColor)
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("folder_name"));
        if (headShowCount)
        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(std::to_string(count))
                .with_size(ComponentSize{pixels(kCountColW), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("folder_count"));

        return collapsed;
    }

    // ---- render one collapsible group (header + its rows) ----
    // `members` is the pre-collected, already-filtered row list for this group.
    // Returns the group's member count (so the caller drives the no-results
    // state); a collapsed group still counts (its header stays visible).
    int render_group(UIContext<InputAction>& ctx, Entity& parent, int base,
                     const std::string& name, const std::string& key,
                     const std::vector<const api::SessionSummary*>& members,
                     AppComponent& app, const std::string& q, float panelW,
                     bool archived, bool headerless = false,
                     int cap = kBucketCap) {
        if (members.empty()) return 0;
        // Headerless: unfoldered sessions render as a plain flat list with NO
        // folder header (per Gabe: "only keep the real folders" — no invented
        // "Recent" folder). Still capped with a "show more" so it can't dump a
        // 200-row wall.
        if (!headerless) {
            theme::Color headColor =
                archived ? theme::text_faint() : theme::text_secondary();
            bool collapsed = render_group_header(
                ctx, parent, base, name, key,
                static_cast<int>(members.size()), headColor, app, q, panelW);
            // Collapsed: header only, no body rows.
            if (collapsed) return static_cast<int>(members.size());
        }

        // Defect #2: a single heavy bucket (e.g. real data landing ~92
        // sessions in "Yesterday") is still a scroll-pit even though it has a
        // header — the finer time-bucketing MOVED the pile, it didn't SPLIT
        // it. So within an expanded group we CAP the visible rows at `cap`
        // and, when there are more, render a "Show N more…" expander
        // row instead of the full wall. Clicking it flips a per-group "show
        // all" flag so the user opts into the long list explicitly. This is
        // the standard chat-app fix and it GUARANTEES no single section
        // renders a 90-row wall on first open, regardless of how the backend
        // clusters timestamps.
        //
        // `cap` defaults to kBucketCap (named folders / time buckets), but the
        // headerless catch-all passes a larger, viewport-sized cap so it FILLS
        // the scroll area with the "Show N more…" button riding at the bottom
        // (V6) instead of leaving empty space below a fixed ~12-row cap.
        //
        // The expanded state is stored in the existing collapsedFolders set
        // (no new AppComponent field — that component is owned elsewhere) under
        // a distinct "__more_<key>__" sentinel so it never collides with a
        // real folder/bucket key or the collapse keys. Presence = expanded.
        // A live search (q non-empty) shows ALL matches uncapped — the filter
        // has already narrowed the list and hiding matches behind "show more"
        // would defeat the search.
        const int total = static_cast<int>(members.size());
        const std::string moreKey = "__more_" + key + "__";
        const bool expandedMore =
            !q.empty() || app.collapsedFolders.count(moreKey) > 0;
        const int limit =
            (expandedMore || total <= cap) ? total : cap;

        int i = 0;
        for (const auto* s : members) {
            if (i >= limit) break;
            render_chat_row(ctx, parent, base + 1 + (++i), *s, app, archived,
                            panelW);
        }

        // "Show N more…" expander (only when capped). Clicking adds the more-
        // key so the next frame renders every row. Placed at the TOP of the
        // slot's id range (base + kMoreRowIdOffset) so it never collides with a
        // body row id (base + 1 .. base + total).
        if (!expandedMore && total > cap) {
            const int hidden = total - cap;
            auto more = div(ctx, mk(parent, base + kMoreRowIdOffset),
                ComponentConfig{}
                    .with_label("Show " + std::to_string(hidden) + " more\xe2\x80\xa6")
                    .with_size(ComponentSize{percent(1.0f), pixels(24)})
                    // V7: align the "Show N more…" label with the thread-row
                    // TITLE text above it, not the row's left edge. A thread
                    // row's title begins after the row left inset (22px) PLUS
                    // its leading status-glyph slot (kGlyphW = 12px), so its
                    // text starts at 34px. The old 22px here left the label
                    // flush under the glyph column instead of under the titles;
                    // matching 22 + kGlyphW lands "Show N more…" directly under
                    // the thread titles so it reads as part of the same list.
                    .with_padding(Padding{.top = pixels(2), .right = pixels(8),
                                          .bottom = pixels(2),
                                          .left = pixels(kRowLeftInset +
                                                         kGlyphW)})
                    .with_custom_background(theme::sidebar_bg())
                    .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::ROW)
                    .with_alignment(TextAlignment::Left)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_roundness(0.3f)
                    .with_debug_name("sb_show_more"));
            more.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            if (more.ent().get<afterhours::ui::HasClickListener>().down) {
                app.collapsedFolders.insert(moreKey);
            }
        }
        return total;
    }



    // ---- high-signal chat row ----
    // `panelW` is the live sidebar width (LayoutComponent::sidebar.width).
    void render_chat_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const api::SessionSummary& s, AppComponent& app,
                         bool archived, float panelW) {
        bool attn = is_attention(s.state);
        bool selected = app.selectedId == s.id;
        // Defect #5: cron / scheduled rows are visually de-emphasized (not
        // hidden). Detect purely by title shape ("Schedule:" prefix / "-tick"
        // suffix). When automated, the row draws a quiet "automated" glyph and
        // a fainter title so real conversations stand out.
        bool automated = is_automated(s.title);
        Glyph glyph = automated ? Glyph::Automated : glyph_for(s);

        // ---- STABLE row hover (fixes the "star hover flashes the whole row")
        // The trailing star is a real clickable child of the row, and hot is a
        // single entity: the moment the pointer crosses from the row body onto
        // the star, hot flips to the star, is_hot(row) goes false, and the row
        // drops its hover wash for exactly those frames — the whole row flashes.
        // The wash is therefore baked into the row's BASE fill, asking about the
        // whole subtree rather than the row entity alone, so the fill is
        // identical no matter which of {row, star} currently owns hot.
        // A selected row always wins; an unhovered unselected row stays plain.

        // Denser rows: 24px tall with tight vertical padding, so more threads
        // fit — matching the mock's compact sidebar feel. Rows are indented
        // (left pad) to sit under their folder header, mirroring the mock's
        // indented .folder-body (padding-left 14 + margin-left 13).
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(2), .right = pixels(8),
                                      .bottom = pixels(2),
                                      .left = pixels(kRowLeftInset)})
                .with_custom_background(selected ? theme::selected_bg()
                                                 : theme::sidebar_bg())
                // Selected row = no hover reaction (hover bg == selected fill);
                // only an unselected row gets the subtle hover wash.
                .with_custom_hover_bg(selected ? theme::selected_bg()
                                               : theme::hover_over(
                                                     theme::sidebar_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("chat_row"));

        // Bake the hover wash into the row's BASE fill whenever the pointer is
        // anywhere in the row's subtree, so the fill never flickers as hot moves
        // between the row and its star (see note above). Selected always wins.
        {
            const bool rowHot = ctx.mouse_in_subtree(row.ent().id) ||
                                ctx.mouse_was_in_subtree(row.ent().id);
            if (!selected && rowHot) {
                if (row.ent().has<afterhours::HasColor>())
                    row.ent()
                        .get<afterhours::HasColor>()
                        .set(theme::hover_over(theme::sidebar_bg()));
            }
        }

        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        // Defer the open-thread decision: a click on the STAR (a child button)
        // also registers as a click on the row, so opening here would both star
        // AND open the thread (the star appeared "not working" because the
        // thread opened over it). Capture the row-click now, but only actually
        // open below IF the star wasn't the thing clicked this frame.
        bool rowClicked = row.ent().get<afterhours::ui::HasClickListener>().down;
        bool starClicked = false;

        // Status glyph slot: a small transparent box whose foreground draw
        // paints the shape-per-status glyph. Nothing is drawn when calm.
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(12), pixels(20)})
                .with_transparent_bg()
                .with_font_size(FontSize::Small)
                .with_roundness(0.0f)
                .with_on_draw_fg([glyph](RectangleType rect) {
                    draw_glyph(rect, glyph);
                })
                .with_debug_name("row_glyph"));

        // Title color (V5): a normal thread row's title uses the SAME token as
        // the VIEWS-section rows (Home/Blocked/Review/Starred/Archived), which
        // render their label in text_secondary() when unselected (see
        // smart_item's `txt = active ? primary : secondary`). Earlier this file
        // dimmed running/parked rows to text_faint(), which is darker than the
        // VIEWS token — so the thread list read noticeably darker than the
        // views above it. Unify the base to text_secondary() so the thread
        // titles match the VIEWS rows exactly. Attention rows still brighten to
        // primary (the mock's bold-on-attention intent). Only the DELIBERATELY
        // de-emphasized families stay faint: archived (a low-signal, separate
        // smart view) and automated/cron rows (defect #5 — quiet metadata).
        theme::Color titleColor = theme::text_secondary();
        if (attn) titleColor = theme::text_primary();
        else if (archived) titleColor = theme::text_faint();
        // Defect #5: automated/cron rows always read as quiet metadata — a
        // faint title — so real conversations stand out even inside a bucket.
        // (Applied last so it de-emphasizes regardless of the state above.)
        if (automated) titleColor = theme::text_faint();

        // Title height matches the row's content box (row 24 − top/bottom
        // pad 2 = 20) so the label's own vertical-centering lands the text on
        // the row's true center — otherwise a child shorter than the content
        // box lets fontstash's ascent/descent asymmetry push the glyphs low,
        // and the (full-row) highlight bg looks off-center against the text.
        //
        // Width is in PIXELS, not percent(1.0): afterhours has no flex-grow
        // (afterhours_gaps.md #18), so a percent(1.0f) title in this NoWrap
        // row means 100% of the row and would overflow past the glyph + time +
        // star siblings every frame. The row content box = panelW − row pad
        // (left 22 + right 8) = panelW − 30. Columns, in order:
        //   glyph slot   12px  (leading status indicator — always drawn)
        //   title        (flex-ish, sized in px — takes the remaining width)
        //   time slot    kRowTimeColW (trailing relative-time)
        //   star slot    18px  (RIGHTMOST — right-aligned per Gabe; renders on
        //                       hover / when starred, but ALWAYS reserved so
        //                       the row doesn't reflow on hover)
        //
        // WIDTH MATH / no-overflow (defect: layout-warn spam + solve_violations
        // churn). The columns are fixed-px in a NoWrap row, so if
        // glyph+title+star+time ever exceeds the row content box the row
        // overflows EVERY frame — afterhours logs a wrap/overflow warning AND
        // runs solve_violations (up to 10 iters) trying to resolve it, a real
        // per-frame drag. This hit hard at narrow widths and all through the
        // collapse tween (280→52 sweeps every intermediate width). So instead
        // of a fixed reserve + a title floor that can exceed the box, we FIT
        // the columns to the box: keep the title at a sane minimum and DROP the
        // optional trailing columns (time first, then star) when they wouldn't
        // fit, rather than overflow. Below the floor even without them, the
        // title simply shrinks (it can't overflow — it's the only flex column).
        const float kRowPad = kRowLeftInset + 8.0f;  // row left + right 8
        const float kStarW = 18.0f;       // trailing star slot
        const float kTitleMin = 40.0f;    // title floor before dropping columns
        float rowContent = panelW - kRowPad;
        if (rowContent < kGlyphW + 10.0f) rowContent = kGlyphW + 10.0f;
        // The trailing time column is sized to the ACTUAL rendered width of this
        // row's label (not a fixed 46px box). A fixed right-aligned box put the
        // label flush to the row's right edge but left the STAR — which sits to
        // the label's left — pinned to the box's LEFT edge, ~24px of dead gap
        // between the star and "now" (Gabe: "star is still not in the right
        // spot"). Measuring the label means the star hugs the text. The title
        // flex column absorbs the slack, so every timestamp still right-aligns
        // to the same edge. Capped at kRowTimeColW so a long date can't blow the
        // layout; a tiny pad keeps the star glyph off the digits.
        const int64_t nowSecsPre = static_cast<int64_t>(std::time(nullptr));
        const std::string ageLabelPre = row_time_label(s.updated_at, nowSecsPre);
        float timeW = ageLabelPre.empty()
                          ? 0.0f
                          : theme::text_px(ageLabelPre, theme::type::SM) + 2.0f;
        if (timeW > kRowTimeColW) timeW = kRowTimeColW;
        // Greedily reserve trailing columns only while the title can still hold
        // its floor. Time is the first to go, then the star (matches "drop the
        // least essential column at narrow widths"). The DROP decision uses the
        // max column width (kRowTimeColW) so it's conservative; the actual
        // reserve uses the measured timeW so the title fills the freed space.
        bool showTime = !ageLabelPre.empty() &&
                        (rowContent - kGlyphW - kTitleMin) >= kRowTimeColW;
        bool showStar =
            (rowContent - kGlyphW - kTitleMin -
             (showTime ? kRowTimeColW : 0.0f)) >= kStarW;
        float reserved = kGlyphW + (showTime ? timeW : 0.0f) +
                         (showStar ? kStarW : 0.0f);
        float rowTitleW = rowContent - reserved;
        if (rowTitleW < 16.0f) rowTitleW = 16.0f;  // never zero/negative
        // Ellipsize to the title column's width. At ROW size (12.5px) an avg
        // proportional glyph is ~6.0px; budget at /6.1 (was /6.6, which
        // under-counted ~8% and clipped titles a couple chars early even though
        // the column had room). The label widget also hard-clips at its pixel
        // width, so a hair-generous char budget just lets the text use the full
        // column instead of ellipsizing before it needs to.
        size_t titleChars = static_cast<size_t>(rowTitleW / 6.1f);
        if (titleChars < 4) titleChars = 4;
        if (titleChars > 48) titleChars = 48;
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(strip_parked_prefix(s.title),
                                               titleChars))
                .with_size(ComponentSize{pixels(rowTitleW), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(titleColor)
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("row_title"));

        // Per-row relative timestamp: a small, faint, right-aligned age
        // ("2h","3d","Jul 28"). Now the RIGHTMOST column (star sits to its
        // left â star/time swapped per Gabe). Fixed-width slot sized in
        // pixels (gap #18). Empty label (unknown/future updated_at) => the slot
        // renders blank but still reserves its width, keeping row layout stable.
        // Star affordance: shown when the row is HOVERED, or always when the
        // thread is already starred (so starred state stays visible at rest).
        // Uses was_hot (previous frame's hot id) since the current frame's hot
        // state isn't resolved until after this render pass. A starred row shows
        // a filled accent star; a hovered-unstarred row shows a faint hollow
        // star to toggle; an unhovered-unstarred row shows nothing.
        // Subtree, not the row entity alone: the star child owns hot while the
        // pointer is on it, and the affordance must not blink out from under
        // the cursor.
        bool rowHovered = ctx.mouse_was_in_subtree(row.ent().id) ||
                          ctx.mouse_in_subtree(row.ent().id) ||
                          // Test-only: force one row's hover (e.g. to capture
                          // the star-on-hover affordance headlessly). No-op
                          // unless HANABI_TEST_HOVER=row:<sessionId>.
                          hanabi::test_hooks::force_hover("row:" + s.id);

        // Trailing relative-time column FIRST (so it sits to the LEFT of the
        // ---- M5: star sits to the LEFT of the timestamp, both right-aligned.
        // Render the STAR first, then the time column — in a Row, earlier
        // children lay out further left, so this yields  title … [star] [time]
        // with the timestamp flush to the row's right edge and the star just
        // to its left. (Previously time-then-star put the star rightmost.)
        if (showStar && (s.starred || rowHovered)) {
            theme::Color starColor =
                s.starred ? theme::tag_ready_fg() : theme::text_faint();
            std::string sid = s.id;
            auto star = button(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(18), pixels(20)})
                    // No background of its own (and no hover-bg box): the star
                    // is a bare affordance that sits in the row, NOT a boxed
                    // button. transparent lets the row's own fill show through
                    // so only the glyph is visible. skip_hover_override keeps
                    // even that transparent fill from being tinted on hover.
                    .with_transparent_bg()
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_on_draw_fg([starColor, st = s.starred](RectangleType r) {
                        // Star sits toward the right of its slot but with a
                        // deliberate GAP before the timestamp column to its
                        // right (Gabe: "add padding between the star and time").
                        // ~13px in from the slot's right edge leaves a clean
                        // gap between the star glyph and the time digits.
                        const float cx = r.x + r.width - 13.0f;
                        const float cy = r.y + r.height * 0.5f - 1.0f;
                        if (!hanabi::icons::draw_at(
                                "star", cx, cy, 12.0f, starColor)) {
                            afterhours::draw_text(st ? "\xe2\x98\x85"
                                                     : "\xe2\x98\x86",
                                                  cx - 6.0f, cy - 6.0f, 12.0f,
                                                  starColor);
                        }
                    })
                    .with_debug_name("row_star"));
            // Keep the star's own fill from ever washing on hover (belt +
            // suspenders with transparent_bg): the row carries the hover wash.
            if (star.ent().has<afterhours::HasColor>())
                star.ent().get<afterhours::HasColor>().skip_hover_override =
                    true;
            if (star) {
                app.requestToggleStar = sid;
                starClicked = true;  // suppress the row's open-thread this frame
            }
        } else if (showStar) {
            // Reserve the star slot even when no star is shown, so the row's
            // trailing columns stay put (no reflow on hover). This blank slot
            // is a plain div (no HasClickListener), so it never steals hot from
            // the row — only the live star button (above) does.
            div(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(18), pixels(20)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("row_star_slot"));
        }

        // Trailing relative-time column LAST → the RIGHTMOST column ("2h","3d",
        // "Jul 28"), right-aligned, with the star immediately to its left (M5).
        // Column sized to the MEASURED label width (timeW) so the star hugs the
        // text instead of a fixed box's left edge. Dropped at very narrow widths
        // (showTime).
        const int64_t nowSecs = static_cast<int64_t>(std::time(nullptr));
        std::string ageLabel = row_time_label(s.updated_at, nowSecs);
        if (showTime)
        div(ctx, mk(row.ent(), 4),
            ComponentConfig{}
                .with_label(ageLabel)
                .with_size(ComponentSize{pixels(timeW), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("row_time"));

        // Apply the deferred row-open: open the thread on a row click UNLESS the
        // star was what got clicked (starring must not also open the thread).
        if (rowClicked && !starClicked) {
            app.requestOpenTab = s.id;
        }
    }
};

}  // namespace ecs
