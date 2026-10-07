#pragma once

// ---------------------------------------------------------------------------
// The collapsed sidebar's session dots (the reference's SidebarRailDots,
// D122996657..D122996666), as arithmetic.
//
// The folded rail draws one dot per thread Home lists -- not archived, not a
// thread an automation started (unless it is pinned or the reader asked to see
// automations), top level only -- in list order. Each dot is 10px across on a
// 19px row (the dot plus a 9px gap), so the whole rail width and the gap are
// one click target. A 10px slot above and below the dots is always reserved
// for an edge arrow, which shows only while dots are scrolled out of view that
// way; clicking it scrolls a screenful less one row. Resting 0.2 s on a dot
// opens a card naming the thread and quoting the last message you sent it;
// once a card is up, moving to another dot switches it at once, and leaving
// the dots closes it after a 0.12 s grace.
//
// Everything here is a pure function of numbers so the unit test can pin it
// (tests/unit/test_sidebar_rail.cpp); sidebar_system.h draws it.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../api/types.h"
#include "sidebar_buckets.h"

namespace ecs::rail {

inline constexpr float kDotDiameter = 10.0f;
inline constexpr float kDotSpacing = 9.0f;
inline constexpr float kRowH = kDotDiameter + kDotSpacing;
inline constexpr float kArrowSlotH = 10.0f;
inline constexpr float kArrowGlyphPx = 9.0f;
inline constexpr double kHoverDelayS = 0.2;
inline constexpr double kSwitchGraceS = 0.12;
// The card: 300 wide, 10 padding plus the 6px arrow on its leading edge, 5px
// corners, a 12x6 arrow, 2px off the rail, never nearer than 4px to the
// rail's top or bottom; its arrow rests level with the title line.
inline constexpr float kCardW = 300.0f;
inline constexpr float kCardPad = 10.0f;
inline constexpr float kCardCorner = 5.0f;
inline constexpr float kCardArrowW = 12.0f;
inline constexpr float kCardArrowDepth = 6.0f;
inline constexpr float kCardGap = 2.0f;
inline constexpr float kCardMargin = 4.0f;
inline constexpr float kCardArrowRestY = 18.0f;
inline constexpr int kCardTitleLines = 2;
inline constexpr int kCardMessageLines = 4;

// The threads Home lists, the rail's dots: the same cut the sidebar's own
// buckets make (SidebarBuckets::rebuild), with no search and no saved view.
inline bool shows(const api::SessionSummary& s, bool showAutomation,
                  bool hideAutomatedTitles = false) {
    if (!s.parent_id.empty()) return false;
    if (model::is_archived(s)) return false;
    if (!s.starred && !showAutomation && api::is_automation_born(s)) return false;
    if (hideAutomatedTitles && model::is_automated_title(s.title)) return false;
    return true;
}

// Which part of the dot column is on screen, in the column's own coordinates.
struct Scroll {
    float top = 0.0f;
    float visibleH = 0.0f;
    float contentH = 0.0f;

    // Under half a pixel is rounding at an end, not a dot out of view.
    static constexpr float kSlack = 0.5f;

    [[nodiscard]] float max_top() const { return std::max(0.0f, contentH - visibleH); }
    [[nodiscard]] bool hides_above() const { return top > kSlack; }
    [[nodiscard]] bool hides_below() const { return top + visibleH < contentH - kSlack; }
    [[nodiscard]] float clamp(float t) const { return std::min(std::max(t, 0.0f), max_top()); }
    // A screenful less one row, so the dot at the edge stays in view.
    [[nodiscard]] float page_target(bool down) const {
        const float step = std::max(kRowH, visibleH - kRowH);
        return clamp(top + (down ? step : -step));
    }
};

inline float content_h(std::size_t dots) { return static_cast<float>(dots) * kRowH; }

// The y of dot `i`'s centre in the column (unscrolled).
inline float dot_center(std::size_t i) { return (static_cast<float>(i) + 0.5f) * kRowH; }

// Whether row `i`'s dot centre is on screen, so a card never points at a dot
// scrolled out of view.
inline bool row_visible(const Scroll& s, std::size_t i) {
    const float c = dot_center(i);
    return c >= s.top && c <= s.top + s.visibleH;
}

// The row under a y measured from the top of the visible column, or -1.
inline int row_at(const Scroll& s, float yInColumn, std::size_t dots) {
    if (yInColumn < 0.0f || yInColumn >= s.visibleH) return -1;
    const float y = yInColumn + s.top;
    const int row = static_cast<int>(std::floor(y / kRowH));
    if (row < 0 || static_cast<std::size_t>(row) >= dots) return -1;
    return row;
}

// Where the card's top sits and where along its leading edge the arrow points,
// so the arrow meets the dot while the card stays inside the rail.
struct CardPlacement {
    float top = 0.0f;
    float arrowY = 0.0f;
};
inline CardPlacement card_placement(float dotY, float cardH, float railTop, float railH) {
    const float preferred = dotY - kCardArrowRestY;
    const float lowest = std::max(railTop + kCardMargin, railTop + railH - kCardMargin - cardH);
    const float top = std::min(std::max(preferred, railTop + kCardMargin), lowest);
    return CardPlacement{top, dotY - top};
}

// The arrow tip's y on the card, clamped clear of the rounded corners.
inline float arrow_tip(float arrowY, float cardH) {
    const float half = kCardArrowW * 0.5f;
    return std::min(std::max(arrowY, kCardCorner + half), cardH - kCardCorner - half);
}

// The hover state machine: which dot the pointer is on, which card is up, and
// when the pending change lands. `step` is called once a frame with the dot
// under the pointer ("" for none) and the time.
struct Hover {
    std::string hovered;
    std::string shown;
    double pendingAt = -1.0;  // when `shown` becomes `hovered`; < 0 = nothing pending

    void step(const std::string& under, double now) {
        if (under != hovered) {
            hovered = under;
            if (!hovered.empty() && !shown.empty()) {
                shown = hovered;  // a card already up follows at once
                pendingAt = -1.0;
            } else {
                pendingAt = now + (hovered.empty() ? kSwitchGraceS : kHoverDelayS);
            }
        }
        if (pendingAt >= 0.0 && now >= pendingAt) {
            shown = hovered;
            pendingAt = -1.0;
        }
    }
    void close() {
        hovered.clear();
        shown.clear();
        pendingAt = -1.0;
    }
};

// The last message the reader sent a thread, from what this client holds: the
// newest User message with any text. Empty when there is none.
inline std::string last_sent(const std::vector<api::Message>& messages) {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role != api::Role::User) continue;
        const std::string& t = it->text;
        if (t.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        return t;
    }
    return std::string();
}

}  // namespace ecs::rail
