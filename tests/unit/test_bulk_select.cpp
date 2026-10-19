#include <cstdio>

#include "../../src/ui/bulk_select.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using namespace hanabi::bulk;

static void test_gestures() {
    const std::vector<std::string> visible{"a", "b", "c", "d", "e"};
    Applied r = apply(Gesture::Toggle, "b", {}, "", visible);
    CHECK(r.selected == Selected{"b"});
    CHECK(r.anchor == "b");
    r = apply(Gesture::Toggle, "b", r.selected, r.anchor, visible);
    CHECK(r.selected.empty());
    CHECK(r.anchor == "b");
    r = apply(Gesture::Extend, "d", Selected{"b"}, "b", visible);
    CHECK((r.selected == Selected{"b", "c", "d"}));
    CHECK(r.anchor == "b");
    r = apply(Gesture::Extend, "a", r.selected, r.anchor, visible);
    CHECK((r.selected == Selected{"a", "b", "c", "d"}));
    CHECK(r.anchor == "b");
    r = apply(Gesture::Extend, "e", Selected{"b"}, "zz", visible);
    CHECK((r.selected == Selected{"b", "e"}));
    CHECK(r.anchor == "e");
    r = apply(Gesture::Extend, "zz", Selected{"b"}, "b", visible);
    CHECK(r.selected == Selected{"b"});
    CHECK(r.anchor == "b");
    CHECK(range("", "c", visible) == std::vector<std::string>{"c"});
    CHECK((range("d", "b", visible) == std::vector<std::string>{"b", "c", "d"}));
}

static void test_actionable_and_remaining() {
    const std::vector<std::string> visible{"a", "b", "b", "d"};
    CHECK((actionable(Selected{"b", "d", "x"}, visible) == std::vector<std::string>{"b", "d"}));
    Outcomes o;
    o["a"] = Outcome::succeeded();
    o["b"] = Outcome::unchanged();
    o["c"] = Outcome::failed("no");
    o["d"] = Outcome::unknown("lost");
    CHECK((remaining(Selected{"a", "b", "c", "d", "e"}, o) == Selected{"c", "d", "e"}));
    CHECK((unresolved(o, Selected{"a", "c", "d"}) == Selected{"d"}));
    CHECK(worst({Outcome::succeeded(), Outcome::failed("x"), Outcome::unchanged()}).is_failed());
    CHECK(worst({Outcome::unknown("u"), Outcome::failed("x")}).is_unknown());
    CHECK(worst({}) == Outcome::unchanged());
    const Tally t = tally(o, Selected{"c", "d"});
    CHECK((t == Tally{2, 1, 1}));
    CHECK(tally(o, Selected{}).failed == 0);
    CHECK(blocks_actions(Phase::Idle, o, Selected{"d"}));
    CHECK(!blocks_actions(Phase::Idle, o, Selected{"c"}));
    CHECK(blocks_actions(Phase::Running, {}, {}));
}

static void test_words() {
    CHECK(count_label(0) == "0 selected");
    CHECK(count_label(1) == "1 selected");
    CHECK(label(3, {}, Phase::Idle) == "3 selected");
    CHECK(label(3, {}, Phase::Running) == "Working\xe2\x80\xa6");
    CHECK(label(3, Tally{0, 2, 0}, Phase::Idle) == "2 failed");
    CHECK(label(3, Tally{0, 2, 1}, Phase::Idle) == "1 unconfirmed");
    CHECK(detail(3, Tally{1, 2, 1}, Phase::Idle) ==
          "3 selected \xc2\xb7 2 failed \xc2\xb7 1 unconfirmed");
    CHECK(detail(3, {}, Phase::Reconciling) == "3 selected \xc2\xb7 checking\xe2\x80\xa6");
    const Outcome f = Outcome::failed("HTTP 500");
    CHECK(row_note(&f) == "Not done: HTTP 500");
    const Outcome u = Outcome::unknown("timed out");
    CHECK(row_note(&u) == "Unconfirmed: timed out. Check again before retrying.");
    CHECK(row_note(nullptr).empty());
    CHECK(verb(Action::archive()) == "Archive");
    CHECK(verb(Action::move("f1")) == "Move to folder");
    CHECK(verb(Action::mark_read()) == "Mark read");
    CHECK(verb(Action::mark_unread()) == "Mark unread");
    CHECK(archive_hint(1) == "Archive 1 thread");
    CHECK(archive_hint(4) == "Archive 4 threads, sub-agents included");
    CHECK(archived_toast(1) == "1 thread archived");
    CHECK(archived_toast(2) == "2 threads archived");
}

int main() {
    test_gestures();
    test_actionable_and_remaining();
    test_words();
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
