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
                    [--score-region NAME]

`--region` names a rectangle to price. `--score-region` says which of
compare.py's own regions the prices are quoted in (default: the smallest named
region that contains each rectangle). With no `--region` at all it prices a
default partition of the sidebar.
"""
import importlib.util
import os
import sys

from PIL import Image, ImageFilter

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
}


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

    floors = g["floor_by_region"](ref, regions)
    rb = ref.filter(ImageFilter.GaussianBlur(blur))
    base = g["diff_mask"](rb, shot.filter(ImageFilter.GaussianBlur(blur)))

    print(f"  {'rectangle':<16}{'now':>8}{'ceiling':>10}{'floor':>16}   verdict")
    for name, box in regions.items():
        sub = base.crop(box)
        area = max(1, sub.width * sub.height)
        now = 100.0 * sub.histogram()[255] / area

        # The ceiling over this rectangle alone: the reference's own pixels in,
        # and re-scored over the SAME rectangle, so the number is comparable
        # with `now` and with the floor rather than with a whole region.
        pasted = shot.copy()
        pasted.paste(ref.crop(box), (box[0], box[1]))
        pm = g["diff_mask"](rb, pasted.filter(ImageFilter.GaussianBlur(blur)))
        ceil = 100.0 * pm.crop(box).histogram()[255] / area

        lo, hi = floors[name]
        if now <= hi:
            verdict = "AT FLOOR -- done"
        elif hi >= now - 0.01:
            verdict = "at floor"
        else:
            # What is left after the floor takes its cut. The ceiling is the
            # bound on a paste; this is the bound on a DESIGN change.
            verdict = f"+{now - hi:.2f} reachable"
        print(f"  {name:<16}{now:7.2f}%{ceil:9.2f}%  {lo:6.2f}-{hi:6.2f}   {verdict}")
    print()
    print("  now     = this shot over that rectangle")
    print("  ceiling = the same rectangle with the reference's pixels pasted in")
    print("  floor   = an identical design at a different rasterization phase")
    return 0


if __name__ == "__main__":
    sys.exit(main())
