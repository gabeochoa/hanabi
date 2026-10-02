#pragma once

// Applies a ScrollMemory (ui/scroll_memory.h) to a built scroll view: called
// once per build, right after the scroll view is made and before anything
// reads its offset. Writes the offset and the wheel's target together, so
// the easing does not pull it straight back.

#include "../ui/scroll_memory.h"
#include "ui_imports.h"

namespace ecs {

inline void keep_scroll_place(afterhours::Entity& scrollEnt, hanabi::ui::ScrollMemory& memory,
                              int key) {
    if (!scrollEnt.has<afterhours::ui::HasScrollView>()) return;
    auto& sv = scrollEnt.get<afterhours::ui::HasScrollView>();
    if (const auto y = memory.step(afterhours::ui::imm::ui_build_frame, key, sv.scroll_offset.y)) {
        sv.scroll_offset.y = *y;
        sv.scroll_target.y = *y;
    }
}

}  // namespace ecs
