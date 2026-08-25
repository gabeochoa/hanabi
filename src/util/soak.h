#pragma once

// ---------------------------------------------------------------------------
// The soak probe: does the app get slower, or heavier, the longer it runs?
//
// Reported symptom: "it just gets slower and slower every second until it
// freezes". Nothing in the test suite could have caught that. `make test`
// renders 45 frames and asserts on the 45th; `measure_launch.sh` gates the
// FIRST frame and peak RSS over a run that lasts under a second. Both are
// budgets on a young process, and a leak is a young process looking fine.
//
// So this measures the SLOPE instead of the value. It runs the real system
// manager for thousands of frames and reports, per bucket, the frame time and
// the process's resident size — and then whether either is trending up.
//
// WHY IT LIVES IN THE APP rather than in a script: a leak lives in the ECS and
// in the caches, and nothing outside the process can see an entity count. The
// whole thing is behind HANABI_SOAK and is a hard no-op when unset, the same
// contract as test_hooks.h.
//
//   HANABI_SOAK=<frames>   run that many frames, report, exit
//   HANABI_SOAK_EVERY=<n>  bucket size (default 250)
//
// Reading the output: RSS that climbs and never plateaus is a leak. Frame time
// that climbs with it is the leak being walked over every frame — an
// unbounded container that something iterates. Frame time climbing with FLAT
// RSS is different and worse to find: work proportional to something that
// grows without allocating, like a counter driving a loop.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <mach/mach.h>
#include <malloc/malloc.h>

#include "../../vendor/afterhours/src/core/entity_helper.h"
#include "../../vendor/afterhours/src/plugins/ui/components.h"

namespace hanabi::soak {

// Frames to run, or 0 when the probe is off. Read once.
inline int frames() {
    static const int n = [] {
        const char* v = std::getenv("HANABI_SOAK");
        return (v != nullptr && *v != '\0') ? std::atoi(v) : 0;
    }();
    return n;
}

inline int bucket() {
    static const int n = [] {
        const char* v = std::getenv("HANABI_SOAK_EVERY");
        const int parsed = (v != nullptr && *v != '\0') ? std::atoi(v) : 0;
        return parsed > 0 ? parsed : 250;
    }();
    return n;
}

// Resident size in KB. mach_task_basic_info rather than /usr/bin/time, because
// the whole point is to sample it repeatedly from inside the run.
inline long rss_kb() {
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return static_cast<long>(info.resident_size / 1024);
}

// Drive the sidebar's scroll view by `dy` pixels.
//
// By debug name, because that is the one handle an out-of-tree driver has on
// an immediate-mode widget: the entity is rebuilt every frame but `mk()` keeps
// its id stable, so the component and its offset survive.
inline void scroll_sidebar(float dy) {
    for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod()) {
        if (!ptr) continue;
        afterhours::Entity& e = *ptr;
        if (!e.has<afterhours::ui::UIComponentDebug>()) continue;
        if (e.get<afterhours::ui::UIComponentDebug>().name_value !=
            "sidebar_scroll")
            continue;
        if (!e.has<afterhours::ui::HasScrollView>()) continue;
        auto& sv = e.get<afterhours::ui::HasScrollView>();
        sv.scroll_target.y += dy;
        sv.clamp_scroll();
        return;
    }
}

// Live allocation count and bytes, from the malloc zones themselves.
//
// RSS alone cannot tell a leak from a cache that grew once and settled, and it
// moves in page-sized steps that lag the allocation by a long way. The zone's
// own in-use count moves the instant something is not freed, and the BLOCK
// COUNT next to the byte total says how big the leaked thing is -- which is
// most of the way to finding it.
struct HeapStat {
    unsigned count = 0;
    size_t bytes = 0;
};

inline HeapStat heap_in_use() {
    HeapStat out;
    vm_address_t* zones = nullptr;
    unsigned n = 0;
    if (malloc_get_all_zones(mach_task_self(), nullptr, &zones, &n) !=
        KERN_SUCCESS)
        return out;
    for (unsigned i = 0; i < n; ++i) {
        auto* z = reinterpret_cast<malloc_zone_t*>(zones[i]);
        if (z == nullptr || z->introspect == nullptr) continue;
        malloc_statistics_t st{};
        malloc_zone_statistics(z, &st);
        out.count += st.blocks_in_use;
        out.bytes += st.size_in_use;
    }
    return out;
}

