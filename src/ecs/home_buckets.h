#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../api/types.h"
#include "../util/prof.h"
#include "thread_model.h"

namespace ecs::model {

class HomeBuckets {
   public:
    explicit HomeBuckets(std::size_t recentLimit) : recentLimit_(recentLimit) {}

    void update(std::uint64_t catalogRevision,
                const std::vector<api::SessionSummary>& sessions) {
        hanabi::prof::tick("home.scan_reuse", 0);
        if (valid_ && revision_ == catalogRevision) {
            hanabi::prof::tick("home.scan_reuse");
            return;
        }

        hanabi::prof::Scope collect("home.collect");
        hanabi::prof::tick("home.scan_rebuild");
        hanabi::prof::tick("home.scan_visits", sessions.size());
        hanabi::prof::gauge("home.catalog", sessions.size());

        revision_ = catalogRevision;
        valid_ = true;
        waiting_.clear();
        finished_.clear();
        running_.clear();
        recent_.clear();

        for (const api::SessionSummary& session : sessions) {
            if (session.state == api::ThreadState::Attention) {
                if (session.tag == api::ThreadTag::Blocked)
                    waiting_.push_back(&session);
                else
                    finished_.push_back(&session);
            }
            if (session.state == api::ThreadState::Running)
                running_.push_back(&session);
            if (!is_archived(session) &&
                session.state != api::ThreadState::Attention &&
                session.state != api::ThreadState::Running)
                recent_.push_back(&session);
        }

        const auto byRecency = [](const api::SessionSummary* a,
                                  const api::SessionSummary* b) {
            return a->updated_at > b->updated_at;
        };
        const std::size_t keep = std::min(recent_.size(), recentLimit_);
        std::partial_sort(recent_.begin(), recent_.begin() + keep,
                          recent_.end(), byRecency);
        ++rebuilds_;
    }

    const std::vector<const api::SessionSummary*>& waiting() const {
        return waiting_;
    }
    const std::vector<const api::SessionSummary*>& finished() const {
        return finished_;
    }
    const std::vector<const api::SessionSummary*>& running() const {
        return running_;
    }
    const std::vector<const api::SessionSummary*>& recent() const {
        return recent_;
    }
    std::size_t rebuilds() const { return rebuilds_; }

   private:
    std::vector<const api::SessionSummary*> waiting_;
    std::vector<const api::SessionSummary*> finished_;
    std::vector<const api::SessionSummary*> running_;
    std::vector<const api::SessionSummary*> recent_;
    std::uint64_t revision_ = 0;
    const std::size_t recentLimit_;
    std::size_t rebuilds_ = 0;
    bool valid_ = false;
};

}  // namespace ecs::model
