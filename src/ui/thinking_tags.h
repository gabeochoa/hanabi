#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

// Display-only unwrapping of the model's <thinking> protocol tags in
// assistant prose: the stored text, the raw view, Copy and Export keep the
// tags; the rendered prose drops them. Pairs are matched with a stack and
// both plain tokens removed (the text between is kept); an attributed tag
// claims its partner and both survive; a lone plain tag is removed only at
// the text's first non-whitespace offset. A tag inside a code span, on a
// blockquote line, or preceded by a backslash is sheltered. Fenced code
// never reaches this (the caller splits fences first).
namespace hanabi::thinking_tags {

struct Tag {
    std::size_t start = 0;
    std::size_t end = 0;  // one past '>'
    bool closer = false;
    bool plain = false;
};

inline bool ci_starts_with(std::string_view s, std::string_view lower) {
    if (s.size() < lower.size()) return false;
    for (std::size_t i = 0; i < lower.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != lower[i]) return false;
    return true;
}

inline std::size_t first_non_space(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return i;
}

inline bool on_blockquote_line(std::string_view s, std::size_t at) {
    std::size_t ls = s.rfind('\n', at == 0 ? 0 : at - 1);
    ls = ls == std::string_view::npos ? 0 : ls + 1;
    while (ls < at && (s[ls] == ' ' || s[ls] == '\t')) ++ls;
    return ls < s.size() && s[ls] == '>' && ls < at;
}

// Scans for <thinking…> / </thinking…> tags, skipping sheltered ones.
inline std::vector<Tag> scan(std::string_view s) {
    std::vector<Tag> tags;
    bool inCode = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '`') {
            inCode = !inCode;
            continue;
        }
        if (inCode || c != '<') continue;
        const bool escaped = i > 0 && s[i - 1] == '\\';
        std::string_view rest = s.substr(i);
        bool closer = false;
        std::string_view name = "<thinking";
        if (ci_starts_with(rest, "</thinking")) {
            closer = true;
            name = "</thinking";
        } else if (!ci_starts_with(rest, "<thinking")) {
            continue;
        }
        const std::size_t after = i + name.size();
        if (after >= s.size()) continue;
        const char d = s[after];
        if (d != '>' && d != ' ' && d != '\t') continue;
        const std::size_t gt = s.find('>', after);
        if (gt == std::string_view::npos) continue;
        if (escaped || on_blockquote_line(s, i)) {
            i = gt;
            continue;
        }
        Tag t;
        t.start = i;
        t.end = gt + 1;
        t.closer = closer;
        t.plain = d == '>';
        tags.push_back(t);
        i = gt;
    }
    return tags;
}

// Fenced code blocks (``` … ```) are left whole; prose between them is
// unwrapped part by part, each part judged from its own first offset.
inline std::string unwrap_prose(std::string_view s);
inline std::string unwrap(std::string_view s) {
    std::string out;
    std::size_t pos = 0;
    for (;;) {
        const std::size_t fence = s.find("```", pos);
        if (fence == std::string_view::npos) {
            out += unwrap_prose(s.substr(pos));
            return out;
        }
        const std::size_t close = s.find("```", fence + 3);
        if (close == std::string_view::npos) {
            // An unclosed fence: prose before it, code to the end.
            out += unwrap_prose(s.substr(pos, fence - pos));
            out.append(s.substr(fence));
            return out;
        }
        out += unwrap_prose(s.substr(pos, fence - pos));
        out.append(s.substr(fence, close + 3 - fence));
        pos = close + 3;
    }
}

inline std::string unwrap_prose(std::string_view s) {
    const std::vector<Tag> tags = scan(s);
    if (tags.empty()) return std::string(s);
    const std::size_t startOffset = first_non_space(s);
    std::vector<bool> remove(tags.size(), false);
    std::vector<std::size_t> openers;
    for (std::size_t k = 0; k < tags.size(); ++k) {
        const Tag& t = tags[k];
        if (!t.closer) {
            openers.push_back(k);
            continue;
        }
        if (openers.empty()) {
            if (t.plain && t.start == startOffset) remove[k] = true;
            continue;
        }
        const std::size_t o = openers.back();
        openers.pop_back();
        if (tags[o].plain && t.plain) remove[o] = remove[k] = true;
    }
    for (const std::size_t o : openers)
        if (tags[o].plain && tags[o].start == startOffset) remove[o] = true;
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    for (std::size_t k = 0; k < tags.size(); ++k) {
        if (!remove[k]) continue;
        out.append(s.substr(pos, tags[k].start - pos));
        pos = tags[k].end;
    }
    out.append(s.substr(pos));
    return out;
}

inline bool is_blank(std::string_view s) { return first_non_space(s) == s.size(); }

}  // namespace hanabi::thinking_tags
