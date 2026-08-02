#pragma once

// Settings overlay (Phase K). Renders a centered settings sheet over a dimmed
// full-window backdrop when AppComponent::showSettings is true. The panel
// carries a THEME toggle (Light / Dark / System) wired live: selecting a mode
// sets app.themeChoice, persists it via Settings::set_theme (auto-saves), and
// swaps the active token set via theme::set_mode so the whole UI re-tints this
// frame. Closes on Cmd+, (toggle), Esc, the ✕ close button, or clicking the
// backdrop.
//
// "System" mode: afterhours exposes no OS-appearance query (afterhours_gaps.md
// #16), so System is a labelled *choice* that currently falls back to Dark for
// the rendered palette; app.themeChoice remembers "system" so a future OS hook
// can honor it without touching this UI.
//
// Owns this file only. The gear button that would toggle showSettings lives in
// sidebar_system.h (owned by another agent); until that one-line hook lands,
// Cmd+, opens/closes this overlay.

#include <cstdio>
#include <string>

#include "../api/disk_cache.h"
#include "../settings.h"
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
        const float ph = kPadV * 2.0f + kHeaderH +
                         (kSectionH + kThemeRowH) +      // Theme
                         (kSectionH + kCacheRowH) +      // Cache (usage+clear)
                         (kSectionH + kLimitRowH) +      // Cache limit
                         (kSectionH + kAccountRowH) +    // Account
                         (kFootnoteGap + kFootnoteH);    // footnote
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
        render_theme_row(ctx, panel.ent(), *app);
        render_cache_row(ctx, panel.ent(), *app);
        render_cache_limit_row(ctx, panel.ent(), *app);
        render_account_row(ctx, panel.ent(), *app);
        render_footnote(ctx, panel.ent(), *app);
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
    static constexpr float kAccountRowH = 24.0f; // identity + counts line
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
        section_label(ctx, parent, 2, "Theme", "settings_theme_label");

        auto row = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kThemeRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("settings_theme_row"));

        theme_choice(ctx, row.ent(), 1, "Light", "light", app);
        theme_choice(ctx, row.ent(), 2, "Dark", "dark", app);
        theme_choice(ctx, row.ent(), 3, "System", "system", app);
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
        int idx = 1;
        for (int i = 0; i < 4; ++i) {
            const auto& opt = kCapOptions[i];
            const bool selected = (opt.bytes == current);
            auto btn = button(ctx, mk(row.ent(), idx++),
                ComponentConfig{}
                    .with_label(opt.label)
                    // Four segments across a 320px content width (~76 each).
                    .with_size(ComponentSize{pixels(76), pixels(30)})
                    .with_margin(Margin{.right = pixels(4)})
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
    }

    // One segmented theme button. Selected = accent fill; others = secondary.
    void theme_choice(UIContext<InputAction>& ctx, Entity& parent, int id,
                      const std::string& label, const std::string& value,
                      AppComponent& app) {
        bool selected = (app.themeChoice == value);
        auto btn = button(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(102), pixels(32)})
                .with_margin(Margin{.right = pixels(6)})
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
        // System has no OS-appearance query (gap #16) — fall back to dark for
        // both the persisted mode and the live palette, but keep themeChoice
        // == "system" so intent is remembered.
        const bool light = (value == "light");
        auto& s = Settings::get();
        s.set_theme(light ? "light" : "dark");
        // NOTE: in this tree Settings::set_theme is a bare in-memory setter
        // (it does NOT auto-save — see REPORT), so persist explicitly through
        // the public write_save_file(). Once set_theme auto-saves upstream this
        // line becomes redundant but harmless.
        s.write_save_file();
        theme::set_mode(light ? theme::Mode::Light : theme::Mode::Dark);
    }

    void render_footnote(UIContext<InputAction>& ctx, Entity& parent,
                         AppComponent& app) {
        std::string note =
            app.themeChoice == "system"
                ? "System follows the OS soon; using Dark for now."
                : "Persists across relaunch. Esc or click outside to close.";
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
