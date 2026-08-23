#pragma once

#include <cstdint>
#include <ctime>

namespace capture_clock {

// The reading a duration on screen is measured against.
//
// A headless capture stamps its demo state with one clock reading and renders
// that state with another: std::time() is the wall clock, so a run that crosses
// a second boundary between the two photographs a different number than the one
// it set up. That is how 26_thinking_dark came to show "32s" on most runs and
// "33s" on some — the elapsed timer is (render now - streamStartedAt), and the
// two reads are milliseconds to seconds apart. Pinning both to one instant is
// what makes a duration reproducible; a threshold wide enough to swallow the
// digit would also swallow a real regression in the same row.
//
// Only the SCREENSHOT path freezes. The app, the scripted UI tests and every
// real run read the wall clock exactly as before.
//
// Deliberately NOT wired into the relative ages in the sidebar and transcript.
// Their datum is seeded by the mock from its own std::time() call, which
// happens after the freeze, so pinning the display side alone would make every
// age a fraction of a second SHORT of its true value — enough to round a datum
// seeded at exactly now-3h down to "2h".
inline int64_t g_frozen_at = 0;

inline void freeze() { g_frozen_at = static_cast<int64_t>(std::time(nullptr)); }

inline bool frozen() { return g_frozen_at != 0; }

inline int64_t now() {
    return g_frozen_at != 0 ? g_frozen_at
                            : static_cast<int64_t>(std::time(nullptr));
}

// Where in its cycle a time-driven animation is drawn. A capture pins it, so a
// widget that breathes (the thinking dot eases its radius with a sine of the
// graphics clock) is photographed at one point in the cycle rather than
// wherever the run happened to land — that is worth a few pixels of difference
// between two runs of the same screen. Zero puts the sine at its midpoint,
// which is the pulse's average size rather than either extreme. Outside a
// capture the caller's live reading passes straight through.
inline constexpr double kFrozenAnimPhase = 0.0;

inline double anim_time(double live_seconds) {
    return g_frozen_at != 0 ? kFrozenAnimPhase : live_seconds;
}

}  // namespace capture_clock
