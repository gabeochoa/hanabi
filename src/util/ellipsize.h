#pragma once

// ---------------------------------------------------------------------------
// Ellipsize a string to a pixel width, in O(log n) measurements.
//
// WHY THIS IS ITS OWN FILE. The sidebar had this inline as a backward linear
// scan: drop one code point, substr, measure, repeat. Measuring a prefix is
// itself linear in the prefix -- fontstash walks the glyphs and
// stbtt_GetGlyphKernAdvance binary-searches the kern table per pair -- so
// ellipsizing one title was quadratic in its length with a malloc and a free
// per probe, and it ran for every rendered row of every frame. At a
// 2000-session catalog it was 34% of the main thread's samples, long after the
// rows themselves were capped at 38.
//
// Fixing that in place would have left the fix untestable, because the sidebar
// cannot be constructed without graphics. Here the metric is a CALLABLE, so a
// headless test can hand it a ruler it controls -- including a deliberately
// unkind one -- and check the answer against a straightforward linear
// reference. That is what makes "the same string, faster" a claim with
// evidence behind it rather than a claim about the cases someone looked at.
//
// ON KERNING, AND WHAT THIS ACTUALLY GUARANTEES. Prefix width is monotonic in
// prefix length for any font anyone ships, but kerning means it is not
// monotonic BY CONSTRUCTION: a pair can pull the next glyph left by more than
// that glyph's own advance, so a longer prefix can be narrower than a shorter
// one. A binary search assumes monotonicity. So, precisely:
//
//   * For a MONOTONIC metric this returns exactly what the linear scan
//     returned -- the longest prefix that fits. Checked differentially at
//     every width in tests/unit/test_ellipsize.cpp.
//   * For a NON-MONOTONIC one it still returns a prefix, still ends on a code
//     point boundary, and still FITS -- it can just be shorter than the
//     longest one that would have. Recovering the true maximum there needs the
//     linear scan back, because a dip can hide an arbitrarily long fitting
//     prefix behind a non-fitting one, and paying O(n) measurements a row for
//     a case no shipped font produces is the trade this change exists to
//     refuse. The visible consequence if it ever happened is a title
//     ellipsized one glyph early.
//
// "Still fits" is structural, not a safety net bolted on: `lo` is only ever
// assigned a boundary that measured as fitting, so whatever the metric does,
// the prefix that comes back is one that was checked. A first draft of this
// carried a walk-back loop to enforce that afterwards; it could be deleted
// with every test still green, which is what dead code looks like.
//
// WHERE THE SEARCH STARTS, AND WHY THAT IS MOST OF THE COST NOW. A bisection
// that starts in the middle of the string spends its first few probes finding
// out something the caller already knows: the full string's width was just
// measured, and text is roughly as wide as it is long, so the answer is near
// `len * budget / full` rather than near `len / 2`. Seeding there and then
// GALLOPING outward in doubling steps brackets the cut point in about two
// probes for real prose, and the bisection that follows has a handful of
// bytes left to search instead of the whole string.
//
// This changes no answer: the seed is only ever used to narrow [lo, hi], and
// every boundary assigned to `lo` was measured as fitting exactly as before.
// tests/unit/test_ellipsize.cpp checks that the string returned is still the
// linear scan's, at every width in a quarter-unit sweep under three rulers,
// and separately counts the probes so the saving is a number and not a claim.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>

#include "prof.h"

