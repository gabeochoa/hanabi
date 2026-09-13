// tests/unit/test_wrap_count.cpp
//
// Counting wrapped lines without materialising them (src/util/wrap_count.h),
// checked against the function it has to agree with.
//
// WHY THIS TEST IS DIFFERENTIAL AND NOT EXAMPLE-BASED. The count decides a
// message box's HEIGHT, and afterhours then wraps the same string again to
// DRAW it. A disagreement of one line is a clipped message or a gap under it
// -- so "these twelve cases look right" is not the property worth pinning.
// The property is: for every string, at every width, the counter returns
// exactly `ui::detail::wrap_text_to_width(...).size()`. That function is the
// REAL vendored wrapper, included here and called with the same metric, so
// the test cannot drift from what ships the way a hand-copied reference
// would.
//
// The metrics are callables, so no font and no graphics are involved, and the
// unkind ones (a ruler where every glyph is 1 wide, one where 'W' is 40, one
// with a BACKWARDS kern) are the point rather than an afterthought: they
// generate break patterns no English prose would.
//
// WHAT THE LAST CASE PROVES, AND WHAT IT DOES NOT. Under a non-monotonic
// metric the bisecting counter may disagree with the linear one, and the test
// says so out loud with a constructed example instead of leaving it in a
// comment. The linear counter agrees with the vendor wrapper under every
// metric here, monotonic or not; that is what makes it usable as the runtime
// cross-check behind HANABI_VERIFY_WRAP.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "afterhours/src/plugins/ui/text_selection.h"
#include "src/util/wrap_count.h"

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

using hanabi::text::wrapped_line_count_fast;
using hanabi::text::wrapped_line_count_linear;

// The vendor wrapper's own answer: the thing both counters must equal.
template <class M>
static int vendor_lines(const std::string& s, float w, M&& m) {
    return static_cast<int>(
        afterhours::ui::detail::wrap_text_to_width(s, w, m).size());
}

// ---- metrics ---------------------------------------------------------------

// Every code point 7 wide. Monotonic, uniform, boring on purpose.
static float uniform(const std::string& s) {
    return static_cast<float>(s.size()) * 7.0f;
}

// Proportional: the widths differ enough that a break lands in a different
// place than a character count would put it.
static float proportional(const std::string& s) {
    float w = 0.0f;
    for (char c : s) {
        if (c == 'i' || c == 'l' || c == '.' || c == ' ') w += 3.0f;
        else if (c == 'W' || c == 'M' || c == 'm') w += 14.0f;
        else w += 8.0f;
    }
    return w;
}

// A ruler that is not monotonic in length: "AV" is NARROWER than "A".
static float backwards_kern(const std::string& s) {
    float w = 0.0f;
    for (size_t i = 0; i < s.size(); ++i) {
        w += (s[i] == ' ') ? 4.0f : 10.0f;
        if (i > 0 && s[i - 1] == 'A' && s[i] == 'V') w -= 30.0f;
    }
    return w < 0.0f ? 0.0f : w;
}

// The shape that actually breaks a bisection, which the one above does not:
// a metric where prefix width DIPS in the middle and comes back. 'Z' pulls
// 100px left, so a prefix ending before the Z can overflow while a LONGER one
// ending after it fits -- and bisection, probing the end first, sees a fitting
// line where the greedy wrapper has already broken. Constructed rather than
// found, because no shipped font does this; the point is to make the
// documented caveat something the suite demonstrates instead of asserts.
static float dipping_kern(const std::string& s) {
    float w = 0.0f;
    for (char c : s) {
        w += (c == ' ') ? 4.0f : 10.0f;
        if (c == 'Z') w -= 100.0f;
    }
    return w;
}

// ---- the corpus ------------------------------------------------------------

