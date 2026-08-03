#pragma once

// Settings overlay (Phase K). Renders a centered settings sheet over a dimmed
// full-window backdrop when AppComponent::showSettings is true. Closes on
// Cmd+, (toggle), Esc, the ✕ close button, or clicking the backdrop.
//
// STRUCTURE — mirrors the navi web settings menu, grouped the SAME way and in
// the SAME order (Appearance / Behavior / Notifications / Data / Model /
// Advanced / Account). Each row is either wired here or rendered as a visible
// TODO stub (label + disabled-looking control) so the full structure is
// legible even before the backing API/UI lands.
//
// ─── API SETTABILITY (the web app's PUT /api/user/preferences schema) ───────
// The web backend can persist ONLY these preference fields:
//   defaultModelId, yapLevel, branchOverrideUrl, memoryBackend,
//   notificationSound, compactionThreshold, enabledExperiments,
//   keyboardShortcuts, autoArchiveDays.
// It CANNOT set: theme, font  — those are CLIENT-LOCAL (persisted here in
// Settings, never sent to the backend). Rows that would need the API but
// aren't wired to it yet carry `// TODO(settings-api):` pointing at the exact
// field; genuinely client-only rows carry `// NOTE: client-local only`.
// hanabi has no PUT-preferences client yet, so every API-backed row below is a
// visible stub until that wiring exists (see REPORT).
//
// Appearance:
//   Theme  (Light/Dark/System)  — WIRED, client-local (theme::set_mode).
//   Font   (Default/Hyperlegible) — WIRED, client-local (FontManager swap).
// Behavior:
//   Yap level (0/1/2)           — stub, API field yapLevel.
//   Auto-archive (days)         — stub, API field autoArchiveDays.
//   Memory backend (Trad/Hind)  — stub, API field memoryBackend (Hindsight
//                                  admin-only in the web app).
// Notifications:
//   Notification sound (Off/Ping) — stub, API field notificationSound.
// Data:  cache usage + clear / cache limit / export — WIRED, all client-local.
// Model: Default model          — stub, API field defaultModelId (needs a
//                                  model list from the API too).
// Advanced: branch override URL / enabled experiments / compaction threshold /
//   keyboard shortcuts / reset onboarding+sandbox — all stubs (API fields
//   branchOverrideUrl / enabledExperiments / compactionThreshold /
//   keyboardShortcuts; reset needs API support).
// Account: identity + counts — WIRED (read-only /whoami). Sign out — stub.
//
// "System" theme tracks the real macOS appearance via hanabi::os_is_dark_mode()
// (afterhours exposes no OS-appearance query — see afterhours_gaps.md #16).
//
// Owns this file only. The gear button that would toggle showSettings lives in
// sidebar_system.h (owned by another agent); Cmd+, opens/closes this overlay.

#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

#include <afterhours/src/plugins/files.h>

#include "../api/disk_cache.h"
#include "../settings.h"
#include "../version.h"
#include "../native_extras.h"  // hanabi::os_is_dark_mode (System theme)
#include "ui_imports.h"

#include "../ui/icons.h"

