#pragma once

// ---------------------------------------------------------------------------
// Reading a unified diff, one line at a time.
//
// The edit tool reports what it changed as a patch, and the transcript used to
// print it in one flat grey: a wall of text where "-" and "+" are the only
// difference between the code that was there and the code that is there now.
// Colour is the whole point of a diff, so the transcript needs to know what
// each line IS.
//
// PURE and header-only: no UI types, no afterhours, no api types. That is what
// lets the classification be unit-tested (tests/unit/test_diff.cpp) instead of
// eyeballed in a screenshot — the renderer only picks colours from the answers
// this gives.
//
// WHAT IS DELIBERATELY CONSERVATIVE. `looks_like_diff` demands a STRUCTURAL
// marker — a hunk header, or the `---`/`+++` file-header pair. It is not
// enough for lines to start with "+" or "-", because plenty of ordinary tool
// output does: a markdown bullet list, a CLI's usage text, a table drawn in
// dashes. Painting those red would be worse than leaving a real diff grey, so
// the text has to say it is a diff before it is read as one.
// ---------------------------------------------------------------------------

#include <string>
#include <string_view>

namespace hanabi::diff {

enum class LineKind {
    Context,  // unchanged, or anything a diff carries that is not the below
    Added,    // "+…"
    Removed,  // "-…"
    Hunk,     // "@@ …" — the location header
    Meta,     // "--- a/x", "+++ b/x", "diff --git …", "index …"
};

inline bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// What one line of a patch is. The file headers are checked BEFORE the +/-
// rules they would otherwise match: "--- a/foo.rs" is not three deletions.
inline LineKind classify(std::string_view line) {
    if (starts_with(line, "@@")) return LineKind::Hunk;
    if (starts_with(line, "--- ") || starts_with(line, "+++ ") ||
        starts_with(line, "diff --git ") || starts_with(line, "index "))
        return LineKind::Meta;
    if (starts_with(line, "+")) return LineKind::Added;
    if (starts_with(line, "-")) return LineKind::Removed;
    return LineKind::Context;
}

// Does this text announce itself as a patch? See the note above: a structural
// marker is required, never a majority vote on leading characters.
inline bool looks_like_diff(std::string_view text) {
    bool sawOldFile = false;
    bool sawNewFile = false;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        const std::string_view line =
            text.substr(start, (nl == std::string_view::npos ? text.size() : nl) -
                                   start);
        if (starts_with(line, "@@")) return true;
        if (starts_with(line, "--- ")) sawOldFile = true;
        if (starts_with(line, "+++ ")) sawNewFile = true;
        if (sawOldFile && sawNewFile) return true;
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return false;
}

}  // namespace hanabi::diff
