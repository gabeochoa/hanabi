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
#include "theme.h"

namespace hanabi::find_highlight {

// The renderer's constants, named rather than sprinkled.
inline constexpr float kWrapInset = 10.0f;  // rect.width - this = wrap width
inline constexpr float kTextMarginX = 5.0f;
inline constexpr float kVPad = 1.0f;  // trims the band off the line box

// Case-insensitive byte offsets of every occurrence of `needle` in `hay`.
// Non-overlapping: the scan resumes past each hit, so "aa" in "aaa" is one.
inline std::vector<size_t> occurrences(const std::string& hay,
                                       const std::string& needle) {
    std::vector<size_t> out;
    if (needle.empty() || hay.size() < needle.size()) return out;
    const auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    };
    for (size_t i = 0; i + needle.size() <= hay.size();) {
        bool hit = true;
        for (size_t j = 0; j < needle.size(); ++j)
            if (lower(hay[i + j]) != lower(needle[j])) { hit = false; break; }
        if (hit) { out.push_back(i); i += needle.size(); }
        else ++i;
    }
    return out;
}

// Paint a band behind every occurrence of `query` in `text`, as that text is
// laid out inside `rect` at `fontPx`. A no-op when the query is empty or the
// font manager is not up yet.
inline void draw(RectangleType rect, const std::string& text,
                 const std::string& query, float fontPx) {
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

    const theme::Color band = theme::over(theme::find_match(), theme::panel_bg());
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        for (size_t off : occurrences(ln, query)) {
            const float x0 = rect.x + kTextMarginX + measure(ln.substr(0, off));
            const float w = measure(ln.substr(off, query.size()));
            if (w <= 0.0f) continue;
            afterhours::draw_rectangle_rounded(
                RectangleType{x0, y0 + lineH * static_cast<float>(i) + kVPad,
                              w, lineH - kVPad * 2.0f},
                0.25f, 6, band);
        }
    }
}

}  // namespace hanabi::find_highlight
