#pragma once

// ---------------------------------------------------------------------------
// The rail's MARKS, as arithmetic: what kinds there are, how tall a slot is,
// and how many marks a rail of a given height can actually show.
//
// Split out of minimap.h for the same reason minimap_scrub.h was: minimap.h
// draws, so it reaches theme and afterhours, and a unit test cannot link it.
// Everything here is pure -- no afterhours, no RectangleType, no theme -- so
// tests/unit/test_minimap_marks.cpp can drive the grouping directly instead of
// the property being reachable only through a screenshot of a long thread.
// minimap.h includes this and keeps the two functions that paint.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hanabi::minimap {

// What a mark stands for. The transcript's row kinds collapse into these five:
// a reader scanning the rail wants "where are the answers, where did it go off
// and do things, where did I ask something", not a row-type taxonomy.
enum class Mark {
    Machinery,  // tool runs — a pile or a single block
    Reply,      // an assistant turn
    Ask,        // something the reader said
    Notice,     // a sub-agent spawn: a notable event in the thread
    Note,       // reasoning, dividers — present, but quiet
};

// Rail geometry.
inline constexpr float kRailW = 10.0f;      // the strip itself
inline constexpr float kRailInset = 6.0f;   // gap to the pane's right edge
inline constexpr float kDotW = 6.0f;        // a mark's drawn width
inline constexpr float kMinDotH = 2.0f;     // and its smallest drawn height
inline constexpr float kMaxDotH = 10.0f;    // and its largest

// A rail is only worth the space when there is something off screen to reach.
inline bool worth_showing(float contentH, float viewH) {
    return contentH > viewH + 40.0f;
}

// The slot height for an item of `itemH` in a transcript of `totalH`, on a
// rail `railH` tall. Slots are proportional and are NOT clamped: they have to
// sum to the rail exactly, or the marks stop lining up with the scrollbar and
// every mark below the drift points somewhere it does not mean. The DRAWN dot
// inside the slot is what gets a minimum size.
inline float slot_h(float itemH, float totalH, float railH) {
    if (totalH <= 0.0f || railH <= 0.0f) return 0.0f;
    return railH * (itemH / totalH);
}

// ---- Grouping: a rail cannot show more marks than it has pixels -----------
//
// THE BUG THIS FIXES IS BOTH HALVES AT ONCE, which is why it is here and not
// in a perf patch. `slot_h` is exact and unclamped, `draw_mark` clamps the
// DRAWN dot up to kMinDotH, and neither of those is wrong on its own. Put
// together on a long thread they are: at 3,672 messages the transcript builds
// 2,263 items, the rail is about 800px, so the average slot is 0.35px and
// every one of those 2,263 dots is clamped up to 2px. They overlap six deep.
// The rail paints a solid stripe — it stops being a map, silently, and the
// longer the thread the less it says, which is precisely backwards.
//
// It is also 2,263 button entities rebuilt every frame, each with its own
// `std::to_string` debug name: 1.33 ms of a 8.14 ms frame, and the single
// largest per-message cost left in this app (docs/perf/EVENTS.md).
//
// The bound is not tuned. A 2px dot needs 2px of rail, so `kMinDotH` is the
// exact height at which marks begin to overlap, and a rail of `railH` pixels
// can hold `railH / kMinDotH` of them and no more. Below that density every
// item keeps its own mark and NOTHING changes — which is why the 160-message
// fixture in tests/ui/minimap_navigator.e2e still gets one mark per item and
// still finds question #20 under mark 60.
//
// Position is preserved exactly: a group's slot is the SUM of its items'
// heights, so the slots still sum to the rail and no mark below a group
// drifts. A click on a group goes to the group's FIRST item, which is the
// same promise a single mark makes, at the resolution the rail can express.

// One drawn mark, after grouping. `firstItem` is what a click jumps to;
// `height` is in CONTENT pixels and is the sum over the group.
struct Slot {
    int firstItem = 0;
    float topY = 0.0f;
    float height = 0.0f;
    Mark mark = Mark::Note;
};

// Which of two kinds a group should wear. A reader scanning the rail is
// looking for the shape of the conversation — where they asked something,
// where something notable happened, where the answers are — and machinery and
// reasoning are the texture between those. So the group takes the rarest,
// most-worth-seeing kind in it rather than the first or the tallest: a turn
// collapsed with the forty tool rows after it should still read as a turn.
inline int mark_priority(Mark m) {
    switch (m) {
        case Mark::Ask: return 0;
        case Mark::Notice: return 1;
        case Mark::Reply: return 2;
        case Mark::Machinery: return 3;
        case Mark::Note: return 4;
    }
    return 4;
}

// Walk the items and close a group as soon as it has earned a drawable slot.
// `firstTop` is the content-y the first item starts at (the transcript's lead
// block sits above it). Groups come out in order and their heights sum to the
// same total the ungrouped items did.
inline std::vector<Slot> group_marks(const std::vector<float>& heights,
                                     const std::vector<Mark>& marks,
                                     float firstTop, float totalH,
                                     float railH) {
    std::vector<Slot> out;
    const size_t n = heights.size() < marks.size() ? heights.size()
                                                   : marks.size();
    if (n == 0 || totalH <= 0.0f || railH <= 0.0f) return out;

    // THE GUARD, and it is why nothing about a normal thread moves. Grouping
    // is a response to a rail with more marks on it than pixels, and that is
    // an AGGREGATE property: `n * kMinDotH` against the rail. Under it every
    // item keeps its own mark, at whatever slot height it earns, exactly as
    // before -- including the sub-2px slots a short row gets in a sparse
    // thread, whose dots overlap their neighbours by well under a pixel and
    // have never been a problem. Thresholding each item on its own would have
    // merged those too, which is a behaviour change at a density that works,
    // and tests/ui/minimap_navigator.e2e (160 messages, 120 items, 240px of
    // dots on a ~590px rail) is the reader who would notice.
    if (static_cast<float>(n) * kMinDotH <= railH) {
        out.reserve(n);
        float t = firstTop;
        for (size_t i = 0; i < n; ++i) {
            out.push_back(Slot{static_cast<int>(i), t, heights[i], marks[i]});
            t += heights[i];
        }
        return out;
    }

    // The ceiling the loop below cannot exceed, reserved up front so a long
    // thread does not grow this vector a dozen times a frame.
    const size_t cap = static_cast<size_t>(railH / kMinDotH) + 2;
    out.reserve(n < cap ? n : cap);

    float top = firstTop;
    Slot cur;
    bool open = false;
    for (size_t i = 0; i < n; ++i) {
        if (!open) {
            cur = Slot{static_cast<int>(i), top, 0.0f, marks[i]};
            open = true;
        } else if (mark_priority(marks[i]) < mark_priority(cur.mark)) {
            cur.mark = marks[i];
        }
        cur.height += heights[i];
        top += heights[i];
        if (slot_h(cur.height, totalH, railH) >= kMinDotH) {
            out.push_back(cur);
            open = false;
        }
    }
    // The tail: whatever is left has not earned a full dot, and dropping it
    // would lose the end of the thread from the rail — the one place a reader
    // is most likely to aim at. It gets its own short slot.
    if (open) out.push_back(cur);
    return out;
}

}  // namespace hanabi::minimap
