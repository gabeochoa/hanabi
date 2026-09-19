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

using ecs::model::composer_holds_keyboard;
using ecs::model::KeyboardClaim;

static void test_a_composer_alone_owns_the_keyboard() {
    KeyboardClaim claim;
    CHECK(composer_holds_keyboard(claim));
    claim.observe(0, true, false, true);
    CHECK(composer_holds_keyboard(claim));
}

static void test_a_surface_raised_after_the_composer_takes_the_keyboard() {
    KeyboardClaim claim;
    claim.observe(0, true, false, true);
    CHECK(composer_holds_keyboard(claim));
    claim.observe(ecs::model::kSurfaceModalSheet, true, false, false);
    CHECK(!composer_holds_keyboard(claim));
}

static void test_a_composer_claimed_after_a_surface_keeps_the_keyboard() {
    KeyboardClaim claim;
    claim.observe(ecs::model::kSurfaceAskWaiting, false, false, false);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(ecs::model::kSurfaceAskWaiting, true, false, true);
    CHECK(composer_holds_keyboard(claim));
}

static void test_a_second_surface_outranks_a_reclaimed_composer() {
    KeyboardClaim claim;
    claim.observe(ecs::model::kSurfaceAskWaiting, false, false, false);
    claim.observe(ecs::model::kSurfaceAskWaiting, true, false, true);
    CHECK(composer_holds_keyboard(claim));
    claim.observe(ecs::model::kSurfaceAskWaiting | ecs::model::kSurfaceRowMenu,
                  true, false, false);
    CHECK(!composer_holds_keyboard(claim));
}

static void test_a_surface_leaving_hands_the_keyboard_back() {
    KeyboardClaim claim;
    claim.observe(0, true, false, true);
    claim.observe(ecs::model::kSurfaceModalSheet, true, false, false);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(0, false, false, false);
    CHECK(composer_holds_keyboard(claim));
}

static void test_only_a_deliberate_caret_is_a_claim() {
    KeyboardClaim grabbed;
    grabbed.observe(ecs::model::kSurfaceModalSheet, false, false, false);
    grabbed.observe(ecs::model::kSurfaceModalSheet, true, false, false);
    CHECK(!composer_holds_keyboard(grabbed));

    KeyboardClaim clicked;
    clicked.observe(ecs::model::kSurfaceModalSheet, false, false, false);
    clicked.observe(ecs::model::kSurfaceModalSheet, true, false, true);
    CHECK(composer_holds_keyboard(clicked));
}

static void test_a_caret_that_never_left_is_not_a_fresh_claim() {
    KeyboardClaim claim;
    claim.observe(0, true, false, true);
    claim.observe(ecs::model::kSurfaceAskWaiting, true, false, false);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(ecs::model::kSurfaceAskWaiting, true, false, true);
    CHECK(!composer_holds_keyboard(claim));
}

static void test_a_surface_still_up_never_loses_to_an_older_claim() {
    for (int bits = 0; bits < 8; ++bits) {
        KeyboardClaim claim;
        claim.observe(0, (bits & 1) != 0, false, (bits & 2) != 0);
        claim.observe(ecs::model::kSurfaceFind, (bits & 4) != 0, false, false);
        CHECK(!composer_holds_keyboard(claim));
    }
}

// The third claimant. No surface is up in any of these: the sidebar's search
// is the case the surface set cannot describe, and it has to outrank the
// composer on its own.
static void test_a_field_claimed_after_the_composer_takes_the_keyboard() {
    KeyboardClaim claim;
    claim.observe(0, true, false, true);
    CHECK(composer_holds_keyboard(claim));
    claim.observe(0, false, true, true);
    CHECK(!composer_holds_keyboard(claim));
}

static void test_a_composer_clicked_after_a_field_takes_it_back() {
    KeyboardClaim claim;
    claim.observe(0, false, true, true);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(0, true, false, true);
    CHECK(composer_holds_keyboard(claim));
}

