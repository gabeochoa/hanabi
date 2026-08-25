#pragma once

// ---------------------------------------------------------------------------
// The PURE half of decode-to-fit (src/ui/decode_to_fit.h): how far to reduce
// an image before it is uploaded, and the halve that does it. No afterhours,
// no sokol, no GPU -- so tests/unit/test_downscale.cpp can assert the policy
// and the filter without linking a graphics stack.
//
// Read decode_to_fit.h first for WHY any of this happens; this file is the
// arithmetic that file's argument rests on.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hanabi::downscale {

// The largest DEVICE-pixel extent an inline image is ever drawn at.
//
// Derived, not guessed: render_bubble caps the reading column at 644 points
// (main_pane_system.h, kBubbleCap) and the transcript caps an inline image at
// 420 points tall, so 644 is the long side of the biggest box. The app runs
// and captures at up to ui_scale 2 (scripts/shoot_hanabi.sh HANABI_SHOOT_2X),
// which doubles it.
//
// afterhours clamps ui_scale to [0.5, 3.0], so a caller CAN ask for 3 -- a
// test hook does. Sizing for 3.0 would mean 1932, and a 3024-wide grab would
// then not be halved at all: the entire saving given up to protect a scale
// the shipped app never selects. What 3.0 costs instead is measured in
// decode_to_fit.h and it is nothing, because the sampler still never reaches
// the level that was dropped.
inline constexpr int kMaxTextureDim = 644 * 2;

// One 2x2 box halve of a tightly-packed RGBA8 buffer.
//
// THIS IS build_mip_chain's INNER LOOP, transcribed, including the clamp that
// makes an odd dimension reuse its last row or column and the (sum + 2) / 4
// rounding. That is the point of the whole design and not a coincidence: if
// this filter matches the one the vendored backend applies at upload, then
// every mip level of the reduced texture is bit-identical to the
// corresponding level of the full one, and an image drawn from the reduced
// texture samples exactly the bytes it sampled before. Verified against
// build_mip_chain over four shapes -- 3024x1964, 640x480, 1023x777 and the
// degenerate 2000x3 -- all 39 levels identical, zero mismatches.
inline std::vector<unsigned char> halve(const unsigned char* src, int w, int h,
                                        int& outW, int& outH) {
    const int nw = w > 1 ? w / 2 : 1;
    const int nh = h > 1 ? h / 2 : 1;
    std::vector<unsigned char> out(static_cast<std::size_t>(nw) * nh * 4);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            const int x0 = std::min(x * 2, w - 1);
            const int x1 = std::min(x * 2 + 1, w - 1);
            const int y0 = std::min(y * 2, h - 1);
            const int y1 = std::min(y * 2 + 1, h - 1);
            for (int c = 0; c < 4; ++c) {
                const int sum =
                    src[(static_cast<std::size_t>(y0) * w + x0) * 4 + c] +
                    src[(static_cast<std::size_t>(y0) * w + x1) * 4 + c] +
                    src[(static_cast<std::size_t>(y1) * w + x0) * 4 + c] +
                    src[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                out[(static_cast<std::size_t>(y) * nw + x) * 4 + c] =
                    static_cast<unsigned char>((sum + 2) / 4);
            }
        }
    }
    outW = nw;
    outH = nh;
    return out;
}

// How many times a w x h image is halved before it is uploaded: halve while
// the RESULT still covers maxDim on its long side, so the level kept is the
// smallest one the sampler still has to MINIFY. Stopping one level earlier
// would mean magnifying a downscaled image, which is the one thing this must
// never do.
inline int halvings_for(int w, int h, int maxDim = kMaxTextureDim) {
    if (w <= 0 || h <= 0 || maxDim <= 0) return 0;
    int n = 0;
    int cw = w;
    int ch = h;
    while (std::max(cw, ch) / 2 >= maxDim && (cw > 1 || ch > 1)) {
        cw = cw > 1 ? cw / 2 : 1;
        ch = ch > 1 ? ch / 2 : 1;
        ++n;
    }
    return n;
}

}  // namespace hanabi::downscale
