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

#include <mach-o/dyld.h>
#include <time.h>

#include <algorithm>
#include <cstdint>
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

// THREAD CPU TIME, not wall clock, and the difference is the whole reason any
// of these numbers can be believed. Measured on a laptop that was also running
// two Xcode builds, Puffin, Spotlight indexing and a virus scanner at 199% CPU
// -- load average 29 -- the app's WALL-clock frame time on the 480-message
// fixture read 4.5 ms at its best bucket and 10.6 ms at its median, and an
// A/B of two binaries came out with the faster one 50% slower. Wall clock on a
// contended machine measures the machine, and no number of repetitions fixes
// that: the contention is not noise around a true value, it is a different
// quantity.
//
// CLOCK_THREAD_CPUTIME_ID counts only cycles this thread was actually given,
// so being descheduled costs nothing. The frame loop is single-threaded, which
// is what makes this the right clock -- GPU wait and vsync fall out of the
// reading too, and for "how much work does the transcript do" that is the
// question anyway.
inline unsigned long long cpu_nanos() {
    struct timespec ts {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<unsigned long long>(ts.tv_sec) * 1000000000ULL +
           static_cast<unsigned long long>(ts.tv_nsec);
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

// A GAUGE: the last value seen, not a running total. A counter answers "how
// many times", and for a cache the question that decides whether its bound
// holds is "how big did it get" -- which a sum of sizes cannot answer and a
// max of them can. Kept in its own table so `dump` can print gauges apart from
// counters rather than showing a cache's size as if it were a call count.
inline std::unordered_map<std::string, unsigned long long>& gauges() {
    static std::unordered_map<std::string, unsigned long long> g;
    return g;
}

// Record the HIGH WATER MARK of `label`. A bound is a claim about the maximum,
// so the maximum is what gets reported.
inline void gauge(const char* label, unsigned long long v) {
    if (!enabled()) return;
    unsigned long long& cur = gauges()[label];
    if (v > cur) cur = v;
}

// RAII phase timer. Accumulates into `label` on destruction.
struct Scope {
    const char* label;
    unsigned long long t0 = 0;
    bool on;
    explicit Scope(const char* l) : label(l), on(enabled()) {
        if (on) t0 = cpu_nanos();
    }
    ~Scope() {
        if (!on) return;
        Entry& e = table()[label];
        ++e.calls;
        e.nanos += cpu_nanos() - t0;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// Whole-frame CPU, sampled by the soak loop, so the phase totals can be read
// as a share of a frame that is itself measured on the same clock.
inline void frame_cpu(unsigned long long ns) {
    if (!enabled()) return;
    Entry& e = table()["FRAME (cpu)"];
    ++e.calls;
    e.nanos += ns;
}

// ---- Allocation counting ------------------------------------------------
//
// "Every std::string built to be handed to a label is an allocation, 60 times
// a second" is a claim about a COUNT, and nothing already in the box reports
// one. soak.h's live-block delta sees only allocations that are RETAINED,
// which is a leak detector, not a churn meter -- a string built and destroyed
// inside the frame is invisible to it and is exactly the thing being looked
// for. `malloc_history -allBySize` has the same blind spot: it walks live
// blocks, so on this app it reports Metal's one-time setup and nothing about
// the frame at all.
//
// A global operator new/delete counter sees every C++ allocation, transient
// ones included. Defined in exactly one TU (main.cpp) via
// HANABI_PROF_DEFINE_ALLOC_COUNTERS. Costs one predicted branch and one
// non-atomic increment per allocation next to malloc's own tens of
// nanoseconds; measured at zero effect on frame CPU.
inline unsigned long long& alloc_count() {
    static unsigned long long n = 0;
    return n;
}
inline unsigned long long& alloc_bytes() {
    static unsigned long long n = 0;
    return n;
}

// ---- Call-site attribution ----------------------------------------------
//
// The counter above says the idle frame allocates 4,271 times. It does not say
// WHERE, and nothing else in the box does either:
//
//   * `malloc_history -allBySize` walks blocks that are LIVE right now. Run
//     against this app mid-soak it reports Metal's device init, the sokol
//     buffers and the mock catalog -- every one of them a one-time cost -- and
//     not a single frame allocation, because the frame frees everything it
//     allocates. That is the tool that found the Metal leak in one run, and it
//     is structurally blind to churn. (`-allEvents` is not blind, and dumps
//     every malloc and free the process ever made, in order: on a 60-frame run
//     that is ~250,000 records to read by hand.)
//   * `AllocScope` attributes a phase, and a phase is only as narrow as
//     somebody thought to bracket. It cannot find a caller nobody suspected.
//
// So: hash the top three return addresses at every `operator new` and count.
// Fixed table, linear probing, no allocation anywhere inside the recorder --
// an allocating profiler of allocations recurses on its first call. Addresses
// are printed raw with the image load address; scripts/alloc_sites.sh pipes
// them through `atos`, which resolves the inlined header code that `dladdr`
// (nearest exported symbol) gets wrong.
//
// The stack is walked through the frame-pointer chain rather than
// __builtin_return_address(1+), which is documented to be allowed to fault:
// arm64 always has the chain, and every hop is bounds-checked.
inline bool sites_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_PROF_SITES");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

#ifndef HANABI_PROF_SITE_DEPTH
#define HANABI_PROF_SITE_DEPTH 8
#endif
inline constexpr size_t kSiteDepth = HANABI_PROF_SITE_DEPTH;
inline constexpr size_t kSiteSlots = 16384;

struct Site {
    const void* pc[kSiteDepth] = {};
    unsigned long long calls = 0;
    unsigned long long bytes = 0;
};

inline Site* site_table() {
    static Site table[kSiteSlots];
    return table;
}

inline unsigned long long& site_overflow() {
    static unsigned long long n = 0;
    return n;
}

__attribute__((always_inline)) inline void capture_site(const void** out) {
    for (size_t i = 0; i < kSiteDepth; ++i) out[i] = nullptr;
    auto* fp = static_cast<void**>(__builtin_frame_address(0));
    for (size_t i = 0; i < kSiteDepth; ++i) {
        const auto raw = reinterpret_cast<uintptr_t>(fp);
        if (fp == nullptr || (raw & 0x7u) != 0u) break;
        out[i] = fp[1];
        auto* next = static_cast<void**>(fp[0]);
        if (next <= fp) break;
        fp = next;
    }
}

inline void record_site(const void* const* pc, size_t n) {
    uintptr_t h = 0;
    for (size_t i = 0; i < kSiteDepth; ++i)
        h = (h ^ (reinterpret_cast<uintptr_t>(pc[i]) >> 2)) *
            0x9E3779B97F4A7C15ull;
    Site* table = site_table();
    for (size_t probe = 0; probe < 32; ++probe) {
        Site& s = table[(h + probe) & (kSiteSlots - 1)];
        if (s.calls == 0)
            for (size_t i = 0; i < kSiteDepth; ++i) s.pc[i] = pc[i];
        bool same = true;
        for (size_t i = 0; i < kSiteDepth; ++i) same = same && s.pc[i] == pc[i];
        if (same) {
            ++s.calls;
            s.bytes += n;
            return;
        }
    }
    ++site_overflow();
}

// Snapshot / diff, so a phase can report its OWN allocations rather than the
// frame's. `AllocScope` records the delta into a counter under `label`.
struct AllocScope {
    const char* label;
    unsigned long long c0 = 0;
    bool on;
    explicit AllocScope(const char* l) : label(l), on(enabled()) {
        if (on) c0 = alloc_count();
    }
    ~AllocScope() {
        if (!on) return;
        table()[label].calls += alloc_count() - c0;
    }
    AllocScope(const AllocScope&) = delete;
    AllocScope& operator=(const AllocScope&) = delete;
};

// Print the busiest call sites, most calls first. Raw addresses plus the
// image's load address: scripts/alloc_sites.sh turns them into file:line.
inline void dump_sites() {
    if (!sites_enabled()) return;
    const double f = static_cast<double>(frames() > 0 ? frames() : 1);
    Site* table = site_table();
    std::vector<const Site*> rows;
    for (size_t i = 0; i < kSiteSlots; ++i)
        if (table[i].calls > 0) rows.push_back(&table[i]);
    std::sort(rows.begin(), rows.end(),
              [](const Site* a, const Site* b) { return a->calls > b->calls; });
    std::printf("[sites] %zu distinct sites, %llu overflowed\n", rows.size(),
                site_overflow());
    // Every loaded image and where it landed, so the symbolizer can pick the
    // right one per address: a frame inside libsystem_malloc is as much of an
    // answer as a frame inside hanabi, and `atos -o hanabi.exe` renders it as
    // a bare hex number.
    for (uint32_t i = 0; i < _dyld_image_count(); ++i)
        std::printf("[sites-image] 0x%llx %s\n",
                    static_cast<unsigned long long>(
                        reinterpret_cast<uintptr_t>(_dyld_get_image_header(i))),
                    _dyld_get_image_name(i));
    std::printf("[sites] %12s %10s %12s  %s\n", "calls", "calls/f", "B/frame",
                "return addresses, innermost first");
    const size_t n = rows.size() < 40 ? rows.size() : 40;
    for (size_t i = 0; i < n; ++i) {
        std::printf("[sites] %12llu %10.1f %12.0f ", rows[i]->calls,
                    static_cast<double>(rows[i]->calls) / f,
                    static_cast<double>(rows[i]->bytes) / f);
        for (size_t d = 0; d < kSiteDepth; ++d)
            std::printf(" 0x%llx", static_cast<unsigned long long>(
                                       reinterpret_cast<uintptr_t>(
                                           rows[i]->pc[d])));
        std::printf("\n");
    }
    std::fflush(stdout);
}

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
    std::printf("\n[prof] %llu frames  (CPU time, not wall clock)\n",
                frames());
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
    std::printf("[prof] %-34s %12llu %10.1f  (%llu KB, %.0f B/frame)\n",
                "ALLOCATIONS (operator new)", alloc_count(),
                static_cast<double>(alloc_count()) / f,
                alloc_bytes() / 1024, static_cast<double>(alloc_bytes()) / f);
    if (!gauges().empty()) {
        std::vector<std::pair<std::string, unsigned long long>> gs(
            gauges().begin(), gauges().end());
        std::sort(gs.begin(), gs.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::printf("[prof] %-34s %12s\n", "gauge (high water)", "peak");
        for (const auto& [name, v] : gs)
            std::printf("[prof] %-34s %12llu\n", name.c_str(), v);
    }
    std::fflush(stdout);
    dump_sites();
}

}  // namespace hanabi::prof

// Define the global operator new / delete counters. EXACTLY ONE TU may do
// this; main.cpp does.
#ifdef HANABI_PROF_DEFINE_ALLOC_COUNTERS
#include <cstdlib>
#include <new>
void* operator new(std::size_t n) {
    if (hanabi::prof::enabled()) {
        ++hanabi::prof::alloc_count();
        hanabi::prof::alloc_bytes() += n;
        if (hanabi::prof::sites_enabled()) {
            const void* pc[hanabi::prof::kSiteDepth];
            hanabi::prof::capture_site(pc);
            hanabi::prof::record_site(pc, n);
        }
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif
