#include <cmath>
#include <cstdio>

#include "../../src/ui/reading_column.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    namespace rc = hanabi::reading_column;
    // The reference's worked numbers.
    CHECK(near(rc::fit(600.0f), 600.0f));    // under the floor: the whole pane
    CHECK(near(rc::fit(768.0f), 768.0f));
    CHECK(near(rc::fit(1000.0f), 942.0f));   // 29 either side
    CHECK(near(rc::fit(1280.0f), 1152.0f));
    CHECK(near(rc::fit(1600.0f), 1280.0f));  // capped
    // Never narrower than Comfortable (min(pane, 768)) and never wider than the pane.
    for (float p = 100.0f; p < 3000.0f; p += 37.0f) {
        CHECK(rc::fit(p) + 0.001f >= std::fmin(p, 768.0f));
        CHECK(rc::fit(p) <= p + 0.001f);
    }
    CHECK(rc::fit(0.0f) == 0.0f);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
