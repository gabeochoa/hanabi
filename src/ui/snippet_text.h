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
#include <string_view>

#include "../util/format.h"

namespace hanabi::snippet_text {

// How much of the line to keep around the match. A sidebar row is narrow;
// this is about what fits, not about how much context is nice.
inline constexpr size_t kContext = 22;

// A one-line window of `text` around the first occurrence of `lowerQuery`,
// marked with an ellipsis where it was cut, written into `out`. Returns
// whether there was a match at all: on false `out` is empty, and the caller
// then has nothing to highlight and shows the line plain.
//
// `lowerQuery` must ALREADY be lowercased — the fold is one-sided, against a
// haystack folded a byte at a time as it is scanned, so a query with a capital
// in it matches nothing. A filter lowercases its query once per frame and its
// candidates never, which is the point. `out` is the caller's own buffer:
// reusing one across rows keeps its capacity, so a steady frame allocates
// nothing here at all.
inline bool extract_into(std::string& out, std::string_view text,
                         std::string_view lowerQuery,
                         size_t context = kContext) {
    out.clear();
    if (text.empty() || lowerQuery.empty()) return false;
    const size_t at = fmtutil::find_lower(text, lowerQuery);
    if (at == std::string_view::npos) return false;

    size_t begin = at > context ? at - context : 0;
    size_t end = at + lowerQuery.size() + context;
    if (end > text.size()) end = text.size();
    // Never start or end mid-word: "…eature flags" reads as a different word
    // than the one that matched.
    if (begin > 0) {
        const size_t sp = text.find_first_of(" \t\n", begin);
        if (sp != std::string_view::npos && sp < at) begin = sp + 1;
    }
    if (end < text.size()) {
        const size_t sp = text.find_last_of(" \t\n", end);
        if (sp != std::string_view::npos && sp > at + lowerQuery.size())
            end = sp;
    }

    out.assign(text.substr(begin, end - begin));
    for (char& c : out)
        if (c == '\n' || c == '\t' || c == '\r') c = ' ';
    if (begin > 0) out.insert(0, "\xe2\x80\xa6");
    if (end < text.size()) out += "\xe2\x80\xa6";
    return true;
}

// The value-returning spelling, kept for callers that have a query in hand
// rather than a folded one: it lowers the query, then defers to the same cut.
inline std::string extract(const std::string& text, const std::string& query,
                           size_t context = kContext) {
    std::string out;
    extract_into(out, text, fmtutil::to_lower(query), context);
    return out;
}

}  // namespace hanabi::snippet_text
