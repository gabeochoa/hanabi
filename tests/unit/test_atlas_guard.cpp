// The glyph-atlas measurement guard (src/util/atlas_guard.h), as pure logic.
//
// The condition it detects needs a GPU and a full 2048x2048 font atlas to
// reach, and it is reached for real by `hanabi.exe --atlas-stress` under
// scripts/atlas_gate.sh. What is testable here without one is the part that
// decides whether a number is believable, and that part has to be exactly
// right in two directions: it must not miss a zero, and it must not fire on
// the whole of launch (before a font is loaded `measure_text_internal`
// returns 0 by design, and a guard that screams through every startup is a
// guard somebody turns off).
//
// No graphics, no network.
#include <cmath>
#include <cstdio>
#include <string>

#include "../../src/util/atlas_guard.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::atlas::Fault;

static void reset(bool armed) {
    hanabi::atlas::reset_for_test();
    hanabi::atlas::state().armed = armed;
}

static void test_a_good_measurement_is_silent() {
    std::printf("test_atlas_a_good_measurement_is_silent\n");
    reset(true);
    CHECK(hanabi::atlas::check("hello", 13.0f, 31.5f) == 31.5f);
    CHECK(hanabi::atlas::check("M", 400.0f, 260.0f) == 260.0f);
    CHECK(hanabi::atlas::fault_count() == 0);
    CHECK(hanabi::atlas::first_fault() == Fault::None);
}

static void test_zero_for_a_real_string_is_a_fault() {
    std::printf("test_atlas_zero_for_a_real_string_is_a_fault\n");
    reset(true);
    // The observable symptom of afterhours_gaps.md #211, and the number the
    // gap recorded: the 94-character printable-ASCII string at 288pt.
    hanabi::atlas::check("the ninety-four character string", 288.0f, 0.0f);
    CHECK(hanabi::atlas::fault_count() == 1);
    CHECK(hanabi::atlas::first_fault() == Fault::ZeroWidth);
    CHECK(hanabi::atlas::first_fault_px() == 288.0f);
    CHECK(hanabi::atlas::first_fault_text() ==
          "the ninety-four character string");

    // A negative width is the same fault: it cannot be true either.
    hanabi::atlas::check("x", 13.0f, -1.0f);
    CHECK(hanabi::atlas::fault_count() == 2);
}

static void test_not_finite_is_a_fault() {
    std::printf("test_atlas_not_finite_is_a_fault\n");
    reset(true);
    hanabi::atlas::check("x", 13.0f, std::nanf(""));
    CHECK(hanabi::atlas::fault_count() == 1);
    CHECK(hanabi::atlas::first_fault() == Fault::NotFinite);
    // A NaN is worse than a zero -- it poisons the COMPARISON a wrap makes,
    // rather than the width -- so it is caught before the > 0 test, not after.
    reset(true);
    hanabi::atlas::check("x", 13.0f, INFINITY);
    CHECK(hanabi::atlas::fault_count() == 1);
    CHECK(hanabi::atlas::first_fault() == Fault::NotFinite);
}

static void test_launch_is_not_a_fault() {
    std::printf("test_atlas_launch_is_not_a_fault\n");
    reset(false);   // no font loaded yet: the whole of launch looks like this
    for (int i = 0; i < 100; ++i)
        hanabi::atlas::check("Home", 13.0f, 0.0f);
    CHECK(hanabi::atlas::fault_count() == 0);
    CHECK(hanabi::atlas::prefont_zero_count() == 100);
    // …and the moment a face exists, the same answer IS a fault.
    hanabi::atlas::arm();
    hanabi::atlas::check("Home", 13.0f, 0.0f);
    CHECK(hanabi::atlas::fault_count() == 1);
}

static void test_blank_text_measures_zero_legitimately() {
    std::printf("test_atlas_blank_text_measures_zero_legitimately\n");
    reset(true);
    hanabi::atlas::check("", 13.0f, 0.0f);
    hanabi::atlas::check(" ", 13.0f, 0.0f);
    hanabi::atlas::check("\n", 13.0f, 0.0f);
    hanabi::atlas::check("\t \n", 13.0f, 0.0f);
    CHECK(hanabi::atlas::fault_count() == 0);
}

static void test_the_probe_asks_a_new_question_every_time() {
    std::printf("test_atlas_the_probe_asks_a_new_question_every_time\n");
    reset(true);
    // fontstash keys a glyph on (codepoint, size*10, blur). A probe that asked
    // at the same size twice would be answered from the glyph cache the second
    // time and could never see a full atlas, so consecutive probes must
    // differ by at least a tenth of a point.
    std::string sizes;
    float last = -1.0f;
    bool allDistinct = true;
    for (int i = 0; i < 8; ++i) {
        float seen = 0.0f;
        hanabi::atlas::probe(48.0f, [&](const char*, float px) {
            seen = px;
            return 10.0f;
        });
        if (seen == last) allDistinct = false;
        last = seen;
        CHECK(seen >= 48.0f);
    }
    CHECK(allDistinct);
    CHECK(hanabi::atlas::probe_count() == 8);
    CHECK(hanabi::atlas::fault_count() == 0);

    // A probe that comes back with no advance is the atlas saying it is full,
    // one step BEFORE the app's own text starts measuring short.
    reset(true);
    const bool ok = hanabi::atlas::probe(
        48.0f, [](const char*, float) { return 0.0f; });
    CHECK(!ok);
    CHECK(hanabi::atlas::fault_count() == 1);
    CHECK(hanabi::atlas::first_fault() == Fault::AtlasFull);
}

static void test_the_log_decays_but_the_count_does_not() {
    std::printf("test_atlas_the_log_decays_but_the_count_does_not\n");
    reset(true);
    // Once the atlas is full EVERY measurement faults, every frame. The
    // counter must stay exact (it is what the soak column and the gate read)
    // even though the stderr line backs off.
    for (int i = 0; i < 500; ++i) hanabi::atlas::check("x", 13.0f, 0.0f);
    CHECK(hanabi::atlas::fault_count() == 500);
}

int main() {
    std::printf("=== test_atlas_guard ===\n");
    test_a_good_measurement_is_silent();
    test_zero_for_a_real_string_is_a_fault();
    test_not_finite_is_a_fault();
    test_launch_is_not_a_fault();
    test_blank_text_measures_zero_legitimately();
    test_the_probe_asks_a_new_question_every_time();
    test_the_log_decays_but_the_count_does_not();
    if (g_failures == 0) {
        std::printf("=== test_atlas_guard: ALL PASSED ===\n");
        return 0;
    }
    std::printf("=== test_atlas_guard: %d FAILED ===\n", g_failures);
    return 1;
}
