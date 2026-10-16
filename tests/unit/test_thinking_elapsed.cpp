#include <cstdio>
#include <string>

#include "../../src/ui/thinking_elapsed.h"

namespace te = hanabi::thinking_elapsed;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_thinking_elapsed ===\n");
    CHECK(te::format(0).empty());
    CHECK(te::format(999).empty());
    CHECK(te::format(1000) == "1s");
    CHECK(te::format(6400) == "6s");
    CHECK(te::format(59999) == "59s");
    CHECK(te::format(60000) == "1m 00s");
    CHECK(te::format(65000) == "1m 05s");
    CHECK(te::format(723000) == "12m 03s");
    CHECK(te::format(3600000) == "1h 00m");
    CHECK(te::format(3660000) == "1h 01m");
    CHECK(te::word_count("") == 0);
    CHECK(te::word_count("one") == 1);
    CHECK(te::word_count("  two\nwords\t here ") == 3);
    CHECK(te::summary("", 0) == "Thought for a moment");
    CHECK(te::summary("one", 0) == "Thought for a moment  \xc2\xb7  1 word");
    CHECK(te::summary("a b c", 500) == "Thought for a moment  \xc2\xb7  3 words");
    CHECK(te::summary("", 6000) == "Thought for 6s");
    CHECK(te::summary("a b c", 6000) == "Thought for 6s  \xc2\xb7  3 words");
    CHECK(te::summary("a b", 65000) == "Thought for 1m 05s  \xc2\xb7  2 words");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
