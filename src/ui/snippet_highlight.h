#pragma once

// ---------------------------------------------------------------------------
// The matched words in a sidebar search result, lit up.
//
// [APP-WORKAROUND] Same shape as find_highlight.h and the same reason: nothing
// in afterhours will say where a byte range of a label landed on screen
// (afterhours_gaps.md #51), so the only way to paint behind the matched run is
// to redo the layout the renderer just did. That arithmetic is NOT copied here
// — this calls find_highlight::paint_bands, which owns the one transcription
// of the renderer's constants. What is separate is the TALLY: find's band
// count is asserted against find's own painting, and a snippet band landing in
// that number would break the one reading that can corroborate find's tally at
// all (tests/ui/find_counts_only_what_it_could_paint.e2e). So the counters are
// two, and they are two on purpose.
//
// Fragility, inherited whole: the bands are placed by re-measuring the text
// with the renderer's wrap inset, line pitch, vertical centering and left
// margin. Any of those changing upstream slides every band off its word with
// nothing failing to build.
// ---------------------------------------------------------------------------

#include <string>

#include "../util/textscan.h"
#include "find_highlight.h"
#include "snippet_text.h"
#include "theme.h"

namespace hanabi::snippet_highlight {

using snippet_text::extract;
using snippet_text::kContext;

// Bands painted since the last read. Its own counter, deliberately not find's
// (see the header comment). Test-only readers: the sidebar's audit label.
inline int& band_count() {
    static int n = 0;
    return n;
}
inline int take_band_count() {
    const int n = band_count();
    band_count() = 0;
    return n;
}

// Paint a band behind every occurrence of `query` in `text`, laid out inside
// `rect` at `fontPx`. Counted into THIS file's tally, never find's.
inline void draw(RectangleType rect, const std::string& text,
                 const std::string& query, float fontPx) {
    // A quieter band than find's: the sidebar is a list being filtered, not a
    // reader hunting one occurrence at a time.
    find_highlight::paint_bands(
        rect, text, query, fontPx,
        theme::over(theme::find_match(), theme::sidebar_bg()), band_count());
}

}  // namespace hanabi::snippet_highlight
