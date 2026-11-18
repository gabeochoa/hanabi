#pragma once

// The title a NEW thread is created with (Knots kt-2guz, kt-jko9; the
// reference's ThreadTitle in AgentcloudSessionSummary.swift at e8395d87eff5:
// explicitCreateTitle, createTitle, isPlaceholder and named). Pure.
//
// The reference's rule, rung for rung: the server writes a model-generated
// title only for a root session that arrives UNTITLED, and any title on the
// create -- a prompt slice included -- suppresses it for good (spec 398
// FR11/FR14). So with automatic naming on (the reference's Settings -> Chat
// -> "Name new threads automatically", default on), only a name the person
// typed goes on the wire; off, the first line of the prompt is stamped as
// before.

#include <string>

namespace api::create_title {

inline constexpr std::size_t kMaxTitle = 120;

// What the reference's createSession stamps when the caller named nothing
// (its DEFAULT_SESSION_TITLE). A stored title with this text is a
// placeholder, not a name -- unless a person typed it (title_is_human).
inline constexpr const char* kCreatePlaceholder = "agentcloud session";

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

// Whether a stored title is the create-time placeholder or nothing at all.
// A HUMAN title is never filtered by its text -- the caller checks
// title_is_human first (see named_title).
inline bool is_placeholder(const std::string& title) {
    const std::string t = trimmed(title);
    return t.empty() || t == kCreatePlaceholder;
}

// The best name for a thread, or "" when nothing on the wire named it (the
// reference's ThreadTitle.named): human title -> stored title that is not
// the placeholder -> the agent's status subject -> its headline -> nothing.
// The caller supplies the fallback ("(untitled)" on a catalog row).
inline std::string named_title(const std::string& title, bool isHuman,
                               const std::string& subject,
                               const std::string& headline) {
    const std::string stored = trimmed(title);
    if (isHuman && !stored.empty()) return stored;
    if (!is_placeholder(stored)) return stored;
    if (const std::string s = trimmed(subject); !s.empty()) return s;
    if (const std::string h = trimmed(headline); !h.empty()) return h;
    return {};
}

// The title the create carries: the typed name when there is one, else the
// prompt's first line (what every create did before the field existed).
inline std::string for_create(const std::string& typedName, const std::string& prompt) {
    if (std::string t = explicit_title(typedName); !t.empty()) return t;
    std::string first = prompt.substr(0, prompt.find('\n'));
    return cut_utf8(first, kMaxTitle);
}

// The title a create sends when automatic naming is in play, or "" to send
// none (the reference's ThreadTitle.createTitle). Empty is a real answer:
// create_command_json omits an empty title, and that omission is what lets
// the server name the thread. An image-only prompt off the automatic path
// is named "Image", as the reference names it.
inline std::string for_create(const std::string& typedName, const std::string& prompt,
                              bool autoName, bool hasImages) {
    if (std::string t = explicit_title(typedName); !t.empty()) return t;
    if (autoName) return {};
    if (const std::string slice = for_create("", prompt); !slice.empty()) return slice;
    return hasImages ? std::string("Image") : std::string{};
}

}  // namespace api::create_title
