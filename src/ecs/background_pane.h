#pragma once
// The window background behind the transcript, drawn through the pane's own
// background hook from the scene ui/background.h computes each frame. Holds
// the little state a uniform block needs across frames (frame count, the
// clocks a focus or status change starts, a smoothed pointer) and knows the
// renderer; the preset maths stays in ui/background.h.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "../api/types.h"
#include "../settings.h"
#include "../ui/background.h"
#include "../ui/theme.h"
#include "ui_clock.h"
#include "ui_imports.h"

namespace ecs::background_pane {

namespace bg = hanabi::background;

struct State {
    int frame = 0;
    double lastTime = 0.0;
    bool lastFocus = true;
    double focusChangedAt = 0.0;
    int lastStatus = 0;
    double statusChangedAt = 0.0;
    float mouseX = 0.0f, mouseY = 0.0f;
    bool mouseSeen = false;
    float glow = 0.0f;
    unsigned long paints = 0;       // draw() calls so far
    std::size_t lastPrimitives = 0; // the last scene's quads + blobs
};

inline State& state() {
    static State s;
    return s;
}

inline bg::Rgba rgba(theme::Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}
inline theme::Color color(bg::Rgba c) {
    const auto b = [](float v) {
        return static_cast<unsigned char>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    return theme::Color{b(c.r), b(c.g), b(c.b), b(c.a)};
}

inline bg::Preset preset() { return bg::preset_from_key(Settings::get().get_background()); }

// The uniform block for one frame of one pane.
inline bg::Uniforms uniforms(const LayoutComponent::Rect& r, float mouseX, float mouseY, bool appActive,
                             bool reduceMotion, int status, bool toolActive, float streamRate,
                             float scroll, const std::string& threadId, float glowPulse) {
    State& s = state();
    const double now = hanabi::ui_clock::now_seconds();
    const double dt = s.lastTime > 0.0 ? now - s.lastTime : 0.0;
    s.lastTime = now;
    ++s.frame;
    if (appActive != s.lastFocus) {
        s.lastFocus = appActive;
        s.focusChangedAt = now;
    }
    if (status != s.lastStatus) {
        s.lastStatus = status;
        s.statusChangedAt = now;
    }
    // The pointer eases toward where it is, so the glow glides rather than
    // snapping when it re-enters the pane.
    const float px = mouseX - r.x, py = mouseY - r.y;
    const bool inside = px >= 0.0f && py >= 0.0f && px <= r.width && py <= r.height;
    if (inside) {
        if (!s.mouseSeen) {
            s.mouseX = px;
            s.mouseY = py;
            s.mouseSeen = true;
        } else {
            const float k = reduceMotion ? 1.0f : 0.18f;
            s.mouseX += (px - s.mouseX) * k;
            s.mouseY += (py - s.mouseY) * k;
        }
    }
    s.glow = glowPulse;

    bg::Uniforms u;
    u.resolution[0] = r.width;
    u.resolution[1] = r.height;
    u.time = static_cast<float>(now);
    u.time_delta = static_cast<float>(dt);
    u.frame = s.frame;
    u.mouse[0] = s.mouseX;
    u.mouse[1] = s.mouseY;
    u.mouse_inside = s.mouseSeen;
    u.scroll = scroll;
    u.pane_origin[0] = r.x;
    u.pane_origin[1] = r.y;
    u.focus = appActive;
    u.time_focus = static_cast<float>(s.focusChangedAt);
    u.reduced_motion = reduceMotion;
    u.status = status;
    u.time_status = static_cast<float>(now - s.statusChangedAt);
    u.stream_rate = streamRate;
    u.run_elapsed = status == 1 ? static_cast<float>(now - s.statusChangedAt) : 0.0f;
    u.tool_active = toolActive;
    u.background = rgba(theme::chrome::content());
    u.foreground = rgba(theme::text_primary());
    u.accent = rgba(theme::accent());
    u.light = theme::mode() == theme::Mode::Light;
    u.thread_hash = bg::hash_id(threadId);
    u.glow = glowPulse;
    return u;
}

// Paints base colour, scene, then the legibility scrim, clipped to the pane.
inline void draw(const RectangleType& rect, const bg::Scene& scene, theme::Color base,
                 float scrimAlpha) {
    ++state().paints;
    state().lastPrimitives = bg::primitive_count(scene);
    afterhours::draw_rectangle(rect, base);
    afterhours::begin_scissor_mode(static_cast<int>(rect.x), static_cast<int>(rect.y),
                                   static_cast<int>(rect.width), static_cast<int>(rect.height));
    {
        afterhours::blend_scope blend(scene.additive ? afterhours::blend::Mode::Additive
                                                     : afterhours::blend::Mode::Alpha);
        for (const bg::Quad& q : scene.quads)
            afterhours::draw_rectangle_gradient_ex(
                RectangleType{rect.x + q.x, rect.y + q.y, q.w, q.h}, color(q.tl), color(q.bl),
                color(q.tr), color(q.br));
        for (const bg::Blob& b : scene.blobs)
            afterhours::draw_circle_gradient(static_cast<int>(rect.x + b.x),
                                             static_cast<int>(rect.y + b.y), b.r, color(b.inner),
                                             color(b.outer));
    }
    afterhours::end_scissor_mode();
    theme::Color scrim = base;
    scrim.a = static_cast<unsigned char>(std::lround(std::clamp(scrimAlpha, 0.0f, 1.0f) * 255.0f));
    afterhours::draw_rectangle(rect, scrim);
}

}  // namespace ecs::background_pane
