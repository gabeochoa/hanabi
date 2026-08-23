#pragma once

// ---------------------------------------------------------------------------
// Keyboard shortcut reference (Cmd+/).
//
// Every shortcut in this app is invisible. Cmd+B folds the sidebar, Cmd+, opens
// settings, Cmd+W closes a tab, Enter sends — and nothing on screen says so, so
// they are only ever found by accident or by reading the source. This is the
// one sheet that says what the keyboard does.
//
// It is a REFERENCE, not a settings screen: nothing here is editable, and the
// rows are read from one table so a shortcut cannot be listed here and bound
// somewhere else. Dismissed with Cmd+/ again, Esc, or a click outside.
// ---------------------------------------------------------------------------

#include <array>
#include <string_view>

#include "../keys.h"
#include "../ui/icons.h"
#include "ui_imports.h"

namespace ecs {

struct ShortcutsSystem : afterhours::System<UIContext<InputAction>> {
    // One row of the sheet. A blank `keys` makes a section heading.
    struct Row {
        std::string_view keys;
        std::string_view what;
    };

    // The shortcuts this app actually binds. Each entry names the file that
    // owns the binding, so a shortcut that moves cannot quietly go stale here.
    static constexpr std::array<Row, 20> kRows{{
        {"", "Conversations"},
        {"Cmd N", "Start a new task"},              // composer_system.h
        {"Cmd W", "Close the current tab"},         // tab_bar_system.h
        {"Enter", "Send the message"},              // main_pane_system.h
        {"Esc", "Clear the composer"},              // main_pane_system.h
        {"", "Reading a thread"},
        {"Home  End", "Jump to the start or the newest"},   // main_pane_system.h
        {"Page Up/Dn", "Move a screenful"},
        {"Up  Down", "Move a few lines"},
        {"Cmd F", "Find in this conversation"},
        {"Cmd C", "Copy the selected text"},
        {"", "Window"},
        {"Cmd B", "Show or hide the sidebar"},      // sidebar_system.h
        {"Cmd ,", "Settings"},                      // settings_system.h
        {"Cmd /", "This list"},                     // here
        {"Esc", "Close whatever is open"},
        {"", "Anywhere on the desktop"},
        {"Cmd Shift N", "Bring hanabi forward and start a task"},  // native_extras.mm
        {"", "Mouse"},
        {"Hover a message", "Copy it, and see when it was sent"},
    }};

    static constexpr float kPanelW = 460.0f;
    static constexpr float kPadH = 24.0f;
    static constexpr float kPadV = 20.0f;
    static constexpr float kHeaderH = 34.0f;
    static constexpr float kRowH = 26.0f;
    static constexpr float kSectionH = 30.0f;
    static constexpr float kKeyColW = 132.0f;

    // Must count the section headings' top margin too — the panel is sized
    // from this, and 8px missed per heading is a clipped last row.
    static constexpr float kSectionGap = 8.0f;
    static float content_height() {
        float h = 0.0f;
        for (const auto& r : kRows)
            h += r.keys.empty() ? (kSectionH + kSectionGap) : kRowH;
        return h;
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Cmd+/ toggles. Mirrors the Cmd+, pattern in settings_system.h.
        if (hanabi::keys::cmd_down() &&
            hanabi::keys::pressed(hanabi::keys::kSlash))
            app->showShortcuts = !app->showShortcuts;

        if (!app->showShortcuts) return;

        // Esc closes. Settings owns the same key, so this only runs when the
        // shortcuts sheet is the thing that is open.
        if (hanabi::keys::pressed(hanabi::keys::kEscape)) {
            app->showShortcuts = false;
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            static_cast<float>(afterhours::graphics::get_screen_width());
        const float sh =
            static_cast<float>(afterhours::graphics::get_screen_height());

        const float pw = kPanelW;
        const float ph = kPadV * 2.0f + kHeaderH + content_height();
        const float px = (sw - pw) * 0.5f;
        const float py = (sh - ph) * 0.5f;

        // Dimmed backdrop. Click-outside dismisses, but ONLY when the press
        // lands outside the panel — the backdrop spans the window and sits
        // under the sheet, so it reports a press for clicks on the sheet too
        // (the same trap settings_system.h documents).
        auto backdrop = button(ctx, mk(uiRoot, 8300),
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
                .with_debug_name("shortcuts_backdrop"));
        if (backdrop &&
            !afterhours::ui::is_mouse_inside(ctx.mouse.pos,
                                             RectangleType{px, py, pw, ph})) {
            app->showShortcuts = false;
            return;
        }

        auto panel = div(ctx, mk(uiRoot, 8310),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(pw), pixels(ph)})
                .with_absolute_position()
                .with_translate(px, py)
                .with_custom_background(theme::panel_bg())
                .with_border(theme::border(), pixels(1.0f))
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(kPadV),
                                      .right = pixels(kPadH),
                                      .bottom = pixels(kPadV),
                                      .left = pixels(kPadH)})
                .with_corner_radius(8.0f)
                .with_render_layer(11)
                .with_debug_name("shortcuts_panel"));

        render_header(ctx, panel.ent(), *app);

        int id = 100;
        for (const auto& r : kRows) {
            if (r.keys.empty())
                section(ctx, panel.ent(), id++, r.what);
            else
                shortcut_row(ctx, panel.ent(), id++, r.keys, r.what);
        }
    }

  private:
    static float content_w() { return kPanelW - 2.0f * kPadH; }

    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kHeaderH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_header"));
        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label("Keyboard shortcuts")
                // Fills the row minus the close button, which pins it right —
                // afterhours has no flex-grow (gap #18).
                .with_size(ComponentSize{pixels(content_w() - 34.0f),
                                         pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_title"));
        auto closeBtn = button(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(26), pixels(26)})
                .with_margin(Margin{.left = pixels(8)})
                .with_custom_background(theme::panel_bg())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_click_activation(ClickActivationMode::Press)
                .with_roundness(0.3f)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "close", "\xc3\x97", theme::text_secondary(), 14.0f))
                .with_debug_name("shortcuts_close"));
        if (closeBtn) app.showShortcuts = false;
    }

    void section(UIContext<InputAction>& ctx, Entity& parent, int id,
                 std::string_view text) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(fmtutil::to_upper(std::string(text)))
                .with_size(ComponentSize{percent(1.0f), pixels(kSectionH)})
                .with_margin(Margin{.top = pixels(kSectionGap)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::MICRO)
                .with_letter_spacing(0.8f)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_section"));
    }

    void shortcut_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                      std::string_view keys, std::string_view what) {
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(kRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_row"));
        // The chord, on a key-cap surface so it reads as something to press.
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(std::string(keys))
                .with_size(ComponentSize{pixels(kKeyColW), pixels(19)})
                .with_padding(Padding{.right = pixels(8), .left = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_debug_name("shortcuts_keys"));
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(std::string(what))
                .with_size(ComponentSize{pixels(content_w() - kKeyColW - 12.0f),
                                         pixels(19)})
                .with_margin(Margin{.left = pixels(12)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_what"));
    }
};

}  // namespace ecs
