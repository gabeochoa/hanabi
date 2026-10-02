#pragma once

// One conversation is ONE row (the reference's 0.8.6 fix: "one
// conversation cannot appear twice in the sidebar or in search").
//
// Every surface that lists threads -- the sidebar's buckets, Home, the session
// search, the palette -- reads the one catalogue, so the catalogue is where
// the rule lives: a replacement list that names an id twice keeps one row for
// it, the freshest (newest update; the first on a tie), in the position the
// id first took. Nothing on the wire promises uniqueness: a list assembled
// from pages, a cache merged with a live reply, or a server that lists a
// session under two owners all produce the same symptom, a thread drawn twice
// and starred, archived or renamed in only one of its copies.

#include <string>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace api::catalog {

// Returns how many duplicate rows were dropped.
inline std::size_t keep_one_row_per_id(std::vector<SessionSummary>& rows) {
    std::unordered_map<std::string, std::size_t> firstAt;
    firstAt.reserve(rows.size());
    std::vector<SessionSummary> out;
    out.reserve(rows.size());
    std::size_t dropped = 0;
    for (auto& row : rows) {
        const auto [it, fresh] = firstAt.emplace(row.id, out.size());
        if (fresh) {
            out.push_back(std::move(row));
            continue;
        }
        ++dropped;
        SessionSummary& kept = out[it->second];
        if (row.updated_at > kept.updated_at) kept = std::move(row);
    }
    rows = std::move(out);
    return dropped;
}

}  // namespace api::catalog
