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

#include <cstdio>
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

int main() {
    std::printf("-- wrapped line count vs afterhours' own wrapper --\n");
    sweep("uniform", uniform, true);
    sweep("proportional", proportional, true);
    sweep("backwards-kern", backwards_kern, false);
    sweep("dipping-kern", dipping_kern, false);
    spans_are_the_lines("uniform", uniform);
    spans_are_the_lines("proportional", proportional);
    degenerate_widths();
    overlong_word();
    scratch_is_reused();
    non_monotonic_is_documented();

    if (g_failures == 0) {
        std::printf("OK (0 skipped/pending)\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
