#include <cstdio>
#include <string>

#include "../../src/ecs/text_edit_actions.h"

namespace ea = ecs::edit_actions;
using afterhours::text_input::HasTextAreaState;
using afterhours::text_input::HasTextInputState;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static std::string g_clip;
static int g_reads = 0;
static int g_writes = 0;
static ea::Clipboard counting() {
    return ea::Clipboard{[] { ++g_reads; return g_clip; },
                         [](std::string_view t) { ++g_writes; g_clip.assign(t); }};
}
static void reset() { g_clip.clear(); g_reads = 0; g_writes = 0; }

static void select(auto& s, size_t from, size_t to) {
    s.selection_anchor = from;
    s.cursor_position = to;
}

static void test_copy_needs_a_selection_and_writes_it() {
    reset();
    HasTextInputState s("hello world", 0, 0.5f);
    CHECK(!ea::copy(s, counting()) && g_writes == 0);
    select(s, 6, 11);
    CHECK(ea::copy(s, counting()) && g_writes == 1 && g_clip == "world");
    CHECK(s.text() == "hello world" && s.has_selection());
}

static void test_cut_moves_the_selection_and_undo_brings_it_back() {
    reset();
    HasTextInputState s("hello world", 0, 0.5f);
    select(s, 0, 6);
    CHECK(ea::cut(s, counting()) && g_clip == "hello " && s.text() == "world" && s.cursor_position == 0);
    CHECK(!s.has_selection());
    CHECK(ea::undo(s) && s.text() == "hello world");
    CHECK(ea::redo(s) && s.text() == "world");
    s.readonly = true;
    select(s, 0, 2);
    CHECK(!ea::cut(s, counting()) && s.text() == "world" && g_writes == 1);
    CHECK(!ea::undo(s) && !ea::redo(s));
}

static void test_paste_replaces_a_selection_and_is_one_undo_step() {
    reset();
    HasTextInputState s("abc", 0, 0.5f);
    g_clip = "XY";
    s.cursor_position = 3;
    CHECK(ea::paste_text(s, counting()) && s.text() == "abcXY" && s.cursor_position == 5 && g_reads == 1);
    select(s, 0, 3);
    g_clip = "Q";
    CHECK(ea::paste_text(s, counting()) && s.text() == "QXY" && !s.has_selection());
    CHECK(ea::undo(s) && s.text() == "abcXY");
    CHECK(ea::undo(s) && s.text() == "abc");
}

static void test_empty_clipboard_pastes_nothing_and_leaves_undo_alone() {
    reset();
    HasTextInputState s("abc", 0, 0.5f);
    const size_t before = s.undo_stack.size();
    CHECK(!ea::paste_text(s, counting()) && s.text() == "abc" && g_reads == 1);
    CHECK(s.undo_stack.size() == before);
    s.readonly = true;
    g_clip = "x";
    CHECK(!ea::paste_text(s, counting()) && s.text() == "abc" && g_reads == 1);
}

static void test_newlines_paste_into_an_area_and_drop_from_an_input() {
    reset();
    g_clip = "one\ntwo";
    HasTextInputState line("", 0, 0.5f);
    CHECK(ea::paste_text(line, counting()) && line.text() == "onetwo");
    HasTextAreaState area("", 0, 0.5f);
    CHECK(ea::paste_text(area, counting()) && area.text() == "one\ntwo");
}

static void test_unicode_keeps_codepoint_boundaries() {
    reset();
    HasTextInputState s("", 0, 0.5f);
    g_clip = "caf\xc3\xa9 \xf0\x9f\x8e\x89!";
    CHECK(ea::paste_text(s, counting()) && s.text() == g_clip && s.cursor_position == s.text_size());
    select(s, 3, 5);
    CHECK(ea::cut(s, counting()) && g_clip == "\xc3\xa9" && s.text() == "caf \xf0\x9f\x8e\x89!");
    select(s, 4, 8);
    CHECK(ea::copy(s, counting()) && g_clip == "\xf0\x9f\x8e\x89");
}

static void test_select_all_and_perform_on_dispatch() {
    reset();
    HasTextAreaState s("ab\ncd", 0, 0.5f);
    CHECK(ea::perform_on(s, hanabi::EditVerb::SelectAll, counting()));
    CHECK(s.selection_anchor == 0 && s.cursor_position == 5 && s.selected_text() == "ab\ncd");
    CHECK(ea::perform_on(s, hanabi::EditVerb::Copy, counting()) && g_clip == "ab\ncd");
    CHECK(!ea::perform_on(s, hanabi::EditVerb::Count, counting()));
    CHECK(g_reads == 0 && g_writes == 1);
}

