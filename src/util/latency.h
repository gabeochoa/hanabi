#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hanabi::latency {

enum class Outcome {
    UiAppears,
    UiDisappears,
    TextAppears,
    TextDisappears,
    FocusGained,
    FocusLost,
    PressHeld,
    HoverHeld,
};

enum class Phase {
    AwaitingBaseline,
    AwaitingEvent,
    AwaitingInk,
    Settled,
    Failed,
};

inline constexpr int kInkTolerance = 24;
inline constexpr int kInkFloor = 40;
inline constexpr long kDeadlineFrames = 600;

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;

    static Frame from_rgba(int width, int height, const std::uint8_t* rgba,
                           std::size_t bytes) {
        Frame out;
        const std::size_t pixels = width > 0 && height > 0
                                       ? static_cast<std::size_t>(width) *
                                             static_cast<std::size_t>(height)
                                       : 0;
        if (rgba == nullptr || pixels == 0 || bytes != pixels * 4) return out;
        out.width = width;
        out.height = height;
        out.luma.resize(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            const std::size_t p = i * 4;
            out.luma[i] = static_cast<std::uint8_t>(
                (77u * rgba[p] + 150u * rgba[p + 1] + 29u * rgba[p + 2]) >> 8u);
        }
        return out;
    }

    bool valid() const {
        return width > 0 && height > 0 &&
               luma.size() == static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height);
    }

    bool flat() const {
        return !valid() ||
               std::all_of(luma.begin() + 1, luma.end(),
                           [first = luma.front()](std::uint8_t value) {
                               return value == first;
                           });
    }

    int background() const {
        std::array<int, 256> histogram{};
        for (std::uint8_t value : luma) ++histogram[value];
        return static_cast<int>(std::distance(
            histogram.begin(),
            std::max_element(histogram.begin(), histogram.end())));
    }

    int ink() const {
        if (!valid()) return 0;
        const int ground = background();
        return static_cast<int>(std::count_if(
            luma.begin(), luma.end(), [ground](std::uint8_t value) {
                return std::abs(static_cast<int>(value) - ground) >
                       kInkTolerance;
            }));
    }
};

struct Difference {
    int changed = 0;
    int ink_gained = 0;
    int ink_lost = 0;
};

inline std::optional<Difference> difference(const Frame& before,
                                            const Frame& after) {
    if (!before.valid() || !after.valid() || before.width != after.width ||
        before.height != after.height ||
        before.luma.size() != after.luma.size())
        return std::nullopt;
    const int beforeGround = before.background();
    const int afterGround = after.background();
    Difference out;
    for (std::size_t i = 0; i < before.luma.size(); ++i) {
        const int a = before.luma[i];
        const int b = after.luma[i];
        if (std::abs(a - b) <= kInkTolerance) continue;
        ++out.changed;
        const bool beforeInk = std::abs(a - beforeGround) > kInkTolerance;
        const bool afterInk = std::abs(b - afterGround) > kInkTolerance;
        if (!beforeInk && afterInk) ++out.ink_gained;
        if (beforeInk && !afterInk) ++out.ink_lost;
    }
    return out;
}

struct Reading {
    std::uint64_t event_to_ink_us = 0;
    std::uint64_t probe_us = 0;
    long frames = -1;
    int changed = 0;
    int ink_gained = 0;
    int ink_lost = 0;
    int required_ink = 0;
    int ink_before = 0;
    int ink_after = 0;
};

struct Watch {
    std::string label;
    Outcome outcome = Outcome::UiAppears;
    std::string target;
    Phase phase = Phase::AwaitingBaseline;
    Frame baseline;
    std::uint64_t event_at_us = 0;
    long event_frame = -1;
    long first_probe_frame = -1;
    bool outcome_seen = false;
    bool ink_seen = false;
    std::optional<Reading> reading;
    std::string failure;
    bool reported = false;
};

inline std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

enum class InkDirection { Gained, Lost };

inline InkDirection ink_direction(Outcome outcome) {
    switch (outcome) {
        case Outcome::UiAppears:
        case Outcome::TextAppears:
        case Outcome::FocusGained:
        case Outcome::PressHeld:
        case Outcome::HoverHeld:
            return InkDirection::Gained;
        case Outcome::UiDisappears:
        case Outcome::TextDisappears:
        case Outcome::FocusLost:
            return InkDirection::Lost;
    }
    std::unreachable();
}

inline const char* ink_direction_name(Outcome outcome) {
    return ink_direction(outcome) == InkDirection::Gained ? "gained" : "lost";
}

inline int directional_ink(Outcome outcome, const Difference& difference) {
    return ink_direction(outcome) == InkDirection::Gained
               ? difference.ink_gained
               : difference.ink_lost;
}

inline bool expects_present(Outcome outcome) {
    return outcome == Outcome::UiAppears || outcome == Outcome::TextAppears ||
           outcome == Outcome::FocusGained || outcome == Outcome::PressHeld ||
           outcome == Outcome::HoverHeld;
}

inline std::vector<Watch>& watches() {
    static std::vector<Watch> value;
    return value;
}

inline long& presented_frames() {
    static long value = 0;
    return value;
}

inline Watch* find(std::string_view label) {
    for (auto& watch : watches())
        if (watch.label == label) return &watch;
    return nullptr;
}

inline Watch* active() {
    for (auto& watch : watches())
        if (!watch.reported) return &watch;
    return nullptr;
}