struct Sample {
    int frame = 0;
    double msPerFrame = 0.0;
    long rssKb = 0;
    size_t entities = 0;
    HeapStat heap;
};

// A bucket's worth of frames, closed out and printed.
inline void report(std::vector<Sample>& out, int frame, double ms, long rss,
                   size_t ents) {
    const HeapStat h = heap_in_use();
    out.push_back(Sample{frame, ms, rss, ents, h});
    std::printf("[soak] frame %6d  %7.3f ms/f  RSS %7ld KB  entities %6zu  "
                "live %8u blocks / %8zu KB\n",
                frame, ms, rss, ents, h.count, h.bytes / 1024);
    std::fflush(stdout);
}

// The verdict. Compares the LAST bucket against the SECOND (not the first:
// the first carries lazy-init costs that are not a leak and would make every
// run look like it improved).
inline int verdict(const std::vector<Sample>& s) {
    if (s.size() < 3) {
        std::printf("[soak] too few buckets to judge a trend\n");
        return 0;
    }
    const Sample& a = s[1];
    const Sample& b = s.back();
    const double dMs = b.msPerFrame - a.msPerFrame;
    const double dRss = static_cast<double>(b.rssKb - a.rssKb);
    const long dEnt = static_cast<long>(b.entities) -
                      static_cast<long>(a.entities);
    const int frames_between = b.frame - a.frame;
    // Per 1000 frames -- ~17 seconds of wall clock at 60fps, which is the
    // scale the report is about ("slower every second").
    const double per1k = frames_between > 0
                             ? 1000.0 / static_cast<double>(frames_between)
                             : 0.0;

    std::printf("\n[soak] over %d frames after warmup:\n", frames_between);
    std::printf("[soak]   frame time  %+7.3f ms  (%+.3f ms per 1000 frames)\n",
                dMs, dMs * per1k);
    std::printf("[soak]   RSS         %+7.0f KB  (%+.0f KB per 1000 frames)\n",
                dRss, dRss * per1k);
    std::printf("[soak]   entities    %+7ld     (%+.0f per 1000 frames)\n",
                dEnt, static_cast<double>(dEnt) * per1k);
    const long dBlocks = static_cast<long>(b.heap.count) -
                         static_cast<long>(a.heap.count);
    const long dBytes = static_cast<long>(b.heap.bytes) -
                        static_cast<long>(a.heap.bytes);
    std::printf("[soak]   live blocks %+7ld     (%+.0f per 1000 frames)\n",
                dBlocks, static_cast<double>(dBlocks) * per1k);
    std::printf("[soak]   live bytes  %+7ld KB  (%+.0f KB per 1000 frames)\n",
                dBytes / 1024, static_cast<double>(dBytes) / 1024.0 * per1k);
    if (dBlocks > 0) {
        // The size of the thing being leaked, which is the strongest single
        // clue available from outside: a 32-byte leak is a node in a map, a
        // few-hundred-byte one is a string or a small vector, a huge one is a
        // buffer.
        std::printf("[soak]   => mean leaked block %.0f bytes\n",
                    static_cast<double>(dBytes) / static_cast<double>(dBlocks));
    }

    // Thresholds are deliberately loose: this is a LEAK detector, not a
    // budget. A leak of any size grows without bound, so over a few thousand
    // frames it clears these by a mile; noise does not.
    int bad = 0;
    if (dMs * per1k > 0.5) {
        std::printf("[soak] FAIL: frame time is trending UP\n");
        bad = 1;
    }
    if (dRss * per1k > 2048.0) {
        std::printf("[soak] FAIL: RSS is trending UP (>2 MB per 1000 frames)\n");
        bad = 1;
    }
    if (static_cast<double>(dEnt) * per1k > 100.0) {
        std::printf("[soak] FAIL: entity count is trending UP\n");
        bad = 1;
    }
    if (bad == 0) std::printf("[soak] PASS: flat over the run\n");
    return bad;
}

}  // namespace hanabi::soak
