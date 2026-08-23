#!/usr/bin/env python3
"""Score how far hanabi is from Puffin, and say WHERE.

One number is not enough to work from: "8% different" does not tell you
whether the sidebar is the wrong width or the font is the wrong size. So this
reports the overall figure plus a per-region breakdown, and writes a diff
image with the changed pixels in red.

The figure is the fraction of pixels whose colour differs by more than a small
per-channel tolerance. Antialiasing noise between two different renderers is
real and uninteresting; a tolerance of 12/255 ignores it and still catches a
wrong colour, a wrong position or a wrong glyph.

  usage: compare.py <puffin.png> <hanabi.png> [--diff out.png] [--regions]
"""
import sys
from PIL import Image, ImageChops, ImageFilter

TOL = 12

# Puffin renders at 2x on a retina screen and is downsampled to meet hanabi's
# 1x capture. That asymmetry has a floor, and it is not small: downsampling ONE
# Puffin frame with two different filters and comparing the results against
# each other gives 2.27% overall and 10% in the text-dense session list. No
# design change can get under that on the raw metric, because it is resampling,
# not disagreement.
#
# So there are two numbers. RAW is the literal per-pixel answer. STRUCTURAL
# blurs both by 0.8px first, which forgives sub-pixel glyph edges while still
# catching a wrong colour, a wrong position or a wrong size -- on that same
# identical-source pair it reads 0.23%, so it has room to mean something.
# Quote both; a claim of parity needs the structural number to be small AND
# the raw number to have stopped falling.
STRUCT_BLUR = 0.8


def load(path):
    return Image.open(path).convert("RGB")


def diff_mask(a, b, tol=TOL):
    """Per-pixel: True where the two images differ beyond tolerance."""
    d = ImageChops.difference(a, b)
    # Max of the three channel deltas, thresholded.
    return d.convert("L").point(lambda v: 255 if v > tol else 0)


def pct(mask):
    hist = mask.histogram()
    changed = hist[255]
    return 100.0 * changed / (mask.width * mask.height)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 2
    ap, bp = args[0], args[1]
    a, b = load(ap), load(bp)

    if a.size != b.size:
        print(f"SIZE MISMATCH  puffin={a.size}  hanabi={b.size}")
        print("  (resizing hanabi to compare anyway -- fix the window size)")
        b = b.resize(a.size)

    mask = diff_mask(a, b)
    overall = pct(mask)
    sa = a.filter(ImageFilter.GaussianBlur(STRUCT_BLUR))
    sb = b.filter(ImageFilter.GaussianBlur(STRUCT_BLUR))
    struct = pct(diff_mask(sa, sb))
    print(f"RAW         {overall:.2f}%   (floor ~2.3% -- retina downsample)")
    print(f"STRUCTURAL  {struct:.2f}%   (floor ~0.2% -- this is the one to drive)")

    if "--regions" in sys.argv:
        # Regions are fractions of the frame, not hardcoded pixel boxes, so
        # they still mean something when the window size changes.
        W, H = a.size
        regions = {
            "sidebar":      (0, 0, int(W * 0.24), H),
            "  views":      (0, 0, int(W * 0.24), int(H * 0.28)),
            "  search":     (0, int(H * 0.28), int(W * 0.24), int(H * 0.33)),
            "  list":       (0, int(H * 0.33), int(W * 0.24), int(H * 0.96)),
            "  footer":     (0, int(H * 0.96), int(W * 0.24), H),
            "tabbar":       (int(W * 0.24), 0, W, int(H * 0.075)),
            "main":         (int(W * 0.24), int(H * 0.075), W, H),
        }
        print()
        for name, box in regions.items():
            sub = mask.crop(box)
            hist = sub.histogram()
            p = 100.0 * hist[255] / max(1, sub.width * sub.height)
            bar = "#" * int(p / 2)
            print(f"  {name:<12} {p:6.2f}%  {bar}")

    if "--diff" in sys.argv:
        out = sys.argv[sys.argv.index("--diff") + 1]
        red = Image.new("RGB", a.size, (255, 0, 0))
        composed = b.copy()
        composed.paste(red, (0, 0), mask)
        composed.save(out)
        print(f"\nwrote {out}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
