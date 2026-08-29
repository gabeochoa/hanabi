#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "../keys.h"
#include "../menubar.h"
#include "../settings.h"
#include "../ui/icons.h"
#include "../ui/secondary_surface.h"
#include "ui_imports.h"

namespace ecs {

struct ShortcutsSystem : afterhours::System<UIContext<InputAction>> {
    struct ReferenceRow {
        std::string_view keys;
        std::string_view title;
    };

    static constexpr std::array<ReferenceRow, 17> kReferenceRows{{
        {"", "Editing text"},
        {"Opt Left/Right", "Move a word"},
        {"Opt Backspace", "Delete the word before"},
        {"Opt Delete", "Delete the word after"},
        {"Cmd Left/Right", "Start or end of the line"},
        {"Cmd Backspace", "Delete back to the line start"},
        {"Cmd A", "Select everything in the field"},
        {"Shift + a move", "Select as you go"},
        {"", "Reading and composing"},
        {"Return", "Send or insert a line, as configured"},
        {"Home / End", "Jump to the start or the newest"},
        {"Page Up / Down", "Move a screenful"},
        {"Up / Down", "Move a few lines"},
        {"Cmd C", "Copy the selected text"},
        {"", "Anywhere on the desktop"},
        {"Cmd Shift N", "Bring the app forward and start a task"},
        {"Cmd Shift K", "Bring the app forward and search"},
    }};

    static constexpr float kPanelW = 900.0f;
    static constexpr float kPanelH = 590.0f;
    static constexpr float kColGap = 28.0f;
    static constexpr float kTwoColumnMinPanelW = 700.0f;
    static constexpr float kPadH = hanabi::surface::kSheetPadH;
    static constexpr float kPadV = hanabi::surface::kSheetPadV;
    static constexpr float kHeaderH = hanabi::surface::kHeaderH;
    static constexpr float kCommandRowH = 34.0f;
    static constexpr float kReferenceRowH = 26.0f;
    static constexpr float kSectionH = 30.0f;
    static constexpr float kKeyColW = 150.0f;

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (app == nullptr || !app->showShortcuts) return;

        ctx.theme.background = theme::panel_bg();
        ctx.theme.font_muted = theme::text_secondary();

        if (app->escape == EscapeIntent::CancelShortcutRecording) {
            app->shortcutRecording = -1;
            app->shortcutMessage = "Recording cancelled.";
        } else if (app->escape == EscapeIntent::CloseShortcuts) {
            close(*app);
            return;
        }

        if (app->shortcutRecording >= 0) capture(*app);

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto panelRect = hanabi::surface::centered(
            hanabi::viewport::width(), hanabi::viewport::height(), kPanelW,
            kPanelH);
        const float contentW = panelRect.width - kPadH * 2.0f;
        const bool twoColumns = panelRect.width >= kTwoColumnMinPanelW;
        const float colW = twoColumns ? (contentW - kColGap) * 0.5f
                                      : contentW;

        auto backdrop = button(
            ctx, mk(uiRoot, 8300),
            hanabi::surface::scrim(hanabi::viewport::width(),
                                   hanabi::viewport::height(), 10)
                .with_debug_name("shortcuts_backdrop"));
        if (backdrop &&
            !afterhours::ui::is_mouse_inside(
                ctx.mouse.pos,
                RectangleType{panelRect.x, panelRect.y, panelRect.width,
                              panelRect.height})) {
            close(*app);
            return;
        }

        auto panel = div(ctx, mk(uiRoot, 8310),
                         hanabi::surface::sheet(panelRect, 11)
                             .with_debug_name("shortcuts_panel"));
        render_header(ctx, panel.ent(), *app, contentW);

