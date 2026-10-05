#include <cstdio>
#include <string>

#include "../../src/api/create_title.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace ct = api::create_title;

int main() {
    CHECK(ct::explicit_title("  Payroll audit \n") == "Payroll audit");
    CHECK(ct::explicit_title("   ").empty());
    CHECK(ct::explicit_title("a\nb") == "a b");
    CHECK(ct::explicit_title(std::string(300, 'x')).size() == ct::kMaxTitle);
    // A cap never splits a UTF-8 sequence.
    std::string s(119, 'x');
    s += "\xc3\xa9\xc3\xa9";
    const std::string cut = ct::explicit_title(s);
    CHECK(cut.size() == 119);
    // The create's title: the typed name wins; else the prompt's first line.
    CHECK(ct::for_create("Named", "the prompt\nmore") == "Named");
    CHECK(ct::for_create("  ", "the prompt\nmore") == "the prompt");
    CHECK(ct::for_create("", std::string(200, 'p')).size() == ct::kMaxTitle);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
