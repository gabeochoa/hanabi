#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "../../src/api/inbox_state_wire.h"
#include "../../src/ecs/snooze_wake.h"
#include "../../src/ui/snooze_presets.h"
#include "../../vendor/nlohmann/json.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        const auto va_ = (a);                                                 \
        const auto vb_ = (b);                                                 \
        if (!(va_ == vb_)) {                                                  \
            std::printf("  FAIL: %s == %s (line %d)\n", #a, #b, __LINE__);    \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace presets = hanabi::snooze_presets;
namespace wake = hanabi::snooze_wake;
namespace wire = api::inbox_state;
using presets::Choice;
using presets::Option;
using json = nlohmann::json;

static void use_zone(const char* zone) {
    setenv("TZ", zone, 1);
    tzset();
}

// America/New_York, computed with an independent tz database (Python zoneinfo).
namespace ny {
constexpr std::int64_t fri_1500 = 1789758000;         // Fri 2026-09-18 15:00 EDT
constexpr std::int64_t fri_1700 = 1789765200;         // Fri 2026-09-18 17:00 EDT
constexpr std::int64_t sat_0800 = 1789819200;         // Sat 2026-09-19 08:00 EDT
constexpr std::int64_t mon_0800 = 1789992000;         // Mon 2026-09-21 08:00 EDT
constexpr std::int64_t fri_1830 = 1789770600;         // Fri 2026-09-18 18:30 EDT
constexpr std::int64_t sun_1000 = 1789912800;         // Sun 2026-09-20 10:00 EDT
constexpr std::int64_t sat_1000 = 1789826400;         // Sat 2026-09-19 10:00 EDT
constexpr std::int64_t sun_0800 = 1789905600;         // Sun 2026-09-20 08:00 EDT
constexpr std::int64_t thu_1400 = 1789668000;         // Thu 2026-09-17 14:00 EDT
constexpr std::int64_t spring_sat_1000 = 1772895600;  // Sat 2026-03-07 10:00 EST
constexpr std::int64_t spring_sun_0800 = 1772971200;  // Sun 2026-03-08 08:00 EDT (21h later)
constexpr std::int64_t spring_mon_0800 = 1773057600;  // Mon 2026-03-09 08:00 EDT
constexpr std::int64_t fall_sat_1000 = 1793455200;    // Sat 2026-10-31 10:00 EDT
constexpr std::int64_t fall_sun_0800 = 1793538000;    // Sun 2026-11-01 08:00 EST (23h later)
constexpr std::int64_t fri_2330 = 1789788600;         // Fri 2026-09-18 23:30 EDT
constexpr std::int64_t sat_0030 = 1789792200;         // Sat 2026-09-19 00:30 EDT
constexpr std::int64_t thu24_0900 = 1790254800;       // Thu 2026-09-24 09:00 EDT (+6 days)
constexpr std::int64_t fri25_0900 = 1790341200;       // Fri 2026-09-25 09:00 EDT (+7 days)
constexpr std::int64_t fri_1200 = 1789747200;         // Fri 2026-09-18 12:00 EDT
constexpr std::int64_t fri_0005 = 1789704300;         // Fri 2026-09-18 00:05 EDT
constexpr std::int64_t dec3_1645 = 1796334300;        // Thu 2026-12-03 16:45 EST
}  // namespace ny
namespace utc {
constexpr std::int64_t fri_1500 = 1789743600;
constexpr std::int64_t fri_1700 = 1789750800;
constexpr std::int64_t sat_0800 = 1789804800;
}  // namespace utc

static const Option* find(const std::vector<Option>& options, Choice choice) {
    for (const Option& o : options)
        if (o.choice == choice) return &o;
    return nullptr;
}

static bool sorted_by_instant(const std::vector<Option>& options) {
    for (std::size_t i = 1; i < options.size(); ++i)
        if (options[i - 1].until >= options[i].until) return false;
    return true;
}

// ---- presets ---------------------------------------------------------------

static void test_a_friday_afternoon_offers_all_six_in_instant_order() {
    use_zone("America/New_York");
    const auto options = presets::options(ny::fri_1500);
    CHECK_EQ(options.size(), std::size_t{6});
    CHECK(sorted_by_instant(options));
    CHECK(find(options, Choice::OneHour) && find(options, Choice::OneHour)->until == ny::fri_1500 + 3600);
    CHECK(find(options, Choice::Today5pm) && find(options, Choice::Today5pm)->until == ny::fri_1700);
    CHECK(find(options, Choice::ThreeHours) && find(options, Choice::ThreeHours)->until == ny::fri_1500 + 10800);
    CHECK(find(options, Choice::Tomorrow8am) && find(options, Choice::Tomorrow8am)->until == ny::sat_0800);
    CHECK(find(options, Choice::NextWorkday8am) && find(options, Choice::NextWorkday8am)->until == ny::mon_0800);
    CHECK(find(options, Choice::OneWeek) && find(options, Choice::OneWeek)->until == ny::fri_1500 + 604800);
    CHECK(options.front().choice == Choice::OneHour);
    CHECK(options.back().choice == Choice::OneWeek);
}

static void test_this_evening_drops_out_once_five_pm_has_passed() {
    use_zone("America/New_York");
    const auto options = presets::options(ny::fri_1830);
    CHECK_EQ(options.size(), std::size_t{5});
    CHECK(find(options, Choice::Today5pm) == nullptr);
    CHECK(find(options, Choice::Tomorrow8am)->until == ny::sat_0800);
    const auto at_five = presets::options(ny::fri_1700);
    CHECK(find(at_five, Choice::Today5pm) == nullptr);
    const auto just_before = presets::options(ny::fri_1700 - 1);
    CHECK(find(just_before, Choice::Today5pm) != nullptr);
    CHECK(find(just_before, Choice::Today5pm)->until == ny::fri_1700);
}

static void test_the_weekend_is_skipped_and_a_sunday_dedupes_by_instant() {
    use_zone("America/New_York");
    CHECK_EQ(presets::days_to_next_workday(1), 1);
    CHECK_EQ(presets::days_to_next_workday(5), 1);
    CHECK_EQ(presets::days_to_next_workday(6), 3);
    CHECK_EQ(presets::days_to_next_workday(7), 2);
    const auto saturday = presets::options(ny::sat_1000);
    CHECK(find(saturday, Choice::Tomorrow8am)->until == ny::sun_0800);
    CHECK(find(saturday, Choice::NextWorkday8am)->until == ny::mon_0800);
    const auto sunday = presets::options(ny::sun_1000);
    CHECK(find(sunday, Choice::Tomorrow8am)->until == ny::mon_0800);
    CHECK(find(sunday, Choice::NextWorkday8am) == nullptr);
    std::size_t at_monday_eight = 0;
    for (const Option& o : sunday)
        if (o.until == ny::mon_0800) ++at_monday_eight;
    CHECK_EQ(at_monday_eight, std::size_t{1});
}

static void test_an_elapsed_duration_wins_a_collision_with_a_wall_clock_row() {
    use_zone("America/New_York");
    const auto options = presets::options(ny::thu_1400);
    CHECK(find(options, Choice::Today5pm) == nullptr);
    CHECK(find(options, Choice::ThreeHours) != nullptr);
    CHECK(find(options, Choice::ThreeHours)->until == ny::thu_1400 + 10800);
    std::size_t at_five = 0;
    for (const Option& o : options)
        if (o.until == ny::thu_1400 + 10800) ++at_five;
    CHECK_EQ(at_five, std::size_t{1});
    CHECK(find(options, Choice::Tomorrow8am) != nullptr);
    CHECK(find(options, Choice::NextWorkday8am) == nullptr);
    CHECK_EQ(options.size(), std::size_t{4});
    CHECK(sorted_by_instant(options));
}

static void test_wall_clock_rows_follow_the_calendar_across_dst() {
    use_zone("America/New_York");
    const auto spring = presets::options(ny::spring_sat_1000);
    CHECK(find(spring, Choice::Tomorrow8am)->until == ny::spring_sun_0800);
    CHECK(find(spring, Choice::NextWorkday8am)->until == ny::spring_mon_0800);
    CHECK(find(spring, Choice::OneWeek)->until == ny::spring_sat_1000 + 604800);
    const auto fall = presets::options(ny::fall_sat_1000);
    CHECK(find(fall, Choice::Tomorrow8am)->until == ny::fall_sun_0800);
    CHECK(find(fall, Choice::OneHour)->until == ny::fall_sat_1000 + 3600);
}

static void test_the_viewer_zone_decides_the_wall_clock() {
    use_zone("UTC");
    const auto options = presets::options(utc::fri_1500);
    CHECK(find(options, Choice::Today5pm)->until == utc::fri_1700);
    CHECK(find(options, Choice::Tomorrow8am)->until == utc::sat_0800);
    CHECK(find(options, Choice::OneHour)->until == utc::fri_1500 + 3600);
    use_zone("America/New_York");
}

static void test_every_option_is_in_range_and_names_its_route_choice() {
    use_zone("America/New_York");
    for (const Option& o : presets::options(ny::fri_1500)) {
        CHECK(presets::is_in_range(o.until, ny::fri_1500));
        CHECK(std::string(presets::raw_name(o.choice)).size() > 0);
        CHECK(std::string(presets::title(o.choice)).size() > 0);
    }
    CHECK_EQ(std::string(presets::raw_name(Choice::NextWorkday8am)), std::string("next_workday_8am"));
    CHECK_EQ(std::string(presets::raw_name(Choice::OneHour)), std::string("one_hour"));
    CHECK_EQ(std::string(presets::raw_name(Choice::ThreeHours)), std::string("three_hours"));
    CHECK_EQ(std::string(presets::raw_name(Choice::OneWeek)), std::string("one_week"));
    CHECK_EQ(std::string(presets::raw_name(Choice::Today5pm)), std::string("today_5pm"));
    CHECK_EQ(std::string(presets::raw_name(Choice::Tomorrow8am)), std::string("tomorrow_8am"));
    CHECK_EQ(std::string(presets::title(Choice::OneHour)), std::string("In 1 Hour"));
    CHECK_EQ(std::string(presets::title(Choice::ThreeHours)), std::string("In 3 Hours"));
    CHECK_EQ(std::string(presets::title(Choice::OneWeek)), std::string("In 1 Week"));
    CHECK_EQ(std::string(presets::title(Choice::Today5pm)), std::string("This Evening"));
    CHECK_EQ(std::string(presets::title(Choice::Tomorrow8am)), std::string("Tomorrow Morning"));
    CHECK_EQ(std::string(presets::title(Choice::NextWorkday8am)), std::string("Next Workday"));
}

static void test_a_custom_instant_is_refused_never_clamped() {
    using presets::CustomVerdict;
    const std::int64_t now = ny::fri_1500;
    CHECK(presets::custom(now, now).kind == CustomVerdict::Kind::NotInTheFuture);
    CHECK(presets::custom(now - 1, now).kind == CustomVerdict::Kind::NotInTheFuture);
    CHECK(presets::custom(now + 1, now).kind == CustomVerdict::Kind::Accepted);
    CHECK(presets::custom(now + 1, now).until == now + 1);
    CHECK(presets::custom(now + presets::kMaxHorizonSeconds, now).kind == CustomVerdict::Kind::Accepted);
    CHECK(presets::custom(now + presets::kMaxHorizonSeconds + 1, now).kind == CustomVerdict::Kind::PastTheHorizon);
    CHECK(presets::custom(now + presets::kMaxHorizonSeconds + 1, now).until == now + presets::kMaxHorizonSeconds + 1);
    CHECK(!presets::custom(now + 1, now).refusal().has_value());
    CHECK_EQ(std::string(*presets::custom(now, now).refusal()),
             std::string("That time has already passed. Pick one that is still ahead."));
    CHECK_EQ(std::string(*presets::custom(now + presets::kMaxHorizonSeconds + 1, now).refusal()),
             std::string("A thread can be snoozed at most a year ahead."));
    CHECK_EQ(presets::custom_default(now), now + 24 * 3600);
    CHECK(presets::is_in_range(now + presets::kMaxHorizonSeconds, now));
    CHECK(!presets::is_in_range(now + presets::kMaxHorizonSeconds + 1, now));
    CHECK(!presets::is_in_range(now, now));
}

// ---- wording -----------------------------------------------------------------

static void test_due_wording_buckets_by_calendar_day() {
    use_zone("America/New_York");
    const std::int64_t now = ny::fri_1500;
    CHECK_EQ(presets::due_text(ny::fri_1700, now), std::string("5:00 PM"));
    CHECK_EQ(presets::due_text(now, now), std::string("3:00 PM"));
    CHECK_EQ(presets::due_text(ny::fri_1200, now), std::string("12:00 PM"));
    CHECK_EQ(presets::due_text(ny::fri_0005, now), std::string("12:05 AM"));
    CHECK_EQ(presets::due_text(ny::sat_0800, now), std::string("tomorrow, 8:00 AM"));
    CHECK_EQ(presets::due_text(ny::sat_0030, ny::fri_2330), std::string("tomorrow, 12:30 AM"));
    CHECK_EQ(presets::due_text(ny::mon_0800, now), std::string("Monday, 8:00 AM"));
    CHECK_EQ(presets::due_text(ny::thu24_0900, now), std::string("Thursday, 9:00 AM"));
    CHECK_EQ(presets::due_text(ny::fri25_0900, now), std::string("25 Sep, 9:00 AM"));
    CHECK_EQ(presets::due_text(ny::dec3_1645, now), std::string("3 Dec, 4:45 PM"));
    CHECK_EQ(presets::due_text(ny::fall_sun_0800, ny::fall_sat_1000), std::string("tomorrow, 8:00 AM"));
    CHECK_EQ(presets::due_text(ny::spring_sun_0800, ny::spring_sat_1000), std::string("tomorrow, 8:00 AM"));
    CHECK_EQ(presets::option_text(Option{Choice::Tomorrow8am, ny::sat_0800}, now),
             std::string("Tomorrow Morning \xe2\x80\x94 tomorrow, 8:00 AM"));
    CHECK_EQ(presets::snoozed_until_text(ny::mon_0800, now), std::string("Snoozed until Monday, 8:00 AM"));
    use_zone("UTC");
    CHECK_EQ(presets::due_text(utc::fri_1700, utc::fri_1500), std::string("5:00 PM"));
    CHECK_EQ(presets::due_text(utc::sat_0800, utc::fri_1500), std::string("tomorrow, 8:00 AM"));
    use_zone("America/New_York");
}

// ---- wake --------------------------------------------------------------------

static void test_a_snooze_is_due_at_its_instant_and_not_a_second_before() {
    using wake::State;
    const auto before = wake::verdict(1000, 500, 999, std::nullopt, std::nullopt);
    CHECK(before.state == State::Active);
    CHECK(!before.wake_at_ms.has_value());
    const auto at = wake::verdict(1000, 500, 1000, std::nullopt, std::nullopt);
    CHECK(at.state == State::Due);
    CHECK(at.wake_at_ms.has_value() && *at.wake_at_ms == 1000000);
    const auto after = wake::verdict(1000, 500, 5000, std::nullopt, std::nullopt);
    CHECK(after.state == State::Due && *after.wake_at_ms == 1000000);
}

static void test_a_message_wakes_only_from_the_next_whole_second_after_the_stamp() {
    using wake::State;
    const auto exact = wake::verdict(1000, 500, 600, std::nullopt, 501000);
    CHECK(exact.state == State::Message && *exact.wake_at_ms == 501000);
    const auto one_ms_short = wake::verdict(1000, 500, 600, std::nullopt, 500999);
    CHECK(one_ms_short.state == State::Active);
    const auto same_second = wake::verdict(1000, 500, 600, std::nullopt, 500000);
    CHECK(same_second.state == State::Active);
    const auto settled = wake::verdict(1000, 500, 600, 501000, std::nullopt);
    CHECK(settled.state == State::Settled && *settled.wake_at_ms == 501000);
}

static void test_no_baseline_and_no_clock_both_keep_the_row_parked() {
    using wake::State;
    CHECK(wake::verdict(1000, std::nullopt, 600, 900000, 900000).state == State::Active);
    CHECK(wake::verdict(1000, 500, 600, 0, 0).state == State::Active);
    CHECK(wake::verdict(1000, 500, 600, std::nullopt, std::nullopt).state == State::Active);
    CHECK(wake::verdict(1000, std::nullopt, 1000, 900000, 900000).state == State::Due);
}

static void test_the_earliest_wake_wins() {
    using wake::State;
    const auto message_first = wake::verdict(1000, 500, 1000, 700000, 600000);
    CHECK(message_first.state == State::Message && *message_first.wake_at_ms == 600000);
    const auto settled_first = wake::verdict(1000, 500, 1000, 550000, 600000);
    CHECK(settled_first.state == State::Settled && *settled_first.wake_at_ms == 550000);
    const auto due_first = wake::verdict(1000, 500, 1000, 1200000, 1100000);
    CHECK(due_first.state == State::Due && *due_first.wake_at_ms == 1000000);
    const auto tie = wake::verdict(1000, 500, 1000, 1000000, 1000000);
    CHECK(tie.state == State::Due && *tie.wake_at_ms == 1000000);
    const auto message_settled_tie = wake::verdict(1000, 500, 600, 600000, 600000);
    CHECK(message_settled_tie.state == State::Message);
}

static void test_open_acknowledges_a_woken_snooze_only() {
    using wake::State;
    CHECK(!wake::acknowledges_on_open(wake::Verdict{State::Active, std::nullopt}));
    CHECK(wake::acknowledges_on_open(wake::Verdict{State::Due, 1}));
    CHECK(wake::acknowledges_on_open(wake::Verdict{State::Message, 1}));
    CHECK(wake::acknowledges_on_open(wake::Verdict{State::Settled, 1}));
}

static void test_due_at_is_the_instant_only_while_active() {
    CHECK(wake::due_at(1000, 500, 600, std::nullopt, std::nullopt) == std::optional<std::int64_t>{1000});
    CHECK(!wake::due_at(1000, 500, 1000, std::nullopt, std::nullopt).has_value());
    CHECK(!wake::due_at(1000, 500, 600, std::nullopt, 501000).has_value());
    CHECK(wake::due_at(1000, std::nullopt, 600, 900000, 900000) == std::optional<std::int64_t>{1000});
}

static void test_partition_splits_parked_from_woken() {
    const std::map<std::string, std::int64_t> until = {{"a", 1000}, {"b", 1000}, {"c", 1000}, {"d", 1000}};
    const std::map<std::string, std::int64_t> at = {{"a", 500}, {"b", 500}, {"c", 500}};
    const std::map<std::string, std::int64_t> runs = {{"c", 501000}};
    const std::map<std::string, std::int64_t> messages = {{"b", 700000}, {"d", 900000}};
    const auto split = wake::partition(until, at, 800, runs, messages);
    CHECK(split.parked.count("a") == 1);
    CHECK(split.parked.count("d") == 1);
    CHECK_EQ(split.parked.size(), std::size_t{2});
    CHECK(split.wake_at_ms.count("b") == 1 && split.wake_at_ms.at("b") == 700000);
    CHECK(split.wake_at_ms.count("c") == 1 && split.wake_at_ms.at("c") == 501000);
    CHECK_EQ(split.wake_at_ms.size(), std::size_t{2});
    const auto later = wake::partition(until, at, 1000, {}, {});
    CHECK(later.parked.empty());
    CHECK_EQ(later.wake_at_ms.size(), std::size_t{4});
    CHECK(later.wake_at_ms.at("d") == 1000000);
}

static void test_a_wake_raises_attention_but_never_lowers_it() {
    CHECK(wake::effective_attention_ms(std::nullopt, std::nullopt) == std::nullopt);
    CHECK(wake::effective_attention_ms(5, std::nullopt) == std::optional<std::int64_t>{5});
    CHECK(wake::effective_attention_ms(std::nullopt, 7) == std::optional<std::int64_t>{7});
    CHECK(wake::effective_attention_ms(5, 7) == std::optional<std::int64_t>{7});
    CHECK(wake::effective_attention_ms(9, 7) == std::optional<std::int64_t>{9});
}

// ---- wire ------------------------------------------------------------------

static void test_the_route_body_parses_into_a_snapshot_with_int_and_double_tolerance() {
    const auto parsed = wire::parse_snapshot(R"({
      "read": ["s1", "s2", "", 7],
      "archived": ["s3"],
      "starred": [],
      "lastSeenAt": {"s1": 1789758000, "s2": 1789758001.9, "s3": "soon", "s4": null},
      "snoozes": {
        "s5": {"snoozedAt": 1789758000, "snoozedUntil": 1789765200},
        "s6": {"snoozedAt": 1789758000.0, "snoozedUntil": 1789765200.7},
        "s7": {"snoozedUntil": 1789765200},
        "s8": "not an entry",
        "s9": {"snoozedAt": "x", "snoozedUntil": true}
      },
      "labels": {"s1": ["urgent", "", 3], "s2": [], "s3": "x"},
      "labelVocabulary": ["urgent"],
      "somethingNew": {"a": 1}
    })");
    CHECK(parsed.has_value());
    if (!parsed) return;
    CHECK_EQ(parsed->read.size(), std::size_t{2});
    CHECK(parsed->read.count("s1") == 1 && parsed->read.count("s2") == 1);
    CHECK(parsed->archived == std::set<std::string>{"s3"});
    CHECK(parsed->starred.empty());
    CHECK_EQ(parsed->last_seen_at.size(), std::size_t{2});
    CHECK(parsed->last_seen_at.at("s1") == 1789758000);
    CHECK(parsed->last_seen_at.at("s2") == 1789758001);
    CHECK(parsed->snoozed_until.at("s5") == 1789765200 && parsed->snoozed_at.at("s5") == 1789758000);
    CHECK(parsed->snoozed_until.at("s6") == 1789765200 && parsed->snoozed_at.at("s6") == 1789758000);
    CHECK(parsed->snoozed_until.at("s7") == 1789765200);
    CHECK(parsed->snoozed_at.count("s7") == 0);
    CHECK(parsed->snoozed_until.count("s8") == 0 && parsed->snoozed_until.count("s9") == 0);
    CHECK_EQ(parsed->snoozed_until.size(), std::size_t{3});
    CHECK_EQ(parsed->snoozed_at.size(), std::size_t{2});
    CHECK(parsed->labels.at("s1") == std::vector<std::string>{"urgent"});
    CHECK(parsed->labels.count("s2") == 0 && parsed->labels.count("s3") == 0);
}

static void test_a_body_without_the_success_shape_is_not_an_empty_inbox() {
    CHECK(!wire::parse_snapshot("<!DOCTYPE html><html><body>Sign in</body></html>").has_value());
    CHECK(!wire::parse_snapshot(R"({"error":"Not authenticated"})").has_value());
    CHECK(!wire::parse_snapshot(R"({"redirect":"https://login.example/"})").has_value());
    CHECK(!wire::parse_snapshot("[]").has_value());
    CHECK(!wire::parse_snapshot("").has_value());
    CHECK(!wire::parse_snapshot("{").has_value());
    const auto minimal = wire::parse_snapshot(R"({"read":[]})");
    CHECK(minimal.has_value() && minimal->read.empty() && minimal->snoozed_until.empty());
    const auto only_snoozes = wire::parse_snapshot(R"({"snoozes":{}})");
    CHECK(only_snoozes.has_value());
    const auto old_route = wire::parse_snapshot(R"({"read":["a"],"archived":["b"]})");
    CHECK(old_route.has_value() && old_route->starred.empty() && old_route->last_seen_at.empty());
    const auto with_error_beside_data = wire::parse_snapshot(R"({"read":[],"error":"warning"})");
    CHECK(with_error_beside_data.has_value());
}

static void test_the_post_body_says_absent_null_or_number() {
    wire::Patch leave_alone;
    leave_alone.archived = true;
    const auto a = wire::body_for("sess-1", leave_alone);
    CHECK(a.has_value());
    if (a) {
        const json j = json::parse(*a);
        CHECK(j.at("sessionId") == "sess-1");
        CHECK(j.at("archived") == true);
        CHECK(!j.contains("snoozedUntil"));
        CHECK(!j.contains("starred") && !j.contains("read"));
    }
    wire::Patch clear;
    clear.snoozed_until = std::optional<std::int64_t>{};
    const auto c = wire::body_for("sess-1", clear);
    CHECK(c.has_value());
    if (c) {
        const json j = json::parse(*c);
        CHECK(j.contains("snoozedUntil"));
        CHECK(j.at("snoozedUntil").is_null());
        CHECK_EQ(j.size(), std::size_t{2});
    }
    wire::Patch set;
    set.snoozed_until = std::optional<std::int64_t>{1789765200};
    const auto s = wire::body_for("sess-1", set);
    CHECK(s.has_value());
    if (s) {
        const json j = json::parse(*s);
        CHECK(j.at("snoozedUntil").is_number_integer());
        CHECK(j.at("snoozedUntil").get<std::int64_t>() == 1789765200);
    }
    wire::Patch everything;
    everything.read = false;
    everything.starred = true;
    everything.snoozed_until = std::optional<std::int64_t>{5};
    const auto e = wire::body_for("sess-1", everything);
    CHECK(e.has_value());
    if (e) {
        const json j = json::parse(*e);
        CHECK(j.at("read") == false && j.at("starred") == true && j.at("snoozedUntil") == 5);
    }
    CHECK(!wire::body_for("sess-1", wire::Patch{}).has_value());
    CHECK(!wire::body_for("", set).has_value());
    CHECK(!wire::body_for(std::string(256, 'a'), set).has_value());
    CHECK(wire::body_for(std::string(255, 'a'), set).has_value());
    CHECK(wire::is_valid_session_id("x"));
    CHECK(!wire::is_valid_session_id(""));
    CHECK_EQ(std::string(wire::kCsrfHeader), std::string("x-agentcloud-csrf"));
    CHECK_EQ(std::string(wire::kCsrfHeaderValue), std::string("1"));
    CHECK_EQ(std::string(wire::kRoutePath), std::string("/api/inbox/state"));
    CHECK_EQ(std::string(wire::kAuthCookieName), std::string("intern_cat_token"));
}

static void test_the_echo_confirms_a_set_only_with_both_fields_and_the_requested_value() {
    using wire::Confirmation;
    const auto set_echo = wire::parse_echo(R"({"ok":true,"lastSeenAt":1789758000,"snoozedAt":1789758000,"snoozedUntil":1789765200})");
    CHECK(set_echo.has_value() && set_echo->ok);
    if (set_echo) {
        CHECK(set_echo->last_seen_at == std::optional<std::int64_t>{1789758000});
        const auto confirmed = wire::confirm_snooze(*set_echo, std::optional<std::int64_t>{1789765200});
        CHECK(confirmed.kind == Confirmation::Confirmed);
        CHECK(confirmed.snoozed_at == std::optional<std::int64_t>{1789758000});
        CHECK(confirmed.snoozed_until == std::optional<std::int64_t>{1789765200});
        CHECK(wire::confirm_snooze(*set_echo, std::optional<std::int64_t>{1789765201}).kind == Confirmation::Unconfirmed);
        CHECK(wire::confirm_snooze(*set_echo, std::nullopt).kind == Confirmation::Unconfirmed);
    }
    const auto clear_echo = wire::parse_echo(R"({"ok":true,"lastSeenAt":null,"snoozedAt":null,"snoozedUntil":null})");
    CHECK(clear_echo.has_value());
    if (clear_echo) {
        CHECK(!clear_echo->last_seen_at.has_value());
        CHECK(wire::confirm_snooze(*clear_echo, std::nullopt).kind == Confirmation::Cleared);
        CHECK(wire::confirm_snooze(*clear_echo, std::optional<std::int64_t>{5}).kind == Confirmation::Unconfirmed);
    }
    const auto untouched = wire::parse_echo(R"({"ok":true,"lastSeenAt":1789758000})");
    CHECK(untouched.has_value());
    if (untouched) {
        CHECK(!untouched->snoozed_at.has_value() && !untouched->snoozed_until.has_value());
        CHECK(wire::confirm_snooze(*untouched, std::optional<std::int64_t>{5}).kind == Confirmation::Unconfirmed);
        CHECK(wire::confirm_snooze(*untouched, std::nullopt).kind == Confirmation::Unconfirmed);
    }
    const auto half = wire::parse_echo(R"({"ok":true,"snoozedUntil":1789765200})");
    CHECK(half.has_value());
    if (half) CHECK(wire::confirm_snooze(*half, std::optional<std::int64_t>{1789765200}).kind == Confirmation::Unconfirmed);
    const auto not_ok = wire::parse_echo(R"({"ok":false,"snoozedAt":1,"snoozedUntil":2})");
    CHECK(not_ok.has_value() && !not_ok->ok);
    if (not_ok) CHECK(wire::confirm_snooze(*not_ok, std::optional<std::int64_t>{2}).kind == Confirmation::Unconfirmed);
    const auto doubles = wire::parse_echo(R"({"ok":true,"snoozedAt":1789758000.0,"snoozedUntil":1789765200.0})");
    CHECK(doubles.has_value());
    if (doubles) CHECK(wire::confirm_snooze(*doubles, std::optional<std::int64_t>{1789765200}).kind == Confirmation::Confirmed);
    CHECK(!wire::parse_echo(R"({"error":"Missing CSRF header"})")->ok);
    CHECK(!wire::parse_echo("<html>").has_value());
    CHECK(!wire::parse_echo("[1]").has_value());
    const auto zero_stamp = wire::parse_echo(R"({"ok":true,"lastSeenAt":0})");
    CHECK(zero_stamp.has_value() && !zero_stamp->last_seen_at.has_value());
}

static void test_unsafe_numbers_are_skipped_not_reinterpreted() {
    const auto parsed = wire::parse_snapshot(R"({
      "lastSeenAt": {"huge": 1e30, "tiny": -1e30, "neg": -5, "frac": 12.99, "big_int": 9007199254740993, "text": "12", "flag": true},
      "snoozes": {"ms": {"snoozedAt": 1789758000123, "snoozedUntil": 1789765200456}}
    })");
    CHECK(parsed.has_value());
    if (!parsed) return;
    CHECK(parsed->last_seen_at.count("huge") == 0);
    CHECK(parsed->last_seen_at.count("tiny") == 0);
    CHECK(parsed->last_seen_at.count("text") == 0);
    CHECK(parsed->last_seen_at.count("flag") == 0);
    CHECK(parsed->last_seen_at.at("neg") == -5);
    CHECK(parsed->last_seen_at.at("frac") == 12);
    CHECK(parsed->last_seen_at.at("big_int") == 9007199254740993LL);
    CHECK(parsed->snoozed_at.at("ms") == 1789758000123LL);
    CHECK(parsed->snoozed_until.at("ms") == 1789765200456LL);
}

int main() {
    std::printf("== test_snooze (presets, wording, wake, inbox-state wire) ==\n");
    test_a_friday_afternoon_offers_all_six_in_instant_order();
    test_this_evening_drops_out_once_five_pm_has_passed();
    test_the_weekend_is_skipped_and_a_sunday_dedupes_by_instant();
    test_an_elapsed_duration_wins_a_collision_with_a_wall_clock_row();
    test_wall_clock_rows_follow_the_calendar_across_dst();
    test_the_viewer_zone_decides_the_wall_clock();
    test_every_option_is_in_range_and_names_its_route_choice();
    test_a_custom_instant_is_refused_never_clamped();
    test_due_wording_buckets_by_calendar_day();
    test_a_snooze_is_due_at_its_instant_and_not_a_second_before();
    test_a_message_wakes_only_from_the_next_whole_second_after_the_stamp();
    test_no_baseline_and_no_clock_both_keep_the_row_parked();
    test_the_earliest_wake_wins();
    test_open_acknowledges_a_woken_snooze_only();
    test_due_at_is_the_instant_only_while_active();
    test_partition_splits_parked_from_woken();
    test_a_wake_raises_attention_but_never_lowers_it();
    test_the_route_body_parses_into_a_snapshot_with_int_and_double_tolerance();
    test_a_body_without_the_success_shape_is_not_an_empty_inbox();
    test_the_post_body_says_absent_null_or_number();
    test_the_echo_confirms_a_set_only_with_both_fields_and_the_requested_value();
    test_unsafe_numbers_are_skipped_not_reinterpreted();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
