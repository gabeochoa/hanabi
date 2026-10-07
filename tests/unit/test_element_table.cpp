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

static void test_a_nested_table_takes_its_summary_lines_place() {
    std::printf("test_a_nested_table_takes_its_summary_lines_place\n");
    // json::array spelled out: a two-string brace list is an OBJECT to nlohmann.
    const json props{{"caption", "Steps"},
                     {"cols", json::array({"Step", "Took"})},
                     {"rows", json::array({json::array({"Lint", "2 min"}), json::array({"Unit", 6})})}};
    CHECK(el::table_summary(props) == "Steps: 2 rows (Step, Took)");
    CHECK(el::table_summary(json{{"rows", json::array()}}) == "Table: 0 rows ()");
    const json tree{{"t", "std/Card"},
                    {"c", {json{{"t", "std/Text"}}, json{{"t", "std/Stack"}, {"c", {json{{"t", "std/Table"}, {"p", props}}}}}}}};
    const std::string proj = "Gate run\nSteps: 2 rows (Step, Took)\nAll green.";
    CHECK(el::container_text_from_tree("std/Card", proj, tree) ==
          "Gate run\nStep | Took\nLint | 2 min\nUnit | 6\nAll green.");
    // A projection cut before the table's line: the table follows the text.
    CHECK(el::container_text_from_tree("std/Card", "Gate run", tree) == "Gate run\nStep | Took\nLint | 2 min\nUnit | 6");
    // A root table answers from its own props; a tree with no table answers nothing.
    CHECK(el::container_text_from_tree("std/Table", proj, tree).empty());
    CHECK(el::container_text_from_tree("std/Card", proj, json{{"t", "std/Card"}}).empty());
    CHECK(el::container_text_from_tree("std/Card", proj, json()).empty());
}

int main() {
    test_a_nested_table_takes_its_summary_lines_place();
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
