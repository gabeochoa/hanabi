#pragma once

// Settings overlay (Phase K). Renders a centered settings sheet over a dimmed
// full-window backdrop when AppComponent::showSettings is true. Closes on
// Cmd+, (toggle), Esc, the ✕ close button, or clicking the backdrop.
//
// LAYOUT — a WIDE (kPanelW) two-column sheet so the whole section set fits a
// normal window WITHOUT scrolling. Grouped the SAME way + order as the navi
// web settings (Appearance / Behavior / Notifications / Data / Model /
// Advanced / Account). Every content width derives from ONE panel-width
// constant (kPanelW → content_w()/col_w()) so segmented-control gutters can't
// drift. Left column: Appearance / Behavior / Notifications / Model. Right
// column: Data / Advanced / Account. The footnote spans full width beneath.
//
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
//
// The web PUT-preferences schema fields we map onto: defaultModelId, yapLevel,
// memoryBackend, notificationSound, autoArchiveDays (+ theme/font which stay
// CLIENT-LOCAL — the web schema has no theme/font field).
//
// WIRED (persist locally + sync):
//   Appearance · Theme  (Light/Dark/System)   — client-local (theme::set_mode).
//   Appearance · Rotate theme (Off/15m/30m/1h) — client-local; the interval
//     persists, the palette it lands on does not (theme_rotation_system.h).
//   Appearance · Font   (Default/Hyperlegible) — client-local (FontManager).
//   Custom colours · Accent + Find highlight (named swatches) — client-local;
//     layered over whichever palette is active (src/ui/theme.h).
//   Behavior · Yap level (No yapping/A little/Full).
//   Behavior · Auto-archive (Never/5/14/30 days).
//   Behavior · Memory backend (Traditional/Hindsight).
//   Notifications · Sound (Off/Ping).
//   Data · cache usage + clear / cache limit / export — client-local.
//   Account · identity + counts — read-only /whoami.
//
// Model and effort are selected from the composer's live picker, not duplicated
// here. Controls without a working action are omitted.
//
// "System" theme tracks the real macOS appearance via hanabi::os_is_dark_mode()
// (afterhours exposes no OS-appearance query — see afterhours_gaps.md #16).
//
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
#include "../util/text_epoch.h"
#include "theme_rotation_system.h"  // theme_rotation::restart (interval clock)
#include "ui_imports.h"

#include "../ui/icons.h"
#include "../ui/secondary_surface.h"

namespace ecs {

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

        if (!app->showSettings) return;

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

