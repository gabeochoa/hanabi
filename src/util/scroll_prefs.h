#pragma once

#include <cstdlib>
#include <string_view>

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

// Decide whether scroll views should invert their offset sign so the app
// matches the OS. Honors HANABI_INVERT_SCROLL as an override; otherwise
// derives from the macOS natural-scroll pref (invert = natural; see below).
inline bool should_invert_scroll() {
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
    // afterhours default (invert_scroll = false, direction = -1) is the sign
    // that feels correct, and inverting on natural-OFF scrolled BACKWARDS.
    // AppKit already encodes the natural/OFF choice in the sign of
    // scrollingDeltaY that sokol passes through, so MATCH the OS rather than
    // counter it: invert only when natural scrolling is ON.
    return macos_natural_scroll();   // invert when natural scrolling is ON
#else
    // Non-mac: no OS seam wired; default to afterhours' natural-ON behavior.
    return false;
#endif
}

// Apply the resolved scroll-direction preference to a freshly-built scroll
// entity. Safe to call right after a preset::ScrollPanel() div(): the
// HasScrollView component is attached during config application (component_init
// creates it for Overflow::Auto/Scroll), so it exists by the time this runs.
inline void apply_scroll_prefs(afterhours::Entity& e) {
    if (e.has<afterhours::ui::HasScrollView>()) {
        e.get<afterhours::ui::HasScrollView>().invert_scroll =
            should_invert_scroll();
    }
}

}  // namespace hanabi
