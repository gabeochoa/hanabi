#include <cstdio>
#include <string>

#include "../../src/ui/session_theme.h"

namespace st = hanabi::session_theme;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_session_theme ===\n");
    CHECK(st::parse("").inherits() && st::parse("inherit").inherits());
    CHECK(st::parse("dark").mode == "dark" && st::parse("dark").accent.empty());
    CHECK(st::parse("amber").accent == "amber" && st::parse("amber").mode.empty());
    CHECK(st::parse("light+green").mode == "light" && st::parse("light+green").accent == "green");
    CHECK(st::parse("green+light").mode == "light" && st::parse("green+light").accent == "green");
    for (const char* s : {"", "dark", "amber", "light+green"}) CHECK(st::serialize(st::parse(s)) == s);
    CHECK(st::serialize(st::parse("green+light")) == "light+green");

    const st::Choice none;
    CHECK(st::apply_pick(none, "dark") == st::parse("dark"));
    CHECK(st::apply_pick(st::parse("dark"), "amber") == st::parse("dark+amber"));
    CHECK(st::apply_pick(st::parse("dark+amber"), "light") == st::parse("light+amber"));
    CHECK(st::apply_pick(st::parse("dark+amber"), "accent-inherit") == st::parse("dark"));
    CHECK(st::apply_pick(st::parse("dark+amber"), "inherit").inherits());

    CHECK(st::action_id("dark") == "theme:dark");
    CHECK(st::picked_key("theme:amber") == std::string("amber"));
    CHECK(!st::picked_key("sound:off"));

    auto r = st::resolve(none, true, "violet");
    CHECK(r.light && r.accent == "violet" && !r.own);
    r = st::resolve(st::parse("dark"), true, "violet");
    CHECK(!r.light && r.accent == "violet" && r.own);
    r = st::resolve(st::parse("amber"), false, "violet");
    CHECK(!r.light && r.accent == "amber" && r.own);
    r = st::resolve(st::parse("light+green"), false, "violet");
    CHECK(r.light && r.accent == "green");

    CHECK(st::label(none, "") == "Theme: Default");
    CHECK(st::label(st::parse("dark"), "") == "Theme: Dark");
    CHECK(st::label(st::parse("amber"), "Amber") == "Theme: Amber");
    CHECK(st::label(st::parse("light+amber"), "Amber") == "Theme: Light \xc2\xb7 Amber");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
