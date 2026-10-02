#pragma once

// `@` in the composer: pointing this message at another thread by name (the
// reference's 0.8.4 thread mentions).
//
// WHAT A MENTION BECOMES ON THE WIRE is the thread's canonical web URL,
// `<web base>/<id>`, and nothing else -- the reference's rule, for its three
// reasons: the id is in it, so the agent (and a reader with a browser) can act
// on it; the title is never on the wire, so a rename leaves no stale copy; and
// a private thread's name cannot leak into a message others can read. So a
// build with no web base configured offers no picker: it has nothing honest to
// write.
//
// The query is the TAIL of the draft: the last `@`, which must open a word
// (draft start or after whitespace, so `gabe@meta.com` and `foo@bar` stay
// text), followed by at most 64 characters whose first is not a space. Spaces
// after that are part of the query (thread names have them); what stops a
// query running into the sentence is having no match left, which closes the
// picker by itself.
//
// Rows: root threads only (a sub-agent belongs to its run), never the thread
// being written in. A bare `@` offers the freshest; a query ranks title-starts,
// then word-starts, then contains, freshest first inside a rung. A thread the
// draft already names keeps its row, marked.
//
// Pure: the catalog, the clock and the web base come in as arguments.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../api/spaces_wire.h"
#include "../api/types.h"
#include "../util/format.h"

namespace hanabi::mention {

inline constexpr std::size_t kMaxQuery = 64;
inline constexpr std::size_t kMaxBare = 8;
inline constexpr std::size_t kMaxMatches = 10;

inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Where the open `@query` starts, or nullopt when the draft has none.
inline std::optional<std::size_t> span_start(std::string_view draft) {
    const std::size_t at = draft.rfind('@');
    if (at == std::string_view::npos) return std::nullopt;
    if (at > 0 && !is_space(draft[at - 1])) return std::nullopt;
    const std::string_view q = draft.substr(at + 1);
    if (q.size() > kMaxQuery) return std::nullopt;
    if (!q.empty() && is_space(q.front())) return std::nullopt;
    return at;
}

inline std::optional<std::string> query(std::string_view draft) {
    const auto at = span_start(draft);
    if (!at) return std::nullopt;
    return std::string(draft.substr(*at + 1));
}

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// 0 = the title starts with the query, 1 = a word does, 2 = it is in there
// somewhere, -1 = not at all. Every occurrence counts, best rung wins.
inline int rank(std::string_view title, std::string_view needleLower) {
    if (needleLower.empty()) return 0;
    const std::string hay = lower(title);
    int best = -1;
    for (std::size_t at = hay.find(needleLower); at != std::string::npos;
         at = hay.find(needleLower, at + 1)) {
        int r = 2;
        if (at == 0) r = 0;
        else {
            const unsigned char p = static_cast<unsigned char>(hay[at - 1]);
            r = (std::isalnum(p) != 0) ? 2 : 1;
        }
        if (best < 0 || r < best) best = r;
        if (best == 0) break;
    }
    return best;
}

// What a row calls a thread: its own name, or -- for one with none yet -- the
// first line of what it carries, so an untitled thread is still a row you can
// pick (the reference's 0.8.5).
inline std::string row_title(const api::SessionSummary& s) {
    // "(untitled)" is the agentcloud parse's own placeholder for a row with no
    // title and no status subject (summary_from_row), so it counts as none.
    std::string t = fmtutil::display_title(s.title);
    if (!t.empty() && t != "(untitled)") return t;
    std::string p = s.preview;
    const std::size_t nl = p.find('\n');
    if (nl != std::string::npos) p.resize(nl);
    if (!p.empty()) return p;
    return "Untitled thread";
}

// The reference a pick writes, with the trailing space that separates it
// from whatever comes next. "" when no web base can spell it.
inline std::string link_for(const std::string& webBase, const std::string& id) {
    std::string base = webBase;
    while (!base.empty() && (base.back() == '/' || base.back() == ' ')) base.pop_back();
    if (base.rfind("https://", 0) != 0 && base.rfind("http://", 0) != 0) return {};
    return base + "/" + id;
}

inline std::string completed(std::string_view draft, const std::string& link) {
    const auto at = span_start(draft);
    if (!at || link.empty()) return std::string(draft);
    return std::string(draft.substr(0, *at)) + link + " ";
}

// Whether the draft already carries exactly this reference, bounded at both
// ends by a non-word character or the draft's edge.
inline bool names(std::string_view draft, const std::string& link) {
    if (link.empty()) return false;
    const auto word = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };
    for (std::size_t at = draft.find(link); at != std::string_view::npos;
         at = draft.find(link, at + 1)) {
        const bool leftOk = at == 0 || !word(draft[at - 1]);
        const std::size_t end = at + link.size();
        const bool rightOk = end == draft.size() || !word(draft[end]);
        if (leftOk && rightOk) return true;
    }
    return false;
}

