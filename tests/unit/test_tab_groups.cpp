#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/tab_groups.h"

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

static std::string ids_in(const TabGroups& m, int g) {
    std::string out;
    for (const auto& t : m.tabs_in(g)) out += t.id;
    return out;
}

static std::string all(const TabGroups& m) {
    std::string out;
    for (int g = 0; g < m.group_count(); ++g) out += (g ? "|" : "") + ids_in(m, g);
    return out;
}

static TabGroups three() {
    TabGroups m;
    m.open("a");
    m.open("b");
    m.open("c");
    return m;
}

static void test_open_adds_to_the_acted_group_and_selects() {
    std::printf("test_open_adds_to_the_acted_group_and_selects\n");
    TabGroups m = three();
    CHECK(all(m) == "abc");
    CHECK(m.group_count() == 1);
    CHECK(*m.selected() == "c");
    m.open("a");
    CHECK(*m.selected() == "a");
    CHECK(m.size() == 3);
}

static void test_a_preview_is_replaced_by_the_next_preview() {
    std::printf("test_a_preview_is_replaced_by_the_next_preview\n");
    TabGroups m;
    m.open("a");
    m.open("p1", true);
    m.open("p2", true);
    CHECK(all(m) == "ap2");
    CHECK(m.find("p2")->preview);
    m.open("p2");
    CHECK(!m.find("p2")->preview);
    m.open("p3", true);
    CHECK(all(m) == "ap2p3");
}

static void test_split_right_makes_a_second_group() {
    std::printf("test_split_right_makes_a_second_group\n");
    TabGroups m = three();
    CHECK(m.split_off("c", 1, SplitAxis::Horizontal));
    CHECK(all(m) == "ab|c");
    CHECK(m.axis() == SplitAxis::Horizontal);
    CHECK(*m.selected_in(0) == "b" || *m.selected_in(0) == "a");
    CHECK(*m.selected_in(1) == "c");
    CHECK(*m.selected() == "c");
    CHECK(m.acted_group() == 1);
    CHECK(m.group_keys().size() == 2);
}

static void test_split_down_sets_the_axis() {
    std::printf("test_split_down_sets_the_axis\n");
    TabGroups m = three();
    CHECK(m.split_off("b", 1, SplitAxis::Vertical));
    CHECK(m.axis() == SplitAxis::Vertical);
    CHECK(all(m) == "ac|b");
}

static void test_a_lone_tab_does_not_split_beside_itself() {
    std::printf("test_a_lone_tab_does_not_split_beside_itself\n");
    TabGroups m;
    m.open("a");
    CHECK(!m.split_off("a", 0, SplitAxis::Horizontal));
    CHECK(!m.split_off("a", 1, SplitAxis::Horizontal));
    CHECK(m.group_count() == 1);
    CHECK(!m.split_off("missing", 1, SplitAxis::Horizontal));
}

static void test_split_left_inserts_before_and_keeps_identities() {
    std::printf("test_split_left_inserts_before_and_keeps_identities\n");
    TabGroups m = three();
    m.split_off("c", 1, SplitAxis::Horizontal);
    const auto before = m.group_keys();
    CHECK(m.split_off("a", 0, SplitAxis::Horizontal));
    CHECK(all(m) == "a|b|c");
    const auto after = m.group_keys();
    CHECK(after.size() == 3);
    CHECK(after[1] == before[0]);
    CHECK(after[2] == before[1]);
    CHECK(after[0] != before[0] && after[0] != before[1]);
}

static void test_three_columns_is_the_same_verb_again() {
    std::printf("test_three_columns_is_the_same_verb_again\n");
    TabGroups m = three();
    m.split_off("b", 1, SplitAxis::Horizontal);
    m.split_off("c", 2, SplitAxis::Horizontal);
    CHECK(all(m) == "a|b|c");
    CHECK(m.group_count() == 3);
    CHECK(m.occupied_group_count() == 3);
}

static void test_closing_a_pane_empties_it_and_the_survivors_keep_identity() {
    std::printf("test_closing_a_pane_empties_it_and_the_survivors_keep_identity\n");
    TabGroups m = three();
    m.split_off("b", 1, SplitAxis::Horizontal);
    m.split_off("c", 2, SplitAxis::Horizontal);
    const auto keys = m.group_keys();
    m.close("a");
    CHECK(all(m) == "b|c");
    CHECK(m.group_keys().size() == 2);
    CHECK(m.group_keys()[0] == keys[1]);
    CHECK(m.group_keys()[1] == keys[2]);
    CHECK(*m.selected_in(0) == "b");
    CHECK(*m.selected_in(1) == "c");
}

