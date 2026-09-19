#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "../../src/api/types.h"
#include "../../src/api/wire_clock.h"
#include "../../src/ecs/snooze_wake.h"
#include "../../src/ui/snooze_menu.h"

using nlohmann::json;
namespace wc = api::wire_clock;
namespace sm = hanabi::snooze_menu;
namespace wake = hanabi::snooze_wake;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static std::optional<std::int64_t> ms(const char* text) { return wc::read_ms(json::parse(text)); }

static void test_read_ms_accepts_nonnegative_integers_only() {
    CHECK(ms("0") == std::optional<std::int64_t>{0});
    CHECK(ms("1") == std::optional<std::int64_t>{1});
    CHECK(ms("1781524800000") == std::optional<std::int64_t>{1781524800000});
    CHECK(ms("9223372036854775807") == std::optional<std::int64_t>{INT64_MAX});
    CHECK(!ms("9223372036854775808"));
    CHECK(!ms("18446744073709551615"));
    CHECK(!ms("-1"));
    CHECK(!ms("-9223372036854775808"));
    CHECK(!ms("true"));
    CHECK(!ms("false"));
    CHECK(!ms("1.5"));
    CHECK(!ms("1.0"));
    CHECK(!ms("1e3"));
    CHECK(!ms("\"12\""));
    CHECK(!ms("null"));
    CHECK(!ms("{}"));
    CHECK(!ms("[1]"));
}

static void test_row_reader_treats_missing_and_non_objects_as_absent() {
    CHECK(!wc::read_ms(json::parse("{}"), "x"));
    CHECK(!wc::read_ms(json::parse("[]"), "x"));
    CHECK(!wc::read_ms(json::parse("7"), "x"));
    CHECK(!wc::read_ms(json::parse("{\"x\":null}"), "x"));
    CHECK(wc::read_ms(json::parse("{\"x\":7}"), "x") == std::optional<std::int64_t>{7});
}

static void test_alias_precedence_is_activity_then_event() {
    CHECK(wc::read_event_ms(json::parse("{\"last_activity_unix_ms\":5,\"last_event_unix_ms\":9}")) ==
          std::optional<std::int64_t>{5});
    CHECK(wc::read_event_ms(json::parse("{\"last_activity_unix_ms\":0,\"last_event_unix_ms\":9}")) ==
          std::optional<std::int64_t>{0});
    CHECK(wc::read_event_ms(json::parse("{\"last_event_unix_ms\":9}")) == std::optional<std::int64_t>{9});
    CHECK(wc::read_event_ms(json::parse("{\"last_activity_unix_ms\":null,\"last_event_unix_ms\":9}")) ==
          std::optional<std::int64_t>{9});
    CHECK(wc::read_event_ms(json::parse("{\"last_activity_unix_ms\":-3,\"last_event_unix_ms\":9}")) ==
          std::optional<std::int64_t>{9});
    CHECK(wc::read_event_ms(json::parse("{\"last_activity_unix_ms\":true,\"last_event_unix_ms\":9}")) ==
          std::optional<std::int64_t>{9});
    CHECK(!wc::read_event_ms(json::parse("{}")));
    CHECK(wc::read_run_complete_ms(json::parse("{\"last_run_complete_unix_ms\":4}")) ==
          std::optional<std::int64_t>{4});
    CHECK(!wc::read_run_complete_ms(json::parse("{\"last_event_unix_ms\":4}")));
}

