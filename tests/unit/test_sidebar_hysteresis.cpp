#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/sidebar_hysteresis.h"

namespace sh = hanabi::sidebar_hysteresis;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

struct Row {
    std::string id;
    std::int64_t updated;
};

static std::vector<Row> sorted_newest_first(std::vector<Row> rows) {
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.updated != b.updated) return a.updated > b.updated;
        return a.id < b.id;
    });
    return rows;
}

static std::string ids(const std::vector<Row>& rows) {
    std::string out;
    for (const Row& r : rows) out += r.id + " ";
    return out;
}

static void pass(std::vector<Row>& rows, sh::Memory& m, std::int64_t now) {
    rows = sorted_newest_first(rows);
    sh::apply(rows, m, now, [](const Row& r) { return r.id; }, [](const Row& r) { return r.updated; });
}

static void test_an_active_row_is_not_overtaken_inside_the_window() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"a", t0 - 5}, {"b", t0 - 3600}, {"c", t0 - 7200}};
    pass(rows, m, t0);
    CHECK(ids(rows) == "a b c ");
    for (Row& r : rows)
        if (r.id == "b") r.updated = t0 + 10;
    pass(rows, m, t0 + 10);
    CHECK(ids(rows) == "a b c ");
    pass(rows, m, t0 + 40);
    CHECK(ids(rows) == "a b c ");
}

static void test_after_the_window_the_newer_row_leads() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"a", t0 - 5}, {"b", t0 - 3600}};
    pass(rows, m, t0);
    for (Row& r : rows)
        if (r.id == "b") r.updated = t0 + 10;
    pass(rows, m, t0 + 10);
    CHECK(ids(rows) == "a b ");
    pass(rows, m, t0 + 61);
    CHECK(ids(rows) == "b a ");
}

static void test_a_row_never_held_before_takes_its_sorted_place() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"a", t0 - 5000}, {"b", t0 - 6000}};
    pass(rows, m, t0);
    CHECK(ids(rows) == "a b ");
    for (Row& r : rows)
        if (r.id == "b") r.updated = t0 + 1;
    pass(rows, m, t0 + 1);
    CHECK(ids(rows) == "b a ");
}

static void test_inactive_rows_reorder_freely() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"a", t0 - 5000}, {"b", t0 - 6000}, {"c", t0 - 7000}};
    pass(rows, m, t0);
    for (Row& r : rows)
        if (r.id == "c") r.updated = t0 - 4000;
    pass(rows, m, t0);
    CHECK(ids(rows) == "c a b ");
}

static void test_overtakers_queue_behind_the_held_block_until_it_expires() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"x", t0 - 1}, {"a", t0 - 5}, {"b", t0 - 8}, {"z", t0 - 9000}};
    pass(rows, m, t0);
    CHECK(ids(rows) == "x a b z ");
    for (Row& r : rows) {
        if (r.id == "b") r.updated = t0 + 2;
        if (r.id == "z") r.updated = t0 + 5;
    }
    pass(rows, m, t0 + 5);
    CHECK(ids(rows) == "x a z b ");
    pass(rows, m, t0 + 70);
    CHECK(ids(rows) == "z b x a ");
}

static void test_a_row_removed_from_the_list_drops_out_of_memory() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"a", t0 - 5}, {"b", t0 - 6}};
    pass(rows, m, t0);
    rows.erase(rows.begin());
    pass(rows, m, t0 + 1);
    CHECK(m.rank.count("a") == 0 && m.rank.count("b") == 1);
}

static void test_two_streams_keep_their_order_past_a_minute_of_activity() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"a", t0}, {"b", t0 - 3600}};
    pass(rows, m, t0);
    CHECK(ids(rows) == "a b ");
    for (std::int64_t t = t0 + 5; t <= t0 + 90; t += 5) {
        for (Row& r : rows) r.updated = (r.id == "b") ? t : t - 2;
        pass(rows, m, t);
        CHECK(ids(rows) == "a b ");
    }
    for (Row& r : rows) r.updated = (r.id == "b") ? t0 + 95 : t0 + 90;
    pass(rows, m, t0 + 200);
    CHECK(ids(rows) == "b a ");
}

static void test_apply_reports_whether_the_order_changed() {
    const std::int64_t t0 = 1'000'000;
    sh::Memory m;
    std::vector<Row> rows{{"a", t0 - 5}, {"b", t0 - 3600}};
    rows = sorted_newest_first(rows);
    CHECK(sh::apply(rows, m, t0, [](const Row& r) { return r.id; }, [](const Row& r) { return r.updated; }));
    CHECK(m.reorders == 1);
    rows = sorted_newest_first(rows);
    CHECK(!sh::apply(rows, m, t0 + 1, [](const Row& r) { return r.id; }, [](const Row& r) { return r.updated; }));
    CHECK(m.reorders == 1);
    for (Row& r : rows)
        if (r.id == "b") r.updated = t0 + 10;
    rows = sorted_newest_first(rows);
    CHECK(!sh::apply(rows, m, t0 + 10, [](const Row& r) { return r.id; }, [](const Row& r) { return r.updated; }));
    rows = sorted_newest_first(rows);
    CHECK(sh::apply(rows, m, t0 + 100, [](const Row& r) { return r.id; }, [](const Row& r) { return r.updated; }));
    CHECK(m.reorders == 2);
}

static void test_recently_active_edges() {
    CHECK(sh::recently_active(100, 100));
    CHECK(sh::recently_active(100, 159));
    CHECK(!sh::recently_active(100, 160));
    CHECK(!sh::recently_active(0, 100));
    CHECK(sh::recently_active(101, 100));
    CHECK(!sh::recently_active(200, 100));
}

int main() {
    std::printf("=== test_sidebar_hysteresis ===\n");
    test_an_active_row_is_not_overtaken_inside_the_window();
    test_after_the_window_the_newer_row_leads();
    test_a_row_never_held_before_takes_its_sorted_place();
    test_inactive_rows_reorder_freely();
    test_overtakers_queue_behind_the_held_block_until_it_expires();
    test_a_row_removed_from_the_list_drops_out_of_memory();
    test_two_streams_keep_their_order_past_a_minute_of_activity();
    test_apply_reports_whether_the_order_changed();
    test_recently_active_edges();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
