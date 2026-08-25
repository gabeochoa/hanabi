#pragma once

// ---------------------------------------------------------------------------
// The memory ladder: where a footprint reading actually comes from, one rung
// at a time, in a process that has done nothing else.
//
// WHY. `scripts/measure_launch.sh` prints ONE peak-RSS number for the whole
// process, at the end of a run that opened a window, loaded a catalog, opened
// a tab and laid out a transcript. That number is the process. It is not the
// catalog, it is not the tab, and it moves when anything ahead of it in the
// launch changes — so an optimisation cannot be judged against it. `soak.h`
// answers a different question again: it measures the SLOPE of a steady state,
// which finds a per-frame leak and is blind to a per-thread one, because a
// hundred threads opened and left open is not a leak, it is a hundred tabs.
//
// Ported from Puffin's `Tests/MemoryAttributionTests.swift`, whose opening
// comment is the reason it exists — the same complaint, about the same kind of
// number, arrived at independently in a Swift app:
//
//     "StressCostTests prints ONE phys_footprint number, at the end of a suite
//      that runs fiftieth in a single-process test binary [...] This walks the
//      ladder in a process of its own and prints the delta at each rung, which
//      is the only form of the number an optimisation can be judged against."
//
// The rungs are the deliverable. Each one is a step a person actually takes —
// launch, see the list, open a thread, open a lot of threads, scroll, close
// everything — and the delta at each says what that step cost. The LAST rung
// is the one that matters most: after closing every tab the app is back in the
// state rung 1 measured, so anything still held is held per thread opened, and
// the ladder is the only instrument here that can see it.
//
// WHAT IT READS, and why there are three columns rather than one:
//
//   * RSS is what the OS charges the process, and it is the number a person
//     sees in Activity Monitor. It is also the WORST of the three at answering
//     "did that come back": free() returns a block to the malloc zone, not to
//     the kernel, so RSS ratchets up and stays. This harness calls
//     malloc_zone_pressure_relief() during every settle, which asks the zones
//     to hand free pages back — without it every rung after a free reads as if
//     nothing was freed.
//   * LIVE BLOCKS is the zone's own in-use count. It moves the instant
//     something is not freed and it moves DOWN the instant something is, so it
//     is the honest answer to "did the close give it back".
//   * LIVE BYTES next to the block count gives the mean block size, which is
//     most of the way to identifying what leaked: ~32B is a map node, a few
//     hundred is a string or a small vector, tens of KB is a buffer.
//
// HOW TO READ A RUNG. A rung's delta is (this reading - the previous one). The
// climb rungs should be positive and roughly proportional to what they added;
// the teardown rung should be negative and roughly equal to the sum of what it
// is undoing. A teardown that returns a fraction of what the opens cost is the
// finding, and `held` on that line names the containers still holding entries.
//
//   HANABI_MEMLADDER=1        run the ladder and exit
//   HANABI_MEM_SESSIONS=<n>   threads to open on the "many threads" rung (8)
//   HANABI_MEM_SETTLE=<n>     frames pumped before each reading (30)
//   HANABI_MEM_REPEAT=<n>     open+close the whole set n times (1)
//
// Everything is behind that first variable and is a hard no-op when unset, the
// same contract as test_hooks.h and soak.h.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <malloc/malloc.h>

#include "soak.h"  // rss_kb(), heap_in_use(), HeapStat

namespace hanabi::memladder {

inline bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_MEMLADDER");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

inline int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    const int parsed = std::atoi(v);
    return parsed > 0 ? parsed : fallback;
}

// Threads opened on the "many threads" rung. Eight is a plausible working set;
// the interesting runs are 100 and 1000, which is what the per-thread cost is
// divided out of.
inline int sessions() { return env_int("HANABI_MEM_SESSIONS", 8); }

// Frames pumped before a reading. A free that happens during a frame is not
// visible until the frame after it, and the immediate-mode tree is rebuilt
// every frame — so a reading taken one frame after a change is reading the
// change plus one frame of scaffolding.
inline int settle_frames() { return env_int("HANABI_MEM_SETTLE", 30); }

inline int repeats() { return env_int("HANABI_MEM_REPEAT", 1); }

// Single opens, each followed by a close. The rung that answers "what does the
// Nth thread you ever opened cost after you closed it".
inline int churn() { return env_int("HANABI_MEM_CHURN", sessions()); }

struct Rung {
    std::string label;
    long rssKb = 0;
    unsigned blocks = 0;
    size_t bytes = 0;
    // Per-container entry counts at this rung, already formatted. This is what
    // turns "something is held" into "s_unread holds 1000 entries".
    std::string held;
};

// Ask every malloc zone to return the free pages it is sitting on.
//
// Without this an RSS column is nearly useless for a teardown rung: the zone
// keeps freed pages for reuse and the process's resident size never falls, so
// a close that gave back every byte reads identically to one that gave back
// none. With it, RSS becomes a real (if lagging and page-granular) answer, and
// the block count is the precise one.
inline void relieve() { malloc_zone_pressure_relief(nullptr, 0); }

// The reading taken before graphics, the app state or a single system exists —
// the floor every other rung is measured above.
//
// It cannot be a `mark()`: marking pumps frames, and there is no loop to pump
// yet. So it is captured by a free call at the very top of the run and adopted
// as rung 0 once the Ladder exists.
struct Floor {
    bool taken = false;
    long rssKb = 0;
    unsigned blocks = 0;
    size_t bytes = 0;
};

inline Floor& floor_reading() {
    static Floor f;
    return f;
}

inline void record_floor() {
    if (!enabled()) return;
    relieve();
    const soak::HeapStat h = soak::heap_in_use();
    floor_reading() = Floor{true, soak::rss_kb(), h.count, h.bytes};
}

