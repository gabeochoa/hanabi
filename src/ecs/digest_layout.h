#pragma once

// The geometry of a digest card list, with no UI in it.
//
// WHY THIS FILE EXISTS. `render_digest` built one card per matching session,
// unconditionally, on all four digest screens (Blocked / Review / Starred /
// Archived). At a 2000-session catalog Blocked, the biggest of them, builds
// 506 cards and 2,024 entities in one frame -- eight times what Home builds
// beside it, and Home is capped.
// `docs/perf/SCROLL.md` had already solved this shape for the sidebar's rows:
// build the slice on screen plus a little slack, stand two spacers in for the
// rest, and the list stays whole while the build stays O(viewport).
//
// The sidebar could do that arithmetic inline because its rows are a fixed
// `kRowHeight`. A digest card is 34 px or 52 depending on whether its second
// line is short enough to ride inline on the title row -- so a window over
// cards has to know a card's height WITHOUT building the card, and the moment
// two places compute a height they drift. Everything in this header exists so
// there is exactly one place: `card_pitch` is what the window's arithmetic is
// made of AND what the built card adds to the running content-y, because it
// is the same call.
//
// It is a separate header from main_pane_system.h for a second reason. That
// file is 8000 lines of UI over a font manager and a graphics device; nothing
// in it can be unit-tested. This is pure arithmetic over an api::SessionSummary
// and it is covered by tests/unit/test_digest_layout.cpp -- the same move
// pane_state.h made for the per-thread caches.

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

#include "../api/types.h"
#include "../util/format.h"