static std::vector<std::string> corpus() {
    return {
        "",
        " ",
        "\n",
        "\n\n",
        "one",
        "one two",
        "a b c d e f g h i j k l m n o p q r s t u v w x y z",
        "supercalifragilisticexpialidocious",
        "short then supercalifragilisticexpialidociousandthensome tail",
        "  leading spaces are indentation and are kept",
        "trailing spaces are kept too   ",
        "double  spaces   between    words",
        "hard\nbreaks\nevery\nword",
        "a paragraph\n\nwith a blank line between it and the next one",
        "Follow-up question #0: can you dig into the ledger and tell me "
        "which rows moved between the two runs, and why the gate went red?",
        "The quick brown fox jumps over the lazy dog. Pack my box with five "
        "dozen liquor jugs. How vexingly quick daft zebras jump!",
        "WWWW MMMM iiii llll WWWW MMMM iiii llll WWWW MMMM iiii llll",
        "mixed\n  indented continuation line that is quite long and wraps\n"
        "and a third",
        "AV AV AV AVAVAV A V AVA VAV",
        "aaaa aaaa aaaa Z aaaa",
        "one two three Z four five Z six seven",
        std::string(200, 'x') + " tail",
        "tail " + std::string(200, 'x'),
    };
}

// ---- the sweeps ------------------------------------------------------------

template <class M>
static void sweep(const char* metricName, M&& m, bool expectFastAgrees) {
    int cases = 0;
    int fastDisagreements = 0;
    for (const std::string& s : corpus()) {
        for (float w = 1.0f; w <= 400.0f; w += 1.0f) {
            const int want = vendor_lines(s, w, m);
            const int lin = wrapped_line_count_linear(s, w, m);
            const int fast = wrapped_line_count_fast(s, w, m);
            ++cases;
            if (lin != want) {
                std::printf(
                    "  FAIL[%s]: linear %d != vendor %d at w=%.0f for \"%.40s\"\n",
                    metricName, lin, want, static_cast<double>(w), s.c_str());
                ++g_failures;
            }
            if (fast != want) ++fastDisagreements;
        }
    }
    std::printf("  %s: %d cases, linear exact, fast disagreed %d\n", metricName,
                cases, fastDisagreements);
    if (expectFastAgrees && fastDisagreements != 0) {
        std::printf("  FAIL[%s]: bisecting counter disagreed on a MONOTONIC "
                    "metric, where it is required to be exact\n", metricName);
        ++g_failures;
    }
}

// A width of 0 or less means "one line, unwrapped" to the vendor wrapper, and
// an empty string is one line. Both are easy to get wrong by returning 0.
static void degenerate_widths() {
    for (float w : {-100.0f, -1.0f, 0.0f}) {
        CHECK(wrapped_line_count_linear("a b c d e", w, uniform) == 1);
        CHECK(wrapped_line_count_fast("a b c d e", w, uniform) == 1);
    }
    CHECK(wrapped_line_count_linear("", 100.0f, uniform) == 1);
    CHECK(wrapped_line_count_fast("", 100.0f, uniform) == 1);
}

// A word wider than the whole column goes on a line of its own rather than
// being split, and it does not take the next word with it.
static void overlong_word() {
    const std::string s = "hi enormouswordthatcannotfit ok";
    const int want = vendor_lines(s, 50.0f, uniform);
    CHECK(want == 3);
    CHECK(wrapped_line_count_linear(s, 50.0f, uniform) == want);
    CHECK(wrapped_line_count_fast(s, 50.0f, uniform) == want);
}

// The counter must not allocate per word. Nothing here can observe malloc, so
// what is pinned instead is the observable consequence: the scratch buffers
// are reused, so counting the same paragraph twice costs the same as once and
// a second, longer paragraph does not corrupt the first answer.
static void scratch_is_reused() {
    const std::string a = "alpha beta gamma delta epsilon zeta eta theta";
    const std::string b = std::string(400, 'q') + " and some words after it";
    const int a1 = wrapped_line_count_fast(a, 120.0f, uniform);
    (void)wrapped_line_count_fast(b, 120.0f, uniform);
    const int a2 = wrapped_line_count_fast(a, 120.0f, uniform);
    CHECK(a1 == a2);
    CHECK(a1 == vendor_lines(a, 120.0f, uniform));
}