class Ladder {
  public:
    // Pumps n frames of the real loop. The caller owns the graphics and system
    // manager; the ladder only needs to be able to advance time.
    using Pump = std::function<void(int)>;
    // What the app holds right now, by container. Read AFTER the settle, never
    // before it: the first version of this took the note as an argument, which
    // C++ evaluates before the call, so every rung reported the state of the
    // rung below it and the catalog appeared to load on the wrong step.
    using Note = std::function<std::string()>;

    Ladder(Pump pump, Note note)
        : pump_(std::move(pump)), note_(std::move(note)) {}

    // Rung 0: the reading record_floor() took before anything existed.
    void adopt_floor(const char* label) {
        const Floor& f = floor_reading();
        if (!f.taken) return;
        rungs_.push_back(Rung{label, f.rssKb, f.blocks, f.bytes, std::string()});
        std::printf("[ladder] %8ld KB  %9u blocks  %8zu KB  %s\n", f.rssKb,
                    f.blocks, f.bytes / 1024, label);
        std::fflush(stdout);
    }

    // Take a reading and record it as a rung. `held` is whatever the caller
    // can say about what is currently retained (see hold_note in main.cpp).
    // `pumpFrames` < 0 uses the configured settle; 0 reads immediately, which
    // is what a rung placed before the first frame needs.
    void mark(std::string label, int pumpFrames = -1) {
        const int n = pumpFrames < 0 ? settle_frames() : pumpFrames;
        if (n > 0) {
            pump_(n);
            relieve();
            pump_(2);
        }
        relieve();
        const soak::HeapStat h = soak::heap_in_use();
        rungs_.push_back(Rung{std::move(label), soak::rss_kb(), h.count,
                              h.bytes, note_ ? note_() : std::string()});
        const Rung& r = rungs_.back();
        std::printf("[ladder] %8ld KB  %9u blocks  %8zu KB  %s\n", r.rssKb,
                    r.blocks, r.bytes / 1024, r.label.c_str());
        if (!r.held.empty())
            std::printf("[ladder]                                          "
                        "     held: %s\n",
                        r.held.c_str());
        std::fflush(stdout);
    }

    const std::vector<Rung>& rungs() const { return rungs_; }

    // The table. Deltas, because a rung's absolute reading includes every rung
    // below it and is therefore the number this harness exists to replace.
    void report() const {
        if (rungs_.empty()) return;
        std::printf("\n===== memory attribution, one clean process =====\n");
        std::printf("[ladder] %10s %9s  %8s %9s  %9s %9s  %s\n", "RSS", "dRSS",
                    "blocks", "dblocks", "bytes", "dbytes", "rung");
        for (size_t i = 0; i < rungs_.size(); ++i) {
            const Rung& r = rungs_[i];
            const long dRss = i == 0 ? 0 : r.rssKb - rungs_[i - 1].rssKb;
            const long dBlk = i == 0 ? 0
                                     : static_cast<long>(r.blocks) -
                                           static_cast<long>(rungs_[i - 1].blocks);
            const long dByt = i == 0 ? 0
                                     : static_cast<long>(r.bytes) -
                                           static_cast<long>(rungs_[i - 1].bytes);
            std::printf("[ladder] %7ld KB %+8ld  %8u %+9ld  %6zu KB %+8ld  %s\n",
                        r.rssKb, dRss, r.blocks, dBlk, r.bytes / 1024,
                        dByt / 1024, r.label.c_str());
            if (!r.held.empty())
                std::printf("[ladder]     held: %s\n", r.held.c_str());
        }
        std::fflush(stdout);
    }

    // What a close actually returned, against what the opens cost.
    //
    // Two rung labels rather than indices: the ladder grows rungs over time and
    // an index would silently start measuring a different pair.
    void compare(const char* fromLabel, const char* peakLabel,
                 const char* backLabel, int opened) const {
        const Rung* from = find(fromLabel);
        const Rung* peak = find(peakLabel);
        const Rung* back = find(backLabel);
        if (from == nullptr || peak == nullptr || back == nullptr) return;
        const long grew = static_cast<long>(peak->bytes) -
                          static_cast<long>(from->bytes);
        const long kept = static_cast<long>(back->bytes) -
                          static_cast<long>(from->bytes);
        const long blkGrew = static_cast<long>(peak->blocks) -
                             static_cast<long>(from->blocks);
        const long blkKept = static_cast<long>(back->blocks) -
                             static_cast<long>(from->blocks);
        std::printf("\n[ladder] opening %d threads cost %ld KB / %ld blocks\n",
                    opened, grew / 1024, blkGrew);
        std::printf("[ladder] closing every tab gave back %ld KB / %ld blocks\n",
                    (grew - kept) / 1024, blkGrew - blkKept);
        std::printf("[ladder] STILL HELD after every tab closed: %ld KB / "
                    "%ld blocks\n",
                    kept / 1024, blkKept);
        if (opened > 0)
            std::printf("[ladder]   => %.0f bytes and %.1f blocks retained per "
                        "thread opened\n",
                        static_cast<double>(kept) / opened,
                        static_cast<double>(blkKept) / opened);
        if (grew > 0)
            std::printf("[ladder]   => %.0f%% of what the opens cost never "
                        "came back\n",
                        100.0 * static_cast<double>(kept) /
                            static_cast<double>(grew));
        std::fflush(stdout);
    }

  private:
    const Rung* find(const char* label) const {
        for (const Rung& r : rungs_)
            if (r.label == label) return &r;
        return nullptr;
    }

    Pump pump_;
    Note note_;
    std::vector<Rung> rungs_;
};

}  // namespace hanabi::memladder
