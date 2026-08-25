#pragma once

// Inline image rendering for the transcript (agent surface): the agent can
// attach a local image (e.g. a screenshot) to a message; the transcript decodes
// it once, caches the GPU texture, and draws it inline under the message text.
//
// afterhours provides load_texture(path) (stb_image decode -> GPU texture) and
// draw_texture_pro (textured quad). This adds a path->texture cache so a
// scrolling transcript doesn't re-decode every frame, plus a fit helper that
// scales an image into the reading column (never upscaling past natural width;
// capping tall images so a screenshot doesn't dominate).
//
// ---------------------------------------------------------------------------
// THE CACHE HAS A BUDGET, AND THIS IS WHY.
//
// It used to be one line -- `static std::unordered_map<std::string, Cached>
// cache;` -- with the word "bounded" in the comment above it and nothing
// bounding it. Every distinct path the app ever drew stayed decoded on the GPU
// for the life of the process. The composer's attachment LIST is capped at
// five (AppComponent::kMaxAttachments); removing a chip removed the chip and
// kept the texture.
//
// MEASURED on the memory ladder (HANABI_MEM_IMAGE_DIR): sixty 640x480 PNGs
// attached and removed ONE AT A TIME, so the composer holds zero attachments
// at the end:
//
//     RSS  42,752 KB  ->  159,680 KB     +114 MB
//     live malloc     +427 KB, +677 blocks
//
// 1.9 MB of resident memory per image, held after every chip was removed. Note
// the second line: a GPU texture is not a malloc block, so the soak probe's
// live-block counter -- the instrument that found the Metal autorelease leak
// -- is completely blind to this one. Only RSS sees it.
//
// A 640x480 test image is small. A Retina screen grab is 3024x1964, which is
// 23.7 MB of RGBA: paste five, remove them, and 119 MB never comes back.
//
// THE BOUND. A byte budget over decoded pixels (w*h*4), evicting
// least-recently-used and calling unload_texture, WITH one refinement that
// matters more than the budget: an entry touched within the last
// kProtectRecent accesses is never evicted. One frame draws at most a handful
// of images, so this guarantees that whatever is on screen right now survives
// the insert of something new -- without it, a working set larger than the
// budget would evict what it is about to draw and re-decode a PNG every single
// frame, which is far worse than the memory it saves.
//
// WHAT THE BOUND COSTS: an image that has not been drawn in a while is dropped
// and re-decoded the next time it appears, which is one slow frame on the
// scroll back to it. Nothing is lost and nothing changes on screen.
//
// AND THE PER-IMAGE COST, which the budget above does nothing about. hanabi
// used to hold FULL-RESOLUTION pixels to draw a 22px composer chip or a
// 420pt-tall inline image, because afterhours' load_texture takes a path and
// nothing else. It now decodes to the size it draws at -- see
// ui/decode_to_fit.h for the mechanism and for why halving by powers of two is
// the same filter the GPU was already going to sample through. A 3024x1964
// screen grab went from 31.6 MB resident to 7.9 MB, and the budget above now
// holds four times as many images in the same bytes.
//
// afterhours_gaps.md #125 stays open: the upload entry point that makes this
// possible is in a BACKEND-PRIVATE namespace, so the workaround compiles on
// the Metal backend only.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

#include "../rl.h"     // global TextureType / RectangleType / Vector2Type + draw_*
#include "../util/autorelease.h"     // a texture load autoreleases; pool it
#include "../util/gpu_mem.h"        // texture_bytes() + the GPU ledger
#include "../util/texture_budget.h"  // the LRU policy, with no texture in it
#include "decode_to_fit.h"           // decode to the size it is DRAWN at (#125)
#include "theme.h"     // theme::Color (== afterhours::Color) for the tint

