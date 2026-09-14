// The snapshot rule the callers apply to a native pick: the row index belongs
// to the items the menu OPENED with, the action belongs to that row's
// action_id, and the id is matched against THIS frame's items -- so a toggle
// whose direction changed underneath ("Mute" became "Unmute") is refused
// rather than inverted, and a row past the snapshot resolves to nothing.
// Pure: no window, no UI headers; native_menu.h's Open only.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/native_menu.h"

static int failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

// A stand-in for surface::MenuItem with the one field the rule reads.
struct MenuItem {
    std::string label;
    std::string debug_name;
    std::string action_id;
};
using NativeMenuOpen = hanabi::native_menu::Open;
constexpr std::size_t kNoMenuRow = hanabi::native_menu::kNoRow;
static std::vector<std::string> ids_of(const std::vector<MenuItem>& v) {
    std::vector<std::string> out;
    for (const auto& m : v) out.push_back(m.action_id);
    return out;
}

static std::vector<MenuItem> row_items(bool muted, bool archived) {
    std::vector<MenuItem> v;
    const auto add = [&](const char* label, const char* id) {
        v.push_back({label, std::string("row_menu_") + id, id});
    };
    add("Open…", "open");
    add("Rename…", "rename");
    add(archived ? "Unarchive" : "Archive", archived ? "unarchive" : "archive");
    add(muted ? "Unmute" : "Mute", muted ? "unmute" : "mute");
    return v;
}

// What the callers do with a native result: resolve by id into this frame's
// items; kNoMenuRow means "refused".
static std::size_t resolve(const NativeMenuOpen& open, std::size_t pickedRow,
                           const std::vector<MenuItem>& now) {
    const std::string& id = open.action_of(pickedRow);
    std::size_t row = kNoMenuRow;
    for (std::size_t i = 0; i < now.size(); ++i)
        if (!id.empty() && now[i].action_id == id) row = i;
    return row;
}

static void test_same_state_resolves_to_the_same_action() {
    NativeMenuOpen open;
    open.generation = 7;
    open.scope = "session:t6";
    open.action_ids = ids_of(row_items(/*muted=*/false, /*archived=*/false));
    const auto now = row_items(false, false);
    CHECK(resolve(open, 3, now) == 3);          // Mute -> mute
    CHECK(open.action_of(3) == "mute");
}

static void test_a_toggle_that_changed_underneath_is_refused_not_inverted() {
    NativeMenuOpen open;
    open.generation = 8;
    open.scope = "session:t6";
    open.action_ids = ids_of(row_items(/*muted=*/false, false));   // menu said "Mute"
    const auto now = row_items(/*muted=*/true, false);   // row got muted meanwhile
    // The pick of row 3 means "mute"; this frame has "unmute" there: refused.
    CHECK(resolve(open, 3, now) == kNoMenuRow);
    // Archive is unchanged and still resolves.
    CHECK(resolve(open, 2, now) == 2);
}

static void test_a_row_past_the_snapshot_is_nothing() {
    NativeMenuOpen open;
    open.generation = 9;
    open.action_ids = ids_of(row_items(false, false));
    CHECK(open.action_of(40).empty());
    CHECK(resolve(open, 40, row_items(false, false)) == kNoMenuRow);
}

static void test_a_reordered_list_still_resolves_by_id() {
    NativeMenuOpen open;
    open.generation = 10;
    open.action_ids = ids_of(row_items(false, false));
    auto now = row_items(false, false);
    std::swap(now[0], now[3]);  // "mute" is now row 0
    CHECK(resolve(open, 3, now) == 0);
}

static void test_clear_forgets_everything() {
    NativeMenuOpen open;
    open.generation = 11;
    open.scope = "tab:3";
    open.action_ids = ids_of(row_items(false, false));
    CHECK(open.open());
    open.focus_before = 5;
    open.eater_id = 6;
    open.clear();
    CHECK(!open.open() && open.scope.empty() && open.action_ids.empty());
    CHECK(open.focus_before == -1 && open.eater_id == -1);
    CHECK(open.action_of(0).empty());
}

// Focus return: the rule the shim applies when a native menu closes.
static void test_focus_returns_only_when_the_menu_took_it() {
    using hanabi::native_menu::focus_to_restore;
    const long long ROOT = 0, EATER = 900, BEFORE = 42, ELSEWHERE = 77;
    // Focus is on the eater at close (a press on it moved focus): restore.
    CHECK(focus_to_restore(BEFORE, EATER, EATER, ROOT, true) == BEFORE);
    // Focus is on nothing at close: restore.
    CHECK(focus_to_restore(BEFORE, EATER, ROOT, ROOT, true) == BEFORE);
    CHECK(focus_to_restore(BEFORE, EATER, -1, ROOT, true) == BEFORE);
    // A menu action moved focus to a real element: leave it.
    CHECK(focus_to_restore(BEFORE, EATER, ELSEWHERE, ROOT, true) == -1);
    // The remembered element is gone (its row was archived away): leave it.
    CHECK(focus_to_restore(BEFORE, EATER, EATER, ROOT, false) == -1);
    // Nothing was focused when the menu opened: nothing to restore.
    CHECK(focus_to_restore(-1, EATER, EATER, ROOT, true) == -1);
    CHECK(focus_to_restore(ROOT, EATER, EATER, ROOT, true) == -1);
    // The eater was never drawn (menu refused before a frame): focus on
    // nothing still restores; focus elsewhere still wins.
    CHECK(focus_to_restore(BEFORE, -1, ROOT, ROOT, true) == BEFORE);
    CHECK(focus_to_restore(BEFORE, -1, ELSEWHERE, ROOT, true) == -1);
}

int main() {
    test_focus_returns_only_when_the_menu_took_it();
    test_same_state_resolves_to_the_same_action();
    test_a_toggle_that_changed_underneath_is_refused_not_inverted();
    test_a_row_past_the_snapshot_is_nothing();
    test_a_reordered_list_still_resolves_by_id();
    test_clear_forgets_everything();
    if (failures == 0) std::printf("test_native_menu_snapshot: OK\n");
    return failures == 0 ? 0 : 1;
}
