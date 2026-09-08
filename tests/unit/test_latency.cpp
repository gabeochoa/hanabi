#include <array>
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "../../src/util/latency.h"

static int failures = 0;
#define CHECK(value)                                               \
    do {                                                           \
        if (!(value)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #value, __LINE__); \
            ++failures;                                            \
        }                                                          \
    } while (false)

namespace latency = hanabi::latency;

struct Block {
    int x;
    int y;
    int width;
    int height;
};

static constexpr Block kPermanent{2, 2, 8, 12};
static constexpr Block kCaret{20, 10, 2, 17};
static constexpr Block kNewGlyph{30, 10, 8, 12};
static constexpr Block kRemovableGlyph{50, 10, 8, 12};

static latency::Frame frame(std::initializer_list<Block> blocks, int width = 80,
                            int height = 40) {
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4,
        240);
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    for (const Block block : blocks)
        for (int y = block.y; y < block.y + block.height; ++y)
            for (int x = block.x; x < block.x + block.width; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * 4;
                rgba[offset] = rgba[offset + 1] = rgba[offset + 2] = 20;
            }
    return latency::Frame::from_rgba(width, height, rgba.data(), rgba.size());
}

static void test_directional_pixels_and_caret() {
    const auto base = frame({kPermanent});
    const auto caret = frame({kPermanent, kCaret});
    const auto glyph = frame({kPermanent, kNewGlyph});
    const auto same = latency::difference(base, base);
    const auto caretAppears = latency::difference(base, caret);
    const auto caretDisappears = latency::difference(caret, base);
    const auto glyphAppears = latency::difference(base, glyph);
    const auto glyphDisappears = latency::difference(glyph, base);

    CHECK(base.valid());
    CHECK(!base.flat());
    CHECK(base.ink() >= latency::kInkFloor);
    CHECK(same.has_value() && same->changed == 0 && same->ink_gained == 0 &&
          same->ink_lost == 0);
    CHECK(caretAppears.has_value() && caretAppears->changed == 34 &&
          caretAppears->ink_gained == 34 && caretAppears->ink_lost == 0);
    CHECK(caretDisappears.has_value() && caretDisappears->changed == 34 &&
          caretDisappears->ink_gained == 0 && caretDisappears->ink_lost == 34);
    CHECK(latency::kInkFloor > 34);
    CHECK(glyphAppears.has_value() && glyphAppears->ink_gained == 96 &&
          glyphAppears->ink_lost == 0);
    CHECK(glyphDisappears.has_value() && glyphDisappears->ink_gained == 0 &&
          glyphDisappears->ink_lost == 96);
    CHECK(!latency::difference(base, frame({kPermanent}, 81, 40)).has_value());
}

static void test_every_outcome_uses_its_direction() {
    using latency::InkDirection;
    using latency::Outcome;
    const std::array gained = {Outcome::UiAppears, Outcome::TextAppears,
                               Outcome::FocusGained, Outcome::PressHeld,
                               Outcome::HoverHeld};
    const std::array lost = {Outcome::UiDisappears, Outcome::TextDisappears,
                             Outcome::FocusLost};
    static_assert(gained.size() + lost.size() ==
                      static_cast<std::size_t>(Outcome::HoverHeld) + 1,
                  "every Outcome must be in exactly one direction array");
    const latency::Difference values{200, 71, 83};

    for (const Outcome outcome : gained) {
        CHECK(latency::ink_direction(outcome) == InkDirection::Gained);
        CHECK(latency::directional_ink(outcome, values) == 71);
        CHECK(latency::expects_present(outcome));
    }
    for (const Outcome outcome : lost) {
        CHECK(latency::ink_direction(outcome) == InkDirection::Lost);
        CHECK(latency::directional_ink(outcome, values) == 83);
        CHECK(!latency::expects_present(outcome));
    }
}

