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

// The list options menu writes the same token the field reads (D54).
static void test_the_menu_writes_the_typed_token() {
    namespace sq = hanabi::sidebar_query;
    CHECK(sq::last_active_value("shard LAST_ACTIVE:Today") == "today");
    CHECK(sq::last_active_value("shard").empty());
    CHECK(sq::with_last_active("shard", "today") == "shard last_active:today");
    CHECK(sq::with_last_active("shard last_active:today more", "2026-09-30") ==
          "shard more last_active:2026-09-30");
    CHECK(sq::with_last_active("Last_Active:yesterday", "") == "");
    CHECK(sq::with_last_active("", "yesterday") == "last_active:yesterday");
    CHECK(sq::with_last_active("last_active: x", "") == "last_active: x");  // a bare word is prose
}

// The reference's grammar mirrored in kt-lpat: space:, is:needs-me,
// is:mine, within:, has:files, `-` exclusion and comma any-of.
static sq::Query::Facts facts(std::int64_t updated, const char* space, bool needsMe) {
    sq::Query::Facts f;
    f.updatedAt = updated;
    f.spaceGroup = space;
    f.needsMe = needsMe;
    return f;
}

static void test_reference_filters() {
    const std::int64_t now = at(2026, 10, 2, 14, 30);  // a Friday
    const auto inSpace = facts(at(2026, 10, 2, 9, 0), "space:12345", false);
    const auto noSpace = facts(at(2026, 10, 2, 9, 0), "", false);
    const auto needsMe = facts(at(2026, 10, 2, 9, 0), "", true);

    const auto space = sq::parse("space:12345 ship", now);
    CHECK(space.text == "ship" && space.filters.size() == 1);
    CHECK(space.admitsRow(inSpace));
    CHECK(!space.admitsRow(noSpace));
    CHECK(sq::parse("SPACE:12345", now).admitsRow(inSpace));

    const auto nm = sq::parse("is:needs-me", now);
    CHECK(nm.admitsRow(needsMe));
    CHECK(!nm.admitsRow(inSpace));
    CHECK(sq::parse("is:mine", now).admitsRow(noSpace));  // the viewer's own rows
    const auto notMine = sq::parse("-is:mine", now);
    CHECK(!notMine.admitsRow(noSpace));

    // Any-of inside one token; exclusion of either.
    CHECK(sq::parse("space:999,12345", now).admitsRow(inSpace));
    CHECK(!sq::parse("space:999,888", now).admitsRow(inSpace));
    CHECK(!sq::parse("-space:999,12345", now).admitsRow(inSpace));
    CHECK(sq::parse("-space:999,12345", now).admitsRow(noSpace));
    CHECK(!sq::parse("-is:needs-me", now).admitsRow(needsMe));
    // `mine` inside a list narrows, never widens.
    CHECK(!sq::parse("is:needs-me,mine", now).admitsRow(inSpace));
    CHECK(sq::parse("is:needs-me,mine", now).admitsRow(needsMe));

    // within:7d = today and the 6 whole days before it.
    const auto within = sq::parse("within:7d", now);
    CHECK(within.admits(at(2026, 10, 2, 0, 0)));
    CHECK(within.admits(at(2026, 9, 26, 0, 0)));
    CHECK(!within.admits(at(2026, 9, 25, 23, 59)));
    CHECK(!within.admits(at(2026, 10, 3, 0, 0)));
    CHECK(sq::parse("within:1d", now).admits(at(2026, 10, 2, 8, 0)));
    CHECK(!sq::parse("within:1d", now).admits(at(2026, 10, 1, 23, 0)));
    CHECK(sq::parse("within:\"30 days\"", now).admits(at(2026, 9, 5, 8, 0)));
    CHECK(!sq::parse("-within:7d", now).admits(at(2026, 10, 2, 8, 0)));
    CHECK(sq::parse("-within:7d", now).admits(at(2026, 9, 1, 8, 0)));

    // has:files parses and is recorded, but a list row cannot answer it:
    // unknown neither admits-by-fact nor refuses (held back, not a match).
    const auto files = sq::parse("has:files", now);
    CHECK(files.filters.size() == 1 && files.text.empty());
    CHECK(files.admitsRow(inSpace));
    sq::Query::Facts withFiles = inSpace;
    withFiles.hasFiles = true;
    sq::Query::Facts withoutFiles = inSpace;
    withoutFiles.hasFiles = false;
    CHECK(files.admitsRow(withFiles));
    CHECK(!files.admitsRow(withoutFiles));
    CHECK(!sq::parse("-has:files", now).admitsRow(withFiles));

    // Values this grammar cannot read stay search text, nothing dropped.
    CHECK(sq::parse("within:foo", now).text == "within:foo");
    CHECK(sq::parse("within:0d", now).filters.empty());
    CHECK(sq::parse("has:pizza", now).text == "has:pizza");
    CHECK(sq::parse("is:pinned", now).text == "is:pinned");
    CHECK(sq::parse("-foo", now).text == "-foo");
    // Tokens AND.
    CHECK(!sq::parse("space:12345 is:needs-me", now).admitsRow(inSpace));
}

int main() {
    test_the_menu_writes_the_typed_token();
    setenv("TZ", "America/New_York", 1);
    tzset();
    test_plain_words_pass_through();
    test_today_and_yesterday_are_local_days();
    test_a_date_is_that_calendar_day();
    test_a_value_it_does_not_know_stays_search_text();
    test_reference_filters();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
