#pragma once

// ---------------------------------------------------------------------------
// Per-thread pane state: the things the transcript remembers about a thread
// between frames, in ONE bounded place instead of five unbounded ones.
//
// WHAT WAS THERE. main_pane_system.h grew five function-local statics, each a
// map keyed by session id, each written on the first frame a thread is drawn
// and never erased:
//
//     static std::unordered_map<std::string, UnreadMark> s_unread;
//     static std::unordered_map<std::string, bool>  s_followMap;
//     static std::unordered_map<std::string, float> s_prevOffsetMap;
//     static std::unordered_map<std::string, float> s_lastScrollYMap;
//     static std::map<std::string, std::string>     replyDrafts;
//
// plus `AppComponent::composerHistory`, keyed identically. Each is individually
// defensible — the comments above them explain, correctly, why the state has to
// be per-session and why a single static would let split-view's two panes
// clobber each other. What none of them says is when an entry goes away, and
// the answer was never. A function-local static keyed by id and never pruned
// grows for the life of the process, one entry per thread ever opened, and
// nothing outside the function can even count them.
//
// WHY IT MATTERS LESS THAN IT LOOKS, AND STILL MATTERS. Measured on the ladder
// (util/mem_ladder.h): opening and closing threads one at a time retains about
// 950 bytes and 10 malloc blocks per thread, so a thousand threads is about a
// megabyte. That is not the freeze anyone reported. It is unbounded, it is
// invisible, and it is the class of thing that is only ever found by looking
// -- so it gets a bound and a number rather than a rewrite.
//
// WHAT THE BOUND COSTS. The store keeps the last kMaxThreads threads you
// touched, most-recently-used first, and drops the oldest past that. What a
// dropped thread forgets: its unread divider recomputes from the persisted
// last-read stamp (so the line comes back, it is not wrong, it just re-marks),
// its follow-the-bottom latch re-arms, and its scroll velocity restarts at
// zero for one frame. All three are recomputed within a frame of reopening
// and none of them is persisted today anyway.
//
// The one thing worth keeping is an unsent DRAFT, so eviction skips any entry
// holding one: the LRU walks back from the oldest until it finds an entry with
// an empty draft. A draft is only ever lost when kMaxThreads threads all have
// unsent text in them at once, which is a different app than this one.
//
// Pure and graphics-free, like transcript_cache.h and tab_model.h: no
// UIContext, no afterhours draw, so a unit test can drive it headlessly.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ui/minimap_scrub.h"
#include "follow_latch.h"
#include "transcript_item_index.h"

namespace ecs::model {

// The last N threads whose pane state is remembered. Sixty-four is well past
// any session's real working set (the tab strip tops out far below it) and
// small enough that the whole store is a few kilobytes.
inline constexpr std::size_t kMaxPaneStates = 64;

struct PaneState {
    // ---- The unread divider ------------------------------------------------
    // Computed ONCE per thread and held, because recomputing it every frame
    // deleted the line while the reader was looking at it.
    bool unreadComputed = false;
    std::int64_t unreadStamp = 0;  // the persisted stamp it was computed from
    std::size_t unreadSeen = 0;    // message count when it was computed
    std::uint64_t unreadRevision = 0;
    std::int64_t unreadLastCreatedAt = 0;
    int unreadFirst = -1;
    int unreadCount = 0;

    // ---- Follow-the-bottom ------------------------------------------------
    // The latch that decides whether this thread is pinned to its newest
    // message, and the previous-frame record it needs to tell a gesture from
    // the layout moving. Its defaults ARE the first-sight values the old maps
    // were seeded with on a miss: follow armed, no previous frame yet.
    // (follow_latch.h; the arithmetic is tested in test_follow_latch.cpp.)
    FollowMemory latch;

    // ---- Virtualiser scroll velocity --------------------------------------
    bool haveLastScrollY = false;  // a first sight has velocity 0, not -scrollY
    float lastScrollY = 0.0f;

    // ---- The minimap drag -------------------------------------------------
    // A press-drag-release along the rail outlives the frame it started in and
    // the rail is rebuilt every frame, so the gesture parks here. Per thread
    // rather than one global, for the same reason the follow-latch is: two
    // panes in split-view each have a rail and only one of them is being
    // dragged.
    hanabi::minimap::DragState minimapDrag;

    // ---- The composer -----------------------------------------------------
    // This thread's half-typed reply, and the Up/Down history walk over what
    // has been sent from this thread. In memory only: both die with the
    // process, which is the deliberate simplification in
    // docs/breakdown/composer.md.
    std::string replyDraft;
    std::vector<std::string> sent;   // oldest first
    std::size_t walkIndex = 0;       // steps back from the live draft
    std::string stashedDraft;        // the draft the walk started from

    std::string copiedMessageKey;
    std::chrono::steady_clock::time_point copiedMessageAt{};
    std::string retriedMessageKey;
    std::chrono::steady_clock::time_point retriedMessageAt{};
    std::string focusedMessageActionKey;