namespace hanabi::text {

// The UTF-8 code point boundary at or below `n`.
inline size_t utf8_floor(const std::string& s, size_t n) {
    if (n > s.size()) n = s.size();
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return n;
}

// The UTF-8 code point boundary strictly above `n` (clamped to the end).
inline size_t utf8_next(const std::string& s, size_t n) {
    if (n >= s.size()) return s.size();
    ++n;
    while (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
        ++n;
    return n;
}

inline constexpr const char* kEllipsis = "\xe2\x80\xa6";

// `measure(const char*) -> float` is the width of a NUL-terminated run in the
// same units as `maxW`.
//
// Returns `text` untouched when it already fits, otherwise the longest prefix
// that fits WITH the ellipsis appended, plus the ellipsis. Returns a bare
// ellipsis when not even one code point fits, and an empty string for a
// non-positive width. "Longest" holds for a monotonic metric; see the kerning
// note above for what a non-monotonic one gets instead.
template <class Measure>
std::string fit_to_width(const std::string& text, float maxW,
                         Measure&& measure) {
    if (maxW <= 0.0f) return std::string();
    hanabi::prof::tick("text.fit_call");
    hanabi::prof::tick("text.fit_probe");
    const float full = measure(text.c_str());
    if (full <= maxW) return text;
    hanabi::prof::tick("text.fit_probe");
    const float ell = measure(kEllipsis);

    // One scratch buffer for every probe. resize() DOWN never reallocates and
    // c_str() re-terminates, so the probes cost no allocation at all. Static
    // because this is called for every row of every frame and the buffer's
    // capacity is the whole point; nothing here recurses or threads.
    static std::string probe;
    probe.assign(text);
    const auto fits = [&](size_t n) {
        hanabi::prof::tick("text.fit_probe");
        probe.resize(n);
        const bool ok = measure(probe.c_str()) + ell <= maxW;
        probe.assign(text);
        return ok;
    };

    // The full string is known not to fit (checked above), and adding the
    // ellipsis only widens it, so hi is a valid upper bound.
    size_t lo = 0;
    size_t hi = text.size();

    // Seed from the width already in hand, then gallop to bracket. `full` is
    // the whole string's width, so budget/full is the share of the string
    // that fits, and length times that share is a byte estimate. It is only a
    // seed: everything it does is narrow [lo, hi], and both ends stay honest.
    const float budget = maxW - ell;
    if (budget > 0.0f && full > 0.0f) {
        const double share = static_cast<double>(budget) / static_cast<double>(full);
        const size_t seed =
            utf8_floor(text, static_cast<size_t>(
                                 static_cast<double>(text.size()) * share));
        if (seed > 0 && seed < hi) {
            if (fits(seed)) {
                lo = seed;
                for (size_t step = 1; step < text.size(); step *= 2) {
                    const size_t n = utf8_floor(text, seed + step);
                    if (n >= hi) break;
                    if (n <= lo) continue;  // step landed inside a code point
                    if (fits(n)) {
                        lo = n;
                    } else {
                        hi = n;
                        break;
                    }
                }
            } else {
                hi = seed;
                for (size_t step = 1; step < text.size(); step *= 2) {
                    const size_t n =
                        utf8_floor(text, seed > step ? seed - step : 0);
                    if (n <= lo) break;
                    if (fits(n)) {
                        lo = n;
                        break;
                    }
                    hi = n;
                }
            }
        }
    }

    // Binary search the largest boundary that fits, in whatever is left.
    //
    // The midpoint is floored to a code point boundary, and flooring can land
    // back on `lo` -- inside a multi-byte character whose start is `lo`. The
    // first version of this treated that as "no boundary between them" and
    // stopped, which is only true when the NEXT boundary is already at or past
    // `hi`. It is not the same thing, and the difference is a title cut one
    // glyph short in front of any multi-byte character: caught by the
    // differential sweep the moment the seed above narrowed the range enough
    // to reach the case (utf8/proportional, "reconciling —", maxW=67).
    while (hi - lo > 1) {
        size_t mid = utf8_floor(text, lo + (hi - lo) / 2);
        if (mid <= lo) mid = utf8_next(text, lo);
        if (mid >= hi) break;  // no code point boundary strictly between them
        if (fits(mid)) lo = mid; else hi = mid;
    }

    // lo is either 0 or a boundary that measured as fitting, so no correction
    // pass is needed to guarantee the result fits -- see the header note.
    return text.substr(0, lo) + kEllipsis;
}

}  // namespace hanabi::text
