// HANABI_POINTER_PROBE=<x>,<y>: post a NATIVE mouseMoved at window point
// (x, y) -- logical pixels, top-left origin -- through AppKit's queue every
// frame, and print, from the same frame, every coordinate the pointer passes
// through on its way to a hit test, beside the rect of the widget the UI
// called hot. Same event path a real mouse takes into sokol (locationInWindow
// -> framebuffer flip); the harness's own mouse helper is NOT used, so a
// transform it shares with production cannot mask itself.
//
//   [pointer-probe] frame N win=<logical w>x<h> dpi=<s> R=<w>x<h>
//                   raw_fb=<x>,<y> window=<x>,<y> dest=<x>,<y>,<w>x<h> scale=<s>
//                   mapped=<x>,<y> hot=<name> rect=<x>,<y>,<w>x<h>
//                   window_in_hot=<0|1> mapped_in_hot=<0|1>
//
// `window` is the pointer in the space paint uses (raw / dpi). `mapped` is
// what hit tests use. They must agree; a frame where window_in_hot != 1 and
// mapped_in_hot == 1 is the hot widget being one the pointer is not over.
#include "pointer_probe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#import <AppKit/AppKit.h>

#include "rl.h"
#include <afterhours/src/ecs.h>
#include <afterhours/src/plugins/input_system.h>
#include <afterhours/src/plugins/ui/components.h>
#include <afterhours/src/plugins/ui/context.h>
#include <afterhours/src/plugins/window_manager.h>
#include "input_mapping.h"

namespace hanabi::pointer_probe {

namespace {
struct Spec {
    bool on = false;
    float x = 0.0f;
    float y = 0.0f;
};
const Spec& spec() {
    static const Spec s = [] {
        Spec out;
        const char* v = std::getenv("HANABI_POINTER_PROBE");
        if (v == nullptr || *v == '\0') return out;
        if (std::sscanf(v, "%f,%f", &out.x, &out.y) == 2) out.on = true;
        return out;
    }();
    return s;
}

NSWindow* main_window() {
    NSWindow* w = [NSApp mainWindow];
    if (w == nil) w = [NSApp keyWindow];
    if (w == nil)
        for (NSWindow* c in [NSApp windows])
            if ([c isVisible]) return c;
    return w;
}

// A mouseMoved at (x, y) logical, top-left origin of the CONTENT view, posted
// to the queue AppKit reads -- what the window server would post for a real
// pointer at that spot.
void post_move(NSWindow* win, float x, float y) {
    const NSRect content = [[win contentView] bounds];
    const NSPoint loc = NSMakePoint(x, content.size.height - y);
    NSEvent* e = [NSEvent mouseEventWithType:NSEventTypeMouseMoved
                                    location:loc
                               modifierFlags:0
                                   timestamp:[[NSProcessInfo processInfo] systemUptime]
                                windowNumber:[win windowNumber]
                                     context:nil
                                 eventNumber:0
                                  clickCount:0
                                    pressure:0.0f];
    [NSApp postEvent:e atStart:NO];
}

bool inside(float px, float py, const RectangleType& r) {
    return px >= r.x && px <= r.x + r.width && py >= r.y && py <= r.y + r.height;
}
}  // namespace

bool armed() { return spec().on; }

}  // namespace hanabi::pointer_probe

// After a native menu closes: the release that ended it never reached the
// view. `+[NSMenu popUpContextMenu:withEvent:forView:]` tracks from the
// right-down and CONSUMES the matching right-up inside AppKit, so sokol's
// `rightMouseUp:` never runs and the backend's button state stays down --
// the next right-down is then no press edge and the app opens nothing
// (measured: a second right-click on the same tab produced no request at
// all). Every button AppKit may have swallowed is reported released here as
// one synthetic release edge. Lives in this TU because it already reads the
// backend's input state.
extern "C" void metal_release_stuck_mouse_buttons(void) {
    auto& s = afterhours::graphics::metal_detail::input_state();
    for (int b = 0; b < afterhours::graphics::metal_detail::MAX_MOUSE_BUTTONS; ++b) {
        if (s.mouse_down[b]) {
            s.mouse_down[b] = false;
            s.mouse_released[b] = true;
        }
    }
}

namespace hanabi::pointer_probe {

void frame() {
    const Spec& s = spec();
    if (!s.on) return;
    static unsigned n = 0;
    ++n;
    NSWindow* win = main_window();
    if (win == nil) return;
    post_move(win, s.x, s.y);
    if (n < 3) return;  // let the first posted move be delivered

    using namespace afterhours;
    auto& in = graphics::metal_detail::input_state();
    const float dpi = sapp_dpi_scale();
    const float winW = static_cast<float>(graphics::get_screen_width());
    const float winH = static_cast<float>(graphics::get_screen_height());
    const float wx = in.mouse_x / dpi;
    const float wy = in.mouse_y / dpi;
    int rw = -1, rh = -1;
    if (auto* pcr = EntityHelper::get_singleton_cmp<
            window_manager::ProvidesCurrentResolution>()) {
        rw = pcr->current_resolution.width;
        rh = pcr->current_resolution.height;
    }
    const auto vp = window_manager::content_viewport(static_cast<int>(winW),
                                                     static_cast<int>(winH));
    const auto mapped = afterhours::input::get_mouse_position();

    std::string hotName = "(none)";
    RectangleType hotRect{0, 0, 0, 0};
    if (auto* ctx = EntityHelper::get_singleton_cmp<ui::UIContext<InputAction>>()) {
        auto hot = ui::UICollectionHolder::getEntityForID(ctx->hot_id);
        if (hot.valid() && hot->has<ui::UIComponent>()) {
            hotRect = hot->get<ui::UIComponent>().rect();
            if (hot->has<ui::UIComponentDebug>())
                hotName = hot->get<ui::UIComponentDebug>().name_value;
            else
                hotName = "id" + std::to_string(ctx->hot_id);
        }
    }
    std::printf(
        "[pointer-probe] frame %u win=%.0fx%.0f dpi=%.2f R=%dx%d raw_fb=%.1f,%.1f "
        "window=%.1f,%.1f dest=%.1f,%.1f,%.0fx%.0f scale=%.4f mapped=%.1f,%.1f "
        "hot=%s rect=%.1f,%.1f,%.0fx%.0f window_in_hot=%d mapped_in_hot=%d\n",
        n, winW, winH, dpi, rw, rh, in.mouse_x, in.mouse_y, wx, wy, vp.dest.x,
        vp.dest.y, vp.dest.width, vp.dest.height, vp.scale, mapped.x, mapped.y,
        hotName.c_str(), hotRect.x, hotRect.y, hotRect.width, hotRect.height,
        inside(wx, wy, hotRect) ? 1 : 0, inside(mapped.x, mapped.y, hotRect) ? 1 : 0);
    std::fflush(stdout);
}

}  // namespace hanabi::pointer_probe
