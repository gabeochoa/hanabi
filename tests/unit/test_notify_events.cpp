// Which state changes are worth a banner (src/util/notify_events.h).
//
// The rule this replaces was a COUNT of blocked threads, and every case below
// that says "the count would not have moved" is a notification the user never
// got. Pure logic — no clock, no AppKit, no session type.
#include <cstdio>
#include <string>

#include "../../src/util/notify_events.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::notify::Activity;
using hanabi::notify::Event;
using hanabi::notify::snapshot;
using hanabi::notify::transitions;

using Row = std::pair<std::string, Activity>;
using Rows = std::vector<Row>;
using Titles = std::map<std::string, std::string>;

static const Titles kTitles = {{"a", "Pricing rollout"},
                               {"b", "Churn query"},
                               {"c", "Nightwatch"}};

static void test_first_sight_is_never_news() {
    std::printf("test_first_sight_is_never_news\n");
    // Launching into an inbox where everything is already blocked must be
    // silent, however many there are.
    const Rows cur = {{"a", Activity::Blocked}, {"b", Activity::Blocked}};
    CHECK(transitions({}, cur, kTitles).empty());
}

static void test_a_thread_that_blocks_is_reported_by_name() {
    std::printf("test_a_thread_that_blocks_is_reported_by_name\n");
    const auto before = snapshot({{"a", Activity::Other}});
    const auto ev = transitions(before, {{"a", Activity::Blocked}}, kTitles);
    CHECK(ev.size() == 1);
    if (ev.size() == 1) {
        CHECK(ev[0].kind == Event::Kind::Blocked);
        CHECK(ev[0].id == "a");
        CHECK(ev[0].title == "Pricing rollout");
    }
}

static void test_a_swap_still_notifies() {
    std::printf("test_a_swap_still_notifies\n");
    // THE case the count rule missed: one thread unblocks in the same refresh
    // as another blocks. Two blocked before, two blocked after.
    const auto before =
        snapshot({{"a", Activity::Blocked}, {"b", Activity::Blocked},
                  {"c", Activity::Other}});
    const auto ev = transitions(
        before,
        {{"a", Activity::Blocked}, {"b", Activity::Other}, {"c", Activity::Blocked}},
        kTitles);
    CHECK(ev.size() == 1);
    if (ev.size() == 1) {
        CHECK(ev[0].kind == Event::Kind::Blocked);
        CHECK(ev[0].id == "c");
    }
}

static void test_finishing_is_its_own_kind() {
    std::printf("test_finishing_is_its_own_kind\n");
    const auto before = snapshot({{"c", Activity::Other}});
    const auto ev = transitions(before, {{"c", Activity::Finished}}, kTitles);
    CHECK(ev.size() == 1);
    if (ev.size() == 1) CHECK(ev[0].kind == Event::Kind::Finished);
}

static void test_standing_still_says_nothing() {
    std::printf("test_standing_still_says_nothing\n");
    const auto before =
        snapshot({{"a", Activity::Blocked}, {"c", Activity::Finished}});
    CHECK(transitions(before,
                      {{"a", Activity::Blocked}, {"c", Activity::Finished}},
                      kTitles)
              .empty());
    // Going quiet is not news either.
    CHECK(transitions(before,
                      {{"a", Activity::Other}, {"c", Activity::Other}},
                      kTitles)
              .empty());
}

static void test_a_vanished_thread_is_not_an_event() {
    std::printf("test_a_vanished_thread_is_not_an_event\n");
    const auto before = snapshot({{"a", Activity::Blocked}});
    CHECK(transitions(before, {}, kTitles).empty());
}

static void test_a_missing_title_is_empty_not_a_crash() {
    std::printf("test_a_missing_title_is_empty_not_a_crash\n");
    const auto before = snapshot({{"zz", Activity::Other}});
    const auto ev = transitions(before, {{"zz", Activity::Blocked}}, kTitles);
    CHECK(ev.size() == 1);
    if (ev.size() == 1) CHECK(ev[0].title.empty());
}

int main() {
    std::printf("=== test_notify_events ===\n");
    test_first_sight_is_never_news();
    test_a_thread_that_blocks_is_reported_by_name();
    test_a_swap_still_notifies();
    test_finishing_is_its_own_kind();
    test_standing_still_says_nothing();
    test_a_vanished_thread_is_not_an_event();
    test_a_missing_title_is_empty_not_a_crash();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
