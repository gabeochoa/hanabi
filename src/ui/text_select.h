#pragma once

// ---------------------------------------------------------------------------
// Selecting text in the transcript.
//
// afterhours has no selection on read-only text (afterhours_gaps.md #37): drag
// across an answer and nothing highlights, and there is no API that says what
// is selected. For an app whose whole surface is text somebody wants to keep,
// that is the first thing a user tries.
//
// This is the app-side version. It reuses the trick find_highlight.h
// establishes — call afterhours' OWN wrapping primitive so the line breaks
// agree by construction, then do the renderer's positioning arithmetic — and
// adds the two things selection needs on top: a point-to-byte-offset mapping
// for the press and the drag, and a byte range to paint and to copy.
//
// SCOPE: a selection lives inside ONE rendered text element — one line of an
// assistant turn, or one user bubble. Dragging past the end clamps there rather
// than continuing into the next paragraph. Selection ACROSS elements needs a
// document order over the widget tree, which is a real design question and is
// left to the library (see the gap entry); within an element covers the actual
// need, which is grabbing a value, a path, or a sentence.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../../vendor/afterhours/src/plugins/clipboard.h"
#include "../../vendor/afterhours/src/plugins/ui/text_selection.h"
#include "find_highlight.h"
#include "theme.h"

namespace hanabi::text_select {

// The live selection. One at a time, keyed by the element that owns it, so a
// press anywhere else drops it.
struct State {
    afterhours::EntityID owner = -1;
    std::string text;    // the element's text, as laid out
    float fontPx = 13.0f;
    size_t anchor = 0;   // where the drag began
    size_t cursor = 0;   // where it is now
    bool dragging = false;