static void test_carry_keeps_fresh_values_and_inherits_only_the_event_clock() {
    const wc::Clocks held{std::optional<std::int64_t>{5}, std::optional<std::int64_t>{6}};
    CHECK(wc::carry_held({std::optional<std::int64_t>{0}, std::nullopt}, &held) ==
          (wc::Clocks{std::optional<std::int64_t>{0}, std::nullopt}));
    CHECK(wc::carry_held({std::optional<std::int64_t>{3}, std::nullopt}, &held) ==
          (wc::Clocks{std::optional<std::int64_t>{3}, std::nullopt}));
    CHECK(wc::carry_held({std::nullopt, std::nullopt}, &held) ==
          (wc::Clocks{std::optional<std::int64_t>{5}, std::nullopt}));
    CHECK(wc::carry_held({std::nullopt, std::optional<std::int64_t>{8}}, &held) ==
          (wc::Clocks{std::optional<std::int64_t>{5}, std::optional<std::int64_t>{8}}));
    CHECK(wc::carry_held({std::nullopt, std::nullopt}, nullptr) == (wc::Clocks{}));
    const wc::Clocks heldNone{};
    CHECK(wc::carry_held({std::nullopt, std::nullopt}, &heldNone) == (wc::Clocks{}));
}

struct FakeRow {
    std::string id;
    std::string title;
    std::optional<std::int64_t> last_event_unix_ms;
    std::optional<std::int64_t> last_run_complete_unix_ms;
};

static FakeRow fake(const char* id, std::optional<std::int64_t> ev, std::optional<std::int64_t> run,
                    const char* title = "t") {
    return FakeRow{id, title, ev, run};
}

static const FakeRow* row_of(const std::vector<FakeRow>& rows, const char* id) {
    for (const FakeRow& r : rows)
        if (r.id == id) return &r;
    return nullptr;
}

static void test_carry_over_rows_matches_the_per_row_rule_and_touches_nothing_else() {
    const std::vector<FakeRow> held{fake("a", 5, 6), fake("b", 7, std::nullopt), fake("dup", std::nullopt, 1),
                                    fake("dup", 9, 2), fake("gone", 3, 4)};
    std::vector<FakeRow> fresh{fake("a", 0, std::nullopt), fake("b", std::nullopt, 8, "renamed"),
                               fake("c", std::nullopt, std::nullopt), fake("dup", std::nullopt, std::nullopt),
                               fake("older", 1, std::nullopt)};
    wc::carry_held_rows(fresh, held);
    CHECK(fresh.size() == 5);
    CHECK(row_of(fresh, "a") && row_of(fresh, "a")->last_event_unix_ms == std::optional<std::int64_t>{0} &&
          !row_of(fresh, "a")->last_run_complete_unix_ms);
    CHECK(row_of(fresh, "b") && row_of(fresh, "b")->last_event_unix_ms == std::optional<std::int64_t>{7} &&
          row_of(fresh, "b")->last_run_complete_unix_ms == std::optional<std::int64_t>{8} &&
          row_of(fresh, "b")->title == "renamed");
    CHECK(row_of(fresh, "c") && !row_of(fresh, "c")->last_event_unix_ms && !row_of(fresh, "c")->last_run_complete_unix_ms);
    CHECK(row_of(fresh, "dup") && !row_of(fresh, "dup")->last_event_unix_ms &&
          !row_of(fresh, "dup")->last_run_complete_unix_ms);
    CHECK(row_of(fresh, "older") && row_of(fresh, "older")->last_event_unix_ms == std::optional<std::int64_t>{1});
    CHECK(!row_of(fresh, "gone"));
    std::vector<FakeRow> unchanged{fake("x", 4, 5), fake("y", std::nullopt, std::nullopt)};
    const std::vector<FakeRow> noHeld;
    wc::carry_held_rows(unchanged, noHeld);
    CHECK(unchanged[0].last_event_unix_ms == std::optional<std::int64_t>{4} &&
          unchanged[0].last_run_complete_unix_ms == std::optional<std::int64_t>{5});
    CHECK(!unchanged[1].last_event_unix_ms && !unchanged[1].last_run_complete_unix_ms);
}

