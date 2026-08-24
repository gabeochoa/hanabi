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
    test_the_rule_survives_a_light_theme();
    if (g_failures != 0) {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