    [[nodiscard]] bool has_range() const {
        return owner != -1 && anchor != cursor;
    }
    [[nodiscard]] size_t begin() const { return anchor < cursor ? anchor : cursor; }
    [[nodiscard]] size_t end() const { return anchor < cursor ? cursor : anchor; }
};

// A second press within this long, and this close, continues a click run.
inline constexpr int kMultiClickMs = 400;
inline constexpr float kMultiClickSlopPx = 4.0f;

inline State& state() {
    static State s;
    return s;
}

inline void clear() { state() = State{}; }

// A press that lands on no selectable text drops the selection — the same way
// clicking empty space does anywhere else. The transcript brackets its render
// with these: nothing claims the press, the selection goes.
inline bool& claimed_this_frame() {
    static bool v = false;
    return v;
}
inline void begin_frame() { claimed_this_frame() = false; }
inline void end_frame(bool justPressed) {
    if (justPressed && !claimed_this_frame()) clear();
}

// The selected text, for the clipboard. Empty when nothing is selected.
inline std::string selected_text() {
    const State& s = state();
    if (!s.has_range() || s.begin() >= s.text.size()) return "";
    return s.text.substr(s.begin(), s.end() - s.begin());
}

namespace detail {

// The renderer's layout for `text` in `rect`, as wrapped lines plus the block's
// top-left. Shared by the hit test and the paint so they cannot disagree.
struct Layout {
    std::vector<std::string> lines;
    float lineH = 0.0f;
    float x0 = 0.0f;  // where a line's glyphs start
    float y0 = 0.0f;  // top of the first line's box
    bool ok = false;
};

inline Layout layout_of(RectangleType rect, const std::string& text,
                        float fontPx) {
    Layout out;
    if (text.empty() || rect.width <= 0.0f) return out;
    auto* fm = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    if (fm == nullptr) return out;
    const afterhours::Font font = fm->get_active_font();
    constexpr float kSpacing = 1.0f;
    const auto measure = [&](const std::string& s) {
        return hanabi::atlas::check(
            s, fontPx,
            afterhours::measure_text(font, s.c_str(), fontPx, kSpacing).x);
    };
    const float maxW = rect.width - find_highlight::kWrapInset;
    if (maxW <= 0.0f) return out;
    out.lines = afterhours::ui::detail::wrap_text_to_width(text, maxW, measure);
    out.lineH = afterhours::measure_text(font, "Ag", fontPx, kSpacing).y;
    const float blockH = out.lineH * static_cast<float>(out.lines.size());
    out.y0 = rect.y + std::max(0.0f, (rect.height - blockH) * 0.5f);
    out.x0 = rect.x + find_highlight::kTextMarginX;
    out.ok = true;
    return out;
}

// Byte offset into `text` (as the joined wrapped lines) nearest a point.
inline size_t offset_at(const Layout& lay, const std::string& text,
                        float px, float py, float fontPx) {
    if (!lay.ok || lay.lines.empty()) return 0;
    auto* fm = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    if (fm == nullptr) return 0;
    const afterhours::Font font = fm->get_active_font();
    const auto measure = [&](const std::string& s) {
        return hanabi::atlas::check(
            s, fontPx,
            afterhours::measure_text(font, s.c_str(), fontPx, 1.0f).x);
    };

    int row = static_cast<int>((py - lay.y0) / lay.lineH);
    if (row < 0) row = 0;
    if (row >= static_cast<int>(lay.lines.size()))
        row = static_cast<int>(lay.lines.size()) - 1;

    // Offset of this line's first byte within `text`. The wrap consumes the
    // space it broke at, so the joined lines are one byte shorter per soft
    // break — walk the source instead of summing line lengths, so an offset
    // always indexes the ORIGINAL string and the copied substring is exact.
    size_t base = 0;
    {
        size_t pos = 0;
        for (int i = 0; i < row; ++i) {
            const std::string& ln = lay.lines[static_cast<size_t>(i)];
            const size_t at = text.find(ln, pos);
            if (at == std::string::npos) break;
            pos = at + ln.size();
            base = pos;
            while (base < text.size() &&
                   (text[base] == ' ' || text[base] == '\n'))
                ++base;
        }
    }

    const std::string& line = lay.lines[static_cast<size_t>(row)];
    const float local = px - lay.x0;
    size_t best = 0;
    float bestDist = std::abs(local);
    for (size_t i = 0; i < line.size();) {
        const size_t len =
            afterhours::ui::utf8_char_length(line, i);
        const size_t next = i + (len > 0 ? len : 1);
        const float w = measure(line.substr(0, next));
        const float d = std::abs(local - w);
        if (d < bestDist) {
            bestDist = d;
            best = next;
        }
        i = next;
    }
    return base + best;
}

// Multi-click bookkeeping. Free functions over statics, to keep the module
// header-only and stateless from the caller's point of view.
inline std::chrono::steady_clock::time_point& last_press_at() {
    static std::chrono::steady_clock::time_point t{};
    return t;
}
inline float& last_press_x() { static float v = 0.0f; return v; }
inline float& last_press_y() { static float v = 0.0f; return v; }
inline int& click_run() { static int n = 0; return n; }

// The word around `off`: a run of anything that is not whitespace. Punctuation
// is kept, so double-clicking a path or an amount takes the whole of it rather
// than stopping at the first dot.
inline std::pair<size_t, size_t> word_at(const std::string& text, size_t off) {
    if (text.empty()) return {0, 0};
    if (off > text.size()) off = text.size();
    const auto is_sep = [](char c) {
        return c == ' ' || c == '\t' || c == '\n';
    };
    size_t b = off;
    while (b > 0 && !is_sep(text[b - 1])) --b;
    size_t e = off;
    while (e < text.size() && !is_sep(text[e])) ++e;
    return {b, e};
}

}  // namespace detail

// Paint the selection inside this element, if it owns one. Call from
// on_draw_bg — the band goes behind the glyphs.
inline void draw(afterhours::EntityID id, RectangleType rect,
                 const std::string& text, float fontPx) {
    const State& s = state();
    if (s.owner != id || !s.has_range()) return;
    const auto lay = detail::layout_of(rect, text, fontPx);
    if (!lay.ok) return;
    auto* fm = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    if (fm == nullptr) return;
    const afterhours::Font font = fm->get_active_font();
    const auto measure = [&](const std::string& str) {
        return hanabi::atlas::check(
            str, fontPx,
            afterhours::measure_text(font, str.c_str(), fontPx, 1.0f).x);
    };

    const theme::Color band =
        theme::over(theme::selection_bg(), theme::panel_bg());
    // Walk the wrapped lines, mapping each back to its span of the source so a
    // range that crosses a soft break paints on both lines.
    size_t pos = 0;
    for (size_t i = 0; i < lay.lines.size(); ++i) {
        const std::string& ln = lay.lines[i];
        const size_t at = text.find(ln, pos);
        if (at == std::string::npos) break;
        const size_t lineBegin = at;
        const size_t lineEnd = at + ln.size();
        pos = lineEnd;

        const size_t from = s.begin() > lineBegin ? s.begin() - lineBegin : 0;
        const size_t to = s.end() < lineEnd ? s.end() - lineBegin : ln.size();
        if (s.end() <= lineBegin || s.begin() >= lineEnd) continue;
        if (to <= from) continue;

        const float xa = lay.x0 + measure(ln.substr(0, from));
        const float xb = lay.x0 + measure(ln.substr(0, to));
        afterhours::draw_rectangle_rounded(
            RectangleType{xa, lay.y0 + lay.lineH * static_cast<float>(i) + 1.0f,
                          xb - xa, lay.lineH - 2.0f},
            0.2f, 6, band);
    }
}

// What a press resolved to, printed when HANABI_SELECT_AUDIT is set.
//
// A coordinate-addressed selection test (tests/ui/select_word_and_line.e2e)
// can only say "6 selected" or "61 selected", and both of those are true of
// more than one line of a transcript -- so when the layout moves, the test
// fails naming a count and not a place, and the reader cannot tell a broken
// selection from a stale coordinate. afterhours_gaps.md #117 is that failure
// mode; #232 is the same thing for link rects. This is the reading that tells
// them apart: the element the press landed in, the byte it resolved to, the
// click run, and the first characters of the text it is about to take.
inline void audit_press(RectangleType rect, const std::string& text, float mx,
                        float my, size_t off, int run) {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SELECT_AUDIT");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    if (!on) return;
    std::string head = text.substr(0, 120);
    for (char& c : head)
        if (c == '\n') c = ' ';
    std::printf("[sel] press=(%.1f,%.1f) run=%d off=%zu  rect=(%.1f,%.1f "
                "%.1fx%.1f) len=%zu  text=\"%s\"\n",
                mx, my, run, off, rect.x, rect.y, rect.width, rect.height,
                text.size(), head.c_str());
    std::fflush(stdout);
}

// Drive the selection for one text element. `hot` is whether the pointer is
// over it this frame. Returns nothing; it mutates the single shared state.
//
// Press starts a selection here and drops any other; motion with the button
// held extends it; release ends the drag but keeps the range (so Cmd+C works).
inline void update(afterhours::EntityID id, RectangleType rect,
                   const std::string& text, float fontPx,
                   float mx, float my, bool leftDown, bool justPressed) {
    // Hit-tested here rather than read off the context's hot element. The
    // transcript builds its widgets during the update phase, and the hit
    // resolver has not seen THIS frame's tree yet — so on the very frame the
    // button goes down, every element still reports last frame's hot, and a
    // press that arrives with the pointer already in place (which is what a
    // synthetic drag does) is missed entirely.
    //
    // The vertical bound is HALF-OPEN, and the horizontal one is not. Transcript
    // lines are stacked with no gap, so line N's bottom edge IS line N+1's top
    // edge: with `my <= rect.y + rect.height` a press on that seam is inside
    // BOTH, and which line it selects is decided by whichever element the
    // update happens to reach second. Measured at the seam between the two
    // account lines of `t2`, one press:
    //
    //   [sel] press=(415.0,234.0) run=1 rect=(329.0,218.0 656.0x16.0) len=61
    //   [sel] press=(415.0,234.0) run=2 rect=(329.0,234.0 656.0x16.0) len=58
    //
    // Two hits, one press. Nothing stacks horizontally like this, so `mx` keeps
    // its inclusive bound and the last pixel of a line stays clickable.
    const bool hot = mx >= rect.x && mx <= rect.x + rect.width &&
                     my >= rect.y && my < rect.y + rect.height;
    State& s = state();
    if (justPressed && hot) {
        // ONE PRESS IS ONE CLICK. The run below is a global counted per
        // ELEMENT HIT, and the line above is not the only way two elements can
        // both take one press -- an overlay, a nested selectable, a rect
        // rounded to the same pixel. When that happens the run increments
        // twice and a SINGLE CLICK behaves as a double click: it selects a
        // word instead of placing a caret, which is the reading in the two
        // lines above. `claimed_this_frame()` already exists to say "the
        // selection has an owner as of this frame"; this is the same fact
        // used one step earlier.
        if (claimed_this_frame()) return;
        const auto lay = detail::layout_of(rect, text, fontPx);
        if (!lay.ok) return;
        const size_t off = detail::offset_at(lay, text, mx, my, fontPx);

        // Multi-click, counted here because a press is all the UI layer
        // reports: the pointer state carries no click count, and the e2e
        // double_click/triple_click commands are simply two and three presses.
        // Same rule every desktop uses — near in time, near in space.
        const auto now = std::chrono::steady_clock::now();
        const bool nearInTime = (now - detail::last_press_at()) <
                                std::chrono::milliseconds(kMultiClickMs);
        const bool nearInSpace =
            std::abs(mx - detail::last_press_x()) < kMultiClickSlopPx &&
            std::abs(my - detail::last_press_y()) < kMultiClickSlopPx;
        detail::click_run() =
            (nearInTime && nearInSpace) ? detail::click_run() + 1 : 1;
        detail::last_press_at() = now;
        detail::last_press_x() = mx;
        detail::last_press_y() = my;

        s.owner = id;
        s.text = text;
        s.fontPx = fontPx;
        claimed_this_frame() = true;
        audit_press(rect, text, mx, my, off, detail::click_run());

        if (detail::click_run() >= 3) {
            // Triple takes the whole element: one source line of an assistant
            // turn, or one user message — the unit a reader means by "this
            // line".
            s.anchor = 0;
            s.cursor = text.size();
            s.dragging = false;
            return;
        }
        if (detail::click_run() == 2) {
            const auto w = detail::word_at(text, off);
            s.anchor = w.first;
            s.cursor = w.second;
            s.dragging = false;
            return;
        }
        s.anchor = off;
        s.cursor = off;
        s.dragging = true;
        return;
    }
    if (s.owner == id && s.dragging) {
        if (!leftDown) {
            s.dragging = false;
            return;
        }
        const auto lay = detail::layout_of(rect, text, fontPx);
        if (!lay.ok) return;
        s.cursor = detail::offset_at(lay, text, mx, my, fontPx);
    }
}

// Copy the selection. True when something was on the clipboard to put there.
inline bool copy() {
    const std::string sel = selected_text();
    if (sel.empty()) return false;
    afterhours::clipboard::set_text(sel);
    return true;
}

}  // namespace hanabi::text_select
