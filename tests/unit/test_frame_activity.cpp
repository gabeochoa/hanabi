#include <cstddef>
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

static void fixed_ten_fps_prototype_adds_visible_input_latency() {
    FrameActivityPolicy policy;
    policy.rendered(0);
    FrameSignals fixed;
    fixed.timer = true;
    std::uint64_t firstFrame = 0;
    for (std::uint64_t now = 8333; now <= 110000; now += 8333) {
        if (!policy.decide(now, fixed).render) continue;
        firstFrame = now;
        break;
    }
    CHECK(firstFrame >= 100000);

    FrameActivityPolicy eventDriven;
    eventDriven.rendered(0);
    FrameSignals input;
    input.key_input = true;
    CHECK(eventDriven.decide(8333, input).render);
}

static void caret_thinking_scroll_and_stream_keep_their_cadence() {
    FrameSignals caret;
    caret.caret = true;
    CHECK(run_for_one_second(caret) >= 10);

    for (int kind = 0; kind < 3; ++kind) {
        FrameSignals active;
        if (kind == 0) active.thinking = true;
        if (kind == 1) active.scrolling = true;
        if (kind == 2) active.streaming = true;
        const int frames = run_for_one_second(active);
        CHECK(frames >= 59);
        CHECK(frames <= 61);
    }
}

static void split_and_native_notification_wake_in_one_callback() {
    FrameActivityPolicy splitPolicy;
    splitPolicy.rendered(0);
    FrameSignals split;
    split.split_change = true;
    CHECK(splitPolicy.decide(8333, split).render);

    FrameActivityPolicy notificationPolicy;
    notificationPolicy.rendered(0);
    FrameSignals notification;
    notification.native_notification = true;
    CHECK(notificationPolicy.decide(8333, notification).render);
}

static void lazy_cache_and_search_transitions_wake() {
    hanabi::FrameActivityTransitions transitions;
    auto initial = transitions.observe(false, 7);
    CHECK(!initial.state_request);
    CHECK(!initial.animation);

    auto searchOpen = transitions.observe(true, 7);
    CHECK(searchOpen.animation);
    auto searchStillOpen = transitions.observe(true, 7);
    CHECK(searchStillOpen.animation);
    CHECK(!searchStillOpen.state_request);

    auto searchReleased = transitions.observe(false, 7);
    CHECK(searchReleased.state_request);
    CHECK(!searchReleased.animation);
    CHECK(!transitions.observe(false, 7).state_request);

    auto cacheWiped = transitions.observe(false, 8, 0);
    CHECK(cacheWiped.state_request);
    CHECK(!transitions.observe(false, 8, 0).state_request);

    auto shortcutChanged = transitions.observe(false, 8, 1, 0);
    CHECK(shortcutChanged.state_request);
    CHECK(!transitions.observe(false, 8, 1, 0).state_request);

    auto fontChanged = transitions.observe(false, 8, 1, 1);
    CHECK(fontChanged.state_request);
    FrameActivityPolicy fontPolicy;
    fontPolicy.rendered(0);
    CHECK(fontPolicy.decide(8333, fontChanged).render);
    CHECK(!transitions.observe(false, 8, 1, 1).state_request);
}

