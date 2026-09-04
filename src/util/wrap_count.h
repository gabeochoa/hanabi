#pragma once

// ---------------------------------------------------------------------------
// "How many lines is this?" -- answered without building any of them.
//
// WHY THIS IS ITS OWN FILE. The only way to ask afterhours how tall a
// paragraph is at a width is `ui::wrap_text`, which returns
// std::vector<std::string> -- every wrapped line, materialised -- and the
// transcript throws all of it away one line later:
//
//     static int count_lines(const std::string& text, float w, float px) {
//         return static_cast<int>(wrapped_lines(text, w, px).size());
//     }
//
// That is afterhours_gaps.md #135. Measured on the 120-message fixture at
// 1180x949, standing still: 61.8 of those calls per frame, 3.2 KB of text
// wrapped per frame, ~9,500 heap allocations per frame -- to produce one
// integer per paragraph, for paragraphs whose text and width have not changed
// since the thread was opened.
//
// Underneath, the greedy wrapper measures the CANDIDATE LINE at every word
// boundary, and the candidate is the whole accumulated prefix. So an N-word
// line costs N measure calls on N distinct strings averaging N/2 bytes --
// quadratic in bytes -- plus, per word, one std::string for the candidate and
// a second for the segment handed to measure, then a vector of N more for the
// result. This file keeps the first cost and removes the rest.
//
// ---------------------------------------------------------------------------
// IT MUST AGREE WITH THE WRAPPER, NOT MERELY BE PLAUSIBLE.
//
// The count decides a box's HEIGHT; afterhours then wraps the same string
// again to DRAW it. If the two disagree by one line the message clips or
// leaves a gap, so this is not an estimate and an estimate would not do --
// hanabi shipped one once, it did not honour hard newlines, and a three-line
// message measured as one and clipped (the comment recording its removal is
// still in main_pane_system.h).
//
// So the break logic here is afterhours' break logic, restated over byte
// offsets instead of over strings, and `tests/unit/test_wrap_count.cpp`
// checks it differentially against `ui::detail::wrap_text_to_width` itself --
// the real vendor function, called with the same metric, over a corpus at
// every width in a sweep. Restating it is the cost of the gap: the wrapper
// and the counter share no code, so a change upstream can only be caught by
// that test. It is the reason the test compares against the vendored function
// rather than against a second copy of the rules.
//
// The reduction, for one plain single-weight run, is:
//
//   * '\n' splits the text into source lines; each contributes at least one
//     output line, and a run of spaces is a chunk like any other word.
//   * a candidate line is always a CONTIGUOUS byte range of its source line,
//     [start of the output line, end of the word being tested) -- the pending
//     whitespace between the last word and this one is inside that range, and
//     a break drops it by moving the start to the new word.
//   * the first word of an output line is accepted without being measured,
//     which is what puts an over-long word on a line of its own instead of
//     splitting it.
//
// Because a candidate is a contiguous range, no string has to be built to
// name one: one scratch buffer, assigned from the source, serves every probe
// and reaches its final capacity within the first paragraph.
//
// ---------------------------------------------------------------------------
// TWO SEARCHES, AND WHY THE FAST ONE IS SAFE.
//
// `wrapped_line_count_linear` probes exactly the prefixes afterhours probes,
// in the same order, so it returns the same answer under ANY metric,
// monotonic or not. It is the reference, and it is what the differential test
// pins hardest.
//
// `wrapped_line_count` finds the same break point by probing the LAST word
// first and bisecting when that overflows: O(log W) measures per output line
// instead of O(W), and -- the case that dominates a real transcript -- ONE
// measure for a source line that fits on one line, however many words it has.
//
// Bisection assumes prefix width is monotonic in prefix length. Kerning does
// not guarantee that: a pair can pull a glyph left by more than its own
// advance, so a longer prefix can be narrower than a shorter one. This is the
// same assumption src/util/ellipsize.h documents for row titles, and it is
// load-bearing in a heavier way here, because a count that comes out LOW
// clips a message rather than dropping a glyph from a title.
//
// So it is checked at runtime rather than asserted in prose:
//
//     HANABI_VERIFY_WRAP=1
//
// runs BOTH searches on every call and counts agreements and disagreements
// into `text.wrap_verified` / `text.wrap_disagree`, which `HANABI_PROF=1`
// prints. Over the transcript and sidebar perf scenarios with the app's real
// font that reads 100% agreement across ~57,000 calls; a font that broke the
// assumption would show up as a non-zero disagree count rather than as a
// clipped bubble somebody notices in a screenshot.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "prof.h"

