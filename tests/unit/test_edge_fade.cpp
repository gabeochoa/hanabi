#include <cmath>
#include <cstdio>

#include "../../src/ui/edge_fade.h"

namespace ef = hanabi::edge_fade;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    std::printf("=== test_edge_fade ===\n");
    const auto fits = ef::strength(0.0f, 200.0f, 300.0f);
    CHECK(fits.top == 0.0f && fits.bottom == 0.0f);
    const auto exact = ef::strength(0.0f, 300.0f, 300.0f);
    CHECK(exact.top == 0.0f && exact.bottom == 0.0f);
    const auto top = ef::strength(0.0f, 600.0f, 300.0f);
    CHECK(top.top == 0.0f && top.bottom == 1.0f);
    const auto bottom = ef::strength(300.0f, 600.0f, 300.0f);
    CHECK(bottom.top == 1.0f && bottom.bottom == 0.0f);
    const auto middle = ef::strength(150.0f, 600.0f, 300.0f);
    CHECK(middle.top == 1.0f && middle.bottom == 1.0f);
    const auto easingIn = ef::strength(9.0f, 600.0f, 300.0f);
    CHECK(near(easingIn.top, 0.5f) && easingIn.bottom == 1.0f);
    const auto easingOut = ef::strength(300.0f - 4.5f, 600.0f, 300.0f);
    CHECK(easingOut.top == 1.0f && near(easingOut.bottom, 0.25f));
    const auto overscroll = ef::strength(-40.0f, 600.0f, 300.0f);
    CHECK(overscroll.top == 0.0f && overscroll.bottom == 1.0f);
    const auto past = ef::strength(900.0f, 600.0f, 300.0f);
    CHECK(past.top == 1.0f && past.bottom == 0.0f);
    const auto noView = ef::strength(10.0f, 600.0f, 0.0f);
    CHECK(noView.top == 0.0f && noView.bottom == 0.0f);
    CHECK(ef::alpha_for(0.0f) == 0);
    CHECK(ef::alpha_for(1.0f) == 255);
    CHECK(ef::alpha_for(0.5f) == 128);
    CHECK(ef::alpha_for(2.0f) == 255);
    CHECK(ef::alpha_for(1.0f, 200) == 200);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
