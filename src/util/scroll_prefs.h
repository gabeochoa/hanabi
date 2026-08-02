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

// Compile-time detection: does this afterhours build have the smooth-scroll
// fields (scroll_target / scroll_smoothing, from vendor_patches/30-smooth-eased-
// scrolling.patch)? Pinned edfe234 does NOT, so all smooth-scroll code below is
// SFINAE-gated to a hard no-op there — hanabi still compiles against the pinned
// submodule. Once Gabe lands the patch + bumps the pointer, these activate with
// zero call-site changes. (Same "compile against pinned" invariant as the other
// vendor patches, but this one needs new FIELDS, hence the detection.)
template <typename SV, typename = void>
struct has_smooth_scroll : std::false_type {};
template <typename SV>
struct has_smooth_scroll<SV, std::void_t<decltype(std::declval<SV>().scroll_target)>>
    : std::true_type {};

// Set scroll_smoothing on a scroll view IF the field exists (else no-op).
template <typename SV>
inline void set_scroll_smoothing(SV& sv, float f) {
    if constexpr (has_smooth_scroll<SV>::value) sv.scroll_smoothing = f;
}
// Keep scroll_target in sync with a programmatic scroll_offset write (else no-op).
template <typename SV>
inline void sync_scroll_target(SV& sv) {
    if constexpr (has_smooth_scroll<SV>::value) sv.scroll_target = sv.scroll_offset;
}
template <typename SV>
inline void set_scroll_target_y(SV& sv, float y) {
    if constexpr (has_smooth_scroll<SV>::value) sv.scroll_target.y = y;
}

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
        auto& sv = e.get<afterhours::ui::HasScrollView>();
        sv.invert_scroll = should_invert_scroll();
        // Smooth (native-feeling) scrolling: the wheel writes scroll_target and
        // scroll_offset eases toward it each frame. Raw afterhours added the
        // wheel delta straight to the rendered offset, so scrolling was stepped
        // and janky vs macOS momentum scroll. 0.28/frame @ ~display-rate gives a
        // quick, smooth glide that still settles fast. HANABI_SCROLL_SMOOTH
        // overrides (1 = legacy instant; 0.1..0.9 = smoothing factor).
        float smooth = 0.28f;
        if (const char* env = std::getenv("HANABI_SCROLL_SMOOTH")) {
            float v = static_cast<float>(std::atof(env));
            if (v > 0.0f && v <= 1.0f) smooth = v;
        }
        set_scroll_smoothing(sv, smooth);  // no-op vs pinned afterhours
    }
}

}  // namespace hanabi
