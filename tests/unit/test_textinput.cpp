// tests/unit/test_textinput.cpp
// Deterministic test for the composer text-input bug Gabe hit on macOS:
//   "hitting backspace adds a space, hitting space does nothing"
//
// ROOT CAUSE (traced, no guessing): the sokol macOS backend
// (vendor/afterhours/vendor/sokol/sokol_app.h keyDown:) emits a
// SAPP_EVENTTYPE_CHAR for EVERY key whose NSEvent.characters is non-empty —
// including BACKSPACE (characters = U+007F DEL). The afterhours sokol backend
// pushes ANY char_code > 0 into the char queue. The text_input widget drains it
// via insert_char(), whose only guard is `codepoint < 32` — so 0x7F (127) is
// NOT rejected and gets INSERTED as a DEL glyph (renders like a space/box) ->
// "backspace adds a space". hanabi::is_typable_char() is the fix (filter applied
// in the composer's char drain).
//
// This test drives the REAL afterhours text_input state + insert_char with the
// exact codepoints the macOS backend produces, gated by the hanabi filter, and
// asserts correct text.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/textinput_filter.h"
#include "afterhours/src/plugins/ui/text_input/state.h"
#include "afterhours/src/plugins/ui/text_input/utils.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);     \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using afterhours::text_input::HasTextInputState;
using afterhours::text_input::insert_char;
using afterhours::text_input::delete_before_cursor;

// Simulate the composer receiving a sequence of macOS CHAR codepoints, gated by
// hanabi's is_typable_char filter (the fix): only codepoints the filter accepts
// reach insert_char, exactly as the patched hanabi char-drain does.
static std::string type_macos(const std::string& start,
                              const std::vector<int>& char_codes) {
    HasTextInputState s;
    s.storage.insert(0, start);
    s.cursor_position = start.size();
    for (int cp : char_codes) {
        if (hanabi::is_typable_char(cp)) insert_char(s, cp);
    }
    return s.text();
}

static void test_space_types() {
    std::printf("test_space_types\n");
    CHECK(type_macos("ab", {0x20, 'c', 'd'}) == "ab cd");
}

static void test_backspace_char_not_inserted() {
    std::printf("test_backspace_char_not_inserted\n");
    std::string out = type_macos("abc", {0x7F});
    CHECK(out == "abc");
    CHECK(out.find('\x7f') == std::string::npos);
}

static void test_control_codes_filtered() {
    std::printf("test_control_codes_filtered\n");
    CHECK(type_macos("x", {0x08, 0x0D, 0x1B, 0x7F, 'y'}) == "xy");
}

static void test_backspace_action_deletes() {
    std::printf("test_backspace_action_deletes\n");
    HasTextInputState s;
    s.storage.insert(0, "hello");
    s.cursor_position = 5;
    delete_before_cursor(s);
    CHECK(s.text() == "hell");
}

static void test_printable_unaffected() {
    std::printf("test_printable_unaffected\n");
    CHECK(type_macos("", {'H', 'i', 0x20, '5', '!'}) == "Hi 5!");
}

// Guard the raw predicate directly too.
static void test_filter_predicate() {
    std::printf("test_filter_predicate\n");
    CHECK(hanabi::is_typable_char(0x20));   // space
    CHECK(hanabi::is_typable_char('a'));
    CHECK(hanabi::is_typable_char(0x09));   // tab
    CHECK(!hanabi::is_typable_char(0x7F));  // DEL (backspace on macOS)
    CHECK(!hanabi::is_typable_char(0x08));  // BS
    CHECK(!hanabi::is_typable_char(0x0D));  // CR
    CHECK(!hanabi::is_typable_char(0x1B));  // ESC
    CHECK(hanabi::is_typable_char(0x263A)); // ☺ (unicode printable)
}

int main() {
    std::printf("== test_textinput (composer key handling) ==\n");
    test_space_types();
    test_backspace_char_not_inserted();
    test_control_codes_filtered();
    test_backspace_action_deletes();
    test_printable_unaffected();
    test_filter_predicate();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
