#pragma once

// Chrome icon system — Lucide (ISC) spritesheet.
//
// One monochrome (white on transparent) PNG atlas lives at
// resources/icons/icons.png; scripts/gen_icons.py generates it + the
// name->rect table in icons_atlas.h. We load the ONE texture lazily on first
// draw and blit each icon's sub-rect via draw_texture_pro, tinting with a theme
// color so the same sheet works for any theme. Everything routes through
// icon(name) so a future swap (e.g. native SF Symbols) is localized here.
//
// Fallback: if the atlas fails to load (missing file / GPU pool exhausted),
// draw_icon_fg falls back to painting the caller-supplied unicode glyph as
// text, so chrome never disappears or crashes. Callers pass both the icon name
// and the legacy glyph; whichever path is available wins (atlas preferred).

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <afterhours/src/plugins/files.h>

#include "icons_atlas.h"
#include "theme.h"

// Pull in the sokol draw helpers (draw_texture_pro / load_texture / TextureType)
// the same way theme.h does (drawing_helpers.h is included transitively there,
// but include it explicitly so this header is self-contained).
#include <afterhours/src/drawing_helpers.h>

namespace hanabi::icons {

// Look up an icon's source rect in the atlas by hanabi-neutral name.
// Returns nullopt if the name is unknown (lets callers fall back to text).
inline std::optional<RectangleType> src_rect(std::string_view name) {
    for (const auto& e : kAtlas) {
        if (e.name == name)
            return RectangleType{e.x, e.y, e.w, e.h};
    }
    return std::nullopt;
}

// Lazily-loaded singleton atlas texture. Loaded on first draw (needs a live GPU
// context, which exists during rendering, not necessarily at preload time).
// state: 0 = untried, 1 = loaded ok, 2 = failed (don't retry every frame).
//
// NOTE (afterhours gap): sokol_gl's default pipeline has alpha blending
// DISABLED, and draw_texture_pro does not enable it, so a white-on-transparent
// atlas would blit its transparent (rgb=0,a=0) pixels as opaque black squares.
// We work around it purely in app code by creating ONE blend-enabled sgl
// pipeline and pushing it around the blit. See afterhours_gaps.md.
struct AtlasTexture {
    TextureType tex{};
    int state = 0;
    sgl_pipeline blend_pip{};
    bool blend_pip_ready = false;

    static AtlasTexture& get() {
        static AtlasTexture inst;
        return inst;
    }

    // A pipeline with standard src-alpha over blending, created once.
    sgl_pipeline blend_pipeline() {
        if (!blend_pip_ready) {
            sg_pipeline_desc pd{};
            pd.colors[0].blend.enabled = true;
            pd.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            pd.colors[0].blend.dst_factor_rgb =
                SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            pd.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pd.colors[0].blend.dst_factor_alpha =
                SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend_pip = sgl_make_pipeline(&pd);
            blend_pip_ready = true;
        }
        return blend_pip;
    }

