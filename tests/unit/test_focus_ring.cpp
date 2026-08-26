// What colour hanabi hands afterhours for the focus ring (src/ui/focus_visible.h).
//
// This is a screenshot's finding turned into arithmetic, the same way
// test_tab_colors is. rendering.h's focus_ring_for draws THREE rounded
// outlines for any ring at all — a contrast edge one pixel outside the stack,
// another one pixel inside it, and the coloured ring between them — and only
// the coloured one is gated on focus_ring_thickness. The two edges take their
// colour from the ring:
//
//     contrast = luminance(ring) > 0.5 ? black@180 : white@180
//
// so a saturated blue on a dark app (WCAG luminance 0.248) got WHITE edges,
// and the "1px hairline" measured as a three-pixel white-blue-white band with
// the white brighter than the blue. Nothing in the theme turns the edges off,
// and no scripted assertion can see a colour (afterhours_gaps.md #61), so the
// shipped guard is this file.
//
// The rule: the ring must land on the same side of afterhours' own threshold
// as the BACKDROP, so the edges it forces resolve toward the backdrop and sink
// into it instead of outshouting the ring. Asserted against afterhours'
// luminance function itself rather than a copy of the formula, because the
// question is not "what do we think this colour is" but "which branch will the
// renderer take".
#include <cstdio>

#include <afterhours/src/plugins/color.h>

#include "../../src/ui/focus_visible.h"
#include "../../src/ui/theme.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

namespace fv = hanabi::ui::focus_visible;
using afterhours::Color;

// The branch rendering.h takes, spelled exactly as rendering.h spells it.
static bool contrast_is_white(Color ring) {
    return !(afterhours::colors::luminance(ring) > 0.5f);
}

// What the app actually hands the renderer, assembled the way
// FocusVisibleSystem assembles it.
static Color ring_as_shipped() {
    const bool dark_backdrop =
        afterhours::colors::luminance(theme::window_bg()) <
        fv::kContrastThreshold;
    return fv::ring_color(theme::focus_ring(), dark_backdrop);
}

// A near-white line at 70% opacity over a near-black app is the whole
// complaint, and "near-white" is not the property — CONTRAST is. On a light
// theme a near-white edge is invisible and a near-black one shouts, so the
// rule cannot be about brightness in either direction. It is: an edge that
// stands out from the backdrop MORE than the ring does is not an edge around
// the ring, it is the ring. Measured with afterhours' own contrast ratio, the
// one WCAG defines, against the composited edge rather than the 70% source.
static void check_the_edge_does_not_outshout_the_ring(const char* what) {
    const Color ring = ring_as_shipped();
    const Color edge = contrast_is_white(ring) ? Color{255, 255, 255, 180}
                                               : Color{0, 0, 0, 180};
    const Color bg = theme::window_bg();
    const Color composited = theme::over(edge, bg);
    const float edge_ratio = afterhours::colors::contrast_ratio(composited, bg);
    const float ring_ratio = afterhours::colors::contrast_ratio(ring, bg);
    const bool ok = edge_ratio < ring_ratio;
    if (!ok)
        std::printf(
            "  ring {%d,%d,%d} lum %.3f -> %s edge {%d,%d,%d}: edge contrasts "
            "%.2f:1 with the backdrop where the ring contrasts %.2f:1 (%s)\n",
            ring.r, ring.g, ring.b,
            static_cast<double>(afterhours::colors::luminance(ring)),
            contrast_is_white(ring) ? "WHITE" : "black", composited.r,
            composited.g, composited.b, static_cast<double>(edge_ratio),
            static_cast<double>(ring_ratio), what);
    CHECK(ok);
}

static void test_the_dark_theme_does_not_get_white_edges() {
    std::printf("test_the_dark_theme_does_not_get_white_edges\n");
    theme::set_accent_choice("default");
    theme::set_mode(theme::Mode::Dark);
    check_the_edge_does_not_outshout_the_ring("dark, default accent");
}

// Light mode was already correct and must stay that way: a dark ring on a
// light app is the same rule with the other answer, and a fix aimed at the
// dark theme that flips this one has traded a bug for a bug.
static void test_the_light_theme_keeps_its_own_answer() {
    std::printf("test_the_light_theme_keeps_its_own_answer\n");
    theme::set_accent_choice("default");
    theme::set_mode(theme::Mode::Light);
    check_the_edge_does_not_outshout_the_ring("light, default accent");
    theme::set_mode(theme::Mode::Dark);
}

// theme::apply_custom replaces the ring's RGB wholesale from the chosen accent
// swatch, so every swatch is a fresh chance to land on the wrong side of the
// threshold. Green in dark mode is the near miss that makes this worth a loop:
// it reads 0.457 untouched, four hundredths under.
static void test_every_accent_swatch_lands_on_the_right_side() {
    std::printf("test_every_accent_swatch_lands_on_the_right_side\n");
    for (const theme::Swatch& s : theme::kAccentSwatches) {
        theme::set_accent_choice(s.key);
        theme::set_mode(theme::Mode::Dark);
        check_the_edge_does_not_outshout_the_ring(s.key);
        theme::set_mode(theme::Mode::Light);
        check_the_edge_does_not_outshout_the_ring(s.key);
    }
    theme::set_accent_choice("default");
    theme::set_mode(theme::Mode::Dark);
}

// The ring still has to be a ring. Sinking the edges by walking the colour to
// the backdrop's side would satisfy every assertion above and leave nothing
// visible, so: whatever comes out must read against the surface it is drawn
// on, and must still be recognisably the accent's hue rather than grey.
static void test_the_ring_is_still_visible_and_still_the_accent() {
    std::printf("test_the_ring_is_still_visible_and_still_the_accent\n");
    theme::set_accent_choice("default");
    for (theme::Mode m : {theme::Mode::Dark, theme::Mode::Light}) {
        theme::set_mode(m);
        const Color ring = ring_as_shipped();
        const float lum_ring = afterhours::colors::luminance(ring);
        const float lum_row =
            afterhours::colors::luminance(theme::selected_bg());
        CHECK(std::abs(lum_ring - lum_row) > 0.15f);
        const Color base = theme::focus_ring();
        const int spread_base =
            std::max({base.r, base.g, base.b}) - std::min({base.r, base.g, base.b});
        const int spread_ring =
            std::max({ring.r, ring.g, ring.b}) - std::min({ring.r, ring.g, ring.b});
        CHECK(spread_ring * 3 >= spread_base);
    }
    theme::set_mode(theme::Mode::Dark);
}

int main() {
    std::printf("== focus ring ==\n");
    test_the_dark_theme_does_not_get_white_edges();
    test_the_light_theme_keeps_its_own_answer();
    test_every_accent_swatch_lands_on_the_right_side();
    test_the_ring_is_still_visible_and_still_the_accent();
    if (g_failures != 0) {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