static void lifecycle_transitions_wake_without_idle_delay() {
    for (int kind = 0; kind < 8; ++kind) {
        hanabi::LifecycleFrameState state;
        if (kind == 0) state.fork_request = true;
        if (kind == 1) state.fork_pending = true;
        if (kind == 2) state.fork_ready = true;
        if (kind == 3) state.subagent_request = true;
        if (kind == 4) state.subagent_pending = true;
        if (kind == 5) state.subagent_ready = true;
        if (kind == 6) state.sse_dirty = true;
        if (kind == 7) state.mute_toggle = true;
        const FrameSignals signals = hanabi::lifecycle_frame_signals(state);
        FrameActivityPolicy policy;
        policy.rendered(0);
        if (state.fork_pending || state.subagent_pending) {
            CHECK(signals.pending_future);
            CHECK(policy.decide(8333, signals).cadence ==
                  FrameCadence::Periodic);
        } else {
            CHECK(policy.decide(8333, signals).render);
        }
    }

    const FrameSignals toast =
        hanabi::lifecycle_frame_signals({.toast_active = true});
    CHECK(toast.timer);
    FrameActivityPolicy actionPolicy;
    actionPolicy.rendered(0);
    FrameSignals pointer;
    pointer.pointer_input = true;
    CHECK(actionPolicy.decide(8333, pointer).render);
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

static void a_tooltip_dwell_gets_frames_to_finish_counting() {
    FrameSignals idle;
    FrameActivityPolicy idlePolicy;
    float idleHeld = 0.0f;
    std::uint64_t idleLast = 0;
    bool idleRevealed = false;
    std::uint64_t idleRevealUs = 0;
    for (std::uint64_t now = 0; now < 5000000 && !idleRevealed; now += 8333) {
        auto d = idlePolicy.decide(now, idle);
        if (!d.render) continue;
        const float dt =
            idleLast == 0 ? 0.0f
                          : static_cast<float>(now - idleLast) / 1000000.0f;
        idleLast = now;
        idleHeld += dt > 0.1f ? 0.1f : dt;
        idlePolicy.rendered(now);
        if (idleHeld >= 0.5f) {
            idleRevealed = true;
            idleRevealUs = now;
        }
    }
    CHECK(idleRevealed);
    CHECK(idleRevealUs > 1500000);

    FrameSignals dwell;
    dwell.tooltip_dwell = true;
    FrameActivityPolicy dwellPolicy;
    float held = 0.0f;
    std::uint64_t last = 0;
    bool revealed = false;
    std::uint64_t revealUs = 0;
    for (std::uint64_t now = 0; now < 5000000 && !revealed; now += 8333) {
        auto d = dwellPolicy.decide(now, dwell);
        if (!d.render) continue;
        const float dt =
            last == 0 ? 0.0f : static_cast<float>(now - last) / 1000000.0f;
        last = now;
        held += dt > 0.1f ? 0.1f : dt;
        dwellPolicy.rendered(now);
        if (held >= 0.5f) {
            revealed = true;
            revealUs = now;
        }
    }
    CHECK(revealed);
    CHECK(revealUs >= 500000);
    CHECK(revealUs <= 560000);

    FrameSignals settled;
    CHECK(run_for_one_second(settled) <= 4);
}

static void the_windowed_merge_carries_every_signal() {
    FrameSignals collected;
    collected.tooltip_dwell = true;

    FrameSignals frame;
    frame.merge_from(collected);
    CHECK(frame.tooltip_dwell);

    // What app_frame() actually does: a fresh frame's own input signals, then
    // the app's collected ones merged in. Every field the collector can set
    // must survive that, which is what the field-by-field copy failed at.
    const auto every_field = [](bool value) {
        FrameSignals s;
        s.pointer_input = value; s.key_input = value; s.window_resize = value;
        s.window_exposure = value; s.native_notification = value;
        s.async_ready = value; s.sse_event = value; s.state_request = value;
        s.split_change = value; s.animation = value; s.streaming = value;
        s.thinking = value; s.scrolling = value; s.dragging = value;
        s.caret = value; s.timer = value; s.pending_future = value;
        s.tooltip_dwell = value;
        return s;
    };
    FrameSignals empty;
    FrameSignals merged;
    merged.merge_from(every_field(true));
    const auto* on = reinterpret_cast<const unsigned char*>(&merged);
    const auto* off = reinterpret_cast<const unsigned char*>(&empty);
    for (std::size_t i = 0; i < sizeof(FrameSignals); ++i) {
        CHECK(on[i] != 0);
        CHECK(off[i] == 0);
    }

    FrameSignals pointerOnly;
    pointerOnly.pointer_input = true;
    pointerOnly.merge_from(FrameSignals{});
    CHECK(pointerOnly.pointer_input);
}

static void a_hovered_tooltip_reveals_on_the_windowed_cadence() {
    // The windowed loop's own arithmetic: a real steady clock, a callback that
    // only runs when the policy says render, and main.cpp's 0.1s dt clamp.
    const auto reveal_ms = [](bool dwell_reaches_the_frame_loop) {
        FrameActivityPolicy policy;
        float held = 0.0f;
        std::uint64_t last = 0;
        for (std::uint64_t now = 0; now < 5000000; now += 8333) {
            FrameSignals frame;
            FrameSignals collected;
            collected.tooltip_dwell = held < 0.5f;
            if (dwell_reaches_the_frame_loop) {
                frame.merge_from(collected);
            } else {
                frame.caret = collected.caret;
                frame.timer = collected.timer;
                frame.pending_future = collected.pending_future;
            }
            const auto d = policy.decide(now, frame);
            if (!d.render) continue;
            const float dt =
                last == 0 ? 0.0f
                          : static_cast<float>(now - last) / 1000000.0f;
            last = now;
            held += dt > 0.1f ? 0.1f : dt;
            policy.rendered(now);
            if (held >= 0.5f) return static_cast<int>(now / 1000);
        }
        return -1;
    };

    const int wired = reveal_ms(true);
    CHECK(wired >= 500);
    CHECK(wired <= 560);

    const int dropped = reveal_ms(false);
    CHECK(dropped > 1500);
}

int main() {
    startup_draws_immediately();
    fully_idle_draws_two_safety_frames_per_second();
    pointer_input_wakes_on_the_next_callback();
    key_input_wakes_on_the_next_callback();
    active_work_stays_at_sixty_frames_per_second();
    periodic_work_stays_at_ten_frames_per_second();
    external_transitions_wake_immediately();
    fixed_ten_fps_prototype_adds_visible_input_latency();
    caret_thinking_scroll_and_stream_keep_their_cadence();
    split_and_native_notification_wake_in_one_callback();
    lazy_cache_and_search_transitions_wake();
    lifecycle_transitions_wake_without_idle_delay();
    legacy_mode_rebuilds_every_callback();
    a_tooltip_dwell_gets_frames_to_finish_counting();
    the_windowed_merge_carries_every_signal();
    a_hovered_tooltip_reveals_on_the_windowed_cadence();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
