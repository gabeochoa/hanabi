#pragma once

// Marks a reader puts on a staged picture before sending it (the reference's
// ImageMarkup / MarkupGeometry; Knots kt-cimg). Two primitives: an ARROW (one
// drag says "look here") and a BOX (frames a region a point cannot name).
// Freehand and text are left out on purpose -- a mouse scrawl points worse
// than an arrow, and the composer under the picture is already the caption.
//
// Strokes are recorded in the IMAGE's own pixels, origin top-left, so a mark
// means the same thing at any canvas size and flattens at full resolution.
// Pure: the canvas draws through these, and the flatten (native_extras)
// paints the same shapes.

#include <algorithm>
#include <cmath>
#include <vector>

namespace hanabi::markup {

enum class Tool { Arrow, Box };

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

// Red on a white casing, not a theme colour: a mark is drawn on a photograph
// of somebody else's window, and the casing reads on white and on black.
inline constexpr float kInk[4] = {0.93f, 0.13f, 0.13f, 1.0f};
inline constexpr float kCasing[4] = {1.0f, 1.0f, 1.0f, 0.92f};
inline constexpr float kCasingRatio = 2.0f;

}  // namespace hanabi::markup
