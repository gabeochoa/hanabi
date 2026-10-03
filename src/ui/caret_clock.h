#pragma once

// The caret blinks on the wall clock, and the frame loop wakes only when it
// toggles (docs/perf/IDLE.md, 2026-10-03).
//
// afterhours' widgets advance their own blink: a text AREA (the composer) by
// a fixed 0.016 s per drawn frame, a text INPUT by the frame's dt. With a
// retained-frame loop neither is time: at the 10 fps a focused field held,
// the composer's 0.53 s half-cycle took 33 frames -- 3.3 s per phase -- and a
// focused field cost a full frame ten times a second to show it.
//
// So, before each drawn frame, every focused field's timer is SET to where a
// wall-clock blink is now, minus the step the widget is about to add, and
// the next toggle is reported as a deadline. A widget reset (typing resets
// the blink to visible) is noticed as a timer that is not what this wrote
// plus one step, and re-anchors the clock there.
//
// vendor/afterhours is unchanged: this writes the widget's public state.

#include <chrono>
#include <cmath>
#include <unordered_map>

namespace hanabi::caret {

struct Anchor {
    double origin = 0.0;    // wall seconds at which the current blink cycle began (visible)
    float written = -1.0f;  // what this wrote last frame (-1: never)
};

struct Set {
    float timer = 0.0f;     // the value to write
    double toggleIn = 0.0;  // seconds until the caret next shows or hides
};

// What to write into a field's blink timer at wall time `now`, given what
// the widget holds (`held`), the step it adds when drawn (`step`), and its
// half-cycle (`rate`). Updates the anchor.
inline Set place(Anchor& a, double now, float held, float step, float rate) {
    const double period = 2.0 * rate;
    if (period <= 0.0) return {held, 1e9};
    const bool drawnSince = a.written >= 0.0f && std::abs(held - (a.written + step)) < 1e-4f;
    const bool wrapped = a.written >= 0.0f && a.written + step >= static_cast<float>(period) && held == 0.0f;
    const bool notDrawn = a.written >= 0.0f && std::abs(held - a.written) < 1e-6f;
    if (!(drawnSince || wrapped || notDrawn)) a.origin = now - held;  // first sight, or the widget reset it
    double phase = std::fmod(now - a.origin, period);
    if (phase < 0.0) phase += period;
    Set out;
    double w = phase - step;
    if (w < 0.0) w += period;
    out.timer = static_cast<float>(w);
    out.toggleIn = phase < rate ? rate - phase : period - phase;
    if (out.toggleIn < 0.001) out.toggleIn = 0.001;
    a.written = out.timer;
    return out;
}

inline double wall_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The soonest toggle across focused fields, as a wall time (0 = none).
inline double& next_toggle_at() {
    static double t = 0.0;
    return t;
}

// The frame loop's question: is a caret due to change?
inline bool due(double now) {
    const double t = next_toggle_at();
    return t > 0.0 && now + 0.002 >= t;
}

}  // namespace hanabi::caret
