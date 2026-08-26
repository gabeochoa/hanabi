#pragma once

// ---------------------------------------------------------------------------
// The app's InputAction enum and the key table that feeds it.
//
// TWO THINGS HAVE TO LINE UP, and only one of them fails loudly.
//
// 1. THE ENUMERATOR NAME. afterhours' text_input guards every editing feature
//    on `if constexpr (magic_enum::enum_contains<InputAction>("TextWordLeft"))`
//    (text_input/component.h). The name is the opt-in: an enum without the
//    enumerator compiles fine and the whole feature is simply not in the
//    binary. That is how word motion and word delete were missing here — the
//    library has had them the whole time, and this enum stopped at
//    TextSelectAll, so `if constexpr` deleted them at compile time with no
//    error, no warning and nothing to grep for. Adding a name here turns code
//    on; removing one turns it silently off again.
//
// 2. THE BINDING. Naming the action is not enough — ctx.pressed() reads the
//    mapping below, and an action with no entry never fires. TextSelectAll was
//    in this enum from the start and had no row in the table, so Cmd+A did
//    nothing for exactly that reason.
//
// The table lives here rather than inline in preload.cpp so the unit test
// asserts the SHIPPING table instead of a copy of it that can drift
// (tests/unit/test_input_pipeline.cpp used to keep its own).
// ---------------------------------------------------------------------------

#include <map>

#include <afterhours/src/core/key_codes.h>
#include <afterhours/src/plugins/input_system.h>

enum class InputAction {
    None,
    // Required by afterhours UI systems
    WidgetUp,
    WidgetDown,
    WidgetRight,
    WidgetLeft,
    WidgetNext,
    WidgetPress,
    WidgetMod,
    WidgetBack,
    // Required by afterhours text_input (T031)
    TextBackspace,
    TextDelete,
    TextHome,
    TextEnd,
    TextSelectAll,
    // Word-granular editing. These four names are what switch the library's
    // word paths on; see (1) above.
    TextWordLeft,
    TextWordRight,
    TextDeleteWordBack,
    TextDeleteWordForward,
    MenuBack,
    // Will be extended by T040 (keyboard navigation)
};

namespace hanabi::input {

// The chords hanabi binds, macOS-first.
//
// WHICH MODIFIER MEANS WHAT. On macOS Option is the WORD modifier and Command
// is the LINE modifier: Alt+Arrow steps a word, Cmd+Arrow goes to the end of
// the line, Alt+Backspace kills a word, Cmd+Backspace kills the line. The
// library's own default_keymap() does not encode that split — it binds every
// editing chord to Cmd AND Ctrl alike, so adopting it wholesale would give
// Cmd+Left "previous word" and leave Option unbound entirely. So the table is
// written out here instead of taken from the library.
//
// WHY THE CTRL TWINS. Every Cmd chord is also bound to Ctrl. Partly that is
// the ordinary cross-platform courtesy the library extends, but the reason it
// is load-bearing HERE is the scripted-UI harness: parse_key_combo maps the
// `CMD+` prefix onto KeyCombo::ctrl and drops `SUPER+` on the floor
// (afterhours_gaps.md #49, #256). A chord bound to Super alone is unreachable
// from a test script; the same chord bound to Ctrl as well is reachable, so
// `key CMD+A` in a .e2e file exercises the real action. The word chords need
// no twin — the harness holds LEFT_ALT natively.
inline afterhours::input::ProvidesInputMapping::GameMapping key_mapping() {
    namespace keys = afterhours::keys;
    using KeyChord = afterhours::input::KeyChord;
    using ValidInputs = afterhours::input::ValidInputs;

    constexpr uint8_t CMD = KeyChord::MOD_SUPER;
    constexpr uint8_t CTRL = KeyChord::MOD_CTRL;
    constexpr uint8_t ALT = KeyChord::MOD_ALT;

    afterhours::input::ProvidesInputMapping::GameMapping m;
    auto bind = [&m](InputAction a, ValidInputs v) {
        m[static_cast<int>(a)] = std::move(v);
    };

    bind(InputAction::TextBackspace, {keys::BACKSPACE});
    bind(InputAction::TextDelete, {keys::DELETE_KEY});
    bind(InputAction::WidgetLeft, {keys::LEFT});
    bind(InputAction::WidgetRight, {keys::RIGHT});
    bind(InputAction::WidgetPress, {keys::ENTER});
    bind(InputAction::MenuBack, {keys::ESCAPE});

    // Tab. Without this the focus ring is a lie: context.h's process_tabbing
    // is the only thing that moves focus_id, it moves it only on
    // InputAction::WidgetNext, and nothing bound that action -- so four Tab
    // presses in a row produced byte-identical frames and the ring sat forever
    // on whatever try_to_grab parked focus on at startup.
    //
    // WidgetBack is deliberately left unbound. afterhours' own default_keymap
    // puts it on BACKSPACE, which in an app with text fields means deleting a
    // character walks focus backwards; Shift+Tab already reverses through the
    // WidgetMod branch of the WidgetNext path, which is the chord a Mac user
    // actually presses.
    //
    // These two arrived on main (fix/focus-rings) as rows in preload.cpp's
    // inline map while this branch was moving that map here. Carried across
    // verbatim so the merge cannot drop them: resolve src/preload.cpp in
    // favour of this branch and both features survive.
    bind(InputAction::WidgetNext, {keys::TAB});
    bind(InputAction::WidgetMod, {keys::LEFT_SHIFT, keys::RIGHT_SHIFT});

    // Line ends. HOME/END keep working for anyone on a full keyboard; Cmd+
    // Arrow is what a Mac laptop actually has. TextHome/TextEnd run through
    // the widget's navigate() helper, so holding Shift extends the selection
    // to the line end instead of collapsing it — that is where Cmd+Shift+
    // Arrow comes from without a binding of its own.
    bind(InputAction::TextHome, {keys::HOME, KeyChord{keys::LEFT, CMD},
                                 KeyChord{keys::LEFT, CTRL}});
    bind(InputAction::TextEnd, {keys::END, KeyChord{keys::RIGHT, CMD},
                                KeyChord{keys::RIGHT, CTRL}});

    bind(InputAction::TextSelectAll,
         {KeyChord{keys::A, CMD}, KeyChord{keys::A, CTRL}});

    // Word motion and word delete. Option only: binding these to Cmd as well
    // would collide with the line chords above on the very same keys.
    bind(InputAction::TextWordLeft, {KeyChord{keys::LEFT, ALT}});
    bind(InputAction::TextWordRight, {KeyChord{keys::RIGHT, ALT}});
    bind(InputAction::TextDeleteWordBack, {KeyChord{keys::BACKSPACE, ALT}});
    bind(InputAction::TextDeleteWordForward, {KeyChord{keys::DELETE_KEY, ALT}});

    return m;
}

}  // namespace hanabi::input
