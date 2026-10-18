#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/slash_extras.h"

namespace sx = hanabi::slash_extras;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_slash_extras ===\n");
    CHECK(sx::parse_autocompact("60").ok && sx::parse_autocompact("60").pct == 60);
    CHECK(sx::parse_autocompact(" 60% ").ok && sx::parse_autocompact(" 60% ").pct == 60);
    CHECK(sx::parse_autocompact("25").ok && sx::parse_autocompact("90").ok);
    CHECK(!sx::parse_autocompact("24").ok && !sx::parse_autocompact("91").ok);
    CHECK(sx::parse_autocompact("91").why == "The boundary is between 25% and 90%.");
    CHECK(sx::parse_autocompact("off").ok && sx::parse_autocompact("off").pct == 0);
    CHECK(sx::parse_autocompact("auto").ok && sx::parse_autocompact("0").ok && sx::parse_autocompact("default").ok);
    CHECK(!sx::parse_autocompact("sixty").ok && !sx::parse_autocompact("").ok && !sx::parse_autocompact("1000").ok);
    CHECK(sx::parse_autocompact("sixty").why == "Say a percentage from 25 to 90, or default.");
    CHECK(sx::autocompact_notice(60, 75) == "The next new thread compacts at 60%.");
    CHECK(sx::autocompact_notice(0, 75) == "New threads compact at 75% (your account's boundary).");
    CHECK(sx::autocompact_notice(0, 0) == "New threads compact on Auto. /autocompact 60 sets the next one.");
    CHECK(sx::session_autocompact_notice(60, 75) == "This thread compacts at 60%.");
    CHECK(sx::session_autocompact_notice(0, 75) == "This thread compacts at your account's 75%. /autocompact 60 sets its own.");
    CHECK(sx::session_autocompact_notice(0, 0) == "This thread compacts on Auto. /autocompact 60 sets its own.");

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
