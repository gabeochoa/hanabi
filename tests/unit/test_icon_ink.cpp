#include <cmath>
#include <cstdio>

#include "../../src/ui/icons_atlas.h"
#include "../../src/ui/icons_ink.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace ic = hanabi::icons;

int main() {
    // Every atlas icon has an ink box, inside its cell.
    CHECK(ic::kInk.size() == ic::kAtlas.size());
    for (const auto& e : ic::kInk) {
        CHECK(e.w > 0 && e.h > 0);
        CHECK(e.x >= 0 && e.y >= 0 && e.x + e.w <= e.cell && e.y + e.h <= e.cell);
    }
    // The close mark's two recorded captures (afterhours_gaps.md #114):
    // drawn at 8 it measured 8 px; the helper hands 8 back for an 8 px target.
    CHECK(std::fabs(ic::draw_px_for_extent("close", 8.0f) - 8.0f) < 0.6f);
    // Drawn at 11 it measured 10-11 px.
    const float at11 = ic::draw_px_for_extent("close", 10.5f);
    CHECK(at11 > 10.4f && at11 < 11.6f);
    // An unknown name keeps the old assumption (box = ink).
    CHECK(ic::draw_px_for_ink("nope", 9.0f) == 9.0f);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
