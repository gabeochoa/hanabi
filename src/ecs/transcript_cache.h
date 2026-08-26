#pragma once

// Pure, graphics-free LRU transcript cache (Phase X).
//
// Instant thread switching: keeps the transcript for the LAST 5 THREADS the
// user interacted with, each capped to its LAST 20 MESSAGES (most-recent), so
// re-opening a recently-viewed thread renders SYNCHRONOUSLY from memory with
// no async round-trip and no Loading flash. Least-recently-used threads past
// the cap of 5 are evicted, which is the ONLY growth point — memory is bounded
// at <= 5 x 20 messages regardless of how many threads the user cycles through.
//
// This lives in the APP layer, behind the api::Client abstraction: it caches
// api::Session values whatever backend produced them, so the mock and the
// generic http adapter benefit equally. Nothing here draws or touches a
// window / UIContext / graphics backend, so it is unit/e2e testable headlessly
// (mirrors the thread_model.h / tab_model.h pattern the e2e tests use).
//
// Backend note: for a live backend you would revalidate-in-background and swap
// in fresh data if it changed. The mock is static, so its cache is
// authoritative (no revalidation needed). The seam is `stale()` / a future
// revalidation hook; we don't over-build it here.

#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

#include "../api/types.h"

namespace ecs::model {

// Bound constants (the RAM budget's only unbounded-ish growth point).
inline constexpr std::size_t kCacheMaxThreads = 5;
inline constexpr std::size_t kCacheMaxMessagesPerThread = 20;

// LRU cache of transcripts. Move-to-front on every access (touch/get/put) so
// the retained set is exactly "the last 5 threads you interacted with".
class TranscriptCache {
  public:
    // True if `id` is currently cached (does NOT change recency — for a pure
    // membership probe use this; use get() to fetch + mark most-recent).
    bool contains(const std::string& id) const {
        return map_.find(id) != map_.end();
    }

    // Fetch a cached transcript and mark it most-recently-used. Returns nullopt
    // on a miss (the caller then takes the async fetch path).
    std::optional<api::Session> get(const std::string& id) {
        auto it = map_.find(id);
        if (it == map_.end()) return std::nullopt;
        touch_locked(it);
        return it->second.session;  // copy out (small: <=20 msgs)
    }

    // Insert / update a transcript, capping to the last 20 messages, and mark
    // it most-recently-used. Evicts the LRU thread if inserting a new one would
    // exceed the 5-thread capacity.
    void put(api::Session session) {
        const std::string id = session.summary.id;
        const bool cut = cap_messages(session);

        auto it = map_.find(id);
        if (it != map_.end()) {
            it->second.session = std::move(session);
            it->second.truncated = cut;
            touch_locked(it);
            return;
        }
        // New entry: evict LRU if at capacity.
        if (map_.size() >= kCacheMaxThreads) evict_lru();
        order_.push_front(id);
        Entry e;
        e.session = std::move(session);
        e.truncated = cut;
        e.pos = order_.begin();
        map_.emplace(id, std::move(e));
    }

    // Was `id`'s copy CUT DOWN on the way in? A reader that treats what it
    // holds as the whole thread needs to know: the cross-session index called
    // a 20-message tail of a 400-message thread "full text" and said so in the
    // sentence whose whole job is admitting what it could not read
    // (docs/SEARCH.md S2). A size check cannot answer this — a thread with
    // exactly kCacheMaxMessagesPerThread messages is complete — so the answer
    // is recorded at the cut.
    bool truncated(const std::string& id) const {
        auto it = map_.find(id);
        return it != map_.end() && it->second.truncated;
    }

    // Read a cached transcript WITHOUT touching recency. Two readers are not
    // the user — the cross-session index walks every thread, and the sidebar's
    // snippets walk every matching row — and neither may reorder the LRU, or
    // searching once would evict what you were reading.
    const api::Session* peek(const std::string& id) const {
        auto it = map_.find(id);
        return it == map_.end() ? nullptr : &it->second.session;
    }

    // Mark an already-cached thread most-recently-used WITHOUT fetching (e.g.
    // re-focusing an open tab). No-op on a miss. Keeps "last 5 interacted with"
    // accurate on every interaction, not just fresh opens.
    void touch(const std::string& id) {
        auto it = map_.find(id);
        if (it != map_.end()) touch_locked(it);
    }

    std::size_t size() const { return map_.size(); }
    bool empty() const { return map_.empty(); }
    void clear() { map_.clear(); order_.clear(); }

    // Ordered ids, most-recently-used first (for tests / introspection).
    const std::list<std::string>& order() const { return order_; }

  private:
    struct Entry {
        api::Session session;
        bool truncated = false;  // messages were dropped to fit the cap
        std::list<std::string>::iterator pos;  // position in order_
    };

    // Returns whether anything was dropped.
    static bool cap_messages(api::Session& s) {
        auto& m = s.messages;
        if (m.size() <= kCacheMaxMessagesPerThread) return false;
        m.erase(m.begin(),
                m.end() - static_cast<long>(kCacheMaxMessagesPerThread));
        return true;
    }

    void touch_locked(std::unordered_map<std::string, Entry>::iterator it) {
        // Move the id to the front of the recency list.
        order_.erase(it->second.pos);
        order_.push_front(it->first);
        it->second.pos = order_.begin();
    }

    void evict_lru() {
        if (order_.empty()) return;
        const std::string& lru = order_.back();
        map_.erase(lru);
        order_.pop_back();
    }

    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> order_;  // MRU front ... LRU back
};

}  // namespace ecs::model
