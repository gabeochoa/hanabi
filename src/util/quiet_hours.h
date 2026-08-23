#pragma once

// ---------------------------------------------------------------------------
// Quiet hours.
//
// A window in the day when a notification must not fire. The only subtlety is
// that the window usually crosses midnight -- 10pm to 8am is not a range you
// can test with a single pair of comparisons -- so the check lives here as one
// pure function with tests, rather than inline in the frame loop where it
// would be untestable and wrong at 3am.
//
// Both ends are minutes since local midnight. The window is half-open: it
// starts AT start and ends BEFORE end, so 22:00-08:00 is quiet at 22:00 and
// noisy again at 08:00. A window whose ends are equal is empty, not all-day --
// "from 8am to 8am" reads as a mistake, and silencing a user's whole day by
// accident is the worse failure.
// ---------------------------------------------------------------------------

namespace hanabi::quiet {

inline bool in_window(int nowMinutes, int startMinutes, int endMinutes) {
    if (startMinutes == endMinutes) return false;
    if (startMinutes < endMinutes)
        return nowMinutes >= startMinutes && nowMinutes < endMinutes;
    // Crosses midnight: quiet from start to the end of the day, and again from
    // the start of the day to end.
    return nowMinutes >= startMinutes || nowMinutes < endMinutes;
}

}  // namespace hanabi::quiet
