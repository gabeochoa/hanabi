#pragma once

// The status mark, drawn.
//
// One picture per status, in one place, because two surfaces draw it: the
// sidebar row and the tab strip. "The same status in both places" is a promise
// about the PICTURE as much as about the classification, so the geometry lives
// here rather than being copied into each.
//
// The classification is ecs::model::status_glyph (src/ecs/thread_model.h),
// which is graphics-free and unit-tested; this is only its ink.

#include "../ecs/thread_model.h"
#include "icons.h"
#include "theme.h"
#include "viewport.h"

namespace hanabi::status_mark {

using Glyph = ecs::model::StatusGlyph;

inline theme::Color color_for(Glyph glyph) {
    switch (glyph) {
        case Glyph::Running: return theme::accent();
        case Glyph::Blocked: return theme::status_blocked();
        // Same shape as Blocked, and the colour is the whole difference --
        // which is how Puffin draws the pair too (IconTable gives both an
        // `exclamationmark` and splits them on attention vs success).
        case Glyph::Waiting: return theme::status_review();
        case Glyph::Done: return theme::status_review();
        // A brake is not an alarm. Both read as quiet-and-stopped rather
        // than as something wanting the reader.
        case Glyph::Frozen: return theme::text_faint();
        case Glyph::Paused: return theme::text_faint();
        case Glyph::Idle: return theme::text_faint();
    }
    return theme::text_faint();
}

// Draw the status mark centered inside `rect` (the on-screen rect of the
// small glyph slot). Uses afterhours' real shape primitives, so a status is
// distinguishable without relying on colour. Every row draws SOMETHING here --
// a settled row gets the plain dot, never a blank -- so no row reads as
// unlabeled or second-class.
//
// SEVEN values, FIVE shapes. Blocked and Waiting deliberately SHARE the bang
// and differ only in colour and in the accessible name
// (`ecs::model::status_label`), which is Puffin's own rule: `IconTable` gives
// both an `exclamationmark` and splits them attention vs success. They are one
// question -- "does this need me?" -- and drawing them as two shapes said they
// were two kinds of thing. The five shapes: arc (Running), bang (Blocked,
// Waiting), tick (Done), bars (Paused), snowflake (Frozen), and the dot that
// every other state falls back to (Idle).
//
// Geometry is measured off ref/01_home.png, glyph by glyph: the dot is an 8px
// circle, the bang a 9px stroke over a 2px tittle, and the arc a 290-degree
// ring. Each is re-derived against the reference's own HALF-COVERAGE
// silhouette rather than by eye: afterhours does not antialias primitives
// (afterhours_gaps.md #92), so hanabi's marks are hard-edged where Puffin's
// have a soft fringe -- which means a mark drawn to the reference's OUTER
// extent lands 30-85% more ink on screen than it has. Drawn to its
// half-coverage extent instead, the ink lands about right and the silhouette
// still matches. The bars and the snowflake have no reference to measure
// against (Puffin draws an emoji for the freeze) and are sized to sit level
// with the measured ones.
inline constexpr float kArcInner = 3.3f;
inline constexpr float kArcOuter = 4.6f;
// The bang, measured by per-pixel coverage rather than by silhouette: the
// reference's stroke is 1.95px wide and runs from 5.5px above the mark's
// centre to 2.46 below it, and its tittle is the same width, 2.28 tall,
// centred 5.26 below.
inline constexpr float kBangT = 1.95f;
inline constexpr float kBangTop = 5.5f;
inline constexpr float kBangBot = 2.46f;
inline constexpr float kBangDotY = 5.26f;
inline constexpr float kBangDotH = 2.28f;
inline constexpr float kDotR = 3.4f;
inline constexpr float kCheckT = 1.8f;

// Where the mark's centre sits relative to the slot's own. Puffin draws it
// above the row's midline and right of a 13px slot's centre; both are
// measured, and without them every glyph reads a row-half low.
inline constexpr float kMarkDx = 0.0f;
inline constexpr float kMarkDy = -1.0f;

// `bg` is what this row is actually painting on: the bang is the one mark
// drawn with hand-composited antialiasing (gap #92 has no other way out),
// and a fringe pre-mixed against the wrong colour is a visible halo. The
// row's own fill changes under the pointer, so the caller passes it rather
// than this assuming the sidebar's.
inline void draw(RectangleType rect, Glyph glyph,
             theme::Color bg) {
    const float cx = rect.x + rect.width * 0.5f + hanabi::viewport::px(kMarkDx);
    const float cy = rect.y + rect.height * 0.5f + hanabi::viewport::px(kMarkDy);
    const theme::Color c = color_for(glyph);
    switch (glyph) {
        case Glyph::Running: {
            // The gap is at the TOP, and this is the one thing in the
            // glyph column that was not a pixel-nudge: hanabi drew the gap
            // in the LOWER LEFT, so the mark read as a hook where the
            // reference draws a bowl.
            //
            // Measured on all four running rows of `ref/02_thread.png`,
            // which are identical to the pixel: ink covers 290 degrees and
            // the 70-degree gap is centred on 275.5, five degrees clockwise
            // of straight up. Angles run clockwise from three o'clock, so
            // that is -49 to 240.
            //
            // Puffin's source cannot settle this. `SessionRowView.statusDot`
            // in the v0.5.2 checkout is a 7pt filled `Circle()` -- the five
            // shapes arrived after it (REFERENCE.md), so the frozen PNG is
            // the only authority for the arc's geometry and every number
            // above comes off it.
            afterhours::draw_ring_segment(cx, cy, hanabi::viewport::px(kArcInner), hanabi::viewport::px(kArcOuter),
                                          -49.0f, 240.0f, 28, c);
            break;
        }
        case Glyph::Idle: {
            // draw_circle_v truncates its centre to int (gap #78), which
            // at this size lands the dot half a pixel off and reads as a
            // lumpy polygon. A zero-inner-radius ring segment is the same
            // shape with a float centre.
            afterhours::draw_ring_segment(cx, cy, 0.0f, hanabi::viewport::px(kDotR), 0.0f,
                                          360.0f, 28, c);
            break;
        }
        case Glyph::Waiting:
        case Glyph::Blocked: {
            // The most common mark in the list -- six of the eighteen
            // visible rows -- and it was carrying twice the reference's
            // ink: three hard columns at full strength where the reference
            // measures 0.44 / 0.97 / 0.50, a 1.95px stroke with a fringe
            // down each side. It also sat a pixel left of every other mark
            // in the column, from a `- px(1)` nobody had re-measured.
            //
            // Both axes of both parts are read off `ref/02_thread.png` by
            // per-pixel coverage, and laid down through `rect_aa`, which
            // paints the fringe itself (afterhours has no primitive
            // antialiasing -- gap #92). A bang and its tittle are the two
            // marks in this vocabulary that are axis-aligned, so they are
            // the two that can have it.
            const float u = hanabi::viewport::px(1.0f);
            const float half = kBangT * 0.5f * u;
            hanabi::glyph::rect_aa(cx - half, cy - kBangTop * u, cx + half,
                                   cy + kBangBot * u, c, bg);
            hanabi::glyph::rect_aa(cx - half, cy + kBangDotY * u - kBangDotH * 0.5f * u,
                                   cx + half,
                                   cy + kBangDotY * u + kBangDotH * 0.5f * u,
                                   c, bg);
            break;
        }
        case Glyph::Done: {
            const float u = hanabi::viewport::px(1.0f);
            afterhours::draw_line_ex(
                afterhours::vec2{cx - 4.0f * u, cy},
                afterhours::vec2{cx - 1.0f * u, cy + 3.0f * u},
                kCheckT * u, c);
            afterhours::draw_line_ex(
                afterhours::vec2{cx - 1.0f * u, cy + 3.0f * u},
                afterhours::vec2{cx + 5.0f * u, cy - 4.0f * u},
                kCheckT * u, c);
            break;
        }
        case Glyph::Paused: {
            // Two bars, the universal "stopped, not finished". Axis-aligned,
            // so it takes rect_aa's composited fringe like the bang does.
            const float u = hanabi::viewport::px(1.0f);
            const float half = kBangT * 0.5f * u;
            const float gap = 2.0f * u;
            hanabi::glyph::rect_aa(cx - gap - half, cy - 4.0f * u,
                                   cx - gap + half, cy + 4.0f * u, c, bg);
            hanabi::glyph::rect_aa(cx + gap - half, cy - 4.0f * u,
                                   cx + gap + half, cy + 4.0f * u, c, bg);
            break;
        }
        case Glyph::Frozen: {
            // Three strokes through one centre -- a snowflake at 8px, the
            // only shape here that is neither dot, bar nor tick. Puffin
            // draws an emoji; the UI font silently drops codepoints that
            // far out (gap #48), so this is primitives like its neighbours.
            const float u = hanabi::viewport::px(1.0f);
            const float r = 4.2f * u;
            const float t = 1.4f * u;
            constexpr float kSin60 = 0.8660254f;
            afterhours::draw_line_ex(afterhours::vec2{cx, cy - r},
                                     afterhours::vec2{cx, cy + r}, t, c);
            afterhours::draw_line_ex(
                afterhours::vec2{cx - r * kSin60, cy - r * 0.5f},
                afterhours::vec2{cx + r * kSin60, cy + r * 0.5f}, t, c);
            afterhours::draw_line_ex(
                afterhours::vec2{cx - r * kSin60, cy + r * 0.5f},
                afterhours::vec2{cx + r * kSin60, cy - r * 0.5f}, t, c);
            break;
        }
    }
}


}  // namespace hanabi::status_mark
