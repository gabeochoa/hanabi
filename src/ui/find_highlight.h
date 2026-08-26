#pragma once

// ---------------------------------------------------------------------------
// Match highlighting for find-in-conversation.
//
// Painting a highlight behind a run of text means knowing where that run
// LANDED — which line the renderer wrapped it onto and how far along that line
// it starts. The app does not lay the text out; afterhours does, inside
// draw_text_in_rect. There is no "where is byte N of this label on screen"
// query, so the only way to answer it is to redo the layout the renderer just
// did, with the same inputs, and hope the two agree (afterhours_gaps.md #51).
//
// That is what this does. It is not a re-implementation: it calls afterhours'
// OWN wrapping primitive (ui::detail::wrap_text_to_width) with its own measure
// function, so the break decisions are identical by construction rather than by
// imitation. The remaining agreement is arithmetic copied from rendering.h:
//
//   max_width  = rect.width - 10        (the soft-wrap inset)
//   line_h     = measure("Ag")          (the wrapped-line pitch)
//   block y    = rect.y + max(0, (rect.height - line_h * lines) / 2)
//                                       (wrapped blocks are vertically centred)
//   text x     = rect.x + 5             (the left text margin)
//
// Every one of those is a constant read out of the renderer, and every one of
// them is a thing that can change on the other side of a version bump without
// this file knowing. See the gap entry for the API that would make it stop
// being a copy.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "../../vendor/afterhours/src/plugins/ui/text_selection.h"
#include "../util/textscan.h"
#include "theme.h"

namespace hanabi::find_highlight {

// The renderer's constants, named rather than sprinkled.
inline constexpr float kWrapInset = 10.0f;  // rect.width - this = wrap width
inline constexpr float kTextMarginX = 5.0f;
inline constexpr float kVPad = 1.0f;  // trims the band off the line box

using textscan::occurrences;

// How many bands were painted since the last read.
//
// This is NOT the find bar's tally and must not be read as it. The tally is
// every paintable match in the thread; a band is painted only for a message
// the virtualization window actually built, so `bands <= tally`, with equality
// only when the whole thread fits the window. The rule that does hold is the
// one on the other side: nothing is counted that this could not paint if the
// message were on screen — same rows, same normalization, same exclusions
// (tests/ui/find_counts_only_what_it_could_paint.e2e), and the tally holds
// still while the bands come and go under a scroll
// (tests/ui/find_counts_the_thread_not_the_window.e2e).
//
// The scripted harness cannot see pixels — a band is drawn, never registered
// as text. Counting them here is the only way a test can hold the painting and
// the count against each other rather than against two readings of the same
// code.
inline int& band_count() {
    static int n = 0;
    return n;
}
inline int take_band_count() {
    const int n = band_count();
    band_count() = 0;
    return n;
}

// Paint a band in `band` behind every occurrence of `query` in `text`, as that
// text is laid out inside `rect` at `fontPx`, and add each one to `tally`. A
// no-op when the query is empty or the font manager is not up yet.
//
// The geometry lives here ONCE and takes its tally as a parameter, because a
// second caller appeared (the sidebar's search snippets) and the two must not
// share a counter: find's band count is asserted against find's own painting
// (tests/ui/find_counts_only_what_it_could_paint.e2e), and a band painted
// somewhere else entirely landing in that count would break the one reading
// that can corroborate the tally at all. Copying the arithmetic instead would
// have been worse — it is a transcription of the renderer's private constants
// (gap #51), and two transcriptions rot independently.
inline void paint_bands(RectangleType rect, const std::string& text,
                        const std::string& query, float fontPx,
                        theme::Color band, int& tally) {
    if (query.empty() || text.empty() || rect.width <= 0.0f) return;
    auto* fm = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    if (fm == nullptr) return;

    const afterhours::Font font = fm->get_active_font();
    constexpr float kSpacing = 1.0f;  // 1 + letter_spacing, and body text has none
    const auto measure = [&](const std::string& s) {
        return afterhours::measure_text(font, s.c_str(), fontPx, kSpacing).x;
    };

    const float maxW = rect.width - kWrapInset;
    if (maxW <= 0.0f) return;
    const std::vector<std::string> lines =
        afterhours::ui::detail::wrap_text_to_width(text, maxW, measure);
    const float lineH =
        afterhours::measure_text(font, "Ag", fontPx, kSpacing).y;
    const float blockH = lineH * static_cast<float>(lines.size());
    const float y0 = rect.y + std::max(0.0f, (rect.height - blockH) * 0.5f);

    // Match over the WHOLE line, then place the hit on the wrapped ones.
    //
    // Searching each wrapped line separately is what a straddling multi-word
    // query falls through: the wrapper consumes the whitespace at a break, so
    // "6 failures" broken across two rendered lines is in neither of them,
    // while collect_matches — which scans the logical line — counts it. That
    // is a match no scroll can ever bring under a band, and it breaks the one
    // rule the tally rests on (docs/SEARCH.md S12).
    //
    // The mapping is reconstructed rather than asked for, because the wrapper
    // returns only the lines. It is exact rather than a guess, and the reason
    // is a property of the wrapper worth naming: it breaks ONLY between
    // whitespace-separated chunks, never inside a word (a word wider than the
    // line gets a line to itself, uncut), and it never rewrites a byte —
    // "hard-broken text round-trips byte for byte", text_selection.h. So each
    // wrapped line is a contiguous substring of the original, in order, and
    // find() from the previous line's end locates it. See afterhours_gaps.md
    // #366 for the API that would make this stop being a reconstruction.
    std::vector<size_t> lineAt(lines.size(), 0);
    for (size_t i = 0, from = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            lineAt[i] = from;
            continue;
        }
        const size_t at = text.find(lines[i], from);
        if (at == std::string::npos) return;  // not our text; paint nothing
        lineAt[i] = at;
        from = at + lines[i].size();
    }

    for (size_t off : occurrences(text, query)) {
        const size_t end = off + query.size();
        // One tally per MATCH, one rectangle per line it lands on. A hit
        // across a break is one match the reader can see, drawn in two
        // pieces — counting it twice would make the audit disagree with the
        // find bar for a reason that is not a defect.
        bool counted = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            const size_t lo = lineAt[i];
            const size_t hi = lo + lines[i].size();
            if (hi <= off || lo >= end) continue;
            const size_t a = off > lo ? off - lo : 0;
            const size_t b = (end < hi ? end : hi) - lo;
            if (b <= a) continue;
            const float x0 =
                rect.x + kTextMarginX + measure(lines[i].substr(0, a));
            const float w = measure(lines[i].substr(a, b - a));
            if (w <= 0.0f) continue;
            if (!counted) {
                ++tally;
                counted = true;
            }
            afterhours::draw_rectangle_rounded(
                RectangleType{x0, y0 + lineH * static_cast<float>(i) + kVPad,
                              w, lineH - kVPad * 2.0f},
                0.25f, 6, band);
        }
    }
}

// Find's own bands: the find colour, counted into find's tally.
inline void draw(RectangleType rect, const std::string& text,
                 const std::string& query, float fontPx) {
    paint_bands(rect, text, query, fontPx,
                theme::over(theme::find_match(), theme::panel_bg()),
                band_count());
}

}  // namespace hanabi::find_highlight
