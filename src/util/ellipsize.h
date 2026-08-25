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
    if (measure(text.c_str()) <= maxW) return text;
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

    // Binary search the largest boundary that fits. The full string is known
    // not to fit (checked above), so hi is a valid upper bound.
    size_t lo = 0;
    size_t hi = text.size();
    while (hi - lo > 1) {
        const size_t mid = utf8_floor(text, lo + (hi - lo) / 2);
        if (mid == lo) break;  // no code point boundary strictly between them
        if (fits(mid)) lo = mid; else hi = mid;
    }

    // lo is either 0 or a boundary that measured as fitting, so no correction
    // pass is needed to guarantee the result fits -- see the header note.
    return text.substr(0, lo) + kEllipsis;
}

}  // namespace hanabi::text
