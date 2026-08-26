#pragma once

// ---------------------------------------------------------------------------
// Search across threads, over the corpus this app actually has.
//
// THE CHOICE. Cmd+Shift+F wants "search everything you have ever said". Two
// ways to answer it: ask the server, or index what is on this machine.
//
//   * There is no server verb to ask. api::Client (src/api/client.h) has
//     list_sessions / get_session / create_session / send_message / steer /
//     rename / settings / events — and nothing that searches. Config carries a
//     path for every one of those, and none for search. Adding one would mean
//     inventing an endpoint and compiling a guess at a real service into a repo
//     whose whole API layer is deliberately runtime-configured. A search box
//     wired to an endpoint nobody serves is worse than no search box.
//
//   * What is on this machine is: every session's TITLE and PREVIEW (the list
//     is fully loaded, and it is the app's spine), plus the TRANSCRIPTS we hold
//     — the in-memory LRU (ecs::model::TranscriptCache, the last 5 threads) and
//     the on-disk transcript cache (api::disk_cache, every thread opened on a
//     real backend). That is a real corpus and it costs no network.
//
// So: a local index, and the panel SAYS what it could not read. A thread whose
// transcript we have never held is searched by title and preview only, and
// coverage_note() is the sentence that admits it. A search that quietly misses
// half your history and reports "3 results" is the failure mode this file
// exists to avoid — the count is only meaningful next to what it covered.
//
// Pure: no graphics, no app state, no I/O. The caller collects the documents
// (that is where the cache and the disk live) and hands them over.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>
#include <vector>

#include "../util/format.h"

namespace hanabi::search {

// How much of a thread the index could actually read.
//
// Windowed exists because "or as much as the cache kept" used to be a
// parenthetical on Full, and a parenthetical is not an admission. The
// in-memory LRU holds the last 20 messages of a thread; build_index preferred
// it over the disk copy because it is the NEWER of the two, which is true and
// irrelevant — newer is not fuller. So the threads you had just been reading
// were the least deeply indexed ones in the corpus, and coverage_note called
// them Full (docs/SEARCH.md S2).
enum class Depth {
    TitleOnly,  // the list row: title + preview. Never the conversation.
    Windowed,   // the tail of the conversation: what a windowed copy held
    Full,       // the whole conversation, as this machine has it
};

struct Doc {
    std::string id;
    std::string title;
    std::string preview;  // the list's one-line preview; always available
    std::string body;     // the transcript text, when we hold one
    Depth depth = Depth::TitleOnly;
};

struct Hit {
    std::string id;
    std::string title;
    std::string snippet;   // context around the match; empty for a title hit
    bool in_body = false;  // matched inside the conversation, not the title
    bool partial = false;  // this thread was searched title-and-preview only
    bool windowed = false; // only the tail of this thread was searched
};

// What the answer is worth: how many threads were searched, and how many of
// them all the way through.
struct Coverage {
    std::size_t threads = 0;
    std::size_t full = 0;
    std::size_t windowed = 0;
    std::size_t shallow() const { return threads - full - windowed; }
    bool complete() const { return threads > 0 && full == threads; }
};

inline constexpr std::size_t kSnippetContext = 32;

// A run of `body` around [off, off+len), trimmed to word boundaries and marked
// with ellipses where it was cut. Newlines become spaces: a snippet is one
// line in a list row, and a transcript is full of them.
inline std::string snippet_around(const std::string& body, std::size_t off,
                                  std::size_t len,
                                  std::size_t context = kSnippetContext) {
    if (off >= body.size()) return std::string();
    if (off + len > body.size()) len = body.size() - off;

    std::size_t begin = off > context ? off - context : 0;
    std::size_t end = off + len + context;
    if (end > body.size()) end = body.size();
    // Don't start or end mid-word — a snippet that begins "…imization" reads
    // as a different word than the one that matched.
    if (begin > 0) {
        const std::size_t sp = body.find_first_of(" \t\n\r", begin);
        if (sp != std::string::npos && sp < off) begin = sp + 1;
    }
    if (end < body.size()) {
        const std::size_t sp = body.find_last_of(" \t\n\r", end);
        if (sp != std::string::npos && sp > off + len) end = sp;
    }

    std::string out = body.substr(begin, end - begin);
    for (char& c : out)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    // Collapse the runs the flattening just made, so a snippet does not carry
    // the transcript's blank lines across as a stretch of nothing.
    std::string flat;
    flat.reserve(out.size());
    bool space = false;
    for (char c : out) {
        if (c == ' ') {
            if (!space && !flat.empty()) flat.push_back(' ');
            space = true;
            continue;
        }
        space = false;
        flat.push_back(c);
    }
    while (!flat.empty() && flat.back() == ' ') flat.pop_back();
    if (begin > 0) flat.insert(0, "\xe2\x80\xa6");   // …
    if (end < body.size()) flat += "\xe2\x80\xa6";
    return flat;
}

class Index {
  public:
    void add(Doc d) {
        lowered_.push_back(Lowered{fmtutil::to_lower(d.title),
                                   fmtutil::to_lower(d.preview),
                                   fmtutil::to_lower(d.body)});
        docs_.push_back(std::move(d));
    }

