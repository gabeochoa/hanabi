// What colour a pinned tab's pushpin comes out (src/ecs/tab_colors.h).
//
// This is a screenshot's finding turned into arithmetic. The pin used to be
// drawn in the tab's own title colour, which on the active tab is pure white,
// and against ref/01_home.png that mark measured 209 above its background where
// Puffin's measures 71 — every pixel of it a difference on brightness alone.
// The rule it should follow is in Puffin's source (TabStrip.swift:506 gives
// `pin.fill` its own foregroundColor and its own .opacity(0.7), overriding the
// chip's) and the constants it produces are in the frozen reference, and the
// two agree to two units on both tabs at once.
//
// Asserted here rather than on screen because assert_ui reads x/y/w/h/hidden/
// text and never a pixel (afterhours_gaps.md #61), so the shipped guard against
// someone passing the title colour back in is this file.
#include <cstdio>
#include <cstdlib>

#include "../../src/ecs/tab_colors.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using afterhours::Color;

// The comparison harness's own per-channel tolerance (scripts/compare.py, TOL).
// A claim about a colour is worth exactly as much as that harness would let
// through, so it is the same number here.
static const int kTol = 12;

static bool near(Color got, int r, int g, int b, int tol) {
    return std::abs(static_cast<int>(got.r) - r) <= tol &&
           std::abs(static_cast<int>(got.g) - g) <= tol &&
           std::abs(static_cast<int>(got.b) - b) <= tol;
}

// Reports on the way out, because a colour assertion that only says "false" is
// a colour assertion you have to rebuild the binary to read.
static bool within(Color got, int r, int g, int b, int tol = kTol) {
    if (near(got, r, g, b, tol)) return true;
    std::printf("    got (%d,%d,%d) want (%d,%d,%d) +/-%d\n", got.r, got.g,
                got.b, r, g, b, tol);
    return false;
}

// The same check where a MISS is the pass, so the miss must stay quiet.
static bool apart(Color got, int r, int g, int b, int tol = kTol) {
    if (!near(got, r, g, b, tol)) return true;
    std::printf("    got (%d,%d,%d), which should NOT be (%d,%d,%d) +/-%d\n",
                got.r, got.g, got.b, r, g, b, tol);
    return false;
}

// Sampled off docs/visual-parity/ref/01_home.png, over the solid interior of
// each pin rather than at its brightest pixel — the first tab's is at x296..301
// y45..50 and the second's at x520..525, and the outer column of either is a
// third of a covered pixel, which is what makes a peak sample lie.
static const int kRefPinOnInactive[3] = {107, 107, 127};
static const int kRefPinOnActive[3] = {114, 117, 143};

static void test_the_pin_matches_the_reference_on_an_inactive_tab() {
    std::printf("test_the_pin_matches_the_reference_on_an_inactive_tab\n");
    theme::set_mode(theme::Mode::Dark);
    const Color got = theme::over(ecs::tab_colors::pin_ink(),
                                  ecs::tab_colors::tab_inactive());
    CHECK(within(got, kRefPinOnInactive[0], kRefPinOnInactive[1],
                 kRefPinOnInactive[2]));
}

static void test_the_pin_matches_the_reference_on_the_active_tab() {
    std::printf("test_the_pin_matches_the_reference_on_the_active_tab\n");
    theme::set_mode(theme::Mode::Dark);
    const Color got = theme::over(ecs::tab_colors::pin_ink(),
                                  ecs::tab_colors::tab_active());
    CHECK(within(got, kRefPinOnActive[0], kRefPinOnActive[1],
                 kRefPinOnActive[2]));
}

// The two tabs' fills differ, so the composites differ too — but only by the
// backdrop showing through 30% of the mark, never by the mark changing colour.
// This is the regression the reference caught: reading the pin's ink off the
// tab's title colour makes it track selection, and on a dark theme that means a
// white pin on the one tab a reader looks at most.
static void test_the_pin_does_not_follow_the_title_colour() {
    std::printf("test_the_pin_does_not_follow_the_title_colour\n");
    theme::set_mode(theme::Mode::Dark);
    const Color titled = theme::over(ecs::tab_colors::tab_text_act(),
                                     ecs::tab_colors::tab_active());
    CHECK(apart(titled, kRefPinOnActive[0], kRefPinOnActive[1],
                kRefPinOnActive[2]));
    const Color inactive = theme::over(ecs::tab_colors::pin_ink(),
                                       ecs::tab_colors::tab_inactive());
    const Color active = theme::over(ecs::tab_colors::pin_ink(),
                                     ecs::tab_colors::tab_active());
    CHECK(within(inactive, active.r, active.g, active.b, 20));
}