    bool worth_keeping() const { return !replyDraft.empty(); }
};

template <class Messages>
void update_unread(PaneState& state, const Messages& messages,
                   std::int64_t lastRead,
                   const TranscriptMutation& mutation) {
    const std::size_t size = messages.size();
    bool recompute = !state.unreadComputed || lastRead != state.unreadStamp ||
                     size < state.unreadSeen;
    const bool mutationChanged = mutation.revision != state.unreadRevision;
    if (mutationChanged && !recompute) {
        if (mutation.base_revision != state.unreadRevision ||
            mutation.kind == TranscriptMutationKind::Reset ||
            mutation.kind == TranscriptMutationKind::Prepend) {
            recompute = true;
        } else if (mutation.kind == TranscriptMutationKind::Update) {
            const bool onlyLast = mutation.count == 1 && size != 0 &&
                                  mutation.first + 1 == size;
            if (!onlyLast || messages.back().created_at !=
                                 state.unreadLastCreatedAt)
                recompute = true;
        }
    }

    if (recompute) {
        state.unreadComputed = true;
        state.unreadStamp = lastRead;
        state.unreadFirst = -1;
        state.unreadCount = 0;
        if (lastRead > 0) {
            for (std::size_t i = 0; i < size; ++i) {
                if (messages[i].created_at <= lastRead) continue;
                if (state.unreadFirst < 0)
                    state.unreadFirst = static_cast<int>(i);
                ++state.unreadCount;
            }
        }
        if (state.unreadFirst == 0 &&
            state.unreadCount == static_cast<int>(size))
            state.unreadFirst = -1;
    } else if (mutationChanged &&
               mutation.kind == TranscriptMutationKind::Append) {
        const std::size_t first = std::min(mutation.first, size);
        for (std::size_t i = first; i < size; ++i) {
            if (messages[i].created_at <= lastRead) continue;
            if (state.unreadFirst < 0)
                state.unreadFirst = static_cast<int>(i);
            ++state.unreadCount;
        }
    }

    state.unreadSeen = size;
    state.unreadRevision = mutation.revision;
    state.unreadLastCreatedAt = size == 0 ? 0 : messages.back().created_at;
}

// An LRU over PaneState, keyed by PANE AND session id.
//
// The key used to be the session id alone, and for one pane that was the same
// thing. It stopped being the same thing the moment a split could show ONE
// thread in TWO panes -- which is what splitting does by default. Sharing the
// entry then means the two panes share a follow-the-bottom latch (scroll one,
// the other jumps), a scroll velocity, a minimap drag (press in one rail, both
// scrub) and, worst, a DRAFT: type on the left, watch it appear on the right,
// send it once and lose it in both.
//
// So the key is `pane_key(index, id)`. What that costs: the two panes on one
// thread hold two entries instead of one, and the bound is a count of entries,
// so a split halves how many THREADS are remembered. kMaxPaneStates is 64 and
// the tab strip tops out far below it, so this is a bound nobody reaches.
//
// (The pseudo-ids -- the composer's "__kickoff__", the empty id -- go through
// the same helper, so a kickoff draft is per pane too. Two panes offering to
// start two different new tasks is right for the same reason.)
inline std::string pane_key(int paneIndex, const std::string& id) {
    std::string k;
    k.reserve(id.size() + 2);
    k.push_back(static_cast<char>('0' + (paneIndex & 1)));
    k.push_back('\x1f');
    k.append(id);
    return k;
}

class PaneStateStore {
  public:
    // This thread's state, creating it if new and marking it most-recently
    // used. The reference is stable until the next call that inserts.
    PaneState& touch(const std::string& id) {
        auto it = map_.find(id);
        if (it != map_.end()) {
            promote(it);
            return it->second.state;
        }
        if (map_.size() >= kMaxPaneStates) evict_lru();
        order_.push_front(id);
        Entry e;
        e.pos = order_.begin();
        auto [ins, ok] = map_.emplace(id, std::move(e));
        (void)ok;
        return ins->second.state;
    }

    // Read without disturbing recency (tests, introspection).
    const PaneState* peek(const std::string& id) const {
        auto it = map_.find(id);
        return it == map_.end() ? nullptr : &it->second.state;
    }

    void forget(const std::string& id) {
        auto it = map_.find(id);
        if (it == map_.end()) return;
        order_.erase(it->second.pos);
        map_.erase(it);
    }

    std::size_t size() const { return map_.size(); }
    void clear() {
        map_.clear();
        order_.clear();
    }

    // Ids, most-recently-used first (tests / introspection).
    const std::list<std::string>& order() const { return order_; }

    // How many held entries carry an unsent draft — the ones eviction refuses
    // to take. If this ever reaches kMaxPaneStates the store stops bounding,
    // which is the honest failure mode and worth being able to assert on.
    std::size_t drafts() const {
        std::size_t n = 0;
        for (const auto& kv : map_)
            if (kv.second.state.worth_keeping()) ++n;
        return n;
    }

  private:
    struct Entry {
        PaneState state;
        std::list<std::string>::iterator pos;
    };

    void promote(std::unordered_map<std::string, Entry>::iterator it) {
        order_.erase(it->second.pos);
        order_.push_front(it->first);
        it->second.pos = order_.begin();
    }

    // Drop the oldest entry that is not holding an unsent draft. Walking back
    // rather than taking order_.back() unconditionally is the whole difference
    // between a bound that costs nothing and one that eats your typing.
    void evict_lru() {
        for (auto rit = order_.rbegin(); rit != order_.rend(); ++rit) {
            auto found = map_.find(*rit);
            if (found == map_.end()) continue;
            if (found->second.state.worth_keeping()) continue;
            order_.erase(std::next(rit).base());
            map_.erase(found);
            return;
        }
        // Every entry holds a draft. Keep them all: a bound is not worth
        // silently deleting what someone typed.
    }

    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> order_;  // MRU front ... LRU back
};

// The one instance. A singleton for the same reason the statics it replaces
// were statics: the transcript is rebuilt from scratch every frame, so the
// state that has to survive a frame cannot live in the widget tree.
inline PaneStateStore& pane_states() {
    static PaneStateStore store;
    return store;
}

}  // namespace ecs::model
