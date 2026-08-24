#pragma once

// ---------------------------------------------------------------------------
// One owner for "should the focus ring be painted".
//
// Registered ahead of every UI system, so theme.focus_ring_thickness is already
// settled by the time anything renders. The rule itself, and why afterhours
// cannot answer this question on its own, is in ui/focus_visible.h.
// ---------------------------------------------------------------------------

#include "../keys.h"
#include "../ui/focus_visible.h"
#include "ui_imports.h"

namespace ecs {

struct FocusVisibleSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        namespace fv = hanabi::ui::focus_visible;
        namespace k = hanabi::keys;
        const bool navKey = k::pressed(k::kTab) || k::pressed(k::kUp) ||
                            k::pressed(k::kDown) || k::pressed(k::kLeft) ||
                            k::pressed(k::kRight);
        fv::observe(navKey, ctx.mouse.just_pressed);
        ctx.theme.focus_ring_thickness = fv::ring_thickness();
    }
};

}  // namespace ecs
