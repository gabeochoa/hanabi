#pragma once
// A flat, test-only marker the capture receipt is PROVEN on.
//
// The receipt maps a widget's rect into a window capture. Proving that
// mapping on an arbitrary panel is not possible from its fill: panels carry
// strokes, rounded corners and children, so their outer pixels need not equal
// any one colour. This marker is the one widget whose pixels ARE known: a
// plain opaque rectangle of `fill` with a `kCorner`-pt square of `corner` in
// each corner, no stroke, no children, no roundness -- so a capture cropped by
// its receipt must show exactly those pixels at exactly those offsets, before
// and after a resize. Once the transform is proven on the marker, the same
// receipt is trusted for arbitrary panels with bounds / PID / frame checks
// only.
//
// Gated three ways: compiled under AFTER_HOURS_ENABLE_E2E_TESTING only; drawn
// only while a script has switched it on (`capture_marker on x y w h`, off by
// default and never persisted); and the command itself refuses outside a
// windowed mock run (the same gate as capture_receipt). Nothing production
// reads it.
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING

#include "../ui/context_menu.h"
#include "ui_imports.h"

namespace ecs {

struct CaptureMarkerState {
    bool on = false;
    float x = 0, y = 0, w = 0, h = 0;  // content points, top-left origin
};
inline CaptureMarkerState& capture_marker_state() {
    static CaptureMarkerState s;
    return s;
}

inline constexpr float kCaptureMarkerCorner = 6.0f;
// Two colours no theme uses, far apart in every channel.
inline constexpr afterhours::Color kCaptureMarkerFill{0x10, 0xC0, 0x20, 255};
inline constexpr afterhours::Color kCaptureMarkerCornerInk{0xE0, 0x20, 0xA0, 255};

struct CaptureMarkerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        const CaptureMarkerState& m = capture_marker_state();
        if (!m.on || m.w <= 0 || m.h <= 0) return;
        Entity& uiRoot = ui_imm::getUIRootEntity();
        div(ctx, mk(uiRoot, 8391),
            ComponentConfig{}
                .with_absolute_position()
                .with_translate(m.x, m.y)
                .with_size(ComponentSize{pixels(m.w), pixels(m.h)})
                .with_custom_background(kCaptureMarkerFill)
                .with_roundness(0.0f)
                .with_render_layer(hanabi::surface::kContextMenuLayer + 1)
                .with_on_draw_fg([](RectangleType r) {
                    const float c = kCaptureMarkerCorner;
                    afterhours::draw_rectangle(RectangleType{r.x, r.y, c, c}, kCaptureMarkerCornerInk);
                    afterhours::draw_rectangle(RectangleType{r.x + r.width - c, r.y, c, c},
                                               kCaptureMarkerCornerInk);
                    afterhours::draw_rectangle(RectangleType{r.x, r.y + r.height - c, c, c},
                                               kCaptureMarkerCornerInk);
                    afterhours::draw_rectangle(
                        RectangleType{r.x + r.width - c, r.y + r.height - c, c, c},
                        kCaptureMarkerCornerInk);
                })
                .with_debug_name("capture_marker"));
    }
};

}  // namespace ecs

#endif  // AFTER_HOURS_ENABLE_E2E_TESTING