static void test_a_caret_leaving_a_field_hands_the_keyboard_back() {
    KeyboardClaim claim;
    claim.observe(0, true, false, true);
    claim.observe(0, false, true, true);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(0, false, false, false);
    CHECK(composer_holds_keyboard(claim));
}

static void test_only_a_deliberate_caret_in_a_field_is_a_claim() {
    KeyboardClaim grabbed;
    grabbed.observe(0, true, false, true);
    grabbed.observe(0, false, true, false);
    CHECK(composer_holds_keyboard(grabbed));

    KeyboardClaim clicked;
    clicked.observe(0, true, false, true);
    clicked.observe(0, false, true, true);
    CHECK(!composer_holds_keyboard(clicked));
}

// The order between the two is the whole of it, exactly as it is for a
// surface: a field the reader left and came back to outranks the composer
// again, and a composer clicked after the field outranks the field.
static void test_a_field_and_the_composer_trade_the_keyboard_by_order() {
    KeyboardClaim claim;
    claim.observe(0, false, true, true);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(0, true, false, true);
    CHECK(composer_holds_keyboard(claim));
    claim.observe(0, false, true, true);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(0, true, false, true);
    CHECK(composer_holds_keyboard(claim));
}

static void test_a_live_field_never_loses_to_an_older_claim() {
    for (int bits = 0; bits < 4; ++bits) {
        KeyboardClaim claim;
        claim.observe((bits & 1) != 0 ? ecs::model::kSurfaceFind : 0u, true,
                      false, true);
        claim.observe((bits & 2) != 0 ? ecs::model::kSurfaceFind : 0u, false,
                      true, true);
        CHECK(!composer_holds_keyboard(claim));
    }
}

// A field the caret has left stops outranking the composer even while the
// surface that was up when it claimed is still there, so the two claimants are
// read independently rather than as one "somebody else" flag.
static void test_a_surface_and_a_field_are_ranked_separately() {
    KeyboardClaim claim;
    claim.observe(ecs::model::kSurfaceFind, false, false, false);
    claim.observe(ecs::model::kSurfaceFind, true, false, true);
    CHECK(composer_holds_keyboard(claim));
    claim.observe(ecs::model::kSurfaceFind, false, true, true);
    CHECK(!composer_holds_keyboard(claim));
    claim.observe(ecs::model::kSurfaceFind, false, false, false);
    CHECK(composer_holds_keyboard(claim));
}

