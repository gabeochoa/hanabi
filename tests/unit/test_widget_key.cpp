// What makes an immediate-mode widget the SAME widget next frame.
//
// The tab strip hands `mk()` indices like `940 + i`, `950 + i`, `960 + i`, and
// the ranges overlap once a window holds ten tabs. That looks like an identity
// collision waiting to happen, and a reviewer reading only those numbers will
// conclude it is one -- so this pins down what the key is actually made of.
//
// `hanabi::ui::widget_key` mixes FIVE facts: the parent id, the caller's
// index, and the file / function / line:column of the CALL SITE. Two different
// lines therefore never collide however their indices overlap, and one line
// only needs indices unique among the widgets it makes -- which a loop index
// is, for any number of tabs, with no cap and no reserved range.
//
// Without this test the safe-looking fix is a bigger magic offset, which buys
// nothing and has to be re-picked every time a surface is added.
#include <cstdio>
#include <set>
#include <source_location>
#include <vector>

#include "../../src/ui/mk.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

using Key = afterhours::ui::imm::UI_UUID;

// Four call sites, standing in for the four per-tab widgets the strip builds.
//
// Each evaluates `source_location::current()` in its OWN body, which is what
// `mk()` sees: its `loc` parameter defaults to `current()` at the call site,
// so the location is the line that called `mk`, not a line inside it. Taking
// the location as a defaulted parameter here instead would capture whichever
// test line called the helper -- collapsing all four to one location and
// making them collide, which is a trap worth naming rather than repeating.
static Key tab_button(afterhours::EntityID parent, int i) {
    return hanabi::ui::widget_key(parent, i,
                                  std::source_location::current());
}
static Key tab_close(afterhours::EntityID parent, int i) {
    return hanabi::ui::widget_key(parent, i,
                                  std::source_location::current());
}
static Key tab_pin(afterhours::EntityID parent, int i) {
    return hanabi::ui::widget_key(parent, i,
                                  std::source_location::current());
}
static Key tab_status(afterhours::EntityID parent, int i) {
    return hanabi::ui::widget_key(parent, i,
                                  std::source_location::current());
}

static void test_the_call_site_is_part_of_the_identity() {
    std::printf("test_the_call_site_is_part_of_the_identity\n");
    // The same parent and the SAME index from two different lines: distinct.
    CHECK(tab_button(7, 950) != tab_close(7, 950));
    CHECK(tab_close(7, 950) != tab_pin(7, 950));
    CHECK(tab_pin(7, 950) != tab_status(7, 950));
    // And the numerically overlapping ranges the strip actually uses.
    CHECK(tab_close(7, 950 + 12) != tab_pin(7, 960 + 2));
}

static void test_one_call_site_needs_only_a_unique_index() {
    std::printf("test_one_call_site_needs_only_a_unique_index\n");
    std::set<Key> keys;
    for (int i = 0; i < 500; ++i) keys.insert(tab_status(7, i));
    CHECK(keys.size() == 500);
    // The same index twice from one line IS the same widget -- which is the
    // property that makes a widget persist across frames, not a bug.
    CHECK(tab_status(7, 3) == tab_status(7, 3));
}

// The strip at a size no cap allows for: every widget of every tab distinct.
static void test_a_strip_of_many_tabs_has_no_collisions() {
    std::printf("test_a_strip_of_many_tabs_has_no_collisions\n");
    constexpr int kTabs = 500;
    std::set<Key> keys;
    for (int i = 0; i < kTabs; ++i) {
        keys.insert(tab_button(7, 910 + i));
        keys.insert(tab_close(7, 950 + i));
        keys.insert(tab_pin(7, 960 + i));
        keys.insert(tab_status(7, i));
    }
    CHECK(keys.size() == static_cast<std::size_t>(kTabs) * 4);
}

// Two panes are two parents, and the parent is in the key: the same tab index
// in each is two widgets, not one shared between them.
static void test_the_parent_separates_two_panes() {
    std::printf("test_the_parent_separates_two_panes\n");
    CHECK(tab_status(7, 4) != tab_status(8, 4));
}

int main() {
    std::printf("=== test_widget_key ===\n");
    test_the_call_site_is_part_of_the_identity();
    test_one_call_site_needs_only_a_unique_index();
    test_a_strip_of_many_tabs_has_no_collisions();
    test_the_parent_separates_two_panes();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
