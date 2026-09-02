#pragma once

// ---------------------------------------------------------------------------
// The sidebar's member lists: one pass over the catalog, and not even that
// once the catalog stops moving.
//
// `render_folder` used to collect its own members, so the panel walked
// `app.sessions` end to end once per folder, once for the Recent catch-all,
// and once more to discover which folders exist -- (F+2) full traversals a
// frame to draw the ~29 rows a viewport holds. Measured on boulder-KF74T3NW36
// at a 2020-session catalog with the two-folder fixture: 8,080 session visits
// a frame, and both folders are COLLAPSED by default, so two of those
// traversals existed to produce a number for a header.
//
// The disk-cache probe was NOT one of the costs: `content_matches` sat behind
// `match &&`, so a session was probed in its own group's pass and nowhere
// else -- once a frame then, once a frame now. This claimed otherwise until
// the review of this change read the removed lines.
//
// The traversals are identical -- same catalog, same predicates, same order --
// and differ only in which bucket a session lands in. So this walks once and
// files each session where it belongs.
//
// AND THEN IT KEEPS THE ANSWER. With no query the buckets are a pure function
// of the catalog, and the catalog already publishes a revision:
// `AppComponent::sessionCatalogRevision`, bumped by the only methods that can
// mutate `sessions` (replace_sessions, apply_renamed_title, apply_starred,
// apply_archived, apply_muted). That is not a new invariant to maintain --
// SubagentParentIndex has depended on exactly this counter since
// perf/subagent-index, and it is also what makes the cached `SessionSummary*`
// safe: a reallocation can only come from a mutation, and a mutation bumps the
// revision, so a stale pointer is never reachable.
//
// A QUERY ALWAYS REBUILDS, deliberately. With a query the answer also depends
// on `disk_cache::content_matches`, and the disk cache publishes no revision,
// so a memo keyed on the catalog would go on answering after a transcript
// landed. Searching still gets the one-pass win: one traversal instead of
// F+2.
//
// Two invariants the per-folder scan had and this keeps. Discovery is NOT
// filtered: a folder is listed if it holds any non-archived session whatever
// the query says, and a folder whose members all fail the filter renders
// nothing because its bucket is empty -- which is what the old empty-list
// early return did. Members keep catalog order, so the partial_sort that
// follows sees the sequence it saw before; the caller sorts a copy, because a
// kept answer that got sorted in place would be re-sorted from the wrong
// starting order on the next frame.
//
// tests/unit/test_sidebar_buckets.cpp holds this against a restatement of the
// old per-folder scan over a corpus of catalogs, queries and flags; the two
// must agree pointer for pointer, and it drives the revision contract
// (including the stale-answer bug the query rule exists to prevent).
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../api/types.h"
#include "../util/format.h"
#include "../util/prof.h"
#include "thread_model.h"

