#pragma once

#include <cstdint>
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