        auto cols = div(
            ctx, mk(panel.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(contentW),
                                         pixels(panelRect.height - kPadV * 2.0f -
                                                kHeaderH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_overflow(Overflow::Scroll, Axis::Y)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(11)
                .with_debug_name("shortcuts_cols"));

        auto content = div(
            ctx, mk(cols.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(contentW), children()})
                .with_flex_direction(twoColumns ? FlexDirection::Row
                                                : FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(11)
                .with_debug_name("shortcuts_content"));

        auto left = div(ctx, mk(content.ent(), 1),
                        ComponentConfig{}
                            .with_size(ComponentSize{pixels(colW), children()})
                            .with_margin(Margin{.right = pixels(twoColumns ? kColGap : 0.0f)})
                            .with_flex_direction(FlexDirection::Column)
                            .with_flex_wrap(FlexWrap::NoWrap)
                            .with_transparent_bg()
                            .with_roundness(0.0f)
                            .with_render_layer(11)
                            .with_debug_name("shortcuts_commands"));
        auto right = div(ctx, mk(content.ent(), 2),
                         ComponentConfig{}
                             .with_size(ComponentSize{pixels(colW), children()})
                             .with_flex_direction(FlexDirection::Column)
                             .with_flex_wrap(FlexWrap::NoWrap)
                             .with_transparent_bg()
                             .with_roundness(0.0f)
                             .with_render_layer(11)
                             .with_debug_name("shortcuts_reference"));

        section(ctx, left.ent(), 1, "Customizable commands", colW);
        int id = 20;
        for (const auto& item : hanabi::shortcuts::kDefinitions)
            command_row(ctx, left.ent(), id++, item, *app, colW);

        id = 200;
        for (const auto& row : kReferenceRows) {
            if (row.keys.empty())
                section(ctx, right.ent(), id++, row.title, colW);
            else
                reference_row(ctx, right.ent(), id++, row.keys, row.title, colW);
        }
    }

   private:
    static void close(AppComponent& app) {
        app.showShortcuts = false;
        app.shortcutRecording = -1;
        app.shortcutMessage.clear();
        menubar_set_shortcut_recording(-1);
    }

    static void capture(AppComponent& app) {
        std::optional<hanabi::shortcuts::Shortcut> candidate;
        int key = 0;
        unsigned char modifiers = 0;
        if (menubar_take_recorded_shortcut(&key, &modifiers))
            candidate = hanabi::shortcuts::Shortcut{key, modifiers};
        else
            candidate = hanabi::keys::capture_shortcut();
        if (!candidate.has_value()) return;

        const auto command = static_cast<hanabi::shortcuts::Command>(
            app.shortcutRecording);
        const auto result = Settings::get().set_shortcut(command, *candidate);
        if (!result.ok) {
            app.shortcutMessage = result.explanation;
            return;
        }
        app.shortcutMessage =
            "Saved " + std::string(hanabi::shortcuts::definition(command).title) +
            " as " + hanabi::shortcuts::display(*candidate) + ".";
        app.shortcutRecording = -1;
        menubar_set_shortcut_recording(-1);
    }

    static void render_header(UIContext<InputAction>& ctx, Entity& parent,
                              AppComponent& app, float contentW) {
        auto header = div(ctx, mk(parent, 1),
                          ComponentConfig{}
                              .with_size(ComponentSize{pixels(contentW),
                                                       pixels(kHeaderH)})
                              .with_flex_direction(FlexDirection::Column)
                              .with_flex_wrap(FlexWrap::NoWrap)
                              .with_transparent_bg()
                              .with_roundness(0.0f)
                              .with_debug_name("shortcuts_header"));
        auto titleRow = div(ctx, mk(header.ent(), 1),
                            ComponentConfig{}
                                .with_size(ComponentSize{
                                    pixels(contentW),
                                    pixels(hanabi::surface::kTitleH)})
                                .with_flex_direction(FlexDirection::Row)
                                .with_flex_wrap(FlexWrap::NoWrap)
                                .with_align_items(AlignItems::Center)
                                .with_transparent_bg()
                                .with_roundness(0.0f)
                                .with_debug_name("shortcuts_title_row"));
        div(ctx, mk(titleRow.ent(), 1),
            ComponentConfig{}
                .with_label("Keyboard shortcuts")
                .with_size(ComponentSize{pixels(contentW - 190.0f), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::H1)
                .with_font_weight(theme::type::EMPHASIS)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_title"));
        auto restore = button(
            ctx, mk(titleRow.ent(), 2),
            ComponentConfig{}
                .with_label("Restore defaults")
                .with_size(ComponentSize{pixels(142), pixels(28)})
                .with_custom_background(theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_debug_name("shortcuts_restore_defaults"));
        auto closeButton = button(
            ctx, mk(titleRow.ent(), 3),
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
                .with_debug_name("shortcuts_close"));
        const std::string subtitle = app.shortcutMessage.empty()
                                         ? "Choose a command, then press a new shortcut"
                                         : app.shortcutMessage;
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label(subtitle)
                .with_size(ComponentSize{pixels(contentW),
                                         pixels(hanabi::surface::kSubtitleH)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(app.shortcutMessage.empty()
                                            ? theme::text_secondary()
                                            : theme::accent())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_subtitle"));
        if (restore) {
            Settings::get().reset_shortcuts();
            app.shortcutRecording = -1;
            app.shortcutMessage = "Default shortcuts restored.";
        }
        if (closeButton) close(app);
    }

    static void section(UIContext<InputAction>& ctx, Entity& parent, int id,
                        std::string_view title, float width) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(fmtutil::to_upper(std::string(title)))
                .with_size(ComponentSize{pixels(width), pixels(kSectionH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_faint())
                .with_font_size(theme::type::MICRO)
                .with_letter_spacing(0.8f)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_section"));
    }

    static void command_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                            const hanabi::shortcuts::Definition& item,
                            AppComponent& app, float width) {
        auto row = div(ctx, mk(parent, id),
                       ComponentConfig{}
                           .with_size(ComponentSize{pixels(width),
                                                    pixels(kCommandRowH)})
                           .with_flex_direction(FlexDirection::Row)
                           .with_flex_wrap(FlexWrap::NoWrap)
                           .with_align_items(AlignItems::Center)
                           .with_transparent_bg()
                           .with_roundness(0.0f)
                           .with_debug_name("shortcut_command_row"));
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(std::string(item.title))
                .with_size(ComponentSize{pixels(width - kKeyColW - 12.0f),
                                         pixels(22)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcut_command_label"));
        const bool active = app.shortcutRecording == static_cast<int>(item.command);
        const std::string label =
            active ? "Press shortcut..."
                   : hanabi::shortcuts::display(
                         Settings::get().get_shortcut(item.command));
        auto recorder = button(
            ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(kKeyColW), pixels(26)})
                .with_custom_background(active ? theme::selected_bg()
                                               : theme::panel_bg_2())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg_2()))
                .with_border(active ? theme::accent() : theme::border(),
                             pixels(1.0f))
                .with_custom_text_color(active ? theme::text_primary()
                                               : theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_debug_name("shortcut_record_" + std::string(item.key)));
        if (recorder) {
            app.shortcutRecording = static_cast<int>(item.command);
            app.shortcutMessage = "Press a shortcut with Command. Escape cancels.";
            ctx.set_focus(recorder.ent().id);
        }
    }

    static void reference_row(UIContext<InputAction>& ctx, Entity& parent, int id,
                              std::string_view keys, std::string_view title,
                              float width) {
        auto row = div(ctx, mk(parent, id),
                       ComponentConfig{}
                           .with_size(ComponentSize{pixels(width),
                                                    pixels(kReferenceRowH)})
                           .with_flex_direction(FlexDirection::Row)
                           .with_flex_wrap(FlexWrap::NoWrap)
                           .with_align_items(AlignItems::Center)
                           .with_transparent_bg()
                           .with_roundness(0.0f)
                           .with_debug_name("shortcuts_reference_row"));
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(std::string(keys))
                .with_size(ComponentSize{pixels(kKeyColW), pixels(19)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.3f)
                .with_debug_name("shortcuts_reference_keys"));
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(std::string(title))
                .with_size(ComponentSize{pixels(width - kKeyColW - 12.0f),
                                         pixels(19)})
                .with_margin(Margin{.left = pixels(12)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::MD)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("shortcuts_reference_title"));
    }
};

}
