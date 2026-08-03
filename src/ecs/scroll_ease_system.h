#pragma once

// Hanabi-side smooth (eased) scrolling — works against the PINNED afterhours
// submodule (edfe234) WITHOUT the vendor smooth-scroll patch (#30).
//
// Why: afterhours' HandleScrollInput writes the wheel delta STRAIGHT into
// HasScrollView::scroll_offset (offset += direction * wheel * speed) and the
// renderer reads that same offset the same frame — so each wheel notch is an
// instant ~N-px jump. On macOS that reads as stepped / "chunky" vs the OS
// momentum glide (Gabe: "the scrolling is still too chunky").
//
// The vendor patch (#30) adds scroll_target/scroll_smoothing fields + eases
// inside afterhours, but it isn't landed yet. This system reproduces the effect
// entirely in hanabi-owned state, so it activates immediately and idles the day
// the vendor patch lands (the has_smooth_scroll guard: when the fields exist we
// defer to the vendor easing and this system disables itself).
//
// How it works, per HasScrollView entity (keyed by stable entity id):
//   * We remember the offset we wrote last frame (`lastOffset`).
//   * This system runs as a RENDER system, BEFORE the UI render systems read
//     scroll_offset, and AFTER HandleScrollInput (post-layout). If afterhours
//     moved the offset since last frame (a wheel step), that new value is the
//     DESTINATION the user asked for — we record it as `target` and glide
//     `shown` toward it, writing `shown` back so the view GLIDES.
//   * A programmatic pin (the transcript follow-latch sets offset = 1e9 then
//     clamps to the end) shows up as a jump to ~content end; we treat any jump
//     whose destination is at/beyond max scroll (or at the very top) as an
//     INSTANT snap so stay-at-bottom / jump-to-top are never slowed.
//
// Tuning: HANABI_SCROLL_SMOOTH overrides the ease fraction (>=1.0 = instant/off;
// 0.1..0.9 = glide). Default 0.35/frame @ ~display rate = quick but smooth.

#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include "ui_imports.h"
#include "../../vendor/afterhours/src/plugins/ui/components.h"
#include "../util/scroll_prefs.h"

namespace ecs {

struct ScrollEaseSystem : afterhours::System<afterhours::ui::HasScrollView> {
    struct St {
        float shown = 0.0f;
        float target = 0.0f;
        float lastOffset = 0.0f;
        bool init = false;
    };
    std::unordered_map<afterhours::EntityID, St> state_;
    float ease_ = 0.35f;
    bool disabled_ = false;

    void once(float) override {
        if constexpr (hanabi::has_smooth_scroll<
                          afterhours::ui::HasScrollView>::value) {
            disabled_ = true;  // vendor easing owns it; idle
        }
        if (const char* env = std::getenv("HANABI_SCROLL_SMOOTH")) {
            float v = static_cast<float>(std::atof(env));
            if (v >= 0.99f) disabled_ = true;           // 1.0 => instant/off
            else if (v > 0.0f && v < 0.99f) ease_ = v;  // explicit glide
        }
    }

    void for_each_with(afterhours::Entity& e,
                       afterhours::ui::HasScrollView& sv, float) override {
        if (disabled_ || !sv.vertical_enabled) return;

        const float maxY =
            std::max(0.0f, sv.content_size.y - sv.viewport_size.y);
        float off = sv.scroll_offset.y;
        St& st = state_[e.id];

        if (!st.init) {
            st.shown = st.target = st.lastOffset = off;
            st.init = true;
            return;
        }

        // Did the offset move since we last wrote it? That's a new destination.
        if (std::fabs(off - st.lastOffset) > 0.5f) {
            const bool snapEnd = off >= maxY - 1.0f;
            const bool snapTop = off <= 1.0f;
            if (snapEnd || snapTop) {
                st.target = st.shown = off;  // instant (pin / top)
            } else {
                st.target = off;             // glide toward it
            }
        }

        float next = st.shown + (st.target - st.shown) * ease_;
        if (std::fabs(st.target - next) < 0.5f) next = st.target;
        if (next < 0.0f) next = 0.0f;
        if (next > maxY) next = maxY;

        sv.scroll_offset.y = next;
        st.shown = next;
        st.lastOffset = next;
    }
};

}  // namespace ecs
