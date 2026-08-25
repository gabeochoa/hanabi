// tests/unit/test_text_cache.cpp
//
// The BOUND on src/util/text_cache.h, and what LRU order means at the edge of
// it.
//
// WHY A TEST AND NOT A CODE REVIEW. Every cache in this app was "bounded"
// when it was written. The transcript's render cache was bounded by clearing
// itself on a thread change, which in split view meant it cleared twice a
// frame and memoized nothing (docs/perf/MEMORY.md entry 3). The composer's
// image cache was bounded by a comment that said "a small bounded
// path->texture cache" above a map nothing ever erased -- 114 MB of it
// (entry 4). A bound that is not exercised by a test is a sentence in a
// header.
//
// So the three properties here are the three that were violated in this
// repo, by real code, in the last month:
//
//   * size() never exceeds capacity(), under far more distinct keys than the
//     cap -- the property the image cache did not have.
//   * reaching the cap EVICTS ONE and keeps the rest, rather than emptying
//     the whole thing -- the property the sidebar's ellipsis memo still does
//     not have, and the reason a resize drag through it costs 4096 cold
//     entries instead of one.
//   * a hit is promoted, so a working set smaller than the cap survives an
//     arbitrarily long scan past it -- the property that turns the bound from
//     a cliff into a cost of one recompute.

#include <cstdio>
#include <string>

#include "src/util/text_cache.h"

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

using hanabi::text::TextKeyCache;

static void never_exceeds_capacity() {
    TextKeyCache<int> c(64);
    std::size_t peak = 0;
    for (int i = 0; i < 64 * 200; ++i) {
        const std::string k = "row-" + std::to_string(i);
        c.put(k, 100.0f, 13.0f, i);
        if (c.size() > peak) peak = c.size();
    }
    std::printf("  12800 distinct keys into a 64-entry cache: peak %zu\n", peak);
    CHECK(peak <= c.capacity());
    CHECK(c.size() == 64);
}

// The same text at many WIDTHS is the resize-drag shape, and it is the one
// that grew a width-keyed map by an entry per message per frame the last time
// somebody reached for one (afterhours_gaps.md #136).
static void a_resize_drag_is_bounded() {
    TextKeyCache<int> c(32);
    for (int w = 200; w < 1400; ++w)
        c.put("the same row title all the way through the drag",
              static_cast<float>(w), 13.0f, w);
    CHECK(c.size() == 32);
    // The widths at the END of the drag are the ones still held.
    CHECK(c.find("the same row title all the way through the drag", 1399.0f,
                 13.0f) != nullptr);
    CHECK(c.find("the same row title all the way through the drag", 200.0f,
                 13.0f) == nullptr);
}

static void eviction_drops_one_not_all() {
    TextKeyCache<int> c(4);
    for (int i = 0; i < 4; ++i) c.put("k" + std::to_string(i), 1.0f, 2.0f, i);
    CHECK(c.size() == 4);
    c.put("k4", 1.0f, 2.0f, 4);
    CHECK(c.size() == 4);
    // k0 was least recently used and is gone; k1..k4 are all still here. A
    // cache that CLEARS at the cap loses all four and passes only the first
    // of these five checks.
    CHECK(c.find("k0", 1.0f, 2.0f) == nullptr);
    for (int i = 1; i <= 4; ++i)
        CHECK(c.find("k" + std::to_string(i), 1.0f, 2.0f) != nullptr);
}

static void a_hit_is_promoted() {
    TextKeyCache<int> c(3);
    c.put("a", 1.0f, 1.0f, 1);
    c.put("b", 1.0f, 1.0f, 2);
    c.put("c", 1.0f, 1.0f, 3);
    CHECK(c.find("a", 1.0f, 1.0f) != nullptr);  // a is now MRU
    c.put("d", 1.0f, 1.0f, 4);                  // evicts b, the LRU
    CHECK(c.find("a", 1.0f, 1.0f) != nullptr);
    CHECK(c.find("b", 1.0f, 1.0f) == nullptr);
    CHECK(c.find("c", 1.0f, 1.0f) != nullptr);
    CHECK(c.find("d", 1.0f, 1.0f) != nullptr);
}

