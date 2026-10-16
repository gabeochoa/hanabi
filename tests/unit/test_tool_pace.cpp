#include <cstdio>
#include <string>

#include "../../src/ui/tool_pace.h"

namespace tp = hanabi::tool_pace;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_tool_pace ===\n");
    tp::Row r;
    r.running = true;
    r.created_at_unix = 1000;
    CHECK(tp::slow_after_ms(r) == tp::kSlowAfterMsDefault);
    CHECK(!tp::is_slow(r, 1000));
    CHECK(!tp::is_slow(r, 1019));
    CHECK(tp::is_slow(r, 1020));
    CHECK(std::string(tp::mark(r, 1005)).empty());
    CHECK(std::string(tp::mark(r, 1030)) == "taking a while");
    r.check_ms = 5000;
    CHECK(tp::slow_after_ms(r) == 5000);
    CHECK(!tp::is_slow(r, 1004) && tp::is_slow(r, 1005));
    r.check_ms = 90000;
    r.timeout_ms = 60000;
    CHECK(tp::slow_after_ms(r) == 60000);
    CHECK(!tp::is_slow(r, 1059) && tp::is_slow(r, 1060));
    r.check_ms = 0;
    r.timeout_ms = 3000;
    CHECK(tp::slow_after_ms(r) == 3000);
    CHECK(tp::is_slow(r, 1003));
    r.timeout_ms = 0;
    r.running = false;
    CHECK(!tp::is_slow(r, 2000));
    r.running = true;
    r.created_at_unix = 0;
    CHECK(!tp::is_slow(r, 2000));
    r.created_at_unix = 1000;
    r.background = true;
    CHECK(std::string(tp::mark(r, 1001)) == "in the background");
    CHECK(std::string(tp::mark(r, 9999)) == "in the background");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
