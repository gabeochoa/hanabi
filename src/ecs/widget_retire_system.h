#pragma once

// ---------------------------------------------------------------------------
// The frame boundary for src/ui/widget_epoch.h, and the sweep that retires
// what nothing built.
//
// Registered immediately after afterhours' pre-layout bridge (which clears
// every widget's children and opens the UI context) and BEFORE every
// UI-creating system, so that:
//
//   * the epoch advances exactly once per frame, ahead of the frame's first
//     `mk()` -- every widget built during frame N is stamped N; and
//   * anything retired is retired before this frame's builders run, so a
//     screen coming BACK on this very frame rebuilds cleanly instead of
//     finding half of itself marked for death. (The rebuild is a new entity
//     from a new `mk()`; the id it is given cannot collide with a retired one,
//     because the retired ids are not returned to the free list until the
//     cleanup at the end of this frame's update phase.)
//
// WHAT IS NOT HERE, AND WHY. Nothing resets UIContext's focus / hot / active
// ids, and that is deliberate rather than forgotten. All three are re-derived
// every frame from widgets that were BUILT this frame: `focus_id` is dropped
// by EndUIContextManager on any frame its widget does not call `try_to_grab`,
// `hot_id` is re-resolved by a hit test over live entities, `active_id` is
// released on mouse-up. A widget that has not been built for `grace` frames
// gave all three up on the first of those frames. tests/test_widget_retire.cpp
// pins that reasoning so it fails if it stops being true.
//
// hanabi's own cross-frame widget id is the text selection's owner, which is
// NOT self-healing -- it is held until something else takes the selection --
// so it is dropped here when its widget stops being one of ours. Without that,
// a recycled EntityID would hand the selection to an unrelated widget.
// ---------------------------------------------------------------------------

#include "../../vendor/afterhours/src/core/system.h"
#include "../ui/text_select.h"
#include "../ui/widget_epoch.h"

namespace ecs {

struct WidgetRetireSystem : afterhours::System<> {
    bool should_iterate() const override { return false; }

    void once(float) override {
        hanabi::widget_epoch::begin_epoch();
        if (!hanabi::widget_epoch::retire_enabled()) return;
        if (hanabi::widget_epoch::epoch() %
                hanabi::widget_epoch::sweep_every() !=
            0)
            return;
        const auto swept = hanabi::widget_epoch::retire_stale(
            hanabi::widget_epoch::grace_frames());
        if (swept.retired == 0) return;
        drop_selection_if_its_widget_is_gone();
    }

    static void drop_selection_if_its_widget_is_gone() {
        const afterhours::EntityID owner =
            hanabi::text_select::state().owner;
        if (owner < 0) return;
        if (hanabi::widget_epoch::stamp_read(owner) != 0u) return;
        hanabi::text_select::clear();
    }
};

}  // namespace ecs
