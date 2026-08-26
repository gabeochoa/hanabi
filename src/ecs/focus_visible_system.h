#pragma once

// ---------------------------------------------------------------------------
// One owner for "should the focus ring be painted".
//
// Registered ahead of every UI system, so theme.focus_ring_thickness is already
// settled by the time anything renders. The rule itself, and why afterhours
// cannot answer this question on its own, is in ui/focus_visible.h.
// ---------------------------------------------------------------------------

#include "../keys.h"
#include "../test_hooks.h"
#include "../ui/focus_visible.h"
#include "../ui/theme.h"
#include "ui_imports.h"

namespace ecs {

struct FocusVisibleSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        namespace fv = hanabi::ui::focus_visible;
        namespace k = hanabi::keys;
        fv::observe(k::pressed(k::kTab), ctx.mouse.just_pressed);
        // A capture that force-focuses the composer arms the ring too: the
        // rule is "no ring until the keyboard was used", and the hook stands
        // in for the keystroke that is not being typed (src/test_hooks.h).
        if (hanabi::test_hooks::focus_composer()) fv::armed() = true;
        ctx.theme.focus_ring_thickness = fv::ring_thickness();
        // The single writer of the ring's colour. It used to be re-asserted
        // as theme::accent() by each of the three systems that build a
        // focusable widget, because ctx.theme is one global struct read at
        // render time (gap #90) — but nothing in afterhours writes theme.focus
        // per frame, so one owner ahead of them all is enough, and the ring
        // stops depending on which system rendered last.
        ctx.theme.focus = fv::ring_color(
            theme::focus_ring(),
            afterhours::colors::luminance(theme::window_bg()) <
                fv::kContrastThreshold);
    }
};

}  // namespace ecs