struct Row {
    std::string id;
    std::string title;
    int64_t updated_at = 0;
    bool archived = false;
    bool alreadyNamed = false;
    bool space = false;  // a Metamate Space: a pick writes `space:<id> `
};

inline constexpr std::size_t kMaxSpaces = 5;

// What a Space pick writes (the reference's spelling); "" for an id that is
// not an fbid, which is then never offered.
inline std::string space_reference(const std::string& id) {
    return api::spaces::valid_id(id) ? "space:" + id : std::string();
}

template <class IsArchived>
std::vector<Row> rows(const std::vector<api::SessionSummary>& sessions,
                      std::string_view draft, const std::string& webBase,
                      const std::string& excludeId, IsArchived&& isArchived,
                      const std::vector<api::spaces::Space>& spaces = {}) {
    std::vector<Row> out;
    const auto q = query(draft);
    if (!q) return out;
    // Threads need a web base to be spelled; Spaces do not.
    const bool threadsOk = !link_for(webBase, "x").empty();
    const std::string needle = lower(*q);
    struct Ranked {
        int rank;
        Row row;
    };
    std::vector<Ranked> found;
    for (const api::SessionSummary& s : sessions) {
        if (!threadsOk) break;
        if (!s.parent_id.empty() || s.id == excludeId || s.id.empty()) continue;
        Row r{s.id, row_title(s), s.updated_at, isArchived(s), false};
        const int k = rank(r.title, needle);
        if (k < 0) continue;
        r.alreadyNamed = names(draft, link_for(webBase, s.id));
        found.push_back({needle.empty() ? 0 : k, std::move(r)});
    }
    std::stable_sort(found.begin(), found.end(), [](const Ranked& a, const Ranked& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.row.updated_at > b.row.updated_at;
    });
    const std::size_t cap = needle.empty() ? kMaxBare : kMaxMatches;
    for (std::size_t i = 0; i < found.size() && i < cap; ++i)
        out.push_back(std::move(found[i].row));
    // METAMATE SPACES after the threads: the Space list's own order at a bare
    // `@`, the same three rungs for a query.
    std::vector<Ranked> sp;
    for (const api::spaces::Space& s : spaces) {
        if (space_reference(s.id).empty() || s.name.empty()) continue;
        const int k = rank(s.name, needle);
        if (k < 0) continue;
        Row r;
        r.id = s.id;
        r.title = s.emoji.empty() ? s.name : s.emoji + " " + s.name;
        r.space = true;
        r.alreadyNamed = names(draft, space_reference(s.id));
        sp.push_back({needle.empty() ? 0 : k, std::move(r)});
    }
    std::stable_sort(sp.begin(), sp.end(),
                     [](const Ranked& a, const Ranked& b) { return a.rank < b.rank; });
    for (std::size_t i = 0; i < sp.size() && i < kMaxSpaces; ++i)
        out.push_back(std::move(sp[i].row));
    return out;
}

// THREAD REFERENCES IN TEXT: every bare `<web base>/<session id>` -- what a
// pick writes, and what the web app's own links look like. A URL that runs on
// into more path, a query or a fragment is not a bare reference and is left
// alone; neither is one glued to a word before it.
struct Found {
    std::size_t off = 0;
    std::size_t len = 0;
    std::string id;
};
inline bool id_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
}
inline std::vector<Found> find_threads(const std::string& text, const std::string& webBase) {
    std::vector<Found> out;
    std::string base = link_for(webBase, "");
    if (base.empty()) return out;
    for (std::size_t at = text.find(base); at != std::string::npos;
         at = text.find(base, at + 1)) {
        if (at > 0 && id_char(text[at - 1])) continue;
        std::size_t j = at + base.size();
        while (j < text.size() && id_char(text[j])) ++j;
        if (j == at + base.size()) continue;
        if (j < text.size() && (text[j] == '/' || text[j] == '?' || text[j] == '#')) continue;
        out.push_back({at, j - at, text.substr(at + base.size(), j - at - base.size())});
        at = j - 1;
    }
    return out;
}

}  // namespace hanabi::mention