// The non-monotonic case, stated rather than implied: with a metric that dips
// in the middle of a line, greedy breaks where bisection does not. The linear
// counter still matches the vendor wrapper exactly, which is the whole reason
// it exists and the whole reason HANABI_VERIFY_WRAP can cross-check the fast
// one against it at runtime.
static void non_monotonic_is_documented() {
    const std::string s = "aaaa aaaa aaaa Z aaaa";
    int differing = 0;
    for (float w = 1.0f; w <= 300.0f; w += 1.0f) {
        const int want = vendor_lines(s, w, dipping_kern);
        CHECK(wrapped_line_count_linear(s, w, dipping_kern) == want);
        if (wrapped_line_count_fast(s, w, dipping_kern) != want) ++differing;
    }
    std::printf("  dipping kern: fast differed at %d of 300 widths "
                "(linear: never)\n", differing);
    if (differing == 0) {
        std::printf("  FAIL: the constructed non-monotonic case no longer "
                    "separates the two searches, so this test is not "
                    "demonstrating the caveat it claims to\n");
        ++g_failures;
    }
}

// The spans are the LINES, byte for byte. This is what lets the hug measure
// each wrapped line without any of them being built as a std::string, and it
// is a stronger check than the count: two wraps can agree on how many lines
// there are and disagree about where the whitespace went.
template <class M>
static void spans_are_the_lines(const char* metricName, M&& m) {
    std::vector<std::pair<size_t, size_t>> spans;
    int checked = 0;
    for (const std::string& s : corpus()) {
        for (float w = 1.0f; w <= 400.0f; w += 3.0f) {
            const std::vector<std::string> want =
                afterhours::ui::detail::wrap_text_to_width(s, w, m);
            hanabi::text::wrapped_line_spans(s, w, m, spans);
            ++checked;
            if (spans.size() != want.size()) {
                std::printf("  FAIL[%s]: %zu spans != %zu lines at w=%.0f for "
                            "\"%.40s\"\n", metricName, spans.size(),
                            want.size(), static_cast<double>(w), s.c_str());
                ++g_failures;
                continue;
            }
            for (size_t i = 0; i < spans.size(); ++i) {
                const std::string got =
                    s.substr(spans[i].first, spans[i].second - spans[i].first);
                if (got != want[i]) {
                    std::printf("  FAIL[%s]: span %zu is \"%s\" but the line "
                                "is \"%s\" at w=%.0f\n", metricName, i,
                                got.c_str(), want[i].c_str(),
                                static_cast<double>(w));
                    ++g_failures;
                    break;
                }
            }
        }
    }
    std::printf("  %s spans: %d wraps compared line for line\n", metricName,
                checked);
}

// ---- the widths an answer holds at -----------------------------------------

// Prose the shape of what the transcript measures, on top of corpus(): the
// synthetic fixture's assistant turn wraps in one paragraph and can flatter
// an interval; a real answer has many, and its interval is the intersection
// of all of them.
static std::vector<std::string> interval_corpus() {
    std::vector<std::string> out = corpus();
    out.push_back(
        "Here's the breakdown for step 7:\n\n"
        "1. Pulled the trace and diffed it against the baseline.\n"
        "2. The hot path is `handle_request` calling into "
        "`parser_cache.entries` on every event.\n"
        "3. Under load that's ~40k calls/sec, each allocating.\n"
        "4. The fix caps the cache and hashes the key.\n\n"
        "Ruled out:\n- connection pool (steady)\n- metrics buffer (flat)\n\n"
        "Applying the LRU cap now and adding a regression test so this stays "
        "bounded going forward. Expected steady-state drop is significant.");
    out.push_back(
        "## What changed\n\n"
        "The counter records, over every probe it makes, the widest prefix "
        "that fit and the narrowest that did not. Between those two numbers "
        "every comparison resolves the same way, so the same breaks come out "
        "and the same count. That is the whole argument; it needs no "
        "monotonicity, only determinism.\n\n"
        "Three things follow. First, a message that fits on its hard lines "
        "has no overflowing probe and holds at every wider width, which is "
        "the rule that already shipped. Second, a message of fifty wrapped "
        "lines has fifty chances to pin the interval and it will be narrow. "
        "Third, a hard line of one word is never probed and constrains "
        "nothing, because it is one line everywhere.\n\n"
        "- the render cache keeps the interval per entry\n"
        "- the line-count memo keeps it per paragraph\n"
        "- the audit re-measures every hit the long way");
    out.push_back(
        "short reply\n\nwith a second paragraph that is long enough to wrap "
        "at most of the widths this sweep visits, and a third\n\nthat is not");
    return out;
}

