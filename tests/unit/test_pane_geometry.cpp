#include <cmath>
#include <cstdio>
#include <vector>

#include "../../src/ecs/pane_geometry.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using namespace ecs::model;

static void test_two_panes_tile_the_row_with_the_divider_between_them() {
    std::printf("test_two_panes_tile_the_row_with_the_divider_between_them\n");
    const auto w = pane_widths(2, 0.5f, {}, 820.0f);
    CHECK(w.size() == 2 && w[0] == 408.0f && w[1] == 407.0f);
    CHECK(w[0] + kDividerW + w[1] == 820.0f);
}

static void test_a_ratio_is_held_at_the_minimum_pane() {
    std::printf("test_a_ratio_is_held_at_the_minimum_pane\n");
    const auto w = pane_widths(2, 0.1f, {}, 820.0f);
    CHECK(w[0] == 280.0f && w[1] == 535.0f);
    const auto r = pane_widths(2, 0.95f, {}, 820.0f);
    CHECK(r[1] == 280.0f && r[0] == 535.0f);
}

static void test_a_row_too_narrow_for_two_gives_the_left_pane_everything() {
    std::printf("test_a_row_too_narrow_for_two_gives_the_left_pane_everything\n");
    CHECK(!split_fits(479.0f) && split_fits(480.0f));
    const auto w = pane_widths(2, 0.5f, {}, 400.0f);
    CHECK(w[0] == 395.0f && w[1] == 0.0f);
    const auto m = pane_widths(2, 0.5f, {}, 500.0f);
    CHECK(m[0] == 248.0f && m[1] == 247.0f);
}

static void test_one_pane_takes_the_row() {
    std::printf("test_one_pane_takes_the_row\n");
    const auto w = pane_widths(1, 0.5f, {}, 820.0f);
    CHECK(w.size() == 1 && w[0] == 820.0f);
}

static void test_three_panes_share_by_their_shares() {
    std::printf("test_three_panes_share_by_their_shares\n");
    const auto w = pane_widths(3, 0.5f, {0.5f, 0.25f, 0.25f}, 2010.0f);
    CHECK(w.size() == 3 && w[0] == 1000.0f && w[1] == 500.0f && w[2] == 500.0f);
    const auto even = pane_widths(3, 0.5f, {}, 1510.0f);
    CHECK(even.size() == 3 && std::fabs(even[0] - 500.0f) < 0.01f);
}

static void test_three_panes_never_go_under_the_tiled_floor() {
    std::printf("test_three_panes_never_go_under_the_tiled_floor\n");
    const auto w = pane_widths(3, 0.5f, {}, 1200.0f);
    CHECK(w.size() == 3 && w[0] == 480.0f && w[1] == 480.0f && w[2] == 480.0f);
    const auto skew = pane_widths(3, 0.5f, {0.8f, 0.1f, 0.1f}, 2010.0f);
    CHECK(skew[1] == 480.0f && skew[2] == 480.0f && skew[0] > 1000.0f);
    CHECK(kPaneMinWidth == 480.0f);
}

static void test_origins_advance_by_width_and_divider() {
    std::printf("test_origins_advance_by_width_and_divider\n");
    const auto o = pane_origins({408.0f, 407.0f}, 280.0f);
    CHECK(o.size() == 2 && o[0] == 280.0f && o[1] == 693.0f);
    CHECK(pane_origins({}, 1.0f).empty());
}

int main() {
    test_two_panes_tile_the_row_with_the_divider_between_them();
    test_a_ratio_is_held_at_the_minimum_pane();
    test_a_row_too_narrow_for_two_gives_the_left_pane_everything();
    test_one_pane_takes_the_row();
    test_three_panes_share_by_their_shares();
    test_three_panes_never_go_under_the_tiled_floor();
    test_origins_advance_by_width_and_divider();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