static void test_a_clock_only_refresh_keeps_ids_and_titles_and_moves_only_the_clocks() {
    const std::vector<FakeRow> held{fake("t6", 100, 50, "SKU backfill"), fake("t7", 200, std::nullopt, "shards")};
    std::vector<FakeRow> fresh{fake("t6", 300, 50, "SKU backfill"), fake("t7", std::nullopt, std::nullopt, "shards")};
    wc::carry_held_rows(fresh, held);
    CHECK(fresh[0].id == "t6" && fresh[0].title == "SKU backfill" &&
          fresh[0].last_event_unix_ms == std::optional<std::int64_t>{300} &&
          fresh[0].last_run_complete_unix_ms == std::optional<std::int64_t>{50});
    CHECK(fresh[1].id == "t7" && fresh[1].title == "shards" &&
          fresh[1].last_event_unix_ms == std::optional<std::int64_t>{200} && !fresh[1].last_run_complete_unix_ms);
    std::vector<FakeRow> again{fake("t6", 300, std::nullopt, "SKU backfill")};
    wc::carry_held_rows(again, fresh);
    CHECK(again[0].last_event_unix_ms == std::optional<std::int64_t>{300} && !again[0].last_run_complete_unix_ms);
}

static void test_seeded_row_clocks_parse_like_the_wire_boundary() {
    const auto none = wc::seeded_clocks_for("", "t6");
    CHECK(!none.found);
    const auto other = wc::seeded_clocks_for("t7:5", "t6");
    CHECK(!other.found);
    const auto ev = wc::seeded_clocks_for("t7:1,t6:1781524770000,t8:2", "t6");
    CHECK(ev.found && ev.clocks.event_ms == std::optional<std::int64_t>{1781524770000} && !ev.clocks.run_complete_ms);
    const auto both = wc::seeded_clocks_for("t6:12:34", "t6");
    CHECK(both.found && both.clocks.event_ms == std::optional<std::int64_t>{12} &&
          both.clocks.run_complete_ms == std::optional<std::int64_t>{34});
    const auto runOnly = wc::seeded_clocks_for("t6::34", "t6");
    CHECK(runOnly.found && !runOnly.clocks.event_ms && runOnly.clocks.run_complete_ms == std::optional<std::int64_t>{34});
    const auto zero = wc::seeded_clocks_for("t6:0:0", "t6");
    CHECK(zero.found && zero.clocks.event_ms == std::optional<std::int64_t>{0} &&
          zero.clocks.run_complete_ms == std::optional<std::int64_t>{0});
    const auto bad = wc::seeded_clocks_for("t6:-5:1.5", "t6");
    CHECK(bad.found && !bad.clocks.event_ms && !bad.clocks.run_complete_ms);
    const auto text = wc::seeded_clocks_for("t6:abc:true", "t6");
    CHECK(text.found && !text.clocks.event_ms && !text.clocks.run_complete_ms);
    const auto max = wc::seeded_clocks_for("t6:9223372036854775807", "t6");
    CHECK(max.found && max.clocks.event_ms == std::optional<std::int64_t>{INT64_MAX});
    const auto over = wc::seeded_clocks_for("t6:9223372036854775808:18446744073709551615", "t6");
    CHECK(over.found && !over.clocks.event_ms && !over.clocks.run_complete_ms);
    const auto first = wc::seeded_clocks_for("t6:1,t6:2", "t6");
    CHECK(first.found && first.clocks.event_ms == std::optional<std::int64_t>{1});
    const auto malformedItem = wc::seeded_clocks_for("t6,t6:3", "t6");
    CHECK(malformedItem.found && malformedItem.clocks.event_ms == std::optional<std::int64_t>{3});
}

