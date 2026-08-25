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

#include <time.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mach/mach.h>
#include <malloc/malloc.h>

#include "gpu_mem.h"
#include "heap_walk.h"
#include "prof.h"
#include "trend.h"
#include "../../vendor/afterhours/src/core/entity_helper.h"
#include "../../vendor/afterhours/src/plugins/ui/components.h"
#include "../ui/widget_epoch.h"

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

// Drive a named scroll view by `dy` pixels. Returns false when no such view is
// on screen this frame, so a caller can tell "I scrolled nothing" from "I
// scrolled and nothing moved" — the two look identical in the numbers and mean
// opposite things.
//
// By debug name, because that is the one handle an out-of-tree driver has on
// an immediate-mode widget: the entity is rebuilt every frame but `mk()` keeps
// its id stable, so the component and its offset survive.
inline bool scroll_named(const char* debugName, float dy) {
    for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod()) {
        if (!ptr) continue;
        afterhours::Entity& e = *ptr;
        if (!e.has<afterhours::ui::UIComponentDebug>()) continue;
        if (e.get<afterhours::ui::UIComponentDebug>().name_value != debugName)
            continue;
        if (!e.has<afterhours::ui::HasScrollView>()) continue;
        auto& sv = e.get<afterhours::ui::HasScrollView>();
        // Move the OFFSET as well as the eased target, which is exactly what
        // the pane's own jump-to-bottom and minimap-click paths do
        // (`scroll_offset.y = want; set_scroll_target_y(sv, want)`).
        //
        // Target alone is not enough for the transcript, and the reason is
        // worth recording: the transcript pins itself to the bottom while its
        // follow-latch is engaged, and the latch only disengages when the pane
        // OBSERVES the offset decrease at the top of a frame. A driver that
        // writes only the target is overwritten by the pin before the offset
        // ever moves, so the scroll silently does nothing -- measured as
        // byte-identical counters between `idle` and a scrolling run, which is
        // how this was found.
        sv.scroll_target.y += dy;
        sv.scroll_offset.y += dy;
        sv.clamp_scroll();
        return true;
    }
    return false;
}

inline void scroll_sidebar(float dy) { (void)scroll_named("sidebar_scroll", dy); }

// Live allocation count and bytes.
//
// RSS alone cannot tell a leak from a cache that grew once and settled, and it
// moves in page-sized steps that lag the allocation by a long way. The block
// COUNT next to the byte total moves the instant something is not freed, and
// says how big the leaked thing is -- which is most of the way to finding it.
//
// The count is a WALK of the zones, not the zones' own tally: the tally drifts
// by a thousand blocks over a run on which nothing was allocated, and cost
// scroll_gate.sh a one-in-five red before anyone read it. src/util/heap_walk.h
// has the two runs side by side. `approx` is that tally, kept as a
// report-only column so the divergence stays visible.
struct HeapStat {
    unsigned count = 0;
    size_t bytes = 0;
    unsigned approx = 0;
};

inline HeapStat heap_in_use() {
    const hanabi::heapwalk::Live l = hanabi::heapwalk::live();
    return HeapStat{l.count, l.bytes, l.approxCount};
}

// The entity count, broken down by the widget that made them.
//
// The bare total says the catalog is being materialized somewhere and stops
// there -- 300 entities at a 20-row catalog and 3063 at a 2020-row one is a
// fact with no address. Every UI entity carries the debug name its
// ComponentConfig was built with, so grouping the live set by that name turns
// the total into a list of suspects sorted by how much each one costs, which
// is the difference between "something is O(catalog)" and "digest_card is".
//
// Printed once at the end of a soak run (it walks every entity, so it is not
// something to do per frame) and only when HANABI_SOAK_CENSUS is set, because
// the census is a debugging session's question, not a soak's.
//
// The line above the breakdown is the one that says whether the app is paying
// for widgets nobody is drawing: LIVE against BUILT THIS FRAME. The epoch at
// the end of it is the frame counter the stamp uses, and it is there so a
// reader (and scripts/retire_gate.sh) can tell "nothing is stale" from "the
// system that advances the epoch is not registered any more", which otherwise
// produce the same zero. On a screen
// the app has sat on those two are the same number. After navigating away from
// one they are not, and the difference never comes back down on its own
// (afterhours_gaps.md #115).
inline bool census_wanted() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SOAK_CENSUS");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

inline void census() {
    if (!census_wanted()) return;
    const hanabi::widget_epoch::Tally t = hanabi::widget_epoch::tally();
    std::printf("[soak] widgets: %zu live, %zu built this frame, %zu stale, "
                "%zu unstamped (library's own), epoch %u\n",
                t.live, t.built_this_frame, t.stale, t.unstamped,
                hanabi::widget_epoch::epoch());
    std::unordered_map<std::string, int> byName;
    int unnamed = 0;
    for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod()) {
        if (!ptr) continue;
        if (!ptr->has<afterhours::ui::UIComponentDebug>()) {
            ++unnamed;
            continue;
        }
        ++byName[ptr->get<afterhours::ui::UIComponentDebug>().name_value];
    }
    std::vector<std::pair<std::string, int>> rows(byName.begin(), byName.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    std::printf("[soak] entity census (top 20 by debug name):\n");
    const size_t n = rows.size() < 20 ? rows.size() : 20;
    for (size_t i = 0; i < n; ++i)
        std::printf("[soak]   %6d  %s\n", rows[i].second, rows[i].first.c_str());
    std::printf("[soak]   %6d  (no UIComponentDebug)\n", unnamed);
    std::fflush(stdout);
}

