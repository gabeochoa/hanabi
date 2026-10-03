#pragma once

#include <string>
#include <unordered_map>

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
#include "mm3_faces.h"
#include "mm3_marks.h"
#include <optional>
#include "theme.h"
#include "viewport.h"

namespace hanabi::status_mark {

using Glyph = ecs::model::StatusGlyph;

// The MM3 set (Settings > Appearance > Icons; puffin_gaps.md D51). Which
// face a status wears, and whether it moves, is ui/mm3_marks.h. The motion
// state is refreshed once a frame by whoever draws marks: the clock (frame dt,
// so a scripted run is deterministic), Reduce Motion, and whether the app is
// in front / its window can be seen.
inline bool& mm3_on() {
    static bool on = false;
    return on;
}
struct Mm3Motion {
    double clock = 0.0;
    bool reduceMotion = false;
    bool appActive = true;
    bool windowVisible = true;
    // A moving face drew in the last rendered frame, and how long until any
    // face drawn there shows a different frame. The frame loop wakes THEN --
    // not at the display rate: an angry loop changes ~20 times a second and a
    // calm blink rests 4.5 s between plays, and a frame between changes would
    // draw the same pixels (the reference's CoreAnimation keyframes cost no
    // app redraw at all; this is the nearest a retained-frame loop gets).
    bool movedLastFrame = false;
    double nextChangeIn = 1e9;
    std::uint64_t sinceDrawUs = 0;  // set by the frame loop before admission
    unsigned long movedDraws = 0;   // every moving draw, for scripted checks
    [[nodiscard]] bool change_due() const {
        return movedLastFrame && static_cast<double>(sinceDrawUs) + 1000.0 >= nextChangeIn * 1e6;
    }
};
inline Mm3Motion& mm3_motion() {
    static Mm3Motion m;
    return m;
}
// What each sidebar row's MM3 mark was last frame, by session id, and
// whether it was moving (scripted checks only: written under E2E builds).
struct RowMarkSeen {
    mm3::Mark mark;
    bool moving = false;
};
inline std::unordered_map<std::string, RowMarkSeen>& row_marks_seen() {
    static std::unordered_map<std::string, RowMarkSeen> m;
    return m;
}
// At the start of a drawn frame: the clock moves, and this frame's faces will
// say whether anything moves and when it next changes.
inline void mm3_advance(double dt) {
    auto& m = mm3_motion();
    m.clock += dt;
    m.movedLastFrame = false;
    m.nextChangeIn = 1e9;
}

// A face's cells, `h` tall measured on the ROW box (mm3::kRow*: room for
// every frame, so nothing is cut; the reference's 8pt, D123162830 / kt-tgsx),
// centred on (cx, cy).
inline void draw_cells(const mm3::Cell* cells, std::size_t n, float cx, float cy, float h,
                       theme::Color c) {
    const float scale = h / mm3::kRowH;
    const float x0 = cx - (mm3::kRowX + mm3::kRowW * 0.5f) * scale;
    const float y0 = cy - (mm3::kRowY + mm3::kRowH * 0.5f) * scale;
    for (std::size_t i = 0; i < n; ++i) {
        const mm3::Cell& k = cells[i];
        afterhours::draw_rectangle(
            RectangleType{x0 + k.x * scale, y0 + k.y * scale, k.w * scale, k.h * scale}, c);
    }
}
inline void draw_face(mm3::Face f, float cx, float cy, float h, theme::Color c) {
    const auto cells = mm3::cells(f);
    draw_cells(cells.data, cells.size, cx, cy, h, c);
}
// A face in its own box (chrome: the shelf), `h` tall, centred on (cx, cy).
inline void draw_face_boxed(mm3::Face f, float cx, float cy, float h, theme::Color c) {
    const mm3::Box b = mm3::box_of(f);
    const float scale = h / b.h;
    const float x0 = cx - (b.x + b.w * 0.5f) * scale;
    const float y0 = cy - (b.y + b.h * 0.5f) * scale;
    const auto cells = mm3::cells(f);
    for (std::size_t i = 0; i < cells.size; ++i) {
        const mm3::Cell& k = cells.data[i];
        afterhours::draw_rectangle(
            RectangleType{x0 + k.x * scale, y0 + k.y * scale, k.w * scale, k.h * scale}, c);
    }
}
// A mark: its moving frame when it plays, else its still face.
// Cells in an explicit box (chrome draws an animation in its face's own box).
inline void draw_cells_in(const mm3::Cell* cells, std::size_t n, mm3::Box b, float cx, float cy, float h,
                          theme::Color c) {
    const float scale = h / b.h;
    const float x0 = cx - (b.x + b.w * 0.5f) * scale;
    const float y0 = cy - (b.y + b.h * 0.5f) * scale;
    for (std::size_t i = 0; i < n; ++i) {
        const mm3::Cell& k = cells[i];
        afterhours::draw_rectangle(RectangleType{x0 + k.x * scale, y0 + k.y * scale, k.w * scale, k.h * scale}, c);
    }
}

// The frame a moving mark shows now, and when it next changes. A finish wave
// is a once-play on its own clock; everything else loops on the shared one.
struct MovingFrame {
    std::size_t frame = 0;
    double nextChangeIn = 1e9;
};
inline MovingFrame moving_frame(const mm3::Mark& m, const mm3::Anim& a, double clock) {
    if (m.once) {
        MovingFrame f{mm3::frame_once(a, m.onceT), 1e9};
        if (m.onceT < a.duration)
            f.nextChangeIn = std::min(mm3::next_change_in(a, m.onceT, 0.0), a.duration - m.onceT);
        return f;
    }
    const double rest = mm3::rest_for(a, m.urgent);
    return {mm3::frame_at(a, clock, rest), mm3::next_change_in(a, clock, rest)};
}

inline void draw_mm3(const mm3::Mark& m, float cx, float cy, float h, theme::Color c) {
    Mm3Motion& mo = mm3_motion();
    if (mm3::plays(m, mo.reduceMotion, mo.appActive, mo.windowVisible)) {
        const mm3::Anim a = mm3::anim(*m.moves);
        const MovingFrame f = moving_frame(m, a, mo.clock);
        const auto cells = mm3::frame_cells(a, f.frame);
        draw_cells(cells.data, cells.size, cx, cy, h, c);
        mo.movedLastFrame = true;
        mo.nextChangeIn = std::min(mo.nextChangeIn, f.nextChangeIn);
        ++mo.movedDraws;
        return;
    }
    draw_face(m.still, cx, cy, h, c);
}

// A VIEWS-list face (chrome): Home and Pinned blink calmly in their face's
// own box (D123182666); every other shelf face is still.
inline void draw_shelf_face(mm3::Face f, float cx, float cy, float h, theme::Color c) {
    Mm3Motion& mo = mm3_motion();
    if (const auto motion = mm3::shelf_motion(f)) {
        const mm3::Mark m{f, motion};
        if (mm3::plays(m, mo.reduceMotion, mo.appActive, mo.windowVisible)) {
            const mm3::Anim a = mm3::anim(*motion);
            const MovingFrame fr = moving_frame(m, a, mo.clock);
            const auto cells = mm3::frame_cells(a, fr.frame);
            draw_cells_in(cells.data, cells.size, a.box, cx, cy, h, c);
            mo.movedLastFrame = true;
            mo.nextChangeIn = std::min(mo.nextChangeIn, fr.nextChangeIn);
            ++mo.movedDraws;
            return;
        }
    }
    draw_face_boxed(f, cx, cy, h, c);
}

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
// An MM3 face's frame height in a mark slot, on a row and a tab alike -- the
// height of the ROW box, so the face itself is ~6.5pt and the box ~9.9pt wide,
// inside the 10pt slot (the reference's Size.mm3Face, D123162830 / kt-tgsx).
inline constexpr float kMm3FaceH = 8.0f;

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
             theme::Color bg, bool failed = false, const mm3::Moment& moment = {}) {
    const float cx = rect.x + rect.width * 0.5f + hanabi::viewport::px(kMarkDx);
    const float cy = rect.y + rect.height * 0.5f + hanabi::viewport::px(kMarkDy);
    const theme::Color c = color_for(glyph);
    if (mm3_on()) {
        if (const auto mark = mm3::mark_for(glyph, failed, moment)) {
            draw_mm3(*mark, cx, cy, hanabi::viewport::px(kMm3FaceH), c);
            return;
        }
    }
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
