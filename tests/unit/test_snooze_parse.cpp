#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "../../src/native_snooze_prompt.h"
#include "../../src/ui/snooze_parse.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

namespace sp = hanabi::snooze_parse;
using sp::Choice;
using sp::Command;
using sp::Failure;
using Kind = Command::Kind;

static void use_zone(const char* zone) {
    setenv("TZ", zone, 1);
    tzset();
}

namespace ny {
constexpr std::int64_t wed_sep16_1000_edt = 1789567200;
constexpr std::int64_t wed_sep16_1700_edt = 1789592400;
constexpr std::int64_t wed_sep16_1705_edt = 1789592700;
constexpr std::int64_t wed_sep16_1030_edt = 1789569000;
constexpr std::int64_t wed_sep16_1200_edt = 1789574400;
constexpr std::int64_t thu_sep17_0000_edt = 1789617600;
constexpr std::int64_t thu_sep17_0800_edt = 1789646400;
constexpr std::int64_t thu_sep17_0900_edt = 1789650000;
constexpr std::int64_t thu_sep17_0930_edt = 1789651800;
constexpr std::int64_t fri_sep18_0800_edt = 1789732800;
constexpr std::int64_t mon_sep21_0930_edt = 1789997400;
constexpr std::int64_t wed_sep16_1800_edt = 1789596000;
constexpr std::int64_t wed_sep23_1700_edt = 1790197200;
constexpr std::int64_t fri_sep18_1500_edt = 1789758000;
constexpr std::int64_t mon_sep21_0800_edt = 1789992000;
constexpr std::int64_t sun_sep20_0800_edt = 1789905600;
constexpr std::int64_t sat_mar07_1000_est = 1772895600;
constexpr std::int64_t mon_mar09_0230_edt = 1773037800;
constexpr std::int64_t sun_mar08_0300_edt = 1772953200;
constexpr std::int64_t sat_oct31_1000_edt = 1793455200;
constexpr std::int64_t sun_nov01_0130_edt_first = 1793511000;
constexpr std::int64_t sun_nov01_0130_est_second = 1793514600;
}
namespace berlin {
constexpr std::int64_t wed_sep16_1000_cest = 1789545600;
constexpr std::int64_t wed_sep16_1700_cest = 1789570800;
constexpr std::int64_t thu_sep17_0900_cest = 1789628400;
constexpr std::int64_t sat_mar28_1000_cet = 1774688400;
constexpr std::int64_t mon_mar30_0230_cest = 1774830600;
}

static Command set(Choice c, std::int64_t until) { return Command::set(c, until); }
static Command invalid(Failure f) { return Command::invalid(f); }

