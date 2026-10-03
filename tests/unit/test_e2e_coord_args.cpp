#include <cstdio>

#include "../../src/ecs/e2e_coord_args.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

int main() {
    using hanabi::e2e::coord_args_ok;
    CHECK(coord_args_ok({"500", "330"}));
    CHECK(coord_args_ok({"50%", "12.5"}));
    CHECK(!coord_args_ok({"", ""}));  // what the parser hands a bare `mouse_down`
    CHECK(!coord_args_ok({"500"}));
    CHECK(!coord_args_ok({"500", "x"}));
    CHECK(!coord_args_ok({"%", "3"}));
    CHECK(!coord_args_ok({"5px", "3"}));
    CHECK(hanabi::e2e::is_coord_command("mouse_down") && !hanabi::e2e::is_coord_command("mouse_up"));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
