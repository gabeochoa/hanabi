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
    CHECK(wake::seconds_to_ms(0) == std::optional<std::int64_t>{0});
    CHECK(wake::seconds_to_ms(wake::kLargestSecondsInMillis) ==
          std::optional<std::int64_t>{wake::kLargestSecondsInMillis * 1000});
    CHECK(!wake::seconds_to_ms(wake::kLargestSecondsInMillis + 1));
    CHECK(!wake::seconds_to_ms(-1));
    const wake::Verdict hugeDue = wake::verdict(INT64_MAX, std::nullopt, INT64_MAX, std::nullopt, std::nullopt);
    CHECK(hugeDue.state == wake::State::Due && !hugeDue.wake_at_ms);
    CHECK(wake::acknowledges_on_open(hugeDue));
    CHECK(!wake::due_at(INT64_MAX, std::nullopt, INT64_MAX, std::nullopt, std::nullopt));
    const auto part = wake::partition({{"huge", INT64_MAX}, {"soon", 2000}}, {}, INT64_MAX, {}, {});
    CHECK(part.parked.empty() && part.wake_at_ms.count("soon") == 1 && part.wake_at_ms.count("huge") == 0);
}

int main() {
    std::printf("=== test_wire_clock ===\n");
    test_read_ms_accepts_nonnegative_integers_only();
    test_row_reader_treats_missing_and_non_objects_as_absent();
    test_alias_precedence_is_activity_then_event();
    test_carry_keeps_fresh_values_and_inherits_only_the_event_clock();
    test_menu_due_at_matches_the_reference_rule();
    test_after_snooze_units_and_bounds();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
