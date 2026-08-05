// tests/unit/test_textinput.cpp
// Deterministic test for the composer text-input bug Gabe hit on macOS:
//   "hitting backspace adds a space, hitting space does nothing"
//
// ROOT CAUSE (traced, no guessing): the sokol macOS backend
// (vendor/afterhours/vendor/sokol/sokol_app.h keyDown:) emits a
// SAPP_EVENTTYPE_CHAR for EVERY key whose NSEvent.characters is non-empty —
// including BACKSPACE (characters = U+007F DEL). The afterhours sokol backend
// pushed ANY char_code > 0 into the char queue, and insert_char's only guard
// was `codepoint < 32` — so 0x7F (127) was NOT rejected and got INSERTED as a
// DEL glyph (renders like a space/box) -> "backspace adds a space".
//
// FIXED UPSTREAM (afterhours gap #31): insert_char now rejects C0, DEL and C1,
// and the sokol backend no longer queues control codes at all. hanabi's
// ComposerCharFilterSystem and is_typable_char existed only to work around
// this and are DELETED.
//
// This test now drives afterhours' insert_char with the exact codepoints the
// macOS backend produces and NO filter of our own. If it passes, upstream is
// carrying it; if it ever fails, the workaround has to come back.

#include <cstdio>
#include <string>
#include <vector>

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

// Simulate the composer receiving a sequence of macOS CHAR codepoints, handed
// to insert_char UNFILTERED -- which is what hanabi does now that upstream
// rejects control codes itself.
static std::string type_macos(const std::string& start,
                              const std::vector<int>& char_codes) {
    HasTextInputState s;
    s.storage.insert(0, start);
    s.cursor_position = start.size();
    for (int cp : char_codes) {
        insert_char(s, cp);
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

// Guard upstream's predicate directly too, including the C1 block that
// hanabi's own filter never covered.
static void test_upstream_rejects_control_codes() {
    std::printf("test_upstream_rejects_control_codes\n");
    using afterhours::text_input::is_control_codepoint;
    CHECK(!is_control_codepoint(0x20));  // space
    CHECK(!is_control_codepoint('a'));
    CHECK(!is_control_codepoint(0x09));  // tab is text
    CHECK(is_control_codepoint(0x7F));   // DEL (backspace on macOS)
    CHECK(is_control_codepoint(0x08));   // BS
    CHECK(is_control_codepoint(0x0D));   // CR
    CHECK(is_control_codepoint(0x1B));   // ESC
    CHECK(is_control_codepoint(0x85));   // C1 NEL
    CHECK(!is_control_codepoint(0x263A)); // ☺ (unicode printable)
}

int main() {
    std::printf("== test_textinput (composer key handling) ==\n");
    test_space_types();
    test_backspace_char_not_inserted();
    test_control_codes_filtered();
    test_backspace_action_deletes();
    test_printable_unaffected();
    test_upstream_rejects_control_codes();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
