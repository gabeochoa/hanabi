#pragma once

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../../vendor/afterhours/src/plugins/ui/components.h"

// Scroll-direction preference: make hanabi's scroll views track the macOS
// "natural scrolling" system setting instead of hard-coding a direction.
//
// The afterhours scroll system (systems.h) computes:
//     direction = invert_scroll ? +1.0f : -1.0f;
//     scroll_offset.y += direction * wheel.y * speed;
// where scroll_offset.y grows DOWNWARD (child.y -= offset). On macOS, sokol
// passes AppKit's `scrollingDeltaY` through raw, and AppKit ALREADY encodes the
// natural-scroll setting in that delta's sign. So the app should MATCH the OS,
// not counter it:
//   * Natural OFF -> the afterhours default (invert_scroll = FALSE,
//     direction = -1.0f) is the sign that feels correct. (Verified live on a
//     natural-OFF Mac, 2026-08-02.)
//   * Natural ON  -> invert (invert_scroll = TRUE, direction = +1.0f).
// Hence: invert_scroll = natural_scroll.
// (An earlier version derived invert = !natural from first-principles reasoning
// about the delta sign; live testing showed that was backwards, so we trust the
// observed behavior: invert = natural.)
//
// Manual escape hatch: HANABI_INVERT_SCROLL=0|1 forces the flag regardless of
// the OS setting. Default (unset) follows the OS.

// Defined in sokol_impl.mm on macOS (windowed link). true = natural ON.
// Declared unconditionally so shared UI code can call it; on a non-mac build
// this symbol is simply not linked (the .mm is macOS-only) and the header is
// only reached from the windowed app.
extern "C" bool macos_natural_scroll(void);

