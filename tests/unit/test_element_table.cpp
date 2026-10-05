#include <cstdio>
#include <string>

#include "../../src/api/element_table.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace el = api::elements;
using nlohmann::json;

int main() {
    // Cells as text: header, then rows; scalars of every kind.
    json p = {{"cols", {"A", "B"}}, {"rows", {{"x", 1}, {true, nullptr}, {2.5, "y\nz"}}}};
    CHECK(el::table_text_from_props("std/Table", p) == "A | B\nx | 1\ntrue | \n2.5 | y z");
    // Only a std/Table; anything not a table falls back (empty).
    CHECK(el::table_text_from_props("std/Note", p).empty());
    CHECK(el::table_text_from_props("std/Table", json{{"cols", {"A"}}}).empty());
    CHECK(el::table_text_from_props("std/Table", json{{"rows", {{"x", json::object()}}}}).empty());
    CHECK(el::table_text_from_props("std/Table", json{{"rows", json::array()}}).empty());
    // The emit's own maxRows holds rows back, and says how many.
    json held = {{"cols", {"n"}}, {"rows", {{1}, {2}, {3}}}, {"maxRows", 2}};
    CHECK(el::table_text_from_props("std/Table", held) == "n\n1\n2\n(+1 more rows)");
    // The caps: rows past kRowCap end in an ellipsis; a long cell is cut.
    json big = {{"rows", json::array()}};
    for (int i = 0; i < 205; ++i) big["rows"].push_back({i});
    const std::string t = el::table_text_from_props("std/Table", big);
    CHECK(t.size() > 3 && t.substr(t.size() - 3) == "\xe2\x80\xa6");
    json longCell = {{"rows", {{std::string(3000, 'c')}}}};
    const auto lt = el::table_from_props(longCell);
    CHECK(lt && lt->rows[0][0].size() == el::kCellCap && lt->truncated);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
