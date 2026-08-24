#pragma once

// The sidebar footer's arithmetic, with nothing graphical in it.
//
// Split out of `sidebar_footer_status.h` so a unit test can call it. The band
// holds one fact a scripted test cannot reach and a screenshot cannot report:
// whether a shape's origin lands on a whole pixel. `assert_ui` reads
// x/y/w/h/hidden/text (gap #86) and ROUNDS what it reports, so a widget asked
// for y=932.5 reads back as 933 and a widget asked for 932.4 reads back as
// 932 -- the assertion cannot tell a snapped position from an unsnapped one
// that happens to round the same way. The property is arithmetic, so it is
// tested as arithmetic, exactly as `tab_colors.h` is.
//
// Why it matters here: afterhours never rounds a position. Grid snapping is
// off in hanabi (`preload.cpp`) and only ever snapped SIZES anyway, so a
// fractional origin reaches a rasterizer with no antialiasing (gap #92) and a
// 6px box at y=932.5 comes out FIVE rows tall. The shipped activity light was
// 6 wide and 5 tall -- an ellipse -- because its x happened to land whole and
// its y was the band's odd 10.5px inset. Gap #110.

#include <cmath>

namespace ecs::footer_status {

// Puffin's footer is `.padding(.horizontal, 10)` on the whole HStack
// (`SidebarColumn.sidebarFooter`), so its version label's INK begins at x=10
// and the reference confirms it to the pixel. Everything in this band that is
// positioned from the sidebar's leading edge measures from here.
constexpr float kFooterPadX = 10.0f;

// The action cluster is three 22px buttons centred on panelW-70 / -46 / -22,
// so the leftmost begins at panelW-81; the count ends kActionGap short of it.
constexpr float kActionsLeft = 81.0f;
constexpr float kActionGap = 10.0f;
constexpr float kDot = 6.0f;
constexpr float kDotGap = 6.0f;
// afterhours insets label text 5px from its box on every alignment, with no
// way off (afterhours_gaps.md #84). A label's box therefore starts 5px left of
// where its ink is wanted.
constexpr float kAhTextInset = 5.0f;
// Below this the version label and the count would collide, so the count is
// dropped rather than overlapped. Only reachable mid-expand-tween: the folded
// rail returns before the footer is drawn at all.
constexpr float kMinPanelW = 210.0f;

// A label box positioned so its INK begins at `ink_x`.
constexpr float label_box_x(float ink_x) { return ink_x - kAhTextInset; }

// The activity light's origin, snapped to whole pixels on both axes.
//
// floor, not round, on y: the band's own centre is a half pixel below the
// count's ink centre, because a label's ascent is taller than its descent.
// Measured on the reference-sized frame -- band y922..948 centres on 935 and
// the count's ink runs y931..937, centre 934 -- so flooring the half lands the
// light on the TEXT rather than on the box.
//
// x is snapped for a reason that is easy to miss: it is derived from
// `text_px` of a string whose length changes with the catalog, so without
// this it is the COLUMN that goes missing on some session counts and the row
// on others.
inline float dot_y(float band_top, float band_h) {
    return std::floor(band_top + (band_h - kDot) * 0.5f);
}
inline float dot_x(float text_left) {
    return std::floor(text_left - kDotGap - kDot);
}

}  // namespace ecs::footer_status
