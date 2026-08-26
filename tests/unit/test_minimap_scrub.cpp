// The rail's coordinate system (src/ui/minimap_scrub.h).
//
// WHY THIS IS A UNIT TEST AND NOT A SCRIPT. Dragging along the minimap is the
// inverse of drawing the scrubber band, and the way that goes wrong is DRIFT:
// the band creeps away from the cursor as the drag gets longer. A screenshot
// cannot see it (the button has to be held), and the scripted suite can only
// assert on text that rendered — which tells you the transcript moved, not
// that it moved to where the finger was. The property is arithmetic, so it is
// asserted as arithmetic.
//
// The whole file was written against a scrub that divided by the RAIL height
// instead of by the band's TRAVEL — the obvious spelling, and the one this
// pins shut (`return range / railH;` in scrub_scale):
//
//     test_a_drag_down_the_rail_covers_the_thread_exactly
//       FAIL: near(mm::scrub_offset(0.0f, travel, kRailH, kViewH, kContentH), range) (line 67)
//       FAIL: near(mm::scrub_offset(0.0f, travel * 0.5f, kRailH, kViewH, kContentH), range * 0.5f) (line 70)
//       FAIL: near(b - a, range * 0.5f) (line 76)
//     test_the_band_stays_under_the_cursor
//       FAIL: near(mm::scrubber_top(kRailH, off, kViewH, kContentH), dy) (line 89)   [x4]
//     test_a_short_thread_still_maps_end_to_end
//       FAIL: near(mm::scrub_offset(0.0f, 200.0f, railH, viewH, contentH), 400.0f) (line 125)
//     8 failed
//
// A drag down the whole rail reached 96% of the thread and stopped, and every
// round trip through the pair was short by the height of the band.

#include <cmath>
#include <cstdio>

#include "../../src/ui/minimap_scrub.h"

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

// A long thread: 700 px of rail over 48,000 px of content in a 700 px
// viewport. The band is a sliver, so the difference between dividing by the
// rail and dividing by the travel is small — which is exactly the case that
// hides the bug at the top of a drag and shows it at the bottom.
static constexpr float kRailH = 700.0f;
static constexpr float kViewH = 700.0f;
static constexpr float kContentH = 48000.0f;

static void test_the_band_is_never_a_hairline() {
    std::printf("test_the_band_is_never_a_hairline\n");
    // 700 / 48000 is 1.5% of the rail, ten pixels. The floor lifts it to 4%.
    CHECK(near(mm::scrubber_h(kRailH, kViewH, kContentH),
               kRailH * mm::kMinScrubberFrac));
    // A viewport that holds everything fills the rail rather than overflowing
    // it: the band is the whole thing and there is nowhere to travel.
    CHECK(near(mm::scrubber_h(kRailH, 900.0f, 800.0f), kRailH));
    CHECK(near(mm::scrubber_travel(kRailH, 900.0f, 800.0f), 0.0f));
}

static void test_a_drag_down_the_rail_covers_the_thread_exactly() {
    std::printf("test_a_drag_down_the_rail_covers_the_thread_exactly\n");
    const float travel = mm::scrubber_travel(kRailH, kViewH, kContentH);
    const float range = kContentH - kViewH;
    // The property the whole gesture rests on: moving the cursor by the band's
    // full travel moves the transcript by its full scroll range. Not 90% of
    // it, and not past the end.
    CHECK(near(mm::scrub_offset(0.0f, travel, kRailH, kViewH, kContentH),
               range));
    // Half the travel is half the thread.
    CHECK(near(mm::scrub_offset(0.0f, travel * 0.5f, kRailH, kViewH, kContentH),
               range * 0.5f));
    // And it is linear, so a drag has no fast or slow patch in the middle.
    const float a =
        mm::scrub_offset(0.0f, travel * 0.25f, kRailH, kViewH, kContentH);
    const float b =
        mm::scrub_offset(0.0f, travel * 0.75f, kRailH, kViewH, kContentH);
    CHECK(near(b - a, range * 0.5f));
}

