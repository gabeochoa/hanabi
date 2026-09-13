#pragma once

// Settings overlay (Phase K). Renders a centered settings sheet over a dimmed
// full-window backdrop when AppComponent::showSettings is true. Closes on
// Cmd+, (toggle), Esc, the ✕ close button, or clicking the backdrop.
// LAYOUT — a wide two-column sheet at normal desktop widths, collapsing to a
// single scrollable column before either column becomes cramped. Control widths
// derive from the current panel width, so both modes retain the same gutters.
// ─── PERSISTENCE + SYNC ─────────────────────────────────────────────────────
// Every WIRED control persists LOCALLY first: it writes through the Settings
// singleton, which auto-saves to the on-disk settings JSON immediately
// (mirrors set_theme). A change also flips Settings' sync-dirty flag; the
// loader (loader_system.h, drive_settings_sync) DEBOUNCES it and best-effort
// PUSHES a snapshot to the backend via ApiClient::update_settings so the web
// app matches local. With the zero-config mock the push stores in memory; a
// real backend only activates when the user sets settings_update_path in their
// LOCAL config (never committed). If no write path is configured, local-only
// persistence still works and no error is surfaced.
// The web PUT-preferences schema fields we map onto: defaultModelId, yapLevel,
// memoryBackend, notificationSound, autoArchiveDays (+ theme/font which stay
// CLIENT-LOCAL — the web schema has no theme/font field).
// WIRED (persist locally + sync):
//   Appearance · Theme  (Light/Dark/System)   — client-local (theme::set_mode).
//   Appearance · Rotate theme (Off/15m/30m/1h) — client-local; the interval
//     persists, the palette it lands on does not (theme_rotation_system.h).
//   Appearance · Font family + emphasis — client-local (FontManager/CoreText).
//   Custom colours · Accent + Find highlight (named swatches) — client-local;
//     layered over whichever palette is active (src/ui/theme.h).
//   Behavior · Yap level (No yapping/A little/Full).
//   Behavior · Auto-archive (Never/5/14/30 days).
//   Behavior · Memory backend (Traditional/Hindsight).
//   Notifications · Sound (Off/Ping).
//   Data · cache usage + clear / cache limit / export — client-local.
//   Account · identity + counts — read-only /whoami.
// Model and effort are selected from the composer's live picker, not duplicated
// here. Controls without a working action are omitted.
// "System" theme tracks the real macOS appearance via hanabi::os_is_dark_mode()
// (afterhours exposes no OS-appearance query — see afterhours_gaps.md #16).
// Owns this file only. The gear button that would toggle showSettings lives in
// sidebar_system.h (owned by another agent); Cmd+, opens/closes this overlay.

#include <array>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>
#include <algorithm>

#include <afterhours/src/plugins/files.h>

#include "../api/disk_cache.h"
#include "../settings.h"
#include "../version.h"
#include "../native_extras.h"  // hanabi::os_is_dark_mode (System theme)
#include "../keys.h"
#include "../shortcuts.h"
#include "../global_hotkeys.h"
#include "../native_extras.h"
#include "../ui/font_system.h"
#include "global_hotkey_apply.h"
#include "theme_rotation_system.h"  // theme_rotation::restart (interval clock)
#include "ui_imports.h"

#include "../ui/icons.h"
#include "../ui/secondary_surface.h"
#include "../ui/settings_catalog.h"
#include "../ui/minimap_marks.h"
#include "../ui/model_menu.h"
#include "../ui/effort_menu.h"
#include "../ui/slash_commands.h"
#include "../util/scroll_prefs.h"
#include "../ui/accessibility.h"
#include "../ui/control_state.h"
#include "../ui/edged_field.h"
#include "keyboard_focus.h"

namespace ecs {

namespace cat = hanabi::settings_catalog;

// The pane list, drawn into whatever host asks for it. The sheet calls this
// with its own column; the sidebar calls it with the sidebar's, at the width
// the sidebar reserves and over the sidebar's own fill -- the host owns both,
// so the list never paints a background or carries a width of its own.
// The anchor the hosted list wants focused this frame, handed to the sheet's
// own focus pass because the host draws first and the sheet is the one owner.
inline afterhours::EntityID& hosted_focus_anchor() {
    static afterhours::EntityID id = 0;
    return id;
}

void render_settings_pane_list(UIContext<InputAction>& ctx, Entity& parent,
                               AppComponent& app, float width, bool rail);

struct SettingsSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Apply a PENDING font swap at the top of the frame, BEFORE any text is
        // rendered this frame — mutating the FontManager's DEFAULT_FONT handle
        // mid-render (from a click handler) can leave the in-flight render using
        // a stale/half-updated font handle (freeze/crash risk). Deferring the
        // load_font to frame-top makes the swap atomic w.r.t. rendering.
        apply_pending_font();

        if (!app->showSettings) {
            if (wasOpen_) release_focus(ctx, *app);
            wasOpen_ = false;
            if (restoreFrames_ > 0) {
                --restoreFrames_;
                ctx.set_focus(restoreId_);
            }
            return;
        }

        ctx.theme.background = theme::panel_bg();
        ctx.theme.font_muted = theme::text_secondary();

        // Fetch account/settings from the backend ONCE when the overlay opens
        // (so the user can verify setup). The loader services requestSettings on
        // a worker; we only kick it when idle + not already loaded. Read-only.
        if (app->settingsState == ecs::LoadState::Idle && !app->requestSettings)
            app->requestSettings = true;

        // Keep themeChoice in sync with the palette the app booted with
        // (main.cpp applies the persisted theme via theme::set_mode at startup
        // but doesn't touch themeChoice). Only reconcile the light/dark case;
        // leave an explicit "system" choice intact.
        if (app->themeChoice != "system") {
            app->themeChoice =
                (theme::mode() == theme::Mode::Light) ? "light" : "dark";
        }

        if (!wasOpen_) adopt_open_state(ctx, *app);
        else if (!app->settingsRoute.empty()) follow_route(*app);
        wasOpen_ = true;

        if (app->escape == EscapeIntent::CloseSettings) {
            if (!app->settingsQuery.empty()) {
                clear_query(*app);
                return;
            }
            app->showSettings = false;
            release_focus(ctx, *app);
            wasOpen_ = false;
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw = hanabi::viewport::width();
        const float sh = hanabi::viewport::height();

        cat::Pane pane = current_pane(*app);
        const std::vector<const cat::Row*> paneRows = visible_rows(pane);
        const std::vector<cat::Hit> hits = cat::search(app->settingsQuery);
        const bool searching = !cat::normalize_query(app->settingsQuery).empty();

        cat::Stops stops;
        stops.content = static_cast<int>(paneRows.size());
        stops.searching = searching;
        stops.results = static_cast<int>(hits.size());
        apply_keyboard(ctx, *app, stops, hits, paneRows);

        const cat::Focus focus = read_focus(*app, stops);

        const bool rail = sw < kRailBelowWindowW;
        const float navW = rail ? kNavRailW : kNavW;
        const float wantedPh = kPadV * 2.0f + kHeaderH + kSearchRowH + kBodyH;
        const RectangleType host = layout_sidebar_rect();
        // Beside the host column, not on top of it -- but never at the cost of
        // the sheet's own margin: on a narrow window the host takes most of the
        // width, and a sheet squeezed into what is left is narrower than the
        // window can afford. Below that point the host gives the space back.
        const float beside = sw - host.width;
        const bool besideFits =
            beside >= kMinSheetW + hanabi::surface::kWindowMargin * 2.0f;
        const float hostW = besideFits ? host.width : 0.0f;
        hanabi::surface::Rect panelRect = hanabi::surface::centered(
            sw - hostW, sh, kPanelW, wantedPh);
        panelRect.x += hostW;
        active_panel_w_ = panelRect.width;
        const float ph = panelRect.height;
        const float bodyViewH = std::max(
            40.0f, ph - kPadV * 2.0f - kHeaderH - kSearchRowH);
        const float px = panelRect.x;
        const float py = panelRect.y;

        // Dimmed full-window backdrop. The UI fill pipeline has alpha blending
        // disabled (afterhours gap #13), so pre-blend a translucent black over
        // the window background via theme::over to get a real "dim" instead of
        // an opaque black slab.
        // Click-outside-to-close: the backdrop spans the whole window and sits
        // UNDER the panel, but the immediate-mode button reports a click for
        // ANY press while the cursor is over its (full-window) rect — including
        // presses that land on the panel drawn on top of it. That fired the
        // dismiss on every click inside the modal (the reported bug). Fix: only
        // dismiss when the cursor is genuinely OUTSIDE the panel rect. Esc-close
        // still works (handled above); clicks on rows/labels/empty panel space
        // now do nothing.
        auto backdrop = button(
            ctx, mk(uiRoot, 8000),
            hanabi::surface::scrim(sw, sh, 10)
                .with_debug_name("settings_backdrop"));
        if (backdrop) {
            const bool insidePanel = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{px, py, panelRect.width, ph});
            // The sidebar HOSTS the pane list while this sheet is open, so a
            // press there is navigation, not a reach past the sheet. Treat the
            // host's column as part of the surface: dismissing on a click that
            // was choosing a pane is the same bug the panel-rect check above
            // was added for, one column further left.
            const bool insideHost =
                layout_sidebar_rect().width > 0.0f &&
                afterhours::ui::is_mouse_inside(ctx.mouse.pos,
                                                layout_sidebar_rect());
            if (!insidePanel && !insideHost) {
                app->showSettings = false;
                release_focus(ctx, *app);
                wasOpen_ = false;
                return;
            }
        }

        auto panel = div(
            ctx, mk(uiRoot, 8010),
            hanabi::surface::sheet(panelRect, 11)
                .with_debug_name("settings_panel"));

        render_header(ctx, panel.ent(), *app);
        const float bodyW = std::max(120.0f, panelRect.width - kPadH * 2.0f);
        render_search_field(ctx, panel.ent(), *app, navW, bodyW);

