#pragma once

// ---------------------------------------------------------------------------
// Decode an image to the size it will be DRAWN at, not the size it was saved
// at. afterhours_gaps.md #125.
//
// THE PROBLEM. The whole afterhours texture API is `load_texture(const char*)`.
// It stbi_loads the file at its natural size, uploads that, and returns a
// TextureType whose width/height are the file's. There is no max-dimension, no
// scale factor, no decode-to-fit. draw_texture_pro then scales the full-size
// texture down at draw time, which is correct and free on the GPU -- the cost
// is entirely RESIDENCY. A Retina screen grab is 3024x1964: 23.7 MB of base
// level, 31.6 MB with the mip chain afterhours builds on top of it, to draw an
// image the transcript caps at 420 points tall.
//
// THE ARGUMENT THAT MAKES THIS SAFE. afterhours already box-filters that image
// down into a full mip chain at upload, and the GPU already samples one of
// those reduced levels when it draws it minified -- that is what the chain is
// FOR. So the pixels on screen were never coming from the base level. This
// does the same halving with the same 2x2 box filter, before the upload
// instead of after it, and stops keeping the levels above the one that gets
// sampled. It is not a new resampling; it is declining to store the input to
// one that was already happening.
//
// Halving by powers of two, and only by powers of two, is deliberate. A 2x2
// box halve is exact -- every output pixel is the mean of four inputs, no
// weights, no ringing, no phase shift -- and it is bit-for-bit what
// build_mip_chain does in the vendored backend. An arbitrary-ratio resample
// would be a different filter from the one the GPU uses, so a downscaled image
// and a mip-sampled one would not match.
//
// WHAT IT COSTS. An image whose long side is under the threshold is untouched.
// One that is over it is drawn from a level that still covers the largest box
// the app will ever put it in, so the sampler is still minifying, never
// magnifying -- EXCEPT above ui_scale 2, where the reading column can exceed
// the retained level and the sampler magnifies by up to 1.17x on the long
// side. That is a real cost and it is scored: scripts/compare.py against both
// frozen references, before and after, is in the commit message.
//
// WHY THIS IS IN HANABI AND NOT UPSTREAM. vendor/afterhours is read-only here;
// ~20 projects vendor it. The minimal upstream fix is one optional argument,
// `load_texture(path, max_dimension = 0)`, and it is filed as #125.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <vector>

#include "../rl.h"
#include "../util/autorelease.h"
#include "../util/downscale.h"

