// tests/unit/test_input_pipeline.cpp
// Guards the ROOT CAUSE of "Enter doesn't send" + "Backspace doesn't delete":
// hanabi added the InputCollector component (preload) but never registered
// afterhours::input::InputSystem — the system that maps raw keys -> actions
// each frame. So ctx.pressed(WidgetPress)/pressed_or_repeat(TextBackspace)
// never fired (typing worked via the separate char queue).
//
// This test exercises the exact ACTION-RESOLUTION logic that InputSystem runs
// (input::check_single_action_impl) against hanabi's real key mapping, with a
// backend-free key-check stub — proving BACKSPACE resolves to TextBackspace and
// ENTER to WidgetPress. No graphics backend needed. It documents + guards the
// mapping the InputSystem consumes; the registration itself is a one-liner in
// build_systems (src/main.cpp) verified by the app build.
//
// It now reads hanabi::input::key_mapping() — the table preload.cpp actually
// installs — instead of the hand-copied three rows it used to rebuild. The copy
// was the whole problem: it could not have caught the two bugs below, because
// neither of them was in the three rows it knew about.
//
//   - TextSelectAll was an enumerator with no row in the table, so Cmd+A did
//     nothing at all.
//   - The word actions were absent from the enum, so afterhours deleted its own
//     word-editing code at compile time (`if constexpr enum_contains`) and
//     Alt+Backspace did nothing.
//
// MODIFIERS ARE NOT SYNTHESISABLE HERE. check_single_action_impl reads the held
// modifiers from input::get_current_modifiers(), which asks the graphics
// backend directly and cannot be stubbed; with no backend linked, nothing is
// ever held. So a chord's POSITIVE case belongs in the scripted UI suite
// (tests/ui/composer_word_editing.e2e) and what is asserted here is the two
// things that do not need a held modifier: the SHAPE of each chord in the
// table, and the negative — that a bare keypress does not fire a chord action.
// The negative is the half that actually bites: a chord written without its
// modifier would make plain Backspace eat a whole word.

#define FMT_HEADER_ONLY
#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include <cstdio>
#include <map>
#include "../../src/input_mapping.h"
#include <afterhours/src/plugins/input_system.h>
#include <afterhours/src/core/key_codes.h>
#include <magic_enum/magic_enum.hpp>

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do { if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); ++g_failures; } } while (0)

using namespace afterhours;

// Resolve an action's value using the SAME impl InputSystem runs, but with a
// backend-free key-check that reports exactly `pressed_key` as down.
static float action_value(const input::ValidInputs& vis, int pressed_key) {
    auto key_check = [pressed_key](int k) { return k == pressed_key; };
    auto btn_check = [](input::GamepadID, auto) { return 0.f; };
    auto r = input::check_single_action_impl(/*id=*/0, vis, key_check, btn_check);
    return r.value;
}

static const input::ValidInputs& binding_for(
    const input::ProvidesInputMapping::GameMapping& m, InputAction a) {
    static const input::ValidInputs empty;
    auto it = m.find(static_cast<int>(a));
    return it == m.end() ? empty : it->second;
}

// Does this action's binding list contain exactly this key with exactly these
// required modifiers?
static bool has_chord(const input::ValidInputs& vis, int key, uint8_t mods) {
    for (const auto& in : vis) {
        if (in.index() != 0) continue;
        const auto& c = std::get<0>(in);
        if (c.key == key && c.has_explicit_modifiers &&
            c.required_modifiers == mods)
            return true;
    }
    return false;
}

// Is this key bound with NO modifier requirement (fires on a bare press)?
static bool has_bare_key(const input::ValidInputs& vis, int key) {
    for (const auto& in : vis) {
        if (in.index() != 0) continue;
        const auto& c = std::get<0>(in);
        if (c.key == key && !c.has_explicit_modifiers) return true;
    }
    return false;
}