// SOUNDNESS: at every sweep width w' inside the interval recorded at w, the
// counter returns the same count and the spans are the same lines. Checked
// under every metric, the non-monotonic ones included -- the interval is the
// same COMPUTATION, so it holds wherever the bisection itself is used, and
// not only where it agrees with the vendor. What is NOT asserted: that the
// answer differs just outside the interval. A moved break can leave the
// count, even the lines, unchanged; the interval is where the answer is
// KNOWN to hold, not the whole set where it happens to.
//
// Cost: every (w, w') pair per string per metric is quadratic in the sweep.
// The routine run steps 1 px over 1..400 (~20 M pairs, a few tenths of a
// second on top of the suite); HANABI_WRAP_STRESS=1 steps half a pixel, which
// quadruples that and is the run to make when the counter itself changes.
static float interval_step() {
    const char* v = std::getenv("HANABI_WRAP_STRESS");
    return (v != nullptr && *v != '\0' && std::string(v) != "0") ? 0.5f : 1.0f;
}

template <class M>
static void intervals_hold(const char* metricName, M&& m) {
    constexpr float kLo = 1.0f, kHi = 400.0f;
    const float kStep = interval_step();
    const std::size_t nw = static_cast<std::size_t>((kHi - kLo) / kStep) + 1;
    std::vector<int> counts(nw);
    std::vector<std::vector<std::pair<size_t, size_t>>> spans(nw);
    std::vector<hanabi::text::FitInterval> holds(nw);
    long checked = 0;
    long wrappedIntervals = 0;
    double sumWidth = 0.0;
    int stringsWithFiniteHi = 0;
    for (const std::string& s : interval_corpus()) {
        bool finiteHi = false;
        for (std::size_t i = 0; i < nw; ++i) {
            const float w = kLo + static_cast<float>(i) * kStep;
            counts[i] = hanabi::text::wrapped_line_count(s, w, m, &holds[i]);
            hanabi::text::wrapped_line_spans(s, w, m, spans[i]);
            if (!holds[i].contains(w)) {
                std::printf("  FAIL[%s]: interval [%g, %g) does not hold its "
                            "own width %g for \"%.40s\"\n", metricName,
                            static_cast<double>(holds[i].lo),
                            static_cast<double>(holds[i].hi),
                            static_cast<double>(w), s.c_str());
                ++g_failures;
            }
            // Never a width the counter answers by its early return (an
            // empty text is the one answer that is the same everywhere).
            if (!s.empty()) {
                CHECK(!holds[i].contains(0.0f));
                CHECK(!holds[i].contains(-1.0f));
            }
            if (std::isfinite(holds[i].hi)) {
                finiteHi = true;
                ++wrappedIntervals;
                sumWidth += static_cast<double>(holds[i].hi - holds[i].lo);
            }
        }
        if (finiteHi) ++stringsWithFiniteHi;
        for (std::size_t i = 0; i < nw; ++i) {
            for (std::size_t j = 0; j < nw; ++j) {
                const float wj = kLo + static_cast<float>(j) * kStep;
                if (!holds[i].contains(wj)) continue;
                ++checked;
                if (counts[j] != counts[i] || spans[j] != spans[i]) {
                    std::printf("  FAIL[%s]: [%g, %g) recorded at w=%g says "
                                "the answer holds at w=%g, but %d/%zu lines "
                                "became %d/%zu for \"%.40s\"\n", metricName,
                                static_cast<double>(holds[i].lo),
                                static_cast<double>(holds[i].hi),
                                static_cast<double>(kLo + static_cast<float>(i) * kStep),
                                static_cast<double>(wj), counts[i],
                                spans[i].size(), counts[j], spans[j].size(),
                                s.c_str());
                    ++g_failures;
                    goto next_string;
                }
            }
        }
    next_string:;
    }
    // The recorder must actually record: strings that wrap somewhere in the
    // sweep produce finite upper bounds, and the check above must have had
    // something to check.
    CHECK(stringsWithFiniteHi >= 10);
    CHECK(checked > 0);
    std::printf("  %s intervals: %ld (w, w') pairs held; %ld wrapped "
                "intervals, mean width %.1f\n", metricName, checked,
                wrappedIntervals,
                wrappedIntervals ? sumWidth / static_cast<double>(wrappedIntervals)
                                 : 0.0);
}

