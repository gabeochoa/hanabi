#pragma once

// A selection in the transcript, added to the chat (Knots kt-tlnb, web
// parity: "select code in a diff or code view and add it to chat"). Pure.
//
// Code goes in a fence so its indentation and symbols survive the send; prose
// goes in as a quote, the shape the message Quote action already uses. Either
// way it lands after whatever the person had typed, with a blank line, so
// adding a selection never eats a draft.

#include <string>
#include <string_view>

namespace hanabi::selection_quote {

// Whether a selection reads as code rather than prose: more than one line
// with leading indentation, or the punctuation code is made of.
inline bool looks_like_code(std::string_view s) {
    if (s.find('\n') != std::string_view::npos) {
        for (std::size_t at = 0; at < s.size();) {
            const std::size_t nl = s.find('\n', at);
            const std::string_view line = s.substr(at, nl == std::string_view::npos ? s.npos : nl - at);
            if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) return true;
            if (nl == std::string_view::npos) break;
            at = nl + 1;
        }
    }
    int marks = 0;
    for (char c : s)
        if (c == '{' || c == '}' || c == ';' || c == '(' || c == ')' || c == '=' || c == '<' || c == '>') ++marks;
    return marks >= 3;
}

inline std::string trim_newlines(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == '\n' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == ' ')) --e;
    return std::string(s.substr(b, e - b));
}

// The block a selection becomes.
inline std::string block(std::string_view selection) {
    const std::string sel = trim_newlines(selection);
    if (sel.empty()) return {};
    if (looks_like_code(sel)) {
        // A fence longer than any backtick run inside, so the code cannot close it.
        std::size_t run = 0, best = 0;
        for (char c : sel) {
            run = c == '`' ? run + 1 : 0;
            if (run > best) best = run;
        }
        const std::string fence(best >= 3 ? best + 1 : 3, '`');
        return fence + "\n" + sel + "\n" + fence;
    }
    std::string out = "> ";
    for (char c : sel) {
        out += c;
        if (c == '\n') out += "> ";
    }
    return out;
}

// The draft with the selection added after it.
inline std::string appended(const std::string& draft, std::string_view selection) {
    const std::string b = block(selection);
    if (b.empty()) return draft;
    std::string d = draft;
    while (!d.empty() && (d.back() == '\n' || d.back() == ' ')) d.pop_back();
    if (d.empty()) return b + "\n\n";
    return d + "\n\n" + b + "\n\n";
}

}  // namespace hanabi::selection_quote