namespace ecs {

struct SettingsSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Cmd+, toggles the settings overlay (mirrors sidebar's Cmd+B pattern:
        // 343/347 = left/right super, 44 = KEY_COMMA).
        bool cmdDown = afterhours::graphics::is_key_down(343) ||
                       afterhours::graphics::is_key_down(347);
        if (cmdDown && afterhours::graphics::is_key_pressed(44)) {
            app->showSettings = !app->showSettings;
        }

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

        // Esc closes.
        if (afterhours::graphics::is_key_pressed(256)) {  // KEY_ESCAPE
            app->showSettings = false;
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            static_cast<float>(afterhours::graphics::get_screen_width());
        const float sh =
            static_cast<float>(afterhours::graphics::get_screen_height());

        // Panel geometry, computed BEFORE the backdrop so the backdrop click
        // handler can hit-test the cursor against the panel rect (see below).
        // Height is derived from the content stack so there's no dead space at
        // the bottom (Task B) — pad(top+bottom) + header + each section
        // (gap + label + control) + footnote (gap + line).
        const float pw = 360.0f;
        // contentH = the full stacked height of every section body (everything
        // BELOW the fixed header). This mirrors navi web's settings, which has
        // far more rows than fit in a fixed sheet — so the body scrolls.
        // Update this sum whenever a section is added/removed.
        const float contentH =
                         (kSectionH + kThemeRowH) +      // Appearance: Theme
                         (kSectionH + kThemeRowH) +      // Appearance: Font
                         (kSectionH + kThemeRowH) +      // Behavior: Yap level
                         (kSectionH + kCacheRowH) +      // Behavior: Auto-archive
                         (kSectionH + kCacheRowH) +      // Behavior: Memory backend
                         (kSectionH + kCacheRowH) +      // Notifications: sound
                         (kSectionH + kCacheRowH) +      // Data: cache (usage+clear)
                         (kSectionH + kLimitRowH) +      // Data: cache limit
                         (kSectionH + kCacheRowH) +      // Data: export
                         (kSectionH + kCacheRowH) +      // Model: default model
                         (kSectionH + kAdvancedH) +      // Advanced: stubs + note
                         (kSectionH + kAccountRowH) +    // Account: identity+signout
                         (kFootnoteGap + kFootnoteH);    // footnote
        // Ideal (unclipped) panel height = pad + header + all content.
        const float idealPh = kPadV * 2.0f + kHeaderH + contentH;
        // But the panel is a fixed centered sheet — cap it so it always fits
        // the window (leave a margin top+bottom); the body div scrolls when the
        // content is taller than the visible area.
        const float kWinMargin = 48.0f;  // min gap to window edges
        const float ph = std::min(idealPh, sh - kWinMargin);
        // Visible height available for the scrollable body (panel minus pad +
        // header). The body's CONTENT is contentH; when contentH > bodyViewH
        // the overflow scrolls.
        const float bodyViewH = ph - kPadV * 2.0f - kHeaderH;
        const float px = (sw - pw) * 0.5f;
        const float py = (sh - ph) * 0.5f;

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
        auto backdrop = button(ctx, mk(uiRoot, 8000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(sw), pixels(sh)})
                .with_absolute_position()
                .with_translate(0.0f, 0.0f)
                .with_custom_background(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_custom_hover_bg(
                    theme::over(theme::scrim(), theme::window_bg()))
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_render_layer(10)
                .with_debug_name("settings_backdrop"));
        if (backdrop) {
            const bool insidePanel = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos, RectangleType{px, py, pw, ph});
            if (!insidePanel) {
                app->showSettings = false;
                return;
            }
        }

        auto panel = div(ctx, mk(uiRoot, 8010),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(pw), pixels(ph)})
                .with_absolute_position()
                .with_translate(px, py)
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(kPadV), .right = pixels(20),
                                      .bottom = pixels(kPadV),
                                      .left = pixels(20)})
                .with_roundness(0.35f)
                .with_render_layer(11)
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
                // Small top pad so the first section label's text isn't clipped
                // by the scroll viewport's top edge (its own top margin sits at
                // the clip origin otherwise).
                .with_padding(Padding{.top = pixels(2.0f)})
                .with_roundness(0.0f)
                .with_debug_name("settings_body_scroll"));
        Entity& b = body.ent();

        // Appearance
        render_theme_row(ctx, b, *app);
        render_font_row(ctx, b, *app);
        // Behavior
        render_yap_row(ctx, b, *app);
        render_autoarchive_row(ctx, b, *app);
        render_memory_backend_row(ctx, b, *app);
        // Notifications
        render_notification_row(ctx, b, *app);
        // Data
        render_cache_row(ctx, b, *app);
        render_cache_limit_row(ctx, b, *app);
        render_export_row(ctx, b, *app);
        // Model
        render_model_row(ctx, b, *app);
        // Advanced
        render_advanced_section(ctx, b, *app);
        // Account
        render_account_row(ctx, b, *app);
        render_footnote(ctx, b, *app);
    }

    // ---- layout constants (single source of truth for panel height + the
    // consistent vertical rhythm; Task B). A section = a small gap, a header
    // label, then its control. Between-section gap == kSectionGap; label->
    // control gap is baked into the label's own bottom via kLabelH sizing.
    static constexpr float kPadV = 20.0f;        // panel top/bottom padding
    static constexpr float kHeaderH = 28.0f;     // title + close row
    static constexpr float kSectionGap = 20.0f;  // space above each section label
    static constexpr float kLabelH = 22.0f;      // section header label height
    static constexpr float kLabelPadB = 6.0f;    // gap under a section label
    // Total vertical footprint of one section header (gap + label + gap-below).
    static constexpr float kSectionH = kSectionGap + kLabelH + kLabelPadB;
    static constexpr float kThemeRowH = 34.0f;   // segmented control
    static constexpr float kCacheRowH = 30.0f;   // usage + clear button
    static constexpr float kLimitRowH = 34.0f;   // cache-limit segmented control
    static constexpr float kAccountRowH = 42.0f; // identity/counts + sign-out
    // Advanced section body: 5 stacked stub rows (~20 each) + a note line.
    static constexpr float kAdvancedH = 20.0f * 5.0f + 24.0f;
    static constexpr float kFootnoteGap = 20.0f; // space above footnote
    static constexpr float kFootnoteH = 18.0f;   // footnote line

  private:
    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_header"));

        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label("Settings")
                .with_size(ComponentSize{pixels(pw_title()), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_title"));

        // Close (✕).
        auto closeBtn = button(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(26), pixels(26)})
                .with_custom_background(theme::panel_bg())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.3f)
                // Lucide "close" sprite (atlas); unicode \xc3\x97 is the fallback.
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "close", "\xc3\x97", theme::text_secondary(), 14.0f))
                .with_debug_name("settings_close"));
        if (closeBtn) app.showSettings = false;
    }

    // Consistent section header: a slightly-prominent label with a fixed gap
    // above it (kSectionGap) and a fixed small gap to its control below
    // (kLabelPadB). Unifies Theme/Cache/Account so they read as peers (Task B).
    // Uses MARGIN (not padding) for the gaps: the autolayout stacks children by
    // computed size + margin, so margin adds real space BETWEEN elements, while
    // padding would only inset text inside the label's own fixed box (leaving
    // the sections cramped + the panel with dead space — the bug this fixes).
    void section_label(UIContext<InputAction>& ctx, Entity& parent, int id,
                       const std::string& text, const std::string& dbg) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{percent(1.0f), pixels(kLabelH)})
                .with_margin(Margin{.top = pixels(kSectionGap),
                                    .bottom = pixels(kLabelPadB)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name(dbg));
    }

    void render_theme_row(UIContext<InputAction>& ctx, Entity& parent,
                          AppComponent& app) {
        // Appearance group -> Theme. NOTE: client-local only (not an API
        // setting) — the web PUT /api/user/preferences has no theme field.
        section_label(ctx, parent, 2, "Appearance \xc2\xb7 Theme",
                      "settings_theme_label");

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
        // real content width: panel 360 − left/right pad 20 each = 320, minus
        // the inter-segment gaps, split N ways. No trailing margin so the group
        // spans exactly left-inset → right-inset.
        constexpr float kSegGap = 6.0f;
        const float content = 360.0f - 20.0f - 20.0f;
        const float segW = (content - kSegGap * 2.0f) / 3.0f;
        theme_choice(ctx, row.ent(), 1, "Light", "light", app, segW, true);
        theme_choice(ctx, row.ent(), 2, "Dark", "dark", app, segW, true);
        theme_choice(ctx, row.ent(), 3, "System", "system", app, segW, false);
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
        section_label(ctx, parent, 6, "Appearance \xc2\xb7 Font",
                      "settings_font_label");

        auto row = div(ctx, mk(parent, 7),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_font_row"));

        // Same gutter math as render_theme_row: content = 360 - 20 - 20 = 320,
        // minus one inter-segment gap, split 2 ways.
        constexpr float kSegGap = 6.0f;
        const float content = 360.0f - 20.0f - 20.0f;
        const float segW = (content - kSegGap) / 2.0f;
        font_choice_btn(ctx, row.ent(), 1, "Default", "default", segW, true);
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
                .with_size(ComponentSize{pixels(segW), pixels(32)})
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
        section_label(ctx, parent, 20, "Cache", "settings_cache_label");

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

        // Current on-disk usage (cheap stat walk of the active namespace).
        const std::string usage = human_bytes(api::disk_cache::total_bytes());
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(usage + " on disk")
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
            api::disk_cache::wipe_all();
        }
    }

    // Data / export row (local-first idea #4): a "Data" section with the export
    // destination on the left and an "Export all" button hugging the right edge
    // (V8). Writes every cached transcript to ~/hanabi/threads/*.md — user-owned,
    // survives a backend sunset. A transient "· exported N" note confirms.
    void render_export_row(UIContext<InputAction>& ctx, Entity& parent,
                           AppComponent& app) {
        (void)app;
        section_label(ctx, parent, 60, "Data", "settings_data_label");
        auto row = div(ctx, mk(parent, 61),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kCacheRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_export_row"));

        static int s_exported = -1;  // -1 = not yet; >=0 = last export count
        std::string left = "Export threads to ~/hanabi/threads";
        if (s_exported >= 0)
            left = "Exported " + std::to_string(s_exported) + " threads";
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(left)
                .with_size(ComponentSize{children(), pixels(20)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_export_usage"));

        auto exp = button(ctx, mk(row.ent(), 2),
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
        if (exp) {
            s_exported = api::disk_cache::export_all_markdown();
        }
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
        section_label(ctx, parent, 40, "Cache limit",
                      "settings_cache_limit_label");

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
        // content width (320) minus 3 inter-segment gaps; no trailing margin so
        // the group spans exactly left-inset → right-inset.
        constexpr float kSegGap = 4.0f;
        const float segW = (320.0f - kSegGap * 3.0f) / 4.0f;
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

    // ── Shared stub helpers ────────────────────────────────────────────────
    // A single disabled-looking segmented control that shows structure but does
    // nothing yet. `selectedIdx` highlights the current value so the row reads
    // as a real (but inert) choice. Used by the API-blocked Behavior /
    // Notifications rows until a PUT-preferences client exists.
    void stub_segmented(UIContext<InputAction>& ctx, Entity& parent,
                        int baseId, const std::vector<std::string>& labels,
                        int selectedIdx, const std::string& dbg) {
        auto row = div(ctx, mk(parent, baseId),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(30)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_row"));
        constexpr float kSegGap = 6.0f;
        const int n = static_cast<int>(labels.size());
        const float content = 360.0f - 20.0f - 20.0f;
        const float segW =
            (content - kSegGap * (n - 1)) / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const bool sel = (i == selectedIdx);
            const bool last = (i == n - 1);
            // Rendered as a div (not a button): visibly present, but inert —
            // the control is a stub until the API is wired.
            div(ctx, mk(row.ent(), i + 1),
                ComponentConfig{}
                    .with_label(labels[static_cast<size_t>(i)])
                    .with_size(ComponentSize{pixels(segW), pixels(28)})
                    .with_margin(Margin{.right = pixels(last ? 0.0f : kSegGap)})
                    .with_custom_background(sel ? theme::button_secondary()
                                                : theme::panel_bg())
                    .with_border(theme::border(), pixels(1.0f))
                    .with_custom_text_color(sel ? theme::text_secondary()
                                                : theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_roundness(0.35f)
                    .with_debug_name(dbg + "_" + std::to_string(i)));
        }
    }

    // A one-line stub row: a small label on the left, a faint value/"coming
    // soon" hint on the right. For Advanced text rows + Model default.
    void stub_line(UIContext<InputAction>& ctx, Entity& parent, int id,
                   const std::string& label, const std::string& hint,
                   const std::string& dbg) {
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_row"));
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{children(), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_label"));
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(hint)
                .with_size(ComponentSize{children(), pixels(18)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name(dbg + "_hint"));
    }

    // ── Behavior group ─────────────────────────────────────────────────────
    // Yap level / verbosity (0 = No yapping, 1 = A little, 2 = Full). Mirrors
    // the web yap-level segmented control.
    // TODO(settings-api): needs PUT /api/user/preferences.yapLevel — not wired
    // yet (hanabi has no preferences-write client). Rendered as an inert stub.
    void render_yap_row(UIContext<InputAction>& ctx, Entity& parent,
                        AppComponent& app) {
        (void)app;
        section_label(ctx, parent, 100, "Behavior \xc2\xb7 Yap level",
                      "settings_yap_label");
        // Default highlight = "Full" (web default is 2) until we can read the
        // real value from the API.
        stub_segmented(ctx, parent, 101,
                       {"No yapping", "A little", "Full"}, 2, "settings_yap");
    }

    // Auto-archive after N days. Numeric in the web app; shown here as a
    // read-only value stub.
    // TODO(settings-api): needs PUT /api/user/preferences.autoArchiveDays —
    // not wired yet.
    void render_autoarchive_row(UIContext<InputAction>& ctx, Entity& parent,
                                AppComponent& app) {
        (void)app;
        section_label(ctx, parent, 110, "Behavior \xc2\xb7 Auto-archive",
                      "settings_autoarchive_label");
        stub_line(ctx, parent, 111, "Archive threads after",
                  "5 days \xc2\xb7 coming soon", "settings_autoarchive");
    }

    // Memory backend: Traditional vs Hindsight (Hindsight is admin-only in the
    // web app). Shown as an inert segmented stub.
    // TODO(settings-api): needs PUT /api/user/preferences.memoryBackend — not
    // wired yet (Hindsight also gated to admins server-side).
    void render_memory_backend_row(UIContext<InputAction>& ctx, Entity& parent,
                                   AppComponent& app) {
        (void)app;
        section_label(ctx, parent, 120, "Behavior \xc2\xb7 Memory backend",
                      "settings_memory_label");
        stub_line(ctx, parent, 121, "Memory backend",
                  "Traditional \xc2\xb7 coming soon", "settings_memory");
    }

    // ── Notifications group ────────────────────────────────────────────────
    // Notification sound: Off / Ping. Inert segmented stub.
    // TODO(settings-api): needs PUT /api/user/preferences.notificationSound —
    // not wired yet.
    void render_notification_row(UIContext<InputAction>& ctx, Entity& parent,
                                 AppComponent& app) {
        (void)app;
        section_label(ctx, parent, 130, "Notifications \xc2\xb7 Sound",
                      "settings_notif_label");
        // Default highlight = Ping (matches the web default).
        stub_segmented(ctx, parent, 131, {"Off", "Ping"}, 1, "settings_notif");
    }

    // ── Model group ────────────────────────────────────────────────────────
    // Default model. Needs a model list fetched from the API to populate a
    // picker, plus a write path to persist the selection.
    // TODO(settings-api): needs PUT /api/user/preferences.defaultModelId — not
    // wired yet; also needs a GET model-list endpoint to enumerate choices.
    void render_model_row(UIContext<InputAction>& ctx, Entity& parent,
                          AppComponent& app) {
        (void)app;
        section_label(ctx, parent, 140, "Model \xc2\xb7 Default model",
                      "settings_model_label");
        stub_line(ctx, parent, 141, "Default model",
                  "(uses server default) \xc2\xb7 coming soon",
                  "settings_model");
    }

    // ── Advanced group ─────────────────────────────────────────────────────
    // A cluster of power-user rows, all inert stubs pending API support.
    void render_advanced_section(UIContext<InputAction>& ctx, Entity& parent,
                                 AppComponent& app) {
        (void)app;
        section_label(ctx, parent, 150, "Advanced", "settings_advanced_label");
        // TODO(settings-api): needs PUT /api/user/preferences.branchOverrideUrl
        stub_line(ctx, parent, 151, "Branch override URL", "\xe2\x80\x94",
                  "settings_branch");
        // TODO(settings-api): needs PUT /api/user/preferences.enabledExperiments
        stub_line(ctx, parent, 152, "Enabled experiments", "none",
                  "settings_experiments");
        // TODO(settings-api): needs PUT
        // /api/user/preferences.compactionThreshold (10\xe2\x80\x9390)
        stub_line(ctx, parent, 153, "Compaction threshold", "default",
                  "settings_compaction");
        // TODO(settings-api): needs PUT /api/user/preferences.keyboardShortcuts
        stub_line(ctx, parent, 154, "Keyboard shortcuts", "defaults",
                  "settings_shortcuts");
        // TODO(settings-api): reset onboarding / sandbox needs a dedicated API
        // endpoint (not part of the preferences schema).
        stub_line(ctx, parent, 155, "Reset onboarding / sandbox", "\xe2\x80\xa6",
                  "settings_reset");
        // Note: these all require API support that hanabi doesn't have yet.
        div(ctx, mk(parent, 156),
            ComponentConfig{}
                .with_label("These require API support (not wired yet).")
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("settings_advanced_note"));
    }

    // Account row: shows the backend-reported identity + counts so the user can
    // verify hanabi is talking to the right account / is set up correctly.
    // Data comes from the async /whoami fetch (app.settings / settingsState),
    // kicked when the overlay opens. Shows a loading/err/empty state honestly.
    void render_account_row(UIContext<InputAction>& ctx, Entity& parent,
                            AppComponent& app) {
        section_label(ctx, parent, 30, "Account", "settings_account_label");

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

        // Sign out — inert stub (no auth-clear/sign-out flow wired yet).
        // TODO(settings): wire to token_store clear + client teardown +
        // return-to-login. Rendered as a disabled-looking secondary button so
        // the row is visible.
        stub_line(ctx, parent, 32, "Sign out", "coming soon",
                  "settings_signout");
    }

    // One segmented theme button. Selected = accent fill; others = secondary.
    void theme_choice(UIContext<InputAction>& ctx, Entity& parent, int id,
                      const std::string& label, const std::string& value,
                      AppComponent& app, float segW = 102.0f,
                      bool trailingGap = true) {
        bool selected = (app.themeChoice == value);
        auto btn = button(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(segW), pixels(32)})
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

    // Apply a theme choice: remember it, persist it, and re-tint live.
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
};

}  // namespace ecs
