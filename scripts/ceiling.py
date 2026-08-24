#!/usr/bin/env python3
"""What is a rectangle worth, and how much of that can anyone actually get?

Two numbers exist for any part of a frame and neither is the region score:

  CEILING -- overwrite hanabi's pixels with the REFERENCE's over that rectangle
    and re-score. Nothing hanabi can do to that element beats copying the
    reference, so the drop is a true upper bound. This is `feat/vis-list2`'s
    paste test, which the friction log calls the tool to reach for first, and
    which three separate agents have now retyped as a one-off.

  FLOOR -- what an IDENTICAL design scores over that same rectangle, from
    rasterization phase alone. `compare.py --floor` prints this for the seven
    named regions; it is exactly as computable for any rectangle you name, and
    that is the half nobody had. A ceiling on its own says "this element is
    worth 0.73 points" and cannot tell you that 0.73 is what a perfect copy of
    it scores anyway.

Together they bracket the work: CEILING is the most a fix can win, FLOOR is the
least a correct one still costs, and an element AT its floor is finished no
matter what its ceiling says. The sidebar's whole glyph column was declared
done on those two numbers on `feat/vis-sb3` -- 1.95% against a floor of
3.22-5.38 -- after four rounds of people measuring only its ceiling.

  usage: ceiling.py <ref.png> <shot.png> [--region NAME=x0,y0,x1,y1]...
                    [--score-region NAME] [--no-exclusions]

`--region` names a rectangle to price. `--score-region` says which of
compare.py's own regions the prices are quoted in (default: the smallest named
region that contains each rectangle). With no `--region` at all it prices a
default partition of the sidebar.

DECLARED SURFACE IS EXCLUDED, the same way `compare.py` excludes it, and
`--no-exclusions` reproduces what this script printed before 2026-08-24.
It did not, until a footer rectangle read "+5.00 reachable" over the version
label -- a rectangle every pixel of which compare.py masks out of the score,
because v0.5.5 is not a number hanabi can become. A tool that ranks work by
reachable size cannot rank an unreachable rectangle top. Both halves of every
figure are masked: the numerator, the denominator, and the FLOOR, which is the
half that is easy to forget -- a floor measured over surface the score does not
look at is a floor for nobody.
"""
import importlib.util
import os
import sys

from PIL import Image, ImageChops, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))


def load_compare():
    """compare.py's own diff_mask/blur/floor, so this cannot drift from it.

    Imported by exec of everything above `def main()` rather than as a module:
    compare.py is a script with argv-driven behaviour at import time, and the
    parts worth reusing are all above it.
    """
    src = open(os.path.join(HERE, "compare.py")).read().split("def main()")[0]
    g = {}
    exec(compile(src, "compare.py", "exec"), g)
    return g


DEFAULT_REGIONS = {
    # The sidebar, partitioned the way four rounds of work found it splits.
    "views/icons": (4, 62, 32, 265),
    "views/labels": (32, 62, 250, 265),
    "views/badges": (250, 62, 279, 265),
    "search": (0, 265, 283, 313),
    "list/marks": (0, 313, 25, 911),
    "list/titles": (25, 313, 236, 911),
    "list/counts": (236, 313, 283, 911),
    # The footer, partitioned the way `feat/vis-footer` found IT splits. The
    # first row is not a mistake: `compare.py` cuts the footer region at
    # H*0.96 = y911 and the footer's own rule is at y921, so ten rows of the
    # session list's last visible row are scored as footer. That band was 39%
    # of the region and it is the LIST's -- price it separately or the footer
    # reads as the worst region in the app for someone else's text.
    "foot/listbleed": (0, 911, 283, 922),
    "foot/version": (0, 922, 60, 949),
    "foot/count": (60, 922, 195, 949),
    "foot/btn-new": (198, 922, 222, 949),
    "foot/btn-palette": (222, 922, 246, 949),
    "foot/btn-gear": (246, 922, 270, 949),
}


def excluded_mask(g, refp, ref):
    """compare.py's own declared rectangles, as one mask over this frame.

    Reused rather than re-listed for the reason `load_compare` exists: two
    copies of this table would disagree within a week, and the disagreement
    would be silent -- a rectangle here and not there reads as a design win.
    """
    entries = g["divergences_for"](refp, ref.size)
    if not entries:
        return None, []
    return g["rect_mask"](ref.size, entries), entries


