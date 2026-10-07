#include <cmath>
#include <cstdio>

#include "../../src/line_spacing.h"

namespace ls = hanabi::line_spacing;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_line_spacing ===\n");
    CHECK(ls::kDefault == 100 && ls::kMin == 100 && ls::kMax == 200 && ls::kStep == 5);
    CHECK(ls::clamp(100) == 100);
    // The reference's floor: a stored value under 100% reads as 100%.
    CHECK(ls::clamp(99) == 100 && ls::clamp(80) == 100 && ls::clamp(0) == 100 && ls::clamp(-40) == 100);
    CHECK(ls::clamp(201) == 200 && ls::clamp(100000) == 200);
    CHECK(ls::clamp(133) == 133);
    CHECK(ls::stepped_up(100) == 105 && ls::stepped_down(100) == 100);
    CHECK(ls::stepped_up(200) == 200 && ls::stepped_up(197) == 200);
    CHECK(ls::stepped_down(105) == 100 && ls::stepped_down(103) == 100);
    CHECK(ls::stepped_up(0) == 105 && ls::stepped_down(999) == 195);
    CHECK(std::fabs(ls::factor(100) - 1.0f) < 1e-6f);
    CHECK(std::fabs(ls::factor(80) - 1.0f) < 1e-6f);
    CHECK(std::fabs(ls::factor(130) - 1.3f) < 1e-6f);
    CHECK(std::fabs(ls::factor(500) - 2.0f) < 1e-6f);
    int p = ls::kMin;
    int steps = 0;
    while (p < ls::kMax) {
        p = ls::stepped_up(p);
        ++steps;
    }
    CHECK(steps == 20 && p == ls::kMax);
    CHECK(std::round(16.0f * 1.3f * ls::factor(100)) == 21.0f);
    CHECK(std::round(16.0f * 1.0f * ls::factor(130)) == 21.0f);
    CHECK(std::round(16.0f * 1.0f * ls::factor(80)) == 16.0f);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
