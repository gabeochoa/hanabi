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

#include <cstddef>
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
    // Fetch the cached render for `key` at wrap width `w`, or nullptr on a
    // miss (caller recomputes + put()s).
    const MsgRender* get(const std::string& key, float w) const {
        auto it = map_.find(key);
        if (it == map_.end()) {
            ++absent_;
            return nullptr;
        }
        if (const MsgRender* m = it->second.find(w)) return m;
        ++stale_;
        return nullptr;
    }

    const MsgRender& put(const std::string& key, MsgRender r) {
        return map_[key].insert(std::move(r));
    }

    // Drop everything when the open thread changes (bounds growth).
    void reset_for_thread(const std::string& thread_id) {
        if (thread_id != thread_) {
            map_.clear();
            thread_ = thread_id;
        }
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
    // is keyed the same way, invalidated the same way, and reset by the same
    // thread change — one owner, one lifetime.
    const float* hug(const std::string& key, float paneW) const {
        auto it = map_.find(key);
        if (it == map_.end() || it->second.hugPaneW != paneW) return nullptr;
        return &it->second.hugTextW;
    }
    void put_hug(const std::string& key, float paneW, float textW) {
        auto& slot = map_[key];
        slot.hugPaneW = paneW;
        slot.hugTextW = textW;
    }

    // Miss BREAKDOWN, because the two kinds mean opposite things. `absent` is
    // a cold entry — the cache working, paying once. `stale` is an entry that
    // exists at a width neither slot holds, which on a STATIC transcript means
    // callers are cycling more widths than the pair can hold; a single
    // "misses" number cannot tell the two apart, and that is why the
    // ping-pong above survived as long as it did.
    std::size_t absent() const { return absent_; }
    std::size_t stale() const { return stale_; }

    std::size_t size() const { return map_.size(); }
    void clear() {
        map_.clear();
        thread_.clear();
    }

  private:
    // Two width slots, newest-first. Insert evicts the older one, so the pair
    // holds the two most recently asked-for widths — which for the two-pass
    // hug measure is exactly both of them.
    struct WidthPair {
        MsgRender a;  // most recent
        MsgRender b;  // previous
        float hugPaneW = -1.0f;  // pane width the hug below was measured at
        float hugTextW = 0.0f;   // widest wrapped line + label inset

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

    std::unordered_map<std::string, WidthPair> map_;
    std::string thread_;
    mutable std::size_t absent_ = 0;
    mutable std::size_t stale_ = 0;
};

}  // namespace ecs::model
