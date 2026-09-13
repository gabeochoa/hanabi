#pragma once
// The click a popover row must not lose.
//
// An imm button answers `if (row)` from `HasClickListener::down`, which the
// library's HandleClicks sets on the press frame and the caller's build reads
// on the NEXT frame. Since afterhours d90db15 (fc0fd04) `imm::popover` decides
// dismissal BEFORE running its body, on the frame focus is found outside the
// panel -- and the end of a press frame leaves focus at ROOT -- so the body
// that would have read `down` never runs and the selection is dropped
// (afterhours_gaps.md #599, frame log in the entry).
//
// The listener itself fires INSIDE HandleClicks, on the press frame, before
// any of that; and it is also what a keyboard activation (WidgetPress on the
// focused row) calls. So a popover row acts in its listener and ignores the
// return value. Correct under both pins.
//
// Integration seam, stated: `HasClickListener::cb` is a public
// `std::function<void(Entity&)>` the imm button installs as a no-op
// (imm_components.h, `button()`); it is reassigned on every build here, so a
// row entity reused by a keyed `mk()` never keeps last frame's lambda, and the
// lambda must capture by value or point at state that outlives the frame
// (the AppComponent singleton, Settings::get()) -- never a stack reference.
#include <functional>
#include <utility>

#include <afterhours/src/plugins/ui/components.h>
#include <afterhours/src/plugins/ui/element_result.h>

namespace hanabi::ui {

template <class Fn>
inline void act_on_press(afterhours::ui::imm::ElementResult& row, Fn&& fn) {
    auto& listener = row.ent().template get<afterhours::ui::HasClickListener>();
    listener.cb = [f = std::forward<Fn>(fn)](afterhours::Entity&) mutable { f(); };
}

}  // namespace hanabi::ui
