#pragma once

// ---------------------------------------------------------------------------
// The texture cache's POLICY, with no texture in it.
//
// This is the LRU that src/ui/inline_image.h runs over decoded images, lifted
// out of it so the policy can be asserted instead of described. It was two
// paragraphs of comment and no test, in a header that cannot be linked without
// a GPU: every claim in those paragraphs -- that the budget is respected, that
// a frame never evicts what it is about to draw, that a working set larger
// than the budget is kept rather than thrashed -- was unfalsifiable.
//
// It knows nothing about textures. `bytes` is whatever the owner says an entry
// costs, and eviction is handed back through a callback, so the owner does the
// unloading and this file stays free of afterhours, sokol and Metal.
//
// THE THREE RULES, and why each exists.
//
//   1. A BYTE BUDGET, not an entry count. The whole problem is that entries
//      differ by four orders of magnitude: a 64x64 icon is 21 KB and a Retina
//      screen grab is 31 MB.
//
//   2. A PROTECTION WINDOW, and it matters more than the budget. An entry
//      touched within the last `protectRecent` accesses is never evicted. One
//      frame draws at most a handful of images, so this guarantees that
//      whatever is on screen right now survives the insert of something new.
//      Without it a working set larger than the budget evicts what it is about
//      to draw and re-decodes a PNG every frame, which is far worse than the
//      memory it saves.
//
//   3. AN ENTRY CAP, so a process handed ten thousand bad paths does not
//      remember all of them. A failed load costs a map entry and no bytes, and
//      the byte budget therefore cannot bound it.
//
// The counters are here because a byte total cannot show thrashing: a cache
// evicting and reloading the same entry every frame reads as a perfectly flat
// `bytes()` with a climbing `inserts()`. That is the failure mode rule 2
// exists to prevent, so it is the one the instrument has to be able to see.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

namespace hanabi::texbudget {

// The app's own settings, here rather than beside the cache so a test can
// assert against the REAL numbers instead of a copy of them that can drift.

// GPU bytes held at once. Thirty-two megabytes is roughly one Retina screen
// grab plus change; the protection window means a working set larger than
// this is kept anyway rather than thrashed.
inline constexpr std::size_t kDefaultMaxBytes = 32u * 1024 * 1024;

// Entries protected from eviction by recency of ACCESS, not of insertion. A
// frame draws at most the composer's five chips plus the inline images in
// view, so sixteen is comfortably a whole frame's working set.
inline constexpr std::size_t kDefaultProtectRecent = 16;

// Distinct keys remembered at all, including the ones that failed to load
// (those cost a map entry and no bytes, so the byte budget cannot bound them).
inline constexpr std::size_t kDefaultMaxEntries = 512;

class Budget {
public:
    Budget(std::size_t maxBytes, std::size_t protectRecent,
           std::size_t maxEntries)
        : maxBytes_(maxBytes),
          protectRecent_(protectRecent),
          maxEntries_(maxEntries) {}

    // Move `key` to the front of the access order. Safe on a key that is not
    // held: the order list and the entry map are allowed to disagree, because
    // the owner may know about an entry this does not.
    void touch(const std::string& key) {
        auto p = pos_.find(key);
        if (p != pos_.end()) order_.erase(p->second);
        order_.push_front(key);
        pos_[key] = order_.begin();
        ++clock_;
        auto e = entries_.find(key);
        if (e != entries_.end()) e->second.stamp = clock_;
    }

    void insert(const std::string& key, std::size_t bytes) {
        auto e = entries_.find(key);
        if (e != entries_.end()) {
            bytes_ -= e->second.bytes;
            e->second.bytes = bytes;
        } else {
            entries_[key] = Entry{bytes, 0};
        }
        bytes_ += bytes;
        ++inserts_;
        touch(key);
    }

    // Drop least-recently-used entries until the budget is met, handing each
    // victim's key and bytes to `onEvict` so the owner can release whatever it
    // is holding. Returns the number dropped.
    template <typename OnEvict>
    std::size_t trim(OnEvict&& onEvict) {
        std::size_t dropped = 0;
        while (over_budget()) {
            const std::string* victim = oldest_evictable();
            if (victim == nullptr) break;  // everything left is protected
            const std::string key = *victim;
            const std::size_t bytes = entries_[key].bytes;
            bytes_ -= bytes;
            entries_.erase(key);
            auto p = pos_.find(key);
            if (p != pos_.end()) {
                order_.erase(p->second);
                pos_.erase(p);
            }
            ++evictions_;
            ++dropped;
            onEvict(key, bytes);
        }
        return dropped;
    }

    bool holds(const std::string& key) const {
        return entries_.find(key) != entries_.end();
    }

    std::size_t bytes_of(const std::string& key) const {
        auto e = entries_.find(key);
        return e == entries_.end() ? 0u : e->second.bytes;
    }

    std::size_t bytes() const { return bytes_; }
    std::size_t size() const { return entries_.size(); }
    std::size_t max_bytes() const { return maxBytes_; }
    std::uint64_t inserts() const { return inserts_; }
    std::uint64_t evictions() const { return evictions_; }

private:
    struct Entry {
        std::size_t bytes = 0;
        std::uint64_t stamp = 0;  // the access clock at the last touch
    };

    bool over_budget() const {
        return bytes_ > maxBytes_ || entries_.size() > maxEntries_;
    }

    // The oldest entry outside the protection window, or null when every
    // remaining entry is inside it. The order list is in access order, so the
    // first protected entry found walking back from the oldest means there is
    // nothing further that may be dropped.
    const std::string* oldest_evictable() {
        for (auto rit = order_.rbegin(); rit != order_.rend(); ++rit) {
            auto e = entries_.find(*rit);
            if (e == entries_.end()) continue;  // order knows a key we do not
            if (clock_ - e->second.stamp < protectRecent_) return nullptr;
            return &*rit;
        }
        return nullptr;
    }

    std::size_t maxBytes_;
    std::size_t protectRecent_;
    std::size_t maxEntries_;

    std::unordered_map<std::string, Entry> entries_;
    std::list<std::string> order_;  // MRU front ... LRU back
    std::unordered_map<std::string, std::list<std::string>::iterator> pos_;
    std::size_t bytes_ = 0;
    std::uint64_t clock_ = 0;
    std::uint64_t inserts_ = 0;
    std::uint64_t evictions_ = 0;
};

}  // namespace hanabi::texbudget
