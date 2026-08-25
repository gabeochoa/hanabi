#pragma once

// ---------------------------------------------------------------------------
// Live malloc blocks, counted by WALKING the zones rather than by asking them.
//
// WHY THIS FILE EXISTS. The soak's live-block column used to be the sum of
// `malloc_zone_statistics().blocks_in_use` over every zone, and that number is
// not a count of live blocks. It is the allocator's own running tally, and on
// this OS it drifts: measured on an IDLE 1200-frame run at a 2000-session
// catalog, with nothing on screen changing and no entity created or destroyed,
// it fell by almost exactly one per frame for nine hundred frames and then
// jumped back up in a single ~930-block step.
//
//   run 1  100580 100483 100384 100265 100165 100066 99966 99864 99766 100696
//   run 2  100724 100727 100728 100727 100709 100710 100710 100710 100710 100712
//
// Two runs of the same binary on the same catalog. One drifts a thousand
// blocks, the other is flat to nineteen. The same walk over the same heap, at
// the same moments, reads 98347 98350 98351 98333 ... 98340 in the first and
// 98346 98350 98350 ... 98332 in the second: flat to nineteen in BOTH.
//
// Setting `MallocNanoZone=0` removes the drift entirely (two runs, spreads 18
// and 20), which places it in the nano allocator's per-magazine object
// counters -- approximate by construction, and re-synced in lumps. The walk
// below does not read those counters, so it does not inherit them.
//
// WHAT IT COST BEFORE THIS: scripts/scroll_gate.sh's live-block arm was red
// about one run in five and had a retry bolted on to absorb it. The "real,
// occasional, one-bucket step of a few hundred live blocks" recorded in that
// script's header is this drift landing inside a bucket half. Nothing in the
// app allocated it.
//
// WHAT THE WALK MISSES. `malloc_get_all_zones` reports one zone here
// ("DefaultMallocZone"), and its enumerator returns about 2,700 fewer blocks
// than the statistics counter claims -- the nano allocator's own arena, which
// the enumerator does not descend into. That arena saturates early and stays
// put (it is the constant offset between the two columns, run after run), so
// the walk is blind to a fixed pool rather than to a growing one. It is not
// blind to leaks: the row-id defect that scroll_gate's blocks arm exists to
// catch reads +8,900 blocks on the walk.
//
// COST. One walk is a traversal of every live block in the process, ~100k of
// them here, and it is done at BUCKET BOUNDARIES only -- once per 250 to 400
// frames. Measured at 3-4 ms, against a bucket that is several hundred frames
// of work.
// ---------------------------------------------------------------------------

#include <malloc/malloc.h>
#include <mach/mach.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hanabi::heapwalk {

struct Live {
    // Exact: one increment per live block the zone enumerator hands back.
    unsigned count = 0;
    size_t bytes = 0;
    // The allocator's own tally, kept for the report-only column. When this
    // and `count` diverge by a moving amount, the divergence is the drift
    // described above and not a leak.
    unsigned approxCount = 0;
    size_t approxBytes = 0;
};

// The enumerator hands ranges to a C callback with a void* context, so the
// accumulator has to travel through it.
struct Accum {
    unsigned count = 0;
    size_t bytes = 0;
    std::unordered_map<size_t, unsigned>* bySize = nullptr;
};

inline void range_recorder(task_t, void* ctx, unsigned type,
                           vm_range_t* ranges, unsigned count) {
    if ((type & MALLOC_PTR_IN_USE_RANGE_TYPE) == 0) return;
    auto* a = static_cast<Accum*>(ctx);
    if (a == nullptr) return;
    for (unsigned i = 0; i < count; ++i) {
        ++a->count;
        a->bytes += ranges[i].size;
        if (a->bySize != nullptr) ++(*a->bySize)[ranges[i].size];
    }
}

// `reader == nullptr` means "the addresses you are about to be given are in
// this process, dereference them directly". That is the only way to run the
// enumerator in-process; every other caller of it is a debugger.
inline void walk(Accum& a) {
    vm_address_t* zones = nullptr;
    unsigned n = 0;
    if (malloc_get_all_zones(mach_task_self(), nullptr, &zones, &n) !=
        KERN_SUCCESS)
        return;
    for (unsigned i = 0; i < n; ++i) {
        auto* z = reinterpret_cast<malloc_zone_t*>(zones[i]);
        if (z == nullptr || z->introspect == nullptr ||
            z->introspect->enumerator == nullptr)
            continue;
        z->introspect->enumerator(mach_task_self(), &a,
                                  MALLOC_PTR_IN_USE_RANGE_TYPE,
                                  reinterpret_cast<vm_address_t>(z), nullptr,
                                  range_recorder);
    }
}

inline Live live() {
    Live out;
    Accum a;
    walk(a);
    out.count = a.count;
    out.bytes = a.bytes;

    vm_address_t* zones = nullptr;
    unsigned n = 0;
    if (malloc_get_all_zones(mach_task_self(), nullptr, &zones, &n) ==
        KERN_SUCCESS) {
        for (unsigned i = 0; i < n; ++i) {
            auto* z = reinterpret_cast<malloc_zone_t*>(zones[i]);
            if (z == nullptr || z->introspect == nullptr) continue;
            malloc_statistics_t st{};
            malloc_zone_statistics(z, &st);
            out.approxCount += st.blocks_in_use;
            out.approxBytes += st.size_in_use;
        }
    }
    // A zone with no usable enumerator would report zero live blocks, which
    // reads as "everything was freed" rather than as "nothing was measured".
    // Fall back rather than lie.
    if (out.count == 0) {
        out.count = out.approxCount;
        out.bytes = out.approxBytes;
    }
    return out;
}

// Live blocks grouped by size class, printed when HANABI_SOAK_SIZES is set.
//
// The total says a thousand more blocks are held than a frame ago and stops
// there. The histogram says how big they are, which is most of an
// identification: a thousand 16-byte blocks and a thousand 512-byte blocks are
// different bugs. This is the diagnostic that found the drift -- it was the
// column that did NOT move.
inline bool sizes_wanted() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SOAK_SIZES");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

inline void dump_sizes(int frame) {
    if (!sizes_wanted()) return;
    std::unordered_map<size_t, unsigned> bySize;
    Accum a;
    a.bySize = &bySize;
    walk(a);
    std::vector<std::pair<size_t, unsigned>> rows(bySize.begin(), bySize.end());
    std::sort(rows.begin(), rows.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });
    for (const auto& [sz, cnt] : rows)
        std::printf("[size] frame %6d  %6zu B  %8u\n", frame, sz, cnt);
    std::fflush(stdout);
}

}  // namespace hanabi::heapwalk
