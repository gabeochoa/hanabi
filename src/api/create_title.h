#pragma once

// The title a NEW thread is created with (Knots kt-2guz; the reference's
// ThreadTitle.explicitCreateTitle and proposedTitle). Pure.

#include <string>

namespace api::create_title {

inline constexpr std::size_t kMaxTitle = 120;

inline std::string trimmed(const std::string& raw) {
    const auto first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = raw.find_last_not_of(" \t\r\n");
    return raw.substr(first, last - first + 1);
}

// Cut to at most `max` bytes without splitting a UTF-8 sequence.
inline std::string cut_utf8(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
    return s;
}

// What a typed name becomes: trimmed, one line, capped; empty means none.
inline std::string explicit_title(const std::string& raw) {
    std::string t = trimmed(raw);
    for (char& c : t)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return cut_utf8(t, kMaxTitle);
}

// The title the create carries: the typed name when there is one, else the
// prompt's first line (what every create did before the field existed).
inline std::string for_create(const std::string& typedName, const std::string& prompt) {
    if (std::string t = explicit_title(typedName); !t.empty()) return t;
    std::string first = prompt.substr(0, prompt.find('\n'));
    return cut_utf8(first, kMaxTitle);
}

}  // namespace api::create_title
