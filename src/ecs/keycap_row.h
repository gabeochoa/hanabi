#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include "../ui/keycaps.h"
#include "ui_imports.h"

namespace ecs::keycap_row {

inline constexpr float kCapH = 20.0f;
inline constexpr float kCapMinW = 22.0f;
inline constexpr float kCapGap = 4.0f;

inline float cap_width(const hanabi::keycaps::Cap& cap) {
    float w = kCapMinW;
    if (auto* fm = afterhours::EntityHelper::get_singleton_cmp<
            afterhours::ui::FontManager>()) {
        const float tw = afterhours::measure_text(fm->get_active_font(),
                                                  cap.text.c_str(),
                                                  theme::type::SM, 1.0f)
                             .x;
        w = std::max(w, tw + 10.0f);
    }
    return w;
}

inline void render(UIContext<InputAction>& ctx, Entity& parent,
                       int firstId, std::string_view display,
                       const std::string& name, bool muted) {
    const auto caps = hanabi::keycaps::split(display);
    if (caps.empty()) {
        div(ctx, mk(parent, firstId),
            ComponentConfig{}
                .with_label(std::string(display))
                .with_size(ComponentSize{children(), pixels(kCapH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name(name + "_text"));
        return;
    }
    int id = firstId;
    for (const hanabi::keycaps::Cap& cap : caps) {
        const bool isCap = cap.kind != hanabi::keycaps::Kind::Text;
        ComponentConfig cfg = ComponentConfig{}
            .with_label(cap.text)
            .with_size(ComponentSize{pixels(isCap ? cap_width(cap) : cap_width(cap) - 10.0f),
                                     pixels(kCapH)})
            .with_margin(Margin{.right = pixels(kCapGap)})
            .with_custom_text_color(muted ? theme::text_secondary()
                                          : theme::text_primary())
            .with_font_size(theme::type::SM)
            .with_alignment(TextAlignment::Center)
            .with_debug_name(name + (isCap ? "_cap" : "_sep") +
                             std::to_string(id - firstId));
        if (isCap) {
            cfg.with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.35f);
        } else {
            cfg.with_transparent_bg().with_roundness(0.0f);
        }
        div(ctx, mk(parent, id++), cfg);
    }
}

}
