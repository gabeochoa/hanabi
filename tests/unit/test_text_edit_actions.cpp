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

int main() {
    std::printf("=== test_text_edit_actions ===\n");
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
