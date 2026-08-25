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
inline bool census_wanted() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SOAK_CENSUS");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    return on;
}

inline void census() {
    if (!census_wanted()) return;
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

struct Sample {
    int frame = 0;
    double msPerFrame = 0.0;
    double cpuMsPerFrame = 0.0;
    long rssKb = 0;
    size_t entities = 0;
    HeapStat heap;
};

// A bucket's worth of frames, closed out and printed.
inline void report(std::vector<Sample>& out, int frame, double ms, double cpuMs,
                   long rss, size_t ents) {
    const HeapStat h = heap_in_use();
    out.push_back(Sample{frame, ms, cpuMs, rss, ents, h});
    std::printf("[soak] frame %6d  %7.3f ms/f cpu  %7.3f ms/f wall  "
                "RSS %7ld KB  entities %6zu  live %8u blocks / %8zu KB\n",
                frame, cpuMs, ms, rss, ents, h.count, h.bytes / 1024);
    std::fflush(stdout);
}

// The MEDIAN of a window of buckets, per column.
//
// A bucket is one instantaneous reading of a process with async workers that
// rebuilds its whole widget tree every frame, so it carries whatever transient
// allocation happened to be outstanding when the sample landed. Comparing one
// bucket against one other bucket therefore reports the noise as loudly as the
// signal: three consecutive runs of the identical scenario read +704, +656 and
// +416 KB of RSS growth and +5194, +4962 and +4395 live blocks -- a 40%
// spread, from a run that is measuring whether a number grew at all.
//
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
    int frame = 0;
};

inline double median3(double a, double b, double c) {
    return a < b ? (b < c ? b : (a < c ? c : a)) : (a < c ? a : (b < c ? c : b));
}

// One bucket as a window, for a run too short to median. The columns mean the
// same thing; they are just noisier, which is the trade a short run makes.
inline Window window_of(const Sample& x) {
    Window w;
    w.msPerFrame = x.msPerFrame;
    w.cpuMsPerFrame = x.cpuMsPerFrame;
    w.rssKb = static_cast<double>(x.rssKb);
    w.entities = static_cast<double>(x.entities);
    w.blocks = static_cast<double>(x.heap.count);
    w.bytes = static_cast<double>(x.heap.bytes);
    w.frame = x.frame;
    return w;
}

// The three buckets ending at `end` (inclusive index), reduced by median.
inline Window window_at(const std::vector<Sample>& s, size_t end) {
    const Sample& x = s[end - 2];
    const Sample& y = s[end - 1];
    const Sample& z = s[end];
    Window w;
    w.msPerFrame = median3(x.msPerFrame, y.msPerFrame, z.msPerFrame);
    w.cpuMsPerFrame =
        median3(x.cpuMsPerFrame, y.cpuMsPerFrame, z.cpuMsPerFrame);
    w.rssKb = median3(static_cast<double>(x.rssKb), static_cast<double>(y.rssKb),
                      static_cast<double>(z.rssKb));
    w.entities = median3(static_cast<double>(x.entities),
                         static_cast<double>(y.entities),
                         static_cast<double>(z.entities));
    w.blocks = median3(static_cast<double>(x.heap.count),
                       static_cast<double>(y.heap.count),
                       static_cast<double>(z.heap.count));
    w.bytes = median3(static_cast<double>(x.heap.bytes),
                      static_cast<double>(y.heap.bytes),
                      static_cast<double>(z.heap.bytes));
    w.frame = y.frame;  // the middle of the window
    return w;
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
//   HANABI_SOAK_MAX_MS_PER1K         frame-time drift,     ms per 1000 frames
//   HANABI_SOAK_MAX_ENT_PER1K        entity growth,     entities per 1000
// ---------------------------------------------------------------------------
struct Budget {
    double rssKbPer1k = 2048.0;
    double heapKbPer1k = 2048.0;
    double msPer1k = 0.5;
    double entPer1k = 100.0;
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
        return out;
    }();
    return b;
}

