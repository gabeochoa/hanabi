#include <cmath>
#include <cstdio>

#include "../../src/ui/caret_clock.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace c = hanabi::caret;

// What the widget does when drawn (afterhours update_blink).
static bool widget_draw(float& timer, float step, float rate) {
    timer += step;
    if (timer >= rate * 2.0f) timer = 0.0f;
    return timer < rate;
}

int main() {
    const float rate = 0.53f;
    // Drawn at 2 fps (a retained-frame idle), the caret still follows the
    // wall clock: visible for the first 0.53 s of each 1.06 s cycle.
    {
        c::Anchor a;
        float timer = 0.0f;
        int wrong = 0;
        for (int i = 0; i < 40; ++i) {
            const double now = 100.0 + i * 0.37;  // irregular frame times
            const auto set = c::place(a, now, timer, 0.016f, rate);
            timer = set.timer;
            const bool shown = widget_draw(timer, 0.016f, rate);
            const double phase = std::fmod(now - 100.0, 2.0 * rate);
            const bool want = phase < rate;
            // allow the boundary instant
            if (shown != want && std::abs(phase - rate) > 0.02 && phase > 0.02 && 2.0 * rate - phase > 0.02) ++wrong;
            CHECK(set.toggleIn > 0.0 && set.toggleIn <= rate + 1e-6);
        }
        CHECK(wrong == 0);
    }
    // Typing resets the widget's timer to 0 (visible): the clock re-anchors
    // there instead of snapping back to its old phase.
    {
        c::Anchor a;
        float timer = 0.0f;
        auto set = c::place(a, 10.0, timer, 0.016f, rate);
        timer = set.timer;
        widget_draw(timer, 0.016f, rate);
        set = c::place(a, 10.7, timer, 0.016f, rate);  // hidden phase now
        timer = set.timer;
        widget_draw(timer, 0.016f, rate);
        CHECK(!(timer < rate));
        timer = 0.0f;  // reset_blink on a keystroke
        set = c::place(a, 10.75, timer, 0.016f, rate);
        timer = set.timer;
        CHECK(widget_draw(timer, 0.016f, rate));  // visible again
        CHECK(std::abs(set.toggleIn - rate) < 1e-3);
    }
    // A frame where the field was not drawn: the clock holds its phase.
    {
        c::Anchor a;
        float timer = 0.0f;
        auto set = c::place(a, 50.0, timer, 0.016f, rate);
        timer = set.timer;  // not drawn this frame
        set = c::place(a, 50.2, timer, 0.016f, rate);
        CHECK(std::abs((set.timer + 0.016f) - 0.2f) < 1e-3);
    }
    // The deadline helper.
    c::next_toggle_at() = 5.0;
    CHECK(!c::due(4.9) && c::due(5.0));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
