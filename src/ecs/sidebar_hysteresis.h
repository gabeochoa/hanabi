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
    std::unordered_map<std::string, std::int64_t> heldSince;
};

inline bool recently_active(std::int64_t updatedAt, std::int64_t now) {
    if (updatedAt <= 0) return false;
    const std::int64_t age = now >= updatedAt ? now - updatedAt : updatedAt - now;
    return age < kWindowSeconds;
}

template <typename Row, typename IdOf, typename UpdatedOf>
void apply(std::vector<Row>& rows, Memory& memory, std::int64_t now, IdOf&& idOf, UpdatedOf&& updatedOf) {
    const std::size_t n = rows.size();
    std::vector<Row> out;
    out.reserve(n);
    std::vector<bool> placed(n, false);
    std::vector<std::pair<std::size_t, std::size_t>> pinned;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string id = idOf(rows[i]);
        if (!recently_active(updatedOf(rows[i]), now)) continue;
        const auto rank = memory.rank.find(id);
        const auto since = memory.heldSince.find(id);
        if (rank == memory.rank.end() || since == memory.heldSince.end()) continue;
        if (now - since->second >= kWindowSeconds) continue;
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
    rows = std::move(out);

    Memory next;
    next.rank.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::string id = idOf(rows[i]);
        next.rank.emplace(id, i);
        if (recently_active(updatedOf(rows[i]), now)) {
            const auto since = memory.heldSince.find(id);
            next.heldSince.emplace(id, since != memory.heldSince.end() ? since->second : now);
        }
    }
    memory = std::move(next);
}

}