def rate(mask, excl, box):
    """Diff pixels and rate over a rectangle, with declared surface out of BOTH.

    Out of both halves, on the same arithmetic as `compare.py`'s region table:
    dropping the excluded pixels from the numerator alone leaves the
    denominator claiming credit for surface nobody is looking at, which
    flatters every figure by the size of the exclusion.
    """
    sub = mask.crop(box)
    if excl is None:
        area = max(1, sub.width * sub.height)
        return sub.histogram()[255], 100.0 * sub.histogram()[255] / area, area
    e = excl.crop(box)
    kept = ImageChops.subtract(sub, e)
    area = max(1, sub.width * sub.height - e.histogram()[255])
    n = kept.histogram()[255]
    return n, 100.0 * n / area, area


def floors_masked(g, ref, regions, excl):
    """`floor_by_region`, over the surface the score actually looks at.

    The floor has to be masked for the same reason the score is. `main` is 85%
    declared and the footer 10%, so an unmasked floor over either is mostly the
    rasterization cost of pixels no design change is being asked to move --
    and, worse, it is subtracted from a MASKED score, so the two sides of
    "headroom" are measured over different surfaces.
    """
    per = {name: [] for name in regions}
    base = ref.filter(ImageFilter.GaussianBlur(g["STRUCT_BLUR"]))
    for dx, dy in g["FLOOR_OFFSETS"]:
        moved = ref.transform(ref.size, Image.AFFINE, (1, 0, dx, 0, 1, dy),
                              resample=Image.BICUBIC)
        m = g["diff_mask"](base,
                           moved.filter(ImageFilter.GaussianBlur(g["STRUCT_BLUR"])))
        for name, box in regions.items():
            per[name].append(rate(m, excl, box)[1])
    return {n: (min(v), max(v)) for n, v in per.items()}


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    refp, shotp = sys.argv[1], sys.argv[2]
    regions = {}
    args = sys.argv[3:]
    for i, a in enumerate(args):
        if a == "--region":
            name, box = args[i + 1].split("=")
            regions[name] = tuple(int(v) for v in box.split(","))
    if not regions:
        regions = dict(DEFAULT_REGIONS)

    g = load_compare()
    blur = g["STRUCT_BLUR"]
    ref = Image.open(refp).convert("RGB")
    shot = Image.open(shotp).convert("RGB")

    excl, entries = (None, [])
    if "--no-exclusions" not in sys.argv:
        excl, entries = excluded_mask(g, refp, ref)

    floors = floors_masked(g, ref, regions, excl)
    rb = ref.filter(ImageFilter.GaussianBlur(blur))
    base = g["diff_mask"](rb, shot.filter(ImageFilter.GaussianBlur(blur)))

    if excl is None:
        print("  (no declared divergences applied)")
    print(f"  {'rectangle':<18}{'now':>8}{'ceiling':>10}{'floor':>16}   verdict")
    for name, box in regions.items():
        n, now, area = rate(base, excl, box)

        # The ceiling over this rectangle alone: the reference's own pixels in,
        # and re-scored over the SAME rectangle, so the number is comparable
        # with `now` and with the floor rather than with a whole region.
        pasted = shot.copy()
        pasted.paste(ref.crop(box), (box[0], box[1]))
        pm = g["diff_mask"](rb, pasted.filter(ImageFilter.GaussianBlur(blur)))
        ceil = rate(pm, excl, box)[1]

        lo, hi = floors[name]
        if now <= hi:
            verdict = "AT FLOOR -- done"
        elif hi >= now - 0.01:
            verdict = "at floor"
        else:
            # What is left after the floor takes its cut. The ceiling is the
            # bound on a paste; this is the bound on a DESIGN change.
            verdict = f"+{now - hi:.2f} reachable"
        # A rectangle that is entirely declared prices as zero of everything,
        # and saying so beats printing three zeroes that read like a win.
        full = (box[2] - box[0]) * (box[3] - box[1])
        if excl is not None and area <= 1 and full > 1:
            verdict = "ALL DECLARED -- unspendable"
        print(f"  {name:<18}{now:7.2f}%{ceil:9.2f}%  {lo:6.2f}-{hi:6.2f}"
              f"   {verdict}")
    print()
    print("  now     = this shot over that rectangle")
    print("  ceiling = the same rectangle with the reference's pixels pasted in")
    print("  floor   = an identical design at a different rasterization phase")
    if entries:
        print(f"  all three exclude {len(entries)} declared rectangles "
              f"(--no-exclusions to price them in)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
