#pragma once

// Marks a reader puts on a staged picture before sending it (the reference's
// ImageMarkup / MarkupGeometry; Knots kt-cimg). The reference's two
// primitives: an ARROW (one drag says "look here") and a BOX (frames a region
// a point cannot name). And the knot's two more, which the reference does not
// have: REDACT (a solid black block -- the reason this matters on a work
// machine: the pixels under it are gone from what is sent, not covered) and
// CROP (keep only a region; the newest crop wins, and undo takes it back like
// any mark).
// Freehand and text are left out on purpose -- a mouse scrawl points worse
// than an arrow, and the composer under the picture is already the caption.
//
// Strokes are recorded in the IMAGE's own pixels, origin top-left, so a mark
// means the same thing at any canvas size and flattens at full resolution.
// Pure: the canvas draws through these, and the flatten (native_extras)
// paints the same shapes.

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace hanabi::markup {

enum class Tool { Arrow, Box, Redact, Crop };

struct Point {
    float x = 0.0f, y = 0.0f;
    bool operator==(const Point&) const = default;
};
struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool operator==(const Rect&) const = default;
};

struct Stroke {
    Tool tool = Tool::Arrow;
    Point start, end;  // image pixels
    bool operator==(const Stroke&) const = default;
};

// Every mark, newest last; undo drops the newest.
struct Markup {
    std::vector<Stroke> strokes;
    bool empty() const { return strokes.empty(); }
    void add(const Stroke& s) { strokes.push_back(s); }
    bool undo() {
        if (strokes.empty()) return false;
        strokes.pop_back();
        return true;
    }
};

// A click is not a mark: a drag shorter than this (canvas points) leaves none.
inline constexpr float kMinimumDrag = 6.0f;

// The picture aspect-fitted and centred inside a canvas -- never enlarged past
// 1:1 (a blown-up blur is a lie about what will be sent).
inline Rect fitted_rect(float imgW, float imgH, float boxW, float boxH) {
    if (imgW <= 0 || imgH <= 0 || boxW <= 0 || boxH <= 0) return {};
    const float scale = std::min(1.0f, std::min(boxW / imgW, boxH / imgH));
    const float w = imgW * scale, h = imgH * scale;
    return {(boxW - w) / 2.0f, (boxH - h) / 2.0f, w, h};
}

// A canvas point (relative to the canvas's top-left) in image pixels, clamped
// to the picture: aiming near the edge is ordinary.
inline Point image_point(Point p, float imgW, float imgH, float boxW, float boxH) {
    const Rect r = fitted_rect(imgW, imgH, boxW, boxH);
    if (r.w <= 0 || r.h <= 0) return {};
    const float scale = imgW / r.w;
    return {std::clamp((p.x - r.x) * scale, 0.0f, imgW), std::clamp((p.y - r.y) * scale, 0.0f, imgH)};
}

inline Point canvas_point(Point p, float imgW, float imgH, float boxW, float boxH) {
    const Rect r = fitted_rect(imgW, imgH, boxW, boxH);
    if (imgW <= 0) return {};
    const float scale = r.w / imgW;
    return {r.x + p.x * scale, r.y + p.y * scale};
}

inline bool worth_keeping(Point a, Point b) {
    return std::hypot(b.x - a.x, b.y - a.y) >= kMinimumDrag;
}

// Proportional to the picture (a 400-px dialog to a 6K display), bounded.
inline float stroke_width(float imgW, float imgH) {
    const float shortest = std::min(imgW, imgH);
    if (shortest <= 0) return 2.0f;
    return std::min(14.0f, std::max(2.0f, shortest / 180.0f));
}

// The arrow head: the tip, then the two barbs; never longer than the arrow.
inline std::vector<Point> arrow_head(Point s, Point e, float width) {
    const float dx = e.x - s.x, dy = e.y - s.y;
    const float len = std::hypot(dx, dy);
    if (len <= 0) return {};
    const Point u{dx / len, dy / len};
    const Point n{-u.y, u.x};
    const float head = std::min(len, width * 4.5f);
    const float half = head * 0.45f;
    const Point base{e.x - u.x * head, e.y - u.y * head};
    return {e, {base.x + n.x * half, base.y + n.y * half}, {base.x - n.x * half, base.y - n.y * half}};
}

// A box from two dragged corners, whichever way the drag went.
inline Rect box(Point a, Point b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y)};
}

// Whether a finished drag leaves something in the PICTURE: a drag that began
// and ended off the same edge clamps to a line along it -- a box, redaction
// or crop of no area, which counted as a mark and drew nothing.
inline bool has_extent(const Stroke& s) {
    const Rect b = box(s.start, s.end);
    if (s.tool == Tool::Arrow) return b.w + b.h >= 1.0f;
    return b.w >= 2.0f && b.h >= 2.0f;
}

// The crop in force: the newest Crop mark's box, in whole image pixels and
// inside the picture; none when there is no crop or it is under 2 px a side.
inline std::optional<Rect> effective_crop(const std::vector<Stroke>& strokes, float imgW, float imgH) {
    for (auto it = strokes.rbegin(); it != strokes.rend(); ++it) {
        if (it->tool != Tool::Crop) continue;
        const Rect b = box(it->start, it->end);
        const float x0 = std::clamp(std::floor(b.x), 0.0f, imgW), y0 = std::clamp(std::floor(b.y), 0.0f, imgH);
        const float x1 = std::clamp(std::ceil(b.x + b.w), 0.0f, imgW), y1 = std::clamp(std::ceil(b.y + b.h), 0.0f, imgH);
        if (x1 - x0 < 2.0f || y1 - y0 < 2.0f) return std::nullopt;
        return Rect{x0, y0, x1 - x0, y1 - y0};
    }
    return std::nullopt;
}

// How many marks are painted (a crop is a frame, not a mark), and the status
// line the sheet shows.
inline std::size_t painted_count(const std::vector<Stroke>& strokes) {
    std::size_t n = 0;
    for (const auto& s : strokes)
        if (s.tool != Tool::Crop) ++n;
    return n;
}

// The colour a redaction is: opaque black, no casing (a casing would be a
// lighter rim around what was hidden).
inline constexpr float kRedact[4] = {0.0f, 0.0f, 0.0f, 1.0f};

// Red on a white casing, not a theme colour: a mark is drawn on a photograph
// of somebody else's window, and the casing reads on white and on black.
inline constexpr float kInk[4] = {0.93f, 0.13f, 0.13f, 1.0f};
inline constexpr float kCasing[4] = {1.0f, 1.0f, 1.0f, 0.92f};
inline constexpr float kCasingRatio = 2.0f;

}  // namespace hanabi::markup
