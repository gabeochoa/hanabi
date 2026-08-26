#pragma once

// ---------------------------------------------------------------------------
// Assembling the cross-session corpus, a bounded slice at a time.
//
// THE PROBLEM. Cmd+Shift+F built its whole index on the frame the panel
// opened: api::disk_cache::load_transcript — a full nlohmann parse — once per
// session not already in the in-memory LRU, synchronously, on the UI thread.
// No cap, no budget, no thread, no progress. The repo's own figure for a
// merely STAT-based walk of 2000 cache files is 5.9 ms
// (api/disk_cache.cpp); this is a parse per file, and it grows with the
// user's history forever. And "once per opening" is undercut by close() and
// the chord both resetting the flag, so it is once per open, every open.
// docs/SEARCH.md S5.
//
// THE SHAPE OF THE FIX. Not a cap on how much is searched — that would make
// Cmd+Shift+F permanently blind to old threads — and not a thread, because
// api::disk_cache has no ownership story for a reader racing a save and the
// panel's whole corpus can change under one. Instead the reads are SPREAD: the
// panel opens having done none of them, and each frame it is open reads a
// fixed few more. Coverage climbs while you are still typing the query, and
// coverage_note is already the sentence that says how far it has got, so the
// partial state is not a state the UI has to learn to render.
//
// A frame is bounded by a COUNT of transcripts, not by a clock. A time budget
// would read a different number of files on a loaded machine than on a quiet
// one, which is a UI whose behaviour depends on what else the box is doing,
// and it cannot be gated: a count is the same number every run.
//
// Pure: no graphics, no app state, no I/O. The caller supplies the rows and a
// loader; this decides what gets read, in what order, and how much per frame.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "session_index.h"

namespace hanabi::search {

// Transcripts read from disk per frame the panel is open.
//
// The number is a slice of one frame's work, chosen from the measured cost of
// reading one realistic cached thread. tools/bench_search_index.cpp, 2000
// threads x 40 messages (19 MB of cache), CLOCK_THREAD_CPUTIME_ID on
// gabeochoa-mac:
//
//     whole cache, one frame     370.5 ms   (0.185 ms/thread)  <- before
//     open the panel               0.2 ms   (0 disk reads)
//     one deepening slice          1.1 ms   (8 disk reads)
//     to full coverage           324.3 ms over 250 frames
//
// So eight is about 1.1 ms — under a tenth of a 16 ms frame — and the total
// work is unchanged, just spread over the frames somebody spends typing
// instead of stacked on the one that opens the panel.
inline constexpr std::size_t kDeepenPerFrame = 8;

// One thread as the builder sees it: the list row, plus whatever body this
// machine already has in memory for it.
struct Row {
    std::string id;
    std::string title;
    std::string preview;
    std::string held;             // flattened in-memory body; empty = none
    bool has_held = false;        // distinguishes "no copy" from "empty one"
    bool held_is_tail = false;    // that copy is only the end of the thread
    std::int64_t updated_at = 0;  // deepening order: newest thread first
};

// A transcript the loader found on disk, flattened.
struct Loaded {
    std::string body;
    bool windowed = false;  // the file itself is only part of the thread
};

class CorpusBuilder {
  public:
    // Seed the index from the rows alone. Costs NO disk I/O: every thread
    // enters as TitleOnly, except the ones whose body is already in memory.
    void begin(std::vector<Row> rows) {
        index_ = Index{};
        pending_.clear();
        cursor_ = 0;
        pending_.reserve(rows.size());
        // Newest first, ties broken by id so the order is total — a partial
        // corpus that reshuffles between frames would move results under the
        // cursor while somebody is arrowing through them.
        std::vector<std::size_t> order(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](std::size_t a, std::size_t b) {
                      if (rows[a].updated_at != rows[b].updated_at)
                          return rows[a].updated_at > rows[b].updated_at;
                      return rows[a].id < rows[b].id;
                  });
        for (std::size_t oi = 0; oi < order.size(); ++oi) {
            Row& r = rows[order[oi]];
            Doc d;
            d.id = r.id;
            d.title = std::move(r.title);
            d.preview = std::move(r.preview);
            if (r.has_held && !r.held_is_tail) {
                d.body = std::move(r.held);
                d.depth = Depth::Full;
            } else {
                // Either nothing in memory, or a tail of one. The tail is
                // still worth having while the disk copy is unread — a
                // Windowed hit is a real hit that says what it is — but the
                // thread stays in the queue, because the disk copy may hold
                // more of it.
                if (r.has_held) {
                    d.body = std::move(r.held);
                    d.depth = Depth::Windowed;
                }
                pending_.push_back(index_.size());
            }
            index_.add(std::move(d));
        }
    }

    std::size_t pending() const { return pending_.size() - cursor_; }
    bool complete() const { return cursor_ >= pending_.size(); }
    const Index& index() const { return index_; }

    // Read at most `budget` more transcripts. `load(id)` returns nullopt when
    // this machine has no copy of that thread. Returns how many threads were
    // asked for — which is what a test asserts against, because "the frame did
    // a bounded amount of work" is a statement about the calls, not the clock.
    //
    // A thread whose disk copy is missing, or holds no more than the tail
    // already indexed, keeps what it had: this only ever deepens.
    template <typename Load>
    std::size_t deepen(std::size_t budget, Load&& load) {
        std::size_t did = 0;
        while (did < budget && cursor_ < pending_.size()) {
            const std::size_t di = pending_[cursor_++];
            ++did;
            std::optional<Loaded> got = load(index_.id_at(di));
            if (!got.has_value()) continue;
            if (index_.depth_at(di) != Depth::TitleOnly &&
                got->body.size() <= index_.body_size_at(di))
                continue;
            index_.set_body(di, std::move(got->body),
                            got->windowed ? Depth::Windowed : Depth::Full);
        }
        return did;
    }

  private:
    Index index_;
    std::vector<std::size_t> pending_;
    std::size_t cursor_ = 0;
};

}  // namespace hanabi::search
