#pragma once

// Per-message render-cost cache for the transcript (Phase: chat-overhaul perf).
//
// The transcript is immediate-mode: every frame it rebuilt EVERY message —
// re-running redact_secrets / strip_inline_md / first_n_lines and re-measuring
// wrapped height for all of them. On a long thread (120+ messages) that
// per-frame string+measure work dominated the ~145ms/frame cost that made
// scrolling choppy.
//
// This memoizes the two expensive, PURE, per-message results keyed by
// (message id + variant, wrap width): the display body (after redaction +
// md-strip) and its wrapped-line count / measured height. A static transcript
// is thus measured ONCE, not every frame. Keyed by width too, so a pane resize
// correctly re-measures.
//
// ---------------------------------------------------------------------------
// IT HOLDS MORE THAN ONE THREAD, AND THIS IS WHY.
//
// It used to hold exactly one: `reset_for_thread(id)` cleared the whole map
// whenever the id it was handed differed from the id it was handed last. That
// is a fine bound and it was a correct one for a single pane.
//
// Split view renders TWO transcripts in one frame, so the one cache was handed
// thread A, then thread B, then A again next frame — and cleared itself every
// single time. In split view the memoization did not merely degrade, it was
// off, in exactly the mode that has twice as much to measure.
//
// So the store is a small LRU over per-slot maps.
//
// A SLOT IS A PANE AND A THREAD, not a thread. That looks like waste when both
// panes show one thread -- which is what splitting does by default -- and it
// is not, because the KEY the measurements hang on is a WIDTH. Each pane asks
// for two widths per user bubble (see WidthPair below: the max text width and
// the hugged width that comes out of it), the pair holds exactly two, and the
// two panes have different widths the moment the divider is dragged off
// centre. Shared, that is four widths through two slots: every ask evicts the
// answer the other pane just wrote, every frame, forever -- the same negative
// hit rate the WidthPair was introduced to kill, one level up.
//
// Four slots: two panes, each with the thread it is showing and the one it
// just came from, so a tab switch while split evicts neither pane's work.
//
// The bound is now: kMaxThreads maps, each holding one entry per MESSAGE of
// that thread. That is proportional to the content those threads already have
// in memory, not to the number of threads ever opened, which is what the old
// clear-on-change was protecting against.
//
// TWO WIDTHS PER KEY, and that is the whole point of the WidthPair below.
// One slot per key looked obviously right and had a NEGATIVE hit rate on every
// user message. A user bubble hugs its text, so measuring it takes two passes
// at two different widths: user_box() asks at the bubble's MAXIMUM text width
// to find the widest wrapped line, and bubble_height() then asks again at the
// HUGGED width that came out of it (630 and 458 px respectively, on a 1180px
// window). With one slot the second put() evicted the first, so the next
// frame's first ask missed, recomputed, evicted the second — every user
// message, every frame, forever. Measured on the 120-message fixture: 72.5
// misses per frame against 37.4 hits, of which 99.8% were this width
// ping-pong and 0.2% were genuinely cold. Holding both widths turns it into
// two cold misses per message for the life of the thread.
//
// Two, not N: a message has exactly one max width and one hugged width at a
// given pane width, so two is the working set, and a fixed pair is bounded by
// construction — a width-keyed map would grow by an entry per message per
// frame across a resize DRAG, which is the failure mode this replaces, not a
// fix for it.
//
// Pure app-layer, graphics-free (like transcript_cache.h) — no UIContext, no
// afterhours draw. The transcript system owns the single instance.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>

namespace ecs::model {

struct MsgRender {
    std::string body;      // redacted + md-stripped display text
    int line_count = 0;    // logical (newline-split) wrapped line count
    float height = 0.0f;   // measured body height at wrap_w (matches render)
    float wrap_w = -1.0f;  // the textW this was computed at (-1 = empty slot)
};

class TranscriptRenderCache {
  public:
    // Slots (pane x thread) whose measurements are kept.
    //
    // It used to keep ONE, clearing on every thread change -- and in split
    // view that means it clears twice a frame and memoizes nothing at all.
    // Two 60-turn threads side by side: 10.90 ms -> 8.77 ms median.
    static constexpr std::size_t kMaxThreads = 4;

    // Select the thread the following get/put calls belong to. Replaces
    // reset_for_thread: same call site, same argument, and it no longer
    // throws away the other pane's work.
    void reset_for_thread(const std::string& thread_id) {
        if (thread_id == active_) return;
        active_ = thread_id;
        auto it = threads_.find(thread_id);
        if (it == threads_.end()) {
            if (threads_.size() >= kMaxThreads) evict_lru();
            order_.push_front(thread_id);
            Slot s;
            s.pos = order_.begin();
            threads_.emplace(thread_id, std::move(s));
            return;
        }
        order_.erase(it->second.pos);
        order_.push_front(thread_id);
        it->second.pos = order_.begin();
    }