// CPU nanoseconds burned by THIS thread, which on the frame loop is the app's
// own work and nothing else.
//
// The soak's frame-time column used to be wall clock, and on this machine wall
// clock does not measure the app. Three other agents build here; the load
// average during these runs ranged from 10 to 34. The same binary running the
// same 6000-frame scroll soak read, bucket by bucket:
//
//   1.788  1.556  1.476  1.748  7.640  7.335  5.592  3.998  2.246  ...
//
// A 5x hump in the middle of a run whose memory columns did not move by a
// kilobyte. Nothing in the app did that; another process did. The verdict row
// built on that column is a coin, which is why its budget had to be set at
// 3.0 ms per 1000 frames -- loose enough that the only frame-time regression
// it can catch is one nobody needs a gate to notice.
//
// CLOCK_THREAD_CPUTIME_ID does not count time this thread was not running, so
// being descheduled costs nothing and a neighbour's build is invisible. It is
// the clock hanabi::prof already uses, and for the same reason; the soak now
// uses it too so that "is the frame getting more expensive?" is a question
// about the frame.
//
// Wall clock is KEPT and still printed. It is the number a person feels, and
// the gap between the two columns is itself the reading that says the box was
// busy rather than the app slow.
inline double cpu_nanos() {
    struct timespec ts {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * 1e9 +
           static_cast<double>(ts.tv_nsec);
}

// The middle of three. Used for the steady-state churn line below; the trend
// machinery does not need it.
inline double median3(double a, double b, double c) {
    return a < b ? (b < c ? b : (a < c ? c : a)) : (a < c ? a : (b < c ? c : b));
}

struct Sample {
    int frame = 0;
    // Wall clock per frame. What a person feels, and what a busy box moves.
    double msPerFrame = 0.0;
    // CPU this thread was actually GIVEN, per frame, from
    // CLOCK_THREAD_CPUTIME_ID. Being descheduled costs nothing here, so this
    // is the column that means the same thing on a quiet machine and a loaded
    // one -- see the note above the gate in verdict().
    double cpuMsPerFrame = 0.0;
    long rssKb = 0;
    size_t entities = 0;
    HeapStat heap;
    // GPU bytes the DRIVER says this process holds, or 0 when no Metal device
    // is answering. Sampled rather than derived, because every other column in
    // this struct comes from the malloc zones and is therefore blind to it:
    // over a ladder rung that leaked 114 MB of textures, live malloc moved
    // +427 KB. afterhours_gaps.md #126.
    long gpuKb = 0;
    // Cumulative operator-new calls at the close of this bucket, and the
    // per-frame rate over the bucket itself.
    //
    // The heap columns above are a LEAK detector: they see only what the frame
    // failed to free. Steady-state churn is invisible to them by construction
    // -- a frame that allocates four thousand times and frees all four
    // thousand reads as perfectly flat, and that frame is the reported symptom
    // ("gets slower and slower") as surely as a leak is. This column is the
    // churn meter, and it is the one number here that is deterministic: the
    // same build on the same catalog allocates the same number of times
    // whatever else the machine is doing, which is what makes it gateable on a
    // box running four other agents' builds.
    unsigned long long allocs = 0;
    double allocsPerFrame = 0.0;
};

// A bucket's worth of frames, closed out and printed.
inline void report(std::vector<Sample>& out, int frame, double ms, double cpuMs,
                   long rss, size_t ents) {
    const HeapStat h = heap_in_use();
    hanabi::heapwalk::dump_sizes(frame);
    const long gpu = static_cast<long>(hanabi::gpu::device_bytes() / 1024);
    const unsigned long long allocs = prof::alloc_count();
    const int prevFrame = out.empty() ? 0 : out.back().frame;
    const unsigned long long prevAllocs = out.empty() ? 0 : out.back().allocs;
    const int span = frame - prevFrame;
    const double perFrame =
        span > 0 ? static_cast<double>(allocs - prevAllocs) /
                       static_cast<double>(span)
                 : 0.0;
    out.push_back(Sample{frame, ms, cpuMs, rss, ents, h, gpu, allocs,
                         perFrame});
    std::printf("[soak] frame %6d  %7.3f ms/f cpu  %7.3f ms/f wall  "
                "RSS %7ld KB  entities %6zu  live %8u blocks / %8zu KB  "
                "(zone tally %u)  GPU %7ld KB",
                frame, cpuMs, ms, rss, ents, h.count, h.bytes / 1024,
                h.approx, gpu);
    if (prof::enabled()) std::printf("  allocs %9.1f /f", perFrame);
    std::printf("\n");
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// THE SLOPE, taken over every bucket past the warm-up.
//
// The estimator itself is util/trend.h -- Theil-Sen, the median of all
// pairwise slopes -- which lives apart from this file so it can be tested
// without a Metal device (tests/unit/test_trend.cpp). What is decided HERE is
// which buckets go into it.
//
// WHICH BUCKETS ARE IN THE FIT: everything after HANABI_SOAK_WARM_FRAMES
// (default 500). Stated in FRAMES and not in buckets, deliberately. The old
// rule was "drop the first two buckets", which means a different amount of
// app time whenever the bucket size changes -- and the bucket size is a knob.
// 500 frames is exactly what the old rule discarded at the gate's 250-frame
// buckets, so the window is unchanged where it was already tuned.
// ---------------------------------------------------------------------------
// util/mem_ladder.h hit the same wall harder (it read +-100%) and solved it the
// same way. Three buckets is enough here because the buckets are already
// averages of 250 frames each; the per-bucket lines printed above are
// untouched, so nothing about what a reader sees changes.
//
// FRAME TIME IS DELIBERATELY NOT WINDOWED, and this is worth writing down
// because the obvious tidy-up is to window it too. It was tried. The memory
// columns are CUMULATIVE -- a leak only ever adds -- so a median window over
// them is strictly better than one sample. Frame time is not cumulative: the
// early buckets carry the launch burst, which on this scenario is 12 ms
// against a steady state of 4 ms, and the old anchor at bucket 2 was catching
// exactly that. Windowing it moved the early anchor into the steady state and
// two runs in three then reported "frame time is trending UP" -- correctly, in
// the sense that the `threads` scenario opens a tab every 30 frames and more
// tabs really are slower, and uselessly, in the sense that a leak detector
// that fails because the machine was busy is a leak detector nobody reads.
// So frame time keeps its old anchor and its old meaning.
struct Window {
    double msPerFrame = 0.0;
    double cpuMsPerFrame = 0.0;
    double rssKb = 0.0;
    double entities = 0.0;
    double blocks = 0.0;
    double bytes = 0.0;
    double allocsPerFrame = 0.0;
    int frame = 0;
};

inline int warm_frames() {
    static const int n = [] {
        const char* v = std::getenv("HANABI_SOAK_WARM_FRAMES");
        const int parsed = (v != nullptr && *v != '\0') ? std::atoi(v) : -1;
        return parsed >= 0 ? parsed : 500;
    }();
    return n;
}

using Trend = hanabi::trend::Trend;

// The buckets past the warm-up. A run entirely inside the warm-up has no fit,
// and falling back to every bucket would quietly measure the launch burst and
// call it a leak -- so it returns nothing and the verdict says so.
inline std::vector<const Sample*> fit_samples(const std::vector<Sample>& s) {
    std::vector<const Sample*> out;
    for (const Sample& x : s)
        if (x.frame > warm_frames()) out.push_back(&x);
    return out;
}

inline Trend trend_of(const std::vector<const Sample*>& pts,
                      double (*value)(const Sample&)) {
    std::vector<hanabi::trend::Point> xy;
    xy.reserve(pts.size());
    for (const Sample* p : pts)
        xy.push_back({static_cast<double>(p->frame), value(*p)});
    return hanabi::trend::theil_sen(xy);
}

// The median-of-three WINDOW machinery that used to soften the two-point
// verdict is gone, along with the two-point verdict itself. The slope above
// takes every bucket, so there is nothing left for a three-bucket window to
// do; `struct Window`, `window_of`, `window_at` and `median3` were removed
// with it rather than left as dead code somebody would later wire back in.
// hanabi::soak::median_of (below) survives because trend_verdict uses it.


// The column accessors, as plain functions so trend_of stays one routine
// rather than a template instantiated five times over the same body.
inline double col_ms(const Sample& x) { return x.msPerFrame; }
inline double col_cpu_ms(const Sample& x) { return x.cpuMsPerFrame; }
inline double col_rss(const Sample& x) { return static_cast<double>(x.rssKb); }
inline double col_ent(const Sample& x) {
    return static_cast<double>(x.entities);
}
inline double col_blocks(const Sample& x) {
    return static_cast<double>(x.heap.count);
}
inline double col_heap_kb(const Sample& x) {
    return static_cast<double>(x.heap.bytes) / 1024.0;
}
inline double col_gpu_kb(const Sample& x) {
    return static_cast<double>(x.gpuKb);
}

// ---------------------------------------------------------------------------
// The budget. What counts as "flat".
//
// Every number here is overridable from the environment, because the same
// probe serves two jobs with very different tolerances: the SHORT run inside
// `make test` (a few hundred frames, thresholds tight enough to catch the
// Metal leak) and the LONG pre-release run (`make soak`, thousands of frames,
// every scenario), where a slow drift has room to show itself.
//
// The defaults below are the LOOSE ones -- a leak detector, not a budget. The
// tight ones live in scripts/soak_gate.sh next to the measurement that set
// them, so the number and its provenance cannot drift apart.
//
//   HANABI_SOAK_MAX_RSS_KB_PER1K     resident growth,      KB per 1000 frames
//   HANABI_SOAK_MAX_HEAP_KB_PER1K    live malloc bytes,    KB per 1000 frames
//   HANABI_SOAK_MAX_MS_PER1K         CPU-time drift,       ms per 1000 frames
//                                    (thread CPU, not wall -- see verdict())
//   HANABI_SOAK_MAX_ENT_PER1K        entity growth,     entities per 1000
//   HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K  live malloc BLOCKS, blocks per 1000
//   HANABI_SOAK_MAX_GPU_KB_PER1K     GPU bytes,            KB per 1000 frames
//
// NOT `HANABI_SOAK_MAX_BLOCKS_PER1K`, which perf/scroll already took for
// trend_verdict's min-of-half rate below. Two budgets over the same quantity,
// measured two different ways, cannot share one name: soak.sh exports 500 for
// the slope and that would silently loosen the other from 250 to 500.
// ---------------------------------------------------------------------------
struct Budget {
    double rssKbPer1k = 2048.0;
    double heapKbPer1k = 2048.0;
    double msPer1k = 0.5;
    double entPer1k = 100.0;
    double blocksPer1k = 20000.0;
    // GPU growth, and this is the tightest budget here by two orders of
    // magnitude, because it is the only column with no noise to absorb.
    // Nothing in a steady-state frame allocates on the GPU: textures are made
    // when an image, an icon atlas or a font atlas is first needed and then
    // held, the render target is made once, and the settle pass has paid all
    // of it. Measured flat to the KILOBYTE across every soak arm. 64 KB is not
    // slack -- it is one 128x128 RGBA texture, the smallest thing whose
    // appearance every frame would be a real defect.
    double gpuKbPer1k = 64.0;
    // A CEILING, not a slope, and the only one here that is. Everything else
    // in this struct asks "is it growing?"; steady-state churn does not grow,
    // it is just paid again every frame forever, so the only question that
    // catches it is "how much?". 0 means report-only, which is the default for
    // the same reason the block column is report-only: the number depends on
    // the catalog and the scenario, so the ceiling belongs next to the run
    // that set it (scripts/alloc_gate.sh), not in a header.
    double allocsPerFrame = 0.0;
};

inline double env_double(const char* name, double fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(v, &end);
    return (end == v) ? fallback : parsed;
}

inline Budget budget() {
    static const Budget b = [] {
        Budget out;
        out.rssKbPer1k = env_double("HANABI_SOAK_MAX_RSS_KB_PER1K", out.rssKbPer1k);
        out.heapKbPer1k = env_double("HANABI_SOAK_MAX_HEAP_KB_PER1K", out.heapKbPer1k);
        out.msPer1k = env_double("HANABI_SOAK_MAX_MS_PER1K", out.msPer1k);
        out.entPer1k = env_double("HANABI_SOAK_MAX_ENT_PER1K", out.entPer1k);
        out.blocksPer1k =
            env_double("HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K", out.blocksPer1k);
        out.gpuKbPer1k =
            env_double("HANABI_SOAK_MAX_GPU_KB_PER1K", out.gpuKbPer1k);
        out.allocsPerFrame =
            env_double("HANABI_SOAK_MAX_ALLOCS_PER_FRAME", out.allocsPerFrame);
        return out;
    }();
    return b;
}

// ---------------------------------------------------------------------------
// THE DIFFABLE REPORT — HANABI_SOAK_REPORT=<path>
//
// A soak run prints twenty lines of numbers that all move a little, so
// comparing two runs means reading them. The point of this file is that a
// regression should be a TEXT DIFF: run the arms, diff against a committed
// baseline, and the diff is either empty or it names what changed.
//
// That only works if a clean run writes the SAME BYTES every time, which
// rules out printing the measurements. So the report carries two kinds of
// line and treats them completely differently:
//
//   THE DETERMINISTIC ONES, exact. Entity count, widget count broken down by
//   the debug name that built it, tabs open, and what the scenario drove.
//   These are properties of the tree and of the script, not of the machine:
//   scripts/scaling_gate.sh measured "348 and 2985 widgets, exactly, every
//   time" over five runs on a box under load 20. A regression that adds a
//   widget per row, or a system that stops tearing an entity down, is one
//   changed line here — and it is caught with no threshold at all, which is
//   the only kind of gate that cannot drift.
//
//   THE MEASURED ONES, banded to `ok` / `OVER 2x` / `OVER 5x` / `OVER 10x`.
//   Deliberately coarse. Printing "+42.3 KB" would make every run differ from
//   every other and the diff would be noise; a band that only moves when a
//   budget is crossed makes the file stable under everything the machine
//   does. The precise numbers are already on stdout for whoever needs them —
//   this file is for the comparison, not for the reading.
//
// Sorted, one `key value` per line, so `diff` output reads as a list of
// changed properties rather than as a rearrangement.
// ---------------------------------------------------------------------------

inline const char* report_path() {
    static const char* p = [] {
        const char* v = std::getenv("HANABI_SOAK_REPORT");
        return (v != nullptr && *v != '\0') ? v : nullptr;
    }();
    return p;
}

// A budget band. Coarse on purpose; see above.
inline const char* band(double per1k, double budget) {
    if (budget <= 0.0) return "ungated";
    if (per1k <= budget) return "ok";
    const double x = per1k / budget;
    if (x < 2.0) return "OVER";
    if (x < 5.0) return "OVER_2x";
    if (x < 10.0) return "OVER_5x";
    return "OVER_10x";
}

// Live widgets grouped by the debug name their ComponentConfig was built with.
// The same walk census() does, returned rather than printed.
inline std::vector<std::pair<std::string, int>> widget_census() {
    std::unordered_map<std::string, int> byName;
    for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod()) {
        if (!ptr) continue;
        if (!ptr->has<afterhours::ui::UIComponentDebug>()) continue;
        ++byName[ptr->get<afterhours::ui::UIComponentDebug>().name_value];
    }
    std::vector<std::pair<std::string, int>> rows(byName.begin(), byName.end());
    // By NAME, not by count: a diff should read as "this widget changed", and
    // sorting by count reshuffles every line whenever one of them moves.
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return rows;
}

struct ReportInput {
    const char* scenario = "none";
    int frames = 0;
    int bucket = 0;
    const char* work = "";
    size_t entities = 0;
    int tabs = -1;
    const char* verdict = "UNKNOWN";
};

inline void write_report(const ReportInput& in, const Trend& rss,
                         const Trend& heap, const Trend& blocks,
                         const Trend& cpu, const Trend& ent, const Trend& gpu,
                         const Budget& bud, int fitPoints) {
    const char* path = report_path();
    if (path == nullptr) return;
    std::FILE* f = std::fopen(path, "w");
    if (f == nullptr) {
        // Loud. A report that silently did not get written turns the next
        // diff into "no change", which is the most expensive wrong answer
        // this file can give.
        std::printf("[soak] COULD NOT WRITE HANABI_SOAK_REPORT to '%s'. No "
                    "report exists,\n[soak] so any diff against it is "
                    "meaningless.\n", path);
        std::fflush(stdout);
        return;
    }
    std::fprintf(f, "scenario %s\n", in.scenario);
    std::fprintf(f, "frames %d\n", in.frames);
    std::fprintf(f, "bucket %d\n", in.bucket);
    std::fprintf(f, "fit_points %d\n", fitPoints);
    std::fprintf(f, "work %s\n", in.work);
    std::fprintf(f, "entities_end %zu\n", in.entities);
    std::fprintf(f, "tabs_end %d\n", in.tabs);
    std::fprintf(f, "band.rss %s\n", band(rss.per1k, bud.rssKbPer1k));
    std::fprintf(f, "band.heap %s\n", band(heap.per1k, bud.heapKbPer1k));
    std::fprintf(f, "band.blocks %s\n", band(blocks.per1k, bud.blocksPer1k));
    std::fprintf(f, "band.cpu %s\n", band(cpu.per1k, bud.msPer1k));
    std::fprintf(f, "band.entities %s\n", band(ent.per1k, bud.entPer1k));
    // Ungated rather than "ok" when no device answered, so a diff against a
    // report from a build without GPU accounting cannot read as a pass.
    std::fprintf(f, "band.gpu %s\n",
                 hanabi::gpu::device_accounting()
                     ? band(gpu.per1k, bud.gpuKbPer1k)
                     : "not_measured");
    std::fprintf(f, "verdict %s\n", in.verdict);
    const auto rows = widget_census();
    int total = 0;
    for (const auto& r : rows) total += r.second;
    std::fprintf(f, "widgets_end %d\n", total);
    for (const auto& r : rows)
        std::fprintf(f, "widget.%s %d\n", r.first.c_str(), r.second);
    std::fclose(f);
    std::printf("[soak] diffable report written to %s\n", path);
    std::fflush(stdout);
}

// One row of the verdict table, so the printing is uniform and a new metric
// cannot accidentally be reported in a different shape from the rest.
inline bool judge_row(const char* label, const char* unit, const Trend& tr,
                      double budgetPer1k, bool gated) {
    // Per MINUTE at 60fps, because that is the unit the bug report was in
    // ("slower every second"), and 2.8 MB per 1000 frames does not sound
    // alarming until it is 10 MB a minute.
    const double perMinute = tr.per1k * 3.6;
    const bool over = gated && tr.per1k > budgetPer1k;
    char verdictCell[48];
    if (!gated)
        std::snprintf(verdictCell, sizeof(verdictCell), "report-only");
    else if (over)
        std::snprintf(verdictCell, sizeof(verdictCell), "FAIL  %.1fx over budget",
                      budgetPer1k > 0.0 ? tr.per1k / budgetPer1k : 0.0);
    else
        std::snprintf(verdictCell, sizeof(verdictCell), "ok");

    char budgetCell[32];
    if (gated)
        std::snprintf(budgetCell, sizeof(budgetCell), "%.0f", budgetPer1k);
    else
        std::snprintf(budgetCell, sizeof(budgetCell), "%s", "-");

    // The rising column is blank below three points, where it can only read
    // 0.00 or 1.00 and means nothing either way. A column that is present but
    // uninformative is worse than an absent one: it gets quoted.
    char risingCell[16];
    if (tr.points >= 3)
        std::snprintf(risingCell, sizeof(risingCell), "%.2f", tr.rising);
    else
        std::snprintf(risingCell, sizeof(risingCell), "%s", "  - ");

    std::printf("[soak]   %-12s %+11.1f %-4s %+11.1f %-8s %8s %6s  %s\n", label,
                tr.per1k, unit, perMinute, unit, budgetCell, risingCell,
                verdictCell);
    return over;
}

// The verdict: is any gated metric climbing across the run?
// ---------------------------------------------------------------------------
// The TREND verdict (HANABI_SOAK_TREND=1). A second reading of the same
// buckets, for the question the anchored verdict above cannot answer.
//
// The anchored verdict subtracts one bucket from another. That is the right
// shape for a leak, which is cumulative and monotone: a bucket 4000 frames
// later is 4000 frames' worth of leak higher, whatever the machine was doing.
// It is the wrong shape for "is the frame getting more expensive", because
// frame time is not cumulative and a single bucket is a sample of a machine
// with three other agents on it. Set a budget loose enough not to flake and it
// catches nothing; set it tight and it fails on a neighbour's build.
//
// So this one reduces each HALF of the run and compares the halves, and it
// reduces frame time by MINIMUM rather than by mean or median. Contention and
// downclocking only ever ADD time to a bucket -- there is no mechanism by
// which a busy machine makes a frame cheaper -- so the minimum bucket of a
// half is the least-polluted estimate of what the app itself costs over that
// half. scripts/perf_ab.sh makes the same argument for the same reason.
//
// And it is a RATIO, not a millisecond. A millisecond threshold on this box is
// a coin flip: the same binary read 8.27 ms on a quiet minute and 16.07 ms on
// a busy one. A ratio between two halves measured minutes apart in the same
// process divides most of that out, and what is left it divides out again by
// taking minima.
//
// Live BLOCKS are the memory half, and they are the right memory metric for a
// scroll: a list that mints a widget per row scrolled past, or a cache keyed
// on the offset, adds blocks long before it adds a page of RSS. They sawtooth
// (the allocator recycles small blocks in bursts), so the halves are reduced
// by MEDIAN and the budget is a rate per 1000 frames rather than a bound on
// the difference.
struct TrendBudget {
    double frameRatio = 1.20;
    double blocksPer1k = 250.0;
};

inline bool trend_wanted() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SOAK_TREND");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

inline TrendBudget trend_budget() {
    static const TrendBudget b = [] {
        TrendBudget out;
        out.frameRatio =
            env_double("HANABI_SOAK_MAX_FRAME_RATIO", out.frameRatio);
        out.blocksPer1k =
            env_double("HANABI_SOAK_MAX_BLOCKS_PER1K", out.blocksPer1k);
        return out;
    }();
    return b;
}

inline double median_of(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Returns 1 when the run trended upward past budget, 0 otherwise.
inline int trend_verdict(const std::vector<Sample>& s) {
    if (!trend_wanted()) return 0;
    // Four buckets is two per half, which is the fewest that can be reduced at
    // all. Six is what the gate runs.
    if (s.size() < 4) {
        std::printf("[soak] trend: too few buckets to compare halves (got "
                    "%zu, need 4).\n", s.size());
        return 0;
    }
    const size_t mid = s.size() / 2;
    double firstMinMs = 0.0, lastMinMs = 0.0;
    std::vector<double> firstBlocks, lastBlocks;
    for (size_t i = 0; i < s.size(); ++i) {
        const bool early = i < mid;
        double& m = early ? firstMinMs : lastMinMs;
        if (m == 0.0 || s[i].cpuMsPerFrame < m) m = s[i].cpuMsPerFrame;
        (early ? firstBlocks : lastBlocks)
            .push_back(static_cast<double>(s[i].heap.count));
    }
    const double ratio = firstMinMs > 0.0 ? lastMinMs / firstMinMs : 0.0;
    const double dBlocks = median_of(lastBlocks) - median_of(firstBlocks);
    const int framesBetween = s.back().frame - s[mid - 1].frame;
    const double blocksPer1k =
        framesBetween > 0
            ? dBlocks * 1000.0 / static_cast<double>(framesBetween)
            : 0.0;
    const TrendBudget bud = trend_budget();

    const bool slowFail = ratio > bud.frameRatio;
    const bool blockFail = blocksPer1k > bud.blocksPer1k;

    std::printf("\n[soak] trend over %zu buckets, first half against last:\n",
                s.size());
    std::printf("[soak]   %-22s %12s %12s  %s\n", "metric", "measured",
                "budget", "verdict");
    std::printf("[soak]   %-22s %11.3fx %11.3fx  %s\n",
                "frame cpu, min-of-half", ratio, bud.frameRatio,
                slowFail ? "FAIL" : "ok");
    std::printf("[soak]   %-22s %+11.1f %11.1f   %s\n",
                "live blocks /1000f", blocksPer1k, bud.blocksPer1k,
                blockFail ? "FAIL" : "ok");
    std::printf("[soak]   (min-of-half cpu: %.3f ms then %.3f ms; blocks: "
                "%.0f then %.0f)\n",
                firstMinMs, lastMinMs, median_of(firstBlocks),
                median_of(lastBlocks));

    if (!slowFail && !blockFail) {
        std::printf("[soak] TREND PASS: the second half of the run costs what "
                    "the first half did.\n");
        return 0;
    }
    std::printf("\n[soak] --------------- SCROLL TREND: FAIL "
                "---------------\n");
    if (slowFail)
        std::printf("[soak] The frame got more expensive as the run went on, "
                    "and this is a\n[soak] ratio of two MINIMA on the thread's "
                    "own CPU clock -- a busy box\n[soak] cannot produce it, "
                    "because contention only ever adds time. Under\n[soak] a "
                    "scroll that means work proportional to how far the list "
                    "has\n[soak] been scrolled: a widget minted per row passed "
                    "and never retired\n[soak] (afterhours_gaps.md #115), a "
                    "memo keyed on the offset, or a\n[soak] window that grows "
                    "instead of sliding.\n");
    if (blockFail)
        std::printf("[soak] Live malloc blocks climbed. Under a scroll the "
                    "usual cause is a\n[soak] per-row allocation that outlives "
                    "the row: a cache keyed on session\n[soak] id with no "
                    "bound reaches one entry per row scrolled past, which\n"
                    "[soak] looks flat on a short list and unbounded on a real "
                    "one.\n");
    std::printf("[soak]\n[soak] To reproduce:  make scroll-gate\n"
                "[soak] What it is:    docs/perf/SCROLL.md\n");
    std::printf("[soak] ------------------------------------------------\n");
    return 1;
}

// The verdict. Compares the LAST bucket against the SECOND (not the first:
// the first carries lazy-init costs that are not a leak and would make every
// run look like it improved).
//
// WHAT THIS PRINTS AND WHY. The old version printed six deltas and the word
// FAIL. That is enough for whoever wrote the probe and nobody else: it names
// no cause, no reproduction, and no next step, so the first thing the reader
// does is open soak.h -- which is exactly the tax this file exists to remove.
// So the failure path says what grew, by how much, against what budget, what
// shape of bug produces that shape of growth, and the two commands that
// reproduce and localise it.
//
// THREE OUTCOMES, NOT TWO. 0 is flat, 1 is a metric over budget, and 2 is
// "this run could not answer the question" -- too few buckets past the
// warm-up to have a slope at all. A probe that reports PASS on no data is a
// probe that has stopped gating, and this one has been in exactly that
// position: with fewer than three buckets the old code printed a sentence and
// returned 0, so `make soak-gate` went green on a run that measured nothing.
// What verdict() measured, handed back so the report writer does not have to
// recompute it (and cannot disagree with what was printed).
struct VerdictTrends {
    Trend rss, heap, blocks, cpu, entities, gpu;
    int fitPoints = 0;
    bool ready = false;
};

inline int verdict(const std::vector<Sample>& s, VerdictTrends& reportOut) {
    const std::vector<const Sample*> pts = fit_samples(s);

    if (pts.size() < 2) {
        std::printf("\n[soak] ---------------- SOAK: INCONCLUSIVE ------------\n");
        std::printf("[soak] %zu bucket(s) landed past the %d-frame warm-up, and a\n"
                    "[soak] trend needs at least two. NOTHING WAS MEASURED. This is\n"
                    "[soak] not a pass: the run was too short, or the buckets too\n"
                    "[soak] large, or the process died before it produced them.\n",
                    pts.size(), warm_frames());
        if (!s.empty())
            std::printf("[soak] Last reading: frame %d, RSS %ld KB, %u live blocks.\n",
                        s.back().frame, s.back().rssKb, s.back().heap.count);
        std::printf("[soak]\n[soak] Raise HANABI_SOAK, lower HANABI_SOAK_EVERY, or lower\n"
                    "[soak] HANABI_SOAK_WARM_FRAMES (currently %d) so buckets land\n"
                    "[soak] inside the measured window.\n", warm_frames());
        std::printf("[soak] ------------------------------------------------\n");
        std::fflush(stdout);
        // reportOut stays `ready = false`, so the report writer emits
        // INCONCLUSIVE rather than a table of zeroes that would diff clean.
        return 2;
    }

    const Trend tMs = trend_of(pts, col_ms);
    const Trend tCpu = trend_of(pts, col_cpu_ms);
    const Trend tRss = trend_of(pts, col_rss);
    const Trend tEnt = trend_of(pts, col_ent);
    const Trend tBlocks = trend_of(pts, col_blocks);
    const Trend tHeap = trend_of(pts, col_heap_kb);
    const Trend tGpu = trend_of(pts, col_gpu_kb);
    const Budget bud = budget();
    const int firstFrame = pts.front()->frame;
    const int lastFrame = pts.back()->frame;
    const int span = lastFrame - firstFrame;

    std::printf("\n[soak] slope over %d buckets from frame %d to frame %d "
                "(%d frames, %.1f s at 60fps).\n",
                tRss.points, firstFrame, lastFrame, span,
                static_cast<double>(span) / 60.0);
    std::printf("[soak] Median of all %ld pairwise slopes (Theil-Sen), so one bad "
                "bucket cannot\n[soak] move the verdict. Everything at or before "
                "frame %d is excluded: the\n[soak] unmeasured settle pass plus the "
                "warm-up, whose growth is lazy-init and\n[soak] not a leak.\n",
                tRss.pairs, warm_frames());
    if (tRss.degraded)
        std::printf("[soak] ONLY TWO BUCKETS: this is a plain two-point delta, which "
                    "carries the\n[soak] full noise of both its endpoints -- the "
                    "thing the slope exists to remove.\n[soak] Treat a marginal "
                    "result here as unmeasured, and lower HANABI_SOAK_EVERY.\n");
    else if (tRss.points < 6)
        // Theil-Sen absorbs a bad bucket by having others to outvote it, and
        // how well it does that is a function of how many it has. Measured on
        // the bigidle arm: at four fit points one clean run in six read +789
        // blocks per 1000 frames against +45 for the other five; at ten fit
        // points the worst of six was +13. Four points is not wrong, it is
        // thin, and a marginal verdict on four should be re-run at more.
        std::printf("[soak] ONLY %d BUCKETS past the warm-up. The median needs "
                    "points to outvote a\n[soak] bad one with, and at four a "
                    "single spiky bucket has been measured moving\n[soak] the "
                    "verdict 17x. A marginal result here should be re-run with "
                    "more\n[soak] frames before it is believed.\n",
                    tRss.points);
    std::printf("[soak]   %-12s %-16s %-20s %8s %6s  %s\n", "metric",
                "slope /1000f", "per minute @60fps", "budget", "rising",
                "verdict");

    int bad = 0;
    // RSS is the metric the reported symptom is actually about: a process that
    // grows without bound is the thing that gets slower and then freezes.
    bad |= judge_row("RSS", "KB", tRss, bud.rssKbPer1k, true) ? 1 : 0;
    // Live malloc bytes move the instant something is not freed, where RSS
    // lags by whole pages -- so on a short run this is the sharper instrument.
    bad |= judge_row("heap bytes", "KB", tHeap, bud.heapKbPer1k, true) ? 1 : 0;
    // GPU bytes, from the device's own counter -- the one memory column the
    // malloc zones cannot see at all. Gated ONLY when a device answered: a
    // column reading +0.0 because no accounting was compiled in is the same
    // glyph as one reading +0.0 because nothing leaked, and passing on the
    // first is how a gate stops gating.
    if (hanabi::gpu::device_accounting())
        bad |= judge_row("GPU bytes", "KB", tGpu, bud.gpuKbPer1k, true) ? 1 : 0;
    else
        std::printf("[soak]   %-12s %-16s %-20s %8s %6s  %s\n", "GPU bytes",
                    "not measured", "", "-", "", 
                    "NO DEVICE -- built without HANABI_GPU_ACCOUNTING?");
    bad |= judge_row("entities", "  ", tEnt, bud.entPer1k, true) ? 1 : 0;
    // FRAME TIME IS GATED ON THE CPU CLOCK, NOT THE WALL CLOCK.
    //
    // This box runs three other agents' builds and its load average has hit
    // 29. Wall clock per frame measures how much of the machine the app was
    // GIVEN; CLOCK_THREAD_CPUTIME_ID measures how much work it DID, and a
    // regression is a change in the second. Measured over 12 clean runs each,
    // 2000 frames, quiet and then under eight synthetic spinners: the wall
    // column's worst slope was +2.6 ms per 1000 frames with no defect present
    // at all, against a budget of 3.0 -- 1.15x of headroom on a metric that
    // was supposed to have some. The CPU column is in the same table in
    // docs/perf/GATES.md and it does not move like that.
    //
    // Wall stays, as report-only. It is what a person feels, and a run where
    // wall climbs while CPU is flat is a real and different finding (the app
    // waiting on something) that a gate on CPU alone would hide.
    bad |= judge_row("cpu time", "ms", tCpu, bud.msPer1k, true) ? 1 : 0;
    judge_row("wall time", "ms", tMs, 0.0, false);
    // LIVE BLOCK COUNT IS GATED, which it never was before.
    //
    // It was report-only because "the allocator recycles small blocks in
    // bursts, so on a short run it sawtooths by thousands either way
    // (measured +0 to +4265 per 1000 frames across five clean runs)". That
    // was true of a two-point delta over a 1000-frame run. It is not true of
    // a median of fifteen pairwise slopes over 2000 frames: 34 clean runs
    // across three load levels read a worst sample of +16.0 blocks per 1000
    // frames, against +9996 from the pool-less binary. Three orders of
    // magnitude of clear air, and it is the SHARPEST of the four -- a leak of
    // one small block a frame moves this long before it moves RSS.
    //
    // Dividing bytes by blocks also names the SIZE of the leaked thing, which
    // is most of the way to finding it, and that is why it was always printed.
    bad |= judge_row("heap blocks", "  ", tBlocks, bud.blocksPer1k, true) ? 1 : 0;

    reportOut.rss = tRss;
    reportOut.heap = tHeap;
    reportOut.blocks = tBlocks;
    reportOut.cpu = tCpu;
    reportOut.entities = tEnt;
    reportOut.gpu = tGpu;
    reportOut.fitPoints = tRss.points;
    reportOut.ready = true;

    // Steady-state churn: a LEVEL, not a slope, so it gets its own line rather
    // than being squeezed into a table whose other rows are all per-1000-frame
    // growth. Only meaningful with HANABI_PROF=1 -- the counter lives behind
    // that flag -- so a run without it says so instead of printing a zero that
    // reads like good news.
    int churnBad = 0;
    if (prof::enabled()) {
        // The median of the last three buckets, or the last one on a run too
        // short for three. The value is the same in every bucket past the
        // warm-up to within one allocation -- the median is not smoothing
        // noise, it is refusing to report a bucket that a killed async load
        // happened to land in.
        const size_t n = s.size();
        const double churn =
            n >= 3 ? median3(s[n - 3].allocsPerFrame, s[n - 2].allocsPerFrame,
                             s[n - 1].allocsPerFrame)
                   : s.back().allocsPerFrame;
        const char* churnVerdict = "report-only";
        char cell[64];
        if (bud.allocsPerFrame > 0.0) {
            if (churn > bud.allocsPerFrame) {
                std::snprintf(cell, sizeof(cell), "FAIL  %.2fx over ceiling",
                              churn / bud.allocsPerFrame);
                churnBad = 1;
            } else {
                std::snprintf(cell, sizeof(cell), "ok  %.0f%% of ceiling",
                              100.0 * churn / bud.allocsPerFrame);
            }
            churnVerdict = cell;
        }
        std::printf("[soak]   %-12s %11.1f  %-34s %8.0f  %s\n", "allocs/frame",
                    churn, "operator new calls, steady state",
                    bud.allocsPerFrame, churnVerdict);
    }
    bad |= churnBad;

    if (bad == 0) {
        std::printf("[soak] PASS: flat over the run.\n");
        std::fflush(stdout);
        return 0;
    }

    std::printf("\n[soak] ---------------- SOAK GATE: FAIL ----------------\n");
    if (churnBad != 0) {
        std::printf("[soak] The frame allocates more than it used to. Nothing "
                    "leaked --\n[soak] every one of these is freed before the "
                    "next frame -- and that is\n[soak] exactly why the heap "
                    "rows above are green. What it costs is\n[soak] malloc "
                    "traffic, forever, at 60 frames a second.\n[soak]\n"
                    "[soak] To see WHERE, with the call sites named:\n"
                    "[soak]   scripts/alloc_sites.sh 2000 300\n[soak]\n"
                    "[soak] The usual causes, in the order they have actually "
                    "turned up here:\n"
                    "[soak]   1. a std::string built per widget per frame for "
                    "a label, an id or\n[soak]      a debug name -- anything "
                    "over 22 characters leaves the SSO\n[soak]      buffer "
                    "and becomes a malloc.\n"
                    "[soak]   2. a container rebuilt by value every frame "
                    "instead of held and\n[soak]      reused, or one grown "
                    "without reserve() to a size already known.\n"
                    "[soak]   3. a std::function whose capture does not fit "
                    "the small-object\n[soak]      buffer, cloned once per "
                    "widget per frame.\n");
    }
    if (tHeap.per1k > bud.heapKbPer1k || tRss.per1k > bud.rssKbPer1k) {
        std::printf("[soak] The app grew while it sat still. Nothing about "
                    "this run\n[soak] asked it to: the catalog is fixed, the "
                    "window never resizes,\n[soak] and the same frame is drawn "
                    "over and over.\n");
        // How much of the run agrees with the slope. A leak adds on every
        // bucket, so a real one reads 1.00 here; noise reads about 0.50. This
        // is the line that says whether to believe the number above it.
        if (tRss.points >= 3)
            std::printf("[soak]\n[soak] %.0f%% of RSS bucket pairs and %.0f%% of "
                        "heap pairs increased. A leak\n[soak] adds on every "
                        "bucket and reads 100%%; noise is a coin and reads "
                        "~50%%.\n",
                        tRss.rising * 100.0, tHeap.rising * 100.0);
        if (tBlocks.per1k > 0.0 && tHeap.per1k > 0.0) {
            const double perFrame = tBlocks.per1k / 1000.0;
            const double meanBytes = (tHeap.per1k * 1024.0) / tBlocks.per1k;
            std::printf("[soak]\n[soak] Shape of it: %.1f live blocks are added "
                        "every frame and never\n[soak] freed, averaging %.0f "
                        "bytes each.\n", perFrame, meanBytes);
        }
        std::printf("[soak]\n[soak] Per-frame allocation with nothing freeing "
                    "it is, in this app,\n[soak] almost always one of three "
                    "things:\n"
                    "[soak]   1. a Metal/Cocoa autoreleased object with no pool "
                    "draining it.\n"
                    "[soak]      Every frame loop in src/main.cpp must open a "
                    "hanabi::Autorelease-\n"
                    "[soak]      Frame (src/util/autorelease.h). Deleting one "
                    "leaks ~2.5 KB a\n"
                    "[soak]      frame -- ~9 MB a minute -- and looks like "
                    "nothing in a diff.\n"
                    "[soak]   2. a cache keyed on something that changes every "
                    "frame, so every\n"
                    "[soak]      frame inserts a new entry and evicts none.\n"
                    "[soak]   3. something appended to a container each frame "
                    "(an event log, a\n"
                    "[soak]      history buffer) with no bound and no drain.\n");
    }
    // GPU growth firing is its own finding, and the first thing to say about
    // it is which columns did NOT move: the malloc counters cannot see a
    // texture, so a green heap column beside a red GPU one means nothing.
    if (tGpu.per1k > bud.gpuKbPer1k) {
        std::printf("[soak]\n[soak] GPU memory grew. That is a texture, a "
                    "render target, a sampler or a\n[soak] render PIPELINE "
                    "created and not destroyed. Note which columns did\n[soak] "
                    "not move: the malloc zones cannot see any of it "
                    "(afterhours_gaps.md #126).\n"
                    "[soak]\n[soak] In this app it is almost always one of:\n"
                    "[soak]   1. inline_image's cache admitting an entry and "
                    "never evicting it\n"
                    "[soak]      (src/ui/inline_image.h -- the bound is a byte "
                    "budget AND an entry\n"
                    "[soak]      cap, so check cached_bytes() and "
                    "cached_count() against the device).\n"
                    "[soak]   2. a texture loaded per frame from a path that "
                    "changes every frame.\n"
                    "[soak]   3. a render target or pipeline recreated on "
                    "resize without an unload\n"
                    "[soak]      -- afterhours_gaps.md #200, which leaks five "
                    "pipelines per resize.\n"
                    "[soak]\n[soak] CEILING WARNING: sokol's pools are FIXED "
                    "(images 128, samplers 64), so\n[soak] a runaway texture "
                    "leak PLATEAUS rather than growing, and this slope goes\n"
                    "[soak] flat once the pool is full. A green GPU column on a "
                    "long run does not\n[soak] prove there is no texture leak; "
                    "it may prove there is no pool left.\n");
    }
    // The block gate firing ALONE is its own finding and needs its own words.
    // It means small allocations are being retained: too small to move RSS
    // (page-granular) and too small to move the byte budget, but a count that
    // only ever goes up. Verified against a build that retains two ~40-byte
    // strings a frame -- RSS +96 KB and heap +128 KB per 1000 frames, both
    // comfortably inside budget, and blocks +2014 at 2.0x over.
    if (tBlocks.per1k > bud.blocksPer1k &&
        tHeap.per1k <= bud.heapKbPer1k && tRss.per1k <= bud.rssKbPer1k) {
        std::printf("[soak] The BLOCK COUNT is climbing while both byte "
                    "budgets are inside\n[soak] theirs. That is a leak of "
                    "SMALL objects: %.0f blocks per 1000\n[soak] frames "
                    "averaging %.0f bytes each.\n",
                    tBlocks.per1k,
                    tBlocks.per1k > 0.0 ? (tHeap.per1k * 1024.0) / tBlocks.per1k
                                        : 0.0);
        std::printf("[soak]\n[soak] Small and retained, in this app, is "
                    "almost always a container keyed\n[soak] by session id "
                    "that nothing erases -- docs/perf/MEMORY.md entry 1 is "
                    "five\n[soak] of them at 0.2 KB a thread. RSS will not "
                    "see that for an hour;\n[soak] this column sees it in "
                    "twenty seconds.\n");
        std::printf("[soak]\n[soak] `HANABI_STRESS=churn` opens and closes "
                    "threads, which is the motion\n[soak] that fills those "
                    "maps. Run it before reading any other arm.\n");
    }
    if (tCpu.per1k > bud.msPer1k && tRss.per1k <= bud.rssKbPer1k) {
        std::printf("[soak]\n[soak] Frame time is climbing while memory is "
                    "FLAT, which is the worse\n[soak] shape: work proportional "
                    "to something that grows without\n[soak] allocating -- a "
                    "counter driving a loop, or a container that is\n[soak] "
                    "reused but never shrunk.\n");
        if (tCpu.points >= 3 && tCpu.rising < 0.75)
            std::printf("[soak]\n[soak] But only %.0f%% of cpu-time pairs "
                        "increased, so the series is not\n[soak] monotone. Re-run before "
                        "believing it.\n", tCpu.rising * 100.0);
    }
    std::printf("[soak]\n[soak] To reproduce and localise:\n"
                "[soak]   make soak-gate              # this exact run again\n"
                "[soak]   make soak                   # the long form, every "
                "scenario\n"
                "[soak]   docs/perf/GATES.md          # what this gate is, and "
                "how to name\n"
                "[soak]                               # the leaking allocation "
                "with `leaks`\n");
    std::printf("[soak] ------------------------------------------------\n");
    std::fflush(stdout);
    return 1;
}

}  // namespace hanabi::soak
