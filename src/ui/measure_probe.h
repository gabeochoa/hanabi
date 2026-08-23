#pragma once

// TEMPORARY measure-vs-draw probe. Off unless HANABI_PROBE_MEASURE=1.
//
// The transcript measures every item twice: bubble_height() sizes the
// virtualization spacers, and the render walk draws the item. Nothing in
// afterhours reports the height an element actually came out at, so the two
// are kept in step by hand and drift silently — a wrong spacer only shows up
// as scroll positions that creep as you page down. This probe compares them:
//
//   * "richbody"  — render_rich_body's accumulated walk vs rich_body_h, the
//                   pair the two functions are supposed to agree on;
//   * "turn#<i>"  — bubble_height(i) vs the height the LAYOUT ENGINE resolved
//                   for that turn's element last frame, which is the only
//                   independent witness available.
//
// It prints every drift and, at exit, how many comparisons it made — a probe
// that never compared anything proves nothing.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <unordered_map>

namespace hanabi::mprobe {

inline bool on() {
    static const bool v = std::getenv("HANABI_PROBE_MEASURE") != nullptr;
    return v;
}

struct State {
    std::unordered_map<std::string, float> expected;
    long checks = 0;
    long drifts = 0;
    ~State() {
        if (checks == 0 && drifts == 0) return;
        std::fprintf(stderr, "[mprobe] %ld comparisons, %ld drifts\n", checks,
                     drifts);
    }
};

inline State& state() {
    static State s;
    return s;
}

// The measure pass records what it told the virtualizer.
inline void expect(const std::string& key, float h) {
    if (!on()) return;
    state().expected[key] = h;
}

// A drawn height arrives; compare it with what was promised.
inline void observe(const std::string& key, float drawn, float tol = 0.5f) {
    if (!on()) return;
    auto it = state().expected.find(key);
    if (it == state().expected.end()) return;
    state().checks++;
    const float d = drawn - it->second;
    if (std::fabs(d) > tol) {
        state().drifts++;
        std::fprintf(stderr,
                     "[mprobe] DRIFT %s: measured %.2f, drew %.2f (%+.2f)\n",
                     key.c_str(), it->second, drawn, d);
    }
}

// Both numbers in hand at once (the rich-body walk).
inline void compare(const std::string& key, float measured, float drawn,
                    float tol = 0.5f) {
    if (!on()) return;
    state().checks++;
    const float d = drawn - measured;
    if (std::fabs(d) > tol) {
        state().drifts++;
        std::fprintf(stderr,
                     "[mprobe] DRIFT %s: measured %.2f, drew %.2f (%+.2f)\n",
                     key.c_str(), measured, drawn, d);
    }
}

}  // namespace hanabi::mprobe