namespace hanabi {

// Smooth-scroll helpers. These were SFINAE-gated on whether the vendored
// afterhours had scroll_target/scroll_smoothing at all, because the old pin did
// not and hanabi had to compile against both. It does now, so the detection is
// gone and these are plain writes -- kept as named helpers only so the
// programmatic-scroll call sites keep reading as intent.
inline void set_scroll_smoothing(afterhours::ui::HasScrollView& sv, float f) {
    sv.scroll_smoothing = f;
}
// A programmatic offset write (jump-to-bottom, a scrollbar drag) must move the
// TARGET too, or the ease immediately glides back to where the wheel left it.
inline void sync_scroll_target(afterhours::ui::HasScrollView& sv) {
    sv.scroll_target = sv.scroll_offset;
}
inline void set_scroll_target_y(afterhours::ui::HasScrollView& sv, float y) {
    sv.scroll_target.y = y;
}

// Decide whether scroll views should invert their offset sign so the app
// matches the OS. Honors HANABI_INVERT_SCROLL as an override; otherwise
// derives from the macOS natural-scroll pref (invert = natural; see below).
inline bool should_invert_scroll() {
    // Resolved ONCE. This function is called from apply_scroll_prefs, which
    // runs per scroll panel per FRAME -- the sidebar, the transcript, a second
    // transcript in split view, the digest -- and the macOS branch below is an
    // @autoreleasepool around an NSUserDefaults read, measured at 333 ns a
    // call (100k calls, -O2, this machine). Three or four of those a frame is
    // about a microsecond, which is a tenth of a percent of a 1.2 ms frame:
    // small, real, and on the hot path to answer a question whose answer does
    // not change. It does NOT show up in the allocation gate, which counts
    // operator new and not CoreFoundation, so it was invisible to every
    // instrument this project has -- which is most of why it is worth writing
    // down. The house style already reads these on demand rather than per
    // frame (sokol_impl.mm, macos_is_dark_mode: "Read on demand (theme apply),
    // cheap"); this brings scrolling in line with it. The cost of caching is
    // that flipping the OS natural-scroll preference now takes effect on the
    // next launch rather than the next frame, which is the right trade for a
    // setting nobody toggles mid-session.
    static const bool resolved = [] {
        if (const char* env = std::getenv("HANABI_INVERT_SCROLL")) {
            std::string_view v{env};
            if (v == "1" || v == "true" || v == "TRUE" || v == "yes")
                return true;
            if (v == "0" || v == "false" || v == "FALSE" || v == "no")
                return false;
            // Unrecognized value: fall through to the OS-derived default.
        }
#if defined(__APPLE__)
        // Empirically verified live on a natural-OFF Mac (2026-08-02): the
        // afterhours default (invert_scroll = false, direction = -1) is the
        // sign that feels correct, and inverting on natural-OFF scrolled
        // BACKWARDS. AppKit already encodes the natural/OFF choice in the sign
        // of scrollingDeltaY that sokol passes through, so MATCH the OS rather
        // than counter it: invert only when natural scrolling is ON.
        return macos_natural_scroll();  // invert when natural scrolling is ON
#else
        // Non-mac: no OS seam wired; default to afterhours' natural-ON
        // behavior.
        return false;
#endif
    }();
    return resolved;
}

// Apply the resolved scroll-direction preference to a freshly-built scroll
// entity. Safe to call right after a preset::ScrollPanel() div(): the
// HasScrollView component is attached during config application (component_init
// creates it for Overflow::Auto/Scroll), so it exists by the time this runs.
inline void apply_scroll_prefs(afterhours::Entity& e) {
    if (e.has<afterhours::ui::HasScrollView>()) {
        auto& sv = e.get<afterhours::ui::HasScrollView>();
        sv.invert_scroll = should_invert_scroll();
        // Smooth (native-feeling) scrolling: the wheel writes scroll_target and
        // scroll_offset eases toward it each frame. Raw afterhours added the
        // wheel delta straight to the rendered offset, so scrolling was stepped
        // and janky vs macOS momentum scroll. 0.28/frame @ ~display-rate gives a
        // quick, smooth glide that still settles fast. HANABI_SCROLL_SMOOTH
        // overrides (1 = legacy instant; 0.1..0.9 = smoothing factor).
        // Resolved once, for the same reason should_invert_scroll is.
        static const float smooth = [] {
            float v = 0.28f;
            if (const char* env = std::getenv("HANABI_SCROLL_SMOOTH")) {
                const float parsed = static_cast<float>(std::atof(env));
                if (parsed > 0.0f && parsed <= 1.0f) v = parsed;
            }
            return v;
        }();
        set_scroll_smoothing(sv, smooth);  // no-op vs pinned afterhours

        // Pixels per unit of wheel delta. afterhours' default is 20 and that
        // is what ships; this is a knob rather than a new default because the
        // right number depends on hardware the harness cannot see, and one
        // number cannot be right for both kinds of scrolling this app
        // receives (afterhours_gaps.md #405):
        //
        //   * A trackpad or a Magic Mouse sends PRECISE deltas, which sokol
        //     scales by 0.1 before we see them (vendor/sokol/sokol_app.h,
        //     scrollWheel:). At 20 the content moves TWICE as far as the
        //     finger; 10 would track it exactly.
        //   * A third-party wheel sends LINE deltas. AppKit documents those
        //     as a line count and it is usually 1 a detent, though that half
        //     is UNVERIFIED here -- no wheel mouse on this machine, and the
        //     e2e injector writes the post-sokol float directly so it cannot
        //     stand in for one (#172). If it is 1, a detent moves one line of
        //     text where the macOS convention is three, and about 57 would
        //     match the platform.
        //
        // Both arrive as one float through one multiplier, so the two wants
        // are irreconcilable here and the gap names the upstream fix. Until
        // then this is a one-line change on the reader's own machine rather
        // than a rebuild.
        static const float speed = [] {
            float v = 0.0f;  // 0 = leave afterhours' own default alone
            if (const char* env = std::getenv("HANABI_SCROLL_SPEED")) {
                const float parsed = static_cast<float>(std::atof(env));
                if (parsed > 0.0f && parsed <= 400.0f) v = parsed;
            }
            return v;
        }();
        if (speed > 0.0f) sv.scroll_speed = speed;
    }
}

}  // namespace hanabi