inline std::optional<std::string> arm(std::string label, Outcome outcome,
                                      std::string target, bool target_present) {
    if (find(label) != nullptr)
        return std::string("duplicate arm '") + label + "'";
    if (active() != nullptr)
        return std::string("another latency arm is still active");
    if (target_present == expects_present(outcome)) {
        return target_present
                   ? std::string("'") + target + "' is ALREADY present"
                   : std::string("'") + target + "' is ALREADY absent";
    }
    Watch watch;
    watch.label = std::move(label);
    watch.outcome = outcome;
    watch.target = std::move(target);
    watches().push_back(std::move(watch));
    return std::nullopt;
}

enum class EventResult { Ignored, Fired, Failed };

inline EventResult event_queued(std::uint64_t timestamp_us = now_us()) {
    Watch* watch = active();
    if (watch == nullptr || watch->phase == Phase::Settled ||
        watch->phase == Phase::Failed)
        return EventResult::Ignored;
    if (watch->phase == Phase::AwaitingBaseline) {
        watch->phase = Phase::Failed;
        watch->failure = "input event arrived before the pixel baseline";
        return EventResult::Failed;
    }
    if (watch->phase != Phase::AwaitingEvent) return EventResult::Ignored;
    watch->event_at_us = timestamp_us;
    watch->event_frame = presented_frames();
    watch->phase = Phase::AwaitingInk;
    return EventResult::Fired;
}

inline bool awaiting_delivery() {
    const Watch* watch = active();
    return watch != nullptr && watch->phase == Phase::AwaitingInk &&
           watch->first_probe_frame < 0;
}

inline void input_delivered(int effect_lag_frames = 0) {
    Watch* watch = active();
    if (watch != nullptr && watch->phase == Phase::AwaitingInk)
        watch->first_probe_frame =
            presented_frames() + 1 + std::max(0, effect_lag_frames);
}

inline bool needs_probe() {
    const Watch* watch = active();
    return watch != nullptr &&
           (watch->phase == Phase::AwaitingBaseline ||
            (watch->phase == Phase::AwaitingInk &&
             watch->first_probe_frame >= 0 &&
             presented_frames() + 1 >= watch->first_probe_frame &&
             !watch->reading.has_value()));
}

inline void fail(Watch& watch, std::string message) {
    watch.phase = Phase::Failed;
    watch.failure = std::move(message);
    watch.baseline = {};
}

template<typename OutcomeFn>
inline void presented(std::optional<Frame> frame, std::uint64_t confirmed_at_us,
                      std::uint64_t probe_us, OutcomeFn&& outcome_reached) {
    ++presented_frames();
    Watch* watch = active();
    if (watch == nullptr || watch->phase == Phase::Settled ||
        watch->phase == Phase::Failed)
        return;

    if (watch->phase == Phase::AwaitingBaseline) {
        if (!frame.has_value() || !frame->valid() || frame->flat() ||
            frame->ink() < kInkFloor) {
            fail(*watch, "pixel baseline was unreadable");
            return;
        }
        watch->baseline = std::move(*frame);
        watch->phase = Phase::AwaitingEvent;
        return;
    }

    if (watch->phase == Phase::AwaitingEvent) return;

    watch->outcome_seen = watch->outcome_seen || outcome_reached(*watch);
    if (watch->first_probe_frame < 0 ||
        presented_frames() < watch->first_probe_frame)
        return;
    if (!watch->reading.has_value()) {
        if (!frame.has_value() || !frame->valid() || frame->flat()) {
            fail(*watch, "first-ink pixel probe was unreadable");
            return;
        }
        const auto delta = difference(watch->baseline, *frame);
        if (!delta.has_value()) {
            fail(*watch, "first-ink pixel probe changed shape");
            return;
        }
        const int requiredInk = directional_ink(watch->outcome, *delta);
        const bool inkNow =
            requiredInk >= kInkFloor && frame->ink() >= kInkFloor;
        watch->ink_seen = watch->ink_seen || inkNow;
        if (inkNow && watch->outcome_seen) {
            Reading reading;
            reading.event_to_ink_us = confirmed_at_us - watch->event_at_us;
            reading.probe_us = probe_us;
            reading.frames = presented_frames() - watch->event_frame;
            reading.changed = delta->changed;
            reading.ink_gained = delta->ink_gained;
            reading.ink_lost = delta->ink_lost;
            reading.required_ink = requiredInk;
            reading.ink_before = watch->baseline.ink();
            reading.ink_after = frame->ink();
            watch->reading = reading;
            watch->baseline = {};
        }
    }

    if (watch->reading.has_value() && watch->outcome_seen) {
        watch->phase = Phase::Settled;
        return;
    }
    if (presented_frames() - watch->event_frame > kDeadlineFrames) {
        fail(*watch, watch->ink_seen
                         ? "first ink appeared but the required outcome did not"
                         : "the deadline passed without first ink");
    }
}

inline std::string unresolved_reason(const Watch& watch) {
    if (watch.phase == Phase::Failed) return watch.failure;
    if (watch.phase == Phase::AwaitingBaseline)
        return "the pixel baseline was never presented";
    if (watch.phase == Phase::AwaitingEvent)
        return "no input event timestamp was recorded";
    if (!watch.ink_seen) return "no first ink was rendered";
    if (!watch.outcome_seen)
        return "first ink rendered but the required outcome did not";
    if (!watch.reading.has_value())
        return "the required outcome arrived with no ink of its own";
    return "the measurement did not settle";
}

inline void mark_reported(Watch& watch) { watch.reported = true; }

inline void reset() {
    watches().clear();
    presented_frames() = 0;
}

}  // namespace hanabi::latency
