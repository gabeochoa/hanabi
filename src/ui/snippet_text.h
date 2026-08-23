#pragma once

// ---------------------------------------------------------------------------
// The line a search result matched on, cut down to a sidebar row.
//
// Split out from snippet_highlight.h so it can be tested with no graphics
// linked: the painting needs a font manager and a live render, the CUTTING is
// string arithmetic with edge cases (a match at the very start, a match longer
// than the window, a line with newlines in it) that a scripted UI test can
// only reach by luck.
// ---------------------------------------------------------------------------

#include <string>

#include "../util/format.h"

namespace hanabi::snippet_text {

// How much of the line to keep around the match. A sidebar row is narrow;
// this is about what fits, not about how much context is nice.
inline constexpr size_t kContext = 22;

// A one-line window of `text` around the first case-insensitive occurrence of
// `query`, marked with an ellipsis where it was cut. Empty when the text does
// not contain the query at all — the caller then has nothing to highlight and
// shows the line plain.
inline std::string extract(const std::string& text, const std::string& query,
                           size_t context = kContext) {
    if (text.empty() || query.empty()) return std::string();
    const std::string hay = fmtutil::to_lower(text);
    const std::string needle = fmtutil::to_lower(query);
    const size_t at = hay.find(needle);
    if (at == std::string::npos) return std::string();

    size_t begin = at > context ? at - context : 0;
    size_t end = at + needle.size() + context;
    if (end > text.size()) end = text.size();
    // Never start or end mid-word: "…eature flags" reads as a different word
    // than the one that matched.
    if (begin > 0) {
        const size_t sp = text.find_first_of(" \t\n", begin);
        if (sp != std::string::npos && sp < at) begin = sp + 1;
    }
    if (end < text.size()) {
        const size_t sp = text.find_last_of(" \t\n", end);
        if (sp != std::string::npos && sp > at + needle.size()) end = sp;
    }

    std::string out = text.substr(begin, end - begin);
    for (char& c : out)
        if (c == '\n' || c == '\t' || c == '\r') c = ' ';
    if (begin > 0) out.insert(0, "\xe2\x80\xa6");
    if (end < text.size()) out += "\xe2\x80\xa6";
    return out;
}

}  // namespace hanabi::snippet_text