// One row of the verdict table, so the printing is uniform and a new metric
// cannot accidentally be reported in a different shape from the rest.
inline bool judge_row(const char* label, const char* unit, double per1k,
                      double budgetPer1k, bool gated) {
    // Per MINUTE at 60fps, because that is the unit the bug report was in
    // ("slower every second"), and 2.8 MB per 1000 frames does not sound
    // alarming until it is 10 MB a minute.
    const double perMinute = per1k * 3.6;
    const bool over = gated && per1k > budgetPer1k;
    char verdictCell[48];
    if (!gated)
        std::snprintf(verdictCell, sizeof(verdictCell), "report-only");
    else if (over)
        std::snprintf(verdictCell, sizeof(verdictCell), "FAIL  %.1fx over budget",
                      budgetPer1k > 0.0 ? per1k / budgetPer1k : 0.0);
    else
        std::snprintf(verdictCell, sizeof(verdictCell), "ok");

    char budgetCell[32];
    if (gated)
        std::snprintf(budgetCell, sizeof(budgetCell), "%.0f", budgetPer1k);
    else
        std::snprintf(budgetCell, sizeof(budgetCell), "%s", "-");

    std::printf("[soak]   %-12s %+11.1f %-4s %+11.1f %-8s %8s  %s\n", label,
                per1k, unit, perMinute, unit, budgetCell, verdictCell);
    return over;
}

