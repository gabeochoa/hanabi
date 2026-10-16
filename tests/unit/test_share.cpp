#include <cstdio>
#include <string>

#include "../../src/ui/share.h"

namespace sh = hanabi::share;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_share ===\n");
    const auto web = sh::session_link("Ship notes", "https://w/sessions/t2", "hanabi://thread/t2");
    CHECK(web.text == "Ship notes\nhttps://w/sessions/t2");
    CHECK(web.notice == "Copied the conversation's link");
    const auto deep = sh::session_link("", "", "hanabi://thread/t2");
    CHECK(deep.text == "hanabi://thread/t2");
    CHECK(deep.notice == "Copied the conversation's link (no web address is set)");
    const auto t = sh::turn("hello\nworld", "https://w/sessions/t2", "hanabi://thread/t2");
    CHECK(t.text == "hello\nworld\n\n\xe2\x80\x94 https://w/sessions/t2");
    CHECK(t.notice == "Copied the turn with its link");
    const auto t2 = sh::turn("one line\n", "", "hanabi://thread/t2");
    CHECK(t2.text == "one line\n\n\xe2\x80\x94 hanabi://thread/t2");
    CHECK(t2.notice == "Copied the turn with the conversation's link (no web address is set)");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
