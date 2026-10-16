#include <cstdio>
#include <string>

#include "../../src/ui/slash_commands.h"

namespace sl = hanabi::slash;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_slash_commands ===\n");
    const sl::Command* q = sl::find("queue");
    CHECK(q != nullptr && q->runnable && q->arg == "<message>" && !q->sends_as_text);
    CHECK(sl::find("QUEUE") == q);
    const sl::Parsed p = sl::parse("/queue send this later");
    CHECK(p.matched && p.known && p.verb == "queue" && p.args == "send this later");
    const sl::Parsed bare = sl::parse("/queue");
    CHECK(bare.matched && bare.known && bare.args.empty());
    CHECK(!sl::sends_as_text("/queue later", false));
    CHECK(sl::find("knot") != nullptr && sl::find("btw") != nullptr && sl::find("new") != nullptr);
    const sl::Command* t = sl::find("title");
    CHECK(t != nullptr && t->runnable && t->arg == "<name>" && !t->sends_as_text);
    const sl::Parsed tp = sl::parse("/title  Ship notes ");
    CHECK(tp.known && tp.verb == "title" && tp.args == "Ship notes ");
    CHECK(!sl::sends_as_text("/title x", false));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
