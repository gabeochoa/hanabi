#pragma once

// Inline image rendering for the transcript (agent surface): the agent can
// attach a local image (e.g. a screenshot) to a message; the transcript decodes
// it once, caches the GPU texture, and draws it inline under the message text.
//
// afterhours provides load_texture(path) (stb_image decode -> GPU texture) and
// draw_texture_pro (textured quad). This adds a small bounded path->texture
// cache so a scrolling transcript doesn't re-decode every frame, plus a fit
// helper that scales an image into the reading column (never upscaling past
// natural width; capping tall images so a screenshot doesn't dominate).

#include <string>
#include <unordered_map>

#include "../rl.h"     // global TextureType / RectangleType / Vector2Type + draw_*
#include "theme.h"     // theme::Color (== afterhours::Color) for the tint

namespace hanabi::inline_image {

struct Cached {
    TextureType tex{};
    bool tried = false;  // attempted a load (don't retry a bad path every frame)
};

inline TextureType& get(const std::string& path) {
    static std::unordered_map<std::string, Cached> cache;
    auto& c = cache[path];
    if (!c.tried) {
        c.tried = true;
        c.tex = afterhours::load_texture(path.c_str());
    }
    return c.tex;
}

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
