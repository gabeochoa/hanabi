// Ellipsizing a row title to a pixel width (src/util/ellipsize.h).
//
// The sidebar's ellipsizer used to be a backward linear scan and is now a
// binary search, because it was 34% of the main thread at a realistic catalog
// size. The ONLY thing that makes that a safe trade is that the new one
// returns the same string, so that is what this test is: the linear scan,
// written out as a reference, and a differential check against it over every
// width from zero to past the end of the string, for strings chosen to break
// the ways a cut point can be got wrong.
//
// The metric is a callable, which is the whole reason the algorithm was lifted
// out of the sidebar -- these rulers are ones no font would give you, and they
// are exactly the ones worth checking:
//
//   * uniform      -- one width per byte. The easy case.
//   * proportional -- per-character widths, so the cut point is not a
//                     function of the length.
//   * kerning      -- a pair that pulls the next glyph LEFT far enough that a
//                     longer prefix is NARROWER than a shorter one. Prefix
//                     width is then not monotonic and no binary search can
//                     find the true longest prefix, so what is checked there
//                     is the weaker guarantee the header states: whatever
//                     comes back is a prefix, ends on a boundary, and FITS.
#include <cstdio>
#include <string>

#include "../../src/util/ellipsize.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::text::fit_to_width;
using hanabi::text::utf8_floor;
using hanabi::text::utf8_next;

// The algorithm as it was before this change, verbatim in shape: walk the cut
// point backwards one code point at a time and stop at the first prefix that
// fits with the ellipsis. Slow on purpose -- it is the answer, not the method.
template <class Measure>
static std::string reference_fit(const std::string& text, float maxW,
                                 Measure&& measure) {
    if (maxW <= 0.0f) return std::string();
    if (measure(text.c_str()) <= maxW) return text;
    const float ell = measure(hanabi::text::kEllipsis);
    size_t n = text.size();
    while (n > 0) {
        --n;
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
            --n;
        if (measure(text.substr(0, n).c_str()) + ell <= maxW) break;
    }
    return text.substr(0, n) + hanabi::text::kEllipsis;
}

// ---- the rulers ----------------------------------------------------------

// Every byte the same width.
static float uniform(const char* s) {
    float w = 0.0f;
    for (const char* p = s; *p; ++p) w += 3.0f;
    return w;
}

// Width varies per byte, so a prefix's width is not a function of its length.
static float proportional(const char* s) {
    float w = 0.0f;
    for (const char* p = s; *p; ++p)
        w += 1.0f + static_cast<float>(static_cast<unsigned char>(*p) % 7);
    return w;
}

// A kerning pair that goes backwards: "AV" is drawn tighter than A and V
// apart, by more than V's own advance. A prefix ending in A is therefore WIDER
// than the same prefix plus V, and prefix width is not monotonic.
static float kerning(const char* s) {
    float w = 0.0f;
    char prev = '\0';
    for (const char* p = s; *p; ++p) {
        w += 6.0f;
        if (prev == 'A' && *p == 'V') w -= 9.0f;
        prev = *p;
    }
    return w < 0.0f ? 0.0f : w;
}

// ---- the check -----------------------------------------------------------

template <class Measure>
static void agrees_at_every_width(const char* label, const std::string& text,
                                  Measure&& measure) {
    const float full = measure(text.c_str());
    // Sweep past the full width so the "it already fits" branch is covered
    // too, in quarter-unit steps so cut points that land between two integer
    // widths are not skipped over.
    for (float w = -1.0f; w <= full + 4.0f; w += 0.25f) {
        const std::string got = fit_to_width(text, w, measure);
        const std::string want = reference_fit(text, w, measure);
        if (got != want) {
            std::printf("  FAIL: %s at maxW=%.2f: got \"%s\" want \"%s\"\n",
                        label, static_cast<double>(w), got.c_str(),
                        want.c_str());
            ++g_failures;
            return;  // one report per case; a wrong cut point is wrong at many
        }
    }
}