    std::size_t size() const { return docs_.size(); }

    // Read-back and replacement, for a corpus that is assembled over several
    // frames instead of all at once (session_corpus.h). set_body re-lowers the
    // one document it touches; nothing else in the index moves, so a query
    // running against a half-deepened corpus is answering over exactly the
    // documents that have arrived.
    const std::string& id_at(std::size_t i) const { return docs_[i].id; }
    Depth depth_at(std::size_t i) const { return docs_[i].depth; }
    std::size_t body_size_at(std::size_t i) const {
        return docs_[i].body.size();
    }
    void set_body(std::size_t i, std::string body, Depth d) {
        lowered_[i].body = fmtutil::to_lower(body);
        docs_[i].body = std::move(body);
        docs_[i].depth = d;
    }

    Coverage coverage() const {
        Coverage c;
        c.threads = docs_.size();
        for (const auto& d : docs_) {
            if (d.depth == Depth::Full) ++c.full;
            else if (d.depth == Depth::Windowed) ++c.windowed;
        }
        return c;
    }

    // Threads matching `q`, in the order they were added. A relevance score
    // over two fields would be a ranking nobody can predict, so the order is
    // recency — and that is now a fact rather than a hope. This comment used
    // to say "the caller adds them newest-first", and the caller iterated
    // app.sessions unmodified: MockClient::list_sessions sorts, HttpClient and
    // AgentcloudClient push rows in WIRE order, and the sidebar sorts its own
    // copy precisely because it cannot trust the incoming order
    // (docs/SEARCH.md S7). CorpusBuilder::begin sorts by updated_at
    // descending, ties broken by id, before anything is added.
    //
    // At most `max_hits`; an empty query matches nothing, because "everything"
    // is not a search result.
    std::vector<Hit> query(const std::string& q, std::size_t max_hits) const {
        std::vector<Hit> out;
        const std::string needle = fmtutil::to_lower(q);
        if (needle.empty() || max_hits == 0) return out;

        for (std::size_t i = 0; i < docs_.size(); ++i) {
            const Doc& d = docs_[i];
            const Lowered& lo = lowered_[i];
            Hit h;
            h.id = d.id;
            h.title = d.title;
            h.partial = d.depth == Depth::TitleOnly;
            h.windowed = d.depth == Depth::Windowed;

            const std::size_t inBody = lo.body.find(needle);
            if (inBody != std::string::npos) {
                h.in_body = true;
                h.snippet = snippet_around(d.body, inBody, needle.size());
            } else if (lo.title.find(needle) != std::string::npos) {
                // A title hit still shows the preview: the row is more use
                // with a line of the conversation under it than without.
                h.snippet = d.preview;
            } else {
                const std::size_t inPrev = lo.preview.find(needle);
                if (inPrev == std::string::npos) continue;
                h.snippet = snippet_around(d.preview, inPrev, needle.size());
            }
            out.push_back(std::move(h));
            if (out.size() >= max_hits) break;
        }
        return out;
    }

  private:
    struct Lowered {
        std::string title, preview, body;
    };
    std::vector<Doc> docs_;
    std::vector<Lowered> lowered_;
};

// The sentence the panel shows under the results. It is not decoration: it is
// the difference between "no results" meaning "you never said that" and it
// meaning "we could not read most of your history".
inline std::string coverage_note(const Coverage& c) {
    if (c.threads == 0) return "Nothing to search yet";
    if (c.complete())
        return "Full text for all " + std::to_string(c.threads) + " threads";
    // Short on purpose: a line nobody can read across the panel is the same
    // as not admitting anything. Three clauses at the very worst, and the two
    // optional ones only appear when the number in front of them is not zero.
    std::string note = "Full text for " + std::to_string(c.full) + " of " +
                       std::to_string(c.threads) + " threads";
    if (c.windowed > 0)
        note += "; " + std::to_string(c.windowed) +
                " to their newest messages only";
    if (c.shallow() > 0) note += "; the rest by title and preview only";
    return note;
}

}  // namespace hanabi::search