// The fenced early returns: an empty text holds everywhere, a non-positive
// width holds only among non-positive widths, and a text of single-word
// lines holds at every positive width.
static void interval_edges() {
    hanabi::text::FitInterval h;
    CHECK(hanabi::text::wrapped_line_count(std::string(), 50.0f, uniform, &h) == 1);
    CHECK(h.contains(-5.0f) && h.contains(0.0f) && h.contains(1e9f));
    CHECK(hanabi::text::wrapped_line_count(std::string("a b c"), 0.0f, uniform, &h) == 1);
    CHECK(h.contains(0.0f) && h.contains(-100.0f) && !h.contains(1.0f) &&
          !h.contains(1e-30f));
    CHECK(hanabi::text::wrapped_line_count(std::string("one\ntwo\nthree"), 5.0f,
                                           uniform, &h) == 3);
    CHECK(!h.contains(0.0f) && h.contains(1e-30f) && h.contains(1e9f));
    // A caller that does not ask for the interval gets the same count.
    CHECK(hanabi::text::wrapped_line_count(std::string("one two three"), 50.0f,
                                           uniform) ==
          hanabi::text::wrapped_line_count(std::string("one two three"), 50.0f,
                                           uniform, &h));
    // An unrecorded interval is empty; merge is intersection.
    hanabi::text::FitInterval none;
    CHECK(!none.contains(0.0f) && !none.contains(100.0f));
    hanabi::text::FitInterval a{10.0f, 50.0f}, b{20.0f, 40.0f};
    a.merge(b);
    CHECK(a.lo == 20.0f && a.hi == 40.0f);
}

static void the_default_wrap_is_unchanged() {
    std::vector<std::pair<size_t, size_t>> implicit;
    std::vector<std::pair<size_t, size_t>> off;
    int checked = 0;
    int mismatched = 0;
    for (const std::string& s : corpus()) {
        for (float w = 1.0f; w <= 400.0f; w += 1.0f) {
            const std::vector<std::string> want =
                afterhours::ui::detail::wrap_text_to_width(s, w, proportional);
            hanabi::text::wrapped_line_spans(s, w, proportional, implicit);
            hanabi::text::wrapped_line_spans(s, w, proportional, off, false);
            ++checked;
            if (implicit != off) {
                std::printf("  FAIL: the defaulted parameter is not "
                            "break_long_words=false at w=%.0f for \"%.40s\"\n",
                            static_cast<double>(w), s.c_str());
                ++g_failures;
            }
            if (implicit.size() != want.size()) {
                ++mismatched;
                continue;
            }
            for (size_t i = 0; i < implicit.size(); ++i) {
                const std::string got = s.substr(
                    implicit[i].first, implicit[i].second - implicit[i].first);
                if (got != want[i]) {
                    ++mismatched;
                    break;
                }
            }
        }
    }
    if (mismatched != 0) {
        std::printf("  FAIL: the default path drifted from the vendor wrapper "
                    "in %d of %d wraps\n", mismatched, checked);
        ++g_failures;
    }
    std::printf("  default path: %d wraps byte-for-byte the vendor wrapper's, "
                "and identical with break_long_words=false\n", checked);
}

static bool one_code_point(const std::string& line) {
    return hanabi::text::wrapdetail::next_char(line, 0, line.size()) >=
           line.size();
}

