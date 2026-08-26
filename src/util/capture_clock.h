#pragma once

#include <cstdint>
#include <cstdlib>
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
// HANABI_CAPTURE_EPOCH=<unix epoch> goes further: it pins the whole notion of
// now for the run, DISPLAY side included, and scripts/screens.sh sets it
// alongside HANABI_MOCK_NOW so the fixture datum and the rendering of it are
// literally the same integer. That is what makes a transcript reproducible.
// Freezing the display alone was deliberately avoided before, and rightly: the
// mock seeds its stamps from its own std::time() call AFTER the freeze, so a
// pinned display against a live datum makes every age a fraction of a second
// short — enough to round now-3h down to "2h". Pinning BOTH to one constant
// removes the skew instead of splitting it.
//
// Unpinned, display_now() is the wall clock, so nothing outside the screenshot
// harness changes: scripts/soak.sh and `make stress` set HANABI_MOCK_NOW and
// not this, and their rendered ages stay exactly what they were.
inline int64_t g_frozen_at = 0;
inline int64_t g_pinned_at = 0;

inline int64_t pinned_epoch() {
    const char* v = std::getenv("HANABI_CAPTURE_EPOCH");
    if (v == nullptr || *v == '\0') return 0;
    const long long parsed = std::atoll(v);
    return parsed > 0 ? static_cast<int64_t>(parsed) : 0;
}

inline void freeze() {
    g_pinned_at = pinned_epoch();
    g_frozen_at = g_pinned_at != 0 ? g_pinned_at
                                   : static_cast<int64_t>(std::time(nullptr));
}

inline bool frozen() { return g_frozen_at != 0; }

inline int64_t now() {
    return g_frozen_at != 0 ? g_frozen_at
                            : static_cast<int64_t>(std::time(nullptr));
}

// The reference a RELATIVE age or a date divider is rendered against, as
// opposed to now(), which a duration inside one capture is measured against.
// Pinned only by HANABI_CAPTURE_EPOCH; otherwise the live wall clock, which is
// what every non-capture caller has always read.
inline int64_t display_now() {
    return g_pinned_at != 0 ? g_pinned_at
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