static void test_it_returns_what_the_linear_scan_returned() {
    std::printf("test_it_returns_what_the_linear_scan_returned\n");
    const std::string ascii =
        "profiling the quota shard and reporting back on what moved";
    const std::string shortish = "oncall handoff";
    const std::string oneChar = "x";
    const std::string empty;

    agrees_at_every_width("ascii/uniform", ascii, uniform);
    agrees_at_every_width("ascii/proportional", ascii, proportional);
    agrees_at_every_width("short/uniform", shortish, uniform);
    agrees_at_every_width("short/proportional", shortish, proportional);
    agrees_at_every_width("onechar/uniform", oneChar, uniform);
    agrees_at_every_width("empty/uniform", empty, uniform);
}

static void test_it_never_cuts_a_utf8_sequence_in_half() {
    std::printf("test_it_never_cuts_a_utf8_sequence_in_half\n");
    // An em dash (3 bytes), an accented letter (2), an emoji (4): a cut point
    // inside any of them renders as a replacement glyph.
    const std::string mixed =
        "reconciling \xe2\x80\x94 caf\xc3\xa9 \xf0\x9f\x94\xa5 the retry queue";
    agrees_at_every_width("utf8/uniform", mixed, uniform);
    agrees_at_every_width("utf8/proportional", mixed, proportional);

    // And directly: every returned prefix must end on a boundary.
    for (float w = 0.0f; w <= uniform(mixed.c_str()) + 4.0f; w += 0.5f) {
        const std::string got = fit_to_width(mixed, w, uniform);
        // Strip the ellipsis when one was added, then the remainder must be a
        // whole number of code points.
        std::string body = got;
        const std::string ell = hanabi::text::kEllipsis;
        if (body.size() >= ell.size() &&
            body.compare(body.size() - ell.size(), ell.size(), ell) == 0)
            body.resize(body.size() - ell.size());
        CHECK(utf8_floor(body, body.size()) == body.size());
    }
}

static void test_a_backwards_kern_gets_a_prefix_that_still_fits() {
    std::printf("test_a_backwards_kern_gets_a_prefix_that_still_fits\n");
    // The case a binary search CANNOT get exactly right, and the reason the
    // header states its guarantee in two halves. With "AV" drawn 9 units
    // tighter than A and V apart -- more than V's own 6-unit advance -- prefix
    // width goes DOWN as the prefix grows across the pair. A dip can hide an
    // arbitrarily long fitting prefix behind a non-fitting one, so no bounded
    // correction recovers the true maximum and only the linear scan does.
    //
    // What must hold anyway, and is the property the row actually depends on:
    // whatever comes back FITS. A prefix chosen one glyph short is invisible;
    // one chosen too long overflows the row.
    const char* cases[] = {"AVAVAVAVAVAVAVAV", "an AVid AVerage AVenue"};
    for (const char* c : cases) {
        const std::string text = c;
        for (float w = 0.5f; w <= kerning(c) + 4.0f; w += 0.25f) {
            const std::string got = fit_to_width(text, w, kerning);
            if (got == text) {  // wide enough for all of it, no ellipsis
                CHECK(kerning(text.c_str()) <= w);
                continue;
            }
            std::string body = got;
            const std::string ell = hanabi::text::kEllipsis;
            if (body.size() >= ell.size() &&
                body.compare(body.size() - ell.size(), ell.size(), ell) == 0)
                body.resize(body.size() - ell.size());
            // Budgeted the way the caller budgets: the prefix measured on its
            // own plus the ellipsis measured on its own, never the two
            // measured joined -- a kern across the join is not something the
            // row's width calculation knows about either. An empty body is the
            // bare ellipsis, which is returned at any width.
            if (!body.empty())
                CHECK(kerning(body.c_str()) + kerning(ell.c_str()) <= w);
            // ...and it is a real prefix of the text, ending on a boundary.
            CHECK(text.compare(0, body.size(), body) == 0);
            CHECK(utf8_floor(body, body.size()) == body.size());
        }
    }

    // And it is not silently giving up and returning nothing: at a width that
    // comfortably holds half the string, it holds about half the string.
    const std::string text = "an AVid AVerage AVenue";
    const std::string got = fit_to_width(text, kerning(text.c_str()) * 0.5f,
                                         kerning);
    CHECK(got.size() > text.size() / 3);
}

