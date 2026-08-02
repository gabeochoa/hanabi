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

// Compact relative age from a unix epoch (seconds): "now", "5m", "3h", "2d"...
inline std::string relative_time(int64_t epoch) {
    if (epoch <= 0) return "";
    double secs = std::difftime(std::time(nullptr), static_cast<std::time_t>(epoch));
    if (secs < 0) secs = 0;
    long s = static_cast<long>(secs);
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

