#!/usr/bin/env python3
"""Find every text run that is the wrong size, position or colour — all at once.

WHY THIS EXISTS. Twice in one sitting a label turned out to be two font sizes
too small and half the contrast it should be — the search field's placeholder
and the VIEWS strip's own heading, in the same file, a day apart. Neither was
found by looking. "A bit small and a bit grey" is not a thing the eye reports,
and neither is visible in a region total: a heading is a few hundred pixels in
a region of thirteen thousand, so being 40% wrong about it moves the score by
less than the noise between two captures.

What DOES find them is measuring, and the measurement is always the same three
numbers: how wide the ink is, how tall it is, and how bright it is. So this
does that for every run of ink in both frames and prints the pairs that
disagree, instead of waiting for someone to trip over them one at a time.

HOW IT SEGMENTS. Ink is "differs from the local background", and the local
background is the most common colour in the row's own strip — which is what
makes this work across a selected row's fill, a section header's tint and the
window colour without being told where they are. Rows of ink are grouped into
bands with a blank-row gap, and each band is split into runs by a horizontal
gap. A run is, in practice, a word or a glyph.

READING THE OUTPUT. Runs are paired left to right within a band. A pair is
flagged when its widths differ by more than `--tol` px, its heights by more
than 1, or its peak brightness by more than `--ctol`. Peak brightness is a
crude colour proxy and deliberately so: this is a SIEVE, not a measurement.
Its job is to hand you a short list of places to go and measure properly.

  usage: inkdiff.py <ref.png> <hanabi.png> [--band y0:y1] [--x x0:x1]
                    [--tol 2] [--ctol 24] [--all]
"""
import sys
from collections import Counter

from PIL import Image

INK_TOL = 14      # per-channel delta from the local background that counts as ink
BAND_GAP = 2      # blank rows that end a band
RUN_GAP = 3       # blank columns that end a run within a band


def local_bg(im, y0, y1, x0, x1):
    """The most common colour in this strip — its background, whatever it is."""
    c = Counter()
    for y in range(y0, y1):
        for x in range(x0, x1):
            c[im.getpixel((x, y))] += 1
    return c.most_common(1)[0][0]


def is_ink(px, bg):
    return max(abs(px[i] - bg[i]) for i in range(3)) > INK_TOL


def bands(im, y0, y1, x0, x1):
    """Contiguous runs of rows that contain any ink, with their own bg."""
    out = []
    start = None
    blank = 0
    for y in range(y0, y1):
        bg = local_bg(im, max(y0, y - 8), min(y1, y + 8), x0, x1)
        row = [x for x in range(x0, x1) if is_ink(im.getpixel((x, y)), bg)]
        if row:
            if start is None:
                start = y
            blank = 0
        elif start is not None:
            blank += 1
            if blank > BAND_GAP:
                out.append((start, y - blank))
                start = None
    if start is not None:
        out.append((start, y1 - 1))
    return out


def runs_in(im, band, x0, x1):
    """Split a band's ink into horizontal runs: words and glyphs."""
    y0, y1 = band
    bg = local_bg(im, y0, y1 + 1, x0, x1)
    cols = sorted({x for y in range(y0, y1 + 1) for x in range(x0, x1)
                   if is_ink(im.getpixel((x, y)), bg)})
    if not cols:
        return []
    out = []
    st = prev = cols[0]
    for x in cols[1:]:
        if x > prev + RUN_GAP:
            out.append((st, prev))
            st = x
        prev = x
    out.append((st, prev))

    detailed = []
    for (a, b) in out:
        pts = [(x, y, im.getpixel((x, y)))
               for y in range(y0, y1 + 1) for x in range(a, b + 1)
               if is_ink(im.getpixel((x, y)), bg)]
        ys = [p[1] for p in pts]
        peak = max(pts, key=lambda p: sum(p[2]))[2]
        detailed.append({
            "x0": a, "x1": b, "w": b - a + 1,
            "y0": min(ys), "y1": max(ys), "h": max(ys) - min(ys) + 1,
            "ink": len(pts), "peak": peak,
        })
    return detailed


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 2
    a, b = Image.open(args[0]).convert("RGB"), Image.open(args[1]).convert("RGB")
    if a.size != b.size:
        print(f"SIZE MISMATCH {a.size} vs {b.size}")
        return 2
    W, H = a.size

    def opt(name, default):
        if name in sys.argv:
            return sys.argv[sys.argv.index(name) + 1]
        return default

    y0, y1 = (int(v) for v in opt("--band", f"0:{H}").split(":"))
    x0, x1 = (int(v) for v in opt("--x", f"0:{W}").split(":"))
    tol = int(opt("--tol", "2"))
    ctol = int(opt("--ctol", "24"))
    show_all = "--all" in sys.argv

    ba, bb = bands(a, y0, y1, x0, x1), bands(b, y0, y1, x0, x1)
    print(f"ref: {len(ba)} bands   hanabi: {len(bb)} bands   "
          f"(y {y0}..{y1}, x {x0}..{x1})")
    if len(ba) != len(bb):
        print("  BAND COUNT DIFFERS -- one frame draws something the other "
              "does not, or a row is split. Pairing by index anyway.")
    print()

    flagged = 0
    for i, (band_a, band_b) in enumerate(zip(ba, bb)):
        ra, rb = runs_in(a, band_a, x0, x1), runs_in(b, band_b, x0, x1)
        head = (f"band {i:2d}  ref y{band_a[0]}..{band_a[1]}  "
                f"hb y{band_b[0]}..{band_b[1]}  "
                f"runs {len(ra)} vs {len(rb)}")
        rows = []
        for j, (u, v) in enumerate(zip(ra, rb)):
            dw = v["w"] - u["w"]
            dh = v["h"] - u["h"]
            dx = v["x0"] - u["x0"]
            dpk = sum(v["peak"]) // 3 - sum(u["peak"]) // 3
            bad = abs(dw) > tol or abs(dh) > 1 or abs(dpk) > ctol or abs(dx) > tol
            if bad or show_all:
                rows.append(
                    f"    run {j:2d} {'!!' if bad else '  '} "
                    f"x {u['x0']:4d}->{v['x0']:4d} ({dx:+3d})  "
                    f"w {u['w']:3d}->{v['w']:3d} ({dw:+3d})  "
                    f"h {u['h']:3d}->{v['h']:3d} ({dh:+2d})  "
                    f"ink {u['ink']:4d}->{v['ink']:4d}  "
                    f"peak {sum(u['peak'])//3:3d}->{sum(v['peak'])//3:3d} "
                    f"({dpk:+4d})")
                if bad:
                    flagged += 1
        if len(ra) != len(rb):
            rows.append(f"    RUN COUNT DIFFERS: {len(ra)} vs {len(rb)}")
        if rows:
            print(head)
            print("\n".join(rows))
    print(f"\n{flagged} flagged run(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
