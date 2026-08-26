#pragma once

// ---------------------------------------------------------------------------
// The outbox's READ side: what to re-send, and when.
//
// WHY THIS EXISTS AS A FILE. `disk_cache::outbox_add` has been writing the
// user's prompt to disk before every send since the local-first branch landed
// (LOCAL_FIRST_DESIGN.md, idea #2/#5), and `disk_cache::outbox_list` -- the
// function that reads it back -- had NO CALLER anywhere in src, tests or
// tools. So the promise in the send path's own comment,
//
//     "On failure: sync -> Failed, kept in the outbox to retry"
//
// was half true: the prompt was kept, and nothing ever retried it. The design
// note says so out loud and calls it deferred ("HONEST GAP: there is no
// reconnect-DRAIN yet ... the user must re-send"). docs/COMMIT_AUDIT.md CB3
// found the same hole from the other end. This is the missing half.
//
// It is a separate file from the loader for one reason: the loader is an ECS
// system that needs graphics, futures, Settings and a live backend to
// instantiate, and the interesting part of a retry is a POLICY -- which entry
// is next, how long to wait, when to give the slot back. That policy is pure,
// so it is here, where tests/unit/test_outbox.cpp can drive it against an
// injected clock and a fake store instead of against a real network failing on
// a real schedule.
//
// THE POLICY, AND WHY EACH PART OF IT
//
//   * FIFO per session, sessions round-robin. Order within one thread is the
//     order the user typed; order ACROSS threads is arbitrary, and taking them
//     strictly in restore order would let one unreachable thread starve every
//     other one.
//   * EXPONENTIAL BACKOFF, capped. A backend that is down is down for minutes,
//     and a retry every frame is a self-inflicted denial of service that also
//     burns the user's battery. 2s, 4s, 8s ... capped at kMaxBackoff.
//   * NEVER GIVE UP, and never drop. The entry is the user's words; the whole
//     point of writing it to disk was that nothing but a server confirmation
//     removes it. attempts() is exposed so a UI can say "not sent (4 tries)",
//     but no attempt count discards anything.
//   * ONE IN FLIGHT AT A TIME. The dispatch seam it feeds (the loader's
//     requestSendPrompt / requestStreamPrompt pair) holds exactly one prompt,
//     and a second retry issued before the first resolves would race it into
//     the same transcript.
//
// Nothing here touches the disk. `restore()` is handed the enumeration; the
// caller (loader_system) is the one that knows about disk_cache, and a test is
// the one that knows about a fake.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace api::outbox {

// Seconds. First wait after a failure, then doubling, then held here.
inline constexpr int64_t kFirstBackoff = 2;
inline constexpr int64_t kMaxBackoff = 60;

// A scripted UI run has no real seconds to spend: the e2e loop ticks a FIXED
// dt (src/main.cpp) so `wait_frames` advances the app without advancing the
// wall clock the backoff is measured against, and a test that wanted to watch
// a retry land would otherwise have to sleep for real. HANABI_OUTBOX_BACKOFF
// (seconds) replaces the whole curve with one flat value; 0 means "retry on
// the next frame it is dispatchable". Unset -- every real run -- and the curve
// above is used unchanged.
inline int64_t backoff_override() {
    static const int64_t v = [] () -> int64_t {
        const char* e = std::getenv("HANABI_OUTBOX_BACKOFF");
        if (!e || !*e) return -1;
        char* end = nullptr;
        const long long n = std::strtoll(e, &end, 10);
        return (end && *end == '\0' && n >= 0) ? static_cast<int64_t>(n) : -1;
    }();
    return v;
}

struct Entry {
    std::string sessionId;
    std::string prompt;
    int attempts = 0;
    // Wall-clock second before which this entry must not be tried again. 0 =
    // ready now, which is what a freshly restored entry is: the reason the app
    // is starting is usually that the last one ended.
    int64_t notBefore = 0;
};

class Retry {
  public:
    // Replace the pending set with what the store holds. Called at startup and
    // anywhere the store may have changed underneath us. An entry already
    // known keeps its attempt count and its backoff -- a restore is not a
    // reason to hammer a backend that has been refusing for ten minutes.
    void restore(const std::vector<Entry>& fromStore) {
        std::vector<Entry> merged;
        merged.reserve(fromStore.size());
        for (const auto& in : fromStore) {
            const Entry* known = find(in.sessionId, in.prompt);
            merged.push_back(known ? *known : in);
        }
        entries_ = std::move(merged);
        if (cursor_ >= entries_.size()) cursor_ = 0;
    }

