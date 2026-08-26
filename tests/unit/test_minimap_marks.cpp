// The rail's mark grouping (src/ui/minimap_marks.h).
//
// WHY THIS IS A UNIT TEST AND NOT A SCRIPT. The property is "the rail never
// draws more marks than it has pixels for", and the only way a script could
// observe it is by counting entities on a thread long enough to trigger it —
// which needs a 3,000-message fixture, a census and a threshold, i.e. a gate.
// The gate exists (scripts/events_gate.sh) and measures the app. This pins the
// arithmetic underneath it, which is where the two ways it can be wrong live:
// grouping when it should not (every existing rail changes, and
// tests/ui/minimap_navigator.e2e goes red), and mis-summing the heights (every
// mark below a group points at the wrong message and nothing says so).
//
// Written against the ungrouped code, which is the defect it exists to catch.
// The injection is one line — the guard forced true, so `group_marks` always
// returns one slot per item, which is exactly what the transcript did before
// this branch:
//
//     test_a_crowded_rail_is_bounded
//       FAIL: out.size() <= static_cast<size_t>(kRailH / mm::kMinDotH) + 2 (line 98)
//       FAIL: out.size() < h.size() (line 99)
//       FAIL: mm::slot_h(out[i].height, total, kRailH) >= mm::kMinDotH (line 102)   [x1999]
//     2001 failed
//
// 2,000 items on a 700px rail came back as 2,000 marks, which is 4,000px of
// 2px dots stacked into 700px of rail, and 1,999 of them cannot draw the dot
// their slot is supposed to hold.
//
// SAY WHICH ROWS DID *NOT* GO RED, because "the test failed" is only evidence
// once you know where. `test_a_group_keeps_the_most_worth_seeing_kind` passed
// under the injection and is right to: with one slot per item, slot 0 IS item
// 0, and item 0 is the Ask. That row guards the priority rule, not the
// bounding, and it goes red on its own defect (swap two arms of
// `mark_priority`). The three "nothing changed" rows passed too, which is the
// whole point of them — they assert the behaviour the injection restores.

#include <cmath>
#include <cstdio>
#include <vector>

#include "../../src/ui/minimap_marks.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

namespace mm = hanabi::minimap;

static bool near(float a, float b) { return std::fabs(a - b) < 0.05f; }

static constexpr float kRailH = 700.0f;

// n items of equal height, all Note.
static void uniform(size_t n, float h, std::vector<float>& heights,
                    std::vector<mm::Mark>& marks) {
    heights.assign(n, h);
    marks.assign(n, mm::Mark::Note);
}

// A thread the rail can show one mark per item of. THE IMPORTANT ONE: this is
// every thread anybody has today, and the answer has to be "nothing changed".
static void test_a_rail_with_room_keeps_one_mark_per_item() {
    std::printf("test_a_rail_with_room_keeps_one_mark_per_item\n");
    std::vector<float> h;
    std::vector<mm::Mark> m;
    // 120 items, 240px of dots on a 700px rail — the shape of
    // tests/ui/minimap_navigator.e2e's 160-message fixture.
    uniform(120, 90.0f, h, m);
    const auto out = mm::group_marks(h, m, 0.0f, 120 * 90.0f, kRailH);
    CHECK(out.size() == 120);
    // Each group is exactly its item, at its own top, in order.
    for (size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].firstItem == static_cast<int>(i));
        CHECK(near(out[i].height, 90.0f));
        CHECK(near(out[i].topY, static_cast<float>(i) * 90.0f));
    }
}

// A SHORT item in a sparse thread keeps its own mark too, sub-2px slot and
// all. The guard is on the rail's total occupancy, not on each item, and this
// is the case that distinguishes the two: per-item thresholding would merge
// the 4px row into its neighbour and change a rail that works.
static void test_a_short_row_in_a_sparse_thread_keeps_its_mark() {
    std::printf("test_a_short_row_in_a_sparse_thread_keeps_its_mark\n");
    std::vector<float> h(60, 200.0f);
    std::vector<mm::Mark> m(60, mm::Mark::Reply);
    h[30] = 4.0f;  // a date divider between two long answers
    const float total = 59 * 200.0f + 4.0f;
    const auto out = mm::group_marks(h, m, 0.0f, total, kRailH);
    CHECK(out.size() == 60);
    CHECK(out[30].firstItem == 30);
    // And its slot really is under a dot high, which is the thing that is
    // being deliberately tolerated.
    CHECK(mm::slot_h(out[30].height, total, kRailH) < mm::kMinDotH);
}

