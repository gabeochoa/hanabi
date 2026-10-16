#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/keycaps.h"

namespace kc = hanabi::keycaps;
using kc::Cap;
using kc::Kind;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_keycaps ===\n");
    CHECK(kc::split("").empty());
    CHECK(kc::split("Unassigned").empty());
    CHECK((kc::split("Cmd K") == std::vector<Cap>{{Kind::Modifier, "\xe2\x8c\x98"}, {Kind::Key, "K"}}));
    CHECK((kc::split("Cmd Shift N") ==
           std::vector<Cap>{{Kind::Modifier, "\xe2\x8c\x98"}, {Kind::Modifier, "\xe2\x87\xa7"}, {Kind::Key, "N"}}));
    CHECK((kc::split("Ctrl Opt Shift Cmd F") ==
           std::vector<Cap>{{Kind::Modifier, "\xe2\x8c\x83"}, {Kind::Modifier, "\xe2\x8c\xa5"},
                            {Kind::Modifier, "\xe2\x87\xa7"}, {Kind::Modifier, "\xe2\x8c\x98"}, {Kind::Key, "F"}}));
    CHECK((kc::split("Opt Left/Right") ==
           std::vector<Cap>{{Kind::Modifier, "\xe2\x8c\xa5"}, {Kind::Key, "\xe2\x86\x90"}, {Kind::Text, "/"},
                            {Kind::Key, "\xe2\x86\x92"}}));
    CHECK((kc::split("Cmd Backspace") == std::vector<Cap>{{Kind::Modifier, "\xe2\x8c\x98"}, {Kind::Key, "\xe2\x8c\xab"}}));
    CHECK((kc::split("Return") == std::vector<Cap>{{Kind::Key, "\xe2\x86\xa9"}}));
    CHECK((kc::split("Home / End") ==
           std::vector<Cap>{{Kind::Key, "\xe2\x86\x96"}, {Kind::Text, "/"}, {Kind::Key, "\xe2\x86\x98"}}));
    CHECK((kc::split("Page Up / Down") ==
           std::vector<Cap>{{Kind::Key, "Page"}, {Kind::Key, "\xe2\x86\x91"}, {Kind::Text, "/"}, {Kind::Key, "\xe2\x86\x93"}}));
    CHECK((kc::split("Shift + a move") ==
           std::vector<Cap>{{Kind::Modifier, "\xe2\x87\xa7"}, {Kind::Text, "+ a move"}}));
    CHECK((kc::split("Cmd [") == std::vector<Cap>{{Kind::Modifier, "\xe2\x8c\x98"}, {Kind::Key, "["}}));
    CHECK((kc::split("Cmd F3") == std::vector<Cap>{{Kind::Modifier, "\xe2\x8c\x98"}, {Kind::Key, "F3"}}));
    CHECK(kc::joined(kc::split("Cmd K")) == "\xe2\x8c\x98 K");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
