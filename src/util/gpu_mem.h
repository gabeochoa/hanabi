#pragma once

// ---------------------------------------------------------------------------
// GPU memory accounting, because every other instrument in this repo is blind
// to it.
//
// soak.h reads the malloc zones' blocks_in_use / size_in_use. That is the
// instrument that found the Metal autorelease leak in one run, and it is
// completely blind to a texture: over the ladder rung that leaked 114 MB of
// them, live malloc moved +427 KB. Every byte was inside sg_make_image, which
// is an IOGPU resource, not a heap allocation. mem_ladder.h reads process RSS,
// which does see it, as one number for the whole process that attributes
// nothing and lags by whole pages. That is afterhours_gaps.md #126.
//
// This file gives the app two numbers instead, and they are deliberately of
// different kinds:
//
//   device_bytes()  GROUND TRUTH. -[MTLDevice currentAllocatedSize], reached
//                   through sokol's own sg_mtl_device(). Every GPU byte the
//                   process holds -- the font atlas, the glyph textures, the
//                   offscreen render target, sokol's vertex buffers and
//                   pipelines, and every texture hanabi loaded. Nothing is
//                   estimated and nothing is missed. It cannot say WHOSE bytes
//                   they are.
//
//   ledger()        ATTRIBUTION. What hanabi asked for, counted where hanabi
//                   asks for it. It knows the caller; it does not know what
//                   the driver did with the request. Every byte in it is also
//                   in device_bytes(), so the two are a pair: the ledger says
//                   what should be there and the device says what is.
//
// The gap between them is the point. A ledger that stays flat while the device
// climbs is a leak in something hanabi does not own (or a texture hanabi
// dropped without unloading). A ledger that climbs is hanabi's own cache.
//
// WHY texture_bytes() IS NOT w*h*4. That was the old estimate, and it is 25%
// low, every time, for a reason nothing in the app said out loud:
// afterhours' load_texture builds and uploads a full box-filtered MIP CHAIN
// (backends/sokol/drawing_helpers.h, build_mip_chain -- sokol has no runtime
// mipmap generation, so levels have to be supplied at image creation). A
// 640x480 RGBA8 image is 1,228,800 bytes of base level and 1,638,352 bytes
// resident. Measured against the device counter on this machine, w*h*4
// under-reports by exactly the chain.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

// HANABI_GPU_ACCOUNTING is defined by the app build (see makefile), which is
// the only build that links src/gpu_mem.mm -- the one translation unit where
// an Objective-C type appears. The unit-test binaries link this header for the
// arithmetic and must NOT drag in Metal, sokol and a GPU device to get it, so
// they compile the other branch and report "not measured".
//
// A compile-time switch rather than a weak symbol, and that was not the first
// attempt: `__attribute__((weak))` on an undefined function is still a hard
// reference on Mach-O and the link fails, and a weak DEFINITION in the header
// is worse -- at -O2 the compiler can inline the fallback and constant-fold
// the accounting to zero inside whichever translation unit saw it, which is a
// fake green that only appears in the optimised build.
#if defined(HANABI_GPU_ACCOUNTING)
extern "C" unsigned long long hanabi_metal_allocated_bytes(void);
#endif

