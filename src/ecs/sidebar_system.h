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
#include <string>

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
        if (!folded) render_search(ctx, panel.ent(), *app);
        render_smart_views(ctx, panel.ent(), *app, folded);

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
        shown += render_folder(ctx, scroll.ent(), 10, "Stars", "stars", *app, q);
        shown += render_folder(ctx, scroll.ent(), 20, "Oncall", "oncall", *app,
                               q);
        shown += render_folder(ctx, scroll.ent(), 30, "Experiments",
                               "experiments", *app, q);
        shown += render_folder(ctx, scroll.ent(), 40, "Recent", "recent", *app,
                               q);
        // Low-signal archived section, greyed.
        shown += render_folder(ctx, scroll.ent(), 50, "Archived", "__archived__",
                               *app, q, /*archived=*/true);

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
    // ---- text helpers ----
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
    // The compact sidebar rows no longer carry a text tag chip. Instead each
    // attention-worthy row gets a small SHAPE-per-status glyph at its left,
    // so status is readable by SHAPE (not color alone), mirroring the mock:
    //   Blocked / needs-you -> RED UP-TRIANGLE  (most urgent)
    //   Review (agent-verified) -> GREEN DIAMOND (square rotated 45 deg)
    //   Done -> BLUE DOT (filled circle)
    //   working / parked / archived -> NO glyph (calm)
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
    // so the three statuses are visually distinct by SHAPE, not just color.
    static void draw_glyph(RectangleType rect, Glyph g) {
        if (g == Glyph::None) return;
        const theme::Color c = glyph_color(g);
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
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

    // ---- header ----
    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       LayoutComponent& layout, bool folded) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(40)})
                .with_flex_direction(folded ? FlexDirection::Column
                                            : FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(9), .right = pixels(8),
                                      .bottom = pixels(5), .left = pixels(14)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_header"));

        if (!folded) {
            // Brand mark (sprite) + name. The ✦ mark is now a Lucide "sparkle"
            // sprite drawn via on_draw_fg; the "hanabi" wordmark stays text.
            auto brand = div(ctx, mk(header.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(0.62f), pixels(24)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("sb_brand"));
            div(ctx, mk(brand.ent(), 1),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(18), pixels(20)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        "brand", "\xe2\x9c\xa6", theme::text_primary(), 15.0f,
                        -1.0f))
                    .with_debug_name("sb_brand_mark"));
            div(ctx, mk(brand.ent(), 2),
                ComponentConfig{}
                    .with_label("hanabi")
                    .with_size(ComponentSize{pixels(120), pixels(24)})
                    .with_padding(Padding{.left = pixels(4)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("sb_brand_name"));

            // New task → open the composer (Phase K composer system renders it).
            auto newBtn = button(ctx, mk(header.ent(), 2),
                icon_btn_sprite("plus", "+").with_debug_name("sb_new"));
            if (newBtn) {
                if (auto* app = find_singleton<AppComponent>())
                    app->composerOpen = true;
            }

            // Settings → open the settings overlay (Phase K settings system).
            auto setBtn = button(ctx, mk(header.ent(), 3),
                icon_btn_sprite("gear", "\xe2\x9a\x99")
                    .with_debug_name("sb_settings"));
            if (setBtn) {
                if (auto* app = find_singleton<AppComponent>())
                    app->showSettings = true;
            }
        }

        // Collapse / expand toggle (present in both states).
        auto collapseBtn = button(ctx, mk(header.ent(), 4),
            icon_btn_sprite(folded ? "sidebar_open" : "sidebar_close",
                            folded ? "\xc2\xbb" : "\xc2\xab")
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

    // Sprite-icon button: same 26x26 chrome button as icon_btn, but instead of
    // a text glyph it blits the Lucide atlas sprite `name` (tinted) via
    // on_draw_fg, keeping the widget label empty. `fallback_glyph` is the
    // legacy unicode text drawn only if the atlas fails to load. Routed through
    // icons::draw_fg so a future icon-source swap is localized.
    static ComponentConfig icon_btn_sprite(const std::string& name,
                                           const std::string& fallback_glyph) {
        return ComponentConfig{}
            .with_label(" ")
            .with_size(ComponentSize{pixels(26), pixels(26)})
            .with_custom_background(theme::sidebar_bg())
            .with_custom_hover_bg(theme::hover_bg())
            .with_click_activation(ClickActivationMode::Press)
            .with_roundness(0.3f)
            .with_on_draw_fg(hanabi::icons::draw_fg(
                name, fallback_glyph, theme::text_secondary(), 16.0f));
    }

    // ---- search (unfolded only) ----
    void render_search(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app) {
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
        // so we let it FILL the pill's remaining width (percent 1.0) — that way
        // its inner surface reads as the whole field instead of a nested box.
        bool hasQuery = !app.searchQuery.empty();
        afterhours::text_input::text_input(
            ctx, mk(field.ent(), 2), app.searchQuery,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
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
            // Apply to every folder key so all fold/unfold in lockstep.
            static const char* kKeys[] = {"stars", "oncall", "experiments",
                                          "recent", "__archived__"};
            if (app.foldAllFolders) {
                for (const char* k : kKeys) app.collapsedFolders.insert(k);
            } else {
                app.collapsedFolders.clear();
            }
        }
    }

    // ---- smart views ----
    void render_smart_views(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app, bool folded) {
        // "VIEWS" section label (unfolded only, per the mock).
        if (!folded) section_label(ctx, parent, 25, "VIEWS");

        auto container = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(148)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(0), .right = pixels(8),
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
                   SmartView::Home, -1, app, folded);
        smart_item(ctx, container.ent(), 2, "blocked", "\xe2\x9b\x94",
                   "Blocked", SmartView::Blocked, blocked, app, folded);
        smart_item(ctx, container.ent(), 3, "review", "\xe2\x9c\x93", "Review",
                   SmartView::Review, review, app, folded);
        smart_item(ctx, container.ent(), 4, "star", "\xe2\x98\x85", "Starred",
                   SmartView::Starred, starred, app, folded);
    }

    void smart_item(UIContext<InputAction>& ctx, Entity& parent, int idx,
                    const std::string& icon_name,
                    const std::string& fallback_glyph,
                    const std::string& label, SmartView view, int count,
                    AppComponent& app, bool folded) {
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

        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{percent(0.72f), pixels(22)})
                .with_padding(Padding{.left = pixels(10)})
                .with_transparent_bg()
                .with_custom_text_color(txt)
                .with_font_size(theme::type::BODY)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sv_label"));

        if (count > 0) {
            div(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(std::to_string(count))
                    .with_size(ComponentSize{pixels(24), pixels(22)})
                    .with_transparent_bg()
                    .with_custom_text_color(active ? theme::text_primary()
                                                   : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("sv_count"));
        }
    }

    // ---- folder group ----
    // Renders a collapsible folder. Returns the number of chat rows actually
    // rendered (used by the caller to drive the search no-results state).
    // `q` is the already-lowercased search query ("" = no filter).
    int render_folder(UIContext<InputAction>& ctx, Entity& parent, int base,
                      const std::string& name, const std::string& key,
                      AppComponent& app, const std::string& q,
                      bool archived = false) {
        // Collect member threads, honoring the live search filter.
        std::vector<const api::SessionSummary*> members;
        for (const auto& s : app.sessions) {
            bool match = archived
                             ? (s.state == api::ThreadState::Archived)
                             : (s.folder == key &&
                                s.state != api::ThreadState::Archived);
            if (match && title_matches(s.title, q)) members.push_back(&s);
        }
        // Hide a folder with no (matching) members. With an active query this
        // is what drops non-matching folders out of the tree.
        if (members.empty()) return 0;

        bool collapsed = app.collapsedFolders.count(key) > 0;
        // A live search overrides collapse: matches must be visible, so a
        // matching folder auto-expands while filtering (mirrors the mock, where
        // search results are always shown regardless of prior fold state).
        if (!q.empty()) collapsed = false;

        theme::Color headColor =
            archived ? theme::text_faint() : theme::text_secondary();

        // Folder header (clickable: toggles this folder's collapse state).
        auto head = div(ctx, mk(parent, base),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(26)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(4), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(10)})
                .with_custom_background(theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_bg())
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.3f)
                .with_debug_name("folder_head"));

        // Clicking the header toggles collapse for this folder key. Disabled
        // while a query is active (results stay pinned open).
        head.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (q.empty() && head.ent().get<afterhours::ui::HasClickListener>().down) {
            if (app.collapsedFolders.count(key))
                app.collapsedFolders.erase(key);
            else
                app.collapsedFolders.insert(key);
            // Any explicit per-folder toggle drops out of "fold all" mode.
            app.foldAllFolders = false;
        }

        // Folder chevron: a shape triangle (down = expanded, right = collapsed)
        // drawn via on_draw_fg — the atlas has no chevron-right, so rotating a
        // drawn triangle is the crisp, dependency-free way to show both states.
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
        div(ctx, mk(head.ent(), 4),
            ComponentConfig{}
                .with_label(name)
                .with_size(ComponentSize{percent(0.8f), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(headColor)
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("folder_name"));
        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(std::to_string(members.size()))
                .with_size(ComponentSize{pixels(24), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("folder_count"));

        // Collapsed: header only, no body rows (but the folder + its matches
        // still counted so the header is visible).
        if (collapsed) return static_cast<int>(members.size());

        int i = 0;
        for (const auto* s : members) {
            render_chat_row(ctx, parent, base + 1 + (++i), *s, app, archived);
        }
        return static_cast<int>(members.size());
    }

    // ---- high-signal chat row ----
    void render_chat_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const api::SessionSummary& s, AppComponent& app,
                         bool archived) {
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
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(s.title, 34))
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(titleColor)
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("row_title"));

        // Star affordance: shown when the row is HOVERED, or always when the
        // thread is already starred (so starred state stays visible at rest).
        // Uses was_hot (previous frame's hot id) since the current frame's hot
        // state isn't resolved until after this render pass. A starred row shows
        // a filled accent star; a hovered-unstarred row shows a faint hollow
        // star to toggle; an unhovered-unstarred row shows nothing.
        bool rowHovered = ctx.was_hot(row.ent().id) ||
                          ctx.is_hot(row.ent().id);
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