// The close mark, which `ref/02_thread.png` is the only frame that shows.
//
// 01's two tabs are both pinned and Puffin draws no × on a pinned chip, so
// every number about this mark comes off 02: its ink is x483..490 y46..53,
// centred on 486.5, in a chip running x284..504.
static void test_the_close_mark_lands_where_the_reference_puts_it() {
    std::printf("test_the_close_mark_lands_where_the_reference_puts_it\n");
    // `TabChip`'s `.padding(.horizontal, 10)` and its `closeButton`'s
    // `.frame(width: 14, height: 14)`, which `TabTooltip` restates as
    // `horizontalPadding` and `markWidth`.
    CHECK(ecs::tab_colors::kChipPadPx == 10.0f);
    CHECK(ecs::tab_colors::kCloseBoxPx == 14.0f);
    const float chipRight = 504.0f;
    const float boxLeft =
        chipRight - ecs::tab_colors::kChipPadPx - ecs::tab_colors::kCloseBoxPx;
    CHECK(boxLeft == 480.0f);
    const float centre = boxLeft + ecs::tab_colors::kCloseBoxPx * 0.5f;
    CHECK(centre >= 486.0f && centre <= 487.5f);  // measured 486.5
    // The glyph inside it: Lucide's `close` inks its whole blit square, so the
    // blit size IS the mark's extent, and the reference's is 8 across.
    CHECK(ecs::tab_colors::kCloseGlyphPx == 8.0f);
}

// The × takes the strip's muted mark ink, never the current tab's title. The
// regression this guards is the pin's, one file over: on a dark theme the
// active title is pure white, and a close mark that followed it would be a
// white × on the one tab a reader looks at most.
static void test_the_close_mark_does_not_follow_the_title_colour() {
    std::printf("test_the_close_mark_does_not_follow_the_title_colour\n");
    theme::set_mode(theme::Mode::Dark);
    const Color ink = ecs::tab_colors::close_ink();
    const Color titled = ecs::tab_colors::tab_text_act();
    CHECK(apart(ink, titled.r, titled.g, titled.b));
    // And it is Puffin's `Chrome.mutedText` to within the harness's own
    // tolerance -- (140,140,166) against hanabi's neutral (142,142,154), which
    // is nine units of blue and the whole of the palette's violet cast. Swept
    // analytically over this strip's three drawn marks it is worth ZERO diff
    // pixels; see FRICTION_LOG.md, `## The tab strip, round four`.
    CHECK(within(ink, 140, 140, 166));
}

// Light mode has no measured reference — there is no light Puffin capture — so
// the only claim worth making is the structural one: the rule is a rule, not a
// dark-theme special case, and the mark stays translucent either way.
static void test_the_rule_survives_a_light_theme() {
    std::printf("test_the_rule_survives_a_light_theme\n");
    theme::set_mode(theme::Mode::Light);
    const Color ink = ecs::tab_colors::pin_ink();
    CHECK(ink.a > 150 && ink.a < 200);
    const Color got =
        theme::over(ink, ecs::tab_colors::tab_active());
    CHECK(got.a == 255);
    theme::set_mode(theme::Mode::Dark);
}

int main() {
    std::printf("== tab colors ==\n");
    test_the_pin_matches_the_reference_on_an_inactive_tab();
    test_the_pin_matches_the_reference_on_the_active_tab();
    test_the_pin_does_not_follow_the_title_colour();
    test_the_close_mark_lands_where_the_reference_puts_it();
    test_the_close_mark_does_not_follow_the_title_colour();
    test_the_rule_survives_a_light_theme();
    if (g_failures != 0) {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