    // Fetch the cached render for `key` at wrap width `w`, or nullptr on a
    // miss (caller recomputes + put()s).
    const MsgRender* get(const std::string& key, float w,
                         const std::string& source) const {
        auto slot = threads_.find(active_);
        if (slot == threads_.end()) {
            ++absent_;
            return nullptr;
        }
        auto it = slot->second.map.find(key);
        if (it == slot->second.map.end()) {
            ++absent_;
            return nullptr;
        }
        if (it->second.source != source) {
            ++changed_;
            return nullptr;
        }
        if (const MsgRender* m = it->second.find(w)) return m;
        ++stale_;
        return nullptr;
    }

    const MsgRender& put(const std::string& key, const std::string& source,
                         MsgRender r) {
        auto& pair = threads_[active_].map[key];
        pair.reset_for_source(source);
        return pair.insert(std::move(r));
    }

    // ---- The hugged width of a user bubble, at a given pane width ---------
    //
    // A user bubble shrinks to fit its longest wrapped line, and afterhours
    // cannot size a box to its own text (gaps #79 / #87 / #103), so hanabi
    // works it out itself: wrap the body, then measure every resulting line
    // and take the widest. That is one wrap plus one measure PER LINE, and it
    // ran for every user message in the thread on every frame, on-screen or
    // not, to produce a number that cannot change unless the text or the pane
    // width does. Memoized here rather than in its own container because it
    // is keyed the same way, invalidated the same way, and evicted with the
    // same thread -- one owner, one lifetime.
    const float* hug(const std::string& key, float paneW,
                     const std::string& source) const {
        auto slot = threads_.find(active_);
        if (slot == threads_.end()) return nullptr;
        auto it = slot->second.map.find(key);
        if (it == slot->second.map.end() || it->second.source != source ||
            it->second.hugPaneW != paneW)
            return nullptr;
        return &it->second.hugTextW;
    }
    void put_hug(const std::string& key, const std::string& source,
                 float paneW, float textW) {
        auto& slot = threads_[active_].map[key];
        slot.reset_for_source(source);
        slot.hugPaneW = paneW;
        slot.hugTextW = textW;
    }

    // Miss BREAKDOWN, because the two kinds mean opposite things. `absent` is
    // a cold entry -- the cache working, paying once. `stale` is an entry that
    // exists at a width neither slot holds, which on a STATIC transcript means
    // callers are cycling more widths than the pair can hold; a single
    // "misses" number cannot tell the two apart, and that is why the
    // ping-pong survived as long as it did.
    std::size_t absent() const { return absent_; }
    std::size_t stale() const { return stale_; }
    std::size_t changed() const { return changed_; }

    // Entries held for the ACTIVE thread (the number the old size() meant).
    std::size_t size() const {
        auto slot = threads_.find(active_);
        return slot == threads_.end() ? 0 : slot->second.map.size();
    }

    // Entries held across every remembered thread, and how many threads.
    std::size_t total_size() const {
        std::size_t n = 0;
        for (const auto& kv : threads_) n += kv.second.map.size();
        return n;
    }
    std::size_t threads() const { return threads_.size(); }

    void clear() {
        threads_.clear();
        order_.clear();
        active_.clear();
    }

  private:
    // Two width slots, newest-first. Insert evicts the older one, so the pair
    // holds the two most recently asked-for widths -- which for the two-pass
    // hug measure is exactly both of them.
    struct WidthPair {
        MsgRender a;  // most recent
        MsgRender b;  // previous
        std::string source;
        float hugPaneW = -1.0f;  // pane width the hug below was measured at
        float hugTextW = 0.0f;   // widest wrapped line + label inset

        void reset_for_source(const std::string& next) {
            if (source == next) return;
            source = next;
            a = {};
            b = {};
            hugPaneW = -1.0f;
            hugTextW = 0.0f;
        }

        const MsgRender* find(float w) const {
            if (a.wrap_w == w) return &a;
            if (b.wrap_w == w) return &b;
            return nullptr;
        }
        MsgRender& insert(MsgRender r) {
            if (a.wrap_w == r.wrap_w) {  // refresh in place, keep b
                a = std::move(r);
                return a;
            }
            b = std::move(a);
            a = std::move(r);
            return a;
        }
    };

    struct Slot {
        std::unordered_map<std::string, WidthPair> map;
        std::list<std::string>::iterator pos;
    };

    void evict_lru() {
        if (order_.empty()) return;
        threads_.erase(order_.back());
        order_.pop_back();
    }

    std::unordered_map<std::string, Slot> threads_;
    std::list<std::string> order_;  // MRU front ... LRU back
    std::string active_;
    mutable std::size_t absent_ = 0;
    mutable std::size_t stale_ = 0;
    mutable std::size_t changed_ = 0;
};

}  // namespace ecs::model
