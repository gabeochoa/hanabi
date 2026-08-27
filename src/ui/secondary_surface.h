#pragma once

#include "../ui_context.h"
#include "secondary_surface_geometry.h"
#include "theme.h"

namespace hanabi::surface {

using afterhours::ui::AlignItems;
using afterhours::ui::ClickActivationMode;
using afterhours::ui::ComponentSize;
using afterhours::ui::FlexDirection;
using afterhours::ui::FlexWrap;
using afterhours::ui::Padding;
using afterhours::ui::TextAlignment;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::pixels;

inline theme::Color destructive_surface() {
    theme::Color tint = theme::destructive();
    tint.a = theme::mode() == theme::Mode::Light ? 34 : 42;
    return theme::over(tint, theme::panel_bg());
}

inline ComponentConfig scrim(float width, float height, int layer) {
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(height)})
        .with_absolute_position()
        .with_translate(0.0f, 0.0f)
        .with_custom_background(theme::over(theme::scrim(), theme::window_bg()))
        .with_custom_hover_bg(theme::over(theme::scrim(), theme::window_bg()))
        .with_click_activation(ClickActivationMode::Press)
        .with_roundness(0.0f)
        .with_render_layer(layer);
    return out;
}

inline ComponentConfig sheet(const Rect& rect, int layer) {
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(rect.width), pixels(rect.height)})
        .with_absolute_position()
        .with_translate(rect.x, rect.y)
        .with_custom_background(theme::panel_bg())
        .with_border(theme::border(), pixels(1.0f))
        .with_flex_direction(FlexDirection::Column)
        .with_flex_wrap(FlexWrap::NoWrap)
        .with_padding(Padding{.top = pixels(kSheetPadV),
                              .left = pixels(kSheetPadH),
                              .bottom = pixels(kSheetPadV),
                              .right = pixels(kSheetPadH)})
        .with_corner_radius(kSheetCorner)
        .with_render_layer(layer);
    return out;
}

inline ComponentConfig menu(float width, float height, int layer) {
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(height)})
        .with_custom_background(theme::panel_bg())
        .with_border(theme::border(), pixels(1.0f))
        .with_flex_direction(FlexDirection::Column)
        .with_flex_wrap(FlexWrap::NoWrap)
        .with_padding(Padding{.top = pixels(4.0f),
                              .left = pixels(4.0f),
                              .bottom = pixels(4.0f),
                              .right = pixels(4.0f)})
        .with_corner_radius(kMenuCorner)
        .with_render_layer(layer);
    return out;
}

inline ComponentConfig field(float width, int layer, float height = kFieldH) {
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(height)})
        .with_custom_background(theme::panel_bg_2())
        .with_border(theme::border(), pixels(1.0f))
        .with_custom_text_color(theme::text_primary())
        .with_alignment(TextAlignment::Left)
        .with_corner_radius(kControlCorner)
        .with_render_layer(layer);
    return out;
}

inline ComponentConfig option_row(float width, float height, bool selected,
                                  int layer,
                                  theme::Color base = theme::panel_bg()) {
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(height)})
        .with_custom_background(selected ? theme::selected_bg() : base)
        .with_custom_hover_bg(theme::hover_over(base))
        .with_custom_text_color(selected ? theme::text_primary()
                                         : theme::text_secondary())
        .with_click_activation(ClickActivationMode::Press)
        .with_corner_radius(kControlCorner)
        .with_render_layer(layer);
    return out;
}

inline ComponentConfig action_button(float width, bool primary, int layer) {
    const theme::Color fill =
        primary ? theme::button_primary() : theme::button_secondary();
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(kButtonH)})
        .with_custom_background(fill)
        .with_custom_hover_bg(theme::hover_over(fill))
        .with_custom_text_color(primary ? theme::window_bg()
                                        : theme::text_primary())
        .with_alignment(TextAlignment::Center)
        .with_align_items(AlignItems::Center)
        .with_click_activation(ClickActivationMode::Press)
        .with_corner_radius(kControlCorner)
        .with_render_layer(layer);
    return out;
}

}  // namespace hanabi::surface
