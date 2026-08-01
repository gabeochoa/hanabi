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
#include <vector>

#include "../test_hooks.h"
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

        // Apply a pending star-toggle request (set by a row's star affordance).
        // The mutation lives HERE so this owned system is the single writer of
        // the sessions vector's starred flag — flipping it updates the Starred
        // smart-view count + membership immediately, on the very next render.
        if (!app->requestToggleStar.empty()) {
            for (auto& s : app->sessions) {
                if (s.id == app->requestToggleStar) {
                    s.starred = !s.starred;
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
        // header(40) + search(40) + VIEWS label(25) + views(148).
        float used = 40.0f + 40.0f + 25.0f + 148.0f;
        float scrollH = r.height - used;
        if (scrollH < 40.0f) scrollH = 40.0f;
        auto scroll = div(ctx, mk(panel.ent(), 5),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(scrollH)})
                .with_custom_background(theme::sidebar_bg())
                .with_debug_name("sidebar_scroll"));

        // "FOLDERS" section label + fold-all control (mirrors the mock's
        // second section header, which carries a fold-all affordance).
        folders_section_head(ctx, scroll.ent(), 4, *app);

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
        shown += render_folder(ctx, scroll.ent(), 1000, "Stars", "stars", *app,
                               q, r.width);
        shown += render_folder(ctx, scroll.ent(), 2000, "Oncall", "oncall",
                               *app, q, r.width);
        shown += render_folder(ctx, scroll.ent(), 3000, "Experiments",
                               "experiments", *app, q, r.width);
        // "Recent" is the catch-all: it holds the "recent" folder AND any
        // session whose folder doesn't match a known folder (folder="" or an
        // unrecognized value). This is what makes a real backend usable —
        // real sessions come back unfoldered, so without a catch-all they'd
        // match no folder and the sidebar would look empty even with the list
        // loaded. Archived sessions are excluded (they live in the low-signal
        // Archived section below).
        shown += render_folder(ctx, scroll.ent(), 4000, "Recent", "recent",
                               *app, q, r.width, /*archived=*/false,
                               /*catchAll=*/true);
        // Low-signal archived section, greyed.
        shown += render_folder(ctx, scroll.ent(), 6000, "Archived",
                               "__archived__", *app, q, r.width,
                               /*archived=*/true);

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
    // Per-row trailing relative-time column width ("2h" / "Jul 28"). Wide
    // enough for a short absolute date so old rows aren't clipped; kept small
    // so the title still gets most of the row.
    static constexpr float kRowTimeColW = 46.0f;
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

    // ---- time-based grouping for the un-foldered "Recent" catch-all ----
    // Real backends return sessions with no folder and mostly-calm state, so a
    // flat "Recent (102)" catch-all isn't scannable. Worse: on a real backend
    // ~all sessions are older than a week, so a coarse Today/This-Week/Earlier
    // split dumped ~100 rows into ONE flat "Earlier (100)" bucket — still not
    // scannable. So we bucket into the finer chat-app-standard set:
    //   Today / Yesterday / Previous 7 Days / Previous 30 Days / Older
    // Each non-empty bucket is its own collapsible dated section, so a big
    // backlog breaks into several scannable groups. The classification is a
    // pure, now-injected function so it is deterministic (updated_at is unix
    // epoch seconds; 0 = unknown). Kept local to this owned file.
    enum class TimeBucket { Today, Yesterday, PrevWeek, PrevMonth, Older };

    // Classify `updated_at` relative to `now` (both unix epoch seconds). The
    // day boundaries are LOCAL-calendar based (so "Today"/"Yesterday" match the
    // user's wall clock), and the wider buckets are rolling day-count windows
    // measured from local midnight so a session doesn't drift between buckets
    // within a day:
    //   Today      = same LOCAL calendar day as `now`
    //   Yesterday  = the local calendar day before today
    //   PrevWeek   = 2..7 days before today (this week's earlier days)
    //   PrevMonth  = 8..30 days before today
    //   Older      = >30 days, in the future (clock skew), or unknown(0)
    static TimeBucket time_bucket(int64_t updated_at, int64_t now) {
        if (updated_at <= 0) return TimeBucket::Older;   // unknown
        if (updated_at > now) return TimeBucket::Older;  // future / skew
        // Whole-day difference between the two LOCAL calendar dates. We compare
        // by local midnight of each so DST / partial days don't misbucket.
        const int64_t day = 24 * 60 * 60;
        std::time_t nt = static_cast<std::time_t>(now);
        std::time_t ut = static_cast<std::time_t>(updated_at);
        std::tm ntm{};
        std::tm utm{};
        localtime_r(&nt, &ntm);
        localtime_r(&ut, &utm);
        // Local midnight (00:00:00) of each date.
        std::tm nmid = ntm;
        nmid.tm_hour = nmid.tm_min = nmid.tm_sec = 0;
        std::tm umid = utm;
        umid.tm_hour = umid.tm_min = umid.tm_sec = 0;
        std::time_t n0 = std::mktime(&nmid);
        std::time_t u0 = std::mktime(&umid);
        int64_t days = static_cast<int64_t>((n0 - u0) / day);
        if (days <= 0) return TimeBucket::Today;
        if (days == 1) return TimeBucket::Yesterday;
        if (days <= 7) return TimeBucket::PrevWeek;
        if (days <= 30) return TimeBucket::PrevMonth;
        return TimeBucket::Older;
    }

    // Stable display label + collapse key per bucket. The key drives the
    // collapsed-set membership and must not collide with named-folder keys
    // (stars/oncall/experiments/recent/__archived__).
    static const char* time_bucket_label(TimeBucket b) {
        switch (b) {
            case TimeBucket::Today: return "Today";
            case TimeBucket::Yesterday: return "Yesterday";
            case TimeBucket::PrevWeek: return "Previous 7 Days";
            case TimeBucket::PrevMonth: return "Previous 30 Days";
            case TimeBucket::Older: return "Older";
        }
        return "Older";
    }
    static const char* time_bucket_key(TimeBucket b) {
        switch (b) {
            case TimeBucket::Today: return "__t_today__";
            case TimeBucket::Yesterday: return "__t_yesterday__";
            case TimeBucket::PrevWeek: return "__t_week__";
            case TimeBucket::PrevMonth: return "__t_month__";
            case TimeBucket::Older: return "__t_older__";
        }
        return "__t_older__";
    }

    // Compact right-aligned per-row timestamp derived from `updated_at`
    // (relative to `now`). Recent rows read as a relative age ("now","5m",
    // "3h","2d","4w"); anything older than the finest week bucket reads as an
    // absolute short date ("Jul 28") so a year-old row isn't a giant "58w".
    // Returns "" for an unknown (0) or future timestamp so the slot stays
    // blank rather than lying. Pure + now-injected for headless testing.
    static std::string row_time_label(int64_t updated_at, int64_t now) {
        if (updated_at <= 0 || updated_at > now) return "";
        int64_t secs = now - updated_at;
        const int64_t day = 24 * 60 * 60;
        if (secs < 60) return "now";
        if (secs < 60 * 60) return std::to_string(secs / 60) + "m";
        if (secs < day) return std::to_string(secs / 3600) + "h";
        if (secs < 7 * day) return std::to_string(secs / day) + "d";
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

    // ASCII-lowercase a copy (search is case-insensitive; titles are UTF-8 but
    // case-folding only the ASCII range is sufficient for these labels).
    static std::string lower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
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
    //   working / parked / archived / calm -> small FAINT neutral dot (calm)
    using Glyph = ecs::model::Glyph;

    // Precedence follows the mock's JS ordering: blocked, then review, then
    // done, then a bare Attention state (waiting-on-you) which also earns the
    // urgent triangle. (Pure logic lives in ecs::model::glyph_for.)
    static Glyph glyph_for(const api::SessionSummary& s) {
        return ecs::model::glyph_for(s);
    }

    static theme::Color glyph_color(Glyph g) {
        switch (g) {
            case Glyph::Triangle: return theme::tag_blocked_fg();  // red
            case Glyph::Diamond: return theme::tag_ready_fg();     // green
            case Glyph::Dot: return theme::tag_done_fg();          // blue
            default: return theme::text_faint();
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
    //   Dot 2.4px(faint) calm  (working / parked / archived / no signal)
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
            default:
                break;
        }
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
                .with_align_items(AlignItems::Center)
                .with_justify_content(folded ? JustifyContent::FlexStart
                                             : JustifyContent::SpaceBetween)
                .with_padding(Padding{.top = pixels(7), .right = pixels(8),
                                      .bottom = pixels(5), .left = pixels(14)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_header"));

        if (!folded) {
            // --- brand block (left-anchored): ✦ mark + "hanabi" wordmark ---
            // Fixed-width so SpaceBetween has real slack to distribute; the
            // ✦ mark is a Lucide "sparkle" sprite (on_draw_fg), the wordmark
            // is text. Sized to just hug its content so the right cluster
            // anchors to the sidebar's right edge.
            auto brand = div(ctx, mk(header.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(96), pixels(26)})
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
            div(ctx, mk(brand.ent(), 2),
                ComponentConfig{}
                    .with_label("hanabi")
                    .with_size(ComponentSize{pixels(72), pixels(24)})
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
                    .with_size(ComponentSize{pixels(92), pixels(28)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_justify_content(JustifyContent::FlexEnd)
                    .with_gap(pixels(3))
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("sb_actions"));

            // New task → open the composer (Phase K composer system renders it).
            auto newBtn = button(ctx, mk(actions.ent(), 1),
                icon_btn_sprite("plus", "+").with_debug_name("sb_new"));
            if (newBtn) {
                if (auto* app = find_singleton<AppComponent>())
                    app->composerOpen = true;
            }

            // Settings → open the settings overlay (Phase K settings system).
            auto setBtn = button(ctx, mk(actions.ent(), 2),
                icon_btn_sprite("gear", "\xe2\x9a\x99")
                    .with_debug_name("sb_settings"));
            if (setBtn) {
                if (auto* app = find_singleton<AppComponent>())
                    app->showSettings = true;
            }

            // Collapse toggle joins the cluster (unfolded state).
            auto collapseBtn = button(ctx, mk(actions.ent(), 3),
                icon_btn_sprite("sidebar_close", "\xc2\xab")
                    .with_debug_name("sb_collapse"));
            if (collapseBtn) {
                layout.sidebarCollapsed = !layout.sidebarCollapsed;
            }
            return;
        }

        // Folded rail: a single expand toggle in the header column.
        auto collapseBtn = button(ctx, mk(header.ent(), 4),
            icon_btn_sprite("sidebar_open", "\xc2\xbb")
                .with_debug_name("sb_collapse"));
        if (collapseBtn) {
            layout.sidebarCollapsed = !layout.sidebarCollapsed;
        }
    }

    static ComponentConfig icon_btn(const std::string& label) {
        return ComponentConfig{}
            .with_label(label)
            .with_size(ComponentSize{pixels(26), pixels(26)})
            .with_custom_background(theme::sidebar_bg())
            .with_custom_hover_bg(theme::hover_bg())
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
            .with_custom_hover_bg(theme::hover_bg())
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
        // editable text_input, so the icon sits in the left gutter and the
        // typed query flows after it. The input is bound directly to
        // app.searchQuery (afterhours' text_input syncs the std::string), so
        // the folder tree filters live as the user types.
        auto field = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(30)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(5), .right = pixels(8),
                                      .bottom = pixels(5), .left = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_roundness(0.3f)
                .with_debug_name("sb_search"));
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
        if (searchTextW < 40.0f) searchTextW = 40.0f;
        afterhours::text_input::text_input(
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

        // Clear affordance (only when a query is present): an ✕ that empties
        // the query and restores the full tree.
        if (hasQuery) {
            auto clr = button(ctx, mk(field.ent(), 3),
                ComponentConfig{}
                    .with_label("\xc3\x97")
                    .with_size(ComponentSize{pixels(18), pixels(20)})
                    .with_transparent_bg()
                    .with_custom_hover_bg(theme::hover_bg())
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::LG)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.3f)
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
                              int base, AppComponent& app) {
        auto head = div(ctx, mk(parent, base),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(6), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(14)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_folders_head"));

        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label("FOLDERS")
                .with_size(ComponentSize{percent(0.72f), pixels(16)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::LABEL)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sb_folders_label"));

        // Fold-all button (chevrons-down-up sprite). Its tint brightens when
        // all folders are currently folded, echoing the mock's active state.
        theme::Color foldTint =
            app.foldAllFolders ? theme::text_secondary() : theme::text_faint();
        auto foldBtn = button(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(20), pixels(18)})
                .with_custom_background(theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_bg())
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.3f)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "fold_all", "\xe2\x87\x85", foldTint, 14.0f, -1.0f))
                .with_debug_name("sb_fold_all"));
        if (foldBtn) {
            app.foldAllFolders = !app.foldAllFolders;
            // Apply to every folder key so all fold/unfold in lockstep. The
            // Recent catch-all renders as time-group headers (Today /
            // Yesterday / Previous 7/30 Days / Older) rather than a single
            // "recent" header, so fold-all targets those bucket keys too.
            static const char* kKeys[] = {
                "stars",           "oncall",       "experiments",
                "__t_today__",     "__t_yesterday__", "__t_week__",
                "__t_month__",     "__t_older__",  "__archived__"};
            if (app.foldAllFolders) {
                for (const char* k : kKeys) app.collapsedFolders.insert(k);
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
                .with_size(ComponentSize{percent(1.0f), pixels(148)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(0), .right = pixels(4),
                                      .bottom = pixels(2), .left = pixels(8)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("smart_views"));

        int review = 0, starred = 0, blocked = blocked_count(app);
        for (const auto& s : app.sessions) {
            if (s.state == api::ThreadState::Ready) ++review;
            if (s.starred) ++starred;
        }

        smart_item(ctx, container.ent(), 1, "home", "\xe2\x8c\x82", "Home",
                   SmartView::Home, -1, app, folded, panelW);
        smart_item(ctx, container.ent(), 2, "blocked", "\xe2\x9b\x94",
                   "Blocked", SmartView::Blocked, blocked, app, folded, panelW);
        smart_item(ctx, container.ent(), 3, "review", "\xe2\x9c\x93", "Review",
                   SmartView::Review, review, app, folded, panelW);
        smart_item(ctx, container.ent(), 4, "star", "\xe2\x98\x85", "Starred",
                   SmartView::Starred, starred, app, folded, panelW);
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
                .with_padding(Padding{.top = pixels(4), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(8)})
                .with_margin(Margin{.top = pixels(1), .right = pixels(0),
                                    .bottom = pixels(1), .left = pixels(0)})
                .with_custom_background(active ? theme::selected_bg()
                                               : theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_bg())
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
        auto iconDraw = hanabi::icons::draw_fg(icon_name, fallback_glyph, txt,
                                               16.0f, -1.0f);
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(folded ? 26 : 18), pixels(22)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([iconDraw, railDot, dotColor](
                                     RectangleType rect) {
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
        float svLabelW = panelW - 28.0f - 18.0f - kCountColW;
        if (svLabelW < 30.0f) svLabelW = 30.0f;
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

        if (count > 0) {
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
    static bool is_named_folder(const std::string& folder) {
        return folder == "stars" || folder == "oncall" ||
               folder == "experiments" || folder == "recent";
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
                      bool archived = false, bool catchAll = false) {
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
            if (match && title_matches(s.title, q)) members.push_back(&s);
        }
        // Hide a folder with no (matching) members. With an active query this
        // is what drops non-matching folders out of the tree.
        if (members.empty()) return 0;

        // The Recent catch-all groups its (mostly un-foldered) sessions by
        // time so a real backend's flat "Recent (102)" becomes scannable.
        // Named folders (stars/oncall/experiments) skip this and render as a
        // single flat group exactly as before.
        if (catchAll) {
            return render_time_groups(ctx, parent, base, members, app, q,
                                      panelW, archived);
        }

        return render_group(ctx, parent, base, name, key, members, app, q,
                            panelW, archived);
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
                .with_custom_hover_bg(theme::hover_bg())
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("folder_head"));

        // Clicking the header toggles collapse for this key. Disabled while a
        // query is active (results stay pinned open).
        head.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (q.empty() &&
            head.ent().get<afterhours::ui::HasClickListener>().down) {
            if (app.collapsedFolders.count(key))
                app.collapsedFolders.erase(key);
            else
                app.collapsedFolders.insert(key);
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
        // the 16px chevron.
        div(ctx, mk(head.ent(), 4),
            ComponentConfig{}
                .with_label(name)
                .with_size(ComponentSize{
                    pixels(label_col_w(panelW, 10.0f, 16.0f)), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(headColor)
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("folder_name"));
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
                     bool archived) {
        if (members.empty()) return 0;
        theme::Color headColor =
            archived ? theme::text_faint() : theme::text_secondary();
        bool collapsed = render_group_header(
            ctx, parent, base, name, key, static_cast<int>(members.size()),
            headColor, app, q, panelW);

        // Collapsed: header only, no body rows.
        if (collapsed) return static_cast<int>(members.size());

        int i = 0;
        for (const auto* s : members) {
            render_chat_row(ctx, parent, base + 1 + (++i), *s, app, archived,
                            panelW);
        }
        return static_cast<int>(members.size());
    }

    // ---- time-based grouping for the Recent catch-all ----
    // Splits `members` into Today / Yesterday / Previous 7 Days / Previous 30
    // Days / Older buckets by updated_at (relative to the system clock) and
    // renders each NON-EMPTY bucket as its own collapsible group, reusing the
    // folder-header + collapse machinery. This turns a real backend's flat
    // "Recent (100)" — which a coarse 3-bucket split would just re-pile into
    // one "Earlier (100)" — into several scannable dated sections. Buckets
    // render newest-first; within a bucket the order is whatever the members
    // vector already carries (newest-first from the backend sort). Each bucket
    // gets its own id block so entity ids never collide.
    int render_time_groups(
        UIContext<InputAction>& ctx, Entity& parent, int base,
        const std::vector<const api::SessionSummary*>& members,
        AppComponent& app, const std::string& q, float panelW, bool archived) {
        const int64_t now = static_cast<int64_t>(std::time(nullptr));

        // Partition, preserving each member's incoming (newest-first) order.
        std::vector<const api::SessionSummary*> today, yesterday, week, month,
            older;
        for (const auto* s : members) {
            switch (time_bucket(s->updated_at, now)) {
                case TimeBucket::Today: today.push_back(s); break;
                case TimeBucket::Yesterday: yesterday.push_back(s); break;
                case TimeBucket::PrevWeek: week.push_back(s); break;
                case TimeBucket::PrevMonth: month.push_back(s); break;
                case TimeBucket::Older: older.push_back(s); break;
            }
        }

        int shown = 0;
        // Each bucket is rendered as its own collapsible group. The id blocks
        // are spaced 300 apart (well above any realistic bucket row count) so
        // entity ids never collide across buckets or with the next folder's
        // base (folders are 1000 apart; 5 buckets * 300 = 1500 would exceed the
        // 1000 spacing, so time-groups get a WIDER 200-apart base range that
        // fits inside the Recent folder's 4000..4999 block — see note). We keep
        // each bucket to a 200-id slot; 5 * 200 = 1000, exactly the folder
        // spacing, and a bucket can hold ~199 rows before touching the next
        // slot (plenty for a real backlog).
        struct B {
            TimeBucket b;
            const std::vector<const api::SessionSummary*>* rows;
        };
        const B order[] = {{TimeBucket::Today, &today},
                           {TimeBucket::Yesterday, &yesterday},
                           {TimeBucket::PrevWeek, &week},
                           {TimeBucket::PrevMonth, &month},
                           {TimeBucket::Older, &older}};
        int slot = 0;
        for (const auto& e : order) {
            if (!e.rows->empty()) {
                shown += render_group(
                    ctx, parent, base + slot * 200, time_bucket_label(e.b),
                    time_bucket_key(e.b), *e.rows, app, q, panelW, archived);
            }
            ++slot;
        }
        return shown;
    }


    // ---- high-signal chat row ----
    // `panelW` is the live sidebar width (LayoutComponent::sidebar.width).
    void render_chat_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const api::SessionSummary& s, AppComponent& app,
                         bool archived, float panelW) {
        bool attn = is_attention(s.state);
        bool running = s.state == api::ThreadState::Running;
        bool parked = s.state == api::ThreadState::Parked;
        bool selected = app.selectedId == s.id;
        Glyph glyph = glyph_for(s);

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
                                      .bottom = pixels(2), .left = pixels(22)})
                .with_custom_background(selected ? theme::selected_bg()
                                                 : theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_bg())
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("chat_row"));

        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (row.ent().get<afterhours::ui::HasClickListener>().down) {
            app.requestOpenTab = s.id;
        }

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

        // Title. attn=bold(primary), running=dim(faint), parked/archived=grey.
        // Bold is approximated via the primary text color (immediate-mode UI
        // has no per-label weight); the mock's bold-on-attention intent is
        // preserved by the primary/secondary/faint color split.
        theme::Color titleColor = theme::text_secondary();
        if (attn) titleColor = theme::text_primary();
        else if (running || parked || archived) titleColor = theme::text_faint();

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
        // (left 22 + right 8) = panelW − 30. Reserve, in order:
        //   glyph slot   12px  (leading status indicator — always drawn)
        //   time slot    kRowTimeColW (trailing relative-time — always reserved)
        //   star slot    18px  (renders on hover / when starred, but ALWAYS
        //                       reserved so the row doesn't reflow on hover)
        // Title width = panelW − 30 − 12 − kRowTimeColW − 18, clamped to a sane
        // min so a narrow sidebar never goes negative. The time + star slots
        // are ADJACENT on the right (time left of star) and both fixed, so the
        // faint timestamp never collides with the star/glyph (gap #18 width
        // math — we size every column in pixels).
        float rowTitleW = panelW - 30.0f - 12.0f - kRowTimeColW - 18.0f;
        if (rowTitleW < 40.0f) rowTitleW = 40.0f;
        // Ellipsize to the title column's width. At ROW size a char is ~6.4px;
        // budget conservatively (÷6.6) so a long title truncates INSIDE its
        // column instead of bleeding under the timestamp. Clamp to a sane
        // range so a narrow sidebar still shows a few chars and a wide one
        // doesn't over-truncate.
        size_t titleChars = static_cast<size_t>(rowTitleW / 6.6f);
        if (titleChars < 8) titleChars = 8;
        if (titleChars > 40) titleChars = 40;
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(s.title, titleChars))
                .with_size(ComponentSize{pixels(rowTitleW), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(titleColor)
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("row_title"));

        // Per-row relative timestamp: a small, faint, right-aligned age
        // ("2h","3d","Jul 28") so a row is more than a bare title. Subtle by
        // design (SM size, faint token) so it reads as metadata, not a second
        // title. Fixed-width slot sized in pixels (gap #18) sitting just left
        // of the star slot, so it never collides with the star or the leading
        // glyph. Empty label (unknown/future updated_at) => the slot renders
        // blank but still reserves its width, keeping row layout stable.
        const int64_t nowSecs = static_cast<int64_t>(std::time(nullptr));
        std::string ageLabel = row_time_label(s.updated_at, nowSecs);
        div(ctx, mk(row.ent(), 4),
            ComponentConfig{}
                .with_label(ageLabel)
                .with_size(ComponentSize{pixels(kRowTimeColW), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("row_time"));

        // Star affordance: shown when the row is HOVERED, or always when the
        // thread is already starred (so starred state stays visible at rest).
        // Uses was_hot (previous frame's hot id) since the current frame's hot
        // state isn't resolved until after this render pass. A starred row shows
        // a filled accent star; a hovered-unstarred row shows a faint hollow
        // star to toggle; an unhovered-unstarred row shows nothing.
        bool rowHovered = ctx.was_hot(row.ent().id) ||
                          ctx.is_hot(row.ent().id) ||
                          // Test-only: force one row's hover (e.g. to capture
                          // the star-on-hover affordance headlessly). No-op
                          // unless HANABI_TEST_HOVER=row:<sessionId>.
                          hanabi::test_hooks::force_hover("row:" + s.id);
        if (s.starred || rowHovered) {
            theme::Color starColor =
                s.starred ? theme::tag_ready_fg() : theme::text_faint();
            std::string sid = s.id;
            auto star = button(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(18), pixels(20)})
                    .with_custom_background(selected ? theme::selected_bg()
                                                     : theme::sidebar_bg())
                    .with_custom_hover_bg(theme::hover_bg())
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.3f)
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        "star", s.starred ? "\xe2\x98\x85" : "\xe2\x98\x86",
                        starColor, 12.0f, -1.0f))
                    .with_debug_name("row_star"));
            if (star) {
                app.requestToggleStar = sid;
            }
        }
    }
};

}  // namespace ecs
