// The sidebar footer's geometry (src/ecs/sidebar_footer_geometry.h).
//
// One property, and it is the one no other harness in this repo can see:
// **the activity light's origin is on a whole pixel, on both axes.**
//
// afterhours never rounds a position. Grid snapping is off in hanabi
// (`preload.cpp`) and only ever snapped SIZES anyway, so a fractional origin
// goes straight to a rasterizer with no antialiasing (afterhours_gaps.md #92)
// and the shape loses a row. Measured on the shipped build before this was
// fixed: a 6x6 light at y=932.5 rendered rows 933..937 -- 4/6/6/6/4 pixels,
// six wide and FIVE tall. A circle that is not round, in a band where nothing
// else is fractional, and neither the parity metric (0.0018 frame points) nor
// the eye at 1x reports it. Gap #130.
//
// Not a `.e2e` file, for a reason worth stating rather than assuming:
// `assert_ui` reads x/y/w/h/hidden/text (gap #86) and ROUNDS them, so a widget
// asked for 932.5 reads back 933 and one asked for 932.4 reads back 932. The
// assertion can pin WHERE the light is and can never pin that it is on a whole
// pixel, which is the half that decides whether it is round. The scripted
// suite still holds the placement (`composer_reaches_the_window_floor.e2e`);
// this holds the property. Same split, and the same reason, as
// `test_tab_colors.cpp`.
#include <cmath>
#include <cstdio>

#include "../../src/ecs/sidebar_footer_geometry.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

namespace fs = ecs::footer_status;

static bool whole(float v) { return v == std::floor(v); }

int main() {
    std::printf("footer geometry\n");

    // 1. The reference-sized frame, which is what every parity capture uses:
    //    a 949px window, a 28px footer whose rule is y921, so the content band
    //    is top=922 and 27 tall. The band's own centre is 935 and the count's
    //    ink centre is 934, so the light floors onto the text.
    CHECK(fs::dot_y(922.0f, 27.0f) == 932.0f);

    // 2. The property, over every band height the footer could be given. The
    //    odd ones are the ones that bite -- (27 - 6) / 2 is the half pixel
    //    that cost the shipped light a row -- so sweeping only the even ones
    //    would pass against the unfixed code.
    for (int h = 12; h <= 48; ++h) {
        const float y = fs::dot_y(922.0f, static_cast<float>(h));
        CHECK(whole(y));
        // Snapped, not moved: never more than a pixel off the true centre.
        const float want = 922.0f + (static_cast<float>(h) - fs::kDot) * 0.5f;
        CHECK(std::fabs(y - want) < 1.0f);
    }

    // 3. The x axis is the one that is easy to forget, and it is worse than a
    //    constant: it is derived from `text_px` of "<N> sessions", so its
    //    fractional part changes with the catalog. Without the snap the light
    //    loses a COLUMN on some session counts and a ROW on others, which is a
    //    bug that reproduces on a colleague's machine and not on yours.
    for (int i = 0; i < 40; ++i) {
        const float text_left = 140.0f + 0.1f * static_cast<float>(i);
        CHECK(whole(fs::dot_x(text_left)));
    }

    // 4. A label's box sits 5px left of where its ink is wanted, because
    //    afterhours insets text by a hardcoded 5 on every alignment (gap #84).
    //    Puffin's footer is `.padding(.horizontal, 10)`, so the version's ink
    //    belongs at x=10 and its BOX at 5. hanabi shipped the box at 10 and
    //    the ink at 15 -- five pixels right of the reference's, in a rectangle
    //    `compare.py` declares, so no score could ever have caught it.
    CHECK(fs::label_box_x(fs::kFooterPadX) == 5.0f);
    CHECK(fs::label_box_x(fs::kFooterPadX) + fs::kAhTextInset
          == fs::kFooterPadX);

    if (g_failures == 0) {
        std::printf("  all footer geometry checks passed\n");
        return 0;
    }
    std::printf("  %d failure(s)\n", g_failures);
    return 1;
}
