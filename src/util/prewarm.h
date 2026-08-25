#pragma once

// ---------------------------------------------------------------------------
// Pre-warm: pay a GPU first-use cost before the first frame anybody measures.
//
// afterhours_gaps.md #155. The first draws after graphics::init cost several
// times a warm draw and nothing in the graphics API acknowledges it -- no
// prewarm(), no precompile(), no way to submit a frame that is not presented.
// Measured per frame (util/launch_curve.h): 3.6-4.1x on frame 0, decaying over
// about five frames, 5.4-6.4 ms of CPU above warm in total.
//
// WHAT IS HERE, AND WHY IT IS ONE THING RATHER THAN THREE.
//
// The obvious pre-warm is three steps: build the icon atlas and its pipeline,
// rasterise the glyph atlas, and render one throwaway frame that touches every
// draw path. All three were built and measured against the launch curve, one
// at a time. Two of them cost more than they save, and the numbers are in the
// commit that added this file. Only the first is here, because only the first
// is FREE:
//
//   step                      CPU cost   CPU saved   FirstFrame p50
//   icon atlas + pipeline       0.52 ms    0.53 ms     51 -> 50 ms
//   glyph atlas, 14 sizes       7.05 ms    1.50 ms     51 -> 62 ms
//   one throwaway frame         2.37 ms    1.63 ms     51 -> 59 ms
//
// The lesson generalises and is worth stating, because the next person will
// have the same idea: a pre-warm CANNOT reduce the cost of a launch. Every
// millisecond of it is on the critical path between process start and the
// first frame, so moving work earlier only helps if the work moved would
// otherwise be done TWICE, or done more than the app needs. The glyph warm
// rasterises 14 sizes across two faces to save a frame that draws four of
// them; the throwaway frame compiles pipelines the first real frame would have
// compiled anyway. The icon atlas is the one case where the answer is
// genuinely the same work at a different time, and it measures as free.
//
// WHAT IT BUYS, since it is not milliseconds. icons.h creates its sgl blend
// pipeline lazily, inside the first frame that happens to draw an icon, and
// decodes icons.png on that same frame. sgl_make_pipeline reaches
// sg_make_pipeline, which on Metal compiles a render pipeline state. So the
// cost lands in whichever frame drew an icon first -- and #155's sharpest
// complaint is exactly that: a launch number that moves when you reorder code
// doing the same work is not a number. Pinning it here makes it one.
//
// Off with HANABI_PREWARM=0, which exists so the A/B could be run interleaved
// on ONE binary rather than against two builds -- docs/perf/STARTUP.md is
// emphatic that on this box two batches measure the box.
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <string>

#include "../ui/icons.h"

namespace hanabi::prewarm {

inline bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_PREWARM");
        return !(v != nullptr && std::string(v) == "0");
    }();
    return on;
}

// Call once, after the systems are built and before the first frame anybody
// measures. Idempotent: both calls are memoised by AtlasTexture, so this is
// not extra work even when something has already drawn an icon.
inline void run() {
    if (!enabled()) return;
    (void)hanabi::icons::AtlasTexture::get().ensure();
    (void)hanabi::icons::AtlasTexture::get().blend_pipeline();
}

}  // namespace hanabi::prewarm
