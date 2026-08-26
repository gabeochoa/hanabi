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

// The focused field's own state, for the editing chords afterhours has no
// action for (text_edit_chords_system.h). Null when nothing is focused.
inline afterhours::text_input::HasTextInputState* focused_text_field() {
    for (const auto& e :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e) continue;
        if (!e->has<afterhours::text_input::HasTextInputState>()) continue;
        auto& st = e->get<afterhours::text_input::HasTextInputState>();
        if (st.is_focused) return &st;
    }
    return nullptr;
}

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

// The inner field of a text_input: the child that can actually take focus.
// The wrapper imm::text_input hands back carries no click listener, so focus
// set on IT is dropped at the end of the frame (afterhours_gaps.md #57) — the
// only way to put a caret in a field the user has not clicked is to reach for
// the child. Second caller, so it lives here rather than as a private copy.
inline afterhours::EntityID focusable_field(Entity& textInput) {
    if (!textInput.has<afterhours::ui::UIComponent>()) return textInput.id;
    for (auto childId :
         textInput.get<afterhours::ui::UIComponent>().children) {
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(childId);
        if (opt.valid() && opt->has<afterhours::ui::InFocusCluster>())
            return childId;
    }
    return textInput.id;
}

}  // namespace ecs