        // Esc closes (escape_system.h decides which overlay it belongs to).
        if (app->escape == EscapeIntent::CloseSettings) {
            app->showSettings = false;
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            hanabi::viewport::width();
        const float sh =
            hanabi::viewport::height();

        const float pw = kPanelW;
        const float ctrlRow = kRowNameFoot + kThemeRowH;
        const float leftH =
            (kGroupH + ctrlRow * 3.0f) +
            (kGroupH + ctrlRow * 6.0f) +
            (kGroupH + ctrlRow * 2.0f);
        const float rightH =
            (kGroupH + ctrlRow * 2.0f) +
            (kGroupH + ctrlRow * 4.0f) +
            (kGroupH + (kRowNameFoot + kCacheRowH) +
             (kRowNameFoot + kLimitRowH) +
             (kRowNameFoot + kExportRowH)) +
            (kGroupH + kAccountRowH);
        const float contentH =
            std::max(leftH, rightH) + (kFootnoteGap + kFootnoteH) + 8.0f;
        const float idealPh = kPadV * 2.0f + kHeaderH + contentH;
        const hanabi::surface::Rect panelRect =
            hanabi::surface::centered(sw, sh, pw, idealPh);
        const float ph = panelRect.height;
        const float bodyViewH =
            std::max(40.0f, ph - kPadV * 2.0f - kHeaderH);
        const float px = panelRect.x;
        const float py = panelRect.y;

        // Dimmed full-window backdrop. The UI fill pipeline has alpha blending
        // disabled (afterhours gap #13), so pre-blend a translucent black over
        // the window background via theme::over to get a real "dim" instead of
        // an opaque black slab.
        //
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
                ctx.mouse.pos, RectangleType{px, py, pw, ph});
            if (!insidePanel) {
                app->showSettings = false;
                return;
            }
        }

        auto panel = div(
            ctx, mk(uiRoot, 8010),
            hanabi::surface::sheet(panelRect, 11)
                .with_debug_name("settings_panel"));

        render_header(ctx, panel.ent(), *app);

        // Scrollable body: fixed visible height (bodyViewH), vertical overflow
        // scrolls. afterhours attaches a HasScrollView + clips children when a
        // child config sets Overflow::Scroll, and the UI plugin drives the
        // wheel. This lets the centered sheet stay a fixed size while holding
        // the full navi-web section set (which is taller than any sheet).
        auto body = div(ctx, mk(panel.ent(), 500),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(bodyViewH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_overflow(Overflow::Scroll, Axis::Y)
                .with_transparent_bg()
                // Top pad so the FIRST group label's ascenders aren't clipped
                // by the scroll viewport's top edge.
                .with_padding(Padding{.top = pixels(8.0f)})
                .with_roundness(0.0f)
                .with_debug_name("settings_body_scroll"));
        Entity& b = body.ent();

        // TWO-COLUMN layout (Task 1): the wider panel is split into two equal
        // columns so the full section set fits a normal window WITHOUT
        // scrolling. Left column = Appearance / Behavior / Notifications /
        // Model; right column = Data / Advanced / Account. Each control sizes
        // to its column via content_w() (set per column below). The footnote
        // spans full width beneath both columns.
        auto cols = div(ctx, mk(b, 490),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_cols"));

        auto leftCol = div(ctx, mk(cols.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(col_w()), children()})
                .with_margin(Margin{.right = pixels(kColGap)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_col_left"));
        auto rightCol = div(ctx, mk(cols.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(col_w()), children()})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_col_right"));

        // LEFT column: Appearance / Behavior / Notifications / Model.
        active_col_w_ = col_w();
        {
            Entity& L = leftCol.ent();
            group_label(ctx, L, 2, "Appearance", "settings_grp_appearance");
            render_theme_row(ctx, L, *app);
            render_theme_rotate_row(ctx, L, *app);
            render_font_row(ctx, L, *app);
            group_label(ctx, L, 3, "Behavior", "settings_grp_behavior");
            render_yap_row(ctx, L, *app);
            render_autoarchive_row(ctx, L, *app);
            render_memory_backend_row(ctx, L, *app);
            render_send_key_row(ctx, L, *app);
            render_shortcut_editor_row(ctx, L, *app);
            render_subagents_row(ctx, L, *app);

            group_label(ctx, L, 4, "Notifications", "settings_grp_notif");
            render_notification_row(ctx, L, *app);
            render_quiet_hours_row(ctx, L, *app);
        }
        // RIGHT column: Data / Advanced / Account.
        {
            Entity& R = rightCol.ent();
            group_label(ctx, R, 9, "Custom colours", "settings_grp_colours");
            render_accent_row(ctx, R, *app);
            render_highlight_row(ctx, R, *app);
            group_label(ctx, R, 10, "Transcript", "settings_grp_transcript");
            render_timestamps_row(ctx, R, *app);
            render_date_dividers_row(ctx, R, *app);
            render_reasoning_row(ctx, R, *app);
            render_foldlong_row(ctx, R, *app);
            group_label(ctx, R, 5, "Data", "settings_grp_data");
            render_cache_row(ctx, R, *app);
            render_cache_limit_row(ctx, R, *app);
            render_export_row(ctx, R, *app);
            group_label(ctx, R, 8, "Account", "settings_grp_account");
            render_account_row(ctx, R, *app);
        }
        active_col_w_ = 0.0f;  // reset to full-width for the spanning footnote

        render_footnote(ctx, b, *app);
    }

    // ---- layout constants (single source of truth for panel WIDTH + height +
    // the consistent vertical rhythm; Task B). A section = a small gap, a
    // header label, then its control. Between-section gap == kSectionGap;
    // label-> control gap is baked into the label's own bottom via kLabelH.
    //
    // PANEL WIDTH is ONE constant (kPanelW). Every content-width computation
    // (segmented-control gutters etc.) derives from content_w() so they can't
    // drift. Widened from 360 → 600 so the whole section set fits WITHOUT
    // vertical scrolling on a normal window (Task 1).
    static constexpr float kPanelW = 720.0f;    // panel width (single source)
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
    static constexpr float full_content_w() {
        return kPanelW - kPadH * 2.0f - kScrollbarW;
    }
    // Column geometry: two equal columns split from full_content_w() with a
    // gutter between them.
    static constexpr float kColGap = 24.0f;
    static constexpr float col_w() {
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
    static constexpr float kRowNameH = 15.0f;    // compact per-row name label
    static constexpr float kRowNameGap = 2.0f;   // gap under a row name
    // One row name's total vertical footprint.
    static constexpr float kRowNameFoot = kRowNameH + kRowNameGap;
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

  private:
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
                .with_size(ComponentSize{percent(1.0f), pixels(kTitleH)})
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
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_title"));
        auto closeBtn = button(ctx, mk(titleRow.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(26), pixels(26)})
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
                .with_label("Appearance, behavior, notifications, and local data")
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
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name(dbg));
    }

    // Compact inline row name above a control (e.g. "Yap level", "Sound").
    // Small + secondary so the group header stays the visual anchor; kRowNameGap
    // under it separates it from the control.
    void row_name(UIContext<InputAction>& ctx, Entity& parent, int id,
                  const std::string& text, const std::string& dbg) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(kRowNameH)})
                .with_margin(Margin{.bottom = pixels(kRowNameGap)})
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_name"));
    }

    void render_theme_row(UIContext<InputAction>& ctx, Entity& parent,
                          AppComponent& app) {
        // Appearance group -> Theme. NOTE: client-local only (not an API
        // setting) — the web PUT /api/user/preferences has no theme field.
        row_name(ctx, parent, 2, "Theme", "settings_theme_label");

        auto row = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_theme_row"));

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
    }

    // Appearance group -> Rotate theme. Off / 15m / 30m / 1h, sharing the theme
    // row's gutter math. Rotation is what the picker above could not do: leave
    // the sheet and the palette keeps moving on its own.
    //
    // The row NAME carries which palette is up right now. Rotation moves the
    // theme while the sheet is open, and the picker above says which one only
    // in colour — which is not something a person glancing at the sheet can
    // read off, nor anything a test can assert.
    void render_theme_rotate_row(UIContext<InputAction>& ctx, Entity& parent,
                                 AppComponent& app) {
        (void)app;
        const int secs = Settings::get().get_theme_rotate_secs();
        std::string name = "Rotate theme";
        if (secs > 0) {
            name += "   \xc2\xb7   showing ";
            name += (theme::mode() == theme::Mode::Light) ? "Light" : "Dark";
        }
        row_name(ctx, parent, 150, name, "settings_theme_rotate_label");

        auto row = div(ctx, mk(parent, 151),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_theme_rotate_row"));

        constexpr float kSegGap = 6.0f;
        const float segW = (content_w() - kSegGap * 3.0f) / 4.0f;
        rotate_choice(ctx, row.ent(), 1, "Off", "off", 0, segW, true);
        rotate_choice(ctx, row.ent(), 2, "15m", "15m", 15 * 60, segW, true);
        rotate_choice(ctx, row.ent(), 3, "30m", "30m", 30 * 60, segW, true);
        rotate_choice(ctx, row.ent(), 4, "1h", "1h", 60 * 60, segW, false);
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
                .with_font_size(theme::type::SM)
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

    // Appearance group -> Font. A 2-way segmented control mirroring the theme
    // control's edge-hugging gutter math. Selecting a choice swaps the active
    // UI font live via FontManager.load_font(DEFAULT_FONT, path) and persists
    // it through Settings::set_font_choice (auto-saves). WIRED + functional.
    // NOTE: client-local only (not an API setting) — the web PUT
    // /api/user/preferences has no font field; font is a pure client choice.
    void render_font_row(UIContext<InputAction>& ctx, Entity& parent,
                         AppComponent& app) {
        (void)app;
        // Clarify this is the APP's UI typeface (a hanabi-local preference — the
        // web schema has no font field), and label the options by what they
        // actually are: the standard UI font vs Atkinson Hyperlegible (an
        // accessibility face). Gabe: "font setting doesn't make sense" — the
        // bare "Default / Hyperlegible" pair read as arbitrary.
        row_name(ctx, parent, 6, "App font", "settings_font_label");

        auto row = div(ctx, mk(parent, 7),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_font_row"));

        // Same gutter math as render_theme_row: content_w(), minus one
        // inter-segment gap, split 2 ways.
        constexpr float kSegGap = 6.0f;
        const float content = content_w();
        const float segW = (content - kSegGap) / 2.0f;
        font_choice_btn(ctx, row.ent(), 1, "Standard", "default", segW, true);
        font_choice_btn(ctx, row.ent(), 2, "Hyperlegible", "hyperlegible", segW,
                        false);
    }

    // One segmented font button. Selected = accent fill; others = secondary.
    // On click: swap DEFAULT_FONT live via FontManager and persist the choice.
    void font_choice_btn(UIContext<InputAction>& ctx, Entity& parent, int id,
                         const std::string& label, const std::string& value,
                         float segW, bool trailingGap) {
        const bool selected = (Settings::get().get_font_choice() == value);
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
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.35f)
                .with_debug_name("settings_font_" + value));
        if (btn) apply_font(value);
    }

    // Apply a font choice: persist it and swap DEFAULT_FONT live so the whole
    // UI re-renders in the new face this frame. Roboto = "default" (the font
    // loaded at DEFAULT_FONT in preload); "hyperlegible" = Atkinson
    // Hyperlegible loaded under the "hyperlegible" name in preload.cpp.
    static void apply_font(const std::string& value) {
        auto& s = Settings::get();
        if (s.get_font_choice() == value) return;  // no-op
        s.set_font_choice(value);  // auto-persists
        // Defer the actual FontManager.load_font to frame-top (see
        // apply_pending_font) so we never swap the DEFAULT_FONT handle while
        // this frame is still rendering text with it.
        pending_font() = value;
    }

    // The pending font choice to apply at frame-top (empty = nothing pending).
    static std::string& pending_font() {
        static std::string p;
        return p;
    }

    // Apply a deferred font swap (called at frame-top, before any text render).
    static void apply_pending_font() {
        std::string& p = pending_font();
        if (p.empty()) return;
        const std::string value = p;
        p.clear();
        auto& fontMgr =
            afterhours::EntityHelper::get_singleton_cmp_enforce<
                afterhours::ui::FontManager>();
        const char* file = (value == "hyperlegible")
                               ? "AtkinsonHyperlegible-Regular.ttf"
                               : "Roboto-Regular.ttf";
        const std::string path =
            afterhours::files::get_resource_path("fonts", file).string();
        fontMgr.load_font(afterhours::ui::UIComponent::DEFAULT_FONT,
                          path.c_str());

        // The glyphs behind DEFAULT_FONT just changed and NOTHING ELSE DID:
        // the font's name, its handle and every size in the app are the same
        // as they were, so every cache keyed by (text, font name, size) is
        // now holding measurements of a face that is not on screen and has no
        // reason to suspect it. Two of them are hanabi's and watch this
        // counter (src/util/text_epoch.h); the third is afterhours' own
        // TextMeasureCache, which has no invalidation hook finer than clear()
        // -- filed as afterhours_gaps.md #190.
        hanabi::text::bump_font_epoch();
        if (auto* tmc = afterhours::EntityHelper::get_singleton_cmp<
                afterhours::ui::TextMeasureCache>())
            tmc->clear();
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
                .with_size(ComponentSize{percent(1.0f), pixels(kCacheRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_cache_row"));

        const std::uint64_t bytes = api::disk_cache::total_bytes();
        std::string usage = human_bytes(bytes) + " on disk";
        if (app.cacheWipeReported)
            usage += " · " + human_bytes(app.cacheWipeReclaimedBytes) +
                     " reclaimed";
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(usage)
                .with_size(ComponentSize{children(), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_cache_usage"));

        auto clear = button(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label("Clear cache")
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
        if (clear) {
            app.clear_transcript_cache();
            const auto result = api::disk_cache::wipe_all_report();
            app.cacheWipeReclaimedBytes = result.bytes_reclaimed;
            app.cacheWipeReported = true;
        }
    }

    // Data / export row (local-first idea #4): a "Data" section with the export
    // destination on the left and, hugging the right edge, the two things you
    // can do with it — choose where it goes, and send it there.
    //
    // Writes every cached transcript to <destination>/*.md — user-owned,
    // survives a backend sunset. A transient "· exported N" note confirms.
    //
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
                .with_size(ComponentSize{percent(1.0f), pixels(kExportRowH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_export_row"));

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
                .with_custom_text_color(theme::text_faint())
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
        if (exp) s_exported = api::disk_cache::export_all_markdown(dest);
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
                .with_size(ComponentSize{percent(1.0f), pixels(kLimitRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_cache_limit_row"));

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
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name(std::string("settings_cache_limit_") +
                                     std::to_string(i)));
            if (btn) {
                auto& s = Settings::get();
                s.set_cache_cap_bytes(opt.bytes);  // auto-persists
                // Apply immediately: trim the on-disk cache to the new cap so
                // the usage line above reflects the change on the next frame.
                // (Ongoing "trim after each save" belongs in the loader — see
                // REPORT; disk_cache::trim_to_cap is exposed for that wiring.)
                api::disk_cache::trim_to_cap(opt.bytes);
            }
        }
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
        auto row = div(ctx, mk(parent, baseId),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_row"));
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
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.35f)
                    .with_debug_name(dbg + "_" + std::to_string(i)));
            if (btn) onPick(i);
        }
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
        row_name(ctx, parent, 130, "Sound", "settings_notif_label");
        const bool on = Settings::get().get_notification_sound();
        real_segmented(ctx, parent, 131, {"Off", "Ping"}, on ? 1 : 0,
                       "settings_notif",
                       [](int i) {
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
        std::string line;
        theme::Color col = theme::text_faint();
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
        div(ctx, mk(parent, 31),
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
                .with_font_size(FontSize::Medium)
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
    //
    // The row name carries the chosen swatch, because the swatch row itself
    // says which one is live only in colour — unreadable at a glance next to a
    // full sheet of blue segments, and invisible to a test.
    void render_accent_row(UIContext<InputAction>& ctx, Entity& parent,
                           AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 160,
                 "Accent   \xc2\xb7   " + swatch_label(theme::accent_choice(),
                                                      theme::kAccentSwatches,
                                                      std::size(theme::kAccentSwatches)),
                 "settings_accent_label");
        auto row = swatch_row(ctx, parent, 161, "settings_accent_row");
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
    }

    void render_highlight_row(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app) {
        (void)app;
        row_name(ctx, parent, 162,
                 "Find highlight   \xc2\xb7   " +
                     swatch_label(theme::highlight_choice(),
                                  theme::kHighlightSwatches,
                                  std::size(theme::kHighlightSwatches)),
                 "settings_highlight_label");
        auto row = swatch_row(ctx, parent, 163, "settings_highlight_row");
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
                .with_size(ComponentSize{percent(1.0f), pixels(kThemeRowH)})
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
                .with_font_size(theme::type::SM)
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

    // The width controls should size to this frame — set to col_w() per column
    // by the render loop. 0 => fall back to the full panel content width.
    float active_col_w_ = 0.0f;
};

}  // namespace ecs
