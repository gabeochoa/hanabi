#pragma once

#include <afterhours/src/plugins/color.h>

// ---------------------------------------------------------------------------
// :focus-visible, by hand.
//
// afterhours parks focus on the first focusable widget of the frame
// (context.h try_to_grab) and ComputeVisualFocusId paints the ring on whatever
// holds focus, regardless of how it got there. There is no notion of a focus
// ring that appears only once the keyboard has been used — the CSS
// :focus-visible rule every desktop toolkit implements — so an app that opens
// with a focusable row at the top of a list opens with a blue box around that
// row, in every screenshot, before the user has touched anything.
//
// FocusSource is not a way out. It is reset to Grab at the top of every frame
// (BeginUIContextManager), so by the time anything renders it reports Grab for
// a focus that was moved by Tab three frames ago. It answers "who claimed
// focus THIS frame", not "how did the focused element come to be focused".
//
// So the rule lives here: the ring is off until a navigation key is pressed,
// and off again after a pointer press — the same heuristic a browser uses. The
// system that applies it writes theme.focus_ring_thickness, which is the one
// knob afterhours exposes for this (theme.h: "Set focus_ring_thickness to 0 to
// disable the ring").
//
// afterhours_gaps.md #83.
// ---------------------------------------------------------------------------

namespace hanabi::ui::focus_visible {

// The ring's own hairline thickness when it IS shown. Matches the value
// preload.cpp installs as the app default (one flush hairline, gap #46).
inline constexpr float kRingThickness = 1.0f;

// Whether the keyboard has been used to navigate since the last pointer press.
inline bool& armed() {
    static bool value = false;
    return value;
}

// A navigation keystroke arms the ring; a pointer press disarms it. Both in
// one call so the order is fixed in one place: a frame carrying both is a
// click, because a click is what the user did last.
inline void observe(bool navKeyPressed, bool pointerPressed) {
    if (navKeyPressed) armed() = true;
    if (pointerPressed) armed() = false;
}

// The thickness the renderer should use this frame.
inline float ring_thickness() { return armed() ? kRingThickness : 0.0f; }

// ---------------------------------------------------------------------------
// The ring's COLOUR, which is not a colour choice — it is the only way to
// reach the ring's contrast edges.
//
// rendering.h's focus_ring_for emits three rounded outlines for a ring of any
// thickness: `outer_contrast` one pixel outside the stack, `inner_contrast`
// one pixel inside it, and the coloured ring between them. Only the coloured
// one is gated on focus_ring_thickness; the two contrast edges are emitted
// whenever a ring is emitted at all, and nothing in the theme turns them off.
//
// Their colour is not a theme value either. It is derived from the RING:
//
//     contrast = luminance(ring) > 0.5 ? black@180 : white@180
//
// which is the right rule for a ring sitting on a fill the same colour as
// itself, and the wrong one for a ring sitting on a dark app. hanabi's dark
// accent {90,128,255} has a WCAG luminance of 0.248, so the two edges came out
// WHITE at 70% opacity over a {23,23,35} backdrop — measurably brighter than
// the blue they were meant to protect. The "1px hairline" was a three-pixel
// white-blue-white band, and its three outlines have three different corner
// radii, so each corner splayed into a bracket.
//
// The one lever the app has is which side of that 0.5 the ring lands on, so
// this puts the ring on the same side as the BACKDROP: light ring on a dark
// app, so the edges resolve to black and sink into it; dark ring on a light
// app, so they resolve to white and do the same. The edges are still drawn —
// they are simply drawn in the backdrop's own direction, and what is left is
// one line of the ring's colour.
//
// afterhours_gaps.md #265.
// ---------------------------------------------------------------------------

// The threshold rendering.h compares against, and the margin we keep off it.
// The threshold cuts straight through the blues an accent would plausibly use
// — {140,190,255} reads 0.496 and {150,195,255} reads 0.527 — so landing near
// it is landing on a coin toss, and a custom accent swatch replaces the ring's
// RGB wholesale (theme::apply_custom).
inline constexpr float kContrastThreshold = 0.5f;
inline constexpr float kContrastMargin = 0.03f;

// Nudge `base` toward white (dark backdrop) or black (light backdrop) until
// afterhours' own luminance function — the one that will make the decision —
// puts it on the backdrop's side of the threshold. A colour already on that
// side is returned untouched, so a palette that was correct keeps its exact
// pixels.
inline afterhours::Color ring_color(afterhours::Color base,
                                    bool dark_backdrop) {
    namespace c = afterhours::colors;
    const auto satisfied = [dark_backdrop](const afterhours::Color& col) {
        const float lum = c::luminance(col);
        return dark_backdrop ? lum > kContrastThreshold + kContrastMargin
                             : lum < kContrastThreshold - kContrastMargin;
    };
    if (satisfied(base)) return base;

    const unsigned char end = dark_backdrop ? 255 : 0;
    const auto step = [](unsigned char from, unsigned char to, float t) {
        return static_cast<unsigned char>(
            static_cast<float>(from) +
            (static_cast<float>(to) - static_cast<float>(from)) * t + 0.5f);
    };
    // A 32-step ramp rather than a solve: the answer only has to clear the
    // threshold, and a fixed ramp gives the same colour on every machine.
    for (int i = 1; i <= 32; ++i) {
        const float t = static_cast<float>(i) / 32.0f;
        const afterhours::Color mixed{step(base.r, end, t),
                                      step(base.g, end, t),
                                      step(base.b, end, t), base.a};
        if (satisfied(mixed)) return mixed;
    }
    return afterhours::Color{end, end, end, base.a};
}

}  // namespace hanabi::ui::focus_visible
