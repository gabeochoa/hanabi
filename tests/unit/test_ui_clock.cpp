#include <cstdio>

#include "../../src/ecs/ui_clock.h"

namespace uc = hanabi::ui_clock;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void reset() { uc::test_offset_seconds() = 0.0; }

int main() {
    std::printf("=== test_ui_clock ===\n");
    reset();
    CHECK(uc::advance_test_offset("1") && uc::test_offset_seconds() == 1.0);
    CHECK(uc::advance_test_offset("0.5") && uc::test_offset_seconds() == 1.5);
    CHECK(uc::advance_test_offset("0") && uc::test_offset_seconds() == 1.5);
    for (const char* bad : {"", "abc", "1abc", "nan", "inf", "-1", "-0.5", "1e400", " 1", "1 ", "+", "."}) {
        const double before = uc::test_offset_seconds();
        CHECK(!uc::advance_test_offset(bad));
        CHECK(uc::test_offset_seconds() == before);
    }
    reset();
    CHECK(uc::advance_test_offset("999999"));
    CHECK(!uc::advance_test_offset("2") && uc::test_offset_seconds() == 999999.0);
    CHECK(uc::advance_test_offset("1") && uc::test_offset_seconds() == 1000000.0);
    CHECK(!uc::advance_test_offset("0.1"));
    reset();
    const double a = uc::now_seconds();
    CHECK(uc::advance_test_offset("10"));
    const double b = uc::now_seconds();
    CHECK(b - a >= 10.0 && b - a < 11.0);
    CHECK(!uc::reduce_motion_override().has_value());
    uc::reduce_motion_override() = true;
    CHECK(uc::reduce_motion());
    uc::reduce_motion_override() = false;
    CHECK(!uc::reduce_motion());
    uc::reduce_motion_override().reset();
    reset();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
