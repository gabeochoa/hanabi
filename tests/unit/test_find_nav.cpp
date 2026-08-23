// What Cmd+G means, and where a step lands (src/ui/find_nav.h).
//
// A scripted test cannot press a Cmd chord (afterhours_gaps.md #49), so the
// binding is a table here rather than a branch inside the system: this is the
// only place the chord's meaning — Cmd+G forward, Shift for backward, a bare G
// is just a letter — can be held to anything.
#include <cstdio>

#include "../../src/ui/find_nav.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::find_nav::advance;
using hanabi::find_nav::chord;
using hanabi::find_nav::Step;

static void test_the_chord_needs_cmd() {
    std::printf("test_the_chord_needs_cmd\n");
    CHECK(chord(true, false, true) == Step::Next);
    CHECK(chord(true, true, true) == Step::Prev);
    // A bare G is a letter on its way into the find field, and so is Shift+G.
    CHECK(chord(false, false, true) == Step::None);
    CHECK(chord(false, true, true) == Step::None);
    // Cmd held with some other key is not this chord.
    CHECK(chord(true, false, false) == Step::None);
    CHECK(chord(true, true, false) == Step::None);
}

static void test_a_step_walks_the_matches() {
    std::printf("test_a_step_walks_the_matches\n");
    CHECK(advance(0, 3, Step::Next) == 1);
    CHECK(advance(1, 3, Step::Next) == 2);
    CHECK(advance(2, 3, Step::Prev) == 1);
    CHECK(advance(1, 3, Step::Prev) == 0);
}

static void test_both_ends_wrap() {
    std::printf("test_both_ends_wrap\n");
    // The last match's next is the first, and the first's previous is the
    // last — what the chevrons already do (find_in_conversation.e2e), which is
    // the point of both going through this function.
    CHECK(advance(2, 3, Step::Next) == 0);
    CHECK(advance(0, 3, Step::Prev) == 2);
    // A single match steps to itself rather than off either end.
    CHECK(advance(0, 1, Step::Next) == 0);
    CHECK(advance(0, 1, Step::Prev) == 0);
}

static void test_nothing_to_step_over() {
    std::printf("test_nothing_to_step_over\n");
    // No matches: the index stays at the start, so the tally can never read
    // "1 of 0".
    CHECK(advance(0, 0, Step::Next) == 0);
    CHECK(advance(3, 0, Step::Prev) == 0);
    CHECK(advance(1, 3, Step::None) == 0);
    // The query narrowed under a held index — restart rather than step from a
    // match that is no longer painted.
    CHECK(advance(7, 3, Step::Next) == 0);
    CHECK(advance(-1, 3, Step::Prev) == 0);
}

int main() {
    std::printf("=== test_find_nav ===\n");
    test_the_chord_needs_cmd();
    test_a_step_walks_the_matches();
    test_both_ends_wrap();
    test_nothing_to_step_over();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