static void test_closing_the_shown_tab_returns_to_the_one_you_came_from() {
    std::printf("test_closing_the_shown_tab_returns_to_the_one_you_came_from\n");
    TabGroups m;
    for (const char* id : {"a", "b", "c", "d"}) m.open(id);
    m.select("a");
    m.select("c");
    m.close("c");
    CHECK(*m.selected() == "a");
    CHECK(all(m) == "abd");
}

static void test_closing_with_no_history_takes_the_neighbour() {
    std::printf("test_closing_with_no_history_takes_the_neighbour\n");
    TabGroups m = three();
    m.close("b");
    CHECK(*m.selected_in(0) == "c" || *m.selected_in(0) == "a");
    m.close("c");
    CHECK(*m.selected() == "a");
    m.close("a");
    CHECK(m.empty());
    CHECK(!m.selected_in(0));
    CHECK(m.group_count() == 1);
}

static void test_closing_an_unshown_tab_leaves_the_selection() {
    std::printf("test_closing_an_unshown_tab_leaves_the_selection\n");
    TabGroups m = three();
    m.close("a");
    CHECK(*m.selected() == "c");
    CHECK(all(m) == "bc");
}

static void test_closing_the_last_tab_of_a_pane_lands_on_the_nearest_pane() {
    std::printf("test_closing_the_last_tab_of_a_pane_lands_on_the_nearest_pane\n");
    TabGroups m = three();
    m.split_off("c", 1, SplitAxis::Horizontal);
    const std::string left = *m.selected_in(0);
    m.close("c");
    CHECK(m.group_count() == 1);
    CHECK(*m.selected() == left);
}

static void test_move_to_group_joins_the_end_of_the_run() {
    std::printf("test_move_to_group_joins_the_end_of_the_run\n");
    TabGroups m = three();
    m.split_off("c", 1, SplitAxis::Horizontal);
    m.move_to_group("a", 1);
    CHECK(all(m) == "b|ca");
    CHECK(*m.selected() == "a");
    m.move_to_group("a", 0);
    CHECK(all(m) == "ba|c");
}

static void test_move_to_one_past_the_end_makes_a_pane() {
    std::printf("test_move_to_one_past_the_end_makes_a_pane\n");
    TabGroups m = three();
    m.move_to_group("c", 1);
    CHECK(all(m) == "ab|c");
    m.move_to_group("c", 5);
    CHECK(all(m) == "ab|c");
}

static void test_moving_the_only_tab_out_of_a_pane_collapses_it() {
    std::printf("test_moving_the_only_tab_out_of_a_pane_collapses_it\n");
    TabGroups m = three();
    m.split_off("c", 1, SplitAxis::Horizontal);
    m.move_to_group("c", 0);
    CHECK(m.group_count() == 1);
    CHECK(all(m) == "abc");
    CHECK(m.group_keys().size() == 1);
}

static void test_a_reserved_group_receives_the_next_open() {
    std::printf("test_a_reserved_group_receives_the_next_open\n");
    TabGroups m = three();
    const int g = m.reserve_group(1, SplitAxis::Vertical);
    CHECK(g == 1);
    CHECK(m.axis() == SplitAxis::Vertical);
    CHECK(m.group_count() == 1);
    m.open("d", false, g);
    CHECK(all(m) == "abc|d");
    CHECK(m.group_count() == 2);
    CHECK(m.group_keys().size() == 2);
}

static void test_a_reserve_in_front_shifts_the_panes_behind_it() {
    std::printf("test_a_reserve_in_front_shifts_the_panes_behind_it\n");
    TabGroups m = three();
    const auto key = m.group_keys()[0];
    m.reserve_group(0, SplitAxis::Horizontal);
    CHECK(m.group_count() == 2);
    CHECK(m.group_keys().size() == 2);
    CHECK(m.group_keys()[1] == key);
    CHECK(ids_in(m, 1) == "abc");
    CHECK(*m.selected_in(1) == "c");
    m.open("d", false, 0);
    CHECK(all(m) == "d|abc");
}

static void test_open_lands_in_the_pane_last_acted_in() {
    std::printf("test_open_lands_in_the_pane_last_acted_in\n");
    TabGroups m = three();
    m.split_off("c", 1, SplitAxis::Horizontal);
    m.select("a");
    m.open("x");
    CHECK(all(m) == "abx|c");
    m.select("c");
    m.open("y");
    CHECK(all(m) == "abx|cy");
}

