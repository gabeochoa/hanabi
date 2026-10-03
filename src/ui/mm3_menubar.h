#pragma once

// The MM3 face in the menu bar, moving (the reference's MM3MenuBar, draft
// D123023215; puffin_gaps.md D51). What it says -- the owner's map: idle
// smiles and blinks now and then, a run blinks in a slow loop, a thread
// waiting on you waves, a lost connection is tired.
//
// A TIMER ONLY WHILE A FRAME IS DUE: between plays one timer waits for the
// next play, during a play one timer waits for the next frame; at rest under
// Reduce Motion, while the screens sleep, or under the Normal icon set there
// is none. A run of steps showing the same frame is one swap and one timer,
// and a frame whose pixels at menu-bar size match the one showing is not
// handed over at all (many library frames differ by a cell that rounds away).
//
// Pure: the plan, the step walk and the pixel key. The driver (menubar.mm)
// owns the NSTimer and the status item.

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mm3_anim.h"
#include "mm3_faces.h"

namespace hanabi::mm3::menubar {

enum class Mood { Idle, Working, NeedsYou, Offline };

// The connection outranks the rows: a thread that looks like it needs you on
// a catalog that cannot be read is not news.
inline Mood mood_of(int needsYou, int working, bool offline) {
    if (offline) return Mood::Offline;
    if (needsYou > 0) return Mood::NeedsYou;
    if (working > 0) return Mood::Working;
    return Mood::Idle;
}

struct Plan {
    Animation animation;
    Face rest;
    double everyLo = 0, everyHi = 0;  // seconds between plays; 0/0 = play once, then hold
    [[nodiscard]] bool repeats() const { return everyHi > 0; }
};

inline Plan plan_for(Mood m) {
    switch (m) {
        case Mood::Idle: return {Animation::SmileBlink, Face::Smile, 4, 6};
        case Mood::Working: return {Animation::Blink, Face::Neutral, 4, 6};
        case Mood::NeedsYou: return {Animation::Wave, Face::Wave, 6, 8};
        case Mood::Offline: return {Animation::Tired, Face::Tired, 0, 0};
    }
    return {Animation::SmileBlink, Face::Smile, 4, 6};
}

// Whether the face moves at all now. The idle blink is decoration, played
// only while the app is in front; a mood that says something still plays
// behind another app.
inline bool moves(Mood m, bool visible, bool reduceMotion, bool appActive) {
    if (!visible || reduceMotion) return false;
    return m != Mood::Idle || appActive;
}

// One step of a play: the frame to show, the last step that shows the same
// frame, and how long until the step after it.
struct Step {
    std::size_t frame = 0;
    std::size_t last = 0;
    double seconds = 0;
    bool done = false;  // past the last step: show the rest face
};

inline Step step_at(const Anim& a, std::size_t step) {
    Step s;
    if (step >= a.steps) {
        s.done = true;
        return s;
    }
    s.frame = a.timeline[step];
    std::size_t last = step;
    while (last + 1 < a.steps && a.timeline[last + 1] == s.frame) ++last;
    s.last = last;
    const double next = last + 1 < a.steps ? a.keyTimes[last + 1] : 1.0;
    s.seconds = std::max(0.01, (next - a.keyTimes[step]) * a.duration);
    return s;
}

// The menu-bar picture: 16pt tall, as wide as the neutral face at 80% of the
// height, inset 10% top and bottom; drawn at `scale` (2 on Retina).
inline constexpr double kSide = 16.0;
inline double image_width() { return std::round(kSide * (kBoxW / kBoxH) * 0.8); }

// A key for what `cells` (in a face's source units, `box`) cover at menu-bar
// size: one bit per device pixel, by pixel-centre coverage. Two frames with
// the same key look the same in the status item.
inline std::vector<std::uint64_t> pixel_key(const Cell* cells, std::size_t n, Box box, double scale) {
    const int w = static_cast<int>(image_width() * scale);
    const int h = static_cast<int>(kSide * scale);
    const double inset = kSide * 0.1 * scale;
    const double s = (h - 2 * inset) / box.h;
    std::vector<std::uint64_t> bits(static_cast<std::size_t>((w * h + 63) / 64), 0);
    for (std::size_t i = 0; i < n; ++i) {
        const double x0 = (cells[i].x - box.x) * s, x1 = x0 + cells[i].w * s;
        const double y0 = inset + (cells[i].y - box.y) * s, y1 = y0 + cells[i].h * s;
        for (int py = std::max(0, static_cast<int>(std::floor(y0))); py < std::min(h, static_cast<int>(std::ceil(y1))); ++py)
            for (int px = std::max(0, static_cast<int>(std::floor(x0))); px < std::min(w, static_cast<int>(std::ceil(x1))); ++px) {
                const double cx = px + 0.5, cy = py + 0.5;
                if (cx >= x0 && cx < x1 && cy >= y0 && cy < y1) {
                    const std::size_t bit = static_cast<std::size_t>(py * w + px);
                    bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
                }
            }
    }
    return bits;
}

// Animation frames and faces in the menu bar share the face box (the
// reference draws both in MM3Face.smile.box / the rest face's own box).
inline Box frame_box() { return {kBoxX, kBoxY, kBoxW, kBoxH}; }

}  // namespace hanabi::mm3::menubar