    // Returns a pointer to a valid texture, or nullptr if unavailable.
    TextureType* ensure() {
        if (state == 1)
            return &tex;
        if (state == 2)
            return nullptr;
        // First attempt.
        const std::string path =
            afterhours::files::get_resource_path("icons", "icons.png").string();
        tex = afterhours::load_texture(path.c_str());
        if (tex.width > 0.0f && tex.height > 0.0f) {
            state = 1;
            return &tex;
        }
        state = 2;  // failed; fall back to text glyphs from here on
        return nullptr;
    }
};

// Build an on_draw_fg callback that blits `name` centered inside the widget
// rect, tinted `color`, scaled to `draw_px` (square). If the atlas can't load
// or the name is unknown, falls back to drawing `fallback_glyph` as centered
// text so chrome is never blank.
//
// `y_bias` nudges the blit vertically (negative = up). Text labels sit a hair
// ABOVE the geometric center of a tall row slot (cap-height vs slot height), so
// a small negative bias optically centers an icon against an adjacent label.
//
// Usage: attach via .with_on_draw_fg(icons::draw_fg("gear", "\xe2\x9a\x99",
//        theme::text_secondary(), 16.f));
inline std::function<void(RectangleType)>
draw_fg(std::string name, std::string fallback_glyph, theme::Color color,
        float draw_px = 16.0f, float y_bias = 0.0f) {
    auto rect = src_rect(name);
    return [name = std::move(name), fallback_glyph = std::move(fallback_glyph),
            color, draw_px, y_bias, rect](RectangleType widget) {
        TextureType* atlas = AtlasTexture::get().ensure();
        if (atlas != nullptr && rect.has_value()) {
            // Center a draw_px x draw_px blit inside the widget rect.
            const float d = draw_px;
            RectangleType dest{
                widget.x + (widget.width - d) * 0.5f,
                widget.y + (widget.height - d) * 0.5f + y_bias, d, d};
            // Push a blend-enabled pipeline so the atlas' transparent pixels
            // don't blit as opaque black (sgl's default pipeline has blending
            // off — see the AtlasTexture note / afterhours_gaps.md).
            sgl_push_pipeline();
            sgl_load_pipeline(AtlasTexture::get().blend_pipeline());
            afterhours::draw_texture_pro(*atlas, *rect, dest,
                                         Vector2Type{0.f, 0.f}, 0.f,
                                         color);
            sgl_pop_pipeline();
            return;
        }
        // Fallback: draw the legacy unicode glyph as centered text. Uses the
        // backend text path (guarded internally against a missing font/context)
        // so a failed atlas never crashes or leaves chrome blank.
        //
        // TODO(icon-atlas): a fallback firing here means `name` has NO sprite in
        // the Lucide atlas (src/ui/icons_atlas.h) and we're drawing a stand-in
        // unicode glyph. Every fallback is a sprite we still owe. As of the
        // Phase H sweep, all chrome icons — including "close" and "archive" —
        // have real Lucide sprites; there are no known-missing sprites today.
        // CONVENTION: whenever you call draw_fg() with a name that isn't in the
        // atlas, add a `// TODO(icon-atlas): <name> sprite missing` at the call
        // site so we can come back and cut the real Lucide sprite.
        if (!fallback_glyph.empty()) {
            afterhours::draw_text(
                fallback_glyph.c_str(),
                widget.x + widget.width * 0.5f - draw_px * 0.5f,
                widget.y + widget.height * 0.5f - draw_px * 0.5f + y_bias,
                draw_px, color);
        }
    };
}

// Immediate-mode variant of draw_fg: blit `name` centered on a point (cx, cy)
// at `px` square, tinted `color`. For callers that draw glyphs at a geometric
// center (e.g. the sidebar's per-row status-glyph slot) rather than inside a
// widget rect. Returns true if the sprite was blitted; false if the atlas/name
// was unavailable (so the caller can fall back to a drawn shape). Uses the same
// blend-enabled pipeline as draw_fg so transparent atlas pixels don't blit as
// opaque black (see afterhours_gaps.md #13/#15).
inline bool draw_at(std::string_view name, float cx, float cy, float px,
                    theme::Color color) {
    auto rect = src_rect(name);
    TextureType* atlas = AtlasTexture::get().ensure();
    if (atlas == nullptr || !rect.has_value()) return false;
    const float d = px;
    RectangleType dest{cx - d * 0.5f, cy - d * 0.5f, d, d};
    sgl_push_pipeline();
    sgl_load_pipeline(AtlasTexture::get().blend_pipeline());
    afterhours::draw_texture_pro(*atlas, *rect, dest, Vector2Type{0.f, 0.f},
                                 0.f, color);
    sgl_pop_pipeline();
    return true;
}

}  // namespace hanabi::icons