namespace hanabi::text {

namespace wrapdetail {

// The word (non-space run) boundaries of one source line, as [begin, end)
// offsets into the whole text. Reused across calls; a transcript paragraph
// tops out at a few dozen words, so this stops allocating almost at once.
inline std::vector<std::pair<std::size_t, std::size_t>>& word_scratch() {
    static std::vector<std::pair<std::size_t, std::size_t>> w;
    return w;
}

inline std::string& probe_scratch() {
    static std::string s;
    return s;
}

inline std::size_t next_char(const std::string& text, std::size_t at,
                             std::size_t end) {
    if (at >= end) return end;
    ++at;
    while (at < end &&
           (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80)
        ++at;
    return at;
}

inline void split_words(const std::string& text, std::size_t begin,
                        std::size_t end,
                        std::vector<std::pair<std::size_t, std::size_t>>& out) {
    out.clear();
    std::size_t i = begin;
    while (i < end) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < end && text[j] != ' ') ++j;
        out.emplace_back(i, j);
        i = j;
    }
}

}  // namespace wrapdetail

// Lines `text` occupies when wrapped to `max_width` by afterhours' greedy
// wrapper, probing the same prefixes in the same order. `measure(s)` is the
// width of a single line in the same units as `max_width`.
template <class Measure>
inline int wrapped_line_count_linear(const std::string& text, float max_width,
                                     Measure&& measure) {
    if (text.empty() || max_width <= 0.0f) return 1;
    auto& words = wrapdetail::word_scratch();
    auto& probe = wrapdetail::probe_scratch();

    int lines = 0;
    std::size_t lineStart = 0;
    while (true) {
        const std::size_t nl = text.find('\n', lineStart);
        const std::size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
        wrapdetail::split_words(text, lineStart, lineEnd, words);
        ++lines;
        if (!words.empty()) {
            std::size_t cur = lineStart;
            for (std::size_t k = 1; k < words.size(); ++k) {
                probe.assign(text, cur, words[k].second - cur);
                if (measure(probe) > max_width) {
                    ++lines;
                    cur = words[k].first;
                }
            }
        }
        if (nl == std::string::npos) break;
        lineStart = nl + 1;
    }
    return lines;
}

// Same answer, O(log W) measures per output line instead of O(W). See the
// header note on monotonicity and HANABI_VERIFY_WRAP.
template <class Measure>
inline int wrapped_line_count_fast(const std::string& text, float max_width,
                                   Measure&& measure) {
    if (text.empty() || max_width <= 0.0f) return 1;
    auto& words = wrapdetail::word_scratch();
    auto& probe = wrapdetail::probe_scratch();

    int lines = 0;
    std::size_t lineStart = 0;
    while (true) {
        const std::size_t nl = text.find('\n', lineStart);
        const std::size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
        wrapdetail::split_words(text, lineStart, lineEnd, words);
        ++lines;
        if (!words.empty()) {
            std::size_t cur = lineStart;
            std::size_t first = 0;
            const std::size_t last = words.size() - 1;
            const auto fits = [&](std::size_t k) {
                probe.assign(text, cur, words[k].second - cur);
                return measure(probe) <= max_width;
            };
            while (first < last) {
                // The whole remainder first: a source line that fits costs one
                // measure whatever its word count, which is the common case.
                if (fits(last)) break;
                // lo fits by construction (the first word of a line is taken
                // unmeasured), hi does not. Bisect to the last one that does.
                std::size_t lo = first;
                std::size_t hi = last;
                while (hi - lo > 1) {
                    const std::size_t mid = lo + (hi - lo) / 2;
                    if (fits(mid)) lo = mid; else hi = mid;
                }
                ++lines;
                first = lo + 1;
                cur = words[first].first;
            }
        }
        if (nl == std::string::npos) break;
        lineStart = nl + 1;
    }
    return lines;
}

