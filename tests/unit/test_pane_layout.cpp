#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "../../src/ecs/pane_layout.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using namespace ecs::model;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static float sum(const std::vector<float>& v) {
    float t = 0.0f;
    for (float x : v) t += x;
    return t;
}

static void test_even_shares_split_the_row_equally() {
    std::printf("test_even_shares_split_the_row_equally\n");
    CHECK(even_shares(0).empty());
    CHECK(even_shares(1).size() == 1 && near(even_shares(1)[0], 1.0f));
    CHECK(near(even_shares(4)[3], 0.25f));
    CHECK(near(sum(even_shares(7)), 1.0f));
}

static void test_a_new_pane_resets_to_equal_shares() {
    std::printf("test_a_new_pane_resets_to_equal_shares\n");
    const auto two = std::vector<float>{0.7f, 0.3f};
    const auto three = shares_after_insert(two, 1);
    CHECK(three.size() == 3 && near(three[0], 1.0f / 3.0f) && near(three[2], 1.0f / 3.0f));
}

static void test_removing_a_pane_hands_its_width_to_the_rest_in_proportion() {
    std::printf("test_removing_a_pane_hands_its_width_to_the_rest_in_proportion\n");
    const auto out = shares_after_remove({0.5f, 0.3f, 0.2f}, 1);
    CHECK(out.size() == 2 && near(out[0], 0.5f / 0.7f) && near(out[1], 0.2f / 0.7f));
    CHECK(shares_after_remove({1.0f}, 0).size() == 1);
    CHECK(shares_after_remove({0.5f, 0.5f}, 9).size() == 1);
}

static void test_a_drag_moves_one_boundary_and_keeps_the_total() {
    std::printf("test_a_drag_moves_one_boundary_and_keeps_the_total\n");
    const std::vector<float> start{0.4f, 0.4f, 0.2f};
    const auto out = shares_after_drag(start, 0, 100.0f, 1000.0f, 100.0f);
    CHECK(near(out[0], 0.5f) && near(out[1], 0.3f) && near(out[2], 0.2f));
    CHECK(near(sum(out), 1.0f));
    const auto back = shares_after_drag(out, 0, -100.0f, 1000.0f, 100.0f);
    CHECK(near(back[0], 0.4f) && near(back[1], 0.4f));
}

static void test_a_drag_stops_at_the_floor_of_either_neighbour() {
    std::printf("test_a_drag_stops_at_the_floor_of_either_neighbour\n");
    const std::vector<float> start{0.5f, 0.5f};
    const auto right = shares_after_drag(start, 0, 900.0f, 1000.0f, 200.0f);
    CHECK(near(right[0], 0.8f) && near(right[1], 0.2f));
    const auto left = shares_after_drag(start, 0, -900.0f, 1000.0f, 200.0f);
    CHECK(near(left[0], 0.2f) && near(left[1], 0.8f));
    const auto squeezed = shares_after_drag(start, 0, 50.0f, 1000.0f, 900.0f);
    CHECK(near(squeezed[0], 0.5f) && near(squeezed[1], 0.5f));
}

static void test_a_drag_with_no_such_divider_changes_nothing() {
    std::printf("test_a_drag_with_no_such_divider_changes_nothing\n");
    const std::vector<float> start{0.5f, 0.5f};
    CHECK(shares_after_drag(start, 1, 10.0f, 1000.0f, 100.0f) == start);
    CHECK(shares_after_drag(start, -1, 10.0f, 1000.0f, 100.0f) == start);
    CHECK(shares_after_drag(start, 0, 10.0f, 0.0f, 100.0f) == start);
}

static void test_widths_follow_the_shares_and_grow_past_the_floor() {
    std::printf("test_widths_follow_the_shares_and_grow_past_the_floor\n");
    const auto roomy = pane_widths_for({0.5f, 0.25f, 0.25f}, 2000.0f, 480.0f);
    CHECK(near(roomy.widths[0], 1000.0f) && near(roomy.widths[1], 500.0f));
    CHECK(!roomy.overflows);
    const auto tight = pane_widths_for(even_shares(3), 1200.0f, 480.0f);
    CHECK(near(tight.widths[0], 480.0f) && near(tight.widths[2], 480.0f));
    CHECK(near(tight.total, 1440.0f) && tight.overflows);
}

static void test_bad_shares_fall_back_to_equal() {
    std::printf("test_bad_shares_fall_back_to_equal\n");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(near(normalized_shares({0.6f, 0.2f}, 2)[0], 0.75f));
    CHECK(near(normalized_shares({0.6f}, 2)[0], 0.5f));
    CHECK(near(normalized_shares({0.6f, -1.0f}, 2)[1], 0.5f));
    CHECK(near(normalized_shares({nan, 1.0f}, 2)[0], 0.5f));
    CHECK(near(normalized_shares({0.0f, 1.0f}, 2)[0], 0.5f));
    CHECK(normalized_shares({1.0f}, 0).empty());
}

static void test_the_window_needs_room_for_every_pane() {
    std::printf("test_the_window_needs_room_for_every_pane\n");
    CHECK(near(window_extent_needed(3, 5.0f, 480.0f, 300.0f), 300.0f + 1440.0f + 10.0f));
    CHECK(near(window_extent_needed(1, 5.0f, 480.0f, 300.0f), 780.0f));
    CHECK(near(window_extent_needed(0, 5.0f, 480.0f, 300.0f), 300.0f));
}

int main() {
    test_even_shares_split_the_row_equally();
    test_a_new_pane_resets_to_equal_shares();
    test_removing_a_pane_hands_its_width_to_the_rest_in_proportion();
    test_a_drag_moves_one_boundary_and_keeps_the_total();
    test_a_drag_stops_at_the_floor_of_either_neighbour();
    test_a_drag_with_no_such_divider_changes_nothing();
    test_widths_follow_the_shares_and_grow_past_the_floor();
    test_bad_shares_fall_back_to_equal();
    test_the_window_needs_room_for_every_pane();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