static void test_the_acted_pane_follows_a_collapse() {
    std::printf("test_the_acted_pane_follows_a_collapse\n");
    TabGroups m = three();
    m.split_off("b", 1, SplitAxis::Horizontal);
    m.split_off("c", 2, SplitAxis::Horizontal);
    CHECK(m.acted_group() == 2);
    m.close("a");
    CHECK(m.acted_group() == 1);
    m.close("c");
    CHECK(m.acted_group() == 0);
}

static void test_stepping_stays_inside_the_pane() {
    std::printf("test_stepping_stays_inside_the_pane\n");
    TabGroups m;
    for (const char* id : {"a", "b", "c", "d"}) m.open(id);
    m.split_off("d", 1, SplitAxis::Horizontal);
    m.select("a");
    CHECK(*m.step(1) == "b");
    m.select("c");
    CHECK(*m.step(1) == "c");
    CHECK(*m.step(1, true) == "a");
    CHECK(*m.step(-1, true) == "b");
}

static void test_pane_step_visits_the_neighbouring_panes_selection() {
    std::printf("test_pane_step_visits_the_neighbouring_panes_selection\n");
    TabGroups m = three();
    CHECK(!m.pane_step(1));
    m.split_off("b", 1, SplitAxis::Horizontal);
    m.split_off("c", 2, SplitAxis::Horizontal);
    m.select("a");
    CHECK(*m.pane_step(1) == "b");
    CHECK(*m.pane_step(-1) == "c");
    m.select("c");
    CHECK(*m.pane_step(1) == "a");
}

static void test_a_kept_tab_sits_in_front_of_its_pane() {
    std::printf("test_a_kept_tab_sits_in_front_of_its_pane\n");
    TabGroups m;
    m.open("a");
    m.open("b");
    m.open("c");
    m.split_off("c", 1, SplitAxis::Horizontal);
    m.open("k", false, 1);
    m.open("j", false, 1);
    const_cast<ecs::model::GroupTab*>(m.find("j"))->kept = true;
    m.move_to_group("k", 0);
    m.move_to_group("j", 0);
    CHECK(ids_in(m, 0) == "jabk");
    CHECK(m.tabs_in(0).front().kept);
}

static void test_keeping_a_tab_moves_it_to_the_front_and_unkeeping_leaves_it() {
    std::printf("test_keeping_a_tab_moves_it_to_the_front_and_unkeeping_leaves_it\n");
    TabGroups m = three();
    m.set_kept("c", true);
    CHECK(ids_in(m, 0) == "cab");
    m.set_kept("b", true);
    CHECK(ids_in(m, 0) == "cba");
    m.set_kept("c", false);
    CHECK(ids_in(m, 0) == "bca");
    m.set_kept("missing", true);
    CHECK(all(m) == "bca");
    m.open("p", true);
    m.set_kept("p", true);
    CHECK(!m.find("p")->preview);
}

static void test_a_layout_is_rebuilt_from_its_flat_state() {
    std::printf("test_a_layout_is_rebuilt_from_its_flat_state\n");
    TabGroups m = three();
    m.split_off("c", 1, SplitAxis::Vertical);
    m.select("a");
    m.select("b");
    const TabGroups back = TabGroups::from_state(m.tabs(), m.selections(), m.acted_group(), m.selected(), m.axis());
    CHECK(all(back) == all(m));
    CHECK(*back.selected() == "b");
    CHECK(*back.selected_in(1) == "c");
    CHECK(back.axis() == SplitAxis::Vertical);
    CHECK(back.group_keys().size() == 2);
}

static void test_flat_state_is_repaired_into_contiguous_numbered_panes() {
    std::printf("test_flat_state_is_repaired_into_contiguous_numbered_panes\n");
    std::vector<ecs::model::GroupTab> flat;
    for (auto [id, g] : {std::pair<const char*, int>{"x", 5}, {"y", 2}, {"z", 5}, {"dup", 2}, {"dup", 5}, {"", 0}}) {
        ecs::model::GroupTab t;
        t.id = id;
        t.group = g;
        flat.push_back(t);
    }
    const TabGroups m = TabGroups::from_state(flat, {{5, "z"}, {2, "ghost"}}, 9, std::nullopt);
    CHECK(m.group_count() == 2);
    CHECK(ids_in(m, 0) == "ydup");
    CHECK(ids_in(m, 1) == "xz");
    CHECK(*m.selected_in(0) == "y");
    CHECK(*m.selected_in(1) == "z");
    CHECK(m.selected().has_value());
    const TabGroups empty = TabGroups::from_state({}, {}, 3, std::nullopt);
    CHECK(empty.empty() && empty.group_count() == 1 && !empty.selected());
}

