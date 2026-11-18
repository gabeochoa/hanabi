#pragma once

// The threads THIS client created untitled, while the server may still be
// naming them (Knots kt-jko9; the reference's GeneratedTitleWait.swift at
// e8395d87eff5, spec 398 FR17). Pure: the clock is an argument, in
// milliseconds, so a test can place the boundary anywhere it likes.
//
// In that window a thread genuinely has no name, and drawing the untitled
// fallback is a label about to be replaced, so a surface that would draw a
// name draws the pending label instead. Pending is claimed only from
// positive evidence and is bounded: this client created the thread and sent
// no title, and the server's generation budget has not run out. A thread
// the server never names settles to the untitled fallback on its own.

#include <cstdint>
#include <map>
#include <string>

namespace api::generated_title_wait {

// The server's own budget (GENERATION_BUDGET, 15 s) plus the reference's
// slack for the commit and the frame's trip back.
inline constexpr int64_t kBudgetMs = (15 + 3) * 1000;

// The label a name's slot carries while a wait holds it. Hanabi draws text,
// not the reference's placeholder bar; the words are the reference's own
// accessibility label for that bar.
inline constexpr const char* kPendingLabel = "Naming this thread";

class Tracker {
   public:
    // Record that this client created `id` and sent no title with it.
    void note(const std::string& id, int64_t nowMs) {
        if (!id.empty()) started_[id] = nowMs;
    }

    // A name arrived for `id` from any source, so nothing is pending.
    void settle(const std::string& id) { started_.erase(id); }

    // Whether `id` was created recently enough that the server may still
    // be naming it. Past the budget the entry no longer answers true; it
    // is left in place (bounded by the number of untitled creates) rather
    // than pruned on a read.
    bool is_awaiting(const std::string& id, int64_t nowMs) const {
        const auto it = started_.find(id);
        return it != started_.end() && nowMs - it->second <= kBudgetMs;
    }

    // Whether a surface that would draw `name` draws the pending label
    // instead: the name in hand is blank, the placeholder, or the
    // untitled fallback the catalog parser substitutes for blank.
    bool is_pending(const std::string& id, const std::string& name,
                    int64_t nowMs) const {
        const bool blank = name.empty() || name == "(untitled)" ||
                           name == "agentcloud session";
        return blank && is_awaiting(id, nowMs);
    }

   private:
    std::map<std::string, int64_t> started_;
};

}  // namespace api::generated_title_wait
