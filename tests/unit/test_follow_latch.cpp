// The follow-the-bottom latch (src/ecs/follow_latch.h).
//
// WHY THIS IS A UNIT TEST AND NOT A SCRIPT. The scripted suite can assert that
// the transcript moved (tests/ui/wheel_scrolls_the_transcript.e2e does exactly
// that, and is the test that would have caught the shipped bug). What it cannot
// do is tell apart the four ways the latch can be wrong, because all four look
// identical on screen: a wheel that never arrived, a wheel that arrived and was
// reverted, a latch that broke and should not have, and a latch that re-armed a
// frame too early. Those are states, so they are asserted as states.
//
// This file is written against the latch as it shipped, whose whole body was:
//
//     nearEnd = (offset + viewH >= contentH - 24);
//     if (prevOffset >= 0 && offset < prevOffset - 2) follow = false;
//     if (nearEnd) follow = true;
//
// Against that body, with the fix backed out:
//
//     test_a_wheel_notch_breaks_the_latch_on_the_frame_it_lands
//       FAIL: !v.follow (line 135)
//     test_a_wheel_gets_the_transcript_off_the_bottom
//       FAIL: s.offset < end - 20.0f (line 151)
//       FAIL: !s.mem.follow (line 152)
//       FAIL: s.offset < end - 140.0f (line 155)
//     test_trackpad_momentum_still_escapes_the_band
//       FAIL: s.offset < end - 10.0f (line 169)
//       FAIL: !s.mem.follow (line 170)
//     test_scrolling_back_to_the_bottom_re_arms
//       FAIL: !s.mem.follow (line 183)
//       FAIL: !s.mem.follow (line 189)
//     8 failed
//
// The first two are the reported bug: eight notches, zero pixels. The third is
// the same bug arriving through a trackpad, where the per-event delta is a tenth
// the size and the old band swallowed it even more completely. The fourth is the
// old code passing for the wrong reason -- it re-armed because it had never let
// go, not because the reader came back.

#include <cmath>
#include <cstdio>

#include "../../src/ecs/follow_latch.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

namespace m = ecs::model;

// A thread taller than its pane: 12,000 px of messages in a 600 px viewport.
static constexpr float kViewH = 600.0f;
static constexpr float kContentH = 12000.0f;
static constexpr float kEnd = kContentH - kViewH;  // 11400

// hanabi's smoothing (util/scroll_prefs.h) and afterhours' default speed
// (plugins/ui/components.h HasScrollView).
static constexpr float kSmoothing = 0.28f;
static constexpr float kSpeedPx = 20.0f;

// One frame of the real pipeline, in the real order.
//
//   1. MainPaneSystem reads the scroll state and steps the latch.
//   2. If it is following, it pins: offset = target = end.
//   3. afterhours' HandleScrollInput eases the offset toward the target, then
//      adds this frame's wheel delta to the target.
//
// Step 3 is `ease_scroll` followed by the wheel add, which is the order
// systems.h runs them in and matters: the notch a frame delivers is not eased
// until the frame after it.
struct Sim {
    m::FollowMemory mem;
    float offset = kEnd;
    float target = kEnd;
    float contentH = kContentH;
    bool followed = false;

    void frame(float wheelNotches) {
        m::FollowInput in{offset, target, kViewH, contentH};
        const m::FollowVerdict v = m::step_follow_latch(mem, in);
        followed = v.follow;

        if (v.follow) {
            const float end = m::follow_end(in);
            offset = end;
            target = end;
            m::note_follow_pinned(mem, offset, target);
        }

        // ease_scroll
        const float d = target - offset;
        if (d > -0.5f && d < 0.5f)
            offset = target;
        else
            offset += d * kSmoothing;

        // HandleScrollInput: the wheel writes the TARGET.
        if (wheelNotches != 0.0f) target -= wheelNotches * kSpeedPx;
        clamp();
    }

    void clamp() {
        const float maxY = std::max(0.0f, contentH - kViewH);
        offset = std::clamp(offset, 0.0f, maxY);
        target = std::clamp(target, 0.0f, maxY);
    }
};

