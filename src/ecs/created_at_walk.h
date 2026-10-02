#pragma once

// Creation times for the sidebar's Oldest-first order (Puffin 0.8.9).
//
// The catalog row does not carry a creation time -- agentcloud's `list` reply
// has none, and the reference reads one off a separate catalog walk -- so the
// app learns it per session (api::Client::session_created_at) and keeps every
// answer: a creation time never changes, so a session is asked once, ever.
// This header is the pure part: which sessions to ask next, and how what is
// known lands on a catalog. The loader owns the asking; disk_cache owns the
// file.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../api/types.h"

namespace hanabi::created_at {

using Store = std::unordered_map<std::string, std::int64_t>;

// How many sessions one walk step asks about. Each answer is a socket attach
// against a real server, so a step is small and the next one starts when it
// lands; a catalog of a thousand threads is learned in the background while
// the rows not reached yet sit last.
inline constexpr std::size_t kBatch = 8;

// Lay what is known over a catalog. A row that already carries a time keeps
// it; the store never overwrites an answer the row came with.
inline void apply(const Store& known, std::vector<api::SessionSummary>& rows) {
    if (known.empty()) return;
    for (auto& s : rows) {
        if (s.created_at > 0) continue;
        const auto it = known.find(s.id);
        if (it != known.end()) s.created_at = it->second;
    }
}

// The next sessions to ask about, in catalog order (which is newest activity
// first, so the rows a reader is most likely to look at are learned first):
// no time on the row, none in the store, and not already refused this run.
inline std::vector<std::string> next_batch(
    const std::vector<api::SessionSummary>& rows, const Store& known,
    const std::unordered_set<std::string>& refused,
    std::size_t limit = kBatch) {
    std::vector<std::string> out;
    for (const auto& s : rows) {
        if (out.size() >= limit) break;
        if (s.id.empty() || s.created_at > 0) continue;
        if (known.count(s.id) != 0 || refused.count(s.id) != 0) continue;
        out.push_back(s.id);
    }
    return out;
}

}  // namespace hanabi::created_at
