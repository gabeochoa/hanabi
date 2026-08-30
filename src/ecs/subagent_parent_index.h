#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "../api/types.h"

namespace ecs::model {

class SubagentParentIndex {
   public:
    bool update(std::uint64_t sessionRevision, std::uint64_t subagentRevision,
                const std::vector<api::SessionSummary>& sessions,
                const std::vector<api::SessionSummary>& subagents) {
        if (sessionRevision_ == sessionRevision &&
            subagentRevision_ == subagentRevision)
            return false;

        entries_.clear();
        entries_.reserve(sessions.size() + subagents.size());
        std::size_t precedence = 0;
        for (const auto& summary : sessions)
            entries_.push_back({summary.id, &summary, precedence++});
        for (const auto& summary : subagents)
            entries_.push_back({summary.id, &summary, precedence++});
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) {
                      return a.id == b.id ? a.precedence < b.precedence
                                          : a.id < b.id;
                  });
        sessionRevision_ = sessionRevision;
        subagentRevision_ = subagentRevision;
        ++rebuilds_;
        return true;
    }

    const api::SessionSummary* find(std::string_view id) const {
        const auto it =
            std::lower_bound(entries_.begin(), entries_.end(), id,
                             [](const Entry& entry, std::string_view value) {
                                 return entry.id < value;
                             });
        return it != entries_.end() && it->id == id ? it->summary : nullptr;
    }

    std::size_t rebuilds() const { return rebuilds_; }

   private:
    struct Entry {
        std::string_view id;
        const api::SessionSummary* summary = nullptr;
        std::size_t precedence = 0;
    };

    std::vector<Entry> entries_;
    std::uint64_t sessionRevision_ = 0;
    std::uint64_t subagentRevision_ = 0;
    std::size_t rebuilds_ = 0;
};

}  // namespace ecs::model
