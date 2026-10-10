#pragma once

// Who owns the keyboard right now.
//
// A text field that has focus owns every key the caret cares about, which is
// why the transcript's arrow scrolling, the composer's history walk and the
// arrow owner (arrow_system.h) all have to ask the same question. They used to
// ask it through a private copy inside the main pane; three copies of "is
// anything focused" is three chances to answer it differently.

#include "ui_imports.h"
#include "../ui/caret_clock.h"
#include <unordered_map>

namespace ecs {

// Did this frame's focus move come from the reader, or from try_to_grab?
// afterhours resets focus_source to Grab every frame and stamps the source that
// moved focus: Explicit for an app's own set_focus and, since upstream 940ba03
// (:focus-visible, in the 65d5292 pin), Pointer for a click (HandleClicks /
// HandleDrags). Both are on purpose; only Grab is automatic. Reading only
// Explicit stopped counting a click on the composer as a claim once clicks
// became Pointer, so the keyboard stayed with whatever field held it before.
[[nodiscard]] constexpr bool focus_claimed_on_purpose(afterhours::ui::FocusSource src) {
    return src != afterhours::ui::FocusSource::Grab;
}
static_assert(focus_claimed_on_purpose(afterhours::ui::FocusSource::Pointer));
static_assert(focus_claimed_on_purpose(afterhours::ui::FocusSource::Explicit));
static_assert(!focus_claimed_on_purpose(afterhours::ui::FocusSource::Grab));

// The focused field's own state, for the editing chords afterhours has no
// action for (text_edit_chords_system.h). Null when nothing is focused.
inline afterhours::text_input::HasTextInputState* focused_text_field() {
    for (const auto& e :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e) continue;
        if (e->has<afterhours::text_input::HasTextAreaState>()) {
            auto& a = e->get<afterhours::text_input::HasTextAreaState>();
            if (a.is_focused) return &a;
            continue;
        }
        if (!e->has<afterhours::text_input::HasTextInputState>()) continue;
        auto& st = e->get<afterhours::text_input::HasTextInputState>();
        if (st.is_focused) return &st;
    }
    return nullptr;
}

inline int focused_text_entity() {
    for (const auto& e :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e) continue;
        if (e->has<afterhours::text_input::HasTextAreaState>()) {
            if (e->get<afterhours::text_input::HasTextAreaState>().is_focused) return static_cast<int>(e->id);
            continue;
        }
        if (e->has<afterhours::text_input::HasTextInputState>() &&
            e->get<afterhours::text_input::HasTextInputState>().is_focused)
            return static_cast<int>(e->id);
    }
    return -1;
}

inline afterhours::text_input::HasTextAreaState* focused_text_area() {
    for (const auto& e :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e || !e->has<afterhours::text_input::HasTextAreaState>()) continue;
        auto& a = e->get<afterhours::text_input::HasTextAreaState>();
        if (a.is_focused) return &a;
    }
    return nullptr;
}

// Before a drawn frame: every focused field's caret is placed on the wall
// clock, and the soonest toggle becomes the frame loop's next caret deadline
// (ui/caret_clock.h). `dt` is the step a text INPUT will add this frame; a
// text AREA adds a fixed 0.016.
inline void drive_caret_clocks(float dt) {
    static std::unordered_map<afterhours::EntityID, hanabi::caret::Anchor> anchors;
    static std::unordered_map<afterhours::EntityID, hanabi::caret::Anchor> seen;
    seen.clear();
    const double now = hanabi::caret::wall_seconds();
    double soonest = 0.0;
    const auto drive = [&](afterhours::EntityID id, auto& st, float step) {
        if (!st.is_focused) return;
        auto it = anchors.find(id);
        hanabi::caret::Anchor a = it == anchors.end() ? hanabi::caret::Anchor{} : it->second;
        const auto set = hanabi::caret::place(a, now, st.cursor_blink_timer, step, st.cursor_blink_rate);
        st.cursor_blink_timer = set.timer;
        seen[id] = a;
        const double at = now + set.toggleIn;
        if (soonest == 0.0 || at < soonest) soonest = at;
    };
    for (const auto& e : afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e) continue;
        if (e->has<afterhours::text_input::HasTextAreaState>()) {
            drive(e->id, e->get<afterhours::text_input::HasTextAreaState>(), 0.016f);
            continue;
        }
        if (e->has<afterhours::text_input::HasTextInputState>())
            drive(e->id, e->get<afterhours::text_input::HasTextInputState>(), dt > 0.f ? dt : 1.f / 60.f);
    }
    anchors.swap(seen);
    hanabi::caret::next_toggle_at() = soonest;
}