namespace hanabi::inline_image {

// THIS BUDGET USED TO BE A THIRD BIGGER THAN IT SAID. It counted w*h*4, and
// afterhours' load_texture uploads a full mip chain on top of that (see
// util/gpu_mem.h). Measured with the device's own counter over the ladder's
// image rung -- 60 640x480 PNGs attached and removed one at a time:
//
//     estimate said        32,400 KB
//     device actually held 48,384 KB     1.49x
//
// It now budgets against hanabi::gpu::texture_bytes, which counts the chain,
// so a 32 MB budget costs 32 MB of GPU memory.
//
// The three numbers live in util/texture_budget.h beside the policy that
// enforces them, so tests/unit/test_texture_budget.cpp asserts against the
// values this cache actually runs with rather than a copy that can drift.
inline constexpr std::size_t kMaxCachedBytes = hanabi::texbudget::kDefaultMaxBytes;
inline constexpr std::size_t kProtectRecent =
    hanabi::texbudget::kDefaultProtectRecent;
inline constexpr std::size_t kMaxEntries = hanabi::texbudget::kDefaultMaxEntries;

struct Cached {
    TextureType tex{};
    bool tried = false;  // attempted a load (don't retry a bad path every frame)
    // The FILE's dimensions, which tex's are no longer: an image over the
    // draw-size threshold is halved before upload (decode_to_fit.h). Every
    // layout decision below is about the image, not about how many pixels
    // happen to be resident for it, so it reads these -- getting that wrong
    // would silently upscale a downscaled screenshot to fill the column.
    int naturalW = 0;
    int naturalH = 0;
};

namespace detail {

// The LRU itself lives in util/texture_budget.h, with no texture in it, so the
// policy can be asserted by a test that links no GPU. What is left here is the
// part that genuinely needs afterhours: the path -> TextureType map, the load,
// and the unload on eviction.
struct Store {
    std::unordered_map<std::string, Cached> map;
    hanabi::texbudget::Budget budget{kMaxCachedBytes, kProtectRecent,
                                     kMaxEntries};
};

inline Store& store() {
    static Store s;
    return s;
}

inline void evict(Store& s) {
    // The unload's own pool: this is reached from get(), which a render system
    // calls inside a pooled frame today and a cache warm-up could call outside
    // one tomorrow. A function that touches Metal owns its pool.
    const hanabi::AutoreleaseFrame texPool;
    s.budget.trim([&s](const std::string& victim, std::size_t bytes) {
        auto found = s.map.find(victim);
        if (found == s.map.end()) return;
        hanabi::gpu::note_unload(bytes);
        afterhours::unload_texture(found->second.tex);
        s.map.erase(found);
    });
}

}  // namespace detail

inline TextureType& get(const std::string& path) {
    detail::Store& s = detail::store();
    auto it = s.map.find(path);
    if (it != s.map.end()) {
        s.budget.touch(path);
        return it->second.tex;
    }
    Cached c;
    c.tried = true;
    const hanabi::decode_to_fit::Loaded loaded =
        hanabi::decode_to_fit::load(path.c_str());
    c.tex = loaded.tex;
    c.naturalW = loaded.naturalW;
    c.naturalH = loaded.naturalH;
    const std::size_t bytes =
        hanabi::gpu::texture_bytes(static_cast<int>(c.tex.width),
                                   static_cast<int>(c.tex.height));
    hanabi::gpu::note_load(bytes);
    s.map[path] = c;
    s.budget.insert(path, bytes);
    detail::evict(s);
    // trim() never drops an entry inside the protection window, and this one
    // was just touched, so the reference is live.
    return s.map[path].tex;
}

// Decoded bytes held, and how many paths (instrumentation; the ladder prints
// these so a bound is a number rather than a claim).
inline std::size_t cached_bytes() { return detail::store().budget.bytes(); }
inline std::size_t cached_count() { return detail::store().map.size(); }

inline bool available(const std::string& path) {
    if (path.empty()) return false;
    const auto& t = get(path);
    return t.width > 0 && t.height > 0;
}

// The FILE's dimensions. Not the texture's: see Cached::naturalW.
inline void natural_size(const std::string& path, float& w, float& h) {
    detail::Store& s = detail::store();
    auto it = s.map.find(path);
    if (it == s.map.end()) {
        w = 0.0f;
        h = 0.0f;
        return;
    }
    w = static_cast<float>(it->second.naturalW);
    h = static_cast<float>(it->second.naturalH);
}

inline float natural_width(const std::string& path) {
    float w = 0.0f;
    float h = 0.0f;
    natural_size(path, w, h);
    return w;
}

// Drawn HEIGHT for an image fit to colW (aspect-preserving, no upscale past
// natural width, capped at maxH). 0 when unavailable.
inline float fitted_height(const std::string& path, float colW,
                           float maxH = 420.0f) {
    if (!available(path)) return 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    natural_size(path, w, h);
    if (w <= 0.0f || h <= 0.0f) return 0.0f;
    float drawW = colW < w ? colW : w;
    float drawH = h * (drawW / w);
    return drawH > maxH ? maxH : drawH;
}

inline void draw(const std::string& path, float x, float y, float colW,
                 float drawnH) {
    if (!available(path)) return;
    const auto& t = get(path);
    // Source rect is the TEXTURE (what is resident); the destination width is
    // capped by the FILE's width (never upscale past natural size). Those are
    // two different numbers once an image has been halved, and conflating them
    // is the whole bug this pair of variables exists to avoid.
    const float w = static_cast<float>(t.width);
    const float h = static_cast<float>(t.height);
    const float natW = natural_width(path);
    if (natW <= 0.0f) return;
    float drawW = colW < natW ? colW : natW;
    afterhours::draw_texture_pro(t, RectangleType{0, 0, w, h},
                                 RectangleType{x, y, drawW, drawnH},
                                 Vector2Type{0, 0}, 0.0f,
                                 theme::Color{255, 255, 255, 255});
}

}  // namespace hanabi::inline_image