static void test_a_yielded_caret_returns_to_the_pane_that_yielded_it() {
    using ecs::model::CaretYield;
    using ecs::model::ComposerIds;
    const ComposerIds pane0{85, 84};
    const ComposerIds pane1{100, 99};
    const long long FAKE = -2;
    const auto frame = [](CaretYield& y, long long focus, bool anyComposer, bool owns,
                          const ComposerIds& ids0, const ComposerIds& ids1) {
        struct Out { bool yield0, restore0, yield1, restore1; } o{};
        long long f = focus;
        o.yield0 = y.should_yield(f, ids0, anyComposer, owns);
        if (o.yield0) { y.yield(0); f = -2; }
        o.restore0 = y.should_restore(0, owns, anyComposer && f != -2);
        if (o.restore0) y.settled();
        o.yield1 = y.should_yield(f, ids1, anyComposer && f != -2, owns);
        if (o.yield1) { y.yield(1); f = -2; }
        o.restore1 = y.should_restore(1, owns, anyComposer && f != -2);
        if (o.restore1) y.settled();
        return o;
    };
    (void)FAKE;

    {
        CaretYield y;
        auto o = frame(y, 100, true, false, pane0, pane1);
        CHECK(!o.yield0 && !o.restore0 && o.yield1 && !o.restore1);
        CHECK(y.yielded() && y.pane == 1);
        o = frame(y, -2, false, false, pane0, pane1);
        CHECK(!o.yield0 && !o.restore0 && !o.yield1 && !o.restore1);
        o = frame(y, -2, false, true, pane0, pane1);
        CHECK(!o.restore0 && o.restore1);
        CHECK(!y.yielded());
    }
    {
        CaretYield y;
        auto o = frame(y, 99, true, false, pane0, pane1);
        CHECK(!o.yield0 && o.yield1 && y.pane == 1);
        o = frame(y, -2, false, true, pane0, pane1);
        CHECK(!o.restore0 && o.restore1 && !y.yielded());
    }
    {
        CaretYield y;
        auto o = frame(y, 85, true, false, pane0, pane1);
        CHECK(o.yield0 && !o.yield1 && y.pane == 0);
        o = frame(y, -2, false, true, pane0, pane1);
        CHECK(o.restore0 && !o.restore1 && !y.yielded());
    }
    {
        CaretYield y;
        CHECK(y.should_yield(85, pane0, true, false));
        y.yield(0);
        CHECK(!y.should_restore(0, false, false));
        CHECK(y.should_restore(0, true, false));
        y.settled();
        CHECK(!y.yielded());
    }
    {
        CaretYield y;
        CHECK(!y.should_yield(500, pane0, false, false));
        CHECK(!y.should_yield(100, pane0, true, false));
        CHECK(!y.should_yield(-1, pane0, true, false));
        y.yield(1);
        CHECK(y.should_yield(85, pane0, true, false));
        y.yield(0);
        CHECK(y.pane == 0);
        CHECK(!y.should_restore(1, true, false) && y.should_restore(0, true, false));
    }
    {
        CaretYield y;
        y.yield(1);
        CHECK(!y.should_restore(1, true, true));
        CHECK(!y.should_restore(0, true, true));
        CHECK(y.should_yield(85, pane0, true, false));
        y.yield(0);
        CHECK(!y.should_restore(1, true, false) && y.should_restore(0, true, false));
    }
    {
        using In = CaretYield::ReconcileInputs;
        const auto split = [&](long long focus, int settingsPane = -1, bool owns = false) {
            In in; in.splitView = true; in.settingsPane = settingsPane; in.settingsTab = false;
            in.focusedPane = 1; in.focusIdAtStart = focus; in.composerOwnsInput = owns;
            in.ids[0] = pane0; in.ids[1] = pane1; return in;
        };
        const auto single = [&](long long focus, int focusedPane, bool settingsTab) {
            In in; in.splitView = false; in.settingsPane = settingsTab ? focusedPane : -1;
            in.settingsTab = settingsTab; in.focusedPane = focusedPane; in.focusIdAtStart = focus;
            in.ids[0] = pane0; in.ids[1] = pane1; return in;
        };
        CaretYield y;
        y.yield(1);
        y.reconcile(split(-2, 1));
        CHECK(!y.yielded());
        y.yield(1);
        y.reconcile(single(-2, 1, true));
        CHECK(!y.yielded());
        y.yield(1);
        y.reconcile(single(-2, 0, false));
        CHECK(!y.yielded());
        y.yield(0);
        y.reconcile(single(-2, 0, false));
        CHECK(y.yielded() && y.pane == 0);
        y.settled();
        y.yield(1);
        y.reconcile(split(-2));
        CHECK(y.yielded() && y.pane == 1);
        y.reconcile(split(0));
        CHECK(y.yielded() && y.pane == 1);
        y.reconcile(split(-1));
        CHECK(y.yielded() && y.pane == 1);
        y.reconcile(split(100));
        CHECK(y.yielded() && y.pane == 1);
        y.reconcile(split(99));
        CHECK(y.yielded() && y.pane == 1);
        y.reconcile(split(700));
        CHECK(y.yielded() && y.pane == 1);
        y.reconcile(split(85));
        CHECK(!y.yielded());
        CHECK(!y.should_restore(1, true, true) && !y.should_restore(0, true, true));
        y.yield(1);
        y.reconcile(split(100, -1, true));
        CHECK(!y.yielded());
        y.reconcile(split(0, -1, true));
        CHECK(!y.yielded());
        CHECK(!y.should_restore(1, true, false) && !y.should_restore(0, true, false));
        y.yield(1);
        y.reconcile(split(99, -1, true));
        CHECK(!y.yielded());
        y.yield(1);
        y.reconcile(split(100, -1, false));
        CHECK(y.yielded() && y.pane == 1);
        y.reconcile(split(-2, -1, true));
        CHECK(y.yielded() && y.pane == 1);
        CHECK(y.should_restore(1, true, false));
        y.yield(1);
        y.reconcile(split(84));
        CHECK(!y.yielded());
        CHECK(y.should_yield(84, pane0, true, false));
        y.yield(0);
        CHECK(y.pane == 0);
        CHECK(CaretYield::composer_draws(true, 0, -1, false, 1) && CaretYield::composer_draws(true, 1, -1, false, 1));
        CHECK(!CaretYield::composer_draws(true, 1, 1, false, 0) && CaretYield::composer_draws(true, 0, 1, false, 0));
        CHECK(!CaretYield::composer_draws(false, 1, -1, false, 0) && CaretYield::composer_draws(false, 0, -1, false, 0));
        CHECK(!CaretYield::composer_draws(false, 0, 0, true, 0) && !CaretYield::composer_draws(false, 1, 0, true, 0));
    }
    {
        ecs::model::KeyboardClaim claim;
        claim.observe(0, true, false, true);
        CHECK(ecs::model::composer_holds_keyboard(claim));
        claim.observe(1, true, false, false);
        CHECK(!ecs::model::composer_holds_keyboard(claim));
        CaretYield y;
        CHECK(y.should_yield(100, pane1, true, ecs::model::composer_holds_keyboard(claim)));
        y.yield(1);
        CHECK(!y.should_restore(0, ecs::model::composer_holds_keyboard(claim), false));
        CHECK(!y.should_restore(1, ecs::model::composer_holds_keyboard(claim), false));
        claim.observe(0, false, false, false);
        CHECK(ecs::model::composer_holds_keyboard(claim));
        CHECK(!y.should_restore(0, ecs::model::composer_holds_keyboard(claim), false));
        CHECK(y.should_restore(1, ecs::model::composer_holds_keyboard(claim), false));
    }
    {
        CaretYield y;
        y.yield(1);
        CHECK(!y.should_restore(1, true, true));
        CHECK(!y.should_restore(0, true, true));
        CHECK(y.should_restore(1, true, false));
    }
    {
        ecs::model::KeyboardClaim claim;
        claim.observe(0, true, false, true);
        claim.observe(1, true, false, false);
        CaretYield y;
        y.yield(1);
        claim.observe(0, false, true, true);
        CHECK(!ecs::model::composer_holds_keyboard(claim));
        CHECK(!y.should_restore(1, ecs::model::composer_holds_keyboard(claim), false));
        CHECK(!y.should_yield(700, pane1, false, ecs::model::composer_holds_keyboard(claim)));
        claim.observe(0, false, false, false);
        CHECK(ecs::model::composer_holds_keyboard(claim));
        CHECK(y.should_restore(1, ecs::model::composer_holds_keyboard(claim), false));
        CHECK(!y.should_restore(0, ecs::model::composer_holds_keyboard(claim), false));
    }
}

int main() {
    test_a_yielded_caret_returns_to_the_pane_that_yielded_it();
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
    test_a_composer_alone_owns_the_keyboard();
    test_a_surface_raised_after_the_composer_takes_the_keyboard();
    test_a_composer_claimed_after_a_surface_keeps_the_keyboard();
    test_a_second_surface_outranks_a_reclaimed_composer();
    test_a_surface_leaving_hands_the_keyboard_back();
    test_only_a_deliberate_caret_is_a_claim();
    test_a_caret_that_never_left_is_not_a_fresh_claim();
    test_a_surface_still_up_never_loses_to_an_older_claim();
    test_a_field_claimed_after_the_composer_takes_the_keyboard();
    test_a_composer_clicked_after_a_field_takes_it_back();
    test_a_caret_leaving_a_field_hands_the_keyboard_back();
    test_only_a_deliberate_caret_in_a_field_is_a_claim();
    test_a_field_and_the_composer_trade_the_keyboard_by_order();
    test_a_live_field_never_loses_to_an_older_claim();
    test_a_surface_and_a_field_are_ranked_separately();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