static void test_a_pane_moves_along_the_row_and_keeps_what_it_holds() {
    std::printf("test_a_pane_moves_along_the_row_and_keeps_what_it_holds\n");
    TabGroups m = three();
    m.split_off("b", 1, SplitAxis::Horizontal);
    m.split_off("c", 2, SplitAxis::Horizontal);
    CHECK(all(m) == "a|b|c");
    const auto keys = m.group_keys();
    CHECK(m.move_group(0, 2));
    CHECK(all(m) == "b|c|a");
    CHECK(m.group_keys()[2] == keys[0] && m.group_keys()[0] == keys[1] && m.group_keys()[1] == keys[2]);
    CHECK(*m.selected_in(2) == "a" && *m.selected_in(0) == "b");
    CHECK(m.acted_group() == 1);
    CHECK(*m.selected() == "c");
    CHECK(m.move_group(2, 0));
    CHECK(all(m) == "a|b|c");
    CHECK(!m.move_group(1, 1));
    CHECK(!m.move_group(-1, 1));
    CHECK(!m.move_group(0, 3));
    TabGroups one = three();
    CHECK(!one.move_group(0, 0));
}

static void test_merging_panes_puts_every_tab_in_the_first_and_keeps_the_selection() {
    std::printf("test_merging_panes_puts_every_tab_in_the_first_and_keeps_the_selection\n");
    TabGroups m = three();
    m.split_off("c", 1, SplitAxis::Horizontal);
    m.select("c");
    CHECK(m.merge_all());
    CHECK(all(m) == "abc");
    CHECK(*m.selected() == "c");
    CHECK(m.group_keys().size() == 1);
    CHECK(m.acted_group() == 0);
    CHECK(!m.merge_all());
}

static void test_the_axis_is_remembered_only_for_a_split() {
    std::printf("test_the_axis_is_remembered_only_for_a_split\n");
    CHECK(!TabGroups::persistable_axis(1, SplitAxis::Vertical));
    CHECK(*TabGroups::persistable_axis(2, SplitAxis::Vertical) == SplitAxis::Vertical);
    TabGroups m;
    m.adopt_axis(SplitAxis::Vertical);
    CHECK(m.axis() == SplitAxis::Vertical);
}

int main() {
    test_open_adds_to_the_acted_group_and_selects();
    test_a_preview_is_replaced_by_the_next_preview();
    test_split_right_makes_a_second_group();
    test_split_down_sets_the_axis();
    test_a_lone_tab_does_not_split_beside_itself();
    test_split_left_inserts_before_and_keeps_identities();
    test_three_columns_is_the_same_verb_again();
    test_closing_a_pane_empties_it_and_the_survivors_keep_identity();
    test_closing_the_shown_tab_returns_to_the_one_you_came_from();
    test_closing_with_no_history_takes_the_neighbour();
    test_closing_an_unshown_tab_leaves_the_selection();
    test_closing_the_last_tab_of_a_pane_lands_on_the_nearest_pane();
    test_move_to_group_joins_the_end_of_the_run();
    test_move_to_one_past_the_end_makes_a_pane();
    test_moving_the_only_tab_out_of_a_pane_collapses_it();
    test_a_reserved_group_receives_the_next_open();
    test_a_reserve_in_front_shifts_the_panes_behind_it();
    test_open_lands_in_the_pane_last_acted_in();
    test_the_acted_pane_follows_a_collapse();
    test_stepping_stays_inside_the_pane();
    test_pane_step_visits_the_neighbouring_panes_selection();
    test_a_kept_tab_sits_in_front_of_its_pane();
    test_keeping_a_tab_moves_it_to_the_front_and_unkeeping_leaves_it();
    test_a_layout_is_rebuilt_from_its_flat_state();
    test_flat_state_is_repaired_into_contiguous_numbered_panes();
    test_a_pane_moves_along_the_row_and_keeps_what_it_holds();
    test_merging_panes_puts_every_tab_in_the_first_and_keeps_the_selection();
    test_the_axis_is_remembered_only_for_a_split();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
