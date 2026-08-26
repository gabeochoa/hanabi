#pragma once

#include "../../vendor/afterhours/src/plugins/color.h"
#include "../../vendor/afterhours/src/plugins/ui/components.h"
#include "../../vendor/afterhours/src/plugins/ui/ui_collection.h"

// ---------------------------------------------------------------------------
// The two bits of chrome `text_area` gets wrong, put back from this side of
// the vendor boundary.
//
// vendor/afterhours is read-only here, so `text_input` draws a focused edge
// and honours a caller's transparent background and `text_area` does neither
// (afterhours_gaps.md #263 and #262). Both are fixed the same way and for the
// same reason: the widget builds its field as a child div and then MUTATES
// that entity -- focus, borders, the caret overlay -- rather than deciding
// everything in the config. The field therefore exists, addressable, by the
// time the call returns, and anything the widget did to it can be done again.
//
// That is what makes these two functions cheap and exact, and it is why
// neither of the workarounds the gaps proposed is the one taken:
//
//   #263 proposed the ring on the app's own WRAPPER div. That costs a frame
//        of lag -- the wrapper is built before the field, so it can only read
//        last frame's focus -- and puts the edge around the 45px box instead
//        of the 29px field. Reaching for the field entity costs neither.
//
//   #262 proposed saving and restoring ctx.theme's Secondary around the call.
//        That works (the usage is resolved at BUILD time, component_init.h:381
//        -- the same reasoning gap #105 records for font_muted), and this
//        composer has in fact been doing it since gap #17, which is why the
//        interior never turned (57,57,68) here. But a colour is not what the
//        caller asked for: `with_transparent_bg()` asks the field to paint
//        NOTHING, and painting the right colour instead is visibly different
//        wherever something is underneath. See below.
// ---------------------------------------------------------------------------

namespace hanabi::ui::field_chrome {

// ---------------------------------------------------------------------------
// #262 -- the forced fill.
//
// text_area builds its field with `.with_background(Theme::Usage::Secondary)`
// and drops the caller's `with_transparent_bg()` on the floor.
//
// The interior colour is NOT what this costs on hanabi's composer, and that
// is worth writing down because the gap says otherwise. hanabi already points
// ctx.theme.secondary at the strip colour before the call, so the fill lands
// on (23,23,35) -- exactly the window colour the interior is supposed to be,
// and nothing about the interior moves.
//
// What it costs is the OUTLINE. The field is percent(1.0f) of a wrap whose
// own 1px border draws ON the box edge, so an opaque field paints over that
// border wherever the two overlap: 42 pixels, measured -- both rounded top
// corners' 7px arcs and the whole right-hand border column down the field's
// 29 rows, all of them (41,41,52) before and (23,23,35) after. The composer
// stopped being a rounded outlined box and became a box missing a corner.
// A same-coloured fill is invisible on a flat surface and destructive on top
// of a line, which is the difference between "the right colour" and "nothing".
//
// So the fill is removed rather than recoloured: the field's HasColor is set
// to a fully transparent colour, which is precisely what
// `with_transparent_bg()` would have put there (component_config.h:344 ->
// `with_custom_background(Color{0,0,0,0})`).
inline void clear_forced_fill(afterhours::EntityID fieldId) {
    auto opt = afterhours::ui::UICollectionHolder::getEntityForID(fieldId);
    if (!opt.valid()) return;
    auto& field = opt.asE();
    if (!field.has<afterhours::HasColor>()) return;
    field.get<afterhours::HasColor>().set(afterhours::colors::transparent());
}

// ---------------------------------------------------------------------------
// #263 -- the missing focused edge.
//
// This is not the app's :focus-visible ring. src/ui/focus_visible.h decides
// when the KEYBOARD has earned a ring around a widget, and Tab is the only
// thing that arms it. This is the other indicator, the one every text field in
// every toolkit has: the field's own border going accent-coloured while it
// holds the caret. focus_visible.h names this edge in its own words -- "the
// focused border the composer draws itself" -- as a thing that already exists
// and that an armed ring must not double up on. Reproducing it is obeying that
// policy; the armed ring is still gated exactly as it was, lands outside this
// edge, and the two stack without arguing.
//
// The four lines are text_input's own (text_input/component.h:257-262):
//
//     if (state.is_focused) {
//       auto focus_color = ctx.theme.accent;
//       field_entity.addComponentIfMissing<HasBorder>();
//       field_entity.get<HasBorder>().border = Border::all(focus_color,
//                                                          pixels(2.0f));
//     }
//
// SELF-CLEARING, and not by accident: the field div is rebuilt every frame and
// component_init.h's apply_border removes HasBorder from any entity whose
// config carries no border, so an unfocused frame strips the edge before this
// runs. That is why there is no else branch to get wrong -- and it is why the
// call must be made every frame rather than only on the focus transition.
inline constexpr float kFocusEdgeThickness = 2.0f;

inline void apply_focus_edge(afterhours::EntityID fieldId, bool focused,
                             afterhours::Color accent) {
    if (!focused) return;
    auto opt = afterhours::ui::UICollectionHolder::getEntityForID(fieldId);
    if (!opt.valid()) return;
    auto& field = opt.asE();
    field.addComponentIfMissing<afterhours::ui::HasBorder>();
    field.get<afterhours::ui::HasBorder>().border =
        afterhours::ui::Border::all(
            accent, afterhours::ui::pixels(kFocusEdgeThickness));
}

}  // namespace hanabi::ui::field_chrome