static void test_a_fresh_thread_opens_following() {
    std::printf("test_a_fresh_thread_opens_following\n");
    Sim s;
    s.frame(0.0f);
    CHECK(s.followed);
    CHECK(std::fabs(s.offset - kEnd) < 0.5f);
}

// The reported bug, at its smallest. One notch, and the question is only
// whether the latch has let go by the time the pane looks again.
static void test_a_wheel_notch_breaks_the_latch_on_the_frame_it_lands() {
    std::printf("test_a_wheel_notch_breaks_the_latch_on_the_frame_it_lands\n");
    Sim s;
    s.frame(0.0f);   // settle, pinned
    s.frame(1.0f);   // a notch lands: target = end - 20, offset still end

    // The next frame is the one that decides. The offset has moved 5.6 px,
    // which is well inside the 24 px "at the end" band -- so a latch that reads
    // the OFFSET sees nothing and re-pins. A latch that reads the TARGET sees
    // the whole 20 px.
    m::FollowInput in{s.offset, s.target, kViewH, s.contentH};
    const m::FollowVerdict v = m::step_follow_latch(s.mem, in);
    CHECK(in.offset > kEnd - m::kFollowRearmPx);  // still inside the old band
    CHECK(in.target < kEnd - 10.0f);              // and the intent is plain
    CHECK(!v.follow);
    CHECK(v.nearEnd);  // the geometry really is near the end; that is the trap
}

// Eight notches over the transcript. The shipped build moved it zero pixels
// (transcript_bottom_pad y=646 before and after, measured); this asserts the
// same gesture as arithmetic.
static void test_a_wheel_gets_the_transcript_off_the_bottom() {
    std::printf("test_a_wheel_gets_the_transcript_off_the_bottom\n");
    Sim s;
    s.frame(0.0f);
    for (int i = 0; i < 8; i++) {
        s.frame(1.0f);
        for (int f = 0; f < 6; f++) s.frame(0.0f);  // let the glide settle
    }
    const float end = kEnd;
    CHECK(s.offset < end - 20.0f);
    CHECK(!s.mem.follow);
    // Eight notches is 160 px of target. The glide should have delivered
    // essentially all of it by now.
    CHECK(s.offset < end - 140.0f);
}

// A Mac trackpad delivers many small deltas rather than a few big ones: sokol
// scales precise scrolling deltas by 0.1 (vendor/sokol/sokol_app.h scrollWheel),
// so a gentle two-finger push is ~0.2 of a notch an event. Against a 24 px band
// read off an eased offset, each one moved 1.1 px and was erased.
static void test_trackpad_momentum_still_escapes_the_band() {
    std::printf("test_trackpad_momentum_still_escapes_the_band\n");
    Sim s;
    s.frame(0.0f);
    for (int i = 0; i < 30; i++) s.frame(0.2f);
    for (int f = 0; f < 20; f++) s.frame(0.0f);
    const float end = kEnd;
    CHECK(s.offset < end - 10.0f);
    CHECK(!s.mem.follow);
}

// Scrolling back down to the end means "follow again" -- the same thing End
// means. It must re-arm, and it must not re-arm before the reader gets there.
static void test_scrolling_back_to_the_bottom_re_arms() {
    std::printf("test_scrolling_back_to_the_bottom_re_arms\n");
    Sim s;
    s.frame(0.0f);
    for (int i = 0; i < 8; i++) {
        s.frame(1.0f);
        for (int f = 0; f < 6; f++) s.frame(0.0f);
    }
    CHECK(!s.mem.follow);  // left the bottom

    // Halfway back is still "not following": re-arming early is how the pane
    // yanks the view out from under someone who is still reading.
    s.frame(-1.0f);
    for (int f = 0; f < 6; f++) s.frame(0.0f);
    CHECK(!s.mem.follow);

    for (int i = 0; i < 10; i++) {
        s.frame(-1.0f);
        for (int f = 0; f < 6; f++) s.frame(0.0f);
    }
    CHECK(s.mem.follow);
    CHECK(std::fabs(s.offset - kEnd) < 0.5f);
}

