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

#include <string>

#include "../util/format.h"
#include "../ui/icons.h"
#include "thread_model.h"
#include "ui_imports.h"

namespace ecs {

struct SidebarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

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
        if (!folded) render_search(ctx, panel.ent());
        render_smart_views(ctx, panel.ent(), *app, folded);

        if (folded) return;  // rail stops after icon views

        // Scrollable region: folders + recent + archived.
        float used = 40.0f + 42.0f + 148.0f;  // header + search + views
        float scrollH = r.height - used;
        if (scrollH < 40.0f) scrollH = 40.0f;
        auto scroll = div(ctx, mk(panel.ent(), 5),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(scrollH)})
                .with_custom_background(theme::sidebar_bg())
                .with_debug_name("sidebar_scroll"));

        render_folder(ctx, scroll.ent(), 10, "Stars", "stars", *app);
        render_folder(ctx, scroll.ent(), 20, "Oncall", "oncall", *app);
        render_folder(ctx, scroll.ent(), 30, "Experiments", "experiments",
                      *app);
        render_folder(ctx, scroll.ent(), 40, "Recent", "recent", *app);
        // Low-signal archived section, greyed.
        render_folder(ctx, scroll.ent(), 50, "Archived", "__archived__", *app,
                      /*archived=*/true);
    }

  private:
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
                .with_padding(Padding{.top = pixels(8), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(10)})
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

            // New task.
            auto newBtn = button(ctx, mk(header.ent(), 2),
                icon_btn_sprite("plus", "+").with_debug_name("sb_new"));
            (void)newBtn;

            // Settings.
            auto setBtn = button(ctx, mk(header.ent(), 3),
                icon_btn_sprite("gear", "\xe2\x9a\x99")
                    .with_debug_name("sb_settings"));
            (void)setBtn;
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
    void render_search(UIContext<InputAction>& ctx, Entity& parent) {
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
        // Search field: a row-flex pill holding a magnifier sprite slot + the
        // "Search" placeholder text, so the icon sits in the left gutter and
        // the text flows after it (text-label padding alone doesn't offset the
        // glyph origin reliably here).
        auto field = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(30)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(6), .right = pixels(8),
                                      .bottom = pixels(6), .left = pixels(8)})
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
                    "search", "\xf0\x9f\x94\x8d", theme::text_faint(), 14.0f,
                    -1.0f))
                .with_debug_name("sb_search_icon"));
        div(ctx, mk(field.ent(), 2),
            ComponentConfig{}
                .with_label("Search")
                .with_size(ComponentSize{pixels(110), pixels(18)})
                .with_padding(Padding{.left = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sb_search_text"));
    }

    // ---- smart views ----
    void render_smart_views(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app, bool folded) {
        auto container = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(148)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(6), .right = pixels(8),
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

        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(folded ? 26 : 20), pixels(22)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg(hanabi::icons::draw_fg(icon_name,
                                                        fallback_glyph, txt,
                                                        15.0f, -1.0f))
                .with_debug_name("sv_icon"));

        if (folded) return;

        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{percent(0.72f), pixels(22)})
                .with_padding(Padding{.left = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(txt)
                .with_font_size(FontSize::Small)
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
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("sv_count"));
        }
    }

    // ---- folder group ----
    void render_folder(UIContext<InputAction>& ctx, Entity& parent, int base,
                       const std::string& name, const std::string& key,
                       AppComponent& app, bool archived = false) {
        // Collect member threads.
        std::vector<const api::SessionSummary*> members;
        for (const auto& s : app.sessions) {
            bool match = archived
                             ? (s.state == api::ThreadState::Archived)
                             : (s.folder == key &&
                                s.state != api::ThreadState::Archived);
            if (match) members.push_back(&s);
        }
        if (members.empty()) return;

        theme::Color headColor =
            archived ? theme::text_faint() : theme::text_secondary();

        // Folder header (name + count).
        auto head = div(ctx, mk(parent, base),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(26)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(4), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(10)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("folder_head"));
        // Folder chevron (sprite) + name. The ▾ marker is now a Lucide
        // "chevron-down" sprite; the folder name stays text.
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(16), pixels(18)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "chevron_down", "\xe2\x96\xbe", headColor, 13.0f, -1.0f))
                .with_debug_name("folder_chevron"));
        div(ctx, mk(head.ent(), 4),
            ComponentConfig{}
                .with_label(name)
                .with_size(ComponentSize{percent(0.8f), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(headColor)
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("folder_name"));
        div(ctx, mk(head.ent(), 2),
            ComponentConfig{}
                .with_label(std::to_string(members.size()))
                .with_size(ComponentSize{pixels(24), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("folder_count"));

        int i = 0;
        for (const auto* s : members) {
            render_chat_row(ctx, parent, base + 1 + (++i), *s, app, archived);
        }
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

        // Denser rows: 24px tall with tight vertical padding (was 28px), so
        // more threads fit — matching the mock's compact sidebar feel.
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(2), .right = pixels(8),
                                      .bottom = pixels(2), .left = pixels(10)})
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

        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(s.title, 32))
                .with_size(ComponentSize{percent(0.92f), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(titleColor)
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("row_title"));
    }
};

}  // namespace ecs
