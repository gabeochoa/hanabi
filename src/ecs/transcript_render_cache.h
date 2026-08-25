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
// Split view renders TWO transcripts in one frame. It does it by swapping
// app.openSession and app.splitSession around a second render_transcript call
// (main_pane_system.h, "Left = the primary open thread"), so the one cache is
// handed thread A, then thread B, then A again next frame — and clears itself
// every single time. In split view the memoization did not merely degrade, it
// was off, in exactly the mode that has twice as much to measure.
//
// So the store is a small LRU over per-thread maps. Two panes is the case that
// exists; three is one spare, and it is the reason the cap is not two — a
// tab switch while split should not evict the pane you did not touch.
//
// The bound is now: kMaxThreads maps, each holding one entry per MESSAGE of
// that thread. That is proportional to the content those threads already have
// in memory, not to the number of threads ever opened, which is what the old
// clear-on-change was protecting against.
//
// Pure app-layer, graphics-free (like transcript_cache.h) — no UIContext, no
// afterhours draw. The transcript system owns the single instance.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>

namespace ecs::model {

struct MsgRender {
    std::string body;      // redacted + md-stripped display text
    int line_count = 0;    // logical (newline-split) wrapped line count
    float height = 0.0f;   // measured body height at wrap_w (matches render)
    float wrap_w = -1.0f;  // the textW this was computed at
};

class TranscriptRenderCache {
  public:
    // Threads whose measurements are kept. Two panes plus one spare.
    static constexpr std::size_t kMaxThreads = 3;

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

    // Fetch the cached render for `key` at wrap width `w`, or nullptr on a miss
    // / stale-width entry (caller recomputes + put()s).
    const MsgRender* get(const std::string& key, float w) const {
        auto slot = threads_.find(active_);
        if (slot == threads_.end()) return nullptr;
        auto it = slot->second.map.find(key);
        if (it == slot->second.map.end()) return nullptr;
        if (it->second.wrap_w != w) return nullptr;  // width changed -> stale
        return &it->second;
    }

    const MsgRender& put(const std::string& key, MsgRender r) {
        auto& slot = threads_[active_].map[key];
        slot = std::move(r);
        return slot;
    }

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
    struct Slot {
        std::unordered_map<std::string, MsgRender> map;
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
};

}  // namespace ecs::model
