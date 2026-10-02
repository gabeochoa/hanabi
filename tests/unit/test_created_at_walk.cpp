#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../src/ecs/created_at_walk.h"
#include "../../src/ecs/thread_model.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::SessionSummary;
namespace ca = hanabi::created_at;

static SessionSummary row(std::string id, std::int64_t updated,
                          std::int64_t created = 0, bool starred = false) {
    SessionSummary s;
    s.id = std::move(id);
    s.updated_at = updated;
    s.created_at = created;
    s.starred = starred;
    return s;
}

static std::vector<std::string> order(std::vector<SessionSummary>& rows,
                                      bool (*before)(const SessionSummary*,
                                                     const SessionSummary*)) {
    std::vector<const SessionSummary*> p;
    for (const auto& r : rows) p.push_back(&r);
    std::sort(p.begin(), p.end(), before);
    std::vector<std::string> out;
    for (const auto* s : p) out.push_back(s->id);
    return out;
}

// Oldest created first; activity has no say; unknown last; pin still lifts.
static void test_oldest_first_orders_by_creation_unknown_last() {
    std::vector<SessionSummary> rows{
        row("busy", 900, 300),       // newest activity, middling age
        row("old", 100, 100),        // oldest
        row("young", 200, 500),      // newest created
        row("unknown_b", 999, 0),    // no creation time yet
        row("unknown_a", 1, 0),
        row("pinned", 50, 400, true),
    };
    const auto oldest = order(rows, ecs::model::sidebar_oldest_before);
    const std::vector<std::string> want{"pinned", "old", "busy", "young",
                                        "unknown_a", "unknown_b"};
    CHECK(oldest == want);
    // The default order is untouched: pin, then newest activity.
    const auto recent = order(rows, ecs::model::sidebar_before);
    CHECK(recent.front() == "pinned");
    CHECK(recent[1] == "unknown_b");
    // Activity moving does not move a row in creation order.
    rows[0].updated_at = 1;
    CHECK(order(rows, ecs::model::sidebar_oldest_before) == want);
}

static void test_ties_break_on_id() {
    std::vector<SessionSummary> rows{row("b", 1, 10), row("a", 2, 10), row("c", 3, 10)};
    CHECK((order(rows, ecs::model::sidebar_oldest_before) ==
           std::vector<std::string>{"a", "b", "c"}));
}

// What is known lands on rows that lack it; a row's own time is kept.
static void test_apply_fills_only_unknown_rows() {
    std::vector<SessionSummary> rows{row("a", 1), row("b", 1, 77), row("c", 1)};
    ca::Store known{{"a", 10}, {"b", 20}};
    ca::apply(known, rows);
    CHECK(rows[0].created_at == 10);
    CHECK(rows[1].created_at == 77);
    CHECK(rows[2].created_at == 0);
}

// The walk asks each session once: not a row that has a time, not one the
// store holds, not one the server refused; catalog order; capped.
static void test_next_batch_skips_known_and_refused() {
    std::vector<SessionSummary> rows;
    for (int i = 0; i < 20; ++i) rows.push_back(row("s" + std::to_string(i), 100 - i));
    rows[0].created_at = 5;
    ca::Store known{{"s1", 6}};
    std::unordered_set<std::string> refused{"s2"};
    const auto batch = ca::next_batch(rows, known, refused);
    CHECK(batch.size() == ca::kBatch);
    CHECK(batch.front() == "s3");
    CHECK(std::find(batch.begin(), batch.end(), "s0") == batch.end());
    CHECK(std::find(batch.begin(), batch.end(), "s1") == batch.end());
    CHECK(std::find(batch.begin(), batch.end(), "s2") == batch.end());
    // Learn the whole batch: the next one starts where it ended.
    for (const auto& id : batch) known[id] = 1;
    const auto second = ca::next_batch(rows, known, refused);
    CHECK(!second.empty() && second.front() == "s11");
    // Everything known or refused: nothing left to ask.
    for (const auto& r : rows) known[r.id] = 1;
    CHECK(ca::next_batch(rows, known, refused).empty());
}

int main() {
    test_oldest_first_orders_by_creation_unknown_last();
    test_ties_break_on_id();
    test_apply_fills_only_unknown_rows();
    test_next_batch_skips_known_and_refused();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
