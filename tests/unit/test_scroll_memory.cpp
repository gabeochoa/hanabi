#include <cstdio>

#include "../../src/ui/scroll_memory.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using hanabi::ui::ScrollMemory;

static void test_a_return_writes_the_place_back_for_a_few_builds() {
    ScrollMemory m;
    CHECK(!m.step(10, 0, 0.0f));
    CHECK(!m.step(11, 0, 300.0f));   // scrolled while built: remembered
    // Away for frames 12..19; the library clamped the entity to 0.
    for (int i = 0; i < ScrollMemory::kRestoreBuilds; ++i) {
        const auto w = m.step(20 + static_cast<std::size_t>(i), 0, 0.0f);
        CHECK(w.has_value() && *w == 300.0f);
    }
    CHECK(!m.step(24, 0, 300.0f));   // the reader's own again
    CHECK(!m.step(25, 0, 120.0f));
    CHECK(m.saved[0] == 120.0f);
}

static void test_a_return_to_the_top_needs_no_help() {
    ScrollMemory m;
    CHECK(!m.step(1, 0, 0.0f));
    CHECK(!m.step(9, 0, 0.0f));
}

static void test_each_key_keeps_its_own_place() {
    ScrollMemory m;
    CHECK(!m.step(1, 7, 500.0f));    // Blocked at 500
    auto w = m.step(2, 8, 500.0f);   // switch to Review: its own place, the top
    CHECK(w.has_value() && *w == 0.0f);
    for (int i = 0; i < ScrollMemory::kRestoreBuilds - 1; ++i) CHECK(m.step(3 + static_cast<std::size_t>(i), 8, 0.0f).has_value());
    CHECK(!m.step(10, 8, 40.0f));    // Review scrolled a little
    w = m.step(11, 7, 40.0f);        // back to Blocked: 500
    CHECK(w.has_value() && *w == 500.0f);
}

int main() {
    test_a_return_writes_the_place_back_for_a_few_builds();
    test_a_return_to_the_top_needs_no_help();
    test_each_key_keeps_its_own_place();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
