#include <cstdio>

#include "../../src/edit_verbs.h"

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_edit_verb_queue ===\n");
    hanabi::EditVerbQueue q;
    int v = -1;
    CHECK(!q.take(&v) && v == -1);
    CHECK(!q.take(nullptr));
    q.push(static_cast<int>(hanabi::EditVerb::Paste));
    q.push(static_cast<int>(hanabi::EditVerb::Paste));
    q.push(static_cast<int>(hanabi::EditVerb::Undo));
    CHECK(q.size() == 3);
    CHECK(q.take(&v) && v == static_cast<int>(hanabi::EditVerb::Paste));
    CHECK(q.take(&v) && v == static_cast<int>(hanabi::EditVerb::Paste));
    CHECK(q.take(&v) && v == static_cast<int>(hanabi::EditVerb::Undo));
    CHECK(!q.take(&v) && q.size() == 0);
    for (int i = 0; i < 1000; ++i) q.push(static_cast<int>(hanabi::EditVerb::Copy));
    CHECK(q.size() == 1000);
    int taken = 0;
    while (q.take(&v)) ++taken;
    CHECK(taken == 1000);
    q.push(-1);
    q.push(static_cast<int>(hanabi::EditVerb::Count));
    q.push(99);
    CHECK(q.size() == 0);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
