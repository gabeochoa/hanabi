#pragma once

// The tab strip's palette and its two rasterizer constants.
//
// Split out of tab_bar_system.h so a headless unit test can reach it. That
// header includes afterhours' clipboard plugin for the context menu's Copy
// item, and that plugin calls sapp_* without declaring them (gap #96), so it
// only compiles in a translation unit that has already pulled the whole sokol
// stack in -- which nothing under tests/ does or should. The pin's ink is
// arithmetic with a measured right answer, and arithmetic should not be
// checkable only by taking a screenshot.

#include <bitset>

#include <afterhours/src/plugins/ui/rounded_corners.h>

#include "../ui/theme.h"

namespace ecs {

namespace tab_colors {
// Two levels, not three. The review that produced the old ladder was right
// that a ~15L active/inactive delta is near-invisible, but it solved that by
// tinting three planes at different values, and the reference does the
// opposite: everything is one background and the ACTIVE tab is the only thing
// that is a different colour at all.
//
//              dark          hex        role
//   strip_bg   window_bg     #171723    the one background
//   inactive   window_bg     #171723    the same — hairline + text only
//   active     selected_bg   #2E3A58    the only filled surface in the strip
//
// The ACTIVE tab fills with selected_bg (#2E3A58 on the reference) — the SAME
// fill the selected sidebar row uses, which is what makes "this is the current
// thing" read the same in both places. The strip and the INACTIVE tabs are both
// the window colour, so an inactive tab is invisible except for its hairline
// and its text: on the reference there is no recessed well behind it. On hover
// an inactive tab gets a faint additive wash over its own fill (theme::over,
// gap #13 — a subtle tint, never a solid block).
inline afterhours::Color strip_bg() { return theme::window_bg(); }
inline afterhours::Color tab_active() { return theme::selected_bg(); }
inline afterhours::Color tab_inactive() { return theme::window_bg(); }
inline afterhours::Color tab_hover() {
    return theme::over(theme::hover_bg(), theme::window_bg());
}
inline afterhours::Color tab_text() { return theme::text_secondary(); }
// The pin's ink, before it is composited over whichever tab carries it.
//
// 70% alpha, which is Puffin's `.opacity(0.7)` on `pin.fill`, over
// text_secondary standing in for its `mutedText`. hanabi's greys are neutral
// where Puffin's carry a violet cast -- (142,142,154) against (140,140,166) --
// so the composite lands within tolerance on red and green and nine short on
// blue. Nine is inside the comparison's twelve; closing it would mean an
// eleventh palette token, and the same nine-unit shortfall shows up on every
// muted mark in the app, so it is a palette observation and not a pin one.
inline afterhours::Color pin_ink() {
    afterhours::Color c = theme::text_secondary();
    c.a = 179;  // 0.7 * 255
    return c;
}
// The reference's active-tab title is pure white on the selected fill, a step
// brighter than text_primary; light mode keeps its own ink.
inline afterhours::Color tab_text_act() {
    return theme::g_mode == theme::Mode::Dark
               ? afterhours::Color{255, 255, 255, 255}
               : theme::text_primary();
}
inline afterhours::Color close_hover() {
    return theme::over(theme::hover_bg(), theme::panel_bg());
}
inline afterhours::Color border() { return theme::border(); }
inline afterhours::Color tab_outline() { return theme::divider(); }
inline afterhours::Color accent() { return theme::accent(); }
inline constexpr float kTabCorner = 4.0f;
inline constexpr float kTabBorderPx = 1.0f;
// afterhours rasterizes a w*h box at (x,y) as (w+1)*(h+1) pixels anchored at
// (x-1,y-1), so a tab whose OUTER edge must land on the reference's measured
// 220x34 at (284,32) is asked for as 219x33 at (285,33) (gap #80).
inline constexpr float kRasterGrow = 1.0f;
// The `+` sits half a pixel high against the reference, and it STAYS there.
//
// Measured on `ref/01_home.png`: the reference's plus runs y43..55 with a
// three-row crossbar on y48..50; hanabi's runs y42..55 with a two-row crossbar
// on y48..49. A half-pixel bias reproduces the reference's geometry exactly --
// same span, same three rows -- and it scores WORSE, 75 diff px against 84.
//
// Not a paradox, and worth the paragraph because the shape of it recurs
// everywhere in this strip. hanabi draws this mark at (141,141,153) and the
// reference draws it at (148,148,172): nineteen units apart on blue, where the
// comparison's tolerance is twelve. So every pixel of this glyph is already a
// difference on colour alone, and the two rows the bias ADDS are two more of
// them. Recoloured to the reference's own ink and re-scored, the order flips
// and the bias wins, 54 px against 56. The geometry fix is right and it is
// blocked on the palette, not on itself -- hanabi's greys are neutral and
// Puffin's carry a violet cast, which is a whole-palette observation and is
// written up in FRICTION_LOG.md under this branch.
//
// Left at zero anyway, because the geometry claim is weaker than it looks:
// hanabi's plus is Lucide's and the reference's is SF Symbols', and there is no
// obligation on one icon set to sit where the other one does. The ink is a
// real defect; half a pixel between two different glyphs is not.
inline constexpr float kPlusYBias = 0.0f;
// Gap #81. The corner bits are named for the OPPOSITE corner: the enum is
// TOP_LEFT=0..BOTTOM_RIGHT=3 and the sokol backend reads the same bitset as
// 3=TL 2=TR 1=BL 0=BR. Naming the bottom two is what rounds the top two on
// screen. Do not "fix" this to read top_left/top_right; it renders inverted.
inline std::bitset<4> tab_corners_top_round_bottom_square() {
    using afterhours::ui::imm::CornerState;
    return afterhours::ui::imm::RoundedCorners()
        .all_sharp()
        .bottom_left(CornerState::ROUND)
        .bottom_right(CornerState::ROUND)
        .get();
}
}  // namespace tab_colors

}  // namespace ecs
