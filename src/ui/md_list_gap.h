#pragma once

// A paragraph under a list gets a paragraph's gap (the reference's
// D122984730): the line directly under a list's last item -- no blank line in
// the source -- is a new paragraph, not more of the item, so it sits a
// paragraph's gap below it instead of tight against it. Hanabi's transcript
// gives a blank source line the paragraph gap, so the rule is spelled as one:
// insert the blank line the author left out. Only between a list item and an
// unindented, non-list, non-blank line; an indented line is the item's own
// continuation and is left alone, and nothing inside a ``` fence is touched.

#include <string>
#include <string_view>

namespace hanabi::md {

// "- x", "* x", "+ x", "• x" (already normalized), "1. x", "12) x".
inline bool is_list_item(std::string_view line) {
    std::size_t s = 0;
    while (s < line.size() && line[s] == ' ') ++s;
    const std::string_view r = line.substr(s);
    if (r.size() >= 2 && (r[0] == '-' || r[0] == '*' || r[0] == '+') && r[1] == ' ') return true;
    if (r.rfind("\xe2\x80\xa2 ", 0) == 0) return true;
    std::size_t d = 0;
    while (d < r.size() && r[d] >= '0' && r[d] <= '9') ++d;
    return d > 0 && d + 1 < r.size() && (r[d] == '.' || r[d] == ')') && r[d + 1] == ' ';
}

inline bool is_fence(std::string_view line) {
    std::size_t s = 0;
    while (s < line.size() && line[s] == ' ') ++s;
    return line.substr(s).rfind("```", 0) == 0;
}

inline std::string with_list_paragraph_gaps(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    bool inFence = false;
    bool prevItem = false;
    std::size_t i = 0;
    while (i <= in.size()) {
        const std::size_t nl = in.find('\n', i);
        const std::size_t end = nl == std::string::npos ? in.size() : nl;
        const std::string_view line(in.data() + i, end - i);
        if (is_fence(line)) {
            inFence = !inFence;
            prevItem = false;
        } else if (!inFence) {
            const bool item = is_list_item(line);
            const bool blank = line.find_first_not_of(" \t") == std::string_view::npos;
            const bool indented = !line.empty() && (line[0] == ' ' || line[0] == '\t');
            if (prevItem && !item && !blank && !indented) out += '\n';
            if (item) prevItem = true;
            else if (blank || !indented) prevItem = false;
        }
        out.append(line);
        if (nl == std::string::npos) break;
        out += '\n';
        i = nl + 1;
    }
    return out;
}

// The same text, or (only when a gap is owed) the gapped copy in `storage`:
// the transcript draws this every frame, so the common case allocates nothing.
inline const std::string& list_gapped(const std::string& in, std::string& storage) {
    // Cheap pre-check: a gap needs a list item followed by an unindented line.
    bool maybe = false;
    std::size_t i = 0;
    bool prevItem = false;
    while (i <= in.size() && !maybe) {
        const std::size_t nl = in.find('\n', i);
        const std::size_t end = nl == std::string::npos ? in.size() : nl;
        const std::string_view line(in.data() + i, end - i);
        const bool item = is_list_item(line);
        const bool blank = line.find_first_not_of(" \t") == std::string_view::npos;
        const bool indented = !line.empty() && (line[0] == ' ' || line[0] == '\t');
        if (prevItem && !item && !blank && !indented) maybe = true;
        if (item) prevItem = true;
        else if (blank || !indented) prevItem = false;
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    if (!maybe) return in;
    storage = with_list_paragraph_gaps(in);
    return storage == in ? in : storage;
}

}  // namespace hanabi::md
