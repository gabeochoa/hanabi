// Where a bare keystroke and a pane click put the caret, tested without a
// window (src/ecs/focus_routing.h).
//
// The two rules share a gate on purpose -- a click and a keystroke both belong
// to the composer only when nothing above it owns the keyboard -- and they part
// on exactly one thing: a modifier. That is the shape the tests below assert,
// rather than each arm on its own, because a rule added later that forgot the
// shared gate would pass an arm-by-arm suite unchanged.
#include <cstdio>
#include <string>

#include "../../src/ecs/focus_routing.h"

static int g_failures = 0;
#define CHECK(value)                                             \
    do {                                                         \
        if (!(value)) {                                          \
            std::printf("FAIL: %s line %d\n", #value, __LINE__); \
            ++g_failures;                                        \
        }                                                        \
    } while (0)

using ecs::model::composer_takes_typing;
using ecs::model::FocusRouting;
using ecs::model::pane_click_takes_caret;

static FocusRouting open_transcript() {
    FocusRouting in;
    in.typingLive = true;
    in.chatView = true;
    return in;
}

static void test_a_transcript_takes_both() {
    const FocusRouting in = open_transcript();
    CHECK(pane_click_takes_caret(in));
    CHECK(composer_takes_typing(in));
}

static void test_an_owner_above_the_composer_takes_neither() {
    FocusRouting in = open_transcript();
    in.typingLive = false;
    CHECK(!pane_click_takes_caret(in));
    CHECK(!composer_takes_typing(in));
}

static void test_a_focused_field_keeps_what_it_has() {
    FocusRouting in = open_transcript();
    in.textFieldFocused = true;
    CHECK(!pane_click_takes_caret(in));
    CHECK(!composer_takes_typing(in));
}

static void test_only_chat_routes() {
    FocusRouting in = open_transcript();
    in.chatView = false;
    CHECK(!pane_click_takes_caret(in));
    CHECK(!composer_takes_typing(in));
}

// The one place the two rules differ. A click with Cmd held is still a click
// at a pane; a keystroke with Cmd held is a chord somebody else answers.
static void test_a_modifier_separates_a_chord_from_typing() {
    FocusRouting in = open_transcript();
    in.modifierHeld = true;
    CHECK(pane_click_takes_caret(in));
    CHECK(!composer_takes_typing(in));
}

// Typing is never routed anywhere a click would not be.
static void test_typing_never_outruns_a_click() {
    for (int bits = 0; bits < 16; ++bits) {
        FocusRouting in;
        in.typingLive = (bits & 1) != 0;
        in.textFieldFocused = (bits & 2) != 0;
        in.chatView = (bits & 4) != 0;
        in.modifierHeld = (bits & 8) != 0;
        if (composer_takes_typing(in)) CHECK(pane_click_takes_caret(in));
    }
}

static void test_control_characters_are_not_text() {
    using ecs::model::typed_char_is_text;
    CHECK(!typed_char_is_text(0));
    CHECK(!typed_char_is_text('\n'));
    CHECK(!typed_char_is_text('\t'));
    CHECK(!typed_char_is_text(27));
    CHECK(!typed_char_is_text(31));
    CHECK(!typed_char_is_text(127));
    CHECK(typed_char_is_text(' '));
    CHECK(typed_char_is_text('a'));
    CHECK(typed_char_is_text('~'));
    CHECK(typed_char_is_text(0x00E9));
}

// A parked character is handed to the field as bytes, so a non-ASCII first
// keystroke has to survive the trip rather than being dropped or truncated.
static void test_a_parked_character_keeps_its_bytes() {
    using ecs::model::append_utf8;
    std::string out;
    append_utf8(out, 'h');
    CHECK(out == "h");
    out.clear();
    append_utf8(out, 0x00E9);  // e-acute
    CHECK(out == "\xC3\xA9");
    out.clear();
    append_utf8(out, 0x4E2D);  // CJK
    CHECK(out == "\xE4\xB8\xAD");
    out.clear();
    append_utf8(out, 0x1F600);  // emoji
    CHECK(out == "\xF0\x9F\x98\x80");
    out.clear();
    append_utf8(out, 'a');
    append_utf8(out, 'b');
    CHECK(out == "ab");
}

static void test_an_emoji_arrives_as_two_units_and_leaves_as_one_character() {
    using ecs::model::TypedRun;
    TypedRun run;
    run.offer(0xD83D);
    CHECK(run.text.empty());
    run.offer(0xDE00);
    CHECK(run.text == "\xF0\x9F\x98\x80");

    TypedRun after_text;
    for (int unit : std::initializer_list<int>{0x68, 0x69, 0xD83D, 0xDE00})
        after_text.offer(unit);
    CHECK(after_text.text == "hi\xF0\x9F\x98\x80");
}

static void test_an_unpaired_surrogate_never_reaches_the_draft() {
    using ecs::model::TypedRun;

    TypedRun trailing;
    trailing.offer('a');
    trailing.offer(0xD83D);
    CHECK(trailing.text == "a");

    TypedRun leading;
    leading.offer(0xDE00);
    leading.offer('a');
    CHECK(leading.text == "a");

    TypedRun abandoned;
    abandoned.offer(0xD83D);
    abandoned.offer('a');
    CHECK(abandoned.text == "a");

    TypedRun restarted;
    restarted.offer(0xD83D);
    restarted.offer(0xD83D);
    restarted.offer(0xDE00);
    CHECK(restarted.text == "\xF0\x9F\x98\x80");

    CHECK(!ecs::model::typed_char_is_text(0xD83D));
    CHECK(!ecs::model::typed_char_is_text(0xDE00));
    CHECK(ecs::model::typed_char_is_text(0xD7FF));
    CHECK(ecs::model::typed_char_is_text(0xE000));
}

int main() {
    test_a_transcript_takes_both();
    test_an_owner_above_the_composer_takes_neither();
    test_a_focused_field_keeps_what_it_has();
    test_only_chat_routes();
    test_a_modifier_separates_a_chord_from_typing();
    test_typing_never_outruns_a_click();
    test_control_characters_are_not_text();
    test_a_parked_character_keeps_its_bytes();
    test_an_emoji_arrives_as_two_units_and_leaves_as_one_character();
    test_an_unpaired_surrogate_never_reaches_the_draft();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