static void test_menu_due_at_matches_the_reference_rule() {
    const std::int64_t now = 1781524800;
    const std::int64_t until = now + 3600;
    const std::optional<std::int64_t> at = now - 60;
    const sm::RowClocks none{};
    CHECK(sm::due_at(until, at, now, none) == std::optional<std::int64_t>{until});
    CHECK(!sm::due_at(until, at, until, none));
    CHECK(!sm::due_at(now, at, now, none));
    CHECK(!sm::due_at(std::nullopt, at, now, none));
    CHECK(!sm::due_at(until, at, now, {std::optional<std::int64_t>{(*at + 1) * 1000}, std::nullopt}));
    CHECK(!sm::due_at(until, at, now, {std::nullopt, std::optional<std::int64_t>{(*at + 1) * 1000}}));
    CHECK(sm::due_at(until, at, now, {std::optional<std::int64_t>{*at * 1000}, std::nullopt}) ==
          std::optional<std::int64_t>{until});
    CHECK(sm::due_at(until, at, now, {std::optional<std::int64_t>{(*at + 1) * 1000 - 1}, std::nullopt}) ==
          std::optional<std::int64_t>{until});
    CHECK(sm::due_at(until, at, now, {std::optional<std::int64_t>{0}, std::optional<std::int64_t>{0}}) ==
          std::optional<std::int64_t>{until});
    CHECK(sm::due_at(until, std::nullopt, now, {std::optional<std::int64_t>{(*at + 1) * 1000}, std::nullopt}) ==
          std::optional<std::int64_t>{until});
    CHECK(!sm::due_at(until, std::nullopt, until, {std::optional<std::int64_t>{(*at + 1) * 1000}, std::nullopt}));
    for (const std::optional<std::int64_t> u : {std::optional<std::int64_t>{now + 1}, std::optional<std::int64_t>{now},
                                                std::optional<std::int64_t>{now - 1}, std::optional<std::int64_t>{}}) {
        CHECK(sm::due_at(u, at, now, none) == sm::active_until(u, now));
        CHECK(sm::due_at(u, std::nullopt, now, none) == sm::active_until(u, now));
    }
}

