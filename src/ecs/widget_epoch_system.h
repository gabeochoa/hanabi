#pragma once

// ---------------------------------------------------------------------------
// The frame boundary for src/ui/widget_epoch.h.
//
// Registered immediately after afterhours' pre-layout bridge (which clears
// every widget's children and opens the UI context) and BEFORE every
// UI-creating system, so the epoch advances exactly once per frame, ahead of
// the frame's first `mk()`. Every widget built during frame N is stamped N;
// anything still carrying an older stamp was not built this frame.
// ---------------------------------------------------------------------------

#include "../../vendor/afterhours/src/core/system.h"
#include "../ui/widget_epoch.h"

namespace ecs {

struct WidgetEpochSystem : afterhours::System<> {
    bool should_iterate() const override { return false; }

    void once(float) override { hanabi::widget_epoch::begin_epoch(); }
};

}  // namespace ecs
