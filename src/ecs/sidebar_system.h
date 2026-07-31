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
    static bool is_attention(api::ThreadState s) {
        return s == api::ThreadState::Attention;
    }
    static int blocked_count(const AppComponent& app) {
        int n = 0;
        for (const auto& s : app.sessions)
            if (s.tag == api::ThreadTag::Blocked) ++n;
        return n;
    }

    static const char* tag_label(api::ThreadTag t) {
        switch (t) {
            case api::ThreadTag::Blocked: return "BLOCKED";
            case api::ThreadTag::Review: return "REVIEW";
            case api::ThreadTag::Done: return "DONE";
            default: return "";
        }
    }
    static theme::Color tag_fg(api::ThreadTag t) {
        switch (t) {
            case api::ThreadTag::Blocked: return theme::tag_blocked_fg();
            case api::ThreadTag::Review: return theme::tag_ready_fg();
            case api::ThreadTag::Done: return theme::tag_done_fg();
            default: return theme::text_faint();
        }
    }
    static theme::Color tag_bg(api::ThreadTag t) {
        switch (t) {
            case api::ThreadTag::Blocked: return theme::tag_blocked_bg();
            case api::ThreadTag::Review: return theme::tag_ready_bg();
            case api::ThreadTag::Done: return theme::tag_done_bg();
            default: return theme::sidebar_bg();
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
            // Brand mark + name.
            div(ctx, mk(header.ent(), 1),
                ComponentConfig{}
                    .with_label("\xe2\x9c\xa6 hanabi")
                    .with_size(ComponentSize{percent(0.62f), pixels(24)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("sb_brand"));

            // New task.
            auto newBtn = button(ctx, mk(header.ent(), 2),
                icon_btn("+").with_debug_name("sb_new"));
            (void)newBtn;

            // Settings.
            auto setBtn = button(ctx, mk(header.ent(), 3),
                icon_btn("\xe2\x9a\x99").with_debug_name("sb_settings"));
            (void)setBtn;
        }

        // Collapse / expand toggle (present in both states).
        auto collapseBtn = button(ctx, mk(header.ent(), 4),
            icon_btn(folded ? "\xc2\xbb" : "\xc2\xab")
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
        div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_label("\xf0\x9f\x94\x8d  Search")
                .with_size(ComponentSize{percent(1.0f), pixels(30)})
                .with_padding(Padding{.top = pixels(6), .right = pixels(8),
                                      .bottom = pixels(6), .left = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_text_color(theme::text_faint())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_debug_name("sb_search"));
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

        smart_item(ctx, container.ent(), 1, "\xe2\x8c\x82", "Home",
                   SmartView::Home, -1, app, folded);
        smart_item(ctx, container.ent(), 2, "\xe2\x9b\x94", "Blocked",
                   SmartView::Blocked, blocked, app, folded);
        smart_item(ctx, container.ent(), 3, "\xe2\x9c\x93", "Review",
                   SmartView::Review, review, app, folded);
        smart_item(ctx, container.ent(), 4, "\xe2\x98\x85", "Starred",
                   SmartView::Starred, starred, app, folded);
    }

    void smart_item(UIContext<InputAction>& ctx, Entity& parent, int idx,
                    const std::string& icon, const std::string& label,
                    SmartView view, int count, AppComponent& app,
                    bool folded) {
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
                .with_label(icon)
                .with_size(ComponentSize{pixels(folded ? 26 : 20), pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(txt)
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
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
        div(ctx, mk(head.ent(), 1),
            ComponentConfig{}
                .with_label("\xe2\x96\xbe " + name)
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

        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(4), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(18)})
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

        // Attention dot (only when attn).
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(attn ? "\xe2\x97\x8f" : " ")
                .with_size(ComponentSize{pixels(12), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(attn ? theme::dot()
                                             : theme::sidebar_bg())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("row_dot"));

        // Title. attn=bold(primary), running=dim(faint), parked/archived=grey.
        theme::Color titleColor = theme::text_secondary();
        if (attn) titleColor = theme::text_primary();
        else if (running || parked || archived) titleColor = theme::text_faint();

        bool hasTag = s.tag != api::ThreadTag::None;
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(fmtutil::ellipsize(s.title, hasTag ? 22 : 30))
                .with_size(ComponentSize{percent(hasTag ? 0.66f : 0.9f),
                                         pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(titleColor)
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("row_title"));

        // One tag chip max.
        if (hasTag) {
            div(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(tag_label(s.tag))
                    .with_size(ComponentSize{children(), pixels(16)})
                    .with_padding(Padding{.top = pixels(1), .right = pixels(5),
                                          .bottom = pixels(1),
                                          .left = pixels(5)})
                    .with_custom_background(tag_bg(s.tag))
                    .with_custom_text_color(tag_fg(s.tag))
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.3f)
                    .with_debug_name("row_tag"));
        }
    }
};

}  // namespace ecs