namespace ecs::digest {

// True when a preview string is just a state/status label (which the grouped
// section header already conveys) rather than a discriminating detail.
inline bool is_bare_state_word(std::string_view p) {
    static const char* kStateWords[] = {
        "self-running", "running", "waiting on you", "waiting",
        "blocked", "done", "ready for review", "review", "active",
        "archived", "parked"};
    for (const char* w : kStateWords)
        if (p == w) return true;
    return false;
}

// The sub-line under a card's title, and the whole of why a card is 34 px or
// 52. Two spellings, because a card in a HOMOGENEOUS list (Blocked, Review --
// the header already names the state) shows only what discriminates it from
// its siblings, while a card in a MIXED list (Starred, Home's Recent) states
// its own state.
//
// Both return a VIEW, and that is the difference between the window being free
// and the window being the new cost. Every branch that does not compose points
// straight into the session -- the preview, its tail after the separator, a
// string literal -- and the two that do compose write into a caller-owned
// scratch that the pass reuses, so a 506-card pitch pass heap-allocates once
// (the first frame, sizing the scratch) rather than 506 times a frame.
//
// The scratch MUST outlive the returned view. It is a parameter and not a
// local static for the obvious reason and one less obvious one: two calls in
// the same expression would otherwise stomp each other.

// Sub-line for a card rendered INSIDE a homogeneous grouped section (Home's
// Waiting / Finished / Self-running, and the single-state digest views). The
// section HEADER already names the state (and carries its color), so restating
// "waiting on you" on every card -- plus a red BLOCKED chip -- was the same
// fact three times (v5 defect #4: "7 identical red chips = noise"). In grouped
// mode we therefore drop the chip and show only the DISCRIMINATING detail that
// actually differs between sibling cards: the age, or a running card's
// progress ("61%", "tests", "landing"). We derive it by stripping a leading
// state phrase from the mock preview ("waiting on you \xc2\xb7 22m" -> "22m",
// "self-running \xc2\xb7 61%" -> "61%"); a real backend (no preview) falls back to
// the relative age.
inline std::string_view grouped_meta(const api::SessionSummary& s,
                                     std::string& scratch) {
    if (!s.preview.empty()) {
        // Take the detail AFTER the first " \xc2\xb7 " separator, i.e. drop the
        // redundant leading state phrase the section header already conveys.
        const std::string_view sep = "\xc2\xb7";
        const std::string_view preview = s.preview;
        const size_t p = preview.find(sep);
        if (p != std::string_view::npos) {
            std::string_view tail = preview.substr(p + sep.size());
            // trim surrounding spaces
            const size_t a = tail.find_first_not_of(' ');
            const size_t b = tail.find_last_not_of(' ');
            if (a != std::string_view::npos) return tail.substr(a, b - a + 1);
        }
        // No separator: the preview is a BARE phrase with no discriminating
        // detail. If it merely restates the section's state word (e.g.
        // "self-running" under the SELF-RUNNING header, "running", "waiting on
        // you"), echoing it is the exact redundancy grouped mode exists to kill
        // (v5 #4) -- so fall back to the age instead. Only a preview that
        // carries REAL detail (not a state label) is kept verbatim.
        if (!is_bare_state_word(preview)) return preview;
    }
    scratch = fmtutil::relative_time(s.updated_at);
    return scratch;  // no discriminating detail: the age is what differs.
}

// Sub-line for a card in a MIXED list. The sub-line must NEVER contradict or
// redundantly restate the derived chip sitting above it. The old code leaked
// the RAW api status word ("active") beneath a derived BLOCKED/DONE chip -- so
// every WAITING card read "3h \xc2\xb7 active" under a red BLOCKED pill and every
// FINISHED card read "1d \xc2\xb7 active" under a DONE pill: two lines fighting each
// other, reads as broken software. Instead, when a card carries a derived
// chip/state we compose a state-MATCHED second token from the SAME derived
// verdict, and deliberately drop the raw status word. Only a genuinely calm
// card (no chip, Unknown state) may fall back to a neutral age line.
inline std::string_view card_meta(const api::SessionSummary& s,
                                  std::string& scratch) {
    if (!s.preview.empty()) return s.preview;  // mock: keep rich preview.
    const std::string age = fmtutil::relative_time(s.updated_at);
    std::string_view phrase;  // the state-matched verdict word.
    switch (s.tag) {
        case api::ThreadTag::Blocked:
        case api::ThreadTag::Waiting:
            // One phrase for the pair -- both mean "this needs me". The chip
            // beside it (BLOCKED / WAITING) is what says which kind.
            phrase = "waiting on you";
            break;
        case api::ThreadTag::Done:
            phrase = "done";  // matches the DONE chip.
            break;
        case api::ThreadTag::Review:
            phrase = "ready for review";  // matches the REVIEW chip.
            break;
        default:
            // No tag chip. Derive from state so a RUNNING card (green RUNNING
            // chip) reads "running \xc2\xb7 <age>", and a calm/archived card gets a
            // NEUTRAL age-first line -- never the raw "active" status word.
            switch (s.state) {
                case api::ThreadState::Running: phrase = "running"; break;
                case api::ThreadState::Archived: phrase = "archived"; break;
                default: break;  // calm: age only.
            }
            break;
    }
    if (phrase.empty()) {
        // Genuinely calm card with no chip: a neutral relative age reads as
        // "last active <age>" without restating a raw session-status word.
        if (age.empty()) return {};
        scratch.assign("last active ").append(age);
        return scratch;
    }
    // Chip-bearing / stateful card: lead with the state-matched verdict, then
    // the age -- e.g. "waiting on you  \xc2\xb7  3h", "done  \xc2\xb7  1d". Never "active".
    if (age.empty()) return phrase;
    scratch.assign(phrase).append("  \xc2\xb7  ").append(age);
    return scratch;
}

inline std::string_view sub_line(const api::SessionSummary& s, bool grouped,
                                 std::string& scratch) {
    return grouped ? grouped_meta(s, scratch) : card_meta(s, scratch);
}

// A "sparse" sub-line is short enough, and plain enough, to ride inline on the
// title row instead of taking a second line of its own: six bytes and no
// separator, which is an age ("22m", "3h", "12mo") and nothing else.
inline bool sub_is_sparse(std::string_view sub) {
    return sub.empty() ||
           (sub.size() <= 6 && sub.find("\xc2\xb7") == std::string_view::npos);
}

inline constexpr float kCardMarginTop = 3.0f;
inline constexpr float kCardMarginBot = 5.0f;
inline constexpr float kCardSparseH = 34.0f;
inline constexpr float kCardRichH = 52.0f;

inline float card_body_height(std::string_view sub) {
    return sub_is_sparse(sub) ? kCardSparseH : kCardRichH;
}

// What one card costs the column: its body plus both margins.
inline float card_pitch(std::string_view sub) {
    return kCardMarginTop + card_body_height(sub) + kCardMarginBot;
}

inline float card_pitch(const api::SessionSummary& s, bool grouped,
                        std::string& scratch) {
    return card_pitch(sub_line(s, grouped, scratch));
}

// ---- the window -------------------------------------------------------
//
// Which slice of the matched cards is worth BUILDING this frame. The list is
// never shortened: `above` and `below` are the exact heights of the cards that
// were skipped, and two spacer divs of those heights keep the content size,
// the scrollbar thumb, the clamp and the y of any given card at the numbers
// they would have been with all of them there. Nothing is truncated and
// nothing is hidden -- which is the whole reason this is not a cap. Blocked's
// job is to show everything blocked on you, and it still does.
struct CardWindow {
    int first = 0;    // index of the first card built
    int last = 0;     // one past the last card built
    float above = 0.0f;   // exact height of cards [0, first)
    float below = 0.0f;   // exact height of cards [last, n)
    float total = 0.0f;   // exact height of all of them
    [[nodiscard]] int built() const { return last - first; }
    [[nodiscard]] bool whole(int n) const { return first == 0 && last == n; }
};

// Slack kept on each side of the viewport, in pixels.
//
// The build runs before autolayout and before ease_scroll, so the offset read
// here is a frame stale by exactly the distance the easing is about to travel
// -- which is `pending` below, and is therefore covered exactly rather than
// guessed at. That is what lets the constant be small: it is not a fling
// budget, it is slack against rounding and against the fact that the card
// under the top edge may be a tall one. 180 px is three of the tallest cards.
inline constexpr float kOverscanPx = 180.0f;
// A fling of more than this in one frame is a fling, and one frame of partial
// fill inside it is not something a person can see -- whereas a window sized
// to an unbounded fling is a fling that costs what the whole list used to.
inline constexpr float kFlingOverscanPx = 1920.0f;
// The card ids run base+1 .. base+kMaxWindowCards and the two spacers sit
// below base, so a window this size cannot reach them. It is also 12x the
// tallest viewport anyone has: 240 cards at the sparse pitch is a 10,080 px
// column.
inline constexpr int kMaxWindowCards = 240;

// `pitch(i)` gives card i's height. Templated on the accessor so the caller
// can compute it however it likes -- the real one walks sessions and calls
// card_pitch, the test hands in a table -- and so that this stays a header of
// arithmetic with no api:: dependency in the loop.
//
// `viewH <= 0` means nothing has been measured yet (frame one). Build the lot;
// by frame two there is a viewport to read.
template <typename Pitch>
CardWindow card_window(int n, Pitch&& pitch, float viewH, float offsetY,
                       float targetY) {
    CardWindow w;
    if (n <= 0) return w;
    if (viewH <= 0.0f) {
        w.last = n;
        for (int i = 0; i < n; ++i) w.total += pitch(i);
        return w;
    }

    // Where the view is ABOUT to be, not only where it is.
    const float pending = std::fabs(targetY - offsetY);
    const float overscan =
        kOverscanPx + std::min(kFlingOverscanPx, pending);
    const float top = offsetY - overscan;
    const float bot = offsetY + viewH + overscan;

    int first = -1;
    int last = -1;
    float y = 0.0f;
    float yFirst = 0.0f;
    float yLast = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float h = pitch(i);
        if (first < 0 && y + h > top) {
            first = i;
            yFirst = y;
        }
        if (first >= 0 && last < 0 &&
            (y >= bot || i - first >= kMaxWindowCards)) {
            last = i;
            yLast = y;
        }
        y += h;
    }
    w.total = y;
    if (first < 0) {
        // The whole list is above the viewport, which happens for one frame
        // after the list shrinks under a scrolled-down view. Build the tail;
        // the clamp puts the view back on it next frame.
        first = n;
        yFirst = y;
    }
    if (last < 0) {
        last = n;
        yLast = y;
    }
    w.first = first;
    w.last = last;
    w.above = yFirst;
    w.below = y - yLast;
    return w;
}

template <typename Pitch>
CardWindow section_window(int n, Pitch&& pitch, float viewH, float offsetY,
                          float targetY, float sectionY) {
    return card_window(n, std::forward<Pitch>(pitch), viewH,
                       offsetY - sectionY, targetY - sectionY);
}

}  // namespace ecs::digest