namespace hanabi::gpu {

// ---------------------------------------------------------------------------
// THE OTHER GPU LIMIT, and it is not bytes.
//
// sokol allocates every GPU object out of FIXED-SIZE pools, sized in sg_desc
// at sg_setup. afterhours calls sg_setup with a default-constructed sg_desc
// (backends/sokol/backend.h) and exposes no hook, so a consumer gets the
// library defaults and cannot raise them: afterhours_gaps.md #210.
//
//     images 128 | samplers 64 | views 256 | pipelines 64 | shaders 32
//
// afterhours' load_texture makes ONE OF EACH of image, view and sampler per
// texture, so the SAMPLER pool is the binding constraint at 64 -- half the
// image pool.
//
// MEASURED, on this machine, in a process that has done exactly what hanabi's
// launch does (graphics::init at 1180x949 plus Preload's four font faces):
// loading textures in a loop, the sampler pool ran out after 61 and the image
// pool after 124. So three sampler slots are already spoken for before the app
// draws anything, and 61 is the real ceiling on textures hanabi may hold at
// once.
//
// WHAT EXHAUSTION LOOKS LIKE, which is the part that makes this dangerous:
// load_texture_from_pixels checks the image and the view and NOT the sampler.
// Past the 61st texture it returns a TextureType with a valid image, a valid
// view, sampler_id == 0 and the file's real width and height -- so every
// "did this load?" test in the app says yes and the thing cannot be sampled.
// Past the 124th the image fails too and the load reports itself honestly.
// Sixty-odd textures of silent wrongness sit between those two points.
inline constexpr std::size_t kSokolSamplerPool = 64;
inline constexpr std::size_t kSokolImagePool = 128;

// Sampler slots NOT available to a texture cache: the three sokol/fontstash
// take before the app starts, plus headroom for the icon atlas, any font
// atlas page fontstash adds as glyphs are rasterised, and an offscreen render
// target's own sampler. Deliberately generous -- the cost of reserving too
// many is a smaller image cache, and the cost of reserving too few is an
// image that silently does not draw.
inline constexpr std::size_t kReservedSamplers = 16;

// The most textures anything in hanabi may hold at once.
inline constexpr std::size_t kMaxLiveTextures =
    kSokolSamplerPool - kReservedSamplers;

// Resident bytes for one RGBA8 texture as afterhours actually uploads it:
// the base level plus every mip level down to 1x1. Each level halves both
// dimensions, floored at 1, which is what build_mip_chain does.
inline std::size_t texture_bytes(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    std::size_t total = static_cast<std::size_t>(w) *
                        static_cast<std::size_t>(h) * 4u;
    int pw = w;
    int ph = h;
    while (pw > 1 || ph > 1) {
        pw = pw > 1 ? pw / 2 : 1;
        ph = ph > 1 ? ph / 2 : 1;
        total += static_cast<std::size_t>(pw) *
                 static_cast<std::size_t>(ph) * 4u;
    }
    return total;
}

// True when a Metal device is answering, i.e. device_bytes() is a measurement
// rather than a default. A caller printing the number must say which it got:
// "0 KB" and "not measured" look identical in a column and mean the opposite
// things, and docs/perf/GATES.md already has a section on a gate that reported
// a killed process as a failure for exactly that reason.
inline unsigned long long device_bytes() {
#if defined(HANABI_GPU_ACCOUNTING)
    return hanabi_metal_allocated_bytes();
#else
    return 0ull;
#endif
}

inline bool device_accounting() { return device_bytes() > 0ull; }

// What hanabi believes it is holding, and how it got there. `loads` and
// `unloads` are cumulative and never decrease, so their difference is the
// live count and their sum is the churn -- a cache that is thrashing reads as
// a flat `bytes` with a climbing `loads`, which no byte counter can show.
struct Ledger {
    std::size_t bytes = 0;
    std::size_t live = 0;
    std::uint64_t loads = 0;
    std::uint64_t unloads = 0;
    std::size_t peakBytes = 0;
};

inline Ledger& ledger() {
    static Ledger l;
    return l;
}

inline void note_load(std::size_t bytes) {
    Ledger& l = ledger();
    l.bytes += bytes;
    ++l.live;
    ++l.loads;
    if (l.bytes > l.peakBytes) l.peakBytes = l.bytes;
}

inline void note_unload(std::size_t bytes) {
    Ledger& l = ledger();
    l.bytes = bytes <= l.bytes ? l.bytes - bytes : 0;
    if (l.live > 0) --l.live;
    ++l.unloads;
}

inline std::size_t ledger_bytes() { return ledger().bytes; }
inline std::size_t ledger_live() { return ledger().live; }

}  // namespace hanabi::gpu
