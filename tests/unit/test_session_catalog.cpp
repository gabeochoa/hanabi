#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/session_catalog.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

static api::SessionSummary row(const std::string& id, std::int64_t updated,
                               const std::string& title = "") {
    api::SessionSummary s;
    s.id = id;
    s.updated_at = updated;
    s.title = title.empty() ? id : title;
    return s;
}

static void test_unique_rows_are_untouched() {
    std::vector<api::SessionSummary> rows{row("a", 3), row("b", 2), row("c", 1)};
    CHECK(api::catalog::keep_one_row_per_id(rows) == 0);
    CHECK(rows.size() == 3 && rows[0].id == "a" && rows[2].id == "c");
}

static void test_a_repeated_id_keeps_one_row_the_freshest_in_its_first_place() {
    std::vector<api::SessionSummary> rows{row("a", 3, "old title"), row("b", 2),
                                          row("a", 9, "new title"), row("c", 1)};
    CHECK(api::catalog::keep_one_row_per_id(rows) == 1);
    CHECK(rows.size() == 3);
    CHECK(rows[0].id == "a" && rows[0].title == "new title");
    CHECK(rows[1].id == "b" && rows[2].id == "c");
}

static void test_a_tie_keeps_the_first() {
    std::vector<api::SessionSummary> rows{row("a", 5, "first"), row("a", 5, "second"),
                                          row("a", 4, "older")};
    CHECK(api::catalog::keep_one_row_per_id(rows) == 2);
    CHECK(rows.size() == 1 && rows[0].title == "first");
}

int main() {
    test_unique_rows_are_untouched();
    test_a_repeated_id_keeps_one_row_the_freshest_in_its_first_place();
    test_a_tie_keeps_the_first();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
