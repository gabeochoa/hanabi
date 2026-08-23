#pragma once

// Who owns the keyboard right now.
//
// A text field that has focus owns every key the caret cares about, which is
// why the transcript's arrow scrolling, the composer's history walk and the
// arrow owner (arrow_system.h) all have to ask the same question. They used to
// ask it through a private copy inside the main pane; three copies of "is
// anything focused" is three chances to answer it differently.

#include "ui_imports.h"

namespace ecs {

inline bool any_text_field_focused() {
    for (const auto& e :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e) continue;
        if (!e->has<afterhours::text_input::HasTextInputState>()) continue;
        if (e->get<afterhours::text_input::HasTextInputState>().is_focused)
            return true;
    }
    return false;
}

}  // namespace ecs