static void test_backspace_maps_to_textbackspace() {
    std::printf("test_backspace_maps_to_textbackspace\n");
    auto m = hanabi::input::key_mapping();
    const auto& vis = binding_for(m, InputAction::TextBackspace);
    CHECK(action_value(vis, keys::BACKSPACE) > 0.f);   // BACKSPACE fires it
    CHECK(action_value(vis, keys::ENTER) == 0.f);      // ENTER does not
    CHECK(action_value(vis, keys::SPACE) == 0.f);      // SPACE does not
}

static void test_enter_maps_to_widgetpress() {
    std::printf("test_enter_maps_to_widgetpress\n");
    auto m = hanabi::input::key_mapping();
    const auto& vis = binding_for(m, InputAction::WidgetPress);
    CHECK(action_value(vis, keys::ENTER) > 0.f);       // ENTER fires send
    CHECK(action_value(vis, keys::SPACE) == 0.f);      // SPACE must NOT send
    CHECK(action_value(vis, keys::BACKSPACE) == 0.f);
}

// SPACE must not be bound to any editing/submit action (it's a typed char).
static void test_space_not_bound_to_actions() {
    std::printf("test_space_not_bound_to_actions\n");
    auto m = hanabi::input::key_mapping();
    for (auto& [action, vis] : m)
        CHECK(action_value(vis, keys::SPACE) == 0.f);
}

// macOS: Option is the word modifier. Alt+Backspace / Alt+Delete kill a word,
// Alt+Arrow steps one.
static void test_word_chords_are_bound_to_option() {
    std::printf("test_word_chords_are_bound_to_option\n");
    auto m = hanabi::input::key_mapping();
    constexpr uint8_t ALT = input::KeyChord::MOD_ALT;
    CHECK(has_chord(binding_for(m, InputAction::TextDeleteWordBack),
                    keys::BACKSPACE, ALT));
    CHECK(has_chord(binding_for(m, InputAction::TextDeleteWordForward),
                    keys::DELETE_KEY, ALT));
    CHECK(has_chord(binding_for(m, InputAction::TextWordLeft), keys::LEFT, ALT));
    CHECK(has_chord(binding_for(m, InputAction::TextWordRight), keys::RIGHT, ALT));
}

// The other half of the same statement: none of the word actions may fire on a
// bare press. Plain Backspace deletes ONE character.
static void test_word_chords_need_their_modifier() {
    std::printf("test_word_chords_need_their_modifier\n");
    auto m = hanabi::input::key_mapping();
    const InputAction word_actions[] = {
        InputAction::TextDeleteWordBack, InputAction::TextDeleteWordForward,
        InputAction::TextWordLeft, InputAction::TextWordRight};
    for (InputAction a : word_actions) {
        const auto& vis = binding_for(m, a);
        CHECK(!vis.empty());
        CHECK(!has_bare_key(vis, keys::BACKSPACE));
        CHECK(!has_bare_key(vis, keys::DELETE_KEY));
        CHECK(!has_bare_key(vis, keys::LEFT));
        CHECK(!has_bare_key(vis, keys::RIGHT));
        // Nothing held: the chord must not resolve.
        CHECK(action_value(vis, keys::BACKSPACE) == 0.f);
        CHECK(action_value(vis, keys::DELETE_KEY) == 0.f);
        CHECK(action_value(vis, keys::LEFT) == 0.f);
        CHECK(action_value(vis, keys::RIGHT) == 0.f);
    }
}

