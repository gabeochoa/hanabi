// How far an image is reduced before it reaches the GPU, and whether the
// filter that does it is the one the GPU was already going to use.
//
// The safety argument for decode-to-fit (src/ui/decode_to_fit.h) is not "a
// downscale looks close enough". It is that afterhours box-filters every
// uploaded image into a mip chain and the GPU samples a reduced level when it
// draws minified, so the pixels on screen were never coming from the base
// level. Halving with the SAME filter before the upload therefore drops levels
// that were never sampled and changes nothing that is drawn.
//
// That argument holds only while two things are true, and both are asserted
// here: the halve is bit-exact 2x2 box with build_mip_chain's rounding and
// odd-dimension clamp, and the policy never reduces past the size the app
// draws at.

#include <cstdio>
#include <utility>
#include <vector>

#include "../../src/util/downscale.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::downscale::halve;
using hanabi::downscale::halvings_for;
using hanabi::downscale::kMaxTextureDim;

// --- the policy never reduces past the size the app draws at ---------------
static void test_the_retained_level_still_covers_the_draw_box() {
    std::printf("test_the_retained_level_still_covers_the_draw_box\n");

    // For every shape, the level kept must still be at least kMaxTextureDim on
    // its long side -- the sampler minifies it, never magnifies it. This is
    // the property, checked over a spread rather than a favourite case.
    for (const auto& [w, h] : std::vector<std::pair<int, int>>{
             {3024, 1964}, {2560, 1600}, {1920, 1080}, {5000, 5000},
             {8000, 200},  {1289, 40},   {644, 420},   {64, 64}}) {
        const int n = halvings_for(w, h);
        int rw = w;
        int rh = h;
        for (int i = 0; i < n; ++i) {
            rw = rw > 1 ? rw / 2 : 1;
            rh = rh > 1 ? rh / 2 : 1;
        }
        const int longSide = rw > rh ? rw : rh;
        const int naturalLong = w > h ? w : h;
        // Either the image was already small enough to leave alone, or what is
        // left still covers the biggest box the app will draw it in.
        CHECK(naturalLong < kMaxTextureDim || longSide >= kMaxTextureDim);
        // And it is never larger than twice that -- one more halve would
        // always have been possible if it were.
        CHECK(naturalLong < kMaxTextureDim || longSide < 2 * kMaxTextureDim);
    }
}

static void test_an_image_that_is_already_small_is_untouched() {
    std::printf("test_an_image_that_is_already_small_is_untouched\n");

    // The 640x480 ladder fixture, a 22px composer chip's source, and the
    // exact threshold. None of these is reduced: the app is already drawing
    // them at or below their natural size.
    CHECK(halvings_for(640, 480) == 0);
    CHECK(halvings_for(64, 64) == 0);
    CHECK(halvings_for(kMaxTextureDim, 900) == 0);
    CHECK(halvings_for(2 * kMaxTextureDim - 1, 10) == 0);
    // One pixel over, and it halves.
    CHECK(halvings_for(2 * kMaxTextureDim, 10) == 1);

    // A Retina screen grab halves exactly once: 3024x1964 -> 1512x982, which
    // still covers 1288 and would not if halved again.
    CHECK(halvings_for(3024, 1964) == 1);
    CHECK(halvings_for(1512, 982) == 0);

    CHECK(halvings_for(0, 100) == 0);
    CHECK(halvings_for(-4, 4) == 0);
}

// --- the filter is 2x2 box, with build_mip_chain's rounding and clamp -------
static void test_the_halve_is_an_exact_two_by_two_box() {
    std::printf("test_the_halve_is_an_exact_two_by_two_box\n");

    // A 2x2 image of four known values. Every output channel must be the mean
    // of the four inputs, rounded half-up -- (sum + 2) / 4, not sum / 4, which
    // would bias every downscaled image dark by up to 0.75 of a level.
    const unsigned char src[16] = {
        10, 20, 30, 255, 12, 22, 32, 255,
        14, 24, 34, 255, 16, 26, 36, 255,
    };
    int ow = 0;
    int oh = 0;
    const std::vector<unsigned char> out = halve(src, 2, 2, ow, oh);
    CHECK(ow == 1 && oh == 1);
    CHECK(out.size() == 4u);
    CHECK(out[0] == 13u);  // (10+12+14+16 + 2) / 4 = 13
    CHECK(out[1] == 23u);
    CHECK(out[2] == 33u);
    CHECK(out[3] == 255u);

    // Rounding: four values summing to 2 must give 1, not 0.
    const unsigned char two[16] = {1, 0, 0, 0, 1, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<unsigned char> r = halve(two, 2, 2, ow, oh);
    CHECK(r[0] == 1u);
}

static void test_an_odd_dimension_reuses_its_last_row() {
    std::printf("test_an_odd_dimension_reuses_its_last_row\n");

    // 3x1 -> 1x1. build_mip_chain clamps both source columns of the 2x2 tap
    // into range, so the result is the mean of columns 0 and 1 counted twice
    // -- NOT the mean of all three. Matching that exactly is the whole point:
    // a different answer here would put the reduced texture's mip chain out of
    // step with the full one's and the "same bytes are sampled" argument fails.
    const unsigned char src[12] = {0, 0, 0, 255, 100, 0, 0, 255, 200, 0, 0, 255};
    int ow = 0;
    int oh = 0;
    const std::vector<unsigned char> out = halve(src, 3, 1, ow, oh);
    CHECK(ow == 1 && oh == 1);
    CHECK(out[0] == 50u);  // (0 + 100 + 0 + 100 + 2) / 4

    // A strip whose short axis is already 1 keeps halving the long one, which
    // is the case a `while (w > 1 && h > 1)` loop drops on the floor.
    const std::vector<unsigned char> strip(4096u * 4u, 128u);
    int sw = 0;
    int sh = 0;
    const std::vector<unsigned char> s2 = halve(strip.data(), 4096, 1, sw, sh);
    CHECK(sw == 2048 && sh == 1);
    CHECK(s2.size() == 2048u * 4u);
    CHECK(s2[0] == 128u);
}

// --- what the reduction is worth -------------------------------------------
static void test_halving_a_retina_grab_is_a_four_fold_saving() {
    std::printf("test_halving_a_retina_grab_is_a_four_fold_saving\n");

    // Each halve is a quarter of the pixels, so one halve is 4x. The whole
    // point of the change, expressed as the arithmetic rather than as a
    // measurement, so it cannot quietly stop being true.
    const int n = halvings_for(3024, 1964);
    CHECK(n == 1);
    int rw = 3024;
    int rh = 1964;
    for (int i = 0; i < n; ++i) {
        rw /= 2;
        rh /= 2;
    }
    const long full = 3024L * 1964L;
    const long kept = static_cast<long>(rw) * rh;
    CHECK(kept * 4 <= full);
    CHECK(kept * 5 > full);
}

int main() {
    std::printf("=== test_downscale ===\n");
    test_the_retained_level_still_covers_the_draw_box();
    test_an_image_that_is_already_small_is_untouched();
    test_the_halve_is_an_exact_two_by_two_box();
    test_an_odd_dimension_reuses_its_last_row();
    test_halving_a_retina_grab_is_a_four_fold_saving();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