static void break_long_words_splits_only_the_overflow() {
    const std::vector<std::string> cases = {
        "hi enormouswordthatcannotpossiblyfitinthiscolumn ok",
        "./scripts/release_batch.sh --ledger=/var/finance/payouts/ledger.csv",
        std::string(200, 'x') + " tail",
        "tail " + std::string(200, 'x'),
        "short",
        "",
    };
    std::vector<std::pair<size_t, size_t>> plain;
    std::vector<std::pair<size_t, size_t>> broken;
    int overflowedByDefault = 0;
    int wrapsThatGainedALine = 0;
    for (const std::string& s : cases) {
        for (float w = 14.0f; w <= 300.0f; w += 7.0f) {
            hanabi::text::wrapped_line_spans(s, w, proportional, plain);
            hanabi::text::wrapped_line_spans(s, w, proportional, broken, true);

            std::string joinedPlain;
            std::string joinedBroken;
            for (const auto& p : plain)
                joinedPlain += s.substr(p.first, p.second - p.first);
            for (const auto& p : broken)
                joinedBroken += s.substr(p.first, p.second - p.first);
            CHECK(joinedPlain == joinedBroken);

            for (const auto& p : plain)
                if (proportional(s.substr(p.first, p.second - p.first)) > w)
                    ++overflowedByDefault;
            for (const auto& p : broken) {
                const std::string line = s.substr(p.first, p.second - p.first);
                if (one_code_point(line)) continue;
                if (proportional(line) > w) {
                    std::printf("  FAIL: break_long_words left \"%s\" %.0f "
                                "wide in a %.0f column\n", line.c_str(),
                                static_cast<double>(proportional(line)),
                                static_cast<double>(w));
                    ++g_failures;
                }
            }
            if (broken.size() > plain.size()) ++wrapsThatGainedALine;
        }
    }
    if (overflowedByDefault == 0) {
        std::printf("  FAIL: no default span overflows its column any more, so "
                    "this case is not exercising the fallback\n");
        ++g_failures;
    }
    if (wrapsThatGainedALine == 0) {
        std::printf("  FAIL: break_long_words never split a span\n");
        ++g_failures;
    }
    std::printf("  break_long_words: %d default spans overflowed, %d wraps "
                "gained a line, none left over-wide\n", overflowedByDefault,
                wrapsThatGainedALine);
}

static void break_long_words_keeps_code_points_whole() {
    const std::string s = "prefix " + [] {
        std::string t;
        for (int i = 0; i < 40; ++i) t += "\u00e9\u2014\u00fc";
        return t;
    }();
    std::vector<std::pair<size_t, size_t>> broken;
    for (float w = 20.0f; w <= 200.0f; w += 5.0f) {
        hanabi::text::wrapped_line_spans(s, w, uniform, broken, true);
        for (const auto& p : broken) {
            if (p.first >= s.size()) continue;
            if ((static_cast<unsigned char>(s[p.first]) & 0xC0) == 0x80) {
                std::printf("  FAIL: a span starts mid code point at w=%.0f\n",
                            static_cast<double>(w));
                ++g_failures;
                break;
            }
        }
    }
    std::printf("  break_long_words: no span starts inside a code point\n");
}

int main() {
    std::printf("-- wrapped line count vs afterhours' own wrapper --\n");
    sweep("uniform", uniform, true);
    sweep("proportional", proportional, true);
    sweep("backwards-kern", backwards_kern, false);
    sweep("dipping-kern", dipping_kern, false);
    spans_are_the_lines("uniform", uniform);
    spans_are_the_lines("proportional", proportional);
    intervals_hold("uniform", uniform);
    intervals_hold("proportional", proportional);
    intervals_hold("backwards-kern", backwards_kern);
    intervals_hold("dipping-kern", dipping_kern);
    interval_edges();
    degenerate_widths();
    overlong_word();
    scratch_is_reused();
    non_monotonic_is_documented();
    the_default_wrap_is_unchanged();
    break_long_words_splits_only_the_overflow();
    break_long_words_keeps_code_points_whole();

    if (g_failures == 0) {
        std::printf("OK (0 skipped/pending)\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