static void test_after_snooze_units_and_bounds() {
    CHECK(wake::after_snooze_ms(0, std::optional<std::int64_t>{1000}) == std::optional<std::int64_t>{1000});
    CHECK(!wake::after_snooze_ms(0, std::optional<std::int64_t>{999}));
    CHECK(!wake::after_snooze_ms(0, std::optional<std::int64_t>{0}));
    CHECK(!wake::after_snooze_ms(std::nullopt, std::optional<std::int64_t>{1000}));
    CHECK(!wake::after_snooze_ms(0, std::nullopt));
    const std::int64_t at = 1781524740;
    CHECK(wake::after_snooze_ms(at, std::optional<std::int64_t>{(at + 1) * 1000}) ==
          std::optional<std::int64_t>{(at + 1) * 1000});
    CHECK(!wake::after_snooze_ms(at, std::optional<std::int64_t>{(at + 1) * 1000 - 1}));
    CHECK(!wake::after_snooze_ms(at, std::optional<std::int64_t>{at * 1000}));
    CHECK(!wake::after_snooze_ms(at, std::optional<std::int64_t>{at}));
    const std::int64_t largestSafeAt = wake::kLargestSecondsInMillis - 1;
    const std::int64_t largestProvablyAfter = (largestSafeAt + 1) * 1000;
    CHECK(largestProvablyAfter <= INT64_MAX);
    CHECK(wake::after_snooze_ms(largestSafeAt, std::optional<std::int64_t>{largestProvablyAfter}) ==
          std::optional<std::int64_t>{largestProvablyAfter});
    CHECK(!wake::after_snooze_ms(largestSafeAt, std::optional<std::int64_t>{largestProvablyAfter - 1}));
    CHECK(!wake::after_snooze_ms(wake::kLargestSecondsInMillis, std::optional<std::int64_t>{INT64_MAX}));
    CHECK(!wake::after_snooze_ms(INT64_MAX, std::optional<std::int64_t>{INT64_MAX}));
    CHECK(!wake::after_snooze_ms(-1, std::optional<std::int64_t>{1000}));
    CHECK(wake::seconds_to_ms_saturating(0) == 0);
    CHECK(wake::seconds_to_ms_saturating(-1) == 0);
    CHECK(wake::seconds_to_ms_saturating(wake::kLargestSecondsInMillis) == wake::kLargestSecondsInMillis * 1000);
    CHECK(wake::seconds_to_ms_saturating(wake::kLargestSecondsInMillis + 1) == INT64_MAX);
    CHECK(wake::seconds_to_ms_saturating(INT64_MAX) == INT64_MAX);
    const wake::Verdict hugeDue = wake::verdict(INT64_MAX, std::nullopt, INT64_MAX, std::nullopt, std::nullopt);
    CHECK(hugeDue.state == wake::State::Due && hugeDue.wake_at_ms == std::optional<std::int64_t>{INT64_MAX});
    CHECK(wake::acknowledges_on_open(hugeDue));
    CHECK(!wake::due_at(INT64_MAX, std::nullopt, INT64_MAX, std::nullopt, std::nullopt));
    const auto part = wake::partition({{"huge", INT64_MAX}, {"soon", 2000}, {"later", INT64_MAX - 1}}, {}, INT64_MAX - 1,
                                      {}, {});
    CHECK(part.parked.count("huge") == 1 && part.wake_at_ms.count("huge") == 0);
    CHECK(part.wake_at_ms.count("soon") == 1 && part.wake_at_ms.at("soon") == 2000000);
    CHECK(part.wake_at_ms.count("later") == 1 && part.wake_at_ms.at("later") == INT64_MAX);
    CHECK(part.parked.size() + part.wake_at_ms.size() == 3);
    const auto due = wake::partition({{"huge", INT64_MAX}}, {}, INT64_MAX, {}, {});
    CHECK(due.parked.empty() && due.wake_at_ms.count("huge") == 1 && due.wake_at_ms.at("huge") == INT64_MAX);
    const std::int64_t hugeUntil = wake::kLargestSecondsInMillis + 1;
    const std::int64_t hugeAt = wake::kLargestSecondsInMillis - 1;
    const wake::Verdict satDue = wake::verdict(hugeUntil, hugeAt, hugeUntil, std::nullopt, std::nullopt);
    CHECK(satDue.state == wake::State::Due && satDue.wake_at_ms == std::optional<std::int64_t>{INT64_MAX});
    const std::int64_t realMessage = (hugeAt + 1) * 1000;
    const wake::Verdict satMessage = wake::verdict(hugeUntil, hugeAt, hugeUntil, std::nullopt, realMessage);
    CHECK(satMessage.state == wake::State::Message && satMessage.wake_at_ms == std::optional<std::int64_t>{realMessage});
    const wake::Verdict satSettled = wake::verdict(hugeUntil, hugeAt, hugeUntil, realMessage, std::nullopt);
    CHECK(satSettled.state == wake::State::Settled && satSettled.wake_at_ms == std::optional<std::int64_t>{realMessage});
    const wake::Verdict satParked = wake::verdict(hugeUntil, hugeAt, hugeUntil - 1, std::nullopt, std::nullopt);
    CHECK(satParked.state == wake::State::Active && !satParked.wake_at_ms);
    const wake::Verdict noEarly = wake::verdict(hugeUntil, wake::kLargestSecondsInMillis, hugeUntil - 1, INT64_MAX, INT64_MAX);
    CHECK(noEarly.state == wake::State::Active && !noEarly.wake_at_ms);
    const wake::Verdict eqMax = wake::verdict(hugeUntil, hugeAt, hugeUntil, std::nullopt, INT64_MAX);
    CHECK(eqMax.state == wake::State::Due && eqMax.wake_at_ms == std::optional<std::int64_t>{INT64_MAX});
}

int main() {
    std::printf("=== test_wire_clock ===\n");
    test_read_ms_accepts_nonnegative_integers_only();
    test_row_reader_treats_missing_and_non_objects_as_absent();
    test_alias_precedence_is_activity_then_event();
    test_carry_keeps_fresh_values_and_inherits_only_the_event_clock();
    test_carry_over_rows_matches_the_per_row_rule_and_touches_nothing_else();
    test_a_clock_only_refresh_keeps_ids_and_titles_and_moves_only_the_clocks();
    test_seeded_row_clocks_parse_like_the_wire_boundary();
    test_menu_due_at_matches_the_reference_rule();
    test_after_snooze_units_and_bounds();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
