#pragma once

#include "../ui_context.h"
#include "secondary_surface_geometry.h"
#include <algorithm>

#include "control_state.h"
#include "theme.h"
#include "edge_fade.h"
#include "div.h"

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
    tint.a = theme::mode() == theme::Mode::Light ? 34 : 16;
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

inline void edge_fades(ui_imm::UIContextType& ctx,
                       afterhours::Entity& parent, int id,
                       afterhours::Entity& scroll, theme::Color ground,
                       int layer, const std::string& name) {
    using afterhours::ui::imm::mk;
    const afterhours::EntityID scrollId = scroll.id;
    hanabi::ui::div(ctx, mk(parent, id),
        ComponentConfig{}
            .with_size(ComponentSize{afterhours::ui::percent(1.0f),
                                     afterhours::ui::percent(1.0f)})
            .with_absolute_position()
            .with_translate(0.0f, 0.0f)
            .with_transparent_bg()
            .with_roundness(0.0f)
            .with_ignore_pointer_events()
            .with_skip_tabbing(true)
            .with_render_layer(layer)
            .with_on_draw_fg([scrollId, ground](RectangleType) {
                auto opt = afterhours::ui::UICollectionHolder::getEntityForID(scrollId);
                if (!opt.valid() || !opt->has<afterhours::ui::HasScrollView>() ||
                    !opt->has<afterhours::ui::UIComponent>())
                    return;
                const auto& sv = opt->get<afterhours::ui::HasScrollView>();
                const RectangleType r = opt->get<afterhours::ui::UIComponent>().rect();
                const float content = std::max(sv.content_size.y, sv.unbuilt_content_size.y);
                const auto f = hanabi::edge_fade::strength(sv.scroll_offset.y, content, r.height);
                theme::Color clear = ground;
                clear.a = 0;
                if (f.top > 0.0f) {
                    theme::Color top = ground;
                    top.a = hanabi::edge_fade::alpha_for(f.top, ground.a);
                    afterhours::draw_rectangle_gradient_v(
                        RectangleType{r.x, r.y, r.width, hanabi::edge_fade::kFadeH}, top, clear);
                }
                if (f.bottom > 0.0f) {
                    theme::Color bottom = ground;
                    bottom.a = hanabi::edge_fade::alpha_for(f.bottom, ground.a);
                    afterhours::draw_rectangle_gradient_v(
                        RectangleType{r.x, r.y + r.height - hanabi::edge_fade::kFadeH, r.width,
                                      hanabi::edge_fade::kFadeH},
                        clear, bottom);
                }
            })
            .with_debug_name(name));
}

inline ComponentConfig field(float width, int layer, float height = kFieldH) {
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(height)})
        .with_custom_background(theme::panel_bg_2())
        .with_border(theme::border(), pixels(1.0f))
        .with_custom_text_color(theme::text_primary())
        .with_font_size(pixels(height * kFieldFontRatio))
        .with_alignment(TextAlignment::Left)
        .with_corner_radius(kControlCorner)
        .with_render_layer(layer);
    return out;
}

inline ComponentConfig option_row(float width, float height,
                                  control::State state, int layer,
                                  theme::Color base = theme::panel_bg()) {
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(height)})
        .with_custom_background(control::fill(state, base))
        .with_custom_hover_bg(control::hover_fill(state, base))
        .with_custom_text_color(control::ink(state))
        .with_click_activation(ClickActivationMode::Press)
        .with_corner_radius(kControlCorner)
        .with_render_layer(layer);
    out.disabled = state.disabled;
    return out;
}

inline ComponentConfig option_row(float width, float height, bool selected,
                                  int layer,
                                  theme::Color base = theme::panel_bg()) {
    control::State state;
    state.selected = selected;
    return option_row(width, height, state, layer, base);
}

inline ComponentConfig action_button(float width, bool primary, int layer,
                                     control::State state = {}) {
    const theme::Color base =
        primary ? theme::button_primary() : theme::button_secondary();
    ComponentConfig out;
    out.with_size(ComponentSize{pixels(width), pixels(kButtonH)})
        .with_custom_background(control::fill(state, base))
        .with_custom_hover_bg(control::hover_fill(state, base))
        .with_custom_text_color(state.disabled ? theme::disabled_text()
                                               : (primary
                                                      ? theme::window_bg()
                                                      : theme::text_primary()))
        .with_alignment(TextAlignment::Center)
        .with_align_items(AlignItems::Center)
        .with_click_activation(ClickActivationMode::Press)
        .with_corner_radius(kControlCorner)
        .with_render_layer(layer);
    out.disabled = state.disabled;
    return out;
}

}  // namespace hanabi::surface