        auto body = div(ctx, mk(panel.ent(), 500),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(bodyViewH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_body"));

        const bool hostedElsewhere = hostW > 0.0f;
        (void)besideFits;
        if (searching)
            render_results(ctx, body.ent(), *app, hits, navW, bodyViewH, focus);
        else if (!hostedElsewhere)
            render_nav(ctx, body.ent(), *app, pane, navW, bodyViewH, rail,
                       focus);

        const float listW = (searching || !hostedElsewhere) ? navW : 0.0f;
        render_pane_column(ctx, body.ent(), *app, pane, paneRows,
                           bodyW - listW, bodyViewH, focus, searching);

        if (focusAnchor_ == 0 && hosted_focus_anchor() != 0)
            focusAnchor_ = hosted_focus_anchor();
        hosted_focus_anchor() = 0;
        if (!ctx.mouse.just_pressed) {
            if (focus.zone == cat::Zone::Search) {
                if (searchFocusFrames_ > 0) --searchFocusFrames_;
                ctx.set_focus(searchFieldId_);
            } else if (focusAnchor_ != 0) {
                ctx.set_focus(focusAnchor_);
            }
        }
        focusAnchor_ = 0;

        if (app->settingsRevealFrames > 0) --app->settingsRevealFrames;
        if (app->settingsRevealFrames == 0) app->settingsRevealRow.clear();
    }

    // ---- layout constants (single source of truth for panel WIDTH + height +
    // the consistent vertical rhythm; Task B). A section = a small gap, a
    // header label, then its control. Between-section gap == kSectionGap;
    // label-> control gap is baked into the label's own bottom via kLabelH.
    // PANEL WIDTH is ONE constant (kPanelW). Every content-width computation
    // (segmented-control gutters etc.) derives from content_w() so they can't
    // drift. Widened from 360 → 600 so the whole section set fits WITHOUT
    // vertical scrolling on a normal window (Task 1).
    static constexpr float kPanelW = 720.0f;    // maximum panel width
    static constexpr float kTwoColumnMinWindowW = 720.0f;
    static constexpr float kNavW = 186.0f;
    static constexpr float kNavRailW = 96.0f;
    static constexpr float kRailBelowWindowW = 720.0f;
    static constexpr float kNavRowH = 28.0f;
    static constexpr float kNavGroupH = 26.0f;
    static constexpr float kNavGutter = 12.0f;
    static constexpr float kSearchRowH = hanabi::surface::kFieldH + 10.0f;
    static constexpr float kBodyH = 470.0f;
    static constexpr float kResultRowH = 40.0f;
    // Below this the sheet cannot sit beside the host and still be read.
    static constexpr float kMinSheetW = 520.0f;
    static constexpr float kPaneFoot = 12.0f;
    static constexpr float kPadH = hanabi::surface::kSheetPadH;
    // Usable content width inside the panel (both horizontal pads removed).
    // Usable content width for controls in the CURRENTLY-rendering column.
    // Two-column layout: set per-column to col_w() before rendering that
    // column's rows so every segmented control sizes to its column, not the
    // whole panel. Defaults to the full content width (single-column fallback).
    // ONE source of truth — no hardcoded widths drift.
    float content_w() const {
        return active_col_w_ > 0.0f ? active_col_w_ : full_content_w();
    }
    // Full inside-panel width: both horizontal pads, and the scrollbar the
    // sheet grows when its content outruns the window. Without that reserve
    // the right column's last segment sits under the bar -- which is exactly
    // how "Amber", "Shown" and "Export all" came to be shaved.
    static constexpr float kScrollbarW = 14.0f;
    static constexpr float kColGap = 24.0f;
    float full_content_w() const {
        return active_panel_w_ - kPadH * 2.0f - kScrollbarW;
    }
    // Column geometry: two equal columns split from this frame's panel width.
    float col_w() const {
        return (full_content_w() - kColGap) * 0.5f;
    }
    static constexpr float kPadV = hanabi::surface::kSheetPadV;
    static constexpr float kHeaderH = hanabi::surface::kHeaderH;
    static constexpr float kTitleH = hanabi::surface::kTitleH;
    static constexpr float kSubtitleH = hanabi::surface::kSubtitleH;
    // Group headers (Appearance / Behavior / …). ONE per group; controls stack
    // under it, each with its own compact inline name (kRowNameH). Grouping
    // keeps the whole set inside a normal window without scrolling (Task 1).
    static constexpr float kGroupGap = 14.0f;    // space above a group label
    static constexpr float kLabelH = 18.0f;      // group header label height
    static constexpr float kLabelPadB = 3.0f;    // gap under a group label
    static constexpr float kGroupH = kGroupGap + kLabelH + kLabelPadB;
    static constexpr float kRowNameH = 15.0f;
    static constexpr float kControlToNameGap = 7.0f;
    static constexpr float kRowNameGap = 3.0f;
    static constexpr float kRowNameFoot =
        kControlToNameGap + kRowNameH + kRowNameGap;
    static constexpr float kThemeRowH = 30.0f;   // segmented control
    // A segment button is exactly its row, and this constant is why there is
    // one number rather than two. Five builders (theme_choice, rotate_choice,
    // font_choice_btn, real_segmented, swatch_btn) each wrote `pixels(32)`
    // into a `pixels(kThemeRowH)` row, so forty-four buttons drew 1px above
    // and 1px below the box their row set aside for them -- the whole of
    // "many buttons are going outside the bounds", one rule spelled twice.
    // 30 and not 32 because kThemeRowH is what the SHEET is measured from
    // (kRowNameFoot + kThemeRowH is a group's height); growing the button
    // would have moved every section under it.
    static constexpr float kSegBtnH = kThemeRowH;
    static constexpr float kCacheRowH = 28.0f;   // usage + clear button
    // The export row is two lines, not one: the destination is a PATH now
    // that the user can choose it, and a path plus two buttons does not fit
    // across one 264px column without truncating the path to uselessness.
    static constexpr float kExportRowH = 52.0f;  // destination line + buttons
    static constexpr float kLimitRowH = 30.0f;   // cache-limit segmented control
    static constexpr float kAccountRowH = 24.0f;
    static constexpr float kSoonRowH = 28.0f;
    static constexpr float kFootnoteGap = 14.0f;
    static constexpr float kFootnoteH = 18.0f;   // footnote line

  public:
    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kHeaderH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_header"));
        auto titleRow = div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{
                    percent(1.0f),
                    pixels(std::max(kTitleH,
                                    hanabi::control::kMinHitTarget))})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_title_row"));
        div(ctx, mk(titleRow.ent(), 1),
            ComponentConfig{}
                .with_label("Settings")
                .with_size(ComponentSize{pixels(full_content_w() - 34.0f),
                                         pixels(kTitleH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::H1)
                .with_font_weight(theme::type::EMPHASIS)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_title"));
        auto closeBtn = button(ctx, mk(titleRow.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{
                    pixels(hanabi::control::kMinHitTarget),
                    pixels(hanabi::control::kMinHitTarget)})
                .with_margin(Margin{.left = pixels(8)})
                .with_custom_background(theme::panel_bg())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_click_activation(ClickActivationMode::Press)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "close", "\xc3\x97", theme::text_secondary(), 14.0f))
                .with_debug_name("settings_close"));
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label("How the app looks, what it keeps, and where it points")
                .with_size(ComponentSize{percent(1.0f), pixels(kSubtitleH)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_subtitle"));
        if (closeBtn) app.showSettings = false;
    }

    static RectangleType layout_sidebar_rect() {
        auto* layout = find_singleton<LayoutComponent>();
        if (layout == nullptr || layout->sidebarCollapsed)
            return RectangleType{0.0f, 0.0f, 0.0f, 0.0f};
        const auto& r = layout->sidebar;
        return RectangleType{r.x, r.y, r.width, r.height};
    }

    void adopt_open_state(UIContext<InputAction>& ctx, AppComponent& app) {
        app.settingsReturnFocus = static_cast<int>(ctx.focus_id);
        invalidate_disk_usage();
        if (!app.settingsRoute.empty()) {
            const std::string slug = app.settingsRoute;
            app.settingsRoute.clear();
            if (cat::is_known_slug(slug))
                app.settingsPane = cat::pane_info(cat::pane_from_slug(slug)).slug;
            else if (app.settingsPane.empty())
                app.settingsPane =
                    cat::pane_info(cat::pane_from_slug(
                                       Settings::get().get_settings_pane()))
                        .slug;
        } else if (app.settingsPane.empty() ||
                   !cat::is_known_slug(app.settingsPane)) {
            app.settingsPane =
                cat::pane_info(
                    cat::pane_from_slug(Settings::get().get_settings_pane()))
                    .slug;
        }
        app.settingsQuery.clear();
        app.settingsRevealRow.clear();
        app.settingsRevealFrames = 0;
        app.settingsFocusZone = static_cast<int>(cat::Zone::Search);
        app.settingsFocusIndex = 0;
        searchFocusFrames_ = 3;
    }

    void release_focus(UIContext<InputAction>& ctx, AppComponent& app) {
        if (app.settingsReturnFocus != 0) {
            const afterhours::EntityID id = restored_focus_id(app);
            if (id != 0) {
                ctx.set_focus(id);
                restoreId_ = id;
                restoreFrames_ = 3;
            }
            app.settingsReturnFocus = 0;
        }
        app.settingsQuery.clear();
        app.settingsRevealRow.clear();
        app.settingsRevealFrames = 0;
        searchFocusFrames_ = 0;
    }

    // The id captured on open, unless the widget that owned it is gone -- the
    // sidebar rebuilds its whole column while it hosts the pane list, so the
    // opener is a NEW entity by the time the sheet closes. Falling back to the
    // gear by name puts the keyboard where the reader left it rather than on
    // whatever the framework grabs first.
    static afterhours::EntityID restored_focus_id(const AppComponent& app) {
        const auto captured =
            static_cast<afterhours::EntityID>(app.settingsReturnFocus);
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(captured);
        if (opt.valid() && opt->has<afterhours::ui::UIComponent>() &&
            opt->get<afterhours::ui::UIComponent>().was_rendered_to_screen)
            return captured;
        for (const auto& handle :
             afterhours::ui::UICollectionHolder::get().collection
                 .get_entities()) {
            if (!handle) continue;
            Entity& e = *handle;
            if (!e.has<afterhours::ui::UIComponentDebug>()) continue;
            if (e.get<afterhours::ui::UIComponentDebug>().name() ==
                "sb_settings")
                return e.id;
        }
        return captured;
    }

    void clear_query(AppComponent& app) {
        app.settingsQuery.clear();
        app.settingsFocusZone = static_cast<int>(cat::Zone::Search);
        app.settingsFocusIndex = 0;
        searchFocusFrames_ = 2;
    }

    void follow_route(AppComponent& app) {
        const std::string slug = app.settingsRoute;
        app.settingsRoute.clear();
        if (!cat::is_known_slug(slug)) return;
        select_pane(app, cat::pane_from_slug(slug));
        app.settingsQuery.clear();
    }

    static cat::Pane current_pane(const AppComponent& app) {
        return cat::pane_from_slug(app.settingsPane);
    }

    static bool row_is_extra(std::string_view id) {
        return id == "global_chords" || id == "command_chords" ||
               id == "version";
    }

    static bool row_available(std::string_view id) {
        if (row_is_extra(id)) return false;
        if (id == "font_weight") return available_font_weight_count() > 1;
        return true;
    }

    void select_pane(AppComponent& app, cat::Pane pane) {
        const char* slug = cat::pane_info(pane).slug;
        if (app.settingsPane == slug) return;
        if (pane == cat::Pane::Storage) invalidate_disk_usage();
        app.settingsPane = slug;
        Settings::get().set_settings_pane(app.settingsPane);
        app.settingsFocusZone = static_cast<int>(cat::Zone::Nav);
        app.settingsFocusIndex = cat::pane_index(pane);
    }

    static cat::Focus read_focus(const AppComponent& app,
                                 const cat::Stops& stops) {
        cat::Focus f;
        f.zone = static_cast<cat::Zone>(app.settingsFocusZone);
        f.index = app.settingsFocusIndex;
        return cat::clamp_focus(f, stops);
    }

    static void write_focus(AppComponent& app, const cat::Focus& f) {
        app.settingsFocusZone = static_cast<int>(f.zone);
        app.settingsFocusIndex = f.index;
    }

    void apply_keyboard(UIContext<InputAction>& ctx, AppComponent& app,
                        const cat::Stops& stops,
                        const std::vector<cat::Hit>& hits,
                        const std::vector<const cat::Row*>& paneRows) {
        cat::Focus f = read_focus(app, stops);

        const bool shift = ctx.is_held_down(InputAction::WidgetMod);
        if (ctx.pressed(InputAction::WidgetNext)) {
            f = shift ? cat::focus_prev(f, stops) : cat::focus_next(f, stops);
            write_focus(app, f);
            if (f.zone == cat::Zone::Search) searchFocusFrames_ = 2;
            return;
        }

        if (f.zone != cat::Zone::Search) {
            int delta = 0;
            if (hanabi::keys::pressed(hanabi::keys::kDown)) delta = +1;
            if (hanabi::keys::pressed(hanabi::keys::kUp)) delta = -1;
            if (delta != 0) {
                f = cat::focus_step_in_zone(f, stops, delta);
                write_focus(app, f);
                if (f.zone == cat::Zone::Nav && !stops.searching)
                    select_pane_from_nav(app, f.index);
                return;
            }
        }

        if (f.zone == cat::Zone::Search &&
            hanabi::keys::pressed(hanabi::keys::kDown) &&
            stops.nav_stops() > 0) {
            write_focus(app, cat::Focus{cat::Zone::Nav, 0});
            return;
        }

        const bool enter = app.activate == ActivateIntent::Settings;
        const bool space = f.zone != cat::Zone::Search &&
                           hanabi::keys::pressed(hanabi::keys::kSpace);
        if (!enter && !space) return;

        if (f.zone == cat::Zone::Nav) {
            if (stops.searching) {
                if (f.index < static_cast<int>(hits.size()))
                    reveal(app, *hits[static_cast<size_t>(f.index)].row);
            } else {
                select_pane_from_nav(app, f.index);
            }
            return;
        }
        if (f.zone == cat::Zone::Content &&
            f.index < static_cast<int>(paneRows.size())) {
            pendingActivate_ = paneRows[static_cast<size_t>(f.index)]->id;
        }
    }

    void select_pane_from_nav(AppComponent& app, int index) {
        select_pane(app, cat::pane_at(index));
        app.settingsFocusZone = static_cast<int>(cat::Zone::Nav);
        app.settingsFocusIndex = index;
    }

    void reveal(AppComponent& app, const cat::Row& row) {
        select_pane(app, row.pane);
        app.settingsQuery.clear();
        app.settingsRevealRow = row.id;
        app.settingsRevealFrames = kRevealFrames;
        const std::vector<const cat::Row*> rows = visible_rows(row.pane);
        for (size_t i = 0; i < rows.size(); ++i)
            if (std::string_view(rows[i]->id) == row.id) {
                app.settingsFocusZone = static_cast<int>(cat::Zone::Content);
                app.settingsFocusIndex = static_cast<int>(i);
            }
    }

    static std::vector<const cat::Row*> visible_rows(cat::Pane pane) {
        std::vector<const cat::Row*> out;
        for (const cat::Row* r : cat::rows_in(pane))
            if (row_available(r->id)) out.push_back(r);
        return out;
    }

    void render_search_field(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app, float navW, float bodyW) {
        const float labelW = navW;
        auto row = div(ctx, mk(parent, 400),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kSearchRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_search_row"));

        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label("Search settings")
                .with_size(ComponentSize{pixels(labelW - kNavGutter),
                                         pixels(kRowNameH)})
                .with_margin(Margin{.left = pixels(4)})
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_search_label"));

        (void)bodyW;
        const float fieldW =
            std::max(80.0f, full_content_w() - (labelW > 0.0f ? labelW : 0.0f));
        auto chrome = hanabi::surface::field(fieldW, 11);
        auto input = hanabi::ui::edged_text_input(
            ctx, mk(row.ent(), 2), app.settingsQuery, chrome,
            "settings_search",
            hanabi::surface::kFieldH * hanabi::surface::kFieldFontRatio);
        hanabi::a11y::set_name(input.ent(), "Search settings");
        searchFieldId_ = focusable_field(input.ent());
        const bool pressedInField =
            ctx.mouse.just_pressed &&
            input.ent().has<afterhours::ui::UIComponent>() &&
            afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, input.ent()
                                   .get<afterhours::ui::UIComponent>()
                                   .rect());
        if (pressedInField &&
            app.settingsFocusZone != static_cast<int>(cat::Zone::Search)) {
            app.settingsFocusZone = static_cast<int>(cat::Zone::Search);
            app.settingsFocusIndex = 0;
            searchFocusFrames_ = 2;
        }
    }

    void render_nav(UIContext<InputAction>& ctx, Entity& parent,
                    AppComponent& app, cat::Pane selected, float navW,
                    float bodyH, bool rail, const cat::Focus& focus,
                    bool hosted = false) {
        auto columnCfg =
            ComponentConfig{}
                .with_size(ComponentSize{pixels(navW),
                                         hosted ? children() : pixels(bodyH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(hosted ? "sb_settings_nav" : "settings_nav");
        if (!hosted) columnCfg.with_overflow(Overflow::Scroll, Axis::Y);
        auto col = div(ctx, mk(parent, hosted ? 61 : 1), columnCfg);

        int id = 1;
        cat::Group heading = cat::Group::App;
        bool first = true;
        for (size_t i = 0; i < cat::kPanes.size(); ++i) {
            const cat::PaneInfo& info = cat::kPanes[i];
            if (!rail && (first || info.group != heading)) {
                heading = info.group;
                first = false;
                div(ctx, mk(col.ent(), id++),
                    ComponentConfig{}
                        .with_label(cat::group_heading(info.group))
                        .with_size(ComponentSize{pixels(navW - kNavGutter),
                                                 pixels(kNavGroupH)})
                        .with_margin(Margin{.top = pixels(i == 0 ? 2 : 10),
                                            .left = pixels(4)})
                        .with_align_items(AlignItems::Center)
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_faint())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_debug_name(std::string("settings_navgrp_") +
                                         cat::group_heading(info.group)));
            }

            const bool isSelected = info.pane == selected;
            const bool isFocused = focus.zone == cat::Zone::Nav &&
                                   focus.index == static_cast<int>(i);
            auto cfg =
                ComponentConfig{}
                    .with_label(info.label)
                    .with_size(ComponentSize{pixels(navW - kNavGutter),
                                             pixels(kNavRowH)})
                    .with_margin(Margin{.bottom = pixels(2), .left = pixels(4)})
                    .with_custom_background(
                        isSelected ? theme::button_primary()
                                   : (isFocused ? theme::hover_bg()
                                                : theme::panel_bg()))
                    .with_custom_hover_bg(isSelected
                                              ? theme::button_primary()
                                              : theme::hover_bg())
                    .with_custom_text_color(isSelected ? theme::window_bg()
                                                       : theme::text_primary())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Left)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{.left = pixels(10)})
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(hanabi::surface::kControlCorner)
                    .with_debug_name((hosted ? std::string("sb_settings_nav_")
                                             : std::string("settings_nav_")) +
                                     info.slug);
            auto btn = button(ctx, mk(col.ent(), id++), cfg);
            if (isFocused) focusAnchor_ = btn.ent().id;
            hanabi::a11y::set_name(
                btn.ent(),
                std::string(info.label) + (isSelected ? ", selected" : ""));
            if (btn) {
                select_pane(app, info.pane);
                if (hosted) app.showSettings = true;
            }
        }
    }

    void render_results(UIContext<InputAction>& ctx, Entity& parent,
                        AppComponent& app, const std::vector<cat::Hit>& hits,
                        float navW, float bodyH, const cat::Focus& focus) {
        auto col = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(navW), pixels(bodyH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_overflow(Overflow::Scroll, Axis::Y)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_results"));

        if (hits.empty()) {
            div(ctx, mk(col.ent(), 1),
                ComponentConfig{}
                    .with_label("No settings match")
                    .with_size(ComponentSize{pixels(navW - kNavGutter),
                                             pixels(kNavRowH)})
                    .with_margin(Margin{.top = pixels(6), .left = pixels(4)})
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("settings_results_empty"));
            div(ctx, mk(col.ent(), 2),
                ComponentConfig{}
                    .with_label("\xe2\x80\x9c" + app.settingsQuery + "\xe2\x80\x9d")
                    .with_size(ComponentSize{pixels(navW - kNavGutter),
                                             pixels(18)})
                    .with_margin(Margin{.left = pixels(4)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("settings_results_echo"));
            return;
        }

        for (size_t i = 0; i < hits.size(); ++i) {
            const cat::Row& row = *hits[i].row;
            const bool isFocused = focus.zone == cat::Zone::Nav &&
                                   focus.index == static_cast<int>(i);
            auto cfg =
                hanabi::surface::option_row(navW - kNavGutter, kResultRowH,
                                            isFocused, 11)
                    .with_margin(Margin{.top = pixels(i == 0 ? 6 : 2),
                                        .left = pixels(4)})
                    .with_flex_direction(FlexDirection::Column)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_padding(Padding{.top = pixels(3), .left = pixels(8),
                                          .bottom = pixels(3),
                                          .right = pixels(6)})
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_debug_name("settings_result_" + std::to_string(i));
            auto res = div(ctx, mk(col.ent(), 100 + static_cast<int>(i)), cfg);
            res.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
                [](Entity&) {});
            const bool clicked =
                res.ent().get<afterhours::ui::HasClickListener>().down;
            hanabi::a11y::set_name(res.ent(),
                                   std::string(row.title) + ", in " +
                                       cat::pane_info(row.pane).label);

            div(ctx, mk(res.ent(), 1),
                ComponentConfig{}
                    .with_label(row.title)
                    .with_size(ComponentSize{pixels(navW - kNavGutter - 16.0f),
                                             pixels(17)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name("settings_result_title_" +
                                     std::to_string(i)));
            div(ctx, mk(res.ent(), 2),
                ComponentConfig{}
                    .with_label(cat::pane_info(row.pane).label)
                    .with_size(ComponentSize{pixels(navW - kNavGutter - 16.0f),
                                             pixels(15)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name("settings_result_pane_" +
                                     std::to_string(i)));
            if (clicked) {
                reveal(app, row);
                return;
            }
        }
    }

    void render_pane_column(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app, cat::Pane pane,
                            const std::vector<const cat::Row*>& rows,
                            float colW, float bodyH, const cat::Focus& focus,
                            bool searching) {
        const float inner = std::max(120.0f, colW - kPadH - kScrollbarW);
        auto col = div(ctx, mk(parent, 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(colW), pixels(bodyH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_overflow(Overflow::Scroll, Axis::Y)
                .with_padding(Padding{.top = pixels(2),
                                      .left = pixels(kPadH),
                                      .bottom = pixels(kPaneFoot)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_pane_scroll"));
        Entity& p = col.ent();

        const cat::PaneInfo& info = cat::pane_info(pane);
        div(ctx, mk(p, 1),
            ComponentConfig{}
                .with_label(info.label)
                .with_size(ComponentSize{pixels(inner), pixels(22)})
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::BODY)
                .with_font_weight(theme::type::EMPHASIS)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_pane_title"));
        div(ctx, mk(p, 2),
            ComponentConfig{}
                .with_label(info.summary)
                .with_size(ComponentSize{pixels(inner), pixels(16)})
                .with_margin(Margin{.bottom = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_pane_summary"));

        render_origin_legend(ctx, p, rows, inner);

        active_col_w_ = inner;
        for (size_t i = 0; i < rows.size(); ++i) {
            const cat::Row& row = *rows[i];
            const bool isFocused = !searching &&
                                   focus.zone == cat::Zone::Content &&
                                   focus.index == static_cast<int>(i);
            const bool activate = pendingActivate_ == row.id;
            render_row(ctx, p, app, row, isFocused, activate);
        }
        pendingActivate_.clear();
        render_pane_extras(ctx, p, app, pane);
        active_col_w_ = 0.0f;
        if (app.settingsRevealFrames > 0)
            scroll_reveal_into_view(col.ent(), bodyH);
    }

    void scroll_reveal_into_view(Entity& scrollEnt, float viewH) {
        if (focusAnchor_ == 0) return;
        if (!scrollEnt.has<afterhours::ui::HasScrollView>()) return;
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(
            focusAnchor_);
        if (!opt.valid() || !opt->has<afterhours::ui::UIComponent>()) return;
        if (!scrollEnt.has<afterhours::ui::UIComponent>()) return;
        auto& sv = scrollEnt.get<afterhours::ui::HasScrollView>();
        const float rowY = opt->get<afterhours::ui::UIComponent>().rect().y;
        const float viewY =
            scrollEnt.get<afterhours::ui::UIComponent>().rect().y;
        const float rowH = opt->get<afterhours::ui::UIComponent>().rect().height;
        const float want = cat::reveal_offset(rowY, viewY, rowH, viewH,
                                              sv.scroll_offset.y);
        sv.scroll_offset.y = want;
        hanabi::set_scroll_target_y(sv, want);
        sv.clamp_scroll();
    }

    void render_origin_legend(UIContext<InputAction>& ctx, Entity& parent,
                              const std::vector<const cat::Row*>& rows,
                              float inner) {
        bool present[3] = {false, false, false};
        for (const cat::Row* r : rows)
            present[static_cast<int>(r->origin)] = true;
        std::string line;
        for (int i = 0; i < 3; ++i) {
            if (!present[i]) continue;
            const auto origin = static_cast<cat::Origin>(i);
            if (!line.empty()) line += "   \xc2\xb7   ";
            line += cat::origin_mark(origin);
            line += ": ";
            line += cat::origin_help(origin);
        }
        if (line.empty()) return;
        div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_label(line)
                .with_size(ComponentSize{pixels(inner), pixels(15)})
                .with_margin(Margin{.bottom = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_origin_legend"));
    }

    void render_row(UIContext<InputAction>& ctx, Entity& parent,
                    AppComponent& app, const cat::Row& row, bool focused,
                    bool activate) {
        rowFocused_ = focused;
        rowOrigin_ = row.origin;
        rowActivate_ = activate;
        revealRow_ = app.settingsRevealRow == row.id;
        rowTitle_ = row.title;
        const std::string_view id = row.id;

        if (id == "send_key") render_send_key_row(ctx, parent, app);
        else if (id == "new_line") render_new_line_row(ctx, parent, app);
        else if (id == "restore_tabs") render_restore_tabs_row(ctx, parent, app);
        else if (id == "timestamps") render_timestamps_row(ctx, parent, app);
        else if (id == "theme_rotate") render_theme_rotate_row(ctx, parent, app);
        else if (id == "font") render_font_row(ctx, parent, app);
        else if (id == "font_weight") render_font_weight_row(ctx, parent, app);
        else if (id == "palette") render_palette_row(ctx, parent, app);
        else if (id == "user_font") render_user_font_row(ctx, parent, app);
        else if (id == "assistant_font")
            render_assistant_font_row(ctx, parent, app);
        else if (id == "minimap_marks")
            render_minimap_marks_row(ctx, parent, app);
        else if (id == "accent") render_accent_row(ctx, parent, app);
        else if (id == "highlight") render_highlight_row(ctx, parent, app);
        else if (id == "reasoning") render_reasoning_row(ctx, parent, app);
        else if (id == "fold_long") render_foldlong_row(ctx, parent, app);
        else if (id == "date_dividers") render_date_dividers_row(ctx, parent, app);
        else if (id == "subagents") render_subagents_row(ctx, parent, app);
        else if (id == "yap") render_yap_row(ctx, parent, app);
        else if (id == "jump_latest") render_jump_latest_row(ctx, parent, app);
        else if (id == "minimap") render_minimap_row(ctx, parent, app);
        else if (id == "notify_show") render_notify_show_row(ctx, parent, app);
        else if (id == "notify_sound") render_notification_row(ctx, parent, app);
        else if (id == "quiet_hours") render_quiet_hours_row(ctx, parent, app);
        else if (id == "run_chime") render_run_chime_row(ctx, parent, app);
        else if (id == "notify_subagents")
            render_notify_subagents_row(ctx, parent, app);
        else if (id == "usage_data") render_usage_data_row(ctx, parent, app);
        else if (id == "disk_usage") render_cache_row(ctx, parent, app);
        else if (id == "disk_cap") render_cache_limit_row(ctx, parent, app);
        else if (id == "export") render_export_row(ctx, parent, app);
        else if (id == "endpoint") render_endpoint_row(ctx, parent, app);
        else if (id == "identity") render_account_row(ctx, parent, app);
        else if (id == "memory_backend") render_memory_backend_row(ctx, parent, app);
        else if (id == "auto_archive") render_autoarchive_row(ctx, parent, app);
        else if (id == "default_model") render_default_model_row(ctx, parent, app);
        else if (id == "default_effort")
            render_default_effort_row(ctx, parent, app);
        else if (id == "slash_commands")
            render_slash_commands_row(ctx, parent, app);

        rowFocused_ = false;
        rowActivate_ = false;
        revealRow_ = false;
        rowTitle_.clear();
        rowOrigin_ = cat::Origin::Device;
    }

    void render_global_rows(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app) {
        int id = 860;
        for (const auto& item : hanabi::globals::kDefinitions) {
            const bool on = Settings::get().get_global_enabled(item.slot);
            const bool armed =
                app.globalRecording == static_cast<int>(item.slot);
            auto row = div(ctx, mk(parent, id++),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(content_w()), pixels(30)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name(std::string("settings_global_row_") +
                                     std::string(item.key)));
            div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_label(std::string(item.title))
                    .with_size(ComponentSize{pixels(content_w() - 190.0f),
                                             pixels(22)})
                    .with_transparent_bg()
                    .with_custom_text_color(on ? theme::text_primary()
                                               : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name(std::string("settings_global_label_") +
                                     std::string(item.key)));

            const std::string chord =
                armed ? "Press shortcut..."
                      : hanabi::shortcuts::display(
                            Settings::get().get_global_shortcut(item.slot));
            auto recorder = button(ctx, mk(row.ent(), 2),
                ComponentConfig{}
                    .with_label(on ? chord : "Off")
                    .with_size(ComponentSize{pixels(110), pixels(26)})
                    .with_margin(Margin{.right = pixels(8)})
                    .with_custom_background(armed ? theme::selected_bg()
                                                  : theme::panel_bg_2())
                    .with_custom_hover_bg(
                        theme::hover_over(theme::panel_bg_2()))
                    .with_border(armed ? theme::accent() : theme::border(),
                                 pixels(1.0f))
                    .with_custom_text_color(on ? theme::text_secondary()
                                               : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_corner_radius(hanabi::surface::kControlCorner)
                    .with_debug_name(std::string("settings_global_record_") +
                                     std::string(item.key)));
            if (recorder && on) {
                app.globalRecording = static_cast<int>(item.slot);
                app.shortcutRecording = -1;
                app.globalMessage.clear();
                ctx.set_focus(recorder.ent().id);
            }

            auto toggle = button(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(on ? "On" : "Off")
                    .with_size(ComponentSize{pixels(58), pixels(26)})
                    .with_custom_background(on ? theme::button_primary()
                                               : theme::button_secondary())
                    .with_custom_hover_bg(on ? theme::button_primary()
                                             : theme::hover_bg())
                    .with_custom_text_color(on ? theme::window_bg()
                                               : theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(hanabi::surface::kControlCorner)
                    .with_debug_name(std::string("settings_global_switch_") +
                                     std::string(item.key)));
            hanabi::a11y::set_name(toggle.ent(),
                                   std::string(item.title) + " shortcut " +
                                       (on ? "on" : "off"));
            if (toggle) {
                Settings::get().set_global_enabled(item.slot, !on);
                if (armed) app.globalRecording = -1;
                apply_globals_or_revert(app, item.slot);
            }

            const std::string note =
                (!app.globalMessage.empty() && armed)
                    ? app.globalMessage
                    : (on ? std::string(item.help)
                          : std::string("Off — these keys go back to every "
                                        "other app."));
            div(ctx, mk(parent, id++),
                ComponentConfig{}
                    .with_label(note)
                    .with_size(ComponentSize{pixels(content_w()), pixels(16)})
                    .with_transparent_bg()
                    .with_custom_text_color(
                        (!app.globalMessage.empty() && armed)
                            ? theme::tag_blocked_fg()
                            : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name(std::string("settings_global_note_") +
                                     std::string(item.key)));
        }
        if (app.globalRecording >= 0) capture_global(app);
    }

    void capture_global(AppComponent& app) {
        const auto slot = static_cast<hanabi::globals::Slot>(
            app.globalRecording);
        std::optional<hanabi::shortcuts::Shortcut> candidate;
        int key = 0;
        unsigned char modifiers = 0;
        if (menubar_take_recorded_shortcut(&key, &modifiers))
            candidate = hanabi::shortcuts::Shortcut{key, modifiers};
        else
            candidate = hanabi::keys::capture_shortcut();
        if (!candidate.has_value()) return;

        const auto other = slot == hanabi::globals::Slot::NewTask
                               ? hanabi::globals::Slot::Palette
                               : hanabi::globals::Slot::NewTask;
        const auto check = hanabi::globals::validate(
            slot, *candidate, Settings::get().get_global_shortcut(other));
        if (!check.ok) {
            app.globalMessage = check.explanation;
            return;
        }
        const auto previous = Settings::get().get_global_shortcut(slot);
        Settings::get().set_global_shortcut(slot, *candidate);
        app.globalRecording = -1;
        if (!apply_globals(app)) {
            Settings::get().set_global_shortcut(slot, previous);
            apply_globals(app);
            app.globalRecording = static_cast<int>(slot);
            app.globalMessage =
                "Another app already owns that chord. Kept " +
                hanabi::shortcuts::display(previous) + ".";
            return;
        }
        app.globalMessage.clear();
    }

    static bool apply_globals(AppComponent&) {
        return apply_global_hotkeys_from_settings();
    }

    void apply_globals_or_revert(AppComponent& app,
                                 hanabi::globals::Slot slot) {
        if (apply_globals(app)) {
            app.globalMessage.clear();
            return;
        }
        Settings::get().set_global_enabled(
            slot, !Settings::get().get_global_enabled(slot));
        apply_globals(app);
        app.globalMessage =
            "Another app already owns that chord, so it stays as it was.";
    }

    void render_shortcut_rows(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app) {
        int id = 900;
        for (const auto& item : hanabi::shortcuts::kDefinitions) {
            auto row = div(ctx, mk(parent, id++),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(content_w()), pixels(30)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name(std::string("settings_chord_row_") +
                                     std::string(item.key)));
            const bool on = Settings::get().get_shortcut_enabled(item.command);
            const bool armed =
                app.shortcutRecording == static_cast<int>(item.command);

            div(ctx, mk(row.ent(), 1),
                ComponentConfig{}
                    .with_label(std::string(item.title))
                    .with_size(ComponentSize{pixels(content_w() - 190.0f),
                                             pixels(22)})
                    .with_transparent_bg()
                    .with_custom_text_color(on ? theme::text_primary()
                                               : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name(std::string("settings_chord_label_") +
                                     std::string(item.key)));

            const std::string chord =
                armed ? "Press shortcut..."
                      : hanabi::shortcuts::display(
                            Settings::get().get_shortcut(item.command));
            auto recorder = button(ctx, mk(row.ent(), 2),
                ComponentConfig{}
                    .with_label(on ? chord : "Off")
                    .with_size(ComponentSize{pixels(110), pixels(26)})
                    .with_margin(Margin{.right = pixels(8)})
                    .with_custom_background(armed ? theme::selected_bg()
                                                  : theme::panel_bg_2())
                    .with_custom_hover_bg(
                        theme::hover_over(theme::panel_bg_2()))
                    .with_border(armed ? theme::accent() : theme::border(),
                                 pixels(1.0f))
                    .with_custom_text_color(on ? theme::text_secondary()
                                               : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_corner_radius(hanabi::surface::kControlCorner)
                    .with_debug_name(std::string("settings_chord_record_") +
                                     std::string(item.key)));
            if (recorder && on) {
                app.shortcutRecording = static_cast<int>(item.command);
                app.shortcutMessage.clear();
                ctx.set_focus(recorder.ent().id);
            }

            auto toggle = button(ctx, mk(row.ent(), 3),
                ComponentConfig{}
                    .with_label(on ? "On" : "Off")
                    .with_size(ComponentSize{pixels(58), pixels(26)})
                    .with_custom_background(on ? theme::button_primary()
                                               : theme::button_secondary())
                    .with_custom_hover_bg(on ? theme::button_primary()
                                             : theme::hover_bg())
                    .with_custom_text_color(on ? theme::window_bg()
                                               : theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(hanabi::surface::kControlCorner)
                    .with_debug_name(std::string("settings_chord_switch_") +
                                     std::string(item.key)));
            hanabi::a11y::set_name(toggle.ent(),
                                   std::string(item.title) + " shortcut " +
                                       (on ? "on" : "off"));
            if (toggle) {
                Settings::get().set_shortcut_enabled(item.command, !on);
                if (armed) app.shortcutRecording = -1;
            }

            if (!app.shortcutMessage.empty() && armed) {
                div(ctx, mk(parent, id++),
                    ComponentConfig{}
                        .with_label(app.shortcutMessage)
                        .with_size(ComponentSize{pixels(content_w()),
                                                 pixels(16)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::tag_blocked_fg())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_roundness(0.0f)
                        .with_debug_name(
                            std::string("settings_chord_refusal_") +
                            std::string(item.key)));
            } else if (Settings::get().get_shortcut(item.command) !=
                       item.shortcut) {
                div(ctx, mk(parent, id++),
                    ComponentConfig{}
                        .with_label("was " +
                                    hanabi::shortcuts::display(item.shortcut))
                        .with_size(ComponentSize{pixels(content_w()),
                                                 pixels(14)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::text_faint())
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_debug_name(std::string("settings_chord_was_") +
                                         std::string(item.key)));
            }
        }
    }

    void render_shortcut_reset(UIContext<InputAction>& ctx, Entity& parent,
                               AppComponent& app) {
        const bool clean = Settings::get().shortcuts_are_default();
        if (app.shortcutResetArmed && !clean) {
            div(ctx, mk(parent, 980),
                ComponentConfig{}
                    .with_label("Reset all keyboard shortcuts? Every command "
                                "goes back to the keys it ships with, and any "
                                "you switched off come back on. There is no "
                                "undo.")
                    .with_size(ComponentSize{pixels(content_w()), pixels(32)})
                    .with_margin(Margin{.top = pixels(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("settings_chord_reset_confirm"));
            auto buttons = div(ctx, mk(parent, 981),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(content_w()), pixels(28)})
                    .with_margin(Margin{.top = pixels(6)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("settings_chord_reset_buttons"));
            auto go = button(ctx, mk(buttons.ent(), 1),
                ComponentConfig{}
                    .with_label("Reset shortcuts")
                    .with_size(ComponentSize{pixels(140), pixels(26)})
                    .with_margin(Margin{.right = pixels(8)})
                    .with_custom_background(
                        hanabi::surface::destructive_surface())
                    .with_custom_hover_bg(theme::hover_over(
                        hanabi::surface::destructive_surface()))
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(hanabi::surface::kControlCorner)
                    .with_debug_name("settings_chord_reset_go"));
            auto cancel = button(ctx, mk(buttons.ent(), 2),
                ComponentConfig{}
                    .with_label("Cancel")
                    .with_size(ComponentSize{pixels(90), pixels(26)})
                    .with_custom_background(theme::button_secondary())
                    .with_custom_hover_bg(theme::hover_bg())
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_corner_radius(hanabi::surface::kControlCorner)
                    .with_debug_name("settings_chord_reset_cancel"));
            if (go) {
                Settings::get().reset_shortcuts();
                apply_globals(app);
                app.shortcutResetArmed = false;
            }
            if (cancel) app.shortcutResetArmed = false;
            return;
        }
        app.shortcutResetArmed = false;
        auto reset = button(ctx, mk(parent, 982),
            ComponentConfig{}
                .with_label("Reset all to defaults")
                .with_size(ComponentSize{pixels(content_w()), pixels(26)})
                .with_margin(Margin{.top = pixels(10)})
                .with_disabled(clean)
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(clean ? theme::text_faint()
                                              : theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_debug_name("settings_chord_reset"));
        if (reset && !clean) app.shortcutResetArmed = true;
    }

    void render_pane_extras(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app, cat::Pane pane) {
        if (pane == cat::Pane::Shortcuts) {
            render_global_rows(ctx, parent, app);
            render_shortcut_rows(ctx, parent, app);
            render_shortcut_reset(ctx, parent, app);
            render_shortcut_editor_row(ctx, parent, app);
            div(ctx, mk(parent, 940),
                ComponentConfig{}
                    .with_label("Standard editing and navigation chords are "
                                "the system's and cannot be rebound here.")
                    .with_size(ComponentSize{pixels(content_w()), pixels(34)})
                    .with_margin(Margin{.top = pixels(10)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("settings_shortcuts_note"));
            return;
        }
        if (pane == cat::Pane::About) render_footnote(ctx, parent, app);
    }

    // Group header: a slightly-prominent label with a fixed gap above it
    // (kGroupGap) and a small gap to its first control below (kLabelPadB). ONE
    // per group (Appearance / Behavior / …); the group's controls stack under
    // it, each with its own compact inline row_name(). Uses MARGIN (not
    // padding) for the gaps: the autolayout stacks children by computed size +
    // margin, so margin adds real space BETWEEN elements.
    void group_label(UIContext<InputAction>& ctx, Entity& parent, int id,
                     const std::string& text, const std::string& dbg) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(kLabelH)})
                .with_margin(Margin{.top = pixels(kGroupGap),
                                    .bottom = pixels(kLabelPadB)})
                // Vertically center the label in its box so ascenders don't clip
                // against the box/scroll top edge (esp. the first group).
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::BODY)
                .with_font_weight(theme::type::EMPHASIS)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name(dbg));
    }

    void row_name(UIContext<InputAction>& ctx, Entity& parent, int id,
                  const std::string& text, const std::string& dbg) {
        row_name_with(ctx, parent, id, text, std::string(), dbg);
    }

    void row_name_with(UIContext<InputAction>& ctx, Entity& parent, int id,
                       const std::string& text, const std::string& suffix,
                       const std::string& dbg) {
        auto line = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()),
                                         pixels(kRowNameH)})
                .with_margin(Margin{.top = pixels(kControlToNameGap),
                                    .bottom = pixels(kRowNameGap)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_line"));

        const bool revealed = revealRow_ && !text.empty();
        std::string shown = rowTitle_.empty() ? text : rowTitle_;
        shown += suffix;
        if (revealed) shown = "\xe2\x80\xba " + shown;  // > , the reveal marker
        div(ctx, mk(line.ent(), 1),
            ComponentConfig{}
                .with_label(shown)
                .with_size(ComponentSize{
                    pixels(std::max(60.0f,
                                    content_w() - kOriginMarkW - kOriginGap)),
                    pixels(kRowNameH)})
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_text_color(revealed ? theme::text_primary()
                                                 : theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_name"));
        div(ctx, mk(line.ent(), 2),
            ComponentConfig{}
                .with_label(cat::origin_mark(rowOrigin_))
                .with_size(ComponentSize{pixels(kOriginMarkW),
                                         pixels(kRowNameH)})
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_origin"));
    }

    void anchor_control(Entity& control) {
        control.addComponentIfMissing<afterhours::ui::HasClickListener>(
            [](Entity&) {});
        if (rowFocused_) focusAnchor_ = control.id;
    }

    int activated_index(int selectedIdx, int n) const {
        if (!rowActivate_ || n <= 0) return -1;
        return (std::max(0, selectedIdx) + 1) % n;
    }

    void render_theme_row(UIContext<InputAction>& ctx, Entity& parent,
                          AppComponent& app) {
        // Appearance group -> Theme. NOTE: client-local only (not an API
        // setting) — the web PUT /api/user/preferences has no theme field.
        row_name(ctx, parent, 2, "Theme", "settings_theme_label");

        auto row = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_theme_row"));
        anchor_control(row.ent());

        // Full-width segmented control (V8: hug both edges, don't float centered).
        // afterhours has no flex-grow (gap #18) so size each segment from the
        // real content width (content_w() = kPanelW − both pads), minus the
        // inter-segment gaps, split N ways. No trailing margin so the group
        // spans exactly left-inset → right-inset.
        constexpr float kSegGap = 6.0f;
        const float content = content_w();
        const float segW = (content - kSegGap * 2.0f) / 3.0f;
        theme_choice(ctx, row.ent(), 1, "Light", "light", app, segW, true);
        theme_choice(ctx, row.ent(), 2, "Dark", "dark", app, segW, true);
        theme_choice(ctx, row.ent(), 3, "System", "system", app, segW, false);

        static const char* kThemeValues[3] = {"light", "dark", "system"};
        int cur = 0;
        for (int i = 0; i < 3; ++i)
            if (app.themeChoice == kThemeValues[i]) cur = i;
        if (const int stepped = activated_index(cur, 3); stepped >= 0)
            apply_theme(app, kThemeValues[stepped]);
    }

    // Appearance group -> Rotate theme. Off / 15m / 30m / 1h, sharing the theme
    // row's gutter math. Rotation is what the picker above could not do: leave
    // the sheet and the palette keeps moving on its own.
    // The row NAME carries which palette is up right now. Rotation moves the
    // theme while the sheet is open, and the picker above says which one only
    // in colour — which is not something a person glancing at the sheet can
    // read off, nor anything a test can assert.
    void render_theme_rotate_row(UIContext<InputAction>& ctx, Entity& parent,
                                 AppComponent& app) {
        (void)app;
        const int secs = Settings::get().get_theme_rotate_secs();
        std::string name = "Rotate theme";
        std::string suffix;
        if (secs > 0) {
            suffix += "   \xc2\xb7   showing ";
            suffix += (theme::mode() == theme::Mode::Light) ? "Light" : "Dark";
        }
        row_name_with(ctx, parent, 150, name, suffix,
                      "settings_theme_rotate_label");

        auto row = div(ctx, mk(parent, 151),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_theme_rotate_row"));
        anchor_control(row.ent());

        constexpr float kSegGap = 6.0f;
        const float segW = (content_w() - kSegGap * 3.0f) / 4.0f;
        rotate_choice(ctx, row.ent(), 1, "Off", "off", 0, segW, true);
        rotate_choice(ctx, row.ent(), 2, "15m", "15m", 15 * 60, segW, true);
        rotate_choice(ctx, row.ent(), 3, "30m", "30m", 30 * 60, segW, true);
        rotate_choice(ctx, row.ent(), 4, "1h", "1h", 60 * 60, segW, false);

        static const int kRotateSecs[4] = {0, 15 * 60, 30 * 60, 60 * 60};
        int curIdx = 0;
        for (int i = 0; i < 4; ++i)
            if (kRotateSecs[i] == secs) curIdx = i;
        if (const int stepped = activated_index(curIdx, 4); stepped >= 0) {
            Settings::get().set_theme_rotate_secs(kRotateSecs[stepped]);
            theme_rotation::restart();
        }
    }

    // One segmented rotation-interval button. "Off" is the 0 case, so an
    // interval and an enabled flag can never contradict each other.
    void rotate_choice(UIContext<InputAction>& ctx, Entity& parent, int id,
                       const std::string& label, const std::string& dbg,
                       int secs, float segW, bool trailingGap) {
        const int current = Settings::get().get_theme_rotate_secs();
        // Any interval the sheet cannot express (a test's two seconds) still has
        // to light SOMETHING other than Off, or the sheet would claim rotation
        // is off while the theme is visibly moving.
        const bool selected = secs == 0 ? current <= 0
                              : current == secs ||
                                    (current > 0 && current < 15 * 60 &&
                                     secs == 15 * 60);
        auto btn = button(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                .with_margin(Margin{.right = pixels(trailingGap ? 6.0f : 0.0f)})
                .with_custom_background(selected ? theme::button_primary()
                                                 : theme::button_secondary())
                .with_custom_hover_bg(selected ? theme::button_primary()
                                               : theme::hover_bg())
                .with_custom_text_color(selected ? theme::window_bg()
                                                 : theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_theme_rotate_" + dbg));
        if (btn) {
            Settings::get().set_theme_rotate_secs(secs);
            // A fresh interval starts now, not part-way through the old one.
            theme_rotation::restart();
        }
    }

    void render_font_row(UIContext<InputAction>& ctx, Entity& parent,
                         AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 6, "App font", "settings_font_label");

        auto row = div(ctx, mk(parent, 7),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_font_row"));
        anchor_control(row.ent());

        const auto& options = hanabi::fonts::families();
        constexpr float kSegGap = 6.0f;
        const float segW = (content_w() - kSegGap *
            static_cast<float>(options.size() > 0 ? options.size() - 1 : 0)) /
            static_cast<float>(std::max<std::size_t>(options.size(), 1));
        for (std::size_t i = 0; i < options.size(); ++i) {
            const auto& option = options[i];
            font_choice_btn(ctx, row.ent(), static_cast<int>(i) + 1,
                            option.label, option.key, segW,
                            i + 1 < options.size());
        }

        const std::string activeFamily = hanabi::fonts::effective_family(
            Settings::get().get_font_choice());
        int curFont = 0;
        for (std::size_t i = 0; i < options.size(); ++i)
            if (options[i].key == activeFamily) curFont = static_cast<int>(i);
        if (const int stepped =
                activated_index(curFont, static_cast<int>(options.size()));
            stepped >= 0)
            pick_font(options[static_cast<std::size_t>(stepped)].key);
    }

    static std::size_t available_font_weight_count() {
        const std::string family = hanabi::fonts::effective_family(
            Settings::get().get_font_choice());
        std::size_t count = 0;
        for (const auto& option : hanabi::fonts::weights())
            if (hanabi::fonts::weight_available(family, option.key)) ++count;
        return count;
    }

    void render_font_weight_row(UIContext<InputAction>& ctx, Entity& parent,
                                AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 8, "Emphasis", "settings_font_weight_label");

        auto row = div(ctx, mk(parent, 9),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_font_weight_row"));
        anchor_control(row.ent());

        const std::string family = hanabi::fonts::effective_family(
            Settings::get().get_font_choice());
        const std::size_t availableCount = available_font_weight_count();
        constexpr float kSegGap = 6.0f;
        const float segW = (content_w() - kSegGap *
            static_cast<float>(availableCount > 0 ? availableCount - 1 : 0)) /
            static_cast<float>(std::max<std::size_t>(availableCount, 1));
        std::size_t shown = 0;
        for (const auto& option : hanabi::fonts::weights()) {
            if (!hanabi::fonts::weight_available(family, option.key)) continue;
            const std::size_t index = shown++;
            font_weight_btn(ctx, row.ent(), static_cast<int>(index) + 1,
                            option.label, option.key, segW,
                            index + 1 < availableCount);
        }

        std::vector<std::string> weightKeys;
        for (const auto& option : hanabi::fonts::weights())
            if (hanabi::fonts::weight_available(family, option.key))
                weightKeys.push_back(option.key);
        const std::string activeWeight =
            hanabi::fonts::effective_weight(family,
                                            Settings::get().get_font_weight());
        int curWeight = 0;
        for (std::size_t i = 0; i < weightKeys.size(); ++i)
            if (weightKeys[i] == activeWeight) curWeight = static_cast<int>(i);
        if (const int stepped =
                activated_index(curWeight, static_cast<int>(weightKeys.size()));
            stepped >= 0) {
            Settings::get().set_font_weight(
                weightKeys[static_cast<std::size_t>(stepped)]);
            queue_font_apply();
        }
    }

    static void pick_font(const std::string& value) {
        auto& settings = Settings::get();
        settings.set_font_choice(value);
        if (!hanabi::fonts::weight_available(value, settings.get_font_weight()))
            settings.set_font_weight("regular");
        queue_font_apply();
    }

    void font_choice_btn(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const std::string& label, const std::string& value,
                         float segW, bool trailingGap) {
        const bool selected =
            hanabi::fonts::effective_family(Settings::get().get_font_choice()) ==
            value;
        auto btn = button(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                .with_margin(Margin{.right = pixels(trailingGap ? 6.0f : 0.0f)})
                .with_custom_background(selected ? theme::button_primary()
                                                 : theme::button_secondary())
                .with_custom_hover_bg(selected ? theme::button_primary()
                                               : theme::hover_bg())
                .with_custom_text_color(selected ? theme::window_bg()
                                                 : theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_font_" + value));
        if (!btn) return;
        pick_font(value);
    }

    void font_weight_btn(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const std::string& label, const std::string& value,
                         float segW, bool trailingGap) {
        const std::string family = hanabi::fonts::effective_family(
            Settings::get().get_font_choice());
        const bool selected = hanabi::fonts::effective_weight(
                                  family, Settings::get().get_font_weight()) ==
                              value;
        auto btn = button(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                .with_margin(Margin{.right = pixels(trailingGap ? 6.0f : 0.0f)})
                .with_custom_background(selected ? theme::button_primary()
                                                 : theme::button_secondary())
                .with_custom_hover_bg(selected ? theme::button_primary()
                                               : theme::hover_bg())
                .with_custom_text_color(selected ? theme::window_bg()
                                                 : theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_font_weight_" + value));
        if (!btn) return;
        Settings::get().set_font_weight(value);
        queue_font_apply();
    }

    static bool& pending_font() {
        static bool pending = false;
        return pending;
    }

    static void queue_font_apply() { pending_font() = true; }

    static void apply_pending_font() {
        if (!pending_font()) return;
        pending_font() = false;
        auto& fontMgr =
            afterhours::EntityHelper::get_singleton_cmp_enforce<
                afterhours::ui::FontManager>();
        hanabi::fonts::apply(fontMgr, Settings::get().get_font_choice(),
                             Settings::get().get_font_weight());
    }

    static std::uint64_t& disk_usage_cache() {
        static std::uint64_t bytes = 0;
        return bytes;
    }
    static bool& disk_usage_valid() {
        static bool valid = false;
        return valid;
    }
    static void invalidate_disk_usage() { disk_usage_valid() = false; }
    static std::uint64_t disk_usage_bytes() {
        if (!disk_usage_valid()) {
            disk_usage_cache() = api::disk_cache::total_bytes();
            disk_usage_valid() = true;
        }
        return disk_usage_cache();
    }

    // Human-readable byte size: B / KB / MB.
    static std::string human_bytes(std::uint64_t b) {
        char buf[32];
        if (b < 1024ull) std::snprintf(buf, sizeof(buf), "%llu B",
                                       (unsigned long long)b);
        else if (b < 1024ull * 1024)
            std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
        else
            std::snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
        return buf;
    }

    // Cache row: on-disk transcript cache usage + a "Clear cache" button
    // (wired to the data layer's disk_cache::total_bytes()/wipe_all()).
    void render_cache_row(UIContext<InputAction>& ctx, Entity& parent,
                          AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 20, "On-disk cache", "settings_cache_label");

        auto row = div(ctx, mk(parent, 21),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kCacheRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_cache_row"));
        anchor_control(row.ent());

        const std::uint64_t bytes = disk_usage_bytes();
        std::string usage = human_bytes(bytes) + " on disk";
        if (app.cacheWipeReported)
            usage += " · " + human_bytes(app.cacheWipeReclaimedBytes) +
                     " reclaimed";
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(usage)
                .with_size(ComponentSize{children(), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_cache_usage"));

        auto clear = button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label("Clear")
                .with_size(ComponentSize{pixels(104), pixels(28)})
                .with_custom_background(theme::button_secondary())
                .with_custom_hover_bg(theme::hover_over(theme::button_secondary()))
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_cache_clear"));
        if (clear || rowActivate_) {
            app.clear_transcript_cache();
            const auto result = api::disk_cache::wipe_all_report();
            app.cacheWipeReclaimedBytes = result.bytes_reclaimed;
            app.cacheWipeReported = true;
            invalidate_disk_usage();
        }
    }

    // Data / export row (local-first idea #4): a "Data" section with the export
    // destination on the left and, hugging the right edge, the two things you
    // can do with it — choose where it goes, and send it there.
    // Writes every cached transcript to <destination>/*.md — user-owned,
    // survives a backend sunset. A transient "· exported N" note confirms.
    // The destination used to be ~/hanabi/threads and nothing else, which is a
    // fine default and a poor only-option: the whole point of the export is
    // that the copies are YOURS, and yours generally means "in the folder I
    // keep things in". "Choose…" opens the native folder picker
    // (native_pick_directory, NSOpenPanel) and the answer is remembered.
    void render_export_row(UIContext<InputAction>& ctx, Entity& parent,
                           AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 60, "Export", "settings_data_label");
        auto row = div(ctx, mk(parent, 61),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kExportRowH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_export_row"));
        anchor_control(row.ent());

        static int s_exported = -1;  // -1 = not yet; >=0 = last export count
        const std::string dest = export_destination();
        std::string left = "Export threads to " + tilde(dest);
        if (s_exported >= 0)
            left = "Exported " + std::to_string(s_exported) + " threads to " +
                   tilde(dest);
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(left)
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_export_usage"));

        auto buttons = div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_margin(Margin{.top = pixels(6)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_export_buttons"));

        auto choose = button(ctx, mk(buttons.ent(), 1),
            ComponentConfig{}
                .with_label("Choose\xe2\x80\xa6")
                .with_size(ComponentSize{pixels(84), pixels(28)})
                .with_margin(Margin{.right = pixels(8)})
                .with_custom_background(theme::button_secondary())
                .with_custom_hover_bg(theme::hover_over(theme::button_secondary()))
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_export_choose"));
        if (choose) {
            std::string picked;
            if (ask_for_directory(&picked)) {
                Settings::get().set_export_dir(picked);
                s_exported = -1;  // a new destination has exported nothing yet
            }
        }

        auto exp = button(ctx, mk(buttons.ent(), 2),
            ComponentConfig{}
                .with_label("Export all")
                .with_size(ComponentSize{pixels(104), pixels(28)})
                .with_custom_background(theme::button_secondary())
                .with_custom_hover_bg(theme::hover_over(theme::button_secondary()))
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_export_btn"));
        if (exp || rowActivate_)
            s_exported = api::disk_cache::export_all_markdown(dest);
    }

    // Where the export writes: the folder the user chose, or the built-in
    // default when they never chose one.
    static std::string export_destination() {
        const std::string& chosen = Settings::get().get_export_dir();
        return chosen.empty() ? api::disk_cache::export_dir() : chosen;
    }

    // A path as a person reads it: $HOME collapsed back to "~". The row is one
    // line and an absolute path under a long home directory pushes the buttons
    // off it.
    static std::string tilde(const std::string& path) {
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == 0) return path;
        const std::string h(home);
        if (path.rfind(h, 0) != 0) return path;
        return "~" + path.substr(h.size());
    }

    // Ask the user for a folder. Normally the native panel; when
    // HANABI_PICK_DIR_TEST is set it answers with that path instead, because a
    // modal NSOpenPanel is not in the widget tree and a scripted test that
    // clicked "Choose…" for real would hang against a panel nothing can
    // dismiss. Everything the answer touches — persistence, the row, where the
    // export actually lands — is then exercised for real; only the panel
    // itself stays manual.
    static bool ask_for_directory(std::string* out) {
        if (const char* forced = std::getenv("HANABI_PICK_DIR_TEST");
            forced != nullptr && *forced != 0) {
            *out = forced;
            return true;
        }
        char buf[1024];
        if (!native_pick_directory("Choose", buf, sizeof(buf))) return false;
        *out = buf;
        return true;
    }

    // Cache-limit options (label, bytes). 0 == Unlimited (no eviction).
    // Default is 1 GB (see Settings::cache_cap_bytes_).
    struct CapOption {
        const char* label;
        std::uint64_t bytes;
    };
    static constexpr CapOption kCapOptions[] = {
        {"100 MB", 100ull * 1024 * 1024},
        {"1 GB", 1024ull * 1024 * 1024},
        {"10 GB", 10ull * 1024ull * 1024 * 1024},
        {"Unlimited", 0},
    };

    // Cache-limit row: a segmented control (100 MB / 1 GB / 10 GB / Unlimited)
    // that caps the on-disk transcript cache. Persisted via Settings; changing
    // it immediately trims the cache to the new cap so the effect is visible.
    void render_cache_limit_row(UIContext<InputAction>& ctx, Entity& parent,
                                AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 40, "Cache limit", "settings_cache_limit_label");

        auto row = div(ctx, mk(parent, 41),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kLimitRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_cache_limit_row"));
        anchor_control(row.ent());

        div(ctx, mk(parent, 42),
            ComponentConfig{}
                .with_label("The cap covers cached conversations. Your "
                            "settings, exports and logs are not counted and "
                            "are never trimmed.")
                .with_size(ComponentSize{pixels(content_w()), pixels(15)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_cache_scope"));
        const std::uint64_t current = Settings::get().get_cache_cap_bytes();
        // Full-width 4-segment control (V8: hug both edges). Size from the real
        // content width (content_w()) minus 3 inter-segment gaps; no trailing
        // margin so the group spans exactly left-inset → right-inset.
        constexpr float kSegGap = 4.0f;
        const float segW = (content_w() - kSegGap * 3.0f) / 4.0f;
        int idx = 1;
        for (int i = 0; i < 4; ++i) {
            const auto& opt = kCapOptions[i];
            const bool selected = (opt.bytes == current);
            const bool last = (i == 3);
            auto btn = button(ctx, mk(row.ent(), idx++),
                ComponentConfig{}
                    .with_label(opt.label)
                    .with_size(ComponentSize{pixels(segW), pixels(30)})
                    .with_margin(Margin{.right = pixels(last ? 0.0f : kSegGap)})
                    .with_custom_background(selected ? theme::button_primary()
                                                     : theme::button_secondary())
                    .with_custom_hover_bg(selected ? theme::button_primary()
                                                   : theme::hover_bg())
                    .with_custom_text_color(selected ? theme::window_bg()
                                                     : theme::text_primary())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name(std::string("settings_cache_limit_") +
                                     std::to_string(i)));
            if (btn) apply_cache_cap(opt.bytes);
        }
        int curCap = 1;
        for (int i = 0; i < 4; ++i)
            if (kCapOptions[i].bytes == current) curCap = i;
        if (const int stepped = activated_index(curCap, 4); stepped >= 0)
            apply_cache_cap(kCapOptions[stepped].bytes);
    }

    static void apply_cache_cap(std::uint64_t bytes) {
        Settings::get().set_cache_cap_bytes(bytes);
        api::disk_cache::trim_to_cap(bytes);
        invalidate_disk_usage();
    }

    // ── Shared control helpers ─────────────────────────────────────────────
    // A REAL, working segmented control: N clickable segments, `selectedIdx`
    // highlighted (accent fill, like theme/font/cache-limit). Clicking segment
    // i invokes onPick(i). Used by every WIRED Behavior/Notifications row —
    // each persists locally + marks the sync-dirty flag through its onPick.
    template <typename Fn>
    void real_segmented(UIContext<InputAction>& ctx, Entity& parent,
                        int baseId, const std::vector<std::string>& labels,
                        int selectedIdx, const std::string& dbg, Fn onPick) {
        auto rowCfg =
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_row");
        auto row = div(ctx, mk(parent, baseId), rowCfg);
        anchor_control(row.ent());
        constexpr float kSegGap = 6.0f;
        const int n = static_cast<int>(labels.size());
        const float segW =
            (content_w() - kSegGap * (n - 1)) / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const bool sel = (i == selectedIdx);
            const bool last = (i == n - 1);
            auto btn = button(ctx, mk(row.ent(), i + 1),
                ComponentConfig{}
                    .with_label(labels[static_cast<size_t>(i)])
                    .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                    .with_margin(Margin{.right = pixels(last ? 0.0f : kSegGap)})
                    .with_custom_background(sel ? theme::button_primary()
                                                : theme::button_secondary())
                    .with_custom_hover_bg(sel ? theme::button_primary()
                                              : theme::hover_bg())
                    .with_custom_text_color(sel ? theme::window_bg()
                                                : theme::text_primary())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name(dbg + "_" + std::to_string(i)));
            if (btn) onPick(i);
        }
        const int stepped = activated_index(selectedIdx, n);
        if (stepped >= 0) onPick(stepped);
    }

    // ── Behavior group ─────────────────────────────────────────────────────
    // Yap level / verbosity (0 = No yapping, 1 = A little, 2 = Full). WIRED:
    // reads Settings::get_yap_level(), writes on click (auto-persists locally +
    // marks the sync-dirty flag so the loader pushes it to the backend). Maps
    // onto the web preferences yapLevel field.
    void render_yap_row(UIContext<InputAction>& ctx, Entity& parent,
                        AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 100, "Yap level", "settings_yap_label");
        const int cur = Settings::get().get_yap_level();
        real_segmented(ctx, parent, 101, {"No yapping", "A little", "Full"},
                       cur, "settings_yap",
                       [](int i) { Settings::get().set_yap_level(i); });
    }

    // Auto-archive after N days. WIRED: a small set of preset day-counts as a
    // segmented control; the selection persists locally + syncs (autoArchiveDays).
    void render_autoarchive_row(UIContext<InputAction>& ctx, Entity& parent,
                                AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 110, "Auto-archive", "settings_autoarchive_label");
        static const int kDays[] = {0, 5, 14, 30};
        const int cur = Settings::get().get_auto_archive_days();
        int selIdx = 1;  // default highlight = 5 days if no exact match
        for (int i = 0; i < 4; ++i)
            if (kDays[i] == cur) selIdx = i;
        real_segmented(ctx, parent, 111,
                       {"Never", "5 days", "14 days", "30 days"}, selIdx,
                       "settings_autoarchive",
                       [](int i) {
                           Settings::get().set_auto_archive_days(kDays[i]);
                       });
    }

    // Memory backend: Traditional vs Hindsight. WIRED locally (persists +
    // syncs the choice as memoryBackend). Note: Hindsight is admin-gated
    // server-side, but the client-local preference is safe to store either way.
    void render_memory_backend_row(UIContext<InputAction>& ctx, Entity& parent,
                                   AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 120, "Memory backend", "settings_memory_label");
        const bool hind =
            (Settings::get().get_memory_backend() == "hindsight");
        real_segmented(ctx, parent, 121, {"Traditional", "Hindsight"},
                       hind ? 1 : 0, "settings_memory",
                       [](int i) {
                           Settings::get().set_memory_backend(
                               i == 1 ? "hindsight" : "traditional");
                       });
    }

    void render_default_model_row(UIContext<InputAction>& ctx, Entity& parent,
                                  AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 500, "Default model", "settings_default_model");
        const auto& models = hanabi::models::all();
        const std::string current = Settings::get().get_default_model();
        std::vector<std::string> labels;
        labels.reserve(models.size());
        for (const auto& m : models) labels.emplace_back(m.name);
        int selected = 0;
        for (std::size_t i = 0; i < models.size(); ++i)
            if (models[i].id == current) selected = static_cast<int>(i);
        radio_list(ctx, parent, 501, labels, selected, "settings_default_model",
                   [](int i) {
                       Settings::get().set_default_model(
                           std::string(hanabi::models::all()
                                           [static_cast<std::size_t>(i)]
                                               .id));
                   });
        div(ctx, mk(parent, 502),
            ComponentConfig{}
                .with_label("Starts the next conversation; a running one keeps "
                            "the model it began on.")
                .with_size(ComponentSize{pixels(content_w()), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_default_model_note"));
    }

    void render_default_effort_row(UIContext<InputAction>& ctx, Entity& parent,
                                   AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 530, "Thinking effort", "settings_default_effort");
        const auto& levels = hanabi::effort::all();
        std::vector<std::string> labels;
        labels.reserve(levels.size());
        for (const auto& l : levels) labels.emplace_back(l.name);
        const std::size_t at =
            hanabi::effort::index_of(Settings::get().get_default_effort());
        const int selected = at < levels.size()
                                 ? static_cast<int>(at)
                                 : static_cast<int>(hanabi::effort::index_of(
                                       hanabi::effort::default_id()));
        real_segmented(ctx, parent, 531, labels, selected,
                       "settings_default_effort", [](int i) {
                           Settings::get().set_default_effort(std::string(
                               hanabi::effort::all()
                                   [static_cast<std::size_t>(i)]
                                       .id));
                       });
    }

    void render_slash_commands_row(UIContext<InputAction>& ctx, Entity& parent,
                                   AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 540, "Slash commands", "settings_slash");
        const auto& commands = hanabi::slash::all();
        for (std::size_t i = 0; i < commands.size(); ++i) {
            const auto& c = commands[i];
            std::string verb = "/" + std::string(c.name);
            if (!c.arg.empty()) verb += " " + std::string(c.arg);
            std::string line = verb + "  \xc2\xb7  " + std::string(c.blurb);
            if (!c.runnable) line += "  \xc2\xb7  not yet: " + c.unwired;
            auto row = div(ctx, mk(parent, 541 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(line)
                    .with_size(ComponentSize{
                        pixels(content_w()),
                        pixels(hanabi::control::kMinHitTarget)})
                    .with_transparent_bg()
                    .with_custom_text_color(c.runnable ? theme::text_secondary()
                                                       : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("settings_slash_" + std::string(c.name)));
            if (i == 0) anchor_control(row.ent());
        }
        div(ctx, mk(parent, 560),
            ComponentConfig{}
                .with_label("Type a slash at the start of the composer to see "
                            "this list there.")
                .with_size(ComponentSize{pixels(content_w()), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_slash_note"));
    }

    template <typename Fn>
    void radio_list(UIContext<InputAction>& ctx, Entity& parent, int baseId,
                    const std::vector<std::string>& labels, int selectedIdx,
                    const std::string& dbg, Fn onPick) {
        const int n = static_cast<int>(labels.size());
        auto col = div(ctx, mk(parent, baseId),
            ComponentConfig{}
                .with_size(ComponentSize{
                    pixels(content_w()),
                    pixels((kSegBtnH + kRadioGap) * static_cast<float>(n))})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_list"));
        anchor_control(col.ent());
        for (int i = 0; i < n; ++i) {
            const bool sel = (i == selectedIdx);
            auto btn = button(ctx, mk(col.ent(), i + 1),
                ComponentConfig{}
                    .with_label(labels[static_cast<size_t>(i)])
                    .with_size(ComponentSize{pixels(content_w()),
                                             pixels(kSegBtnH)})
                    .with_margin(Margin{.bottom = pixels(kRadioGap)})
                    .with_padding(Padding{.left = pixels(10)})
                    .with_custom_background(sel ? theme::button_primary()
                                                : theme::button_secondary())
                    .with_custom_hover_bg(sel ? theme::button_primary()
                                              : theme::hover_bg())
                    .with_custom_text_color(sel ? theme::window_bg()
                                                : theme::text_primary())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Left)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name(dbg + "_" + std::to_string(i)));
            if (btn) onPick(i);
        }
        const int stepped = activated_index(selectedIdx, n);
        if (stepped >= 0) onPick(stepped);
    }

    static constexpr float kRadioGap = 4.0f;

    // Timestamps on transcript rows: Off / On. Local to this machine, so it
    // persists without going near the sync-dirty flag.
    void render_timestamps_row(UIContext<InputAction>& ctx, Entity& parent,
                               AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 136, "Timestamps", "settings_timestamps_label");
        const bool on = Settings::get().get_show_timestamps();
        real_segmented(ctx, parent, 137, {"Off", "On"}, on ? 1 : 0,
                       "settings_timestamps",
                       [](int i) {
                           Settings::get().set_show_timestamps(i == 1);
                       });
    }

    // Which key sends. Return by default; Cmd+Return for people who would
    // rather Return stayed harmless. WIRED: persists locally (send_key), no
    // sync — the backend payload has no field for it.
    void render_send_key_row(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 138, "Send with", "settings_send_key_label");
        const bool cmdReturn =
            Settings::get().get_send_key() == hanabi::kSendKeyCmdReturn;
        real_segmented(ctx, parent, 139, {"Return", "Cmd+Return"},
                       cmdReturn ? 1 : 0, "settings_send_key",
                       [](int i) {
                           Settings::get().set_send_key(
                               i == 1 ? hanabi::kSendKeyCmdReturn
                                      : hanabi::kSendKeyReturn);
                       });
    }

    void render_shortcut_editor_row(UIContext<InputAction>& ctx, Entity& parent,
                                    AppComponent& app) {
        row_name(ctx, parent, 140, "Keyboard shortcuts",
                 "settings_shortcuts_label");
        auto open = button(
            ctx, mk(parent, 141),
            ComponentConfig{}
                .with_label("Customize shortcuts...")
                .with_size(ComponentSize{pixels(content_w()), pixels(kSegBtnH)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_debug_name("settings_open_shortcuts"));
        if (open) {
            app.showSettings = false;
            app.showShortcuts = true;
            app.shortcutRecording = -1;
            app.shortcutMessage.clear();
        }
    }

    // Sub-agent chips in the transcript rollup: hide the finished ones, or
    // list them all. Global, and local-only — how much of a thread's helper
    // work this machine wants to read is not a preference another device wants
    // pushed onto it.
    void render_subagents_row(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 142, "Finished sub-agents",
                 "settings_subagents_label");
        const bool on = Settings::get().get_show_finished_subagents();
        real_segmented(ctx, parent, 143, {"Hide", "Show"}, on ? 1 : 0,
                       "settings_subagents",
                       [](int i) {
                           Settings::get().set_show_finished_subagents(i == 1);
                       });
    }

    // Date dividers: the day row above the first message of a new calendar
    // day. Off is not cosmetic — the row is dropped from the item list, so it
    // is neither measured nor drawn.
    void render_date_dividers_row(UIContext<InputAction>& ctx, Entity& parent,
                                  AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 144, "Date dividers", "settings_dates_label");
        const bool on = Settings::get().get_show_date_dividers();
        real_segmented(ctx, parent, 145, {"Off", "On"}, on ? 1 : 0,
                       "settings_dates",
                       [](int i) {
                           Settings::get().set_show_date_dividers(i == 1);
                       });
    }

    // Reasoning blocks. Hidden drops the "Thought for a moment" rows from the
    // transcript entirely; the answer they sit above is untouched.
    void render_reasoning_row(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 146, "Reasoning", "settings_reasoning_label");
        const bool on = Settings::get().get_show_reasoning();
        real_segmented(ctx, parent, 147, {"Hidden", "Shown"}, on ? 1 : 0,
                       "settings_reasoning",
                       [](int i) {
                           Settings::get().set_show_reasoning(i == 1);
                       });
    }

    // Long messages: fold a very long one behind a "Show more" button, or
    // render every one at full length.
    void render_foldlong_row(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 148, "Long messages", "settings_foldlong_label");
        const bool on = Settings::get().get_fold_long_messages();
        real_segmented(ctx, parent, 149, {"Full", "Fold"}, on ? 1 : 0,
                       "settings_foldlong",
                       [](int i) {
                           Settings::get().set_fold_long_messages(i == 1);
                       });
    }

    // ── Notifications group ────────────────────────────────────────────────
    // Notification sound: Off / Ping. WIRED: persists locally (notificationSound)
    // + syncs.
    void render_notification_row(UIContext<InputAction>& ctx, Entity& parent,
                                 AppComponent& app) {
        (void)app;
        const bool master = Settings::get().get_notifications_enabled();
        row_name_with(ctx, parent, 130, "Play a sound",
                      master ? std::string()
                             : std::string("   \xc2\xb7   notifications are off"),
                      "settings_notif_label");
        const bool on = Settings::get().get_notification_sound();
        real_segmented(ctx, parent, 131, {"Off", "Ping"}, on ? 1 : 0,
                       "settings_notif",
                       [master](int i) {
                           if (!master) return;
                           Settings::get().set_notification_sound(i == 1);
                       });
    }

    // Quiet hours: a window when nothing may fire. Presets rather than a time
    // picker — the UI has no clock control, and a wrong custom window silences
    // you without telling you. Stored as minutes so a real picker can replace
    // these without migrating anyone's settings.
    struct QuietPreset {
        const char* label;
        int start;
        int end;
    };
    static constexpr std::array<QuietPreset, 4> kQuietPresets{{
        {"Off", 0, 0},
        {"10pm-8am", 22 * 60, 8 * 60},
        {"11pm-7am", 23 * 60, 7 * 60},
        {"6pm-9am", 18 * 60, 9 * 60},
    }};

    void render_quiet_hours_row(UIContext<InputAction>& ctx, Entity& parent,
                                AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 132, "Quiet hours", "settings_quiet_label");
        const int start = Settings::get().get_quiet_start_minutes();
        const int end = Settings::get().get_quiet_end_minutes();
        int selIdx = 0;
        for (size_t i = 0; i < kQuietPresets.size(); ++i)
            if (kQuietPresets[i].start == start && kQuietPresets[i].end == end)
                selIdx = static_cast<int>(i);
        real_segmented(ctx, parent, 133,
                       {kQuietPresets[0].label, kQuietPresets[1].label,
                        kQuietPresets[2].label, kQuietPresets[3].label},
                       selIdx, "settings_quiet", [](int i) {
                           Settings::get().set_quiet_window(
                               kQuietPresets[static_cast<size_t>(i)].start,
                               kQuietPresets[static_cast<size_t>(i)].end);
                       });
    }

    void render_account_row(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app) {
        row_name(ctx, parent, 30, "Signed in as", "settings_identity");
        std::string line;
        theme::Color col = theme::text_secondary();
        if (app.settingsState == ecs::LoadState::Loading) {
            line = "checking\xe2\x80\xa6";
        } else if (app.settingsState == ecs::LoadState::Error) {
            line = "couldn't reach the backend";
            col = theme::tag_blocked_fg();
        } else if (app.settings.ok) {
            // e.g. "gabeochoa@\u2026  \u00b7  19048 sessions  \u00b7  62 schedules"
            line = app.settings.user_id.empty() ? "(no identity)"
                                                : app.settings.user_id;
            if (app.settings.session_count >= 0)
                line += "  \xc2\xb7  " +
                        std::to_string(app.settings.session_count) + " sessions";
            if (app.settings.schedule_count >= 0)
                line += "  \xc2\xb7  " +
                        std::to_string(app.settings.schedule_count) +
                        " schedules";
            col = theme::text_secondary();
        } else {
            // Idle with no data — the backend doesn't expose settings, or mock.
            line = "not available on this backend";
        }
        auto readout = div(ctx, mk(parent, 31),
            ComponentConfig{}
                .with_label(line)
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(col)
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_account_value"));
        anchor_control(readout.ent());

    }

    void theme_choice(UIContext<InputAction>& ctx, Entity& parent, int id,
                      const std::string& label, const std::string& value,
                      AppComponent& app, float segW = 102.0f,
                      bool trailingGap = true) {
        bool selected = (app.themeChoice == value);
        auto btn = button(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                .with_margin(Margin{.right = pixels(trailingGap ? 6.0f : 0.0f)})
                .with_custom_background(selected ? theme::button_primary()
                                                 : theme::button_secondary())
                .with_custom_hover_bg(selected ? theme::button_primary()
                                               : theme::hover_bg())
                .with_custom_text_color(selected ? theme::window_bg()
                                                 : theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_theme_" + value));
        if (btn) apply_theme(app, value);
    }

    static void apply_theme(AppComponent& app, const std::string& value) {
        app.themeChoice = value;
        // "System" now tracks the real macOS appearance (macos_is_dark_mode,
        // AppleInterfaceStyle) instead of always falling back to Dark — gap #16
        // resolved. Light/Dark are explicit; System resolves live.
        bool light;
        if (value == "system")
            light = !hanabi::os_is_dark_mode();
        else
            light = (value == "light");
        auto& s = Settings::get();
        // Persist the CHOICE (light/dark/system) so System stays System across
        // relaunch and re-resolves against the OS each launch.
        s.set_theme(value);
        s.write_save_file();
        theme::set_mode(light ? theme::Mode::Light : theme::Mode::Dark);
        // Picking a theme by hand buys a WHOLE interval of it, rather than
        // however many seconds were left on the rotation clock.
        theme_rotation::restart();
    }

    // ── Custom colours (the theme editor) ───────────────────────────────────
    // Two NAMED tokens are editable — the accent family and the find highlight
    // — each as a row of named swatches. What this is not is the colour-picker
    // modal the breakdown sketched: afterhours has no colour-picker widget and
    // no way to raise the macOS one, and a hex field would let one RGB apply to
    // both palettes, which is how the light theme shipped muddy the first time.
    // Each swatch instead carries its own dark and light colour (theme.h).
    // The row name carries the chosen swatch, because the swatch row itself
    // says which one is live only in colour — unreadable at a glance next to a
    // full sheet of blue segments, and invisible to a test.
    void render_accent_row(UIContext<InputAction>& ctx, Entity& parent,
                           AppComponent& app) {
        (void)app;
        row_name_with(ctx, parent, 160, "Accent",
                      "   \xc2\xb7   " +
                          swatch_label(theme::accent_choice(),
                                       theme::kAccentSwatches,
                                       std::size(theme::kAccentSwatches)),
                      "settings_accent_label");
        auto row = swatch_row(ctx, parent, 161, "settings_accent_row");
        anchor_control(row.ent());
        const float segW = swatch_seg_w(std::size(theme::kAccentSwatches) + 1);
        swatch_btn(ctx, row.ent(), 1, "Default", theme::kDefaultChoice, segW,
                   true, "settings_accent_", theme::accent_choice(),
                   [](const std::string& k) { apply_accent(k); });
        int id = 2;
        for (const auto& sw : theme::kAccentSwatches) {
            const bool last = (&sw == std::end(theme::kAccentSwatches) - 1);
            swatch_btn(ctx, row.ent(), id++, sw.label, sw.key, segW, !last,
                       "settings_accent_", theme::accent_choice(),
                       [](const std::string& k) { apply_accent(k); });
        }
        if (const std::string next = stepped_swatch(
                theme::accent_choice(), theme::kAccentSwatches,
                std::size(theme::kAccentSwatches));
            !next.empty())
            apply_accent(next);
    }

    void render_highlight_row(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app) {
        (void)app;
        row_name_with(ctx, parent, 162, "Find highlight",
                      "   \xc2\xb7   " +
                          swatch_label(theme::highlight_choice(),
                                       theme::kHighlightSwatches,
                                       std::size(theme::kHighlightSwatches)),
                      "settings_highlight_label");
        auto row = swatch_row(ctx, parent, 163, "settings_highlight_row");
        anchor_control(row.ent());
        const float segW = swatch_seg_w(std::size(theme::kHighlightSwatches) + 1);
        swatch_btn(ctx, row.ent(), 1, "Default", theme::kDefaultChoice, segW,
                   true, "settings_highlight_", theme::highlight_choice(),
                   [](const std::string& k) { apply_highlight(k); });
        int id = 2;
        for (const auto& sw : theme::kHighlightSwatches) {
            const bool last = (&sw == std::end(theme::kHighlightSwatches) - 1);
            swatch_btn(ctx, row.ent(), id++, sw.label, sw.key, segW, !last,
                       "settings_highlight_", theme::highlight_choice(),
                       [](const std::string& k) { apply_highlight(k); });
        }
        if (const std::string next = stepped_swatch(
                theme::highlight_choice(), theme::kHighlightSwatches,
                std::size(theme::kHighlightSwatches));
            !next.empty())
            apply_highlight(next);
    }

    std::string stepped_swatch(const std::string& current,
                               const theme::Swatch* list, size_t n) const {
        const int count = static_cast<int>(n) + 1;
        int cur = 0;
        for (size_t i = 0; i < n; ++i)
            if (list[i].key == current) cur = static_cast<int>(i) + 1;
        const int stepped = activated_index(cur, count);
        if (stepped < 0) return {};
        return stepped == 0 ? std::string(theme::kDefaultChoice)
                            : list[static_cast<size_t>(stepped - 1)].key;
    }

    static std::string swatch_label(const std::string& key,
                                    const theme::Swatch* list, size_t n) {
        const theme::Swatch* sw = theme::find_swatch(list, n, key);
        return sw ? sw->label : "Default";
    }

    afterhours::ui::imm::ElementResult swatch_row(
        UIContext<InputAction>& ctx, Entity& parent, int id,
        const std::string& dbg) {
        return div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(dbg));
    }

    float swatch_seg_w(size_t segments) const {
        constexpr float kSegGap = 6.0f;
        const float n = static_cast<float>(segments);
        return (content_w() - kSegGap * (n - 1.0f)) / n;
    }

    // One swatch button. The chosen one is filled with the colour it selects
    // (not the generic accent fill the other segmented controls use) so the row
    // reads as a set of colours rather than a set of words.
    template <typename Apply>
    void swatch_btn(UIContext<InputAction>& ctx, Entity& parent, int id,
                    const std::string& label, const std::string& key,
                    float segW, bool trailingGap, const std::string& dbgPrefix,
                    const std::string& current, Apply apply) {
        const bool selected = (current == key);
        auto btn = button(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                .with_margin(Margin{.right = pixels(trailingGap ? 6.0f : 0.0f)})
                .with_custom_background(selected ? theme::button_primary()
                                                 : theme::button_secondary())
                .with_custom_hover_bg(selected ? theme::button_primary()
                                               : theme::hover_bg())
                .with_custom_text_color(selected ? theme::window_bg()
                                                 : theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name(dbgPrefix + key));
        if (btn) apply(key);
    }

    // Apply + persist a colour choice. theme::set_*_choice re-layers the token
    // over the ACTIVE palette immediately, so the sheet retints this frame.
    static void apply_accent(const std::string& key) {
        theme::set_accent_choice(key);
        Settings::get().set_accent_choice(key);  // auto-persists
    }
    static void apply_highlight(const std::string& key) {
        theme::set_highlight_choice(key);
        Settings::get().set_highlight_choice(key);  // auto-persists
    }

    void render_footnote(UIContext<InputAction>& ctx, Entity& parent,
                         AppComponent& app) {
        std::string note =
            app.themeChoice == "system"
                ? "System follows the OS soon; using Dark for now."
                : "Persists across relaunch. Esc or click outside to close.";
        // Append the app version (single source of truth: src/version.h) so the
        // in-app About line and `--version` never drift.
        note += "   \xc2\xb7   hanabi ";
        note += hanabi::kVersion;
        div(ctx, mk(parent, 4),
            ComponentConfig{}
                .with_label(note)
                .with_size(ComponentSize{percent(1.0f), pixels(kFootnoteH)})
                .with_margin(Margin{.top = pixels(kFootnoteGap)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_footnote"));
    }

    static float pw_title() { return 300.0f; }

    void render_new_line_row(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 152, "New line with", "settings_new_line");
        const bool cmdReturn =
            Settings::get().get_send_key() == hanabi::kSendKeyCmdReturn;
        const std::string answer =
            cmdReturn ? "Return  \xc2\xb7  Cmd+Return sends"
                      : "Shift+Return  \xc2\xb7  Cmd+Return also sends";
        auto readout = div(ctx, mk(parent, 153),
            ComponentConfig{}
                .with_label(answer)
                .with_size(ComponentSize{
                    pixels(content_w()),
                    pixels(hanabi::control::kMinHitTarget)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_new_line_value"));
        anchor_control(readout.ent());
    }

    void render_restore_tabs_row(UIContext<InputAction>& ctx, Entity& parent,
                                 AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 154, "Restore open tabs on restart",
                 "settings_restore_tabs");
        const bool on = Settings::get().get_restore_tabs();
        real_segmented(ctx, parent, 155, {"Off", "On"}, on ? 1 : 0,
                       "settings_restore_tabs",
                       [](int i) { Settings::get().set_restore_tabs(i == 1); });
    }

    void render_jump_latest_row(UIContext<InputAction>& ctx, Entity& parent,
                                AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 156, "Jump to the latest message when you return",
                 "settings_jump_latest");
        const bool on = Settings::get().get_jump_to_latest();
        real_segmented(ctx, parent, 157, {"Stay put", "Jump"}, on ? 1 : 0,
                       "settings_jump_latest",
                       [](int i) { Settings::get().set_jump_to_latest(i == 1); });
    }

    void render_minimap_row(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 158, "Chat minimap", "settings_minimap");
        const bool on = Settings::get().get_show_minimap();
        real_segmented(ctx, parent, 159, {"Hidden", "Shown"}, on ? 1 : 0,
                       "settings_minimap",
                       [](int i) { Settings::get().set_show_minimap(i == 1); });
    }

    void render_notify_show_row(UIContext<InputAction>& ctx, Entity& parent,
                                AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 134, "Show notifications",
                 "settings_notify_show");
        const bool on = Settings::get().get_notifications_enabled();
        real_segmented(ctx, parent, 135, {"Off", "On"}, on ? 1 : 0,
                       "settings_notify_show", [](int i) {
                           Settings::get().set_notifications_enabled(i == 1);
                       });
    }

    void render_run_chime_row(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 174, "Chime when a run finishes",
                 "settings_run_chime");
        const bool on = Settings::get().get_run_chime();
        real_segmented(ctx, parent, 175, {"Off", "On"}, on ? 1 : 0,
                       "settings_run_chime",
                       [](int i) { Settings::get().set_run_chime(i == 1); });
    }

    void render_notify_subagents_row(UIContext<InputAction>& ctx,
                                     Entity& parent, AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 176, "Notify me about sub-agents",
                 "settings_notify_subagents");
        const bool on = Settings::get().get_notify_subagents();
        real_segmented(ctx, parent, 177, {"Off", "On"}, on ? 1 : 0,
                       "settings_notify_subagents", [](int i) {
                           Settings::get().set_notify_subagents(i == 1);
                       });
    }

    void render_usage_data_row(UIContext<InputAction>& ctx, Entity& parent,
                               AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 178, "Send settings to your account",
                 "settings_usage_data");
        const bool on = Settings::get().get_send_usage_data();
        real_segmented(ctx, parent, 179, {"Off", "On"}, on ? 1 : 0,
                       "settings_usage_data", [](int i) {
                           Settings::get().set_send_usage_data(i == 1);
                       });
    }

    // The named palettes, each drawing its own colour beside its name. A row
    // of words all in the same ink is a row a reader has to try one at a time;
    // the point of naming a palette is that it can be recognised.
    void render_palette_row(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app) {
        const std::string current = current_palette(app);
        row_name_with(ctx, parent, 190, "Palette", "   \xc2\xb7   " + current,
                      "settings_palette");
        auto row = div(ctx, mk(parent, 191),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()),
                                         pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_palette_row"));
        anchor_control(row.ent());

        constexpr float kGap = 6.0f;
        const int n = static_cast<int>(kPaletteCount);
        const float segW = (content_w() - kGap * (n - 1)) / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const auto& entry = kPalettes[static_cast<size_t>(i)];
            const bool sel = current == entry.label;
            const theme::Color preview =
                entry.light ? theme::Color{236, 236, 240, 255}
                            : theme::Color{32, 32, 44, 255};
            auto btn = button(ctx, mk(row.ent(), i + 1),
                ComponentConfig{}
                    .with_label(entry.label)
                    .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                    .with_margin(Margin{.right =
                                            pixels(i == n - 1 ? 0.0f : kGap)})
                    .with_custom_background(sel ? theme::button_primary()
                                                : preview)
                    .with_custom_hover_bg(sel ? theme::button_primary()
                                              : theme::hover_over(preview))
                    .with_custom_text_color(sel ? theme::window_bg()
                                                : (entry.light
                                                       ? theme::Color{28, 28,
                                                                      34, 255}
                                                       : theme::Color{
                                                             226, 226, 232,
                                                             255}))
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name(std::string("settings_palette_") +
                                     entry.key));
            if (btn) apply_theme(app, entry.key);
        }
        int cur = 0;
        for (int i = 0; i < n; ++i)
            if (current == kPalettes[static_cast<size_t>(i)].label) cur = i;
        if (const int stepped = activated_index(cur, n); stepped >= 0)
            apply_theme(app, kPalettes[static_cast<size_t>(stepped)].key);
    }

    struct PaletteEntry {
        const char* key;
        const char* label;
        bool light;
    };
    static constexpr size_t kPaletteCount = 3;
    static constexpr std::array<PaletteEntry, kPaletteCount> kPalettes{{
        {"light", "Light", true},
        {"dark", "Dark", false},
        {"system", "System", false},
    }};

    static std::string current_palette(const AppComponent& app) {
        for (const auto& entry : kPalettes)
            if (app.themeChoice == entry.key) return entry.label;
        return theme::mode() == theme::Mode::Light ? "Light" : "Dark";
    }

    void render_user_font_row(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app) {
        (void)app;
        render_side_font_row(ctx, parent, 192, "Your messages",
                             "settings_user_font",
                             Settings::get().get_user_font(),
                             [](const std::string& k) {
                                 Settings::get().set_user_font(k);
                             });
    }

    void render_assistant_font_row(UIContext<InputAction>& ctx, Entity& parent,
                                   AppComponent& app) {
        (void)app;
        render_side_font_row(ctx, parent, 194, "Replies",
                             "settings_assistant_font",
                             Settings::get().get_assistant_font(),
                             [](const std::string& k) {
                                 Settings::get().set_assistant_font(k);
                             });
    }

    // One side's message typeface. "App font" is the absence of a choice, not
    // a family of its own, so a reader who never touches this keeps whatever
    // the app font becomes.
    template <typename Apply>
    void render_side_font_row(UIContext<InputAction>& ctx, Entity& parent,
                              int id, const std::string& title,
                              const std::string& dbg,
                              const std::string& current, Apply apply) {
        row_name(ctx, parent, id, title, dbg);
        const auto& families = hanabi::fonts::families();
        std::vector<std::string> labels{"App font"};
        std::vector<std::string> keys{std::string{}};
        for (const auto& choice : families) {
            labels.push_back(choice.label);
            keys.push_back(choice.key);
        }
        int sel = 0;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == current) sel = static_cast<int>(i);
        real_segmented(ctx, parent, id + 1, labels, sel, dbg,
                       [keys, apply](int i) {
                           apply(keys[static_cast<size_t>(i)]);
                       });
    }

    // Which mark kinds the rail draws. Each is its own switch because hiding
    // "everything except asks" is the request; a single on/off is the setting
    // one row above this one.
    void render_minimap_marks_row(UIContext<InputAction>& ctx, Entity& parent,
                                  AppComponent& app) {
        (void)app;
        const std::string hidden = Settings::get().get_minimap_hidden_marks();
        const bool railOn = Settings::get().get_show_minimap();
        int shown = 0;
        for (const auto mark : hanabi::minimap::kAllMarks)
            if (!hanabi::minimap::mark_hidden(hidden, mark)) ++shown;
        row_name_with(ctx, parent, 196, "Marks on the minimap",
                      railOn ? "   \xc2\xb7   " + std::to_string(shown) +
                                   " of " +
                                   std::to_string(
                                       hanabi::minimap::kAllMarks.size())
                             : std::string("   \xc2\xb7   the minimap is off"),
                      "settings_minimap_marks");
        auto row = div(ctx, mk(parent, 197),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(content_w()),
                                         pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_minimap_marks_row"));
        anchor_control(row.ent());

        constexpr float kGap = 6.0f;
        const int n = static_cast<int>(hanabi::minimap::kAllMarks.size());
        const float segW = (content_w() - kGap * (n - 1)) / static_cast<float>(n);
        static const char* kLabels[5] = {"Tools", "Replies", "Asks", "Events",
                                         "Quiet"};
        for (int i = 0; i < n; ++i) {
            const auto mark = hanabi::minimap::kAllMarks[static_cast<size_t>(i)];
            const bool on = !hanabi::minimap::mark_hidden(hidden, mark);
            auto btn = button(ctx, mk(row.ent(), i + 1),
                ComponentConfig{}
                    .with_label(kLabels[i])
                    .with_size(ComponentSize{pixels(segW), pixels(kSegBtnH)})
                    .with_margin(Margin{.right =
                                            pixels(i == n - 1 ? 0.0f : kGap)})
                    .with_custom_background(on && railOn
                                                ? theme::button_primary()
                                                : theme::button_secondary())
                    .with_custom_hover_bg(on && railOn
                                              ? theme::button_primary()
                                              : theme::hover_bg())
                    .with_custom_text_color(on && railOn ? theme::window_bg()
                                            : railOn ? theme::text_primary()
                                                     : theme::text_faint())
                    .with_font_size(theme::type::MD)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name(std::string("settings_mark_") +
                                     std::string(hanabi::minimap::mark_key(
                                         mark))));
            hanabi::a11y::set_name(btn.ent(),
                                   std::string(kLabels[i]) + " marks " +
                                       (on ? "shown" : "hidden"));
            if (btn && railOn)
                Settings::get().set_minimap_hidden_marks(
                    hanabi::minimap::toggle_mark(hidden, mark));
        }
        if (rowActivate_)
            Settings::get().set_minimap_hidden_marks(
                hanabi::minimap::toggle_mark(hidden,
                                             hanabi::minimap::kAllMarks[0]));
    }

    void render_endpoint_row(UIContext<InputAction>& ctx, Entity& parent,
                             AppComponent& app) {
        row_name(ctx, parent, 170, "Endpoint", "settings_endpoint");
        std::string line = app.backend_label.empty() ? "mock (offline sample "
                                                       "data)"
                                                     : app.backend_label;
        auto readout = div(ctx, mk(parent, 171),
            ComponentConfig{}
                .with_label(line)
                .with_size(ComponentSize{
                    pixels(content_w()),
                    pixels(hanabi::control::kMinHitTarget)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_endpoint_value"));
        anchor_control(readout.ent());
        div(ctx, mk(parent, 172),
            ComponentConfig{}
                .with_label("Set in your config file or environment; a change "
                            "takes effect at the next launch.")
                .with_size(ComponentSize{pixels(content_w()), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("settings_endpoint_note"));
    }

    // Widths resolved from this frame's actual panel and active column.
    float active_panel_w_ = kPanelW;
    float active_col_w_ = 0.0f;

    bool rowFocused_ = false;
    bool rowActivate_ = false;
    bool revealRow_ = false;
    std::string rowTitle_;
    cat::Origin rowOrigin_ = cat::Origin::Device;

    bool wasOpen_ = false;
    int searchFocusFrames_ = 0;
    afterhours::EntityID focusAnchor_ = 0;
    afterhours::EntityID searchFieldId_ = 0;
    afterhours::EntityID restoreId_ = 0;
    int restoreFrames_ = 0;
    std::string pendingActivate_;

    static constexpr float kOriginMarkW = 84.0f;
    static constexpr float kOriginGap = 10.0f;
    static constexpr int kRevealFrames = 30;
};

// The sidebar's host call. It reuses the sheet's own row builder, so a pane
// row looks and behaves the same in both places and there is one selection.
inline void render_settings_pane_list(UIContext<InputAction>& ctx,
                                      Entity& parent, AppComponent& app,
                                      float width, bool rail) {
    SettingsSystem host;
    if (app.settingsPane.empty())
        app.settingsPane =
            cat::pane_info(
                cat::pane_from_slug(Settings::get().get_settings_pane()))
                .slug;
    cat::Focus none;
    none.zone = cat::Zone::Search;
    cat::Focus live;
    live.zone = static_cast<cat::Zone>(app.settingsFocusZone);
    live.index = app.settingsFocusIndex;
    host.render_nav(ctx, parent, app, cat::pane_from_slug(app.settingsPane),
                    width, 0.0f, rail, live, /*hosted=*/true);
    hosted_focus_anchor() = host.focusAnchor_;
    (void)none;
}

}  // namespace ecs