// macOS: Command is the LINE modifier, and it must not be the word one. If
// Cmd+Left were left on TextWordLeft (which is what the library's own
// default_keymap does) this is the assertion that would catch it.
static void test_line_chords_are_bound_to_command() {
    std::printf("test_line_chords_are_bound_to_command\n");
    auto m = hanabi::input::key_mapping();
    constexpr uint8_t CMD = input::KeyChord::MOD_SUPER;
    constexpr uint8_t CTRL = input::KeyChord::MOD_CTRL;
    CHECK(has_chord(binding_for(m, InputAction::TextHome), keys::LEFT, CMD));
    CHECK(has_chord(binding_for(m, InputAction::TextEnd), keys::RIGHT, CMD));
    // HOME/END keep working on their own keys.
    CHECK(has_bare_key(binding_for(m, InputAction::TextHome), keys::HOME));
    CHECK(has_bare_key(binding_for(m, InputAction::TextEnd), keys::END));
    // Cmd is not the word modifier.
    CHECK(!has_chord(binding_for(m, InputAction::TextWordLeft), keys::LEFT, CMD));
    CHECK(!has_chord(binding_for(m, InputAction::TextWordRight), keys::RIGHT, CMD));
    // The Ctrl twin is what makes these reachable from a .e2e script, whose
    // CMD+ prefix the harness turns into Ctrl (afterhours_gaps.md #256).
    CHECK(has_chord(binding_for(m, InputAction::TextHome), keys::LEFT, CTRL));
    CHECK(has_chord(binding_for(m, InputAction::TextEnd), keys::RIGHT, CTRL));
}

// Cmd+A. The enumerator existed from the start; the BINDING is what was
// missing, and an enumerator with no row is silent.
// Tab moves focus and Shift reverses it. Rows that arrived on main while this
// table was moving out of preload.cpp; asserted here so the merge cannot drop
// them silently, which is the one way a keymap regression hides.
static void test_tab_focus_bindings_survive() {
    std::printf("test_tab_focus_bindings_survive\n");
    auto m = hanabi::input::key_mapping();
    CHECK(has_bare_key(binding_for(m, InputAction::WidgetNext), keys::TAB));
    CHECK(has_bare_key(binding_for(m, InputAction::WidgetMod), keys::LEFT_SHIFT));
    CHECK(has_bare_key(binding_for(m, InputAction::WidgetMod), keys::RIGHT_SHIFT));
    // WidgetBack stays unbound: on BACKSPACE it would walk focus backwards
    // every time a character is deleted.
    CHECK(binding_for(m, InputAction::WidgetBack).empty());
}

static void test_select_all_is_bound() {
    std::printf("test_select_all_is_bound\n");
    auto m = hanabi::input::key_mapping();
    const auto& vis = binding_for(m, InputAction::TextSelectAll);
    CHECK(!vis.empty());
    CHECK(has_chord(vis, keys::A, input::KeyChord::MOD_SUPER));
    CHECK(has_chord(vis, keys::A, input::KeyChord::MOD_CTRL));
    // A bare 'a' is a typed character, never select-all.
    CHECK(!has_bare_key(vis, keys::A));
    CHECK(action_value(vis, keys::A) == 0.f);
}

// afterhours gates each editing feature on the enumerator NAME existing
// (`if constexpr enum_contains<InputAction>("TextWordLeft")`), so a missing
// name removes the feature with no diagnostic. Assert the names are present:
// this is the check that fails first if someone tidies the enum.
static void test_enum_carries_the_names_afterhours_gates_on() {
    std::printf("test_enum_carries_the_names_afterhours_gates_on\n");
    for (const char* name : {"TextWordLeft", "TextWordRight",
                             "TextDeleteWordBack", "TextDeleteWordForward",
                             "TextSelectAll", "TextHome", "TextEnd"}) {
        const bool present = magic_enum::enum_contains<InputAction>(name);
        if (!present) {
            std::printf("  FAIL: InputAction has no enumerator %s\n", name);
            ++g_failures;
        }
    }
}

int main() {
    std::printf("== test_input_pipeline (key->action mapping) ==\n");
    test_backspace_maps_to_textbackspace();
    test_enter_maps_to_widgetpress();
    test_space_not_bound_to_actions();
    test_word_chords_are_bound_to_option();
    test_word_chords_need_their_modifier();
    test_line_chords_are_bound_to_command();
    test_select_all_is_bound();
    test_tab_focus_bindings_survive();
    test_enum_carries_the_names_afterhours_gates_on();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
