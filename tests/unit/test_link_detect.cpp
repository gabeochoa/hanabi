#include <cstdio>

#include "../../src/ui/link_find.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

int main() {
    using hanabi::links::find;
    auto ids = [](const std::string& t) {
        std::vector<std::string> out;
        for (const auto& l : find(t)) out.push_back(l.id);
        return out;
    };
    CHECK((ids("Landed D117423523; see T275363905.") == std::vector<std::string>{"D117423523", "T275363905"}));
    CHECK((ids("Tracked under PRJ4471120 for the program.") == std::vector<std::string>{"PRJ4471120"}));
    CHECK(ids("PRJ123 is too short").empty());
    CHECK(ids("xPRJ4471120 binds").empty());
    CHECK(ids("PRJ4471120x binds").empty());
    CHECK((ids("(PRJ4471120)") == std::vector<std::string>{"PRJ4471120"}));
    CHECK((ids("PRJ4471120 and T123") == std::vector<std::string>{"PRJ4471120", "T123"}));
    const auto l = find("see PRJ4471120.");
    CHECK(l.size() == 1 && l[0].off == 4 && l[0].len == 10);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
