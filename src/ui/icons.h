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
        if (!fallback_glyph.empty()) {
            afterhours::draw_text(
                fallback_glyph.c_str(),
                widget.x + widget.width * 0.5f - draw_px * 0.5f,
                widget.y + widget.height * 0.5f - draw_px * 0.5f + y_bias,
                draw_px, color);
        }
    };
}

}  // namespace hanabi::icons