namespace ecs::model {

// A folder is "named" -- rendered as its own section, excluded from the Recent
// catch-all -- iff it is a real, non-empty folder value from the API.
// Unfoldered sessions (folder == "") fall through to the catch-all, and so do
// sessions filed under the catch-all's own key: "recent" is that key, not a
// folder anybody filed a thread into.
inline bool is_named_folder(const std::string& folder) {
    return !folder.empty() && folder != "recent";
}

// Kept deliberately small and conservative so it cannot over-match a real
// conversation title: a session is "automated" if its title starts with
// "Schedule:" OR ends with "-tick". Structural naming conventions of scheduled
// jobs, not content words. No company/endpoint/product strings. If this ever
// over-matches, tighten it rather than broadening it.
inline bool is_automated_title(const std::string& title) {
    if (title.rfind("Schedule:", 0) == 0) return true;
    const std::string suf = "-tick";
    return title.size() >= suf.size() &&
           title.compare(title.size() - suf.size(), suf.size(), suf) == 0;
}

inline bool title_matches(const std::string& title, const std::string& q) {
    if (q.empty()) return true;
    return fmtutil::contains_lower(title, q);
}

class SidebarBuckets {
   public:
    // `contentMatch(id, q)` is the disk-cache content search, injected so this
    // stays graphics- and IO-free for the unit test. It is consulted only when
    // there is a query and the title did not match, which is the order the
    // per-folder scan used. Returns true when it walked the catalog.
    template <class ContentMatch>
    bool rebuild(std::uint64_t catalogRevision,
                 const std::vector<api::SessionSummary>& sessions,
                 const std::string& q, bool hideAutomated,
                 ContentMatch&& contentMatch) {
        if (valid_ && q.empty() && query_.empty() &&
            revision_ == catalogRevision && hideAutomated_ == hideAutomated) {
            hanabi::prof::tick("sidebar.scan_reuse");
            return false;
        }
        hanabi::prof::Scope _pcollect("sidebar.collect");
        hanabi::prof::tick("sidebar.scan_rebuild");
        hanabi::prof::tick("sidebar.scan_visits", sessions.size());
        // Visits made with a LIVE QUERY, which is the only kind that pays the
        // per-title filter. The count above cannot say that: an empty query
        // still rebuilds whenever the revision or the automated flag moves,
        // so a gate flooring `scan_visits` would pass over a search field
        // that had stopped being typed into. alloc_gate's search2000 arm
        // floors on this one.
        //
        // The label is kept under 22 characters on purpose. prof::tick indexes
        // an unordered_map<std::string, Entry> with a const char*, so a longer
        // label builds a heap-allocated key on EVERY tick -- an instrument
        // that moves the allocation count it exists to guard. At 25 characters
        // this arm read 1381.0 instead of 1380.1.
        if (!q.empty())
            hanabi::prof::tick("sidebar.query_visits", sessions.size());
        hanabi::prof::gauge("sidebar.catalog", sessions.size());
        revision_ = catalogRevision;
        query_ = q;
        hideAutomated_ = hideAutomated;
        valid_ = true;
        folders_.clear();
        recent_.clear();
        for (Bucket& b : buckets_) b.members.clear();

        for (const api::SessionSummary& s : sessions) {
            const bool archived = is_archived(s);
            const bool named = is_named_folder(s.folder);
            // Discovery runs BEFORE both filters and is resolved to an index
            // here, so a folder whose every member is filtered out is still
            // listed -- and so the push below never re-enters slot() while
            // holding a reference into the vector slot() can grow.
            std::size_t idx = 0;
            if (!archived && named) idx = slot(s.folder);
            if (archived) continue;
            if (hideAutomated && is_automated_title(s.title)) continue;
            if (!(title_matches(s.title, q) ||
                  (!q.empty() && contentMatch(s.id, q))))
                continue;
            if (named)
                buckets_[idx].members.push_back(&s);
            else
                recent_.push_back(&s);
        }
        prune();
        hanabi::prof::gauge("sidebar.folders", folders_.size());
        hanabi::prof::gauge("sidebar.buckets", buckets_.size());
        ++rebuilds_;
        return true;
    }

    const std::vector<std::string>& folders() const { return folders_; }

    const std::vector<const api::SessionSummary*>& members(
        const std::string& key) const {
        for (const Bucket& b : buckets_)
            if (b.key == key) return b.members;
        return empty_;
    }

    const std::vector<const api::SessionSummary*>& recent() const {
        return recent_;
    }

    std::size_t rebuilds() const { return rebuilds_; }

   private:
    struct Bucket {
        std::string key;
        std::vector<const api::SessionSummary*> members;
    };

    // Buckets outlive the frame so their vectors keep the capacity the catalog
    // earned; a folder that disappears stops being listed and its cleared
    // bucket costs one string compare.
    std::size_t slot(const std::string& key) {
        for (std::size_t i = 0; i < buckets_.size(); ++i)
            if (buckets_[i].key == key) {
                if (!listed(key)) folders_.push_back(key);
                return i;
            }
        buckets_.push_back(Bucket{key, {}});
        folders_.push_back(key);
        return buckets_.size() - 1;
    }

    // Drop the buckets for folders that no longer exist. Without this the
    // list is append-only and `slot()` -- a linear scan of it, run once per
    // named session -- grows with every folder key the process has ever seen.
    // A backend that files a thread under its workspace path (see
    // api/agentcloud_client.cpp) churns those keys on every refresh.
    void prune() {
        buckets_.erase(std::remove_if(buckets_.begin(), buckets_.end(),
                                      [this](const Bucket& b) {
                                          return !listed(b.key);
                                      }),
                       buckets_.end());
    }

    bool listed(const std::string& key) const {
        for (const std::string& f : folders_)
            if (f == key) return true;
        return false;
    }

    std::vector<Bucket> buckets_;
    std::vector<std::string> folders_;
    std::vector<const api::SessionSummary*> recent_;
    std::vector<const api::SessionSummary*> empty_;
    std::string query_;
    std::uint64_t revision_ = 0;
    std::size_t rebuilds_ = 0;
    bool hideAutomated_ = false;
    bool valid_ = false;
};

}  // namespace ecs::model
