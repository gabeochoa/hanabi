#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

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
    return relative_time(epoch, static_cast<int64_t>(std::time(nullptr)));
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
inline std::string display_title(const std::string& title) {
    size_t i = 0;
    while (i < title.size() && (title[i] == ' ' || title[i] == '\t')) ++i;
    if (title.compare(i, 3, "[P]") == 0) {
        i += 3;
        while (i < title.size() && (title[i] == ' ' || title[i] == '\t')) ++i;
        return title.substr(i);
    }
    return title;
}

}  // namespace fmtutil

