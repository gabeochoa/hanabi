#include <cstdio>
#include <string>

#include "../../src/ui/run_outcome_label.h"

namespace ro = hanabi::run_outcome;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_run_outcome_label ===\n");
    CHECK(ro::label("interrupted") == "Stopped by you");
    CHECK(ro::label("handed_off") == "Handed off");
    CHECK(ro::label("failed") == "failed");
    CHECK(ro::label("completed") == "completed");
    CHECK(ro::label("") == "");
    CHECK(ro::label("interrupted", "") == "Stopped by you");
    CHECK(ro::label("failed", "2 calls never dispatched") == "failed \xc2\xb7 2 calls never dispatched");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
