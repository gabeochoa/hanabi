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
// WHAT IT DOES NOT FIX: hanabi holds FULL-RESOLUTION pixels to draw a 64px
// chip or a 420px-tall inline image, because afterhours' load_texture takes a
// path and nothing else -- there is no max-dimension or downscale-on-load, and
// vendor/afterhours is read-only here. That is afterhours_gaps.md #125: the
// real saving is 28x and it is upstream's to give.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

#include "../rl.h"     // global TextureType / RectangleType / Vector2Type + draw_*
#include "theme.h"     // theme::Color (== afterhours::Color) for the tint

namespace hanabi::inline_image {

// Decoded pixels held at once. Thirty-two megabytes is roughly one Retina
// screen grab plus change; the protection below means a working set larger
// than this is kept anyway rather than thrashed.
inline constexpr std::size_t kMaxCachedBytes = 32u * 1024 * 1024;

// Entries protected from eviction by recency of ACCESS, not of insertion. A
// frame draws at most the composer's five chips plus the inline images in
// view, so sixteen is comfortably a whole frame's working set.
inline constexpr std::size_t kProtectRecent = 16;

// Distinct paths remembered at all, including the ones that failed to load
// (those cost a map entry and no pixels, but a process that has been handed
// ten thousand bad paths should not remember all of them).
inline constexpr std::size_t kMaxEntries = 512;

struct Cached {
    TextureType tex{};
    bool tried = false;  // attempted a load (don't retry a bad path every frame)
    std::size_t bytes = 0;
    std::uint64_t stamp = 0;  // the access counter's value at the last touch
};

namespace detail {

struct Store {
    std::unordered_map<std::string, Cached> map;
    std::list<std::string> order;  // MRU front ... LRU back
    std::unordered_map<std::string, std::list<std::string>::iterator> pos;
    std::size_t bytes = 0;
    std::uint64_t clock = 0;
};

inline Store& store() {
    static Store s;
    return s;
}

inline void touch(Store& s, const std::string& path) {
    auto it = s.pos.find(path);
    if (it != s.pos.end()) s.order.erase(it->second);
    s.order.push_front(path);
    s.pos[path] = s.order.begin();
    s.map[path].stamp = ++s.clock;
}

// Drop least-recently-used entries until the budget is met, skipping anything
// touched within the last kProtectRecent accesses.
inline void evict(Store& s) {
    for (auto rit = s.order.rbegin(); rit != s.order.rend();) {
        if (s.bytes <= kMaxCachedBytes && s.map.size() <= kMaxEntries) return;
        const std::string victim = *rit;
        auto found = s.map.find(victim);
        if (found == s.map.end()) {
            ++rit;
            continue;
        }
        if (s.clock - found->second.stamp < kProtectRecent) {
            // Everything from here forward is even more recent: the list is in
            // access order, so there is nothing left that may be dropped.
            return;
        }
        s.bytes -= found->second.bytes;
        afterhours::unload_texture(found->second.tex);
        s.map.erase(found);
        auto p = s.pos.find(victim);
        if (p != s.pos.end()) {
            s.order.erase(p->second);
            s.pos.erase(p);
        }
        rit = s.order.rbegin();
    }
}

}  // namespace detail

inline TextureType& get(const std::string& path) {
    detail::Store& s = detail::store();
    auto it = s.map.find(path);
    if (it != s.map.end()) {
        detail::touch(s, path);
        return s.map[path].tex;
    }
    Cached c;
    c.tried = true;
    c.tex = afterhours::load_texture(path.c_str());
    c.bytes = static_cast<std::size_t>(c.tex.width > 0 ? c.tex.width : 0) *
              static_cast<std::size_t>(c.tex.height > 0 ? c.tex.height : 0) * 4u;
    s.bytes += c.bytes;
    s.map[path] = c;
    detail::touch(s, path);
    detail::evict(s);
    // evict() never drops an entry inside the protection window, and this one
    // was just touched, so the reference is live.
    return s.map[path].tex;
}

// Decoded bytes held, and how many paths (instrumentation; the ladder prints
// these so a bound is a number rather than a claim).
inline std::size_t cached_bytes() { return detail::store().bytes; }
inline std::size_t cached_count() { return detail::store().map.size(); }

inline bool available(const std::string& path) {
    if (path.empty()) return false;
    const auto& t = get(path);
    return t.width > 0 && t.height > 0;
}

// Drawn HEIGHT for an image fit to colW (aspect-preserving, no upscale past
// natural width, capped at maxH). 0 when unavailable.
inline float fitted_height(const std::string& path, float colW,
                           float maxH = 420.0f) {
    if (!available(path)) return 0.0f;
    const auto& t = get(path);
    float w = static_cast<float>(t.width);
    float h = static_cast<float>(t.height);
    float drawW = colW < w ? colW : w;
    float drawH = h * (drawW / w);
    return drawH > maxH ? maxH : drawH;
}

inline void draw(const std::string& path, float x, float y, float colW,
                 float drawnH) {
    if (!available(path)) return;
    const auto& t = get(path);
    float w = static_cast<float>(t.width);
    float h = static_cast<float>(t.height);
    float drawW = colW < w ? colW : w;
    afterhours::draw_texture_pro(t, RectangleType{0, 0, w, h},
                                 RectangleType{x, y, drawW, drawnH},
                                 Vector2Type{0, 0}, 0.0f,
                                 theme::Color{255, 255, 255, 255});
}

}  // namespace hanabi::inline_image
