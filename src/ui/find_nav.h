#pragma once

// ---------------------------------------------------------------------------
// Moving between find matches: what a chord means, and where a step lands.
//
// Two things move the current match — the find bar's chevrons and the Cmd+G /
// Cmd+Shift+G chords — and they must land on the same match or the tally lies
// about which band is current. So the wrap arithmetic lives here once instead
// of at each site, and the chord's meaning is a value rather than a branch
// buried in a system: a scripted test cannot press Cmd (afterhours_gaps.md
// #49), so a table that CAN be asserted is the only part of the binding a test
// can hold.
// ---------------------------------------------------------------------------

namespace hanabi::find_nav {

enum class Step { None, Next, Prev };

// Cmd+G is next, Cmd+Shift+G is previous. The Cmd is what makes it a command:
// a bare G is a letter on its way into the find field.
constexpr Step chord(bool cmd, bool shift, bool g_pressed) {
    if (!cmd || !g_pressed) return Step::None;
    return shift ? Step::Prev : Step::Next;
}

// Where `index` goes when `s` is applied over `count` matches. Wraps at both
// ends — the last match's next is the first, which is what every find bar on
// this platform does. An index that is already out of range (the query changed
// and there are fewer matches now) restarts at the first match.
constexpr int advance(int index, int count, Step s) {
    if (count <= 0 || s == Step::None) return 0;
    if (index < 0 || index >= count) return 0;
    int next = index + (s == Step::Prev ? -1 : 1);
    if (next < 0) next = count - 1;
    if (next >= count) next = 0;
    return next;
}

}  // namespace hanabi::find_nav