// The verdict. Compares the LAST bucket against the SECOND (not the first:
// the first carries lazy-init costs that are not a leak and would make every
// run look like it improved).
//
// WHAT THIS PRINTS AND WHY. The old version printed six deltas and the word
// FAIL. That is enough for whoever wrote the probe and nobody else: it names
// no cause, no reproduction, and no next step, so the first thing the reader
// does is open soak.h — which is exactly the tax this file exists to remove.
// So the failure path now says what grew, by how much, against what budget,
// what shape of bug produces that shape of growth, and the two commands that
// reproduce and localise it.
inline int verdict(const std::vector<Sample>& s) {
    if (s.size() < 3) {
        std::printf("[soak] too few buckets to judge a trend (got %zu, need 3)."
                    " Raise HANABI_SOAK or lower HANABI_SOAK_EVERY.\n",
                    s.size());
        return 0;
    }
    // Six buckets buys the median window below; three is enough to answer the
    // question at all. Degrading rather than refusing matters because the two
    // callers want different runs: `make soak-gate` is deliberately short
    // enough to sit inside `make test`, and refusing to judge a short run
    // means the gate reports "no verdict" -- which the runner then has to
    // decide how to read, and reading it as PASS is how a gate stops gating.
    const bool windowed = s.size() >= 6;
    // The memory columns are MEDIAN-WINDOWED (see window_at): one bucket is
    // one instantaneous reading of a process with async workers, and three
    // consecutive runs of the identical scenario read +704, +656 and +416 KB
    // -- a 40% spread on a measurement of whether a number grew at all.
    //
    // Frame time keeps the plain bucket-2-against-last anchor, and that is
    // deliberate. The memory columns are CUMULATIVE, so a median over them is
    // strictly better than one sample. Frame time is not: the early buckets
    // carry the launch burst (12 ms against a steady state of 4), and bucket 2
    // is exactly what catches it. Windowing it was tried -- it moved the early
    // anchor into the steady state and two runs in three then failed on
    // machine load.
    const Window wa = windowed ? window_at(s, 3) : window_of(s[1]);
    const Window wb = windowed ? window_at(s, s.size() - 1)
                               : window_of(s.back());
    const Sample& a = s[1];
    const Sample& b = s.back();
    const int frames_between = b.frame - a.frame;
    // Per 1000 frames -- ~17 seconds of wall clock at 60fps, which is the
    // scale the report is about ("slower every second").
    const double per1k = frames_between > 0
                             ? 1000.0 / static_cast<double>(frames_between)
                             : 0.0;

    // CPU, not wall: see cpu_nanos() above. Unwindowed, and the anchor is still
    // bucket 2, for the reason the comment on Window gives -- frame time is not
    // cumulative and the early buckets carry the launch burst.
    const double dMs = b.cpuMsPerFrame - a.cpuMsPerFrame;
    const double dWallMs = b.msPerFrame - a.msPerFrame;
    const double dRssKb = wb.rssKb - wa.rssKb;
    const double dEnt = wb.entities - wa.entities;
    const long dBlocks = static_cast<long>(wb.blocks - wa.blocks);
    const double dHeapKb = (wb.bytes - wa.bytes) / 1024.0;

    const Budget bud = budget();

    std::printf("\n[soak] measured over %d frames (%.1f s at 60fps), from "
                "frame %d to frame %d.\n[soak] Everything before frame %d is "
                "excluded: the unmeasured settle pass, plus the\n[soak] first "
                "two buckets, whose numbers carry lazy-init that is not a "
                "leak.\n",
                frames_between, static_cast<double>(frames_between) / 60.0,
                a.frame, b.frame, a.frame);
    std::printf("[soak]   %-12s %-16s %-20s %8s  %s\n", "metric",
                "per 1000 frames", "per minute @60fps", "budget", "verdict");

    int bad = 0;
    // RSS is the metric the reported symptom is actually about: a process that
    // grows without bound is the thing that gets slower and then freezes.
    bad |= judge_row("RSS", "KB", dRssKb * per1k, bud.rssKbPer1k, true) ? 1 : 0;
    // Live malloc bytes move the instant something is not freed, where RSS
    // lags by whole pages -- so on a short run this is the sharper instrument.
    bad |= judge_row("heap bytes", "KB", dHeapKb * per1k, bud.heapKbPer1k, true)
               ? 1 : 0;
    bad |= judge_row("entities", "  ", dEnt * per1k, bud.entPer1k, true) ? 1 : 0;
    bad |= judge_row("frame cpu", "ms", dMs * per1k, bud.msPer1k, true) ? 1 : 0;
    // Wall clock is reported and never gated. It is the number a person feels,
    // and a wall row far above the CPU row is this box being busy, not this app
    // being slow -- which is the single most useful thing to know when a
    // frame-time verdict looks wrong.
    judge_row("frame wall", "ms", dWallMs * per1k, 0.0, false);
    // Block COUNT is reported, never gated: the allocator recycles small
    // blocks in bursts, so on a short run it sawtooths by thousands either way
    // (measured +0 to +4265 per 1000 frames across five clean runs). It is
    // here because dividing bytes by blocks names the SIZE of the leaked
    // thing, which is most of the way to finding it.
    judge_row("heap blocks", "  ", static_cast<double>(dBlocks) * per1k, 0.0,
              false);

    if (bad == 0) {
        std::printf("[soak] PASS: flat over the run.\n");
        return 0;
    }

    std::printf("\n[soak] ---------------- SOAK GATE: FAIL ----------------\n");
    if (dHeapKb * per1k > bud.heapKbPer1k || dRssKb * per1k > bud.rssKbPer1k) {
        std::printf("[soak] The app grew while it sat still. Nothing about "
                    "this run\n[soak] asked it to: the catalog is fixed, the "
                    "window never resizes,\n[soak] and the same frame is drawn "
                    "over and over.\n");
        if (dBlocks > 0 && frames_between > 0) {
            const double perFrame =
                static_cast<double>(dBlocks) / static_cast<double>(frames_between);
            const double meanBytes = (dHeapKb * 1024.0) /
                                     static_cast<double>(dBlocks);
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
    if (dMs * per1k > bud.msPer1k && dRssKb * per1k <= bud.rssKbPer1k) {
        std::printf("[soak]\n[soak] Frame time is climbing while memory is "
                    "FLAT, which is the worse\n[soak] shape: work proportional "
                    "to something that grows without\n[soak] allocating -- a "
                    "counter driving a loop, or a container that is\n[soak] "
                    "reused but never shrunk.\n");
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
    return 1;
}

}  // namespace hanabi::soak
