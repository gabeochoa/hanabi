#include <cmath>
#include <cstdio>
#include <string>

#include "../../src/ui/md_table_ops.h"

namespace mt = hanabi::md_table_ops;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_md_table_ops ===\n");
    CHECK(mt::cap_rows(0, false).shown == 0 && mt::cap_rows(0, false).hidden == 0);
    CHECK(mt::cap_rows(10, false).shown == 10 && mt::cap_rows(10, false).hidden == 0);
    CHECK(mt::cap_rows(50, false).shown == 50 && mt::cap_rows(50, false).hidden == 0);
    CHECK(mt::cap_rows(51, false).shown == 50 && mt::cap_rows(51, false).hidden == 1);
    CHECK(mt::cap_rows(120, false).shown == 50 && mt::cap_rows(120, false).hidden == 70);
    CHECK(mt::cap_rows(120, true).shown == 120 && mt::cap_rows(120, true).hidden == 0);
    CHECK(mt::cap_rows(7, false, 3).shown == 3 && mt::cap_rows(7, false, 3).hidden == 4);
    CHECK(mt::more_rows_label(1) == "1 more row" && mt::more_rows_label(70) == "70 more rows");
    const mt::Rows rows{{"Name", "Count"}, {"a|b", "1"}, {"c", ""}};
    CHECK(mt::to_markdown(rows) == "| Name | Count |\n| --- | --- |\n| a\\|b | 1 |\n| c |  |\n");
    CHECK(mt::to_tsv(rows) == "Name\tCount\na|b\t1\nc\t\n");
    CHECK(mt::to_markdown({}).empty() && mt::to_tsv({}).empty());
    CHECK(std::fabs(mt::natural_width(rows, 6.0f, 16.0f) - (4 * 6.0f + 16.0f + 5 * 6.0f + 16.0f)) < 0.01f);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