static void test_wrong_direction_never_settles_any_outcome() {
    using latency::InkDirection;
    using latency::Outcome;
    const std::array outcomes = {
        Outcome::UiAppears,      Outcome::UiDisappears, Outcome::TextAppears,
        Outcome::TextDisappears, Outcome::FocusGained,  Outcome::FocusLost,
        Outcome::PressHeld,      Outcome::HoverHeld,
    };
    static_assert(outcomes.size() ==
                      static_cast<std::size_t>(Outcome::HoverHeld) + 1,
                  "every Outcome must be exercised here");

    for (const Outcome outcome : outcomes) {
        const bool gained =
            latency::ink_direction(outcome) == InkDirection::Gained;
        latency::reset();
        CHECK(
            !latency::arm("direction", outcome, "target", !gained).has_value());
        latency::presented(frame({kPermanent, kRemovableGlyph}), 100, 4,
                           [](const latency::Watch&) { return false; });
        CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
        latency::input_delivered();
        latency::presented(
            gained ? frame({kPermanent})
                   : frame({kPermanent, kRemovableGlyph, kNewGlyph}),
            2'000, 50, [](const latency::Watch&) { return true; });
        auto* watch = latency::find("direction");
        CHECK(watch != nullptr && !watch->reading.has_value());
        latency::presented(
            gained ? frame({kPermanent, kRemovableGlyph, kNewGlyph})
                   : frame({kPermanent}),
            3'000, 50, [](const latency::Watch&) { return true; });
        CHECK(watch != nullptr && watch->phase == latency::Phase::Settled);
        CHECK(watch != nullptr && watch->reading.has_value());
        if (watch != nullptr && watch->reading.has_value()) {
            CHECK(watch->reading->frames == 2);
            CHECK(watch->reading->required_ink == 96);
            CHECK(watch->reading->required_ink ==
                  (gained ? watch->reading->ink_gained
                          : watch->reading->ink_lost));
        }
    }
}

static void test_real_caret_never_satisfies_the_opposite_direction() {
    using latency::InkDirection;
    using latency::Outcome;
    const std::array outcomes = {
        Outcome::UiAppears,      Outcome::UiDisappears, Outcome::TextAppears,
        Outcome::TextDisappears, Outcome::FocusGained,  Outcome::FocusLost,
        Outcome::PressHeld,      Outcome::HoverHeld,
    };
    static_assert(outcomes.size() ==
                      static_cast<std::size_t>(Outcome::HoverHeld) + 1,
                  "every Outcome must be exercised here");

    for (const Outcome outcome : outcomes) {
        const bool gained =
            latency::ink_direction(outcome) == InkDirection::Gained;
        latency::reset();
        CHECK(!latency::arm("caret", outcome, "target", !gained).has_value());
        latency::presented(
            gained ? frame({kPermanent, kCaret}) : frame({kPermanent}), 100, 4,
            [](const latency::Watch&) { return false; });
        CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
        latency::input_delivered();
        latency::presented(
            gained ? frame({kPermanent}) : frame({kPermanent, kCaret}), 2'000,
            50, [](const latency::Watch&) { return true; });
        auto* watch = latency::find("caret");
        CHECK(watch != nullptr && !watch->reading.has_value());
    }
}

static void test_refusals_and_unresolved_results() {
    latency::reset();
    CHECK(latency::arm("present", latency::Outcome::UiAppears, "panel", true)
              .value()
              .find("ALREADY present") != std::string::npos);
    CHECK(latency::arm("gone", latency::Outcome::UiDisappears, "panel", false)
              .value()
              .find("ALREADY absent") != std::string::npos);

    latency::reset();
    CHECK(
        !latency::arm("duplicate", latency::Outcome::UiAppears, "panel", false)
             .has_value());
    CHECK(latency::arm("duplicate", latency::Outcome::UiAppears, "panel", false)
              .value()
              .find("duplicate arm") != std::string::npos);

    latency::reset();
    CHECK(
        !latency::arm("no_outcome", latency::Outcome::UiAppears, "panel", false)
             .has_value());
    latency::presented(frame({kPermanent}), 100, 2,
                       [](const latency::Watch&) { return false; });
    CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
    latency::input_delivered();
    latency::presented(frame({kPermanent, kNewGlyph}), 2'000, 50,
                       [](const latency::Watch&) { return false; });
    auto* noOutcome = latency::find("no_outcome");
    CHECK(noOutcome != nullptr && noOutcome->ink_seen);
    CHECK(noOutcome != nullptr && !noOutcome->reading.has_value());
    if (noOutcome != nullptr)
        CHECK(latency::unresolved_reason(*noOutcome).find("outcome") !=
              std::string::npos);

    latency::reset();
    CHECK(!latency::arm("no_ink", latency::Outcome::UiAppears, "panel", false)
               .has_value());
    latency::presented(frame({kPermanent}), 100, 2,
                       [](const latency::Watch&) { return false; });
    CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
    latency::input_delivered();
    latency::presented(frame({kPermanent}), 2'000, 50,
                       [](const latency::Watch&) { return true; });
    auto* noInk = latency::find("no_ink");
    CHECK(noInk != nullptr && !noInk->reading.has_value());
    if (noInk != nullptr)
        CHECK(latency::unresolved_reason(*noInk).find("no first ink") !=
              std::string::npos);

    latency::reset();
    CHECK(
        !latency::arm("unreadable", latency::Outcome::UiAppears, "panel", false)
             .has_value());
    latency::Frame flat;
    flat.width = 2;
    flat.height = 2;
    flat.luma = {240, 240, 240, 240};
    latency::presented(std::move(flat), 100, 2,
                       [](const latency::Watch&) { return false; });
    auto* unreadable = latency::find("unreadable");
    CHECK(unreadable != nullptr && unreadable->phase == latency::Phase::Failed);
}

static void test_event_and_delivery_boundaries() {
    latency::reset();
    CHECK(!latency::arm("delayed", latency::Outcome::UiAppears, "panel", false)
               .has_value());
    latency::presented(frame({kPermanent}), 100, 2,
                       [](const latency::Watch&) { return false; });
    CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
    latency::presented(frame({kPermanent, kNewGlyph}), 2'000, 50,
                       [](const latency::Watch&) { return true; });
    auto* delayed = latency::find("delayed");
    CHECK(delayed != nullptr && !delayed->reading.has_value());
    latency::input_delivered(1);
    latency::presented(std::nullopt, 3'000, 0,
                       [](const latency::Watch&) { return true; });
    CHECK(delayed != nullptr && !delayed->reading.has_value());
    latency::presented(frame({kPermanent, kNewGlyph}), 4'000, 50,
                       [](const latency::Watch&) { return true; });
    CHECK(delayed != nullptr && delayed->reading.has_value());
    if (delayed != nullptr && delayed->reading.has_value()) {
        CHECK(delayed->reading->frames == 3);
        CHECK(delayed->reading->event_to_ink_us == 3'000);
    }

    latency::reset();
    CHECK(!latency::arm("early", latency::Outcome::UiAppears, "panel", false)
               .has_value());
    CHECK(latency::event_queued(1'000) == latency::EventResult::Failed);
    auto* early = latency::find("early");
    CHECK(early != nullptr && early->phase == latency::Phase::Failed);
}

static void test_a_late_target_is_measured_at_the_target_not_the_first_ink() {
    latency::reset();
    bool outcome = false;
    const auto reached = [&outcome](const latency::Watch&) { return outcome; };

    CHECK(!latency::arm("late_target", latency::Outcome::UiAppears, "panel",
                        false)
               .has_value());
    latency::presented(frame({kPermanent}), 100, 2, reached);
    CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
    latency::input_delivered();

    latency::presented(frame({kPermanent, kNewGlyph}), 2'000, 50, reached);
    auto* watch = latency::find("late_target");
    CHECK(watch != nullptr && watch->ink_seen);
    CHECK(watch != nullptr && !watch->reading.has_value());
    latency::presented(frame({kPermanent, kNewGlyph}), 3'000, 50, reached);
    CHECK(watch != nullptr && !watch->reading.has_value());

    outcome = true;
    latency::presented(frame({kPermanent, kNewGlyph, kRemovableGlyph}), 4'000,
                       50, reached);
    CHECK(watch != nullptr && watch->phase == latency::Phase::Settled);
    CHECK(watch != nullptr && watch->reading.has_value());
    if (watch != nullptr && watch->reading.has_value()) {
        CHECK(watch->reading->frames == 3);
        CHECK(watch->reading->event_to_ink_us == 3'000);
    }
}

static void test_an_outcome_without_ink_is_not_settled_by_later_unrelated_ink() {
    latency::reset();
    bool outcome = false;
    const auto reached = [&outcome](const latency::Watch&) { return outcome; };

    CHECK(!latency::arm("outcome_then_unrelated_ink",
                        latency::Outcome::UiAppears, "panel", false)
               .has_value());
    latency::presented(frame({kPermanent}), 100, 2, reached);
    CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
    latency::input_delivered();

    outcome = true;
    latency::presented(frame({kPermanent}), 2'000, 50, reached);
    auto* watch = latency::find("outcome_then_unrelated_ink");
    CHECK(watch != nullptr && watch->outcome_seen);
    CHECK(watch != nullptr && !watch->ink_seen);
    CHECK(watch != nullptr && !watch->reading.has_value());

    outcome = false;
    latency::presented(frame({kPermanent, kNewGlyph}), 3'000, 50, reached);
    CHECK(watch != nullptr && watch->ink_seen);
    CHECK(watch != nullptr && watch->phase != latency::Phase::Settled);
    CHECK(watch != nullptr && !watch->reading.has_value());
    if (watch != nullptr)
        CHECK(latency::unresolved_reason(*watch).find(
                  "the required outcome arrived with no ink of its own") !=
              std::string::npos);
}

static void test_ink_settles_only_inside_the_render_lag_window() {
    CHECK(latency::kRenderLagFrames == 0);

    for (long gap = 0; gap <= latency::kRenderLagFrames + 1; ++gap) {
        latency::reset();
        bool outcome = false;
        const auto reached = [&outcome](const latency::Watch&) {
            return outcome;
        };
        CHECK(!latency::arm("window", latency::Outcome::UiAppears, "panel",
                            false)
                   .has_value());
        latency::presented(frame({kPermanent}), 100, 2, reached);
        CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
        latency::input_delivered();

        outcome = true;
        latency::presented(frame({kPermanent}), 2'000, 50, reached);
        outcome = false;
        for (long idle = 1; idle < gap; ++idle)
            latency::presented(frame({kPermanent}), 2'500, 50, reached);
        outcome = gap == 0;
        latency::presented(frame({kPermanent, kNewGlyph}), 3'000, 50, reached);

        auto* watch = latency::find("window");
        CHECK(watch != nullptr &&
              (watch->phase == latency::Phase::Settled) ==
                  (gap <= latency::kRenderLagFrames));
    }
}

static void test_a_held_outcome_settles_on_the_frame_its_own_ink_lands() {
    latency::reset();
    int held = 0;
    const auto reached = [&held](const latency::Watch&) { return held > 0; };

    CHECK(!latency::arm("held", latency::Outcome::PressHeld, "button", false)
               .has_value());
    latency::presented(frame({kPermanent}), 100, 2, reached);
    CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
    latency::input_delivered();

    held = 3;
    latency::presented(frame({kPermanent}), 2'000, 50, reached);
    auto* watch = latency::find("held");
    CHECK(watch != nullptr && !watch->reading.has_value());
    --held;
    latency::presented(frame({kPermanent, kNewGlyph}), 3'000, 50, reached);
    CHECK(watch != nullptr && watch->phase == latency::Phase::Settled);
    if (watch != nullptr && watch->reading.has_value())
        CHECK(watch->reading->frames == 2);
}

static void test_wrong_direction_ink_inside_the_window_never_settles() {
    latency::reset();
    const auto reached = [](const latency::Watch&) { return true; };

    CHECK(!latency::arm("inside", latency::Outcome::UiAppears, "panel", false)
               .has_value());
    latency::presented(frame({kPermanent, kRemovableGlyph}), 100, 2, reached);
    CHECK(latency::event_queued(1'000) == latency::EventResult::Fired);
    latency::input_delivered();

    latency::presented(frame({kPermanent}), 2'000, 50, reached);
    auto* watch = latency::find("inside");
    CHECK(watch != nullptr && !watch->ink_seen);
    CHECK(watch != nullptr && !watch->reading.has_value());
}

int main() {
    test_directional_pixels_and_caret();
    test_every_outcome_uses_its_direction();
    test_wrong_direction_never_settles_any_outcome();
    test_real_caret_never_satisfies_the_opposite_direction();
    test_refusals_and_unresolved_results();
    test_event_and_delivery_boundaries();
    test_a_late_target_is_measured_at_the_target_not_the_first_ink();
    test_an_outcome_without_ink_is_not_settled_by_later_unrelated_ink();
    test_ink_settles_only_inside_the_render_lag_window();
    test_a_held_outcome_settles_on_the_frame_its_own_ink_lands();
    test_wrong_direction_ink_inside_the_window_never_settles();
    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
