#pragma once

#include <cstdint>

namespace hanabi {

enum class FrameActivity : std::uint32_t {
    None = 0,
    Startup = 1u << 0,
    PointerInput = 1u << 1,
    KeyInput = 1u << 2,
    WindowResize = 1u << 3,
    WindowExposure = 1u << 4,
    NativeNotification = 1u << 5,
    AsyncReady = 1u << 6,
    SseEvent = 1u << 7,
    StateRequest = 1u << 8,
    SplitChange = 1u << 9,
    Animation = 1u << 10,
    Streaming = 1u << 11,
    Thinking = 1u << 12,
    Scrolling = 1u << 13,
    Dragging = 1u << 14,
    Caret = 1u << 15,
    Timer = 1u << 16,
    PendingFuture = 1u << 17,
    IdlePulse = 1u << 18,
};

constexpr FrameActivity operator|(FrameActivity a, FrameActivity b) {
    return static_cast<FrameActivity>(static_cast<std::uint32_t>(a) |
                                      static_cast<std::uint32_t>(b));
}

constexpr FrameActivity& operator|=(FrameActivity& a, FrameActivity b) {
    a = a | b;
    return a;
}

constexpr bool any(FrameActivity activity) {
    return activity != FrameActivity::None;
}

constexpr bool contains(FrameActivity activity, FrameActivity flag) {
    return (static_cast<std::uint32_t>(activity) &
            static_cast<std::uint32_t>(flag)) != 0;
}

enum class FrameCadence { Idle, Periodic, Active };

struct FrameSignals {
    bool pointer_input = false;
    bool key_input = false;
    bool window_resize = false;
    bool window_exposure = false;
    bool native_notification = false;
    bool async_ready = false;
    bool sse_event = false;
    bool state_request = false;
    bool split_change = false;
    bool animation = false;
    bool streaming = false;
    bool thinking = false;
    bool scrolling = false;
    bool dragging = false;
    bool caret = false;
    bool timer = false;
    bool pending_future = false;
};

struct FrameDecision {
    bool render = false;
    FrameCadence cadence = FrameCadence::Idle;
    FrameActivity activity = FrameActivity::None;
};

class FrameActivityTransitions {
  public:
    FrameSignals observe(bool search_open, std::uint64_t cache_epoch) {
        FrameSignals signals;
        signals.animation = search_open;
        if (initialized_) {
            signals.state_request =
                (search_open_ && !search_open) || cache_epoch_ != cache_epoch;
        }
        initialized_ = true;
        search_open_ = search_open;
        cache_epoch_ = cache_epoch;
        return signals;
    }

  private:
    bool initialized_ = false;
    bool search_open_ = false;
    std::uint64_t cache_epoch_ = 0;
};

class FrameActivityPolicy {
  public:
    static constexpr std::uint64_t kActiveIntervalUs = 16666;
    static constexpr std::uint64_t kPeriodicIntervalUs = 100000;
    static constexpr std::uint64_t kIdleIntervalUs = 500000;

    explicit FrameActivityPolicy(bool enabled = true) : enabled_(enabled) {}

    FrameDecision decide(std::uint64_t now_us, const FrameSignals& s) {
        FrameDecision out;
        add_signals(out, s);
        out.cadence = cadence_for(s);

        if (!enabled_) {
            out.render = true;
            return out;
        }
        if (!started_) {
            out.render = true;
            out.activity |= FrameActivity::Startup;
            return out;
        }
        if (has_immediate_wake(s)) {
            out.render = true;
            return out;
        }

        const std::uint64_t interval = interval_for(out.cadence);
        out.render = now_us - last_frame_us_ >= interval;
        if (out.render && out.cadence == FrameCadence::Idle)
            out.activity |= FrameActivity::IdlePulse;
        return out;
    }

    void rendered(std::uint64_t now_us) {
        started_ = true;
        last_frame_us_ = now_us;
    }

    bool started() const { return started_; }
    std::uint64_t last_frame_us() const { return last_frame_us_; }

  private:
    static bool has_immediate_wake(const FrameSignals& s) {
        return s.pointer_input || s.key_input || s.window_resize ||
               s.window_exposure || s.native_notification || s.async_ready ||
               s.sse_event || s.state_request || s.split_change;
    }

    static FrameCadence cadence_for(const FrameSignals& s) {
        if (s.animation || s.streaming || s.thinking || s.scrolling ||
            s.dragging)
            return FrameCadence::Active;
        if (s.caret || s.timer || s.pending_future)
            return FrameCadence::Periodic;
        return FrameCadence::Idle;
    }

    static std::uint64_t interval_for(FrameCadence cadence) {
        if (cadence == FrameCadence::Active) return kActiveIntervalUs;
        if (cadence == FrameCadence::Periodic) return kPeriodicIntervalUs;
        return kIdleIntervalUs;
    }

    static void add_signals(FrameDecision& out, const FrameSignals& s) {
        if (s.pointer_input) out.activity |= FrameActivity::PointerInput;
        if (s.key_input) out.activity |= FrameActivity::KeyInput;
        if (s.window_resize) out.activity |= FrameActivity::WindowResize;
        if (s.window_exposure) out.activity |= FrameActivity::WindowExposure;
        if (s.native_notification)
            out.activity |= FrameActivity::NativeNotification;
        if (s.async_ready) out.activity |= FrameActivity::AsyncReady;
        if (s.sse_event) out.activity |= FrameActivity::SseEvent;
        if (s.state_request) out.activity |= FrameActivity::StateRequest;
        if (s.split_change) out.activity |= FrameActivity::SplitChange;
        if (s.animation) out.activity |= FrameActivity::Animation;
        if (s.streaming) out.activity |= FrameActivity::Streaming;
        if (s.thinking) out.activity |= FrameActivity::Thinking;
        if (s.scrolling) out.activity |= FrameActivity::Scrolling;
        if (s.dragging) out.activity |= FrameActivity::Dragging;
        if (s.caret) out.activity |= FrameActivity::Caret;
        if (s.timer) out.activity |= FrameActivity::Timer;
        if (s.pending_future) out.activity |= FrameActivity::PendingFuture;
    }

    bool enabled_ = true;
    bool started_ = false;
    std::uint64_t last_frame_us_ = 0;
};

}