// A working set that FITS is never evicted, however long the scan past it.
// This is the property that makes the bound cheap in the app: the visible
// paragraphs are asked for every frame, so they stay resident while a scroll
// streams hundreds of others through.
static void a_resident_working_set_survives_a_scan() {
    TextKeyCache<int> c(16);
    for (int i = 0; i < 8; ++i) c.put("hot" + std::to_string(i), 1.0f, 1.0f, i);
    for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 8; ++i)
            CHECK(c.find("hot" + std::to_string(i), 1.0f, 1.0f) != nullptr);
        c.put("cold" + std::to_string(round), 1.0f, 1.0f, round);
    }
    for (int i = 0; i < 8; ++i)
        CHECK(c.find("hot" + std::to_string(i), 1.0f, 1.0f) != nullptr);
    CHECK(c.size() <= 16);
}

// Re-putting an existing key updates it in place rather than growing.
static void reput_does_not_grow() {
    TextKeyCache<int> c(8);
    for (int i = 0; i < 500; ++i) c.put("same", 10.0f, 20.0f, i);
    CHECK(c.size() == 1);
    const int* v = c.find("same", 10.0f, 20.0f);
    CHECK(v != nullptr && *v == 499);
}

// Widths that differ by a hair are different keys. Hashing the float's VALUE
// through a lossy path would collapse them and return a measurement taken at
// the wrong width -- which is a wrong pixel, not a slow frame.
static void near_widths_are_distinct() {
    TextKeyCache<int> c(8);
    c.put("t", 100.0f, 13.0f, 1);
    c.put("t", 100.0001f, 13.0f, 2);
    const int* a = c.find("t", 100.0f, 13.0f);
    const int* b = c.find("t", 100.0001f, 13.0f);
    CHECK(a != nullptr && *a == 1);
    CHECK(b != nullptr && *b == 2);
    CHECK(c.size() == 2);
}

// EVERY VALUE IN HERE IS A MEASUREMENT, so it is only valid for the glyphs it
// was taken with. hanabi swaps the FACE behind afterhours' DEFAULT_FONT when
// the reader picks Hyperlegible in Settings; the name, the handle and the
// size all stay the same, so no key moves and every entry silently becomes a
// measurement of a font that is no longer on screen. This is the test for the
// counter that fixes it (src/util/text_epoch.h).
static void a_font_swap_drops_the_lot() {
    TextKeyCache<int> c(64);
    for (int i = 0; i < 20; ++i)
        c.put("row-" + std::to_string(i), 100.0f, 13.0f, i);
    CHECK(c.size() == 20);
    CHECK(c.find("row-3", 100.0f, 13.0f) != nullptr);

    hanabi::text::bump_font_epoch();

    // Not "the entry for row-3 is different" -- the entry is GONE, because a
    // measurement taken with the other face is not a better guess than no
    // measurement at all.
    CHECK(c.find("row-3", 100.0f, 13.0f) == nullptr);
    CHECK(c.size() == 0);

    // And it keeps working afterwards rather than dropping everything from
    // then on.
    c.put("row-3", 100.0f, 13.0f, 99);
    const int* v = c.find("row-3", 100.0f, 13.0f);
    CHECK(v != nullptr && *v == 99);
    CHECK(c.size() == 1);
}

// A cache constructed AFTER the swap must not drop its first entries: it never
// held anything measured with the old face. Getting this wrong is a cache that
// clears itself once on every construction, which is invisible and wasteful.
static void a_fresh_cache_is_not_dropped() {
    hanabi::text::bump_font_epoch();
    TextKeyCache<int> c(8);
    c.put("a", 1.0f, 1.0f, 7);
    CHECK(c.find("a", 1.0f, 1.0f) != nullptr);
    CHECK(c.size() == 1);
}

int main() {
    std::printf("-- bounded LRU for text-keyed memos --\n");
    never_exceeds_capacity();
    a_resize_drag_is_bounded();
    eviction_drops_one_not_all();
    a_hit_is_promoted();
    a_resident_working_set_survives_a_scan();
    reput_does_not_grow();
    near_widths_are_distinct();
    a_font_swap_drops_the_lot();
    a_fresh_cache_is_not_dropped();

    if (g_failures == 0) {
        std::printf("OK (0 skipped/pending)\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
