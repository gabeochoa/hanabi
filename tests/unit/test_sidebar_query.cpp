#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "../../src/ui/sidebar_query.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sq = hanabi::sidebar_query;

// 2026-10-02 14:30 local, built in local time so the test holds in any zone.
static std::int64_t at(int y, int mo, int d, int h, int mi) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

static void test_plain_words_pass_through() {
    const auto q = sq::parse("  deploy   staging ", at(2026, 10, 2, 14, 30));
    CHECK(!q.hasDay);
    CHECK(q.text == "deploy staging");
    CHECK(q.admits(0));
}

static void test_today_and_yesterday_are_local_days() {
    const std::int64_t now = at(2026, 10, 2, 14, 30);
    const auto today = sq::parse("last_active:today", now);
    CHECK(today.hasDay && today.text.empty());
    CHECK(today.admits(at(2026, 10, 2, 0, 0)));
    CHECK(today.admits(at(2026, 10, 2, 23, 59)));
    CHECK(!today.admits(at(2026, 10, 1, 23, 59)));
    CHECK(!today.admits(at(2026, 10, 3, 0, 0)));
    const auto yday = sq::parse("Last_Active:YESTERDAY ship", now);
    CHECK(yday.hasDay && yday.text == "ship");
    CHECK(yday.admits(at(2026, 10, 1, 9, 0)));
    CHECK(!yday.admits(at(2026, 10, 2, 9, 0)));
}

static void test_a_date_is_that_calendar_day() {
    const auto q = sq::parse("ledger last_active:2026-09-30", at(2026, 10, 2, 14, 30));
    CHECK(q.hasDay && q.text == "ledger");
    CHECK(q.admits(at(2026, 9, 30, 12, 0)));
    CHECK(!q.admits(at(2026, 9, 29, 23, 0)));
    CHECK(!q.admits(at(2026, 10, 1, 0, 0)));
}

static void test_a_value_it_does_not_know_stays_search_text() {
    const std::int64_t now = at(2026, 10, 2, 14, 30);
    const auto bad = sq::parse("last_active:someday", now);
    CHECK(!bad.hasDay && bad.text == "last_active:someday");
    const auto feb30 = sq::parse("last_active:2026-02-30", now);
    CHECK(!feb30.hasDay);
    const auto bare = sq::parse("last_active:", now);
    CHECK(!bare.hasDay && bare.text == "last_active:");
}

int main() {
    setenv("TZ", "America/New_York", 1);
    tzset();
    test_plain_words_pass_through();
    test_today_and_yesterday_are_local_days();
    test_a_date_is_that_calendar_day();
    test_a_value_it_does_not_know_stays_search_text();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
