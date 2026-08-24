#pragma once

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

}  // namespace hanabi::ui::focus_visible