// The latch's original job, which the fix must not undo: a message arriving
// grows the content, and growing content is not the reader scrolling up.
static void test_content_growth_does_not_break_the_latch() {
    std::printf("test_content_growth_does_not_break_the_latch\n");
    Sim s;
    s.frame(0.0f);
    for (int i = 0; i < 40; i++) {
        s.contentH += 37.0f;  // a token landing, a line at a time
        s.frame(0.0f);
        CHECK(s.mem.follow);
    }
    CHECK(std::fabs(s.offset - (s.contentH - kViewH)) < 0.5f);
}

// Content that gets SHORTER drags the target down through clamp_scroll. That is
// the layout moving, not a gesture, and reading it as one would drop the latch
// every time a thread was switched or a find filtered the view.
static void test_a_shrinking_thread_does_not_break_the_latch() {
    std::printf("test_a_shrinking_thread_does_not_break_the_latch\n");
    Sim s;
    s.frame(0.0f);
    s.contentH = 4000.0f;
    s.clamp();
    s.frame(0.0f);
    CHECK(s.mem.follow);
    s.frame(0.0f);
    CHECK(s.mem.follow);
}

// The minimap drag and the soak driver write scroll_offset directly (they set
// the target with it). The offset signal from the old code is still the one that
// catches them, so it stays.
static void test_a_direct_offset_write_breaks_the_latch() {
    std::printf("test_a_direct_offset_write_breaks_the_latch\n");
    m::FollowMemory mem;
    m::step_follow_latch(mem, {kEnd, kEnd, kViewH, kContentH});
    CHECK(mem.follow);
    // A drag to the middle of the thread: both fields, as soak.h writes them.
    const m::FollowVerdict v =
        m::step_follow_latch(mem, {kEnd / 2, kEnd / 2, kViewH, kContentH});
    CHECK(!v.follow);
    CHECK(!v.nearEnd);
}

// Sub-pixel wobble from the clamp is not a gesture.
static void test_jitter_does_not_break_the_latch() {
    std::printf("test_jitter_does_not_break_the_latch\n");
    m::FollowMemory mem;
    m::step_follow_latch(mem, {kEnd, kEnd, kViewH, kContentH});
    for (int i = 0; i < 20; i++) {
        const float w = (i % 2) ? 0.4f : 0.0f;
        const m::FollowVerdict v =
            m::step_follow_latch(mem, {kEnd - w, kEnd - w, kViewH, kContentH});
        CHECK(v.follow);
    }
}

// A thread shorter than its pane has nowhere to scroll: end is 0 and the view
// is permanently at it.
static void test_a_short_thread_is_always_at_the_end() {
    std::printf("test_a_short_thread_is_always_at_the_end\n");
    m::FollowMemory mem;
    const m::FollowVerdict v = m::step_follow_latch(mem, {0.0f, 0.0f, 600.0f, 200.0f});
    CHECK(v.follow);
    CHECK(v.nearEnd);
    CHECK(std::fabs(m::follow_end({0.0f, 0.0f, 600.0f, 200.0f})) < 0.001f);
}

int main() {
    std::printf("=== follow latch ===\n");
    test_a_fresh_thread_opens_following();
    test_a_wheel_notch_breaks_the_latch_on_the_frame_it_lands();
    test_a_wheel_gets_the_transcript_off_the_bottom();
    test_trackpad_momentum_still_escapes_the_band();
    test_scrolling_back_to_the_bottom_re_arms();
    test_content_growth_does_not_break_the_latch();
    test_a_shrinking_thread_does_not_break_the_latch();
    test_a_direct_offset_write_breaks_the_latch();
    test_jitter_does_not_break_the_latch();
    test_a_short_thread_is_always_at_the_end();

    if (g_failures) {
        std::printf("%d failed\n", g_failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
