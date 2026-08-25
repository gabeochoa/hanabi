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
// correctly re-measures. Bounded by clearing when the open thread changes.
//
// Pure app-layer, graphics-free (like transcript_cache.h) — no UIContext, no
// afterhours draw. The transcript system owns the single instance.

#include <cstddef>
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
    // Fetch the cached render for `key` at wrap width `w`, or nullptr on a miss
    // / stale-width entry (caller recomputes + put()s).
    const MsgRender* get(const std::string& key, float w) const {
        auto it = map_.find(key);
        if (it == map_.end()) {
            ++absent_;
            return nullptr;
        }
        if (it->second.wrap_w != w) {  // width changed -> stale
            ++stale_;
            return nullptr;
        }
        return &it->second;
    }
    // Miss BREAKDOWN, because the two kinds mean opposite things. `absent` is
    // a cold entry -- the cache working, paying once. `stale` is an entry that
    // exists at a DIFFERENT width, which on a static transcript means two
    // callers are asking the same key at two widths and evicting each other
    // every frame; that is a cache with a negative hit rate, and it is
    // invisible in a single "misses" number.
    std::size_t absent() const { return absent_; }
    std::size_t stale() const { return stale_; }

    const MsgRender& put(const std::string& key, MsgRender r) {
        auto& slot = map_[key];
        slot = std::move(r);
        return slot;
    }

    // Drop everything when the open thread changes (bounds growth).
    void reset_for_thread(const std::string& thread_id) {
        if (thread_id != thread_) {
            map_.clear();
            thread_ = thread_id;
        }
    }

    std::size_t size() const { return map_.size(); }
    void clear() { map_.clear(); thread_.clear(); }

  private:
    std::unordered_map<std::string, MsgRender> map_;
    std::string thread_;
    mutable std::size_t absent_ = 0;
    mutable std::size_t stale_ = 0;
};

}  // namespace ecs::model