static void test_ownership_table() {
    using hanabi::EditVerb;
    using ea::EditOwner;
    const ea::Focus none{};
    const ea::Focus composer{true, true, false};
    const ea::Focus otherArea{false, true, false};
    const ea::Focus input{false, false, true};
    CHECK(ea::owner_for(EditVerb::Copy, none, false) == EditOwner::Transcript);
    CHECK(ea::owner_for(EditVerb::Copy, none, true) == EditOwner::Transcript);
    CHECK(ea::owner_for(EditVerb::Copy, input, false) == EditOwner::Field);
    CHECK(ea::owner_for(EditVerb::Copy, composer, true) == EditOwner::Field);
    for (const EditVerb v : {EditVerb::Undo, EditVerb::Redo, EditVerb::Cut, EditVerb::Paste, EditVerb::SelectAll}) {
        CHECK(ea::owner_for(v, none, false) == EditOwner::None);
        CHECK(ea::owner_for(v, input, false) == EditOwner::Field);
        CHECK(ea::owner_for(v, otherArea, true) == EditOwner::Field);
    }
    CHECK(ea::owner_for(EditVerb::Paste, composer, true) == EditOwner::Image);
    CHECK(ea::owner_for(EditVerb::Paste, composer, false) == EditOwner::Field);
    CHECK(ea::owner_for(EditVerb::Paste, none, true) == EditOwner::None);
    CHECK(ea::owner_for(EditVerb::Cut, composer, true) == EditOwner::Field);
}

static void test_pending_verbs_drain_in_order_on_the_named_focused_field_and_sync_its_binding() {
    reset();
    auto& pending = ea::pending_field_edits();
    pending.clear();
    HasTextAreaState composer("hello world", 0, 0.5f);
    std::string bound = "hello world";
    HasTextInputState other("elsewhere", 0, 0.5f);
    std::string otherBound = "elsewhere";
    composer.is_focused = true;
    other.is_focused = true;
    const int composerId = 7;
    const int otherId = 9;
    CHECK(!ea::apply_pending(composerId, composer, bound, counting()));

    pending.push(composerId, hanabi::EditVerb::SelectAll);
    pending.push(composerId, hanabi::EditVerb::Cut);
    CHECK(!ea::apply_pending(otherId, other, otherBound, counting()) && otherBound == "elsewhere");
    CHECK(pending.verbs.size() == 2);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()));
    CHECK(composer.text().empty() && bound.empty() && g_clip == "hello world");
    CHECK(pending.verbs.empty() && pending.target == -1);
    CHECK(!ea::apply_pending(composerId, composer, bound, counting()));

    pending.push(composerId, hanabi::EditVerb::Undo);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello world" &&
          bound == "hello world");

    composer.clear_selection();
    composer.cursor_position = composer.text_size();
    for (const char* piece : {"1", "2", "3"}) {
        g_clip = piece;
        pending.push(composerId, hanabi::EditVerb::Paste);
        CHECK(ea::apply_pending(composerId, composer, bound, counting()));
    }
    CHECK(composer.text() == "hello world123" && bound == composer.text());
    pending.push(composerId, hanabi::EditVerb::Undo);
    pending.push(composerId, hanabi::EditVerb::Undo);
    pending.push(composerId, hanabi::EditVerb::Undo);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello world" &&
          bound == "hello world" && pending.verbs.empty());
    pending.push(composerId, hanabi::EditVerb::Redo);
    pending.push(composerId, hanabi::EditVerb::Redo);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello world12");
    pending.push(composerId, hanabi::EditVerb::Undo);
    pending.push(composerId, hanabi::EditVerb::Undo);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello world");

    g_clip = "X";
    pending.push(composerId, hanabi::EditVerb::SelectAll);
    pending.push(composerId, hanabi::EditVerb::Copy);
    pending.push(composerId, hanabi::EditVerb::Paste);
    CHECK(!ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello world" &&
          bound == "hello world" && g_clip == "hello world" && pending.verbs.empty() && pending.target == -1);
    CHECK(!composer.has_selection() && composer.cursor_position == 11);

    composer.clear_selection();
    composer.cursor_position = 5;
    g_clip = "+";
    pending.push(composerId, hanabi::EditVerb::Paste);
    pending.push(composerId, hanabi::EditVerb::Paste);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello++ world" &&
          bound == composer.text() && composer.cursor_position == 7);
    pending.push(composerId, hanabi::EditVerb::Undo);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello+ world");
    pending.push(composerId, hanabi::EditVerb::Undo);
    CHECK(ea::apply_pending(composerId, composer, bound, counting()) && composer.text() == "hello world");

    pending.push(composerId, hanabi::EditVerb::SelectAll);
    CHECK(!ea::apply_pending(composerId, composer, bound, counting()) && composer.has_selection() &&
          bound == "hello world");

    pending.push(composerId, hanabi::EditVerb::Cut);
    pending.push(otherId, hanabi::EditVerb::Paste);
    CHECK(pending.target == otherId && pending.verbs.size() == 1);
    composer.is_focused = false;
    CHECK(!ea::apply_pending(composerId, composer, bound, counting()));
    other.cursor_position = other.text_size();
    g_clip = "!";
    CHECK(ea::apply_pending(otherId, other, otherBound, counting()) && otherBound == "elsewhere!");
}

int main() {
    std::printf("=== test_text_edit_actions ===\n");
    test_ownership_table();
    test_pending_verbs_drain_in_order_on_the_named_focused_field_and_sync_its_binding();
    test_copy_needs_a_selection_and_writes_it();
    test_cut_moves_the_selection_and_undo_brings_it_back();
    test_paste_replaces_a_selection_and_is_one_undo_step();
    test_empty_clipboard_pastes_nothing_and_leaves_undo_alone();
    test_newlines_paste_into_an_area_and_drop_from_an_input();
    test_unicode_keeps_codepoint_boundaries();
    test_select_all_and_perform_on_dispatch();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