// Small vector marks the UI font cannot draw. Roboto has no arrows, no
// geometric-shape block and no box-drawing block, and a codepoint it lacks is
// rendered as NOTHING — no tofu box, no warning — so a label that leans on one
// silently loses it (afterhours_gaps.md #48). Anything shaped is drawn here
// instead of typed.
namespace hanabi::glyph {

// A disclosure chevron: pointing DOWN when open, RIGHT when collapsed. Filled
// triangle so it stays crisp at any size without a rotated atlas cell.
inline void chevron(RectangleType rect, bool collapsed, theme::Color c,
                    float halfExtent = 3.6f) {
    const float cx = rect.x + rect.width * 0.5f;
    const float cy = rect.y + rect.height * 0.5f;
    const float s = halfExtent;
    if (collapsed)
        afterhours::draw_triangle(afterhours::vec2{cx - s, cy - s},
                                  afterhours::vec2{cx - s, cy + s},
                                  afterhours::vec2{cx + s, cy}, c);
    else
        afterhours::draw_triangle(afterhours::vec2{cx - s, cy - s},
                                  afterhours::vec2{cx + s, cy - s},
                                  afterhours::vec2{cx, cy + s}, c);
}

// An upward send arrow: a stem with a solid head, centred in `rect`. Roboto
// has no U+2191, so a typed one paints nothing at all (afterhours_gaps.md
// #48) — the composer's circular send button draws this instead of labelling
// itself. `extent` is the arrow's half-height.
inline void arrow_up(RectangleType rect, theme::Color c, float extent = 4.5f,
                     float thickness = 1.6f) {
    const float cx = rect.x + rect.width * 0.5f;
    const float cy = rect.y + rect.height * 0.5f;
    const float top = cy - extent;
    const float bot = cy + extent;
    const float head = extent * 0.95f;  // half-width of the arrowhead
    afterhours::draw_line_ex(afterhours::vec2{cx, top + head * 0.4f},
                             afterhours::vec2{cx, bot}, thickness, c);
    afterhours::draw_triangle(afterhours::vec2{cx - head, top + head},
                              afterhours::vec2{cx + head, top + head},
                              afterhours::vec2{cx, top}, c);
}

// A radio mark: a ring, filled when it is the current choice. Drawn rather
// than typed for the same reason the chevron is — the font has no geometric
// shapes, and a missing codepoint paints nothing at all.
inline void radio(RectangleType rect, bool selected, theme::Color c,
                  float radius = 4.0f) {
    const float cx = rect.x + rect.width * 0.5f;
    const float cy = rect.y + rect.height * 0.5f;
    afterhours::draw_ring(cx, cy, radius - 1.0f, radius, 24, c);
    if (selected)
        afterhours::draw_circle(static_cast<int>(cx), static_cast<int>(cy),
                                radius - 2.0f, c);
}

// A pushpin, for a pinned tab. Roboto has no pin codepoint and a missing one
// paints NOTHING (gap #48), so the mark is drawn: a round head, a shaft down
// from it, and a short crossbar where the head meets the shaft.
inline void pin(RectangleType rect, theme::Color c) {
    // Thumbtack in profile: flat cap, narrow shaft, wide flange, needle. A 6x10
    // mark hung from the rect's top-left.
    //
    // Re-traced against `ref/01_home.png` at x295..302, y44..54, by thresholding
    // the reference's coverage at half rather than by eye -- squinting at an
    // intensity ramp read the widest row as 8 wide when the two outer columns
    // are 29 and 35 above background, a third of a covered pixel. The half-
    // coverage silhouette is 6 / 4 / 6 / 4 / 2: a cap, a shaft, a flange, the
    // flange's taper, a needle.
    //
    // What was here drew the shaft 2 wide and hung it off the cap's LEFT half
    // (x+1 of a 6-wide cap), so the mark leaned, and it had no taper. Four wide
    // and centred is both the reference's shape and a pushpin's.
    const float x = rect.x + 1.0f;
    const float y = rect.y + (rect.height - 10.0f) * 0.5f;
    afterhours::draw_rectangle(RectangleType{x, y, 6.0f, 2.0f}, c);
    afterhours::draw_rectangle(RectangleType{x + 1.0f, y + 2.0f, 4.0f, 3.0f}, c);
    afterhours::draw_rectangle(RectangleType{x, y + 5.0f, 6.0f, 2.0f}, c);
    afterhours::draw_rectangle(RectangleType{x + 1.0f, y + 7.0f, 4.0f, 1.0f}, c);
    afterhours::draw_rectangle(RectangleType{x + 2.0f, y + 8.0f, 2.0f, 2.0f}, c);
}

}  // namespace hanabi::glyph
