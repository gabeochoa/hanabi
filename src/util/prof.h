#pragma once

// ---------------------------------------------------------------------------
// A per-phase frame profiler, because the soak probe cannot see inside a frame.
//
// soak.h reports ONE number per bucket: whole-frame ms. On the 160-message perf
// fixture that number reads 5.0-7.5 ms bucket to bucket -- a spread of 2.5 ms
// on a 6 ms frame. Anything worth finding in the transcript is smaller than
// that spread, so a change that genuinely removed 1 ms would be indistinguish-
// able from the next bucket running fast. The instrument was coarser than the
// thing being measured.
//
// This times NAMED PHASES inside the frame and reports the total across the
// whole run, so the sample count does the averaging that a single bucket
// cannot. 1500 frames of a phase that costs 0.2 ms is 300 ms of signal against
// a clock with microsecond resolution.
//
// It also COUNTS. "How many times per frame does the transcript wrap a line of
// text" is the question that actually decides what to fix, and no timer answers
// it -- a phase that is 0.4 ms because it makes 4000 calls at 100 ns is a
// different bug from one that makes 4 calls at 100 us.
//
//   HANABI_PROF=1   collect and dump at the end of a soak run
//
// A hard no-op when unset: one relaxed bool read at each site, the same
// contract as test_hooks.h / soak.h. Single-threaded by construction -- the
// frame loop is the only caller -- so the counters are plain, not atomic.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace hanabi::prof {

inline bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_PROF");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

struct Entry {
    unsigned long long calls = 0;
    unsigned long long nanos = 0;
};

inline std::unordered_map<std::string, Entry>& table() {
    static std::unordered_map<std::string, Entry> t;
    return t;
}

inline unsigned long long& frames() {
    static unsigned long long n = 0;
    return n;
}

// Bump the frame counter, so every total can be reported per-frame. Called
// once per measured frame by the soak loop.
inline void frame() {
    if (enabled()) ++frames();
}

// A pure counter: how many times did this happen, no timing. The cheapest
// possible probe, for the inner calls that a timer would dominate.
inline void tick(const char* label, unsigned long long n = 1) {
    if (!enabled()) return;
    table()[label].calls += n;
}

// RAII phase timer. Accumulates into `label` on destruction.
struct Scope {
    const char* label;
    std::chrono::steady_clock::time_point t0;
    bool on;
    explicit Scope(const char* l)
        : label(l), on(enabled()) {
        if (on) t0 = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (!on) return;
        const auto dt = std::chrono::steady_clock::now() - t0;
        Entry& e = table()[label];
        ++e.calls;
        e.nanos += static_cast<unsigned long long>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// Report. Timed phases first (sorted by cost), then the pure counters.
inline void dump() {
    if (!enabled()) return;
    const double f = static_cast<double>(frames() > 0 ? frames() : 1);
    std::vector<std::pair<std::string, Entry>> rows(table().begin(),
                                                    table().end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        if (a.second.nanos != b.second.nanos)
            return a.second.nanos > b.second.nanos;
        return a.second.calls > b.second.calls;
    });
    std::printf("\n[prof] %llu frames\n", frames());
    std::printf("[prof] %-34s %12s %10s %10s %10s\n", "phase", "calls",
                "calls/f", "ms total", "ms/frame");
    for (const auto& [name, e] : rows) {
        if (e.nanos == 0) continue;
        const double totalMs = static_cast<double>(e.nanos) / 1e6;
        std::printf("[prof] %-34s %12llu %10.1f %10.2f %10.4f\n", name.c_str(),
                    e.calls, static_cast<double>(e.calls) / f, totalMs,
                    totalMs / f);
    }
    bool anyCount = false;
    for (const auto& [name, e] : rows) {
        if (e.nanos != 0) continue;
        if (!anyCount) {
            std::printf("[prof] %-34s %12s %10s\n", "counter", "calls",
                        "calls/f");
            anyCount = true;
        }
        std::printf("[prof] %-34s %12llu %10.1f\n", name.c_str(), e.calls,
                    static_cast<double>(e.calls) / f);
    }
    std::fflush(stdout);
}

}  // namespace hanabi::prof
