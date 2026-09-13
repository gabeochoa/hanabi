#pragma once

// Resizing the headless render target mid-pass frees a live pass's
// attachments and sokol aborts (VALIDATE_APIP_ATTACHMENTS_ALIVE).
// afterhours_gaps.md #374 has the defect, the differential and the upstream
// fix; docs/perf/STRESS.md has the measurements.
//
// The invariant: a resize moves the RESOLUTION immediately and the RENDER
// TARGET only at a frame boundary. Every frame loop opens its frame through
// begin_frame() below, which is the only place the target may change, and
// scripts/check_resize_deferral.py enforces both halves.

#include "../rl.h"
#include <afterhours/src/plugins/window_manager.h>

namespace hanabi::gfx {

struct PendingResize {
    int width = 0;
    int height = 0;
    bool armed = false;
};

inline PendingResize& pending_resize() {
    static PendingResize state;
    return state;
}

// Resizes actually applied at a frame boundary. The planted control for the
// e2e `expect_resizes_applied` command and scripts/stress_resize_gate.sh: a
// deferral that stopped deferring, and a fixture whose resize never happened,
// both read 0.
inline unsigned& applied_resize_count() {
    static unsigned n = 0;
    return n;
}

// The size the backend REPORTS after being resized -- read back from
// graphics, never echoed from the request. Echoing the request would make the
// "the target really moved" check pass even if set_window_size became a no-op,
// which is the one thing that check exists to catch.
inline PendingResize& applied_resize_size() {
    static PendingResize observed;
    return observed;
}

// Layout now, GPU at the next frame boundary.
inline void request_resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (auto* pcr = afterhours::EntityHelper::get_singleton_cmp<
            afterhours::window_manager::ProvidesCurrentResolution>()) {
        pcr->current_resolution.width = w;
        pcr->current_resolution.height = h;
        // No `should_refetch = false` here. stress.h carried one, on the
        // theory that CollectCurrentResolution would put the old size back
        // next frame. Removing it changes nothing: all eight resize scripts
        // still pass, including the four that assert post-resize geometry.
        // The write below is what is load-bearing, and the scripts prove it --
        // sidebar_width_is_responsive asserts w=280 after the resize and fails
        // if the resolution does not move.
    }
    PendingResize& p = pending_resize();
    p = PendingResize{w, h, true};
}

// The only frame opener in this app. Applies a requested resize first, with no
// pass open and nothing recorded against the outgoing target.
inline void begin_frame() {
    if (PendingResize& p = pending_resize(); p.armed) {
        p.armed = false;
        afterhours::window_manager::set_window_size(p.width, p.height);
        applied_resize_size() = PendingResize{
            static_cast<int>(afterhours::graphics::get_screen_width()),
            static_cast<int>(afterhours::graphics::get_screen_height()), false};
        ++applied_resize_count();
    }
    afterhours::graphics::begin_frame();
}

}  // namespace hanabi::gfx
