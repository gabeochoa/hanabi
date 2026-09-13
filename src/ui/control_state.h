#pragma once

#include <algorithm>

#include <afterhours/src/core/base_component.h>
#include <afterhours/src/core/entity.h>
#include "theme.h"

namespace hanabi::control {

inline constexpr float kMinHitTarget = 28.0f;

inline constexpr float kPressAlphaScale = 2.5f;

enum class Phase { Normal, Hover, Press, Disabled };

struct State {
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
    bool selected = false;

    Phase phase() const {
        if (disabled) return Phase::Disabled;
        if (pressed) return Phase::Press;
        if (hovered) return Phase::Hover;
        return Phase::Normal;
    }
};

inline theme::Color press_bg() {
    theme::Color wash = theme::hover_bg();
    const float scaled = static_cast<float>(wash.a) * kPressAlphaScale;
    wash.a = static_cast<unsigned char>(std::min(255.0f, scaled));
    return wash;
}

inline theme::Color press_over(theme::Color backdrop) {
    return theme::over(press_bg(), backdrop);
}

inline theme::Color effective_backdrop(theme::Color own,
                                       theme::Color surface) {
    return own.a > 0 ? own : surface;
}

inline theme::Color fill(State s, theme::Color base) {
    switch (s.phase()) {
        case Phase::Disabled: return theme::disabled_bg();
        case Phase::Press:
            return press_over(s.selected ? theme::selected_bg() : base);
        case Phase::Hover:
            return theme::hover_over(s.selected ? theme::selected_bg() : base);
        case Phase::Normal:
            return s.selected ? theme::selected_bg() : base;
    }
    return base;
}

inline theme::Color hover_fill(State s, theme::Color base) {
    if (s.disabled) return theme::disabled_bg();
    State hovered = s;
    hovered.hovered = true;
    return fill(hovered, base);
}

inline theme::Color ink(State s) {
    if (s.disabled) return theme::disabled_text();
    if (s.selected || s.hovered || s.pressed) return theme::text_primary();
    return theme::text_secondary();
}

inline theme::Color ink_emphasized(State s) {
    if (s.disabled) return theme::disabled_text();
    return theme::text_primary();
}

inline bool meets_hit_target(float w, float h) {
    return std::min(w, h) >= kMinHitTarget;
}

inline afterhours::EntityID& keyboard_pressed_id() {
    static afterhours::EntityID value = -1;
    return value;
}

struct TextSurface : afterhours::BaseComponent {};

inline void mark_text_surface(afterhours::Entity& e) {
    e.addComponentIfMissing<TextSurface>();
}

inline bool is_text_surface(const afterhours::Entity& e) {
    return e.has<TextSurface>();
}

struct Slop {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

inline Slop grow_to_target(float x, float y, float w, float h,
                           float target = kMinHitTarget) {
    const float gw = std::max(w, target);
    const float gh = std::max(h, target);
    return Slop{x - (gw - w) * 0.5f, y - (gh - h) * 0.5f, gw, gh};
}

}  // namespace hanabi::control
