#pragma once

// ---------------------------------------------------------------------------
// The generation counter every text memo has to watch.
//
// THE BUG THIS EXISTS FOR. Hanabi can replace the face and emphasis mapping
// behind afterhours' `UIComponent::DEFAULT_FONT` through the font-system
// settings. The handle does not change. The name does not change. The size does
// not change. Every measurement of every string can now be different, and every
// cache keyed by (text, font NAME, size) still holds the old numbers and is
// certain they are current — there is no input to any of those keys that moved.
//
// That is four caches in this app: the transcript's per-message render memo,
// its hug memo, the sidebar's ellipsis memo and the line-count memo, plus
// afterhours' own TextMeasureCache, which is keyed the same way and has no
// invalidation hook beyond `clear()` (filed as gap #190). None of them was
// invalidated, and the app has shipped that way.
//
// WHAT IT LOOKS LIKE, HONESTLY. Not dramatic, which is exactly why it
// survived: a stale line count is a box an line too short or too tall, a
// stale hug is a bubble a few pixels off its text, a stale ellipsis is a
// title cut for the wrong face. On the mock fixtures the difference between
// switching the font live and starting up with it is below what
// scripts/compare.py can see -- 0.02% of the frame, and every one of those
// pixels turned out to be the relative-time label ticking over rather than
// any measurement at all. So this is not a fix for a reported symptom. It is
// a fix for a cache that is WRONG, found by asking what invalidates it and
// getting no answer, and it matters more the more measurement is memoized --
// which is the whole of this branch.
//
// The mechanism is a counter rather than a clear-them-all call, because a
// clear-them-all call is a list of caches somebody has to remember to add to.
// A memo that reads this counter cannot be forgotten: it drops itself.
// ---------------------------------------------------------------------------

namespace hanabi::text {

// Bumped whenever the glyphs behind a font NAME change. Not a font id: the id
// is what stayed the same.
inline unsigned& font_epoch() {
    static unsigned e = 0;
    return e;
}

inline void bump_font_epoch() { ++font_epoch(); }

}  // namespace hanabi::text
