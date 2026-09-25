#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/tab_find_state.h"

namespace tf = hanabi::tab_find;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void test_switching_tabs_keeps_each_query() {
    tf::Store store;
    tf::PaneFind pane;
    tf::sync(store, pane, "a");
    CHECK(!pane.open && pane.query.empty() && store.size() == 0);
    pane.open = true;
    pane.query = "ledger";
    pane.match = tf::MatchKey{"m7", 1, 4};
    tf::sync(store, pane, "a");
    CHECK(store.size() == 1 && store.entry("a")->query == "ledger" && store.entry("a")->current == (tf::MatchKey{"m7", 1, 4}));
    tf::sync(store, pane, "b");
    CHECK(!pane.open && pane.query.empty() && pane.match.empty());
    pane.open = true;
    pane.query = "invoice";
    tf::sync(store, pane, "b");
    tf::sync(store, pane, "a");
    CHECK(pane.open && pane.query == "ledger" && pane.match == (tf::MatchKey{"m7", 1, 4}));
    tf::sync(store, pane, "b");
    CHECK(pane.open && pane.query == "invoice" && pane.match.empty());
    CHECK(store.size() == 2);
}

static void test_escape_keeps_the_query_and_drops_the_row() {
    tf::Store store;
    tf::PaneFind pane;
    tf::sync(store, pane, "a");
    pane.open = true;
    pane.query = "ledger";
    pane.match = tf::MatchKey{"m3", 0, 0};
    tf::sync(store, pane, "a");
    pane.open = false;
    pane.match = {};
    CHECK(!pane.open && pane.query == "ledger" && pane.match.empty());
    tf::sync(store, pane, "a");
    CHECK(store.entry("a") != nullptr && !store.entry("a")->open && store.entry("a")->query == "ledger");
    tf::sync(store, pane, "b");
    tf::sync(store, pane, "a");
    CHECK(!pane.open && pane.query == "ledger" && pane.match.empty());
}

static void test_closed_and_empty_leaves_no_entry() {
    tf::Store store;
    tf::PaneFind pane;
    tf::sync(store, pane, "a");
    pane.open = true;
    tf::sync(store, pane, "a");
    CHECK(store.size() == 1);
    pane.open = false;
    tf::sync(store, pane, "a");
    CHECK(store.size() == 0 && store.entry("a") == nullptr);
}

static void test_a_closed_tab_is_forgotten_and_a_missing_entry_reads_closed() {
    tf::Store store;
    tf::PaneFind pane;
    tf::sync(store, pane, "a");
    pane.open = true;
    pane.query = "q";
    tf::sync(store, pane, "a");
    store.forget("a");
    CHECK(store.entry("a") == nullptr);
    tf::sync(store, pane, "b");
    tf::sync(store, pane, "a");
    CHECK(!pane.open && pane.query.empty() && pane.match.empty());
    tf::sync(store, pane, "");
    CHECK(!pane.open && pane.query.empty() && store.size() == 0);
}

static void test_two_panes_on_one_session_do_not_clobber_with_stale_copies() {
    tf::Store store;
    tf::PaneFind left;
    tf::PaneFind right;
    tf::sync(store, left, "a");
    tf::sync(store, right, "a");
    left.open = true;
    left.query = "ledger";
    tf::sync(store, left, "a");
    tf::sync(store, right, "a");
    CHECK(store.entry("a") != nullptr && store.entry("a")->query == "ledger");
    tf::sync(store, right, "a");
    tf::sync(store, right, "a");
    CHECK(store.entry("a")->query == "ledger" && store.entry("a")->open);
    right.open = true;
    right.query = "invoice";
    tf::sync(store, right, "a");
    CHECK(store.entry("a")->query == "invoice");
    tf::sync(store, left, "a");
    CHECK(store.entry("a")->query == "invoice");
    tf::sync(store, left, "b");
    tf::sync(store, left, "a");
    CHECK(left.query == "invoice" && left.open);
}

static void test_state_set_before_the_first_frame_seeds_the_store() {
    tf::Store store;
    tf::PaneFind pane;
    pane.open = true;
    pane.query = "demo";
    tf::sync(store, pane, "a");
    CHECK(pane.open && pane.query == "demo" && store.entry("a") != nullptr && store.entry("a")->query == "demo");
    tf::PaneFind later;
    later.open = true;
    later.query = "stale";
    later.syncedId = "x";
    tf::sync(store, later, "a");
    CHECK(later.query == "demo");
    tf::Store empty;
    tf::PaneFind blank;
    tf::sync(empty, blank, "a");
    CHECK(!blank.open && empty.size() == 0);
}

struct M {
    int msg;
    std::size_t line;
    std::size_t off;
};

static std::string id_of(const std::vector<std::string>& ids, int i) {
    return i >= 0 && i < static_cast<int>(ids.size()) ? ids[static_cast<std::size_t>(i)] : "i" + std::to_string(i);
}

static void test_the_current_match_survives_a_message_arriving_above_it() {
    std::vector<std::string> ids{"m1", "m2", "m3"};
    std::vector<M> matches{{0, 0, 2}, {2, 1, 0}, {2, 3, 5}};
    tf::MatchKey current;
    int index = tf::resolve(matches, current, 1, [&](int i) { return id_of(ids, i); });
    CHECK(index == 1 && current == (tf::MatchKey{"m3", 1, 0}));
    ids = {"m0", "m1", "m2", "m3"};
    matches = {{0, 0, 0}, {1, 0, 2}, {3, 1, 0}, {3, 3, 5}};
    index = tf::resolve(matches, current, index, [&](int i) { return id_of(ids, i); });
    CHECK(index == 2 && current == (tf::MatchKey{"m3", 1, 0}));
    current = {};
    index = tf::resolve(matches, current, 3, [&](int i) { return id_of(ids, i); });
    CHECK(index == 3 && current == (tf::MatchKey{"m3", 3, 5}));
    matches = {{0, 0, 0}, {1, 0, 2}};
    index = tf::resolve(matches, current, index, [&](int i) { return id_of(ids, i); });
    CHECK(index == 0 && current == (tf::MatchKey{"m0", 0, 0}));
    matches.clear();
    index = tf::resolve(matches, current, index, [&](int i) { return id_of(ids, i); });
    CHECK(index == 0 && current.empty());
    index = tf::resolve(std::vector<M>{{0, 0, 0}}, current, 9, [&](int i) { return id_of(ids, i); });
    CHECK(index == 0 && current == (tf::MatchKey{"m0", 0, 0}));
}

static void test_store_ignores_an_empty_id() {
    tf::Store store;
    store.set("", tf::Entry{true, "x", {}});
    CHECK(store.size() == 0);
}

int main() {
    std::printf("=== test_tab_find_state ===\n");
    test_switching_tabs_keeps_each_query();
    test_escape_keeps_the_query_and_drops_the_row();
    test_closed_and_empty_leaves_no_entry();
    test_a_closed_tab_is_forgotten_and_a_missing_entry_reads_closed();
    test_two_panes_on_one_session_do_not_clobber_with_stale_copies();
    test_state_set_before_the_first_frame_seeds_the_store();
    test_the_current_match_survives_a_message_arriving_above_it();
    test_store_ignores_an_empty_id();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
