#include <cstdio>
#include <string>

#include "../../src/ecs/tab_groups_json.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using ecs::model::SplitAxis;
using ecs::model::TabGroups;
using nlohmann::json;

static std::string ids_in(const TabGroups& m, int g) {
    std::string out;
    for (const auto& t : m.tabs_in(g)) out += t.id;
    return out;
}

static TabGroups three_panes() {
    TabGroups m;
    for (const char* id : {"a", "b", "c", "d", "e"}) m.open(id);
    m.split_off("c", 1, SplitAxis::Vertical);
    m.move_to_group("d", 1);
    m.split_off("e", 2, SplitAxis::Vertical);
    m.select("a");
    m.select("d");
    m.select("b");
    return m;
}

static void test_a_layout_round_trips() {
    std::printf("test_a_layout_round_trips\n");
    TabGroups m = three_panes();
    const TabGroups back = ecs::model::tab_groups_from_json(ecs::model::tab_groups_to_json(m));
    CHECK(back.group_count() == 3);
    CHECK(ids_in(back, 0) == "ab");
    CHECK(ids_in(back, 1) == "cd");
    CHECK(ids_in(back, 2) == "e");
    CHECK(*back.selected_in(0) == "b");
    CHECK(*back.selected_in(1) == "d");
    CHECK(*back.selected_in(2) == "e");
    CHECK(*back.selected() == "b");
    CHECK(back.acted_group() == 0);
    CHECK(back.axis() == SplitAxis::Vertical);
}

static void test_one_pane_writes_no_axis() {
    std::printf("test_one_pane_writes_no_axis\n");
    TabGroups m;
    m.open("a");
    m.open("b");
    m.adopt_axis(SplitAxis::Vertical);
    const json j = ecs::model::tab_groups_to_json(m);
    CHECK(!j.contains("axis"));
    CHECK(j["groups"].size() == 1);
    json forged = j;
    forged["axis"] = "vertical";
    CHECK(ecs::model::tab_groups_from_json(forged).axis() == SplitAxis::Horizontal);
}

static void test_a_preview_is_not_written() {
    std::printf("test_a_preview_is_not_written\n");
    TabGroups m;
    m.open("a");
    m.open("p", true);
    const TabGroups back = ecs::model::tab_groups_from_json(ecs::model::tab_groups_to_json(m));
    CHECK(back.size() == 1 && back.contains("a"));
}

static void test_kept_tabs_come_back_in_front() {
    std::printf("test_kept_tabs_come_back_in_front\n");
    TabGroups m;
    for (const char* id : {"a", "b", "c"}) m.open(id);
    m.set_kept("c", true);
    CHECK(ids_in(m, 0) == "cab");
    const TabGroups back = ecs::model::tab_groups_from_json(ecs::model::tab_groups_to_json(m));
    CHECK(ids_in(back, 0) == "cab");
    CHECK(back.find("c")->kept && !back.find("a")->kept);
}

static void test_unwanted_and_repeated_tabs_are_dropped_and_empty_panes_collapse() {
    std::printf("test_unwanted_and_repeated_tabs_are_dropped_and_empty_panes_collapse\n");
    const json j = json::parse(R"({"groups":[{"tabs":["a","b"]},{"tabs":["x","y"]},{"tabs":["b","c"]}],"axis":"vertical"})");
    const TabGroups back = ecs::model::tab_groups_from_json(
        j, [](const std::string& id) { return id != "x" && id != "y"; });
    CHECK(back.group_count() == 2);
    CHECK(ids_in(back, 0) == "ab");
    CHECK(ids_in(back, 1) == "c");
    CHECK(back.axis() == SplitAxis::Vertical);
    const TabGroups one = ecs::model::tab_groups_from_json(
        j, [](const std::string& id) { return id == "a"; });
    CHECK(one.group_count() == 1);
    CHECK(one.axis() == SplitAxis::Horizontal);
}

static void test_a_named_selection_that_is_gone_falls_back_to_the_first_tab() {
    std::printf("test_a_named_selection_that_is_gone_falls_back_to_the_first_tab\n");
    const json j = json::parse(R"({"groups":[{"tabs":["a","b"],"selected":"zzz"},{"tabs":["c"]}],"active":"nope"})");
    const TabGroups back = ecs::model::tab_groups_from_json(j);
    CHECK(*back.selected_in(0) == "a");
    CHECK(*back.selected_in(1) == "c");
    CHECK(back.selected().has_value());
}

static void test_bad_input_restores_an_empty_model() {
    std::printf("test_bad_input_restores_an_empty_model\n");
    for (const char* text : {"null", "[]", "{}", R"({"groups":3})", R"({"groups":[1,"a",{"tabs":5}]})",
                             R"({"groups":[{"tabs":[1,null,""]}]})"}) {
        const TabGroups back = ecs::model::tab_groups_from_json(json::parse(text));
        CHECK(back.empty());
        CHECK(back.group_count() == 1);
    }
}

static void test_a_saved_mirror_split_becomes_two_panes_that_move_the_second_tab() {
    std::printf("test_a_saved_mirror_split_becomes_two_panes_that_move_the_second_tab\n");
    const json j = ecs::model::legacy_split_layout({"t1", "t2", "t3"}, "t1", "t3", 1);
    const TabGroups m = ecs::model::tab_groups_from_json(j);
    CHECK(m.group_count() == 2);
    CHECK(ids_in(m, 0) == "t1t2");
    CHECK(ids_in(m, 1) == "t3");
    CHECK(*m.selected_in(0) == "t1" && *m.selected_in(1) == "t3");
    CHECK(*m.selected() == "t3" && m.acted_group() == 1);
    const TabGroups left = ecs::model::tab_groups_from_json(
        ecs::model::legacy_split_layout({"t1", "t2", "t3"}, "t2", "t3", 0));
    CHECK(*left.selected() == "t2" && left.acted_group() == 0);
    const TabGroups gone = ecs::model::tab_groups_from_json(
        ecs::model::legacy_split_layout({"t1", "t2", "t3"}, "t9", "t3", 0));
    CHECK(*gone.selected_in(0) == "t1");
}

static void test_a_saved_mirror_split_that_cannot_be_two_panes_stays_one() {
    std::printf("test_a_saved_mirror_split_that_cannot_be_two_panes_stays_one\n");
    CHECK(ecs::model::legacy_split_layout({"t1", "t2"}, "t1", "t1", 0).is_null());
    CHECK(ecs::model::legacy_split_layout({"t1", "t2"}, "t1", "t9", 0).is_null());
    CHECK(ecs::model::legacy_split_layout({"t1", "t2"}, "", "t2", 0).is_null());
    CHECK(ecs::model::legacy_split_layout({"t1", "t2"}, "t1", "", 0).is_null());
    CHECK(ecs::model::legacy_split_layout({"t2"}, "t1", "t2", 0).is_null());
    CHECK(ecs::model::legacy_split_layout({}, "t1", "t2", 0).is_null());
}

int main() {
    test_a_layout_round_trips();
    test_one_pane_writes_no_axis();
    test_a_preview_is_not_written();
    test_kept_tabs_come_back_in_front();
    test_unwanted_and_repeated_tabs_are_dropped_and_empty_panes_collapse();
    test_a_named_selection_that_is_gone_falls_back_to_the_first_tab();
    test_bad_input_restores_an_empty_model();
    test_a_saved_mirror_split_becomes_two_panes_that_move_the_second_tab();
    test_a_saved_mirror_split_that_cannot_be_two_panes_stays_one();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