static void test_the_band_stays_under_the_cursor() {
    std::printf("test_the_band_stays_under_the_cursor\n");
    // Drawing and dragging are inverses: push the transcript to where a drag
    // of `dy` says it goes, ask the drawing where the band now is, and it has
    // moved by `dy`. This is the drift test, checked at four points along the
    // rail so a mapping that is right at one end cannot pass.
    const float travel = mm::scrubber_travel(kRailH, kViewH, kContentH);
    for (float f : {0.1f, 0.35f, 0.6f, 0.95f}) {
        const float dy = travel * f;
        const float off = mm::scrub_offset(0.0f, dy, kRailH, kViewH, kContentH);
        CHECK(near(mm::scrubber_top(kRailH, off, kViewH, kContentH), dy));
    }
}

static void test_a_drag_that_goes_back_past_the_start() {
    std::printf("test_a_drag_that_goes_back_past_the_start\n");
    // Drag down a quarter, then all the way back up and past: the transcript
    // stops at the top instead of going negative, and the band with it.
    const float travel = mm::scrubber_travel(kRailH, kViewH, kContentH);
    const float anchor =
        mm::scrub_offset(0.0f, travel * 0.25f, kRailH, kViewH, kContentH);
    const float back =
        mm::scrub_offset(anchor, -travel * 2.0f, kRailH, kViewH, kContentH);
    CHECK(near(back, 0.0f));
    CHECK(near(mm::scrubber_top(kRailH, back, kViewH, kContentH), 0.0f));
}

static void test_a_thread_with_nothing_to_scroll() {
    std::printf("test_a_thread_with_nothing_to_scroll\n");
    // The rail is not drawn in this case (minimap::worth_showing), but the
    // gesture must not divide by zero if it ever is: a drag over a thread that
    // fits on screen moves nothing at all.
    CHECK(near(mm::scrub_scale(kRailH, 800.0f, 800.0f), 0.0f));
    CHECK(near(mm::scrub_offset(0.0f, 300.0f, kRailH, 800.0f, 800.0f), 0.0f));
    CHECK(near(mm::scrubber_top(kRailH, 0.0f, 800.0f, 800.0f), 0.0f));
    // And neither does one on a rail of no height.
    CHECK(near(mm::scrub_offset(0.0f, 300.0f, 0.0f, kViewH, kContentH), 0.0f));
}

static void test_a_short_thread_still_maps_end_to_end() {
    std::printf("test_a_short_thread_still_maps_end_to_end\n");
    // A band that is a third of the rail: the travel is two thirds, and the
    // naive rail-height divisor would be 50% off here rather than 4%.
    const float railH = 300.0f, viewH = 200.0f, contentH = 600.0f;
    CHECK(near(mm::scrubber_h(railH, viewH, contentH), 100.0f));
    CHECK(near(mm::scrubber_travel(railH, viewH, contentH), 200.0f));
    CHECK(near(mm::scrub_offset(0.0f, 200.0f, railH, viewH, contentH), 400.0f));
    CHECK(near(mm::scrubber_top(railH, 400.0f, viewH, contentH), 200.0f));
}

static void test_the_gesture_starts_idle() {
    std::printf("test_the_gesture_starts_idle\n");
    // A default-constructed DragState is "no gesture in progress" — the value
    // the pane holds for every thread nobody is dragging, which is all of them
    // almost all of the time.
    mm::DragState d;
    CHECK(!d.armed);
    CHECK(!d.live);
}

int main() {
    std::printf("=== minimap scrub geometry ===\n");
    test_the_band_is_never_a_hairline();
    test_a_drag_down_the_rail_covers_the_thread_exactly();
    test_the_band_stays_under_the_cursor();
    test_a_drag_that_goes_back_past_the_start();
    test_a_thread_with_nothing_to_scroll();
    test_a_short_thread_still_maps_end_to_end();
    test_the_gesture_starts_idle();
    if (g_failures == 0) {
        std::printf("all passed\n");
        return 0;
    }
    std::printf("%d failed\n", g_failures);
    return 1;
}