// The thread that started this: more items than the rail has pixels.
static void test_a_crowded_rail_is_bounded() {
    std::printf("test_a_crowded_rail_is_bounded\n");
    std::vector<float> h;
    std::vector<mm::Mark> m;
    uniform(2000, 60.0f, h, m);
    const float total = 2000 * 60.0f;
    const auto out = mm::group_marks(h, m, 0.0f, total, kRailH);
    CHECK(out.size() <= static_cast<size_t>(kRailH / mm::kMinDotH) + 2);
    CHECK(out.size() < h.size());
    // Every group earns a drawable dot, except possibly the tail.
    for (size_t i = 0; i + 1 < out.size(); ++i)
        CHECK(mm::slot_h(out[i].height, total, kRailH) >= mm::kMinDotH);
}

// POSITION IS THE WHOLE POINT. A group's height is the sum of its items', so
// the slots still tile the rail exactly and no mark below a group drifts.
static void test_grouping_preserves_the_rail_s_arithmetic() {
    std::printf("test_grouping_preserves_the_rail_s_arithmetic\n");
    std::vector<float> h;
    std::vector<mm::Mark> m;
    uniform(2000, 60.0f, h, m);
    const float total = 2000 * 60.0f;
    const auto out = mm::group_marks(h, m, 0.0f, total, kRailH);
    float sum = 0.0f;
    for (const auto& s : out) {
        // Each group starts where the previous one ended.
        CHECK(near(s.topY, sum));
        sum += s.height;
    }
    CHECK(near(sum, total));
    // And the first group still starts at the content offset it was given,
    // which is NOT zero in the app: the sub-agent rollup sits above item 0.
    const auto lead = mm::group_marks(h, m, 137.0f, total, kRailH);
    CHECK(near(lead[0].topY, 137.0f));
}

// A group wears the rarest thing in it. Forty tool rows and one question
// collapsed together is still, to a reader scanning for their own words, a
// question.
static void test_a_group_keeps_the_most_worth_seeing_kind() {
    std::printf("test_a_group_keeps_the_most_worth_seeing_kind\n");
    std::vector<float> h(2000, 60.0f);
    std::vector<mm::Mark> m(2000, mm::Mark::Machinery);
    m[0] = mm::Mark::Ask;
    m[1] = mm::Mark::Note;
    const auto out = mm::group_marks(h, m, 0.0f, 2000 * 60.0f, kRailH);
    CHECK(out.size() > 1);
    CHECK(out[0].firstItem == 0);
    CHECK(out[0].mark == mm::Mark::Ask);
    // Ask beats Notice beats Reply beats Machinery beats Note, and the order
    // is what the group-wearing above depends on.
    CHECK(mm::mark_priority(mm::Mark::Ask) <
          mm::mark_priority(mm::Mark::Notice));
    CHECK(mm::mark_priority(mm::Mark::Notice) <
          mm::mark_priority(mm::Mark::Reply));
    CHECK(mm::mark_priority(mm::Mark::Reply) <
          mm::mark_priority(mm::Mark::Machinery));
    CHECK(mm::mark_priority(mm::Mark::Machinery) <
          mm::mark_priority(mm::Mark::Note));
}

// The tail of a crowded thread is the end of the conversation, which is where
// a reader aims most often. It gets a short slot rather than being dropped.
static void test_the_tail_is_not_dropped() {
    std::printf("test_the_tail_is_not_dropped\n");
    std::vector<float> h(1001, 60.0f);
    std::vector<mm::Mark> m(1001, mm::Mark::Note);
    const auto out = mm::group_marks(h, m, 0.0f, 1001 * 60.0f, kRailH);
    CHECK(!out.empty());
    // The last group's last item is the last item there is: nothing after the
    // final group has been silently discarded.
    float sum = 0.0f;
    for (const auto& s : out) sum += s.height;
    CHECK(near(sum, 1001 * 60.0f));
}

// Degenerate inputs answer with nothing rather than dividing by zero.
static void test_nothing_to_map() {
    std::printf("test_nothing_to_map\n");
    std::vector<float> h;
    std::vector<mm::Mark> m;
    CHECK(mm::group_marks(h, m, 0.0f, 0.0f, kRailH).empty());
    uniform(10, 60.0f, h, m);
    CHECK(mm::group_marks(h, m, 0.0f, 0.0f, kRailH).empty());
    CHECK(mm::group_marks(h, m, 0.0f, 600.0f, 0.0f).empty());
}

int main() {
    std::printf("=== minimap mark grouping ===\n");
    test_a_rail_with_room_keeps_one_mark_per_item();
    test_a_short_row_in_a_sparse_thread_keeps_its_mark();
    test_a_crowded_rail_is_bounded();
    test_grouping_preserves_the_rail_s_arithmetic();
    test_a_group_keeps_the_most_worth_seeing_kind();
    test_the_tail_is_not_dropped();
    test_nothing_to_map();
    if (g_failures == 0) {
        std::printf("all passed\n");
        return 0;
    }
    std::printf("%d failed\n", g_failures);
    return 1;
}
