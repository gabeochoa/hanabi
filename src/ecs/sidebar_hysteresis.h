#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hanabi::sidebar_hysteresis {

inline constexpr std::int64_t kWindowSeconds = 60;

struct Memory {
    std::unordered_map<std::string, std::size_t> rank;
    std::uint64_t reorders = 0;
};

inline bool recently_active(std::int64_t updatedAt, std::int64_t now) {
    if (updatedAt <= 0) return false;
    const std::int64_t age = now >= updatedAt ? now - updatedAt : updatedAt - now;
    return age < kWindowSeconds;
}

template <typename Row, typename IdOf, typename UpdatedOf>
bool apply(std::vector<Row>& rows, Memory& memory, std::int64_t now, IdOf&& idOf, UpdatedOf&& updatedOf) {
    const std::size_t n = rows.size();
    std::vector<Row> out;
    out.reserve(n);
    std::vector<bool> placed(n, false);
    std::vector<std::pair<std::size_t, std::size_t>> pinned;
    for (std::size_t i = 0; i < n; ++i) {
        if (!recently_active(updatedOf(rows[i]), now)) continue;
        const auto rank = memory.rank.find(idOf(rows[i]));
        if (rank == memory.rank.end()) continue;
        if (rank->second >= n || rank->second >= i) continue;
        pinned.emplace_back(rank->second, i);
    }
    std::sort(pinned.begin(), pinned.end());
    std::vector<std::size_t> slotOf(n, n);
    std::size_t nextSlot = 0;
    for (const auto& [slot, i] : pinned) {
        std::size_t s = std::max(slot, nextSlot);
        while (s < n && slotOf[s] != n) ++s;
        if (s >= n) break;
        slotOf[s] = i;
        placed[i] = true;
        nextSlot = s + 1;
    }
    std::size_t cursor = 0;
    for (std::size_t s = 0; s < n; ++s) {
        if (slotOf[s] != n) {
            out.push_back(rows[slotOf[s]]);
            continue;
        }
        while (cursor < n && placed[cursor]) ++cursor;
        if (cursor >= n) break;
        out.push_back(rows[cursor]);
        placed[cursor] = true;
    }

    bool changed = out.size() != memory.rank.size();
    std::unordered_map<std::string, std::size_t> next;
    next.reserve(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::string id = idOf(out[i]);
        next.emplace(id, i);
        if (!changed) {
            const auto was = memory.rank.find(id);
            changed = was == memory.rank.end() || was->second != i;
        }
    }
    rows = std::move(out);
    memory.rank = std::move(next);
    if (changed) ++memory.reorders;
    return changed;
}

}
