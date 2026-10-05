#include <cstdio>

#include "../../src/ui/md_table_align.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace mt = hanabi::md_table;

int main() {
    const auto a = mt::alignments("| :--- | :---: | ---: | --- |");
    CHECK(a.size() == 4);
    CHECK(a[0] == mt::Align::Left && a[1] == mt::Align::Center && a[2] == mt::Align::Right && a[3] == mt::Align::Left);
    // No outer pipes, no spaces.
    const auto b = mt::alignments(":-|-:");
    CHECK(b.size() == 2 && b[0] == mt::Align::Left && b[1] == mt::Align::Right);
    // A single ':' is not right-aligned on its own; past the end is Left.
    CHECK(mt::alignments("|:|")[0] == mt::Align::Left);
    CHECK(mt::at(b, 5) == mt::Align::Left);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