// HOW MANY TIMES DOES IT MEASURE? The answer above is "the same string"; this
// is the other half, and it is the half the change exists for. Measuring a
// prefix is linear in the prefix -- fontstash walks every glyph and binary-
// searches the kern table per pair -- so a probe is not free and the probe
// COUNT is the cost, per row, per frame, on every frame where the text or the
// column width is new.
//
// A count is the right instrument here for the same reason the perf work uses
// counts everywhere else: it is identical on any machine at any load, so this
// can be an assertion rather than an observation. The budget is stated as a
// mean over a realistic sweep, and it is set just above what the current
// algorithm produces -- close enough to bite if a seed goes wrong, loose
// enough not to flake on a boundary case.
static void test_it_does_not_measure_more_than_it_needs_to() {
    std::printf("test_it_does_not_measure_more_than_it_needs_to\n");
    // Real-shaped row titles: the sidebar's diet.
    const char* titles[] = {
        "profiling the quota shard and reporting back on what moved",
        "row 133 banyan diff gate",
        "reconciling the ledger after the migration, second attempt",
        "oncall handoff",
        "why did the retry queue stall overnight and what unblocked it",
        "a very long thread title that goes on well past any column width "
        "a sidebar could plausibly give it, and then keeps going",
    };

    long probes = 0;
    long cuts = 0;
    for (const char* t : titles) {
        const std::string text = t;
        const float full = proportional(text.c_str());
        // Only the widths that actually CUT: a title that fits costs one
        // measure whatever the algorithm is, so including those would dilute
        // the number this is trying to hold down.
        for (float w = 8.0f; w < full; w += 1.0f) {
            int n = 0;
            const auto counting = [&](const char* s) {
                ++n;
                return proportional(s);
            };
            const std::string got = fit_to_width(text, w, counting);
            const std::string want = reference_fit(text, w, proportional);
            CHECK(got == want);
            probes += n;
            ++cuts;
        }
    }

    const double mean = static_cast<double>(probes) / static_cast<double>(cuts);
    std::printf("  %ld cuts, %ld measure calls, mean %.2f per cut\n", cuts,
                probes, mean);

    // Two of those measures are fixed: the whole string, and the ellipsis.
    // Everything above 2.0 is the search. A middle-started bisection on these
    // titles reads 8.10; seeding from the measured width and galloping reads
    // 5.25. 6.0 sits between the two, so this fails if the seed is removed --
    // measured, by removing it -- and does not flake if a title is added.
    if (mean > 6.0) {
        std::printf("  FAIL: mean %.2f measure calls per cut exceeds 6.0 -- "
                    "the search is starting further from the answer than it "
                    "has to\n", mean);
        ++g_failures;
    }
}

static void test_the_edges() {
    std::printf("test_the_edges\n");
    // Non-positive width is an empty string, not an ellipsis: there is no room
    // to say anything, including that something was cut.
    CHECK(fit_to_width("anything", 0.0f, uniform).empty());
    CHECK(fit_to_width("anything", -5.0f, uniform).empty());
    // Room for less than one code point is a bare ellipsis.
    CHECK(fit_to_width("anything", 1.0f, uniform) == hanabi::text::kEllipsis);
    // Text that already fits comes back untouched, ellipsis-free.
    CHECK(fit_to_width("fits", 1000.0f, uniform) == "fits");
    // Exactly-fits is not truncated (the comparison is <=, not <).
    CHECK(fit_to_width("fits", uniform("fits"), uniform) == "fits");

    // The boundary walkers, on their own.
    const std::string s = "a\xc3\xa9z";  // 1 + 2 + 1 bytes
    CHECK(utf8_floor(s, 2) == 1);        // mid-sequence steps back
    CHECK(utf8_next(s, 1) == 3);         // skips the whole sequence
    CHECK(utf8_next(s, 4) == 4);         // clamped at the end
    CHECK(utf8_floor(s, 99) == 4);       // clamped, and 4 is a boundary
}

int main() {
    std::printf("=== test_ellipsize ===\n");
    test_it_returns_what_the_linear_scan_returned();
    test_it_never_cuts_a_utf8_sequence_in_half();
    test_a_backwards_kern_gets_a_prefix_that_still_fits();
    test_it_does_not_measure_more_than_it_needs_to();
    test_the_edges();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