    // The next entry to try, or nullptr. `now` is wall-clock seconds;
    // `dispatchable(id)` answers "can this session take a send right now" --
    // the loader passes a lambda over sending_for() and the open thread.
    template <class Dispatchable>
    const Entry* next(int64_t now, Dispatchable&& dispatchable) const {
        if (inFlight_) return nullptr;
        if (entries_.empty()) return nullptr;
        // Round-robin from the cursor so one unreachable thread cannot hold
        // the others' entries behind it.
        for (std::size_t k = 0; k < entries_.size(); ++k) {
            const Entry& e = entries_[(cursor_ + k) % entries_.size()];
            if (e.notBefore > now) continue;
            if (!dispatchable(e.sessionId)) continue;
            return &e;
        }
        return nullptr;
    }

    // Record that `next()`'s pick has been handed to the send path. The slot
    // is held until confirmed() or failed() says what became of it.
    void attempted(const Entry& picked) {
        inFlight_ = true;
        inFlightId_ = picked.sessionId;
        inFlightPrompt_ = picked.prompt;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].sessionId != picked.sessionId) continue;
            if (entries_[i].prompt != picked.prompt) continue;
            ++entries_[i].attempts;
            cursor_ = (i + 1) % entries_.size();
            return;
        }
    }

    // The server took it: it is gone from the store and gone from here.
    void confirmed(const std::string& sessionId, const std::string& prompt) {
        release(sessionId, prompt);
        erase(sessionId, prompt);
    }

    // It failed again. Back off, keep it.
    void failed(const std::string& sessionId, const std::string& prompt,
                int64_t now) {
        release(sessionId, prompt);
        for (auto& e : entries_) {
            if (e.sessionId != sessionId || e.prompt != prompt) continue;
            e.notBefore = now + backoff_for(e.attempts);
            return;
        }
    }

    // A send this policy did not issue confirmed the same (id, prompt) -- the
    // user re-sent it by hand, say. Drop our copy so it is not sent twice.
    void forget(const std::string& sessionId, const std::string& prompt) {
        confirmed(sessionId, prompt);
    }

    // Wall-clock seconds to wait after the Nth consecutive failure. 2, 4, 8,
    // 16, 32, then 60 forever. Free function shape so the test can pin the
    // curve without building a Retry.
    static int64_t backoff_for(int attempts) {
        if (attempts <= 0) return 0;
        if (const int64_t o = backoff_override(); o >= 0) return o;
        int64_t b = kFirstBackoff;
        for (int i = 1; i < attempts && b < kMaxBackoff; ++i) b *= 2;
        return std::min(b, kMaxBackoff);
    }

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    bool in_flight() const { return inFlight_; }
    const std::vector<Entry>& entries() const { return entries_; }

    bool holds(const std::string& sessionId, const std::string& prompt) const {
        return find(sessionId, prompt) != nullptr;
    }

    // Take on an entry the store already holds but this policy has not seen --
    // the user's own send failed, so the prompt is on disk and the retry loop
    // has to pick it up from here. `attempts` is 1 because that send WAS the
    // first attempt; adopting at 0 would make the automatic retry immediate and
    // charge the backend twice in one frame.
    void adopt(const std::string& sessionId, const std::string& prompt,
               int attempts = 1) {
        if (holds(sessionId, prompt)) return;
        entries_.push_back(Entry{sessionId, prompt, attempts, 0});
    }

    // How many unconfirmed prompts are held for one thread.
    std::size_t count_for(const std::string& sessionId) const {
        std::size_t n = 0;
        for (const auto& e : entries_)
            if (e.sessionId == sessionId) ++n;
        return n;
    }

    int attempts_for(const std::string& sessionId,
                     const std::string& prompt) const {
        const Entry* e = find(sessionId, prompt);
        return e ? e->attempts : 0;
    }

  private:
    const Entry* find(const std::string& sessionId,
                      const std::string& prompt) const {
        for (const auto& e : entries_)
            if (e.sessionId == sessionId && e.prompt == prompt) return &e;
        return nullptr;
    }

    void release(const std::string& sessionId, const std::string& prompt) {
        if (!inFlight_) return;
        if (inFlightId_ != sessionId || inFlightPrompt_ != prompt) return;
        inFlight_ = false;
        inFlightId_.clear();
        inFlightPrompt_.clear();
    }

    void erase(const std::string& sessionId, const std::string& prompt) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->sessionId != sessionId || it->prompt != prompt) continue;
            const std::size_t i =
                static_cast<std::size_t>(it - entries_.begin());
            entries_.erase(it);
            if (entries_.empty()) {
                cursor_ = 0;
            } else if (cursor_ > i) {
                --cursor_;
            } else if (cursor_ >= entries_.size()) {
                cursor_ = 0;
            }
            return;
        }
    }

    std::vector<Entry> entries_;
    mutable std::size_t cursor_ = 0;
    bool inFlight_ = false;
    std::string inFlightId_;
    std::string inFlightPrompt_;
};

}  // namespace api::outbox