static void test_normalisation_empty_off_and_length() {
    CHECK(sp::parse("", ny::wed_sep16_1000_edt) == Command::picker());
    CHECK(sp::parse("   \n\t ", ny::wed_sep16_1000_edt) == Command::picker());
    CHECK(sp::parse("off", ny::wed_sep16_1000_edt) == Command::clear());
    CHECK(sp::parse("  OFF ", ny::wed_sep16_1000_edt) == Command::clear());
    CHECK(sp::parse("Off\n", ny::wed_sep16_1000_edt) == Command::clear());
    CHECK(sp::normalized("  Tomorrow\tAT   9AM \n") == "tomorrow at 9am");
    const std::string forty(40, 'x');
    const std::string forty_one(41, 'x');
    CHECK(sp::parse(forty, ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    CHECK(sp::parse(forty_one, ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    const std::string long_but_valid = "in            2            hours";
    CHECK(sp::parse(long_but_valid, ny::wed_sep16_1000_edt) == set(Choice::Relative, ny::wed_sep16_1000_edt + 7200));
    std::string forty_one_after_collapse = "tomorrow at 9am";
    forty_one_after_collapse += std::string(26, 'x');
    CHECK(forty_one_after_collapse.size() == 41);
    CHECK(sp::parse(forty_one_after_collapse, ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    CHECK(sp::character_count("\xc3\xa9\xc3\xa9") == 2);
    CHECK(sp::normalized("9am\xc2\xa0on\xc2\xa0Monday") == "9am on monday");
    CHECK(sp::normalized("\xe2\x80\x83" "tomorrow\xe2\x80\xaf" "9am\xe3\x80\x80") == "tomorrow 9am");
    CHECK(sp::normalized("in\xe2\x80\xa8" "2h") == "in 2h");
    CHECK(sp::normalized("caf\xc3\xa9 9AM") == "caf\xc3\xa9" " 9am");
    CHECK(sp::parse("9am\xc2\xa0on\xc2\xa0Monday", ny::wed_sep16_1000_edt) == set(Choice::Named, ny::mon_sep21_0930_edt - 1800));
    CHECK(sp::parse("tomorrow\xe2\x80\xaf" "9am", ny::wed_sep16_1000_edt) == set(Choice::Named, ny::thu_sep17_0900_edt));
    CHECK(sp::parse("\xc3\x89 9am", ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    CHECK(sp::normalized("next wor\xe2\x84\xaa" "day") == "next workday");
    CHECK(sp::parse("next wor\xe2\x84\xaa" "day", ny::fri_sep18_1500_edt) == set(Choice::Named, ny::mon_sep21_0800_edt));
    CHECK(sp::normalized("\xef\xbc\xab") == "\xef\xbc\xab");
    CHECK(sp::parse("\xef\xbc\xab" "am", ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("\xe2\x80\x8b" "9am", ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    const std::string forty_accents(40, 'x');
    const std::string forty_one_by_code_point = std::string(39, 'x') + "\xc3\xa9\xc3\xa9";
    CHECK(sp::character_count(forty_one_by_code_point) == 41);
    CHECK(sp::parse(forty_one_by_code_point, ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    const std::string forty_by_code_point = std::string(39, 'x') + "\xc3\xa9";
    CHECK(sp::character_count(forty_by_code_point) == 40);
    CHECK(sp::parse(forty_by_code_point, ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    const std::string combining = std::string(38, 'x') + "e\xcc\x81";
    CHECK(sp::character_count(combining) == 40);
    CHECK(sp::parse(combining, ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("offf", ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("nonsense here", ny::wed_sep16_1000_edt) == invalid(Failure::Unrecognized));
}

static void test_relative_forms() {
    const std::int64_t now = ny::wed_sep16_1000_edt;
    CHECK(sp::parse("in 2 hours", now) == set(Choice::Relative, now + 7200));
    CHECK(sp::parse("in 30m", now) == set(Choice::Relative, now + 1800));
    CHECK(sp::parse("2h", now) == set(Choice::Relative, now + 7200));
    CHECK(sp::parse("3d", now) == set(Choice::Relative, now + 3 * 86400));
    CHECK(sp::parse("in 1 minute", now) == set(Choice::Relative, now + 60));
    CHECK(sp::parse("1 hour", now) == set(Choice::Relative, now + 3600));
    CHECK(sp::parse("in 10 days", now) == set(Choice::Relative, now + 10 * 86400));
    CHECK(sp::parse("in 45 minutes", now) == set(Choice::Relative, now + 45 * 60));
    CHECK(sp::parse("9999m", now) == set(Choice::Relative, now + 9999 * 60));
    CHECK(sp::parse("10000m", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("0h", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("in h", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("2 weeks", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("2hours later", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("365d", now) == set(Choice::Relative, now + 365 * 86400));
    CHECK(sp::parse("366d", now) == invalid(Failure::OutOfRange));
    CHECK(sp::parse("8761h", now) == invalid(Failure::OutOfRange));
    CHECK(sp::parse("8760h", now) == set(Choice::Relative, now + 8760 * 3600));
}

static void test_bare_clock_times_take_the_next_future_occurrence() {
    use_zone("America/New_York");
    const std::int64_t now = ny::wed_sep16_1000_edt;
    CHECK(sp::parse("5pm", now) == set(Choice::Time, ny::wed_sep16_1700_edt));
    CHECK(sp::parse("5 pm", now) == set(Choice::Time, ny::wed_sep16_1700_edt));
    CHECK(sp::parse("5 p.m.", now) == set(Choice::Time, ny::wed_sep16_1700_edt));
    CHECK(sp::parse("5:05pm", now) == set(Choice::Time, ny::wed_sep16_1705_edt));
    CHECK(sp::parse("17:05", now) == set(Choice::Time, ny::wed_sep16_1705_edt));
    CHECK(sp::parse("10:30", now) == set(Choice::Time, ny::wed_sep16_1030_edt));
    CHECK(sp::parse("10:30am", now) == set(Choice::Time, ny::wed_sep16_1030_edt));
    CHECK(sp::parse("9:30am", now) == set(Choice::Time, ny::thu_sep17_0930_edt));
    CHECK(sp::parse("09:30", now) == set(Choice::Time, ny::thu_sep17_0930_edt));
    CHECK(sp::parse("10am", now) == set(Choice::Time, ny::wed_sep16_1000_edt + 86400));
    CHECK(sp::parse("12pm", now) == set(Choice::Time, ny::wed_sep16_1200_edt));
    CHECK(sp::parse("12am", now) == set(Choice::Time, ny::thu_sep17_0000_edt));
    CHECK(sp::parse("00:00", now) == set(Choice::Time, ny::thu_sep17_0000_edt));
    CHECK(sp::parse("17", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("9:5pm", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("13pm", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("0am", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("24:00", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("17:60", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("9:30:00", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("123:00", now) == invalid(Failure::Unrecognized));
}

static void test_day_and_time_in_either_order_with_optional_joiners() {
    use_zone("America/New_York");
    const std::int64_t now = ny::wed_sep16_1000_edt;
    CHECK(sp::parse("tomorrow", now) == set(Choice::Named, ny::thu_sep17_0800_edt));
    CHECK(sp::parse("tomorrow 9am", now) == set(Choice::Named, ny::thu_sep17_0900_edt));
    CHECK(sp::parse("tomorrow at 9am", now) == set(Choice::Named, ny::thu_sep17_0900_edt));
    CHECK(sp::parse("9am tomorrow", now) == set(Choice::Named, ny::thu_sep17_0900_edt));
    CHECK(sp::parse("9am on tomorrow", now) == set(Choice::Named, ny::thu_sep17_0900_edt));
    CHECK(sp::parse("tomorrow, 9:00", now) == set(Choice::Named, ny::thu_sep17_0900_edt));
    CHECK(sp::parse("Tomorrow, 9 a.m.", now) == set(Choice::Named, ny::thu_sep17_0900_edt));
    CHECK(sp::parse("friday", now) == set(Choice::Named, ny::fri_sep18_0800_edt));
    CHECK(sp::parse("monday 9:30am", now) == set(Choice::Named, ny::mon_sep21_0930_edt));
    CHECK(sp::parse("9:30 am on monday", now) == set(Choice::Named, ny::mon_sep21_0930_edt));
    CHECK(sp::parse("on monday at 9:30", now) == set(Choice::Named, ny::mon_sep21_0930_edt));
    CHECK(sp::parse("wednesday 5pm", now) == set(Choice::Named, ny::wed_sep16_1700_edt));
    CHECK(sp::parse("wednesday at 5pm", ny::wed_sep16_1800_edt) == set(Choice::Named, ny::wed_sep23_1700_edt));
    CHECK(sp::parse("wednesday", now) == set(Choice::Named, ny::wed_sep16_1000_edt + 7 * 86400 - 2 * 3600));
    CHECK(sp::parse("next workday", ny::wed_sep16_1800_edt) == set(Choice::Named, ny::thu_sep17_0800_edt));
    CHECK(sp::parse("next workday", ny::fri_sep18_1500_edt) == set(Choice::Named, ny::mon_sep21_0800_edt));
    CHECK(sp::parse("next workday 9:30am", ny::fri_sep18_1500_edt) == set(Choice::Named, ny::mon_sep21_0930_edt));
    CHECK(sp::parse("9:30am next workday", ny::fri_sep18_1500_edt) == set(Choice::Named, ny::mon_sep21_0930_edt));
    CHECK(sp::parse("9 30 am next workday", ny::fri_sep18_1500_edt) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("sunday", ny::fri_sep18_1500_edt) == set(Choice::Named, ny::sun_sep20_0800_edt));
    CHECK(sp::parse("next monday", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("tomorrow tomorrow", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("tomorrow 25pm", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("at 9am", now) == invalid(Failure::Unrecognized));
    CHECK(sp::parse("workday", now) == invalid(Failure::Unrecognized));
}

static void test_dst_gaps_are_refused_and_never_normalised() {
    use_zone("America/New_York");
    CHECK(sp::parse("tomorrow 2:30am", ny::sat_mar07_1000_est) == invalid(Failure::NonexistentTime));
    CHECK(sp::parse("tomorrow at 2:30", ny::sat_mar07_1000_est) == invalid(Failure::NonexistentTime));
    CHECK(sp::parse("sunday 2:30am", ny::sat_mar07_1000_est) == invalid(Failure::NonexistentTime));
    CHECK(sp::parse("2:30am", ny::sat_mar07_1000_est) == set(Choice::Time, ny::mon_mar09_0230_edt));
    CHECK(sp::parse("tomorrow 3am", ny::sat_mar07_1000_est) == set(Choice::Named, ny::sun_mar08_0300_edt));
    CHECK(sp::parse("3am", ny::sat_mar07_1000_est) == set(Choice::Named, ny::sun_mar08_0300_edt) ||
          sp::parse("3am", ny::sat_mar07_1000_est) == set(Choice::Time, ny::sun_mar08_0300_edt));
    CHECK(sp::parse("3am", ny::sat_mar07_1000_est).until == ny::sun_mar08_0300_edt);
    const Command fall = sp::parse("tomorrow 1:30am", ny::sat_oct31_1000_edt);
    CHECK(fall.kind == Kind::Set);
    CHECK(fall.until == ny::sun_nov01_0130_edt_first || fall.until == ny::sun_nov01_0130_est_second);
    CHECK(fall.until == ny::sun_nov01_0130_edt_first);
    CHECK(sp::strict_wall_clock(ny::sat_mar07_1000_est, 1, 2, 30, {}) == std::nullopt);
    CHECK(sp::strict_wall_clock(ny::sat_mar07_1000_est, 1, 3, 0, {}) == std::optional<std::int64_t>{ny::sun_mar08_0300_edt});
    CHECK(sp::next_wall_clock(ny::sat_mar07_1000_est, 2, 30, {}) == std::optional<std::int64_t>{ny::mon_mar09_0230_edt});
    use_zone("Europe/Berlin");
    CHECK(sp::parse("tomorrow 2:30am", berlin::sat_mar28_1000_cet) == invalid(Failure::NonexistentTime));
    CHECK(sp::parse("2:30", berlin::sat_mar28_1000_cet) == set(Choice::Time, berlin::mon_mar30_0230_cest));
    use_zone("America/New_York");
}

static void test_the_viewer_zone_decides() {
    use_zone("Europe/Berlin");
    CHECK(sp::parse("5pm", berlin::wed_sep16_1000_cest) == set(Choice::Time, berlin::wed_sep16_1700_cest));
    CHECK(sp::parse("tomorrow 9am", berlin::wed_sep16_1000_cest) == set(Choice::Named, berlin::thu_sep17_0900_cest));
    CHECK(sp::parse("wednesday 17:00", berlin::wed_sep16_1000_cest) == set(Choice::Named, berlin::wed_sep16_1700_cest));
    use_zone("America/New_York");
    CHECK(sp::parse("5pm", ny::wed_sep16_1000_edt) == set(Choice::Time, ny::wed_sep16_1700_edt));
}

static void test_range_is_the_presets_rule_and_hints_are_verbatim() {
    const std::int64_t now = ny::wed_sep16_1000_edt;
    CHECK(sp::parse("in 1 minute", now).kind == Kind::Set);
    CHECK(sp::parse("365d", now).kind == Kind::Set);
    CHECK(sp::parse("366d", now) == invalid(Failure::OutOfRange));
    CHECK(std::string(sp::hint(Failure::NonexistentTime)) ==
          "That clock time does not exist on that day. Daylight saving skips it.");
    CHECK(std::string(sp::hint(Failure::OutOfRange)) == "Pick a time within the next year.");
    CHECK(std::string(sp::hint(Failure::Unrecognized)) ==
          "Try `9am`, `in 2h`, `wednesday 9am`, `tomorrow at 9am`, `next workday`, or `off`.");
    CHECK(sp::kMaxArgumentCharacters == 40);
}

static void test_the_prompt_outcome_never_wakes_from_off_or_empty() {
    using sp::PromptOutcome;
    using OK = PromptOutcome::Kind;
    const std::int64_t now = ny::wed_sep16_1000_edt;
    const auto set = sp::outcome_of_submitted_phrase("tomorrow 9am", now);
    CHECK(set.kind == OK::Set && set.until == ny::thu_sep17_0900_edt);
    CHECK(sp::outcome_of_submitted_phrase("off", now).kind == OK::Quiet);
    CHECK(sp::outcome_of_submitted_phrase("", now).kind == OK::Quiet);
    CHECK(sp::outcome_of_submitted_phrase("   ", now).kind == OK::Quiet);
    const auto refused = sp::outcome_of_submitted_phrase("yesterday", now);
    CHECK(refused.kind == OK::Refuse);
    CHECK(refused.hint == sp::hint(Failure::Unrecognized));
    CHECK(sp::outcome_of_submitted_phrase("366d", now).hint == sp::hint(Failure::OutOfRange));
    CHECK(sp::outcome_of_submitted_phrase("tomorrow 2:30am", ny::sat_mar07_1000_est).hint ==
          sp::hint(Failure::NonexistentTime));
    CHECK(sp::outcome_of_submitted_phrase("5pm", ny::wed_sep16_1000_edt).until == ny::wed_sep16_1700_edt);
    CHECK(sp::outcome_of_submitted_phrase("5pm", ny::wed_sep16_1800_edt).until == ny::wed_sep16_1700_edt + 86400);
    CHECK(sp::outcome_of_submitted_phrase("wednesday 5pm", ny::wed_sep16_1800_edt).until == ny::wed_sep23_1700_edt);
}

namespace np = hanabi::native_snooze_prompt;
using RK = np::Result::Kind;

static np::Request req(const char* scope) {
    np::Request r;
    r.scope = scope;
    return r;
}

static void test_the_prompt_lifecycle_refuses_reentry_and_gates_results_by_generation() {
    np::Lifecycle life;
    CHECK(!life.busy());
    const std::uint64_t g1 = life.request(req("session:a"));
    CHECK(g1 != 0 && life.busy() && life.current_generation() == g1);
    CHECK(life.request(req("session:b")) == 0);
    const auto dispatched = life.take_for_dispatch();
    CHECK(dispatched.has_value() && dispatched->generation == g1 && dispatched->scope == "session:a");
    CHECK(!life.take_for_dispatch().has_value());
    CHECK(life.request(req("session:c")) == 0);
    int modals = 0;
    const np::Result shown = np::show_or_refuse(
        life, *dispatched, [] { return true; },
        [&](const np::Request&) {
            ++modals;
            np::Result r;
            r.kind = RK::Set;
            r.until_unix_sec = 42;
            return r;
        });
    CHECK(modals == 1);
    CHECK(shown.kind == RK::Set && shown.until_unix_sec == 42 && shown.generation == g1);
    CHECK(!life.busy());
    np::Result out;
    CHECK(!life.take_result(g1 + 1, &out));
    CHECK(life.take_result(g1, &out));
    CHECK(out == shown);
    CHECK(!life.take_result(g1, &out));
}

static void test_a_policy_refusal_at_show_time_cancels_without_a_modal() {
    np::Lifecycle life;
    const std::uint64_t g = life.request(req("session:a"));
    const auto dispatched = life.take_for_dispatch();
    CHECK(dispatched.has_value());
    int modals = 0;
    const np::Result refused = np::show_or_refuse(
        life, *dispatched, [] { return false; },
        [&](const np::Request&) {
            ++modals;
            np::Result r;
            r.kind = RK::Set;
            return r;
        });
    CHECK(modals == 0);
    CHECK(refused.kind == RK::Cancelled && refused.generation == g);
    CHECK(!life.busy());
    np::Result out;
    CHECK(life.take_result(g, &out));
    CHECK(out.kind == RK::Cancelled);
    CHECK(life.request(req("session:b")) != 0);
}

static void test_a_cancel_that_raced_the_pump_wins_and_a_showing_modal_cannot_be_cancelled() {
    np::Lifecycle life;
    const std::uint64_t g = life.request(req("session:a"));
    const auto dispatched = life.take_for_dispatch();
    life.cancel(g);
    CHECK(!life.busy());
    int modals = 0;
    const np::Result dropped = np::show_or_refuse(
        life, *dispatched, [] { return true; },
        [&](const np::Request&) {
            ++modals;
            return np::Result{};
        });
    CHECK(modals == 0);
    CHECK(dropped.kind == RK::Cancelled);
    np::Result out;
    CHECK(!life.take_result(g, &out));

    const std::uint64_t h = life.request(req("session:b"));
    (void)life.take_for_dispatch();
    CHECK(life.begin_show(h));
    life.cancel(h);
    CHECK(life.is_showing(h));
    CHECK(life.busy());
    life.cancel(0);
    CHECK(life.is_showing(h));
    np::Result quiet;
    quiet.generation = h;
    quiet.kind = RK::Quiet;
    life.deliver(quiet);
    CHECK(!life.is_showing(h) && !life.busy());
    CHECK(life.take_result(h, &out) && out.kind == RK::Quiet);
}

static void test_an_injected_result_for_another_generation_leaves_the_showing_slot_alone() {
    np::Lifecycle life;
    const std::uint64_t m = life.request(req("session:m"));
    (void)life.take_for_dispatch();
    CHECK(life.begin_show(m));
    np::Result stale;
    stale.generation = m + 7;
    stale.kind = RK::Set;
    stale.until_unix_sec = 99;
    life.inject_result_for_test(stale);
    CHECK(life.is_showing(m));
    CHECK(life.busy());
    np::Result out;
    CHECK(!life.take_result(m + 7, &out));
    CHECK(!life.take_result(m, &out));
    np::Result real;
    real.generation = m;
    real.kind = RK::Refused;
    life.inject_result_for_test(real);
    CHECK(!life.is_showing(m) && !life.busy());
    CHECK(life.take_result(m, &out) && out.kind == RK::Refused);

    const std::uint64_t q = life.request(req("session:q"));
    np::Result for_queued;
    for_queued.generation = q;
    for_queued.kind = RK::Cancelled;
    life.inject_result_for_test(for_queued);
    CHECK(!life.busy());
    CHECK(life.take_result(q, &out) && out.kind == RK::Cancelled);
    np::Result nobody;
    nobody.generation = 12345;
    life.inject_result_for_test(nobody);
    CHECK(!life.take_result(12345, &out));
}

int main() {
    std::printf("== test_snooze_parse (custom phrase grammar) ==\n");
    use_zone("America/New_York");
    test_normalisation_empty_off_and_length();
    test_relative_forms();
    test_bare_clock_times_take_the_next_future_occurrence();
    test_day_and_time_in_either_order_with_optional_joiners();
    test_dst_gaps_are_refused_and_never_normalised();
    test_the_viewer_zone_decides();
    test_range_is_the_presets_rule_and_hints_are_verbatim();
    test_the_prompt_outcome_never_wakes_from_off_or_empty();
    test_the_prompt_lifecycle_refuses_reentry_and_gates_results_by_generation();
    test_a_policy_refusal_at_show_time_cancels_without_a_modal();
    test_a_cancel_that_raced_the_pump_wins_and_a_showing_modal_cannot_be_cancelled();
    test_an_injected_result_for_another_generation_leaves_the_showing_slot_alone();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