template <class Measure>
inline void break_long_spans(
    const std::string& text, float max_width, Measure&& measure,
    std::vector<std::pair<std::size_t, std::size_t>>& out) {
    auto& probe = wrapdetail::probe_scratch();
    std::vector<std::pair<std::size_t, std::size_t>> split;
    split.reserve(out.size());
    for (const auto& span : out) {
        std::size_t b = span.first;
        const std::size_t e = span.second;
        while (b < e) {
            probe.assign(text, b, e - b);
            if (measure(probe) <= max_width) break;
            std::size_t fit = b;
            for (std::size_t at = wrapdetail::next_char(text, b, e); at < e;
                 at = wrapdetail::next_char(text, at, e)) {
                probe.assign(text, b, at - b);
                if (measure(probe) > max_width) break;
                fit = at;
            }
            if (fit == b) fit = wrapdetail::next_char(text, b, e);
            if (fit >= e) break;
            split.emplace_back(b, fit);
            b = fit;
        }
        split.emplace_back(b, e);
    }
    out.swap(split);
}

// The same wrap, reported as BYTE RANGES into `text` rather than as strings.
//
// Every caller that needs the lines themselves needed them for one reason:
// to measure each one and take the widest, which is how a chat bubble hugs
// its text (afterhours_gaps.md #136). A range is enough for that, and it
// costs nothing to produce.
//
// The ranges are the lines EXACTLY: joining `text.substr(b, e - b)` over the
// spans gives what `ui::wrap_text` returns, string for string, which
// tests/unit/test_wrap_count.cpp checks against the vendored wrapper. Two
// details carry that and are easy to get wrong -- a wrapped line keeps its
// SOURCE line's leading whitespace only when it is the first one (a break
// eats the whitespace it broke at), and the last line of a source line keeps
// the source's TRAILING whitespace, which has width and would otherwise make
// a hug two spaces narrow.
template <class Measure>
inline void wrapped_line_spans(
    const std::string& text, float max_width, Measure&& measure,
    std::vector<std::pair<std::size_t, std::size_t>>& out,
    bool break_long_words = false) {
    out.clear();
    if (text.empty() || max_width <= 0.0f) {
        out.emplace_back(0, text.size());
        return;
    }
    std::vector<std::pair<std::size_t, std::size_t>> words;
    auto& probe = wrapdetail::probe_scratch();

    std::size_t lineStart = 0;
    while (true) {
        const std::size_t nl = text.find('\n', lineStart);
        const std::size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
        wrapdetail::split_words(text, lineStart, lineEnd, words);
        if (words.empty()) {
            out.emplace_back(lineStart, lineEnd);
        } else {
            std::size_t cur = lineStart;
            std::size_t first = 0;
            const std::size_t last = words.size() - 1;
            const auto fits = [&](std::size_t k) {
                probe.assign(text, cur, words[k].second - cur);
                return measure(probe) <= max_width;
            };
            while (first < last) {
                if (fits(last)) break;
                std::size_t lo = first;
                std::size_t hi = last;
                while (hi - lo > 1) {
                    const std::size_t mid = lo + (hi - lo) / 2;
                    if (fits(mid)) lo = mid; else hi = mid;
                }
                out.emplace_back(cur, words[lo].second);
                first = lo + 1;
                cur = words[first].first;
            }
            out.emplace_back(cur, lineEnd);
        }
        if (nl == std::string::npos) break;
        lineStart = nl + 1;
    }
    if (break_long_words) break_long_spans(text, max_width, measure, out);
}

inline bool verify_wrap_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_VERIFY_WRAP");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

// The one callers use.
template <class Measure>
inline int wrapped_line_count(const std::string& text, float max_width,
                              Measure&& measure) {
    const int fast = wrapped_line_count_fast(text, max_width, measure);
    if (verify_wrap_enabled()) {
        const int ref = wrapped_line_count_linear(text, max_width, measure);
        hanabi::prof::tick(ref == fast ? "text.wrap_verified"
                                       : "text.wrap_disagree");
    }
    return fast;
}

}  // namespace hanabi::text
