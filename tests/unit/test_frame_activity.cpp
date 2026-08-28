#include <cstdio>

#include "../../src/frame_activity.h"

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                                \
        }                                                              \
    } while (0)

using hanabi::FrameActivity;
using hanabi::FrameActivityPolicy;
using hanabi::FrameCadence;
using hanabi::FrameSignals;

static int run_for_one_second(FrameSignals signals) {
    FrameActivityPolicy policy;
    int frames = 0;
    for (std::uint64_t now = 0; now < 1000000; now += 8333) {
        auto d = policy.decide(now, signals);
        if (!d.render) continue;
        ++frames;
        policy.rendered(now);
    }
    return frames;
}

static void startup_draws_immediately() {
    FrameActivityPolicy policy;
    auto d = policy.decide(0, {});
    CHECK(d.render);
    CHECK(hanabi::contains(d.activity, FrameActivity::Startup));
}

static void fully_idle_draws_two_safety_frames_per_second() {
    const int frames = run_for_one_second({});
    CHECK(frames >= 2);
    CHECK(frames <= 3);
}

static void pointer_input_wakes_on_the_next_callback() {
    FrameActivityPolicy policy;
    CHECK(policy.decide(0, {}).render);
    policy.rendered(0);
    CHECK(!policy.decide(8333, {}).render);
    FrameSignals s;
    s.pointer_input = true;
    auto d = policy.decide(16666, s);
    CHECK(d.render);
    CHECK(d.cadence == FrameCadence::Idle);
}

static void key_input_wakes_on_the_next_callback() {
    FrameActivityPolicy policy;
    policy.rendered(0);
    FrameSignals s;
    s.key_input = true;
    CHECK(policy.decide(8333, s).render);
}

static void active_work_stays_at_sixty_frames_per_second() {
    for (int kind = 0; kind < 5; ++kind) {
        FrameSignals s;
        if (kind == 0) s.animation = true;
        if (kind == 1) s.streaming = true;
        if (kind == 2) s.thinking = true;
        if (kind == 3) s.scrolling = true;
        if (kind == 4) s.dragging = true;
        const int frames = run_for_one_second(s);
        CHECK(frames >= 59);
        CHECK(frames <= 61);
    }
}

static void periodic_work_stays_at_ten_frames_per_second() {
    for (int kind = 0; kind < 3; ++kind) {
        FrameSignals s;
        if (kind == 0) s.caret = true;
        if (kind == 1) s.timer = true;
        if (kind == 2) s.pending_future = true;
        const int frames = run_for_one_second(s);
        CHECK(frames >= 10);
        CHECK(frames <= 11);
    }
}

static void external_transitions_wake_immediately() {
    for (int kind = 0; kind < 7; ++kind) {
        FrameActivityPolicy policy;
        policy.rendered(0);
        FrameSignals s;
        if (kind == 0) s.window_resize = true;
        if (kind == 1) s.window_exposure = true;
        if (kind == 2) s.native_notification = true;
        if (kind == 3) s.async_ready = true;
        if (kind == 4) s.sse_event = true;
        if (kind == 5) s.state_request = true;
        if (kind == 6) s.split_change = true;
        CHECK(policy.decide(8333, s).render);
    }
}

static void legacy_mode_rebuilds_every_callback() {
    FrameActivityPolicy policy(false);
    int frames = 0;
    for (std::uint64_t now = 0; now < 1000000; now += 8333) {
        if (policy.decide(now, {}).render) {
            ++frames;
            policy.rendered(now);
        }
    }
    CHECK(frames >= 120);
}

int main() {
    startup_draws_immediately();
    fully_idle_draws_two_safety_frames_per_second();
    pointer_input_wakes_on_the_next_callback();
    key_input_wakes_on_the_next_callback();
    active_work_stays_at_sixty_frames_per_second();
    periodic_work_stays_at_ten_frames_per_second();
    external_transitions_wake_immediately();
    legacy_mode_rebuilds_every_callback();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
