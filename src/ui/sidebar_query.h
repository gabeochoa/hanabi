#pragma once

// Sidebar search operators (the reference's Puffin 0.8.4: "Sidebar search by
// `last_active:` today, yesterday, or a date").
//
//   last_active:today        threads whose last activity is today (local time)
//   last_active:yesterday    ... yesterday
//   last_active:2026-09-30   ... on that calendar day
//
// The operator is pulled out of the query and the rest of the words are
// matched as before (title, then the content index). A value this build does
// not understand leaves the whole token as ordinary search text, so a typo
// narrows the list to nothing rather than silently widening it.
//
// Pure: the clock and the time zone come in as arguments, so the unit test
// decides what "today" is.

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace hanabi::sidebar_query {

struct Query {
    std::string text;          // the words left after the operator, trimmed
    bool hasDay = false;
    std::int64_t dayStart = 0;  // unix seconds, inclusive
    std::int64_t dayEnd = 0;    // unix seconds, exclusive
    [[nodiscard]] bool admits(std::int64_t updatedAt) const {
        return !hasDay || (updatedAt >= dayStart && updatedAt < dayEnd);
    }
};

inline constexpr std::string_view kLastActive = "last_active:";

// Local midnight of the day `offsetDays` from the day containing `now`.
inline std::int64_t local_midnight(std::int64_t now, int offsetDays) {
    std::time_t t = static_cast<std::time_t>(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mday += offsetDays;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

inline bool parse_date(std::string_view v, std::int64_t* start) {
    if (v.size() != 10 || v[4] != '-' || v[7] != '-') return false;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
        if (v[i] < '0' || v[i] > '9') return false;
    const auto num = [&](std::size_t at, std::size_t n) {
        int x = 0;
        for (std::size_t i = 0; i < n; ++i) x = x * 10 + (v[at + i] - '0');
        return x;
    };
    std::tm tm{};
    tm.tm_year = num(0, 4) - 1900;
    tm.tm_mon = num(5, 2) - 1;
    tm.tm_mday = num(8, 2);
    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > 31) return false;
    tm.tm_isdst = -1;
    const int wantDay = tm.tm_mday;
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1) || tm.tm_mday != wantDay) return false;  // Feb 30
    *start = static_cast<std::int64_t>(t);
    return true;
}

// `q` is the sidebar query as typed (any case).
inline Query parse(std::string_view q, std::int64_t now) {
    Query out;
    std::string rest;
    std::size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && q[i] == ' ') ++i;
        std::size_t j = i;
        while (j < q.size() && q[j] != ' ') ++j;
        if (j == i) break;
        const std::string_view word = q.substr(i, j - i);
        bool consumed = false;
        if (!out.hasDay && word.size() > kLastActive.size()) {
            std::string head(word.substr(0, kLastActive.size()));
            for (char& c : head)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (head == kLastActive) {
                std::string value(word.substr(kLastActive.size()));
                for (char& c : value)
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                std::int64_t start = 0;
                if (value == "today") {
                    start = local_midnight(now, 0);
                    consumed = true;
                } else if (value == "yesterday") {
                    start = local_midnight(now, -1);
                    consumed = true;
                } else if (parse_date(value, &start)) {
                    consumed = true;
                }
                if (consumed) {
                    out.hasDay = true;
                    out.dayStart = start;
                    out.dayEnd = local_midnight(start, 1);
                }
            }
        }
        if (!consumed) {
            if (!rest.empty()) rest += ' ';
            rest.append(word);
        }
        i = j;
    }
    out.text = std::move(rest);
    return out;
}

}  // namespace hanabi::sidebar_query
