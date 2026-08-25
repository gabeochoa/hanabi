#pragma once

// Renders the single collapsible sidebar.
//
//   Unfolded (full): a bare strip of window-drag space where the OS traffic
//     lights sit → a "VIEWS" section header with a panel toggle at its right →
//     six view rows on a 32px pitch → a rule → search + filter → one FLAT
//     activity-ordered session list → a footer (version left, actions right).
//   Folded (thin rail): icon-only smart views + a collapse/expand toggle in
//     the header. A blocked-count badge sits on the rail.
//
// The unfolded geometry is measured off Puffin (~/w/vis/PUFFIN_SPEC.md) and the
// numbers live in the kSb* constants below; change them there, not inline.
//
// Clicking a smart view swaps the main pane (SmartView). Clicking a thread row
// requests that thread be opened in a tab (handled by TabBarSystem/Loader).
// The collapse toggle flips layout.sidebarCollapsed; Cmd+B does the same.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../test_hooks.h"
#include "../settings.h"
#include "../util/prof.h"
#include "../version.h"
#include "../util/ellipsize.h"
#include "../util/format.h"
#include "../util/prof.h"
#include "../util/text_cache.h"
#include "../util/text_epoch.h"
#include "../ui/icons.h"
#include "../ui/snippet_highlight.h"
#include "../../vendor/afterhours/src/plugins/ui/text_input/text_input.h"
#include "thread_model.h"
#include "sidebar_footer_status.h"
#include "../keys.h"
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
        // The search field's placeholder colour has to be smuggled through
        // the THEME: text_input forces its own colours and ignores the
        // per-widget ones (gap #17), and font_muted is the one it reads for a
        // placeholder. ctx.theme is a single global struct read at RENDER
        // time, not at build time, so this is not scoped to this system --
        // setting it here brightens every muted label in the frame unless the
        // next system re-asserts its own. MainPaneSystem now does; measured,
        // because when it did not the main pane's score moved 0.14 points on
        // a change that touched only the sidebar (gap #90).
        ctx.theme.font_muted = kSearchHintFg;
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
                    app->raise_toast(s.starred ? "Session starred"
                                               : "Session unstarred",
                                     s.id, AppComponent::ToastUndo::Star);
                    break;
                }
            }
            app->requestToggleStar.clear();
        }

        // Apply a pending archive toggle, in the same single-writer spot and
        // for the same reason: the Archived count and every view's membership
        // must agree on the very next render. The overlay is what moves, not
        // the backend's own state, so a later list fetch cannot undo it.
        if (!app->requestToggleArchive.empty()) {
            for (auto& s : app->sessions) {
                if (s.id == app->requestToggleArchive) {
                    const bool nowArchived = !model::is_archived(s);
                    s.archive_override = nowArchived;
                    Settings::get().set_archived(s.id, nowArchived);
                    app->raise_toast(nowArchived ? "Session archived"
                                                 : "Session unarchived",
                                     s.id, AppComponent::ToastUndo::Archive);
                    break;
                }
            }
            app->requestToggleArchive.clear();
        }

        // Apply a pending mute toggle, in the same single-writer spot and for
        // the same reason: the notification gate reads s.muted off this vector
        // on the very next frame, so a bell click has to land before then.
        if (!app->requestToggleMute.empty()) {
            for (auto& s : app->sessions) {
                if (s.id == app->requestToggleMute) {
                    s.muted = !s.muted;
                    Settings::get().set_muted(s.id, s.muted);
                    app->raise_toast(s.muted ? "Session muted"
                                             : "Session unmuted",
                                     s.id, AppComponent::ToastUndo::Mute);
                    break;
                }
            }
            app->requestToggleMute.clear();
        }

        // Seed the manual row order from the durable copy, once. After this the
        // in-memory map is the live one and every drop writes through, so the
        // settings file and the screen never disagree.
        if (!app->rowOrderSeeded) {
            app->rowOrder = Settings::get().get_all_row_order();
            app->rowOrderSeeded = true;
        }

        // Forget a folder's manual order (row menu -> "Reset order"): the rows
        // fall back to activity order on the very next render.
        if (!app->requestResetRowOrder.empty()) {
            app->rowOrder.erase(app->requestResetRowOrder);
            Settings::get().set_row_order(app->requestResetRowOrder, {});
            app->requestResetRowOrder.clear();
        }

        // A press that never passed the drag threshold was a click. Drop the
        // candidate once the button is up so nothing later reads it as a drag —
        // but not on the release frame itself, which is when render_group
        // resolves a real drop.
        if (!ctx.mouse.left_down && !ctx.mouse.just_released &&
            !app->rowDrag.sessionId.empty())
            app->rowDrag = AppComponent::RowDrag{};

        // Cmd+B toggles the sidebar.
        bool cmdDown = hanabi::keys::cmd_down();
        if (cmdDown && hanabi::keys::pressed(hanabi::keys::kB)) {
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

        render_rail_divider(ctx, uiRoot, *layout);

        if (folded) {
            render_smart_views(ctx, panel.ent(), *app, folded, r.width);
            // The rail has no rows of its own, but a digest card in the main
            // pane can have opened the menu, so it still gets a chance to draw.
            render_row_menu(ctx, uiRoot, *app);
            return;
        }

        // Unfolded: the measured Puffin order — traffic-light gap, VIEWS strip,
        // view rows, rule, search, list, footer.
        views_header(ctx, panel.ent(), *app, *layout, r.width);
        const bool viewsOpen = app->collapsedFolders.count(kViewsKey) == 0;
        if (viewsOpen) render_smart_views(ctx, panel.ent(), *app, folded, r.width);
        section_rule(ctx, panel.ent());
        render_search(ctx, panel.ent(), *app, r.x, r.y, r.width);
        // The 4px the list is offset by, as a real child so the column keeps
        // stacking (afterhours has no margin-collapse to lean on).
        spacer(ctx, panel.ent(), 9, kSbListGap);

        // Scrollable region: one flat, activity-ordered session list.
        float used = scroll_top_offset(viewsOpen);
        float scrollH = r.height - used - kSbFooterH;
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
        // Overlay scrollers, the way Puffin's list has them
        // (`.background(OverlayScrollers())` in HomeSessionList): a bar at rest
        // is a permanent 8px stripe down the sidebar's trailing edge, and it
        // paints over the column rule at x=279 while it is there. afterhours
        // has no auto-hide mode, only a `show_scrollbar` bool, so hanabi drives
        // the bool itself — visible while the pointer is in the list, gone the
        // moment it leaves. The list only became long enough for this to matter
        // when the row cap stopped truncating it to less than one viewport.
        if (scroll.ent().has<afterhours::ui::HasScrollView>()) {
            scroll.ent().get<afterhours::ui::HasScrollView>().show_scrollbar =
                ctx.mouse_in_subtree(scroll.ent().id) ||
                ctx.mouse_was_in_subtree(scroll.ent().id);
        }
        // TEMPORARY scroll indicator (afterhours gap #26): thin overlay bar
        // computed from the panel's live HasScrollView metrics.
        // (scrollbar now drawn by afterhours)

        // "FOLDERS" is gone: Puffin's list is FLAT, so every non-archived
        // session lands in one activity-ordered list (see the commit message —
        // this is a real loss of grouping).

        // Live search filter: when the query is non-empty, only matching rows
        // render. Track whether ANY row survived so we can show a no-results
        // empty state.
        const std::string q = lower(app->searchQuery);
        int shown = 0;
        // V6: fill the available vertical space instead of a fixed row cap.
        // The scroll viewport is scrollH px tall and each row is kRowHeight px.
        //
        // TWO viewports, not one. A cap of viewportRows-1 made the panel's
        // content shorter than the panel by construction, so the sidebar's
        // ScrollPanel had nothing to scroll: a twenty-session catalog showed
        // eighteen rows and spent the nineteenth slot on a button saying "Show
        // 2 more…", which costs a row to save a row. The reference just keeps
        // going and scrolls — twenty rows, the last one clipped by the footer.
        // Rendering two viewports' worth restores that for any list up to
        // ~38 rows while keeping the guard: rendered rows are still bounded by
        // viewport height and not by list size, which is the only property the
        // cap was ever protecting.
        int viewportRows = static_cast<int>(scrollH / kRowHeight);
        int fillCap = viewportRows * 2;
        if (fillCap < kBucketCap) fillCap = kBucketCap;
        shown += render_folder(ctx, scroll.ent(), 900000, "", "recent",
                               *app, q, r.width, /*archived=*/false,
                               /*catchAll=*/true, /*headerless=*/true,
                               /*cap=*/fillCap);

// (Archived is a smart VIEW in the Views section above, not a list
        // section. Sending a message to an archived thread unarchives it,
        // same as the backend behavior.)

        // Test-only (HANABI_SNIPPET_AUDIT=1): the bands the previous frame's
        // snippets actually painted. A script can read a label and never a
        // band, so without this "the matched words are lit" is a claim no test
        // can hold — and this number sitting next to find's own audit is what
        // shows the two counts are separate (find_counts_only_what_it_paints
        // .e2e is the rule those bands must not disturb). Absolutely
        // positioned so the test build's extra label cannot push the sidebar's
        // column past its own height, which is a layout warning every frame
        // (gap #53) and a different render than the one being tested.
        const int snippetBands = hanabi::snippet_highlight::take_band_count();
        if (hanabi::test_hooks::snippet_audit())
            div(ctx, mk(panel.ent(), 7),
                ComponentConfig{}
                    .with_label("snippet bands " + std::to_string(snippetBands))
                    .with_size(ComponentSize{pixels(r.width - 20.0f),
                                             pixels(14)})
                    .with_absolute_position()
                    .with_translate(10.0f, r.height - 18.0f)
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(2)
                    .with_debug_name("sb_snippet_audit"));

        // Test-only (HANABI_ROW_AUDIT=1): rows RENDERED, out of rows MATCHED,
        // and the index of the FIRST one built.
        // The gap between the first two is virtualization, and it is invisible
        // to a script otherwise -- "the list is capped" is a claim about rows
        // that are not there, and the "Show N more" row that would say so rides
        // below the fold by construction.
        //
        // The third number is what makes a scrolled list assertable. A window
        // that never moves off zero and a window that follows the offset both
        // report the same COUNT, and they differ by whether the sidebar can
        // reach row 300 at all -- so a virtualization test with only the count
        // to look at passes just as happily against a list that has silently
        // stopped scrolling. Absolutely positioned on its own
        // layer for the same reason the snippet audit is: a test build's extra
        // label must not push the sidebar's column past its own height, which
        // is a layout warning every frame (gap #53) and a different render
        // than the one being tested.
        if (hanabi::test_hooks::row_audit())
            div(ctx, mk(panel.ent(), 8),
                ComponentConfig{}
                    .with_label("sidebar rows " + std::to_string(rowsRendered_) +
                                " of " + std::to_string(rowsMatched_) +
                                " @ " + std::to_string(rowsFirst_))
                    .with_size(ComponentSize{pixels(r.width - 20.0f),
                                             pixels(14)})
                    .with_absolute_position()
                    .with_translate(10.0f, r.height - 34.0f)
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(2)
                    .with_debug_name("sb_row_audit"));

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

        render_footer(ctx, panel.ent(), *app, r);
        render_drop_line(ctx, uiRoot, *app, r, scrollH);
        render_row_menu(ctx, uiRoot, *app);
    }

  private:
    // ---- drop-zone line (drag-to-reorder) ----
    // One absolutely-positioned hairline for the WHOLE list, drawn only while a
    // drag is live — not a per-row affordance. Its y was computed from the
    // dragged group's rendered band (render_group), so the line the user sees
    // and the slot the drop uses are the same number. Clamped to the scroll
    // viewport so a drag past the last row cannot paint over the chrome.
    void render_drop_line(UIContext<InputAction>& ctx, Entity& uiRoot,
                          AppComponent& app, const LayoutComponent::Rect& r,
                          float scrollH) {
        if (!app.rowDrag.live) return;
        const float top =
            r.y + scroll_top_offset(app.collapsedFolders.count(kViewsKey) == 0);
        float y = std::clamp(app.rowDrag.lineY, top, top + scrollH - 2.0f);
        div(ctx, mk(uiRoot, 8890),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width - kRowLeftInset - 8.0f),
                                         pixels(2)})
                .with_absolute_position()
                .with_translate(r.x + kRowLeftInset, y)
                .with_custom_background(theme::accent())
                .with_roundness(0.0f)
                .with_render_layer(20)
                .with_debug_name("row_drop_line"));
    }

    // ---- right-click menu on a thread row ----
    // Anchored at the cursor, above everything. Rename… is offered only when
    // the backend actually has the verb; Archive and Mute are machine-local,
    // so they are
    // always there.
    void render_row_menu(UIContext<InputAction>& ctx, Entity& uiRoot,
                         AppComponent& app) {
        if (!app.rowMenuOpen) return;
        const api::SessionSummary* target = app.find_summary(app.rowMenuSessionId);
        if (target == nullptr) {
            app.close_row_menu();
            return;
        }

        enum class Action { Rename, Archive, Mute, ResetOrder };
        struct Item {
            const char* label;
            const char* name;
            Action action;
        };
        std::vector<Item> items;
        if (app.client && app.client->supports_rename())
            items.push_back({"Rename\xe2\x80\xa6", "row_menu_rename",
                             Action::Rename});
        items.push_back({model::is_archived(*target) ? "Unarchive" : "Archive",
                         "row_menu_archive", Action::Archive});
        items.push_back({target->muted ? "Unmute" : "Mute", "row_menu_mute",
                         Action::Mute});
        // Only for a folder that has actually been hand-arranged: on every
        // other row this would be an item that undoes nothing.
        const std::string orderKey = group_key_for(*target);
        if (app.rowOrder.count(orderKey) != 0)
            items.push_back({"Reset order", "row_menu_reset_order",
                             Action::ResetOrder});

        const float menuW = 150.0f;
        const float itemH = 26.0f;
        const float menuH = itemH * static_cast<float>(items.size());
        float mx = app.rowMenuX;
        float my = app.rowMenuY;
        if (mx + menuW > ctx.screen_width) mx = ctx.screen_width - menuW;
        if (my + menuH > ctx.screen_height) my = ctx.screen_height - menuH;
        if (mx < 0.0f) mx = 0.0f;
        if (my < 0.0f) my = 0.0f;

        constexpr int kMenuLayer = 30;

        // An invisible full-window eater under the menu. Without it a click
        // meant to dismiss the menu lands on whatever row is beneath it and
        // opens that thread on the way out.
        auto eater = button(ctx, mk(uiRoot, 8899),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(ctx.screen_width),
                                         pixels(ctx.screen_height)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_transparent_bg()
                .with_custom_hover_bg(afterhours::Color{0, 0, 0, 0})
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_render_layer(kMenuLayer - 1)
                .with_debug_name("row_menu_eater"));

        div(ctx, mk(uiRoot, 8900),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(menuW), pixels(menuH)})
                .with_absolute_position()
                .with_translate(mx, my)
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1))
                .with_roundness(0.2f)
                .with_render_layer(kMenuLayer)
                .with_debug_name("row_menu"));

        const std::string targetId = target->id;
        for (size_t k = 0; k < items.size(); ++k) {
            auto hit = button(ctx, mk(uiRoot, 8901 + static_cast<int>(k)),
                ComponentConfig{}
                    .with_label(items[k].label)
                    .with_size(ComponentSize{pixels(menuW), pixels(itemH)})
                    .with_absolute_position()
                    .with_translate(mx, my + itemH * static_cast<float>(k))
                    .with_custom_background(theme::panel_bg())
                    .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::ROW)
                    .with_alignment(TextAlignment::Left)
                    .with_padding(Padding{.left = pixels(10)})
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_render_layer(kMenuLayer + 1)
                    .with_debug_name(items[k].name));
            if (!hit) continue;
            switch (items[k].action) {
                case Action::Rename:
                    app.renameOpen = true;
                    app.renameSessionId = targetId;
                    app.renameDraft = target->title;
                    app.renameError.clear();
                    app.renameSubmit = false;
                    break;
                case Action::Archive:
                    app.requestToggleArchive = targetId;
                    break;
                case Action::Mute:
                    app.requestToggleMute = targetId;
                    break;
                case Action::ResetOrder:
                    app.requestResetRowOrder = orderKey;
                    break;
            }
            app.close_row_menu();
            return;
        }
        if (eater) app.close_row_menu();
    }

    // ---- MEASURED Puffin sidebar geometry (PUFFIN_SPEC.md + ref/01_home.png)
    // Every number here was read off the reference, not chosen. The unfolded
    // column stacks in exactly this order, and the sum is the list's top edge:
    //   kSbTitlebarH 36 | kSbStripH 28 | views 4 + 6*32 | rule 4 | search 32 |
    //   kSbListGap 4  ->  list at y=300, first glyph centre y=316
    static constexpr float kSbTitlebarH = 36.0f;  // OS traffic-light zone
    static constexpr float kSbStripH = 28.0f;     // "VIEWS" header strip
    static constexpr float kSbViewsTopPad = 4.0f;
    static constexpr float kSbViewRowH = 32.0f;   // view row pitch
    // The selected fill is shorter than the pitch and rounded; both measured
    // off the reference (fill y69..97 inside a row starting at 68, first row
    // x3..276 against a middle of x0..278).
    static constexpr float kSbViewFillInset = 3.0f;
    // Where the fill's own box starts inside the pitch, and how far its
    // content is padded from that box's top. They move in opposite directions
    // by design: the fill was a pixel above the reference's while the label,
    // the icon and the row's whole content were exactly on it, so the two have
    // to be adjusted together or a fix to one is a regression in the other.
    static constexpr float kSvFillTop = 1.0f;
    static constexpr float kSvPadTop = 6.0f;
    // The pitch stays 32 even though the reference's own is ~32.2: it is a
    // SwiftUI layout captured at 2x and halved, so its rows sit on fractional
    // rows that no integer pitch matches -- 32 puts Blocked and Review exactly
    // right and Archived 1px high, 32.2 the other way round. 32 wins because
    // this constant ALSO sets where the session list starts, and a fractional
    // pitch moved every row of the list a pixel down: sidebar 10.89% -> 13.01%,
    // list 14.25% -> 17.18%. Six view rows are not worth twenty list rows.
    static constexpr float kSbViewFillRadius = 5.0f;
    static constexpr int kSbViewRows = 6;
    static constexpr float kSbRuleH = 4.0f;       // 3px gap + a 1px hairline
    static constexpr float kSbSearchH = 32.0f;    // 7px gap + a 25px field
    // 24, which rasterizes as the reference's 25 (gap #80). It was 26, giving
    // 27.
    static constexpr float kSbFieldH = 24.0f;
    static constexpr float kSbListGap = 4.0f;
    static constexpr float kSbFooterH = 28.0f;    // 1px rule + the footer row
    // One left inset for EVERY column in the sidebar: the strip's chevron, a
    // view row's icon, a session row's glyph. Puffin puts all three at x=9.
    static constexpr float kSbInset = 9.0f;
    // ---- shared count-column geometry (gap #18: afterhours has no flex-grow)
    // The view rows and the session rows both show a right-aligned count. To
    // land every count at the SAME right-edge x, we reserve one count-column
    // width + one right inset consistently and size the preceding label column
    // in PIXELS (label = panelW − left − reserved), so the count box always
    // starts at the same x regardless of section. This is the best we can do
    // without flex-grow, and it makes the count families flush to one edge.
    static constexpr float kCountRightPad = 9.0f;  // inset from panel right
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
    static constexpr int kSpacerAboveIdOffset = 197;
    static constexpr int kSpacerBelowIdOffset = 198;
    // The fixed on-screen height of one session row — the MEASURED Puffin
    // pitch. Used by the fill-the-viewport cap so the list reaches the footer.
    static constexpr float kRowHeight = 32.0f;
    // The snippet line a row grows while the list is being searched.
    static constexpr float kSnippetH = 16.0f;
    // A session row's left inset and its leading status-glyph slot. Puffin puts
    // the glyph's centre at x=15.5 and the title's first ink at x=28, so the
    // slot is 13 wide from kSbInset.
    static constexpr float kRowLeftInset = kSbInset;
    static constexpr float kGlyphW = 13.0f;   // leading status glyph slot
    // kRowTitlePad DOES NOT PLACE THE TITLE, and it is no longer asked to.
    // afterhours draws a label's text from the ELEMENT's own rect plus its
    // private 5px text margin and ignores the element's padding entirely
    // (afterhours_gaps.md #85, #91) — measured here: built at 6, 7 and 20 the
    // captures are byte-identical except where the wider budget re-ellipsized
    // three rows. So this constant is a WIDTH budget for fit_to_width and
    // nothing else, the `.with_padding` that used to sit on the title element
    // is GONE rather than left in as decoration (it would have moved every
    // title six pixels right the day #85 is fixed upstream), and the title's
    // first ink lands at kRowLeftInset + kGlyphW + kAhTextInset + kRowTitleLead.
    //
    // Without the lead that is 27, and the reference's is 28. That one pixel,
    // uniform across all nineteen visible rows, was the whole of the list
    // region's remaining parity headroom: six rounds read the comment above,
    // believed the text was at 28, measured the ink as 77-86% of the
    // reference's and concluded rasterizer. kRowTitleLead is the pixel,
    // applied as a MARGIN — which moves an element and its text together, the
    // way render_snippet below already documents and the way gap #85's escape
    // list wrongly rules out.
    static constexpr float kRowTitlePad = 6.0f;
    static constexpr float kRowTitleLead = 1.0f;
    // Ellipsize by MEASURED width, not by a chars-times-average-advance
    // budget: the estimate is calibrated to one font at one size, so it either
    // clips a title early or overflows the column when either changes.
    // Only the rows in the viewport reach this, so the cost is bounded by
    // viewport height rather than by list size.
    // Ellipsize `text` to fit `maxW` at font size `px`, memoized.
    //
    // The ALGORITHM is hanabi::text::fit_to_width (src/util/ellipsize.h), which
    // is where the note on why it stopped being a linear scan lives. What is
    // here is the other half of the fix, and the bigger one: a row's title, its
    // font size and its column width are all the same this frame as last, and
    // an idle frame should re-measure nothing at all.
    //
    // The cache is keyed on the WHOLE argument tuple of a pure function, so it
    // cannot go stale on its own -- a changed title is a different key, not an
    // invalid entry, and there is nothing to invalidate on a theme change or a
    // catalog refresh. The one thing that DOES invalidate it is a font swap,
    // which changes no key at all (src/util/text_epoch.h); the shared cache
    // handles that, which is half the reason this is no longer a hand-rolled
    // map.
    //
    // The other half is the BOUND. This used to hold 4096 entries and CLEAR
    // itself on reaching them, which is the wrong shape twice over: a clear
    // throws away the resident rows along with the tail, and it does it at the
    // moment the working set is largest. The working set is not the 38 rows on
    // screen -- the sidebar's collapse is ANIMATED, so every frame of a fold
    // draws those rows at a different width and mints a new key each time.
    // Measured, six folds: 220 entries, ~30 per fold. That is 130 folds from a
    // wholesale clear, and it arrives with no warning and no symptom except a
    // cold frame.
    //
    // src/util/text_cache.h evicts the least recently used one instead, and
    // holds the rows drawn every frame however long the tail gets -- the
    // property tests/unit/test_text_cache.cpp calls "a resident working set
    // survives a scan". Its lookup is heterogeneous, which the hand-rolled
    // version was too and for a reason worth keeping written down: a first
    // version built an owning key to search with and returned the hit by
    // value, and `sample` still put this function at 15% of the main thread
    // afterwards, because replacing quadratic measurement with a pair of
    // mallocs is a smaller win than it sounds.
    //
    // The returned reference is valid until the next call that MISSES. Every
    // caller hands it straight to with_label, which copies, so nothing
    // outlives its entry.
    static const std::string& fit_to_width(std::string_view text, float px,
                                           float maxW) {
        static const std::string kEmpty;
        if (maxW <= 0.0f) return kEmpty;

        // 512: the 38 resident rows, plus room for several folds' worth of
        // intermediate animation widths before the oldest of them is dropped.
        constexpr std::size_t kFitEntries = 512;
        static hanabi::text::TextKeyCache<std::string> cache(kFitEntries);

        if (const std::string* hit = cache.find(text, px, maxW)) {
            hanabi::prof::tick("cache.fit_hit");
            return *hit;
        }
        hanabi::prof::tick("cache.fit_miss");

        const std::string owned{text};
        std::string fitted = hanabi::text::fit_to_width(
            owned, maxW, [px](const char* s) { return theme::text_px(s, px); });
        const std::string& out =
            cache.put(text, px, maxW, std::move(fitted));
        hanabi::prof::gauge("cache.fit_entries", cache.size());
        return out;
    }

    // The collapsedFolders sentinel that folds the VIEWS section. Reusing that
    // set keeps this out of AppComponent, which several systems share.
    static constexpr const char* kViewsKey = "__views__";
    // Sentinel for the search row's filter toggle: hide automated/cron rows.
    static constexpr const char* kHideAutoKey = "__hide_automated__";
    // Where the scrollable list starts, measured down from the sidebar's top.
    // This is the SUM of the fixed column above it, so the drop-zone line, the
    // scroll extent and the rendered rows can never disagree about where the
    // list is — the previous hand-maintained constant drifted from the widgets.
    static float scroll_top_offset(bool viewsOpen) {
        return kSbTitlebarH + kSbStripH +
               (viewsOpen ? kSbViewsTopPad +
                                kSbViewRowH * static_cast<float>(kSbViewRows)
                          : 0.0f) +
               kSbRuleH + kSbSearchH + kSbListGap;
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
    // The same answer without the copy, for the per-row render path. The view
    // aliases `title`, which lives in app.sessions and outlives the call.
    static std::string_view display_title_view(const std::string& title) {
        return fmtutil::display_title_view(title);
    }

    // ---- automated / scheduled row detection -------------------------------
    // A real backend mixes human conversations with scheduled/cron sessions
    // ("Schedule: nightly backfill", "kicker-tick", "continuous-triage-tick").
    // This answers ONE question, for ONE caller: the search row's explicit
    // "hide automated rows" toggle, which the reader turns on when they want
    // them gone.
    //
    // It used to also de-emphasize every such row unasked — a fainter title
    // and a separate "automated" glyph in the status slot — and that was a
    // guess with no design behind it. Puffin draws `kicker-tick` exactly like
    // every other row (ref/01_home.png, row 3: the same blue arc as the two
    // live runs around it), because a scheduled thread that is RUNNING is a
    // thread that is running; how it was started is not its status. Worse, the
    // rule decided a row's whole appearance from the SHAPE OF ITS TITLE, so
    // renaming a thread changed what the list said about it, and a blocked
    // cron tick — the one that most wants you — read as the quietest row on
    // screen. An opt-in filter is the honest form of the same idea: the reader
    // says "not now", rather than the client deciding some threads matter less.
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
        hanabi::glyph::chevron(rect, collapsed, c);
    }

    // ---- attention model helpers ----
    // Delegate the pure classification to the graphics-free, headlessly-tested
    // ecs::model layer so the tested logic IS the shipped logic.
    static bool is_attention(api::ThreadState s) {
        return ecs::model::is_attention(s);
    }
    // The smart-view counts the shelf actually badges, in ONE pass.
    //
    // This was two passes computing four numbers, of which two were thrown
    // away. blocked_count() walked app.sessions for the blocked total; the
    // caller then walked it again for review, starred and archived. Starred
    // and archived went nowhere -- Pinned and Archived carry NO badge in
    // either client (`SmartView.showsAttentionBadge` is false for them) and
    // both rows pass -1. A comment said "other things read them"; nothing
    // does, and nothing has for as long as the rows have passed -1.
    //
    // So: one traversal instead of two, and two predicates per session
    // instead of four. The second pass was not merely redundant work, it paid
    // the cache misses over again -- by the time it started, the first had
    // long since evicted the front of a 2000-element catalog.
    //
    // It is STILL O(catalog) per frame and that is deliberate. The honest
    // alternative is a cache, and a cache here must be invalidated by every
    // writer of the sessions vector: the star toggle, the archive toggle, the
    // mute toggle, the loader's refetch, a toast's undo. A stale count is a
    // smart view that says 6 and lists 5, which is precisely the class of
    // regression this work is not allowed to introduce. All the remaining
    // linear work in this file together measures at ~26ns per session per
    // frame (docs/perf/SIDEBAR.md) -- 0.05ms at 2000 sessions, under the noise
    // floor. Halving it is free; risking a wrong number to remove it is not.
    struct ViewCounts {
        int blocked = 0;
        int review = 0;
    };
    static ViewCounts view_counts(const AppComponent& app) {
        ViewCounts c;
        for (const auto& s : app.sessions) {
            if (ecs::model::in_blocked_view(s)) ++c.blocked;
            if (s.state == api::ThreadState::Ready) ++c.review;
        }
        return c;
    }

    // ---- status mark (shape + tone, straight from the shared model) -------
    // EVERY row gets a leading indicator slot of the same size, so no row
    // looks unlabeled. WHICH mark a row wears is not decided here: the pure,
    // headlessly-tested ecs::model::mark_for owns the whole rule (see the long
    // note there), and this file only knows how to draw the five shapes and
    // what the three tones are worth in pixels. The sidebar used to hold a
    // second, richer mapping of its own that re-derived the model's answer —
    // two rules for one question, and the one the reader saw was the one the
    // tests did not cover.
    //
    // The shapes and the three hues are Puffin's, measured off
    // ref/01_home.png: an open arc while a run is live, a bang when the thread
    // wants you, a cross when it died, a chevron on a settled thread that
    // opens, a dot for everything else.
    using Glyph = ecs::model::Glyph;
    using Mark = ecs::model::Mark;
    static constexpr theme::Color kGlyphActive{155, 196, 255, 255};
    static constexpr theme::Color kGlyphAlert{224, 92, 96, 255};
    static constexpr theme::Color kGlyphCalm{146, 146, 171, 255};
    // The sub-agent count's two colours, both measured off the reference
    // capture (docs/visual-parity/ref/01_home.png) the way the glyph colours
    // above were. A count is antialiased text with no solid interior, so
    // neither could be read straight off a peak pixel: the hue comes from the
    // RATIO of (pixel - background) across samples, which is independent of
    // coverage, and the magnitude from the brightest sample.
    //
    // The live colour is NOT kGlyphActive. The reference's running glyph and
    // its running count measure to two different blues on the same row —
    // the count is the more saturated of the two — so the row draws two, and
    // reusing the glyph's would have been a visible miss on the one row that
    // matters most.
    static constexpr theme::Color kCountLive{120, 169, 255, 255};
    // Settled matches the calm glyph exactly, which is the reference's own
    // muted text; measured (130,130,153) and (137,137,160) both sit on this
    // colour's ray out of the row background.
    static constexpr theme::Color kCountSettled = kGlyphCalm;
    // Breathing room between the title's ellipsis and the count's digits.
    static constexpr float kCountTextPad = 6.0f;
    // afterhours insets label text by a hardcoded 5px from its box on EVERY
    // alignment, with no way to switch it off (vendor is read-only —
    // afterhours_gaps.md #84). Right-aligning the count therefore lands its
    // digits 5px shy of the row's right edge, and the reference puts them
    // flush against it — the same 5px by which every count already in this
    // sidebar (the smart-view badges, the folder counts) sits left of where
    // Puffin draws it.
    //
    // The way out without touching the vendor: LEFT-align inside a slot sized
    // to the text PLUS that inset. The inset then falls on the left, where
    // there is nothing to be flush with, and the text's right edge lands
    // exactly on the slot's — which, as the row's last child, is the row's.
    static constexpr float kAhTextInset = 5.0f;
    // Measured off the reference: its count digits stand 8px tall where the
    // row title's stand 11, and the title is LIST_ROW (16.5).
    static constexpr float kCountFontPx = 13.5f;
    // Puffin gives EVERY session-row title the same near-white; the list does
    // not encode attention in the title's brightness the way hanabi did.
    // ---- the smart-view count badge ----
    // Measured off the reference: the count is not a bare number, it is a
    // 17x17 ring with the digit centred in it, right edge at panelW −
    // kCountRightPad. All three colours are the SAME hue at three coverages —
    // the ratio of (pixel − background) is (0.596, 0.778, 1.000) at every
    // sample, ring and fill and digit alike, which is how you read a colour
    // off a 1px antialiased stroke (REFERENCE.md: never by peak pixel).
    //
    // Given as flat colours over the sidebar rather than as alpha, because
    // the badge also sits on the SELECTED row's fill and afterhours composites
    // a border against the element's own background, not the one behind it.
    static constexpr theme::Color kBadgeFill{43, 50, 69, 255};
    static constexpr theme::Color kBadgeRing{79, 96, 129, 255};
    // Re-measured by the (pixel - fill) ratio across every digit pixel of the
    // reference's 6 and 3 badges -- 0.641/0.803 and 0.643/0.804, agreeing to
    // two thousandths on two independent badges, which is what says the read is
    // the colour and not the coverage. Ten levels of red short of where hanabi
    // had it.
    static constexpr theme::Color kBadgeText{162, 199, 255, 255};
    // Asked for 16, not 17: every box rasterizes one pixel bigger and one
    // pixel up-left than requested (gap #80), so a 16px request lands as the
    // measured 17. The lost pixel on the right is given back by shortening
    // this row's right padding by one, below — the badge is the row's last
    // child, so its right edge IS the row's content edge.
    static constexpr float kBadgeD = 16.0f;
    // The digit inside it. Measured on the reference's 6 and 3 badges: 8px of
    // ink tall and 5-6 wide, where `theme::type::SM` gave hanabi 7 by 4 -- 12
    // lit pixels against the reference's 32, and 30 brightness levels short
    // with it, because a smaller glyph never reaches its own colour. This is
    // the sub-agent count's `kCountFontPx` arrived at independently: the same
    // 8px digit, in the same sidebar, at the same size.
    static constexpr float kBadgeFontPx = 14.0f;
    // The badge sat a pixel below the reference's -- ring y140..156 against
    // y139..155 -- while the label, the icon and everything else in the same
    // row were exactly on it, so nothing about the ROW could be moved to fix
    // it. A bottom margin under `AlignItems::Center` lifts the badge alone:
    // the row splits what is left, so 2px of margin under a 16px badge in a
    // 22px content box is a 1px rise. Measured back: ring y139..155, digit
    // y143..150, both exactly the reference's rows.
    static constexpr float kBadgeLift = 2.0f;
    // The reference pads the digits by 5pt on each side; one digit lands on
    // the square case above, two need the capsule.
    static constexpr float kBadgePadX = 5.0f;
    // The smart-view label: measured off the reference, ink starting at x=37
    // and standing 11px tall for "Blocked" over a 49px run. hanabi drew it at
    // theme::type::BODY (13), which is 9px tall over 39px -- barely half the
    // ink, which is most of what the VIEWS region was still scoring.
    static constexpr float kViewLabelPx = 16.8f;
    // Ink colour, read by the (pixel - background) ratio rather than by peak
    // pixel (REFERENCE.md): 0.907 / 0.907 / 1.000, i.e. blue-tinted, where
    // theme::text_secondary is a neutral grey.
    static constexpr theme::Color kViewLabelFg{150, 150, 175, 255};
    static constexpr unsigned char kVIF = 140;
    static constexpr unsigned char kVIFB = 166;
    static constexpr theme::Color kViewLabelActiveFg{251, 250, 255, 255};
    // The ICON gets its own ink, and the reason is the reason the sidebar
    // footer's colour sweep came out backwards (REFERENCE.md): a sprite blit
    // reaches its own colour and 9pt text never does. Puffin paints both from
    // one token -- `SmartViewSidebar` line 297 hands `Chrome.mutedText` to the
    // whole row -- so a single hanabi constant looks like the faithful
    // reading, and it is not: kViewLabelFg was measured off the LABEL, where
    // it is 10 levels above the token to make up for coverage the text never
    // gets. Handed to a blit, those 10 levels arrive in full and every icon
    // peaks 15-22 above the reference's.
    static constexpr theme::Color kViewIconFg{kVIF, kVIF, kVIFB, 255};
    static constexpr float kViewIconPx = 16.0f;
    
    // The gap between the icon slot and the label. It is a SPACER, not the
    // label's padding: padding on a label-only div is silently ignored
    // (afterhours_gaps.md #85), which is why the 12px pad this row used to ask
    // for never moved anything and the label sat 6px left of the reference for
    // the whole parity effort.
    static constexpr float kViewLabelGap = 6.0f;
    // The search pill's hint. Measured: the reference's placeholder stands
    // 10px tall where hanabi's stood 8, and its ink peaks at (163,163,168)
    // where hanabi's peaked at (98,98,110) -- half the contrast, on the one
    // label in the sidebar whose whole job is to be noticed by someone who
    // has not found the field yet.
    // The VIEWS strip's own label. Measured: the reference's "VIEWS" is 37px
    // of ink (x22..58) where hanabi's LABEL size gave 26.
    static constexpr float kViewsHeadPx = 14.5f;
    static constexpr float kSearchPx = 15.5f;
    static constexpr theme::Color kSearchHintFg{168, 168, 174, 255};
    // The filter glyph is brighter than the hint beside it: measured, it peaks
    // at (193,193,196) where the placeholder peaks at (163,163,168).
    static constexpr theme::Color kSearchFilterFg{200, 200, 206, 255};
    // Was `kCountRightPad - 1`, which tied the smart-view badges to the
    // session rows' count. They are two columns in two different lists and
    // they measured out to two different insets: the badges already land on
    // the reference's x255..271 and the counts sat 1.3px left of theirs, so
    // one constant could not be right for both.
    static constexpr float kBadgeRightPad = 8.0f;

    // (247,247,255), measured off the reference's own row titles: same hue
    // ratio as hanabi already had (1.000/1.000/0.986) but nine levels
    // brighter. The eye does not report "nine levels dim"; inkdiff.py does.
    static constexpr theme::Color kRowTitleFg{247, 247, 255, 255};

    // Tone -> ink. The three colours the marks are drawn in, and the only
    // place the model's meanings become pixels.
    static theme::Color mark_color(ecs::model::Tone t) {
        switch (t) {
            case ecs::model::Tone::Alert: return kGlyphAlert;
            case ecs::model::Tone::Live: return kGlyphActive;
            case ecs::model::Tone::Calm: break;
        }
        return kGlyphCalm;
    }

    // The mute mark: a small ring with a slash through it, the universal
    // "silenced". The icon atlas has no bell (see the smart-view note below),
    // and an emoji bell is not in the UI font, so it is drawn from primitives.
    static void draw_mute_mark(RectangleType rect, theme::Color c) {
        // Sits toward the slot's right with a gap before the star, matching how
        // the star insets itself from the timestamp.
        const float cx = rect.x + rect.width - hanabi::viewport::px(12.0f);
        const float cy = rect.y + rect.height * 0.5f - hanabi::viewport::px(1.0f);
        const float r = hanabi::viewport::px(5.0f);
        afterhours::draw_circle_lines(static_cast<int>(cx),
                                      static_cast<int>(cy), r, c);
        const float d = r * 0.72f;
        afterhours::draw_line_ex(afterhours::vec2{cx - d, cy + d},
                                 afterhours::vec2{cx + d, cy - d},
                                 hanabi::viewport::px(1.5f), c);
    }

    // Draw the status mark centered inside `rect` (the on-screen rect of the
    // small glyph slot). Uses afterhours' real shape primitives, so the five
    // statuses are distinct by SHAPE as well as by colour. Every row draws
    // SOMETHING here — a settled row gets the plain dot, never a blank — so no
    // row reads as unlabeled or second-class.
    //
    // Geometry is measured off ref/01_home.png, glyph by glyph: the three dots
    // (live / alert / calm) are one 8px circle in three colours, the bang is a
    // 9px stroke over a 2px tittle, the cross spans 8px corner to corner, and
    // the chevron is 8px tall and 4px wide with its vertex to the right.
    // Every mark's geometry, re-derived against the reference's own HALF-
    // COVERAGE silhouette rather than by eye. afterhours does not antialias
    // primitives (afterhours_gaps.md #92), so hanabi's marks are hard-edged
    // where Puffin's have a soft fringe -- which means a mark drawn to the
    // reference's OUTER extent lands 30-85% more ink on screen than it has.
    // Drawn to its half-coverage extent instead, the ink lands about right and
    // the silhouette still matches.
    static constexpr float kArcInner = 3.3f;
    static constexpr float kArcOuter = 4.6f;
    // The bang, measured by per-pixel coverage rather than by silhouette: the
    // reference's stroke is 1.95px wide and runs from 5.5px above the mark's
    // centre to 2.46 below it, and its tittle is the same width, 2.28 tall,
    // centred 5.26 below.
    static constexpr float kBangT = 1.95f;
    static constexpr float kBangTop = 5.5f;
    static constexpr float kBangBot = 2.46f;
    static constexpr float kBangDotY = 5.26f;
    static constexpr float kBangDotH = 2.28f;
    static constexpr float kDotR = 3.4f;
    static constexpr float kCrossH = 3.0f;
    static constexpr float kCrossT = 1.6f;
    static constexpr float kChevH = 3.6f;
    static constexpr float kChevW = 2.0f;
    static constexpr float kChevT = 1.6f;

    // Where the mark's centre sits relative to the slot's own. Puffin draws it
    // above the row's midline and right of a 13px slot's centre; both are
    // measured, and without them every glyph reads a row-half low.
    static constexpr float kMarkDx = 0.0f;
    static constexpr float kMarkDy = -1.0f;

    // `bg` is what this row is actually painting on: the bang is the one mark
    // drawn with hand-composited antialiasing (gap #92 has no other way out),
    // and a fringe pre-mixed against the wrong colour is a visible halo. The
    // row's own fill changes under the pointer, so the caller passes it rather
    // than this assuming the sidebar's.
    static void draw_mark(RectangleType rect, Mark m, theme::Color bg) {
        const float cx = rect.x + rect.width * 0.5f + hanabi::viewport::px(kMarkDx);
        const float cy = rect.y + rect.height * 0.5f + hanabi::viewport::px(kMarkDy);
        const theme::Color c = mark_color(m.tone);
        switch (m.shape) {
            case Glyph::Arc: {
                // The gap is at the TOP, and this is the one thing in the
                // glyph column that was not a pixel-nudge: hanabi drew the gap
                // in the LOWER LEFT, so the mark read as a hook where the
                // reference draws a bowl.
                //
                // Measured on all four running rows of `ref/02_thread.png`,
                // which are identical to the pixel: ink covers 290 degrees and
                // the 70-degree gap is centred on 275.5, five degrees clockwise
                // of straight up. Angles run clockwise from three o'clock, so
                // that is -49 to 240.
                //
                // Puffin's source cannot settle this. `SessionRowView.statusDot`
                // in the v0.5.2 checkout is a 7pt filled `Circle()` -- the five
                // shapes arrived after it (REFERENCE.md), so the frozen PNG is
                // the only authority for the arc's geometry and every number
                // above comes off it.
                afterhours::draw_ring_segment(cx, cy, hanabi::viewport::px(kArcInner), hanabi::viewport::px(kArcOuter),
                                              -49.0f, 240.0f, 28, c);
                break;
            }
            case Glyph::Dot: {
                // draw_circle_v truncates its centre to int (gap #78), which
                // at this size lands the dot half a pixel off and reads as a
                // lumpy polygon. A zero-inner-radius ring segment is the same
                // shape with a float centre.
                afterhours::draw_ring_segment(cx, cy, 0.0f, hanabi::viewport::px(kDotR), 0.0f,
                                              360.0f, 28, c);
                break;
            }
            case Glyph::Bang: {
                // The most common mark in the list -- six of the eighteen
                // visible rows -- and it was carrying twice the reference's
                // ink: three hard columns at full strength where the reference
                // measures 0.44 / 0.97 / 0.50, a 1.95px stroke with a fringe
                // down each side. It also sat a pixel left of every other mark
                // in the column, from a `- px(1)` nobody had re-measured.
                //
                // Both axes of both parts are read off `ref/02_thread.png` by
                // per-pixel coverage, and laid down through `rect_aa`, which
                // paints the fringe itself (afterhours has no primitive
                // antialiasing -- gap #92). A bang and its tittle are the two
                // marks in this vocabulary that are axis-aligned, so they are
                // the two that can have it.
                const float u = hanabi::viewport::px(1.0f);
                const float half = kBangT * 0.5f * u;
                hanabi::glyph::rect_aa(cx - half, cy - kBangTop * u, cx + half,
                                       cy + kBangBot * u, c, bg);
                hanabi::glyph::rect_aa(cx - half, cy + kBangDotY * u - kBangDotH * 0.5f * u,
                                       cx + half,
                                       cy + kBangDotY * u + kBangDotH * 0.5f * u,
                                       c, bg);
                break;
            }
            case Glyph::Cross: {
                const float h = hanabi::viewport::px(kCrossH);
                afterhours::draw_line_ex(afterhours::vec2{cx - h, cy - h},
                                         afterhours::vec2{cx + h, cy + h},
                                         hanabi::viewport::px(kCrossT), c);
                afterhours::draw_line_ex(afterhours::vec2{cx + h, cy - h},
                                         afterhours::vec2{cx - h, cy + h},
                                         hanabi::viewport::px(kCrossT), c);
                break;
            }
            case Glyph::Chevron: {
                // Two strokes to a vertex on the right, NOT the filled
                // triangle hanabi::glyph::chevron draws for folder headers:
                // the reference's row chevron is a stroked ">" and a solid
                // wedge at this size reads as a play button. Drawn here rather
                // than shared, because the folder one is deliberately filled.
                const float h = hanabi::viewport::px(kChevH);   // half-height, measured
                const float w = hanabi::viewport::px(kChevW);   // half-width
                const afterhours::vec2 top{cx - w, cy - h};
                const afterhours::vec2 tip{cx + w, cy};
                const afterhours::vec2 bot{cx - w, cy + h};
                afterhours::draw_line_ex(top, tip, hanabi::viewport::px(kChevT), c);
                afterhours::draw_line_ex(tip, bot, hanabi::viewport::px(kChevT), c);
                break;
            }
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
        const float dp = hanabi::viewport::px(px);
        const float hw = dp * 0.5f;          // half-width of the triangle base
        const float hh = dp * 0.44f;         // half-height (apex above center)
        const afterhours::vec2 apex{cx, cy - hh};
        const afterhours::vec2 bl{cx - hw, cy + hh};
        const afterhours::vec2 br{cx + hw, cy + hh};
        // Outlined triangle (3 stroked edges) so it reads as a warning sign,
        // not a solid alert. ~1.4px stroke matches the Lucide line weight.
        const float t = hanabi::viewport::px(1.4f);
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

    // ---- pointer-only activation ----
    // afterhours fires a focused element's click listener on Enter as well as
    // on a mouse click (systems.h: `has_focus(id) && pressed(WidgetPress)`),
    // and there is no click-activation mode that means "mouse only". The
    // window's initial focus lands on the first focusable element, which — now
    // that the sidebar opens with its rows rather than a header of buttons — is
    // the first view row. Enter would therefore switch the view out from under
    // the very keystroke the main pane's list cursor uses to OPEN a row.
    //
    // Taking the rows out of the tab order instead (`with_skip_tabbing`) is
    // worse: focus then lands on the search field, which silently swallows
    // every keystroke and the arrow keys with it. So the rows stay focusable
    // and the keystroke is filtered here. (FRICTION_LOG / gap #66.)
    static bool pointer_click(UIContext<InputAction>& ctx, Entity& e) {
        if (!e.get<afterhours::ui::HasClickListener>().down) return false;
        // "Was the pointer on it" is the only reliable read: the listener can
        // fire a frame after the key that caused it, so testing the Enter key
        // here misses it.
        return ctx.mouse_in_subtree(e.id) || ctx.mouse_was_in_subtree(e.id);
    }

    // ---- generic column spacer ----
    // afterhours has no margin between column children and no way to say "the
    // next child starts 4px lower", so every measured gap in the Puffin layout
    // is an empty div that exists only to occupy height.
    // The same idea one axis over, for a Row: afterhours has no margin between
    // row children either, so a measured horizontal gap is an empty div.
    void spacer_x(UIContext<InputAction>& ctx, Entity& parent, int id,
                  float w) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_spacer_x"));
    }

    void spacer(UIContext<InputAction>& ctx, Entity& parent, int id, float h) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(h)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_spacer"));
    }

    // ---- the hairline under the views block ----
    // Puffin puts a 1px rule at y=263, three px under the last view row.
    void section_rule(UIContext<InputAction>& ctx, Entity& parent) {
        auto wrap = div(ctx, mk(parent, 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kSbRuleH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(kSbRuleH - 1.0f)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_rule_wrap"));
        div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
                .with_custom_background(theme::divider())
                .with_roundness(0.0f)
                .with_debug_name("sb_rule"));
    }

    // ---- header / title bar ----
    // UNFOLDED: there is no header. Puffin's sidebar opens with bare window
    // colour where the OS traffic lights sit, and its first content is the
    // VIEWS strip. hanabi runs in a standard NSWindow, so the real dots are
    // drawn by the OS in that band — we reserve the height and draw nothing
    // (faux dots would double them in the shipping app; they are simply absent
    // from a headless capture, which is the one place they can't be matched).
    //
    // The three affordances the old brand row carried are not lost: the
    // collapse toggle moves to the VIEWS strip's right edge, and New task +
    // Settings move to the footer, where Puffin keeps its own icon cluster.
    //
    // FOLDED: unchanged — the thin rail keeps its single expand toggle.
    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       LayoutComponent& layout, bool folded) {
        if (!folded) {
            spacer(ctx, parent, 1, kSbTitlebarH);
            return;
        }
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(40)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                // Folded rail: LEFT-align the collapse toggle (cross-axis =
                // horizontal for a Column) so it sits at the same left inset as
                // the smart-view icons below, forming one flush-left column.
                .with_align_items(AlignItems::FlexStart)
                .with_justify_content(JustifyContent::FlexStart)
                .with_padding(Padding{.top = pixels(7), .right = pixels(8),
                                      .bottom = pixels(5),
                                      .left = pixels(kRailIconInset)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_header"));

        // Folded rail: a single expand toggle in the header column.
        auto collapseBtn = button(ctx, mk(header.ent(), 4),
            icon_btn_sprite("sidebar_open", "\xc2\xbb")
                .with_debug_name("sb_collapse"));
        if (collapseBtn) {
            layout.sidebarCollapsed = !layout.sidebarCollapsed;
            Settings::get().set_sidebar_collapsed(layout.sidebarCollapsed);
        }
    }

    // ---- VIEWS section header strip ----
    // Measured: a 28px band filled a shade above the sidebar (#22222D), a
    // chevron at x=9, the word VIEWS at x=22, and a panel-toggle icon whose
    // glyph sits at x=255..270. The strip is the ONE filled surface in the
    // sidebar's chrome; everything else is the window colour.
    //
    // Clicking the strip folds the view rows away. That state lives in
    // collapsedFolders under a sentinel key rather than a new AppComponent
    // field: several systems share that component and adding to it is a
    // cross-owner change for what is one boolean.
    void views_header(UIContext<InputAction>& ctx, Entity& parent,
                      AppComponent& app, LayoutComponent& layout,
                      float panelW) {
        const bool collapsed = app.collapsedFolders.count(kViewsKey) > 0;
        auto strip = div(ctx, mk(parent, 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kSbStripH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                // 6, not the sidebar's usual kSbInset of 9: the strip's own
                // chevron sits further left than every row below it. Measured
                // -- the reference's chevron spans x9..16 where a 9px inset
                // puts hanabi's at x12..18.
                .with_padding(Padding{.right = pixels(5),
                                      .left = pixels(7)})
                .with_custom_background(theme::section_header_bg())
                .with_custom_hover_bg(theme::hover_over(
                    theme::section_header_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                // Clickable but NOT a keyboard-focus stop: afterhours parks
                // initial focus on the first focusable element and paints its
                // focus ring at rest, so whatever sits first in the sidebar
                // wears a blue box in every screenshot (FRICTION_LOG).
                .with_skip_tabbing(true)
                .with_roundness(0.0f)
                .with_debug_name("sb_views_head"));
        strip.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (pointer_click(ctx, strip.ent())) {
            if (collapsed) app.collapsedFolders.erase(kViewsKey);
            else app.collapsedFolders.insert(kViewsKey);
        }

        // The strip's ink is the same blue-tinted grey as the view labels
        // below it, not theme::text_faint: measured, the reference's VIEWS
        // peaks at (151,151,176) where text_faint is (100,100,112).
        const theme::Color tint = kViewLabelFg;
        div(ctx, mk(strip.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                // 10 wide: the chevron is centred in it and the label follows
                // it in flow, so this one number sets both. 7 + 10 + the 5px
                // text inset nothing can turn off (gap #75) puts the label's
                // ink on the reference's x=22, and the chevron on its x9..16.
                .with_size(ComponentSize{pixels(10), pixels(18)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_on_draw_fg([collapsed, tint](RectangleType rect) {
                    draw_chevron(rect, collapsed, tint);
                })
                .with_debug_name("sb_views_chevron"));
        // Fixed pixel width (gap #18: no flex-grow) so the toggle that follows
        // lands on the measured right edge instead of packing mid-strip.
        float labelW = panelW - 7.0f - 5.0f - 10.0f - 24.0f;
        if (labelW < 20.0f) labelW = 20.0f;
        div(ctx, mk(strip.ent(), 2),
            ComponentConfig{}
                .with_label("VIEWS")
                .with_size(ComponentSize{pixels(labelW), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(tint)
                .with_font_size(kViewsHeadPx)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sb_views_label"));
        // Panel toggle: the collapse affordance the removed brand row carried.
        auto collapseBtn = button(ctx, mk(strip.ent(), 3),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(24), pixels(20)})
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(
                    theme::section_header_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_skip_tabbing(true)
                .with_roundness(0.3f)
                // panel_left, at 17: the reference's toggle is SF
                // `sidebar.leading`, a panel outline with its leading column
                // filled -- not the arrowed bracket `sidebar_close` draws. It
                // spans x255..270, which is 16px of ink.
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "panel_left", "\xc2\xab", tint, 17.0f))
                .with_debug_name("sb_collapse"));
        if (collapseBtn) {
            layout.sidebarCollapsed = !layout.sidebarCollapsed;
            Settings::get().set_sidebar_collapsed(layout.sidebarCollapsed);
        }
    }

    // ---- footer ----
    // Version string left, three small actions right, over a 1px rule. Drawn
    // ABSOLUTELY at the sidebar's bottom rather than as the column's last
    // child: the scroll panel above it is sized in pixels, and a flow child
    // after it would be pushed off the bottom by a single px of rounding.
    //
    // The footer's BLIT ink stays `text_faint`, and that is a MEASURED
    // decision rather than the absence of one. Its TEXT does not -- see the
    // version label below.
    //
    // Puffin gives the version label and all three glyph buttons one token
    // (`SidebarColumn.sidebarFooter` -> `PuffinTheme.Chrome.mutedText`), and on
    // the navi theme the reference was shot in that token is (140,140,166)
    // (`Models.swift:593`; navi is `defaultValue` and its headerBg is the
    // (23,23,35) the frame paints). hanabi's nearest token is `text_secondary`
    // (142,142,154), two units off in luminance -- so the obvious move is to
    // use it for the lot, and it makes the footer WORSE: 4.08% -> 4.58%.
    //
    // The reason is that a colour constant is not what lands on screen. Over
    // this footer's ink the reference's mean (pixel - background) is 54.8 and
    // its brightest sample is 119 above background; hanabi's sprite blits
    // reach full coverage, so setting (142,142,154) puts hanabi's brightest at
    // 139 -- overshooting the reference by more than `text_faint` (100,100,112)
    // undershoots it. Puffin's 9pt/10pt SF Symbols never get to their own
    // colour; hanabi's blits do.
    //
    // Swept analytically across the whole plausible range (recolouring the ink
    // by its recovered per-pixel coverage, which is exact for a blend), the
    // BEST single colour available scores 4.32% against text_faint's 4.44% in
    // the same harness -- 0.11 points for the whole axis, because what
    // actually costs is that two of the three glyphs are different ICONS (see
    // REFERENCE.md, "The sidebar footer's three buttons").
    //
    // `feat/vis-footer` re-ran that sweep PER ELEMENT, which is the part the
    // average hid. The blits are monotonically worse at every ink above
    // text_faint -- including the gear, the one glyph whose shape matches, so
    // this is a real property of a blit and not the two mismatched icons
    // hiding a colour gap:
    //
    //     ink                       plus  search  gear
    //     text_faint  (100,100,112)   76      77    37
    //     mutedText   (140,140,166)   79      84    68
    //
    // The 11px TEXT runs the other way and gets its own constant, one line
    // per element, which is the smart-view row's two-ink rule arriving in this
    // band. An eleventh palette token to buy a tenth of a point is still not a
    // trade; two existing tokens for two different kinds of ink is.

    void render_footer(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, const LayoutComponent::Rect& r) {
        const float top = r.height - kSbFooterH;
        // Test-only (HANABI_FOCUS_AUDIT=1): whether afterhours will paint a
        // focus ring this frame, read off the thickness the renderer itself
        // uses. See test_hooks.h.
        std::string version = std::string("v") + hanabi::kVersion;
        if (hanabi::test_hooks::focus_audit()) {
            version += ctx.theme.focus_ring_thickness > 0.0f ? "  ring on"
                                                             : "  ring off";
        }
        div(ctx, mk(parent, 11),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(1)})
                .with_absolute_position()
                .with_translate(0.0f, top)
                .with_custom_background(theme::divider())
                .with_roundness(0.0f)
                .with_render_layer(2)
                .with_debug_name("sb_footer_rule"));
        div(ctx, mk(parent, 12),
            ComponentConfig{}
                .with_label(version)
                .with_size(ComponentSize{
                    pixels(hanabi::test_hooks::focus_audit() ? 200.0f : 90.0f),
                    pixels(kSbFooterH - 1.0f)})
                .with_absolute_position()
                .with_translate(
                    footer_status::label_box_x(footer_status::kFooterPadX),
                    top + 1.0f)
                .with_transparent_bg()
                // `text_secondary`, where the three glyph buttons below stay
                // on `text_faint` -- one band, two inks, for the reason the
                // smart-view row already has two (REFERENCE.md, "hanabi paints
                // an icon and the label beside it in TWO colours"). Puffin
                // hands `Chrome.mutedText` to the label and all three buttons
                // alike, and hanabi cannot: a sprite blit reaches its colour
                // and 11px text does not.
                //
                // Measured on the reference by (pixel - background), the only
                // coverage-independent read: its version label's mean ink is
                // 54.9 above the (23,23,35) it sits on, and hanabi's at
                // `text_faint` was 37.2 -- 68% of it, and visibly the dimmer
                // of the two side by side. 1.476x of text_faint's own
                // (77,77,77) delta is (114,114,125); `text_secondary` lands
                // (119,119,119), inside six levels on every channel, and it
                // follows the light theme where a hardcoded constant would
                // not. `feat/vis-tabs3` swept this axis and got a NEGATIVE
                // result, correctly, by moving the whole band at once: the
                // blits overshoot by more than the label undershoots, so one
                // constant for both is worse than either alone.
                //
                // It costs and buys exactly ZERO structural points: this rect
                // is declared in `compare.py` ("sidebar footer version
                // string", v0.5.5 against v0.1.0), so no harness can see it.
                // It is here because the reference says so and the eye can,
                // which is the only warrant a masked rectangle can have.
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(2)
                .with_debug_name("sb_version"));

        // Three actions on the reference's own 24px pitch, centred on the
        // measured x = 210 / 234 / 258, in 22px boxes.
        //
        // The pitch is 24 and the checkout says 20: `FooterMetrics.buttonWidth`
        // in ~/kt-ng2w-puffin is 20 (with a comment saying it "was 26"), while
        // the frozen frame's three glyphs sit on x205..215, x230..238 and
        // x253..263 -- centres 210/234/258, which only a 24px slot in a
        // 10px-padded 280px column produces. The checkout is v0.5.2 and the
        // reference is v0.5.5; the PNG wins on constants.
        //
        // `px` is per icon because SF Symbols and Lucide normalise a glyph
        // differently: SF sizes optically, so the reference's three footer
        // glyphs measure 11x11, 9x12 and 11x12 and read as one size, while
        // Lucide sizes by its 24-unit box, so one nominal size here draws
        // `plus` and `search` at 12x12 and `gear` at 10x12 -- the gear carries
        // more internal padding than the other two and comes out visibly
        // smaller in the same cluster. 14 puts it at 12x12, level with its
        // neighbours and against the reference's 11x12.
        //
        // Only the gear is measured against a counterpart, and deliberately:
        // it is the one pair that means the same thing. hanabi's `plus` and
        // `search` face `info.circle` and `ant` (REFERENCE.md, "The sidebar
        // footer's three buttons"), and sizing one icon set's glyph to a
        // different icon set's box is measuring the fixture -- the same claim
        // `feat/vis-tabs3` declined to make about the tab strip's `+`.
        struct FootBtn {
            const char* icon;
            const char* fallback;
            const char* name;
            int id;
            float px;
        };
        const FootBtn btns[3] = {
            {"plus", "+", "sb_new", 13, 13.0f},
            {"search", "\xf0\x9f\x94\x8d", "sb_palette", 14, 13.0f},
            {"gear", "\xe2\x9a\x99", "sb_settings_footer", 15, 14.0f},
        };
        for (int k = 0; k < 3; ++k) {
            const float cx = r.width - 70.0f + 24.0f * static_cast<float>(k);
            auto hit = button(ctx, mk(parent, btns[k].id),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(22), pixels(22)})
                    .with_absolute_position()
                    .with_translate(cx - 11.0f, top + 3.0f)
                    .with_transparent_bg()
                    .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_skip_tabbing(true)
                    .with_roundness(0.3f)
                    .with_render_layer(2)
                    .with_on_draw_fg(hanabi::icons::draw_fg(
                        btns[k].icon, btns[k].fallback, theme::text_faint(),
                        btns[k].px))
                    .with_debug_name(btns[k].name));
            if (!hit) continue;
            if (k == 0) app.composerOpen = true;
            else if (k == 1) app.paletteOpen = true;
            else app.showSettings = true;
        }

        // The live cluster — activity light + session count — where hanabi's
        // deleted bottom strip's information now lives. sidebar_footer_status.h.
        footer_status::render(ctx, parent, app, r.width, top + 1.0f,
                              kSbFooterH - 1.0f);
    }

    // The single hairline parting the sidebar from the main pane.
    //
    // On the reference it is ONE pixel at x=279 running the full window height
    // — over the tab strip above and over the footer below, not just beside the
    // list — and it is the LAST column of the 280-wide sidebar rather than a
    // column carved out between the two panes (the main pane still starts at
    // x=280). afterhours has no border-right that can outlive its own panel's
    // clip rect, and LayoutComponent's rects are consumed by four systems, so a
    // 1px-wide absolutely-positioned div on its own render layer is the way to
    // get one continuous rule instead of three segments that have to agree.
    void render_rail_divider(UIContext<InputAction>& ctx, Entity& uiRoot,
                             LayoutComponent& layout) {
        const float h =
            hanabi::viewport::height();
        const float x = layout.sidebar.x + layout.sidebar.width - 1.0f;
        div(ctx, mk(uiRoot, 1900),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(1), pixels(h)})
                .with_absolute_position()
                .with_translate(x, 0.0f)
                .with_custom_background(theme::divider())
                .with_roundness(0.0f)
                // Above the status bar (5) and the tab strip (6) so the rule is
                // unbroken top to bottom; below the row menu (kMenuLayer).
                .with_render_layer(7)
                .with_debug_name("sidebar_rail_divider"));
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
    // Measured: the field is x=9..248 (240 wide, 26 tall) at y=270..295, with a
    // filter icon OUTSIDE it whose glyph sits at x=260..273. The field is an
    // OUTLINE on the window colour, not a filled pill — Puffin fills almost
    // nothing (see PUFFIN_SPEC).
    //
    // afterhours has NO flex-grow (afterhours_gaps.md #18): a percent(1.0f)
    // child means 100% of the PARENT width, so a percent text field next to
    // fixed siblings overflows the row every frame. Every column here is
    // therefore sized in PIXELS off panelW.
    void render_search(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app, float panelX, float panelY,
                       float panelW) {
        // Wrap in a full-width padded row so the search field itself never
        // extends past the sidebar (margins on a percent(1.0) child overflow).
        auto wrap = div(ctx, mk(parent, 4),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kSbSearchH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                // -1: the pill lands on the reference's y270 rather than 271.
                // Taken off the wrap's own top padding rather than off
                // kSbSearchH, which also sets where the session list starts --
                // twenty rows are not worth one.
                .with_padding(Padding{
                    .top = pixels(kSbSearchH - kSbFieldH - 1.0f),
                                      .right = pixels(0),
                                      .bottom = pixels(0),
                                      .left = pixels(kSbInset)})
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
        // Fill: MEASURED off the reference at (36,36,48) — a step above the
        // window colour, i.e. this one IS a filled control. (The composer's
        // input and chips are outlines on the window colour; the sidebar's
        // search field is not. Sampled at ref/01_home.png y=272, x=9..248.)
        //
        // Border: the accent focus-ring when focused, and OTHERWISE THE FILL'S
        // OWN COLOUR, which is how you say "no border" to a config that has
        // already declared one. The reference has no ring at rest, and
        // afterhours has no with_no_border() / conditional-property escape on
        // the builder chain; a transparent border colour is not an option
        // either, because the rect fill cannot alpha-blend (gap #13) and would
        // render it as an opaque black outline.
        theme::Color fieldFill =
            searchHot ? theme::hover_over(theme::panel_bg_2())
                      : theme::panel_bg_2();
        theme::Color fieldBorder =
            searchFocused ? theme::focus_ring() : fieldFill;
        // Field width in pixels: the wrap's content box minus the filter
        // button that follows it (gap #18 again — the field cannot simply
        // take "the rest").
        // The pill runs x8..249 in the reference and the filter affordance
        // sits OUTSIDE it, glyph on x259..274. So: the pill, then a 5px gap,
        // then the filter's 24px box hard against the sidebar's right edge --
        // which is what centres its glyph on the reference's 266.5.
        float fieldW = panelW - kSbInset - 30.0f;
        if (fieldW < 60.0f) fieldW = 60.0f;
        auto field = div(ctx, mk(wrap.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(fieldW), pixels(kSbFieldH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(3), .right = pixels(6),
                                      .bottom = pixels(3), .left = pixels(4)})
                .with_custom_background(fieldFill)
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_border(fieldBorder,
                             pixels(searchFocused ? 1.5f : 1.0f))
                .with_roundness(0.3f)
                .with_debug_name("sb_search"));
        s_searchFieldId = field.ent().id;
        spacer_x(ctx, field.ent(), 9, 6.0f);
        div(ctx, mk(field.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                // The magnifier's slot is 7 wide with a 6px spacer in front of
                // it, and the two numbers are locked together: the glyph is
                // centred in its slot and the hint follows the slot in flow,
                // so the only way to move the glyph WITHOUT moving the hint is
                // to trade spacer for slot at a constant sum. 6 + 7 == the 13
                // that puts the hint on the reference's x=33, and it centres
                // the glyph on 21.5 where a bare 13 centred it on 18.5. The
                // glyph is 11px in a 7px box, which overhangs by 2 each side --
                // on_draw_fg paints into the widget rect and is not clipped to
                // it.
                .with_size(ComponentSize{pixels(7), pixels(18)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                // 11px, not 13: the reference's magnifier is 10px wide
                // (x 17..26) where hanabi's was 12.
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "search", "\xf0\x9f\x94\x8d", kSearchHintFg, 11.0f,
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
        float searchInner = fieldW - 10.0f;             // field content box
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
                                                 : kSearchHintFg)
                .with_font_size(kSearchPx)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                // "Search", not "Search conversations": the reference's own
                // copy, and the shorter one -- the field is already inside a
                // sidebar of conversations.
                .with_placeholder("Search")
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

        // Filter toggle, OUTSIDE the field at the sidebar's right edge (Puffin
        // puts its glyph at x=260..273). hanabi has no filter model, so this
        // drives the one filter the list already implies: automated / cron rows
        // are drawn as quiet metadata, and this hides them outright. The state
        // rides the collapsedFolders sentinel set for the same reason the VIEWS
        // fold does — AppComponent is shared and this is one boolean.
        const bool hidingAuto = app.collapsedFolders.count(kHideAutoKey) > 0;
        spacer_x(ctx, wrap.ent(), 3, 5.0f);
        auto filt = button(ctx, mk(wrap.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(24), pixels(20)})
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_skip_tabbing(true)
                .with_roundness(0.3f)
                // SF Symbols' `line.3.horizontal.decrease`, which is what
                // Puffin actually draws here -- `SidebarColumn.searchRow`
                // names it. hanabi blitted Lucide's `sliders-horizontal`: the
                // same three rules with a knob on each, which is a settings
                // control rather than a filter, and 60% more ink than the
                // reference's (59.6 against 37.2 by coverage). The rules are
                // drawn rather than atlased because Lucide's `list-filter` is
                // not this drawing either -- 18/10/4 against the reference's
                // measured 12/10/7.
                .with_on_draw_fg([fg = hidingAuto ? theme::accent()
                                                  : kSearchFilterFg](
                                     RectangleType r) {
                    hanabi::glyph::filter_rules(r, fg, theme::sidebar_bg());
                })
                .with_debug_name("sb_search_filter"));
        if (filt) {
            if (hidingAuto) app.collapsedFolders.erase(kHideAutoKey);
            else app.collapsedFolders.insert(kHideAutoKey);
        }
    }

    // ---- smart views ----
    // Puffin's order, measured: Home, Settings, Blocked, Review, Pinned,
    // Archived, on a 32px pitch starting at y=68. "Settings" is not a view —
    // it opens the settings sheet, which is where the removed brand row's gear
    // went. "Pinned" is the star feature renamed: same flag, same settings key.
    // The view row that reads as selected. hanabi flips app.view to Chat when a
    // thread is open, which leaves the sidebar with NO selection at all — the
    // reference keeps the list you came from lit while its thread is on screen,
    // so the sidebar always says where you are. Sidebar-local: the app's view
    // state is untouched, this only decides which row draws lit.
    SmartView lit_view(const AppComponent& app) {
        if (app.view != SmartView::Chat) lastListView_ = app.view;
        return lastListView_;
    }
    SmartView lastListView_ = SmartView::Home;

    // The group-membership scratch buffer, reused across frames so collecting
    // a group's members costs no allocation once the catalog has been seen at
    // its largest. Only one group renders per frame (the flat catch-all), so
    // one buffer is enough; if a second group is ever rendered in the same
    // frame this must become one buffer per nesting level, not one shared.
    std::vector<const api::SessionSummary*> members_;
    // Buffer for more_key(); see the note there.
    std::string moreKeyScratch_;
    // What the last rendered group drew, and out of how many. Test-only
    // (HANABI_ROW_AUDIT=1) -- read by the label at the foot of the panel.
    int rowsRendered_ = 0;
    int rowsMatched_ = 0;
    int rowsFirst_ = 0;

    void render_smart_views(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app, bool folded, float panelW) {
        const SmartView lit = lit_view(app);
        auto container = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                // Folded rail: left inset == the header toggle's, so the icon
                // column is flush-left in one vertical line. Unfolded: rows are
                // FULL-BLEED (Puffin's selected row runs edge to edge), so the
                // inset lives on the row's own padding, not here.
                .with_padding(Padding{.top = pixels(folded ? 0.0f
                                                           : kSbViewsTopPad),
                                      .right = pixels(folded ? 4.0f : 0.0f),
                                      .bottom = pixels(0),
                                      .left = pixels(folded ? kRailIconInset
                                                            : 0.0f)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("smart_views"));

        const ViewCounts counts = view_counts(app);
        const int review = counts.review;
        const int blocked = counts.blocked;

        // Home's count is what is WAITING: the blocked rows plus the ones done
        // and unread. Home itself is a digest, not a filter, so this is the
        // one view whose number is a sum rather than a membership count. That
        // is the reference's rule verbatim -- `SmartView.attentionCounts`
        // returns `[.home: blocked + review, .blocked: blocked, .review:
        // review]` -- and it is why the reference badges Home with 9 over a
        // Blocked of 6 and a Review of 3. This comment described the rule
        // before the row was wired; the row passed -1 and drew nothing.
        smart_item(ctx, container.ent(), 1, "home", "\xe2\x8c\x82", "Home",
                   SmartView::Home, blocked + review, app, folded, panelW, lit);
        // Settings sits between Home and Blocked in Puffin. It is a sheet, not
        // a view, so it carries no count and never reads as selected.
        if (!folded)
            settings_item(ctx, container.ent(), 2, app, panelW);
        // "hand", not "blocked": the atlas' `blocked` is Lucide's ban sign, a
        // circle-slash that reads as "forbidden" rather than "waiting on you".
        // The reference uses SF `hand.raised` (SmartViewSidebar.systemImage),
        // and the atlas now carries Lucide's hand, which is the same drawing.
        smart_item(ctx, container.ent(), 3, "hand", "\xe2\x9b\x94",
                   "Blocked", SmartView::Blocked, blocked, app, folded, panelW,
                   lit);
        // The reference's Review glyph is SF `checkmark.circle` -- the check
        // is INSIDE a ring. The atlas' `review` is a bare check.
        smart_item(ctx, container.ent(), 4, "check_circle", "\xe2\x9c\x93",
                   "Review", SmartView::Review, review, app, folded, panelW,
                   lit);
        // Pinned and Archived carry NO badge, in either client: the badge
        // means "this many things are waiting on you", and a pin is a
        // bookmark, not a queue (`SmartView.showsAttentionBadge` is true for
        // home/blocked/review and false for pinned/archived/settings). Their
        // counts are not computed at all: nothing reads them (see
        // view_counts).
        // A pin, for the view called Pinned. The star sprite stays in the
        // atlas because the per-ROW star affordance still uses it -- Puffin
        // draws the shelf with `pin` and the row action with a star, and the
        // two are different things wearing one glyph here until now.
        smart_item(ctx, container.ent(), 5, "pin", "\xf0\x9f\x93\x8c", "Pinned",
                   SmartView::Starred, -1, app, folded, panelW, lit);
        // "archive" now has a real Lucide sprite in the atlas; \xe2\x96\xa4 stays as fallback.
        smart_item(ctx, container.ent(), 6, "archive", "\xe2\x96\xa4",
                   "Archived", SmartView::Archived, -1, app, folded,
                   panelW, lit);
    }

    // A view-shaped row that opens the Settings sheet. Same geometry as
    // smart_item so the six rows keep one pitch and one icon column; it keeps
    // the sb_settings name the old header gear had, so anything addressing
    // that control still reaches it.
    void settings_item(UIContext<InputAction>& ctx, Entity& parent, int idx,
                       AppComponent& app, float panelW) {
        auto row = div(ctx, mk(parent, 100 + idx),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kSbViewRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(4),
                                      .right = pixels(kCountRightPad),
                                      .bottom = pixels(5),
                                      .left = pixels(kSbInset)})
                .with_custom_background(theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_roundness(0.0f)
                .with_debug_name("sb_settings"));
        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (pointer_click(ctx, row.ent())) app.showSettings = true;
        const theme::Color txt = kViewLabelFg;
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(16), pixels(22)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                // kViewIconFg, not `txt`: same split as smart_item's, and
                // for the same reason -- a blit reaches its colour, the label
                // beside it does not.
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "gear", "\xe2\x9a\x99", kViewIconFg, 16.0f, -1.0f))
                .with_debug_name("sv_icon"));
        // Same geometry as smart_item's label, for the same measured reasons:
        // a real spacer rather than an inert padding (gap #85), the reference's
        // 16.8px rather than BODY, and its blue-tinted ink.
        spacer_x(ctx, row.ent(), 7, kViewLabelGap);
        float labelW = panelW - kSbInset - kCountRightPad - 16.0f -
                       kViewLabelGap;
        if (labelW < 20.0f) labelW = 20.0f;
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label("Settings")
                .with_size(ComponentSize{pixels(labelW), pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(txt)
                .with_font_size(kViewLabelPx)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sv_label"));
    }

    void smart_item(UIContext<InputAction>& ctx, Entity& parent, int idx,
                    const std::string& icon_name,
                    const std::string& fallback_glyph,
                    const std::string& label, SmartView view, int count,
                    AppComponent& app, bool folded, float panelW,
                    SmartView lit) {
        bool active = lit == view;
        auto row = div(ctx, mk(parent, 100 + idx),
            ComponentConfig{}
                // Unfolded: the FILL is shorter than the row's pitch. The
                // reference's selected fill is 29px inside a 32px pitch, so it
                // is sized 29 and the missing 3 are given back as margin --
                // which keeps every later row on the 32px grid while leaving
                // 1px of window colour above the fill and 2px below.
                .with_size(ComponentSize{
                    percent(1.0f),
                    pixels(folded ? 30.0f : kSbViewRowH - kSbViewFillInset)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                // Folded rail: no row left-pad — the container's kRailIconInset
                // already sets the icon column's left edge, and the icon slot
                // (kRailIconSlot, == the header toggle box) makes the centered
                // glyph land on the same vertical line as the toggle above.
                // Unfolded: kSbInset is the ONE left inset the whole sidebar
                // shares, and kCountRightPad puts every count on one right edge.
                // 6/4, not 4/5. The fill lost 3px of height above, and the
                // row's content is centred inside what is left, so the whole
                // row rode up with it. Re-derived by sweep against the
                // reference's own label rows: at 6/4 Blocked lands 142..152
                // and Review 174..184, both exact.
                .with_padding(Padding{.top = pixels(kSvPadTop),
                                      .right = pixels(kBadgeRightPad),
                                      .bottom = pixels(folded ? 4.0f : 4.0f),
                                      .left = pixels(folded ? 0.0f
                                                            : kSbInset)})
                .with_margin(Margin{.top = pixels(kSvFillTop),
                                    .right = pixels(0),
                                    // Whatever the pitch has left under the
                                    // fill once its top margin is taken.
                                    .bottom = pixels(folded
                                                         ? 1.0f
                                                         : kSbViewFillInset -
                                                               kSvFillTop),
                                    .left = pixels(0)})
                .with_custom_background(active ? theme::selected_bg()
                                               : theme::sidebar_bg())
                // A SELECTED item does not react to hover (no double state):
                // its hover bg == its selected fill, so hovering it is a no-op.
                // Only an UNselected item gets the subtle hover wash.
                .with_custom_hover_bg(active ? theme::selected_bg()
                                             : theme::hover_over(
                                                   theme::sidebar_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                // Puffin's selected row runs the full width -- x0..278, the
                // whole sidebar -- but its corners are ROUNDED, not square:
                // measured, the fill's first row spans x3..276 and its middle
                // x0..278, which is a radius of about 5. The comment that used
                // to sit here claimed square corners and was simply wrong.
                //
                // afterhours takes a FRACTION, not a radius: it resolves to
                // min(w,h) * 0.5 * roundness, so on a 29px-tall fill 5px is
                // 5/14.5. Expressed as that division rather than as 0.34, so
                // it still means 5px if the row height ever changes.
                .with_roundness(folded ? 0.3f
                                       : kSbViewFillRadius /
                                             ((kSbViewRowH - kSbViewFillInset) *
                                              0.5f))
                .with_debug_name("smart_item"));

        row.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (pointer_click(ctx, row.ent())) {
            app.view = view;
        }

        // kViewLabelFg, not theme::text_secondary: the reference's inactive
        // view label is blue-tinted, measured by the (pixel - background)
        // ratio rather than by peak pixel. text_secondary is a neutral grey
        // and reads visibly duller beside it.
        // The SELECTED view's label is near-white and slightly violet --
        // (251,250,255), measured over its own fill rather than the window
        // colour. theme::text_primary reads 224 at its peak against the
        // reference's 251. (The reference also draws it semibold; that half is
        // gap #77 and not available to us.)
        theme::Color txt = active ? kViewLabelActiveFg : kViewLabelFg;

        // Folded rail: a smart view whose count > 0 gets a small attention dot
        // at the icon's top-right corner (Blocked = red, others = accent), so
        // the thin rail still signals "something waits here" without labels —
        // matching the mock's rail badge.
        bool railDot = folded && count > 0;
        theme::Color dotColor = (view == SmartView::Blocked)
                                    ? theme::tag_blocked_fg()
                                    : theme::accent();
        // The Blocked view's icon used to be drawn in-app as a warning
        // triangle, because the atlas' `blocked` sprite was Lucide's ban sign
        // -- a circle-slash reading as "forbidden", not "waiting on you" --
        // and a comment here listed the atlas' contents to show nothing better
        // existed. That was true when it was written. The atlas now carries
        // Lucide's `hand`, which is the reference's own glyph, so the override
        // is gone and the row draws its sprite like every other row.
        const bool useAttentionIcon = false;
        const float iconPx = folded ? 18.0f : 16.0f;
        const theme::Color iconInk = active ? kViewLabelActiveFg : kViewIconFg;
        auto attnColor = iconInk;
        auto iconDraw = hanabi::icons::draw_fg(icon_name, fallback_glyph,
                                               iconInk, kViewIconPx, -1.0f);
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{
                    pixels(folded ? kRailIconSlot : 16.0f), pixels(22)})
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
                        afterhours::draw_circle_v(afterhours::vec2{cx, cy},
                                                  hanabi::viewport::px(3.2f),
                                                  dotColor);
                    }
                })
                .with_debug_name("sv_icon"));

        if (folded) return;

        // Label column: explicit pixel width so the count box that follows is
        // pushed flush to the row's right edge (afterhours has no flex-grow, so
        // a percent label would leave the count packed mid-row, not aligned).
        // Row content = panelW − row pad (kSbInset + kCountRightPad).
        // label = content − icon(16) − the count's own width. Puffin puts the label
        // ink at x=37: kSbInset 9 + icon 16 + a 12px pad on the label = 37.
        //
        // No-overflow (defect: layout-warn spam): if icon+label+count exceed
        // the row content the NoWrap row overflows every frame (warn +
        // solve_violations churn). At narrow widths DROP the count rather than
        // clamp the label into an overflow; the label then takes the full
        // remaining width. Uses a small label floor so we only keep the count
        // while it genuinely fits.
        //
        // The count slot is sized to its OWN text plus kAhTextInset and then
        // left-aligned, not a fixed 30px box that is right-aligned: afterhours
        // insets every alignment by a hardcoded 5px that no caller can reach
        // (afterhours_gaps.md #84), so a right-aligned count can never be
        // flush. A fixed 30px box compounded it -- a one-digit count sat 5px
        // in from a box already wider than the digit needed. Both errors are
        // on the same side, which is why every badge in this sidebar has been
        // landing left of where Puffin draws it.
        const float kSvContent = panelW - kSbInset - kBadgeRightPad;
        const float kSvLabelMin = 30.0f;
        const std::string countText = std::to_string(count);
        // A capsule, not a fixed circle: the reference pads its digits by 5pt
        // each side, so a two-digit count is wider than it is tall and a fixed
        // square would clip it. One digit works out to the measured 17x17.
        float svBadgeW = kBadgeD;
        if (count > 9)
            svBadgeW = std::ceil(theme::text_px(countText.c_str(),
                                                theme::type::SM)) +
                       2.0f * kBadgePadX;
        bool svShowCount =
            count > 0 && (kSvContent - 16.0f - kSvLabelMin) >= svBadgeW;
        float svLabelW = kSvContent - 16.0f - kViewLabelGap -
                         (svShowCount ? svBadgeW : 0.0f);
        if (svLabelW < 16.0f) svLabelW = 16.0f;
        if (!folded) spacer_x(ctx, row.ent(), 7, kViewLabelGap);
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(svLabelW), pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(txt)
                .with_font_size(kViewLabelPx)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("sv_label"));

        if (svShowCount) {
            // A square box at full roundness is a circle: afterhours derives a
            // corner radius of min(w,h) * 0.5 * roundness, so 17x17 at 1.0 is
            // r=8.5. Centred text needs no slot-sizing trick — the hardcoded
            // 5px inset that makes Right alignment useless (gap #84) is
            // symmetric under Center, so it cancels.
            div(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(countText)
                    .with_size(ComponentSize{pixels(svBadgeW), pixels(kBadgeD)})
                    .with_margin(Margin{.top = pixels(0.0f),
                                        .right = pixels(0.0f),
                                        .bottom = pixels(kBadgeLift),
                                        .left = pixels(0.0f)})
                    .with_custom_background(kBadgeFill)
                    .with_border(kBadgeRing, pixels(1))
                    .with_custom_text_color(kBadgeText)
                    .with_font_size(kBadgeFontPx)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(1.0f)
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

    // Which rendered GROUP a session belongs to — the key its manual row order
    // is filed under. A named folder is its own group; everything else lands in
    // the headerless catch-all, which renders under the "recent" key.
    static std::string group_key_for(const api::SessionSummary& s) {
        return is_named_folder(s.folder) ? s.folder : std::string("recent");
    }

    static const std::string& more_key(const std::string& key,
                                       std::string& scratch) {
        return ecs::more_key(key, scratch);
    }

    // How many of a group's `total` members will actually be rendered.
    //
    // Computed ONCE per group, in render_folder, and handed to render_group --
    // the two disagreeing would be a sidebar that sorts one prefix and renders
    // another, and the surest way for them to agree is for there to be one
    // answer rather than two derivations of it.
    int visible_limit(const AppComponent& app, const std::string& key,
                      int total, int cap) {
        // A live search used to show ALL matches uncapped, on the reasoning
        // that the filter has already narrowed the list and hiding matches
        // behind "show more" would defeat it. That reasoning holds for a query
        // that narrows to a handful and fails completely for the FIRST
        // KEYSTROKE, which narrows nothing: at a 2020-session catalog, typing
        // "r" rendered every match -- 10,615 entities and 18 ms a frame,
        // thirteen times the idle cost of the same list, and it got worse with
        // every session in the catalog.
        //
        // A search result list is a list. It gets the same cap and the same
        // "Show N more" affordance as the unfiltered one, which is what that
        // affordance is for. The count in the header is still the true number
        // of matches, so the search still ANSWERS with all of them; it just
        // does not draw all of them.
        //
        // The query is no longer an input here at all. It still force-expands
        // a COLLAPSED group (render_group_header) and that rule is still
        // right: a match must not be hidden behind a fold. A cap does not hide
        // a match, it defers it to a click.
        const bool expandedMore =
            app.collapsedFolders.count(more_key(key, moreKeyScratch_)) > 0;
        return (expandedMore || total <= cap) ? total : cap;
    }

    // ---- row virtualization ---------------------------------------------
    //
    // Which slice of a group's rows is worth BUILDING this frame.
    //
    // `visible_limit` above answers a product question -- how much of this
    // list has the user asked to see. This answers the cost question: of the
    // rows they asked for, which ones are on screen. They are different
    // numbers and the gap between them is unbounded. A person with two
    // thousand threads who clicks "Show 1962 more..." has asked for two
    // thousand rows and can see about thirteen; before this, the sidebar built
    // all two thousand, every frame, forever -- 6645 entities and 17.2 ms of
    // CPU a frame against 461 and 1.55 for the same list capped.
    //
    // Rows are a fixed kRowHeight tall, so the slice is arithmetic rather than
    // a hit test: the first row is the offset divided by the pitch. Two
    // spacers stand in for the rows that were not built, so the content height
    // -- and therefore the scrollbar, the clamp, and where a given row lands
    // -- is exactly what it would be if every row were there.
    struct RowWindow {
        int first = 0;
        int last = 0;
        float above = 0.0f;
        float below = 0.0f;
        [[nodiscard]] bool whole(int limit) const {
            return first == 0 && last == limit;
        }
    };

    // Rows kept alive on each side of the viewport.
    //
    // The build runs before autolayout and before ease_scroll, so the offset
    // read here is a frame stale by exactly the distance the easing is about
    // to travel -- which is `pending` below, and is therefore covered exactly
    // rather than guessed at. That is what lets the constant be small: it is
    // not a fling budget, it is slack against rounding and against a row that
    // is a fraction of a pitch taller than kRowHeight.
    static constexpr int kOverscanRows = 3;
    // The row ids run base+1 .. base+window, and the two spacers and the
    // "Show N more..." row sit at base+197, base+198 and base+199. A window
    // this size cannot reach them. It is also 12x the tallest sidebar anyone
    // has: 190 rows at kRowHeight is a 6080 px column.
    static constexpr int kMaxWindowRows = 190;
    static constexpr int kFlingOverscanRows = 32;

    RowWindow row_window(Entity& parent, int limit, bool uniformHeight) const {
        RowWindow w;
        w.last = limit;
        // A search result carries a snippet under its row, so the rows are no
        // longer one height and the arithmetic above does not hold. Searching
        // is already capped at a viewport by visible_limit, so there is
        // nothing here to win.
        if (!uniformHeight) return w;
        if (!parent.has<afterhours::ui::HasScrollView>()) return w;
        const auto& sv = parent.get<afterhours::ui::HasScrollView>();
        const float viewH = sv.viewport_size.y;
        // Frame one: nothing has been measured yet. Build the lot; the cap
        // still bounds it, and by frame two there is a viewport to read.
        if (viewH <= 0.0f) return w;

        // Where the view is ABOUT to be, not only where it is. The offset
        // eases toward the target after this runs, so the rows between the two
        // are the ones that would otherwise be missing for a frame. Bounded at
        // kFlingOverscanRows: a fling of more than a thousand pixels in one
        // frame is a fling, and one frame of partial fill inside it is not
        // something a person can see -- whereas a window sized to an unbounded
        // fling is a fling that costs what the whole list used to.
        const float pending =
            std::fabs(sv.scroll_target.y - sv.scroll_offset.y);
        const int overscan =
            kOverscanRows +
            std::min(kFlingOverscanRows,
                     static_cast<int>(pending / kRowHeight) + 1);

        int first = static_cast<int>(sv.scroll_offset.y / kRowHeight) - overscan;
        if (first < 0) first = 0;
        int span = static_cast<int>(viewH / kRowHeight) + 2 + 2 * overscan;
        if (span > kMaxWindowRows) span = kMaxWindowRows;
        int last = first + span;
        if (last > limit) {
            // At the end of the list the window would otherwise shrink, so the
            // last screenful of a long list would build fewer rows than every
            // other screenful of it. Give the span back at the top instead:
            // the window is then the same size wherever it sits, which is what
            // makes "how much does a scrolled frame cost" one number and not a
            // function of where you stopped.
            last = limit;
            first = last - span;
            if (first < 0) first = 0;
        }
        w.first = first;
        w.last = last;
        w.above = static_cast<float>(first) * kRowHeight;
        w.below = static_cast<float>(limit - last) * kRowHeight;
        return w;
    }

    // The stand-in for rows that were not built. One entity, the exact height
    // of the rows it replaces, so every measurement downstream of it -- the
    // scroll view's content size, the scrollbar thumb, the y a row lands at --
    // is the number it would have been.
    void render_row_spacer(UIContext<InputAction>& ctx, Entity& parent, int id,
                           float h) {
        if (h <= 0.0f) return;
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(h)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sb_row_spacer"));
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
        const bool hideAutomated = app.collapsedFolders.count(kHideAutoKey) > 0;
        // Reused across frames so the collection costs no allocation once the
        // catalog has been seen at its largest. clear() keeps the capacity;
        // the old local vector malloc'd and freed one pointer per matching
        // session on EVERY frame, which at 2000 sessions is a 16 KB round trip
        // sixty times a second to render forty rows.
        std::vector<const api::SessionSummary*>& members = members_;
        members.clear();
        hanabi::prof::Scope _pcollect("sidebar.collect");
        for (const auto& s : app.sessions) {
            bool match;
            if (archived) {
                match = model::is_archived(s);
            } else if (catchAll) {
                // Recent = its own key OR any unfoldered/unknown-folder session
                // that isn't archived. Named-folder sessions are excluded so a
                // session shows in exactly one place.
                match = !model::is_archived(s) &&
                        (s.folder == key || !is_named_folder(s.folder));
            } else {
                match = (s.folder == key && !model::is_archived(s));
            }
            // Match on TITLE or, failing that, on cached CONVERSATION CONTENT
            // (local-first idea #3): the sidebar search now finds threads by
            // what was SAID in them, not just their title — using only the
            // local transcript cache (instant, offline). Content is only
            // checked when there's a query and the title didn't already match.
            // The search row's filter toggle: hide automated / cron rows.
            if (match && hideAutomated && is_automated(s.title)) continue;
            if (match &&
                (title_matches(s.title, q) ||
                 (!q.empty() && api::disk_cache::content_matches(s.id, q))))
                members.push_back(&s);
        }
        // Hide a folder with no (matching) members. With an active query this
        // is what drops non-matching folders out of the tree.
        if (members.empty()) {
            rowsRendered_ = 0;  // the row audit, below; nothing was drawn
            rowsMatched_ = 0;
            rowsFirst_ = 0;
            return 0;
        }
        const int total = static_cast<int>(members.size());
        const int limit = visible_limit(app, key, total, cap);

        // Per Gabe: do NOT day-bucket. Both named folders and the Recent
        // catch-all render as a single FLAT list, newest-first. (The old
        // Today/Yesterday/Prev-week time grouping + render_time_groups were
        // removed — dead per this decision, ponytail types pass 2026-08-03.)
        if (catchAll) {
            // Newest first, ties broken by id.
            //
            // The tie-break is not cosmetic. `updated_at` is a whole number of
            // seconds and a real catalog has plenty of rows sharing one, so
            // without it the order among tied rows is whatever the sort
            // algorithm happens to leave -- which means the list can reshuffle
            // for no reason the user can see, and it means the partial sort
            // below could legitimately render a DIFFERENT set of rows than the
            // full sort did. With it the order is a total order: one answer,
            // independent of how it was reached.
            const auto newestFirst = [](const api::SessionSummary* a,
                                        const api::SessionSummary* b) {
                if (a->updated_at != b->updated_at)
                    return a->updated_at > b->updated_at;
                return a->id < b->id;
            };
            // Sort only as far as the rows that will be drawn. The list is
            // capped at roughly two viewports, so at a 2000-session catalog a
            // full sort ordered 2000 rows to show forty of them, every frame.
            //
            // Sorting a prefix is safe against the pinned-row pass that
            // follows, and the argument is worth writing down because it is
            // the only thing making this legal: apply_row_order moves the
            // pinned rows to the front while preserving the relative order of
            // the rest, and at most `limit` rows are then rendered. If P of
            // them are pinned, the other limit-P come from the non-pinned
            // sequence -- whose first limit-P entries all lie inside the
            // sorted prefix, because the prefix holds `limit` rows and lost at
            // most P of them to the partition.
            hanabi::prof::Scope _psort("sidebar.sort");
            if (limit < total)
                std::partial_sort(members.begin(), members.begin() + limit,
                                  members.end(), newestFirst);
            else
                std::sort(members.begin(), members.end(), newestFirst);
        }
        // The rows the user has hand-arranged rise to the top of the group in
        // the order they were left in; everything else keeps the activity order
        // just established. A folder with no manual order is untouched here.
        {
            auto it = app.rowOrder.find(key);
            if (it != app.rowOrder.end())
                model::apply_row_order(members, it->second);
        }
        return render_group(ctx, parent, base, name, key, members, app, q,
                            panelW, archived, headerless, cap, limit,
                            row_window(parent, limit, q.empty()));
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
                app.collapsedFolders.erase(more_key(key, moreKeyScratch_));
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
        // + churns solve_violations — see the row width note.
        //
        // Count slot sized to its own text plus kAhTextInset and left-aligned,
        // for the reason spelled out over the smart-view count: right-aligned
        // text can never be flush (afterhours_gaps.md #84).
        const float kHeadContent = panelW - 10.0f - kCountRightPad;
        const std::string headCountText = std::to_string(count);
        const float headCountW =
            std::ceil(theme::text_px(headCountText.c_str(), theme::type::SM)) +
            kAhTextInset;
        bool headShowCount = (kHeadContent - 16.0f - 30.0f) >= headCountW;
        float headNameW = headShowCount
                              ? (kHeadContent - 16.0f - headCountW)
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
                .with_label(headCountText)
                .with_size(ComponentSize{pixels(headCountW), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
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
                     bool archived, bool headerless, int cap, int limit,
                     const RowWindow& win) {
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
        const bool expandedMore = limit >= total;
        // For the row audit (HANABI_ROW_AUDIT=1); see the label below. This is
        // rows BUILT, which since virtualization is the window and not the
        // cap: the cap is a claim about what the user asked for and the window
        // is the claim about what the frame costs, and only the second one is
        // what the audit exists to assert.
        rowsRendered_ = win.last - win.first;
        rowsMatched_ = total;
        rowsFirst_ = win.first;

        int i = 0;
        // ---- drag-to-reorder over the RENDERED band ----------------------
        // The rows below are contiguous and all kRowHeight tall, so one y (the
        // first row's) plus the count is the whole geometry the gesture needs:
        // the drop slot is a subtraction, not a hit test per row. That is what
        // keeps a drag the same cost in a folder holding thousands of rows as
        // in one holding ten — nothing off-screen is consulted, because nothing
        // off-screen is rendered.
        auto& drag = app.rowDrag;
        const bool dragIsHere = drag.live && drag.folderKey == key;
        float bandY = 0.0f;
        bool haveBand = false;
        std::vector<std::string> renderedIds;
        renderedIds.reserve(static_cast<size_t>(win.last - win.first));

        render_row_spacer(ctx, parent, base + kSpacerAboveIdOffset, win.above);

        for (int idx = win.first; idx < win.last; ++idx) {
            const auto* s = members[static_cast<size_t>(idx)];
            // The DRAG slot is the row's place in the list; the widget ID is
            // its place in the WINDOW. They have to be different numbers. A
            // reorder records where a row sits among its peers, so it needs
            // the absolute index -- but an id derived from that index would
            // mint a fresh entity for every row scrolled past and never retire
            // one (#115), which is the leak this change exists to avoid, dressed
            // up as a fix. Keying on the window slot means the same handful of
            // entities are re-used as the list moves under them, which is what
            // an immediate-mode row is.
            const int slot = idx;
            const int rowId = base + 1 + (++i);
            // While searching, a row carries the line it matched on, with the
            // matched words lit. Without it the list says WHICH threads
            // matched and never WHY, which for a content match is the whole
            // answer. The row and its snippet go inside one wrapper so the
            // snippet needs no id range of its own (the row ids are already
            // dense, and a second series would collide with a long result
            // list).
            const std::string snip = snippet_for(*s, app, q);
            afterhours::EntityID rowEnt;
            if (snip.empty()) {
                rowEnt = render_chat_row(ctx, parent, rowId, *s, app, archived,
                                         panelW);
            } else {
                auto wrap = div(ctx, mk(parent, rowId),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f),
                                                 pixels(kRowHeight + kSnippetH)})
                        .with_flex_direction(FlexDirection::Column)
                        .with_flex_wrap(FlexWrap::NoWrap)
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("sb_result"));
                rowEnt = render_chat_row(ctx, wrap.ent(), 1, *s, app, archived,
                                         panelW);
                render_snippet(ctx, wrap.ent(), snip, q, panelW);
            }
            renderedIds.push_back(s->id);
            if (!haveBand) {
                auto opt = EntityHelper::getEntityForID(rowEnt);
                if (opt.valid() &&
                    opt->has<afterhours::ui::UIComponent>()) {
                    bandY = opt->get<afterhours::ui::UIComponent>().rect().y;
                    haveBand = true;
                }
            }
            // A row is being HELD when it owns `active` with the button down —
            // afterhours keeps active on the pressed element once the cursor
            // leaves it, which is exactly what a drag needs. Rows past the
            // pinned-prefix cap are not draggable: their new place could not be
            // recorded, and a gesture that silently does nothing is worse than
            // one that never starts.
            if (ctx.mouse.left_down && ctx.is_active(rowEnt) &&
                static_cast<size_t>(slot) < model::kRowOrderMax) {
                if (drag.sessionId != s->id) {
                    drag = AppComponent::RowDrag{};
                    drag.sessionId = s->id;
                    drag.folderKey = key;
                    drag.fromIndex = static_cast<size_t>(slot);
                }
                // afterhours has already decided this press moved far enough to
                // stop being a click (MousePointerState::press_moved), and it
                // withholds the row's click on the same test — so a drag never
                // also opens the thread and nothing here has to suppress it.
                if (ctx.mouse.press_moved) drag.live = true;
            }
        }

        render_row_spacer(ctx, parent, base + kSpacerBelowIdOffset, win.below);

        if (drag.folderKey == key && !drag.sessionId.empty() && haveBand &&
            !renderedIds.empty()) {
            drag.visibleIds = renderedIds;
            drag.dropIndex = model::compute_row_drop_index(
                ctx.mouse.pos.y, bandY, kRowHeight, renderedIds.size());
            // The line sits on the leading edge of the slot being dropped into,
            // which is where the row will actually come to rest.
            drag.lineY = bandY + kRowHeight * static_cast<float>(drag.dropIndex);
        }

        // Drop. The manual order is sidebar-owned state (not the shared
        // sessions vector), so it is applied right here rather than parked as a
        // request: the geometry that decided the slot is only in hand now.
        if (ctx.mouse.just_released && dragIsHere && !drag.visibleIds.empty()) {
            auto next = model::reorder_rows(drag.visibleIds, drag.sessionId,
                                            drag.dropIndex);
            app.rowOrder[key] = next;
            Settings::get().set_row_order(key, std::move(next));
            drag = AppComponent::RowDrag{};
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
                                          .left = pixels(kRowLeftInset + kGlyphW +
                                                         kRowTitlePad)})
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
                app.collapsedFolders.insert(more_key(key, moreKeyScratch_));
            }
        }
        return total;
    }



    // ---- search snippets -------------------------------------------------
    // The line a matching row matched ON. Preference order is "the sentence
    // the user would recognise": a transcript we are already holding in memory
    // beats the list's one-line preview. The DISK cache is not read here even
    // though the filter matches against it — that is a file read per row per
    // frame, and this runs inside the render loop; a row that matched on
    // cached content it cannot afford to re-read falls back to its preview
    // (see the REPORT).
    std::string snippet_for(const api::SessionSummary& s, AppComponent& app,
                            const std::string& q) {
        if (q.empty()) return std::string();
        if (const api::Session* held = app.transcriptCache.peek(s.id)) {
            for (const auto& m : held->messages) {
                if (m.role != api::Role::User &&
                    m.role != api::Role::Assistant)
                    continue;
                const std::string sn =
                    hanabi::snippet_highlight::extract(m.text, q);
                if (!sn.empty()) return sn;
            }
        }
        const std::string sn =
            hanabi::snippet_highlight::extract(s.preview, q);
        // A title match has nothing lit in it; the preview is still the most
        // useful line to put under the title, so it renders plain.
        return sn.empty() ? s.preview : sn;
    }

    // The snippet line itself. NO padding: the highlight bands are placed from
    // the element's own rect plus the renderer's text margin (gap #51), so a
    // padded element would put the text somewhere the bands are not. The
    // indent is a margin, which moves the element and its text together.
    void render_snippet(UIContext<InputAction>& ctx, Entity& parent,
                        const std::string& text, const std::string& q,
                        float panelW) {
        const float indent = kRowLeftInset + kGlyphW + kRowTitlePad;
        float w = panelW - indent - 12.0f;
        if (w < 40.0f) w = 40.0f;
        div(ctx, mk(parent, 2),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{pixels(w), pixels(kSnippetH)})
                .with_margin(Margin{.left = pixels(indent)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_on_draw_bg([text, q](RectangleType r) {
                    hanabi::snippet_highlight::draw(r, text, q,
                                                    theme::type::SM);
                })
                .with_debug_name("sb_snippet"));
    }

    // ---- high-signal chat row ----
    // `panelW` is the live sidebar width (LayoutComponent::sidebar.width).
    // Returns the row entity's id so the caller can read its laid-out rect and
    // its press state for drag-to-reorder.
    afterhours::EntityID render_chat_row(UIContext<InputAction>& ctx,
                                         Entity& parent, int id,
                                         const api::SessionSummary& s,
                                         AppComponent& app, bool archived,
                                         float panelW) {
        // The row's state mark, straight from the shared model. No row-local
        // adjustment: a title-shaped exception here (the "-tick" one that used
        // to live at this line) is a status decided by a filename, and it
        // outranked the thread's real state.
        Mark mark = ecs::model::mark_for(s);

        // ---- STABLE row hover (fixes the "star hover flashes the whole row")
        // The trailing star is a real clickable child of the row, and hot is a
        // single entity: the moment the pointer crosses from the row body onto
        // the star, hot flips to the star, is_hot(row) goes false, and the row
        // drops its hover wash for exactly those frames — the whole row flashes.
        // The wash is therefore baked into the row's BASE fill, asking about the
        // whole subtree rather than the row entity alone, so the fill is
        // identical no matter which of {row, star} currently owns hot.
        // An unhovered row stays plain.

        // Row height is the MEASURED Puffin pitch (kRowHeight = 32). Full
        // bleed, square corners: the hover wash runs edge to edge.
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kRowHeight)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = pixels(6),
                                      .right = pixels(kCountRightPad),
                                      .bottom = pixels(6),
                                      .left = pixels(kRowLeftInset)})
                .with_custom_background(theme::sidebar_bg())
                .with_custom_hover_bg(theme::hover_over(theme::sidebar_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                // Open on RELEASE, not press. afterhours withholds a click
                // whose press moved past the drag threshold, but only on the
                // release test — a press-activated row would have opened the
                // thread before the drag it was starting could be seen.
                .with_click_activation(ClickActivationMode::Release)
                .with_roundness(0.0f)
                .with_debug_name("chat_row"));

        bool rowHot = false;
        // Bake the hover wash into the row's BASE fill whenever the pointer is
        // anywhere in the row's subtree, so the fill never flickers as hot moves
        // between the row and its star (see note above).
        {
            rowHot = ctx.mouse_in_subtree(row.ent().id) ||
                     ctx.mouse_was_in_subtree(row.ent().id);
            if (rowHot) {
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
        bool rowClicked = pointer_click(ctx, row.ent());
        bool starClicked = false;

        // Status glyph slot: a small transparent box whose foreground draw
        // paints the shape-per-status glyph. Nothing is drawn when calm.
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(kGlyphW), pixels(20)})
                .with_transparent_bg()
                .with_font_size(FontSize::Small)
                .with_roundness(0.0f)
                .with_on_draw_fg([mark, rowHot](RectangleType rect) {
                    draw_mark(rect, mark,
                              rowHot ? theme::hover_over(theme::sidebar_bg())
                                     : theme::sidebar_bg());
                })
                .with_debug_name("row_glyph"));

        // Title colour: ONE near-white for every live row. Puffin does not
        // encode attention in a title's brightness — measured, every row in the
        // reference list is (238,238,247) — so the previous
        // secondary/primary/faint split was a hanabi invention that also read
        // as three fonts. Attention still shows in the glyph. Only the
        // deliberately de-emphasized families stay faint.
        theme::Color titleColor = kRowTitleFg;
        if (archived) titleColor = theme::text_faint();
        // The row being dragged reads as lifted off the list — faint, the way a
        // semi-transparent row would, so the eye follows the drop line instead.
        if (app.rowDrag.live && app.rowDrag.sessionId == s.id)
            titleColor = theme::text_faint();

        // Title height matches the row's content box (row 32 − top/bottom
        // pad 6 = 20) so the label's own vertical-centering lands the text on
        // the row's true center — otherwise a child shorter than the content
        // box lets fontstash's ascent/descent asymmetry push the glyphs low,
        // and the (full-row) highlight bg looks off-center against the text.
        //
        // Width is in PIXELS, not percent(1.0): afterhours has no flex-grow
        // (afterhours_gaps.md #18), so a percent(1.0f) title in this NoWrap row
        // means 100% of the row and would overflow past its siblings.
        // Columns, in order:
        //   glyph slot   kGlyphW  (leading status indicator — always drawn)
        //   title        (sized in px — takes the remaining width)
        //   count slot   (only on a row that has sub-agents)
        // and, OUT OF FLOW and over the title's tail, the star.
        //
        // NO TIMESTAMP. Puffin's rows carry no time at all, so the column is
        // gone — see the commit message for what that costs the reader.
        //
        // WIDTH MATH / no-overflow (defect: layout-warn spam +
        // solve_violations churn). The columns are fixed-px in a NoWrap row,
        // so if glyph+title+star ever exceeds the row content box the row
        // overflows EVERY frame — afterhours logs a wrap/overflow warning AND
        // runs solve_violations (up to 10 iters) trying to resolve it, a real
        // per-frame drag. This hit hard through the collapse tween (280->52
        // sweeps every intermediate width). So instead of a fixed reserve plus
        // a title floor that can exceed the box, we FIT the columns to the box:
        // keep the title at a sane minimum and DROP the optional trailing
        // columns when they would not fit, rather than overflow.
        //
        // THE STAR IS NOT A COLUMN. It used to be one — 18px reserved on every
        // row whether or not anything was ever drawn in it, so that the row
        // would not reflow when the pointer arrived. That reserve came out of
        // the title, on all twenty rows, forever, to serve an affordance that
        // is invisible at rest; measured against the reference it is the whole
        // of hanabi's title-width deficit, and it truncated three of the
        // reference's twenty titles a word or two early. Puffin reserves
        // nothing here: SessionRowView's trailing items are all conditional
        // and it has no star at all. So the star is now an absolutely
        // positioned child (afterhours skips those in flow), floating over the
        // title's trailing edge on the rows that draw one, and the title gets
        // the full column back. Nothing reflows on hover either — an absolute
        // child cannot move its siblings, which is a stronger guarantee than
        // the reserved slot gave.
        const float kRowPad = kRowLeftInset + kCountRightPad;
        const float kStarW = 18.0f;       // the floating star's own box
        const float kBellW = 18.0f;       // trailing mute slot, left of the star
        const float kTitleMin = 40.0f;    // title floor before dropping columns
        float rowContent = panelW - kRowPad;
        if (rowContent < kGlyphW + 10.0f) rowContent = kGlyphW + 10.0f;
        bool showStar = (rowContent - kGlyphW - kTitleMin) >= kStarW;
        // The mute mark is claimed ONLY by a thread that is actually muted.
        // Reserving it on every row the way the star slot is reserved would tax
        // ~18px off every title in the list for an affordance almost no row
        // uses. Muting is on the row's context menu instead, so nothing appears
        // on hover and no row reflows; a muted row pays for its own mark.
        bool showBell =
            s.muted && (rowContent - kGlyphW - kTitleMin) >= kBellW;
        // The sub-agent count is claimed only by a thread that HAS sub-agents,
        // and it is measured to its own text rather than given a fixed column
        // — the same bargain the mute mark strikes, for the same reason. A
        // fixed slot wide enough for "1/3" would have taken ~20px off every
        // title in the list to serve the third of rows that spawn anything,
        // and a row of ellipsized titles is a worse list than one without
        // counts. A bare "1" costs ~6px, on its own row, and nothing else
        // moves. Checked LAST so it is the first column dropped as the
        // sidebar narrows (the collapse tween sweeps 280 -> 52 and every
        // intermediate width has to fit without overflowing).
        const std::string countLabel = ecs::model::sub_agent_label(s);
        float countW = 0.0f;
        if (!countLabel.empty()) {
            countW = std::ceil(theme::text_px(countLabel.c_str(),
                                              kCountFontPx)) +
                     kAhTextInset;
            if ((rowContent - kGlyphW - kTitleMin -
                 (showBell ? kBellW : 0.0f)) < countW)
                countW = 0.0f;
        }
        bool showCount = countW > 0.0f;
        float reserved = kGlyphW + (showBell ? kBellW : 0.0f) + countW;
        float rowTitleW = rowContent - reserved;
        if (rowTitleW < 16.0f) rowTitleW = 16.0f;  // never zero/negative
        // Ellipsize to the title column's width. At ROW size (12.5px) an avg
        // proportional glyph is ~6.0px; budget at /6.1. The label widget also
        // hard-clips at its pixel width, so a hair-generous char budget just
        // lets the text use the full column instead of ellipsizing early.
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(fit_to_width(display_title_view(s.title),
                                         theme::type::LIST_ROW,
                                         rowTitleW - kRowTitlePad -
                                             (showCount ? kCountTextPad : 0.0f)))
                .with_size(ComponentSize{pixels(rowTitleW - kRowTitleLead),
                                        pixels(20)})
                .with_margin(Margin{.left = pixels(kRowTitleLead)})
                .with_transparent_bg()
                .with_custom_text_color(titleColor)
                .with_font_size(theme::type::LIST_ROW)
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

        // The muted mark, immediately left of the star, on muted rows only.
        // Clickable, so the one row that shows it also offers the one-click way
        // out; muting in the first place is a row-menu item.
        bool bellClicked = false;
        if (showBell) {
            const std::string mid = s.id;
            auto bell = button(ctx, mk(row.ent(), 5),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(18), pixels(20)})
                    .with_transparent_bg()
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_on_draw_fg([](RectangleType r) {
                        draw_mute_mark(r, theme::text_secondary());
                    })
                    .with_debug_name("row_muted"));
            if (bell.ent().has<afterhours::HasColor>())
                bell.ent().get<afterhours::HasColor>().skip_hover_override =
                    true;
            if (bell) {
                app.requestToggleMute = mid;
                bellClicked = true;  // suppress the row's open-thread this frame
            }
        }

        // ---- The star, floating over the title's trailing edge -------------
        // Absolutely positioned, so it takes no width from the title and can
        // move nothing when it appears. It lands where the old reserved slot
        // used to sit — immediately left of the count — so the column reads
        // the same as before to anyone who was used to it.
        //
        // It paints its own 18px of row fill before the glyph. A floating
        // affordance over a left-aligned label WILL land on the tail of a long
        // title, and a star sharing pixels with a "g" is unreadable; the chip
        // is the row's own current background, so it reads as the title having
        // made room rather than as a box over it.
        if (showStar && (s.starred || rowHovered)) {
            theme::Color starColor =
                s.starred ? theme::tag_ready_fg() : theme::text_faint();
            const bool rowHot = ctx.mouse_in_subtree(row.ent().id) ||
                                ctx.mouse_was_in_subtree(row.ent().id);
            const theme::Color chip = rowHot
                ? theme::hover_over(theme::sidebar_bg())
                : theme::sidebar_bg();
            std::string sid = s.id;
            auto star = button(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(kStarW), pixels(20)})
                    .with_absolute_position(
                        pixels(panelW - kCountRightPad - countW - kStarW),
                        pixels(6.0f))
                    // No background of its own (and no hover-bg box): the star
                    // is a bare affordance that sits in the row, NOT a boxed
                    // button. transparent lets the row's own fill show through
                    // so only the glyph is visible. skip_hover_override keeps
                    // even that transparent fill from being tinted on hover.
                    .with_transparent_bg()
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_on_draw_fg([starColor, chip,
                                      st = s.starred](RectangleType r) {
                        afterhours::draw_rectangle(
                            RectangleType{r.x, r.y, r.width, r.height}, chip);
                        // Star sits toward the right of its slot but with a
                        // deliberate GAP before the count column to its right
                        // (Gabe: "add padding between the star and time").
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
        }

        // The sub-agent count: how many threads this one spawned, and how many
        // are still working. RIGHTMOST, in the slot the removed relative-time
        // column used to hold — the reference draws it flush to the row's
        // right edge, and it is data rather than an affordance, so it belongs
        // outside the star the way the timestamp did.
        //
        // A plain div, not a button: the count is a fact about the row, and
        // making it clickable would give the row a second hit target that
        // steals hot from it (the star already costs one).
        if (showCount) {
            const bool live = ecs::model::sub_agents_live(s);
            theme::Color countColor = live ? kCountLive : kCountSettled;
            if (archived) countColor = theme::text_faint();
            div(ctx, mk(row.ent(), 6),
                ComponentConfig{}
                    .with_label(countLabel)
                    .with_size(ComponentSize{pixels(countW), pixels(20)})
                    .with_transparent_bg()
                    .with_custom_text_color(countColor)
                    .with_font_size(kCountFontPx)
                    // LEFT, not Right — see kAhTextInset. The slot is sized to
                    // the text plus that inset, so left-aligning puts the
                    // digits flush against the slot's (and the row's) right
                    // edge, which is what right-aligning could not do.
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("row_subagent_count"));
        }

        // Right-click opens the row's context menu at the cursor. Always
        // offered now: Archive is machine-local, so the menu has an item to
        // show even against a backend with no rename verb.
        // offered now: Mute is machine-local, so the menu has an item to show
        // even against a backend with no rename verb.
        if (ctx.is_right_click(row.ent().id)) {
            app.rowMenuOpen = true;
            app.rowMenuSessionId = s.id;
            app.rowMenuX = ctx.mouse.pos.x;
            app.rowMenuY = ctx.mouse.pos.y;
        }

        // Apply the deferred row-open: open the thread on a row click UNLESS the
        // star was what got clicked (starring must not also open the thread).
        if (rowClicked && !starClicked && !bellClicked) {
            app.requestOpenTab = s.id;
        }
        return row.ent().id;
    }
};

}  // namespace ecs
