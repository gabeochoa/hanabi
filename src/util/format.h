#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

#include "capture_clock.h"

namespace fmtutil {

// ASCII case conversion (shared — was hand-rolled in ~4 places, REFACTOR_REVIEW
// 1c). Deliberately ASCII-only: the UI labels these format are ASCII section
// headers / search queries, and an ASCII flip is locale-independent + cheap.
inline std::string to_upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}
inline std::string to_lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}
inline char lower_ch(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Case-insensitive substring search that ALLOCATES NOTHING, returning the
// offset of the first match or npos. `lowerNeedle` is already lowercased by
// the caller, because a filter lowercases its query once and its candidates
// once each -- the sidebar's search and the command palette both do exactly
// that.
//
// The obvious spelling, to_lower(hay).find(needle), builds and frees a
// lowercased copy of the HAYSTACK per candidate. A filter over a catalog pays
// that per row per frame: docs/perf/ALLOCATIONS.md entry 7 measures the
// sidebar's at one malloc per session per frame, 57% of everything the frame
// allocated at 2,020 sessions and 88% at 20,020.
inline size_t find_lower(std::string_view hay, std::string_view lowerNeedle) {
    if (lowerNeedle.empty()) return 0;
    if (lowerNeedle.size() > hay.size()) return std::string_view::npos;
    const char first = lowerNeedle.front();
    const size_t last = hay.size() - lowerNeedle.size();
    for (size_t i = 0; i <= last; ++i) {
        if (lower_ch(hay[i]) != first) continue;
        size_t j = 1;
        for (; j < lowerNeedle.size(); ++j)
            if (lower_ch(hay[i + j]) != lowerNeedle[j]) break;
        if (j == lowerNeedle.size()) return i;
    }
    return std::string_view::npos;
}
inline bool contains_lower(std::string_view hay,
                           std::string_view lowerNeedle) {
    return find_lower(hay, lowerNeedle) != std::string_view::npos;
}
// ASCII Title Case: upper-case the first letter of each word (word boundaries
// are space / '-' / '_'). Locale-independent, ASCII-only (same rationale as
// to_upper/to_lower). Shared so the sidebar's folder-name display and any other
// title-caser use ONE implementation (REFACTOR_REVIEW 1c).
inline std::string ascii_title(std::string s) {
    bool start = true;
    for (char& c : s) {
        if (c == ' ' || c == '-' || c == '_') { start = true; continue; }
        if (start && c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        start = false;
    }
    return s;
}

// Compact relative age from a unix epoch (seconds): "now", "5m", "3h", "2d"...
// One canonical ladder. The two-arg overload takes an explicit `now` so callers
// that need a deterministic / within-frame-consistent reference (and testable
// output) can pass it; the no-arg version reads the clock. Both share the exact
// same ladder so a sidebar row and a header can never disagree.
inline constexpr int64_t kDaySecs = 24 * 60 * 60;  // seconds in a day
inline std::string relative_time(int64_t epoch, int64_t now) {
    if (epoch <= 0) return "";
    long s = static_cast<long>(now - epoch);
    if (s < 0) s = 0;
    if (s < 60) return "now";
    long m = s / 60;
    if (m < 60) return std::to_string(m) + "m";
    long h = m / 60;
    if (h < 24) return std::to_string(h) + "h";
    long d = h / 24;
    if (d < 7) return std::to_string(d) + "d";
    if (d < 30) return std::to_string(d / 7) + "w";
    if (d < 365) return std::to_string(d / 30) + "mo";
    return std::to_string(d / 365) + "y";
}
inline std::string relative_time(int64_t epoch) {
    return relative_time(epoch, capture_clock::display_now());
}

// Clock time for a timestamp, e.g. "14:05". Empty for an unset (<=0) epoch.
// Local time: these are stamps on the user's own conversation, so the machine's
// zone is the right one.
inline std::string clock_time(int64_t epoch) {
    if (epoch <= 0) return "";
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
    if (localtime_r(&t, &tm) == nullptr) return "";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return std::string(buf);
}

// Two stamps falling on the same LOCAL calendar day. Not (a - b) < 24h: two
// messages eleven hours apart can be on either side of midnight, and the
// reader thinks in days, not in elapsed seconds. DST is handled for free —
// localtime_r does the arithmetic, we only compare the fields.
inline bool same_local_day(int64_t a, int64_t b) {
    const std::time_t ta = static_cast<std::time_t>(a);
    const std::time_t tb = static_cast<std::time_t>(b);
    std::tm x{}, y{};
    if (localtime_r(&ta, &x) == nullptr || localtime_r(&tb, &y) == nullptr)
        return a == b;
    return x.tm_year == y.tm_year && x.tm_yday == y.tm_yday;
}

// The name of the day a stamp falls on, for a transcript date row:
// "Today", "Yesterday", else "Monday, August 19" — with the year appended
// once the date is in another year, since "August 19" alone is a different
// day every year and the reader is being told WHICH one.
inline std::string day_label(int64_t epoch, int64_t now) {
    if (epoch <= 0) return "";
    if (same_local_day(epoch, now)) return "Today";
    if (same_local_day(epoch, now - kDaySecs)) return "Yesterday";
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
    if (localtime_r(&t, &tm) == nullptr) return "";
    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%A, %B", &tm) == 0) return "";
    // The day number is appended rather than formatted (%d zero-pads and %e
    // space-pads; a date row wants neither "August 09" nor "August  9").
    std::string out = std::string(buf) + " " + std::to_string(tm.tm_mday);
    std::tm nowTm{};
    const std::time_t tnow = static_cast<std::time_t>(now);
    if (localtime_r(&tnow, &nowTm) != nullptr && nowTm.tm_year != tm.tm_year)
        out += ", " + std::to_string(1900 + tm.tm_year);
    return out;
}
inline std::string day_label(int64_t epoch) {
    return day_label(epoch, capture_clock::display_now());
}

// A count as a short human figure: 940, 4.2k, 130k, 1.5M.
//
// One decimal below ten of a unit and none above it, at every magnitude. The
// millions used to skip the decimal, which collapsed every reading from 1.0M
// to 1.9M onto a single "1M" — and a context budget lives exactly there.
inline std::string compact_count(int64_t n) {
    if (n < 0) return "";
    if (n < 1000) return std::to_string(n);
    const auto scaled = [n](int64_t unit, const char* suffix) {
        const int64_t whole = n / unit;
        const int64_t tenth = (n % unit) / (unit / 10);
        if (whole < 10 && tenth > 0)
            return std::to_string(whole) + "." + std::to_string(tenth) + suffix;
        return std::to_string(whole) + suffix;
    };
    if (n < 1000000) return scaled(1000, "k");
    return scaled(1000000, "M");
}

// Truncate to n chars with an ellipsis.
inline std::string ellipsize(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n > 1 ? n - 1 : 0) + "\xe2\x80\xa6";
}

// Display-only title normalization: strip a single leading "[P] " (or bare
// "[P]") parked-marker from a thread title. The app's title convention encodes
// "parked" as a literal "[P] " prefix on the session title; showing that raw
// token in the UI is redundant noise (the parked STATE is already conveyed by
// row styling / ordering). This NEVER mutates the stored title — it is applied
// only at render time, and is the single canonical implementation shared by the
// sidebar rows, digest cards, and tab labels so they can never drift apart.
// The strip is always a PREFIX strip, so the answer is a window onto the
// caller's own string and needs no copy. The sidebar asks this for every
// rendered row of every frame, where a returned std::string is a malloc for
// bytes that already exist. The owning form below is the same function, for
// callers that need to keep the result.
inline std::string_view display_title_view(std::string_view title) {
    size_t i = 0;
    while (i < title.size() && (title[i] == ' ' || title[i] == '\t')) ++i;
    if (title.compare(i, 3, "[P]") == 0) {
        i += 3;
        while (i < title.size() && (title[i] == ' ' || title[i] == '\t')) ++i;
        return title.substr(i);
    }
    return title;
}

inline std::string display_title(const std::string& title) {
    return std::string(display_title_view(title));
}

}  // namespace fmtutil