inline bool any_text_field_focused() {
    for (const auto& e :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e) continue;
        if (e->has<afterhours::text_input::HasTextAreaState>()) {
            if (e->get<afterhours::text_input::HasTextAreaState>().is_focused)
                return true;
            continue;
        }
        if (!e->has<afterhours::text_input::HasTextInputState>()) continue;
        if (e->get<afterhours::text_input::HasTextInputState>().is_focused)
            return true;
    }
    return false;
}

// Is the CARET in the composer, as opposed to some other field?
//
// The composer is the tree's only text_AREA -- every other field (search, the
// palette, rename, settings) is a text_input -- and scripts/composer_parity_gate.sh
// fails the build if a second text_area appears. So "a focused HasTextAreaState"
// is exactly "the composer has the keyboard", and it stays true by construction
// rather than by a name comparison that a rename would silently break.
inline bool composer_field_focused() {
    for (const auto& e :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!e || !e->has<afterhours::text_input::HasTextAreaState>()) continue;
        if (e->get<afterhours::text_input::HasTextAreaState>().is_focused)
            return true;
    }
    return false;
}

inline bool caret_in_composer(afterhours::EntityID focusId) {
    auto focused = afterhours::ui::UICollectionHolder::getEntityForID(focusId);
    if (!focused.valid()) return false;
    if (focused->has<afterhours::text_input::HasTextAreaState>()) return true;
    if (!focused->has<afterhours::ui::UIComponent>()) return false;
    auto owner = afterhours::ui::UICollectionHolder::getEntityForID(
        focused->get<afterhours::ui::UIComponent>().parent);
    return owner.valid() &&
           owner->has<afterhours::text_input::HasTextAreaState>();
}

// Is the caret in a field the composer does not own -- the sidebar's search,
// the find bar's input, a card's answer box, a sheet's field?
//
// The mirror of caret_in_composer, and read off the same focus id rather than
// off the widgets' own is_focused flags. any_text_field_focused() answers the
// same question from the other side, but it answers it a frame late for a
// CLAIM: is_focused is written while the widget is being built, and a click is
// processed inside that same build (text_area.h:366), so on the frame the
// reader lands in a field the flag still describes the frame before. focus_id
// is already the field by the time the claim is taken at the end of the frame.
//
// The text_AREA arm comes first because the composer's wrapper carries a
// shadow HasTextInputState too (the scripted harness reads the draft off it),
// so asking about the input state first would file the composer under "someone
// else's field" and yield the keyboard to itself.
inline bool caret_in_other_field(afterhours::EntityID focusId) {
    if (caret_in_composer(focusId)) return false;
    auto focused = afterhours::ui::UICollectionHolder::getEntityForID(focusId);
    if (!focused.valid()) return false;
    if (focused->has<afterhours::text_input::HasTextInputState>()) return true;
    if (!focused->has<afterhours::ui::UIComponent>()) return false;
    auto owner = afterhours::ui::UICollectionHolder::getEntityForID(
        focused->get<afterhours::ui::UIComponent>().parent);
    return owner.valid() &&
           owner->has<afterhours::text_input::HasTextInputState>();
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
