#include <cstdio>
#include <string>
#include <vector>
#include "../../src/ecs/workspace_switcher.h"
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); ++failures; } } while (0)
using namespace ecs::model;
static WorkspaceSwitcher trio() {
    WorkspaceSwitcher s; s.rename(0, "Reviews");
    s.openChat(0, "Reviews-a"); s.openChat(0, "Reviews-b");
    s.add(); s.rename(1, "THWWW"); s.openChat(1, "THWWW-a"); s.openChat(1, "THWWW-b");
    s.add(); s.rename(2, "Android"); s.openChat(2, "Android-a"); s.openChat(2, "Android-b");
    s.select(1); return s;
}
static void test_chips_list_every_chat_once() {
    std::printf("test_chips_list_every_chat_once\n");
    auto s = trio();
    CHECK(s.sessionChips(false, "THWWW-b").empty());
    auto chips = s.sessionChips(true, "THWWW-b");
    CHECK(chips.size() == 6);
    CHECK(chips[0].sessionId == "Reviews-a" && chips[0].workspace == 0);
    CHECK(chips[5].sessionId == "Android-b" && chips[5].workspace == 2);
    int sel = 0; for (auto& c : chips) if (c.isSelected) { ++sel; CHECK(c.sessionId == "THWWW-b"); }
    CHECK(sel == 1);
    s.duplicate(0); s.select(1);
    auto shared = s.sessionChips(true, "Reviews-a");
    CHECK(shared.size() == 6);
    CHECK(shared[0].workspace == 1 && shared[1].workspace == 1);
}
static void test_chip_actions_follow_the_reference() {
    std::printf("test_chip_actions_follow_the_reference\n");
    auto s = trio();
    CHECK(!WorkspaceSwitcher{}.canClose(0));
    s.duplicate(1);
    CHECK(s.workspaces[2].name == "THWWW copy" && s.activeIndex == 2);
    s.closeOthers(1);
    CHECK(s.count() == 1 && s.workspaces[0].name == "THWWW" && s.activeIndex == 0);
    auto t = trio(); t.close(2);
    CHECK(t.count() == 2 && t.activeIndex == 1);
}
static void test_chip_chords_only_under_station() {
    std::printf("test_chip_chords_only_under_station\n");
    CHECK(chip_hint(0, false).empty());
    CHECK(chip_hint(0, true) == "\xE2\x8C\x98" "1");
    CHECK(chip_hint(8, true) == "\xE2\x8C\x98" "9");
    CHECK(chip_hint(9, true).empty());
    CHECK(chip_help("Reviews", 0, false) == "\xE2\x8C\x98" "1");
    CHECK(chip_help("Reviews", 0, true) == "Reviews  \xE2\x8C\x98" "1");
    CHECK(chip_help("Android", 9, true) == "Android");
}
int main() {
    test_chips_list_every_chat_once();
    test_chip_actions_follow_the_reference();
    test_chip_chords_only_under_station();
    std::printf(failures ? "FAILED %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
