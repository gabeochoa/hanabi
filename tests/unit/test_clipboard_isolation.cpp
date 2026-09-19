#include <cstdio>

#include "../../src/util/clipboard.h"

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_clipboard_isolation ===\n");
    namespace cb = hanabi::clipboard;
    const auto before = cb::os_calls();
    for (int i = 0; i < 10; ++i) {
        cb::set_text("owned text");
        (void)cb::get_text();
    }
    if (cb::kIsolated) {
        CHECK(cb::os_calls().reads == before.reads && cb::os_calls().writes == before.writes);
        CHECK(cb::get_text() == "owned text");
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
        CHECK(cb::test_probe().generation == 10);
        cb::reset_test_probe();
        CHECK(cb::get_text().empty() && cb::test_probe().generation == 0);
        CHECK(cb::os_calls().reads == before.reads);
#endif
    } else {
        CHECK(cb::os_calls().reads == before.reads + 10 && cb::os_calls().writes == before.writes + 10);
    }
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