namespace hanabi::decode_to_fit {

// Uploads refused because sokol's sampler pool was empty. Cumulative, never
// reset: this should be zero for the life of every process, and one is a bug
// somewhere holding more textures than util/gpu_mem.h's kMaxLiveTextures.
inline std::size_t& pool_exhaustions() {
    static std::size_t n = 0;
    return n;
}

struct Loaded {
    TextureType tex{};
    // The file's own dimensions, which the texture's no longer are. Layout
    // needs these: "never upscale past natural width" is a statement about the
    // IMAGE, not about however many pixels are currently resident for it.
    int naturalW = 0;
    int naturalH = 0;
    int halvings = 0;
};

namespace detail {

// A texture with no sampler cannot be drawn, and afterhours will hand one
// back: load_texture_from_pixels checks sg_make_image and sg_make_view and
// NOT sg_make_sampler, so once sokol's 64-entry sampler pool is empty the
// caller gets valid dimensions, a valid image, a valid view and sampler_id 0.
// Every "did this load?" test in this app -- inline_image::available, the
// composer chip, bubble_height's image term -- reads that as success and then
// draws nothing, or worse.
//
// So reject it here, at the one seam every texture in hanabi comes through,
// and destroy the image and view rather than orphaning two pool slots on the
// way out. Counted, because a silent failure that has been turned into a
// silent failure is not an improvement: the count is what the ladder and the
// soak report.
//
// This should never fire. util/texture_budget.h caps the cache below the pool
// specifically so it cannot, and the static_assert there keeps the two numbers
// in agreement. It is here because the cap is one cache's promise and this is
// the invariant: afterhours_gaps.md #210.
inline void reject_if_unsamplable(TextureType& tex) {
    if (tex.img_id == 0) return;       // already an honest failure
    if (tex.sampler_id != 0) return;   // fine
    const hanabi::AutoreleaseFrame texPool;
    ++pool_exhaustions();
    afterhours::unload_texture(tex);   // frees the image and the view too
    tex = TextureType{};
}

}  // namespace detail

#if defined(AFTER_HOURS_USE_METAL)

// Decode `path`, halve it until it is no larger than it needs to be, and
// upload that.
//
// The pixel upload entry point is afterhours::metal_texture_detail::
// load_texture_from_pixels, which is a BACKEND-PRIVATE namespace: this
// function compiles on the sokol/Metal backend only, which is the one hanabi
// builds (-DAFTER_HOURS_USE_METAL, unconditional in the makefile). That
// coupling is the cost of the workaround and it is #125's, not ours to fix --
// the raylib backend has no equivalent, so the fallback below is what any
// other backend would get.
inline Loaded load(const char* path, int maxDim = hanabi::downscale::kMaxTextureDim) {
    // This function's own pool. The upload hands back autoreleased Metal
    // descriptors and this is reached from a render system (pooled by the
    // frame) and, in principle, from anywhere else; a function that creates a
    // texture cannot know whose scope it is in.
    const hanabi::AutoreleaseFrame texPool;
    Loaded out;
    int w = 0;
    int h = 0;
    int comp = 0;
    // Four channels forced, to match the RGBA8 pixel format the upload uses --
    // the same call afterhours' own load_texture makes. stb_image's
    // implementation is already in this binary (it rides along with the
    // SOKOL_IMPL translation unit, src/sokol_impl.mm), so this shares that one
    // copy rather than linking a second: #125 warns that a consumer bringing
    // its own is one ODR violation from a very confusing bug, and it is right.
    unsigned char* pixels = stbi_load(path, &w, &h, &comp, 4);
    if (pixels == nullptr) return out;

    out.naturalW = w;
    out.naturalH = h;
    const int want = hanabi::downscale::halvings_for(w, h, maxDim);

    if (want == 0) {
        out.tex = afterhours::metal_texture_detail::load_texture_from_pixels(
            pixels, w, h);
        stbi_image_free(pixels);
        detail::reject_if_unsamplable(out.tex);
        return out;
    }

    std::vector<unsigned char> buf;
    const unsigned char* src = pixels;
    int cw = w;
    int ch = h;
    for (int i = 0; i < want; ++i) {
        int nw = 0;
        int nh = 0;
        std::vector<unsigned char> next =
            hanabi::downscale::halve(src, cw, ch, nw, nh);
        buf = std::move(next);
        src = buf.data();
        cw = nw;
        ch = nh;
    }
    // Freed before the upload, not after: at this point the full-resolution
    // buffer is the largest thing in the process and the upload is about to
    // allocate a mip chain beside it.
    stbi_image_free(pixels);
    out.tex =
        afterhours::metal_texture_detail::load_texture_from_pixels(src, cw, ch);
    out.halvings = want;
    detail::reject_if_unsamplable(out.tex);
    return out;
}

#else

// No pixel-upload entry point outside the Metal backend (#125), so every other
// backend gets the library's own behaviour: full-resolution pixels, resident.
inline Loaded load(const char* path, int = hanabi::downscale::kMaxTextureDim) {
    const hanabi::AutoreleaseFrame texPool;
    Loaded out;
    out.tex = afterhours::load_texture(path);
    detail::reject_if_unsamplable(out.tex);
    out.naturalW = static_cast<int>(out.tex.width);
    out.naturalH = static_cast<int>(out.tex.height);
    return out;
}

#endif

}  // namespace hanabi::decode_to_fit
