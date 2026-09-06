#!/usr/bin/env python3
"""scripts/focus_edge_gate.py — a focused field's edge is a whole rectangle.

WHY. At pin 9ff9079 the accent border `text_input` sets on focus rendered with
its TOP ROW missing: on 18d_palette_empty_dark, y=160 carried 8 accent pixels
where 506 belong, while the bottom row and both verticals were intact. Nothing
hanabi drew at the field's own top edge survived -- a foreground stroke
included -- so it is a clip, not an overpaint (afterhours_gaps.md #375).

The screenshot suite did NOT catch that as a defect: it reported a diff, and a
diff is equally consistent with an intended change, so it was re-baselined once
as "the border moved inward 2px". A gate has to say WHICH property broke.

WHAT IT CHECKS, per screen, on the focused field's rect:

  1. every one of the four edges is continuous -- at least 90% of the pixels
     along it carry the accent, so a missing or stubbed edge fails by name;
  2. the accent reads at least 3:1 against the surface just outside it, in
     dark AND light, so an edge that survives as a near-invisible hairline
     fails too.

It reads the committed baselines, so it runs in a second and needs no capture.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "docs" / "screenshots" / "baselines"

# screen -> the focused field's rect (x0, y0, x1, y1), inclusive.
SCREENS = {
    "18b_palette_dark": (294, 160, 805, 197),
    "18c_palette_light": (294, 160, 805, 197),
    "18d_palette_empty_dark": (294, 160, 805, 197),
    "18e_palette_empty_light": (294, 160, 805, 197),
    "18f_search_dark": (294, 160, 805, 197),
    "18g_search_light": (294, 160, 805, 197),
    "18h_search_empty_dark": (294, 160, 805, 197),
    "18i_search_empty_light": (294, 160, 805, 197),
}
MIN_EDGE_COVERAGE = 0.90
MIN_CONTRAST = 3.0
# text_input's own focused border: ctx.theme.accent, 2px. The edge must be
# THIS colour and no other, and it must be one band -- an app that draws the
# edge on a wrapper without clearing the field's own copy gets two concentric
# rings in two different blues, which is a defect that "some blue is present"
# cannot see. Measured: the first wrapper fix produced (90,128,255) against
# (0,122,204) in dark and (46,90,236) against (0,122,204) in light.
ACCENT = {"dark": (0, 122, 204), "light": (0, 122, 204)}
MAX_BAND_PX = 2


def relative_luminance(rgb):
    def channel(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

    r, g, b = (channel(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = relative_luminance(a), relative_luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def is_accent(px):
    r, g, b = px[:3]
    return b > 140 and b - r > 60 and g > 80


def band_of(pixels):
    """(longest accent run, colours seen) crossing the edge."""
    run, best, seen = 0, 0, []
    for px in pixels:
        if is_accent(px):
            run += 1
            best = max(best, run)
            if px not in seen:
                seen.append(px)
        else:
            run = 0
    return best, seen


def probe_inward(p, label, box, depth=8):
    """Pixels crossing one edge, from just OUTSIDE it inward.

    Starting inside the rect would miss a second ring drawn one pixel outside
    -- which is exactly the shape a wrapper border plus the field's own
    uncleared border makes, and exactly what this gate exists to catch. The
    walk starts 2px out and the run is counted from the first accent pixel.
    """
    x0, y0, x1, y1 = box
    mx, my = (x0 + x1) // 2, (y0 + y1) // 2
    if label == "top":
        return [p[mx, y0 - 2 + i] for i in range(depth)]
    if label == "bottom":
        return [p[mx, y1 + 2 - i] for i in range(depth)]
    if label == "left":
        return [p[x0 - 2 + i, my] for i in range(depth)]
    return [p[x1 + 2 - i, my] for i in range(depth)]


def check_screen(name, box):
    from PIL import Image

    path = BASE / f"{name}.png"
    if not path.is_file():
        return [f"{name}: baseline missing"]
    p = Image.open(path).convert("RGB").load()
    x0, y0, x1, y1 = box
    problems = []

    def scan(label, groups, outside):
        hit = [g for g in groups if any(is_accent(p[x, y]) for x, y in g)]
        cov = len(hit) / max(1, len(groups))
        if cov < MIN_EDGE_COVERAGE:
            problems.append(
                f"{name}: {label} edge is {cov:.0%} covered "
                f"(need {MIN_EDGE_COVERAGE:.0%}) — broken or stubbed"
            )
            return
        sample = next(
            (p[x, y] for g in hit for x, y in g if is_accent(p[x, y])), None
        )
        ratio = contrast(sample, p[outside[0], outside[1]])
        if ratio < MIN_CONTRAST:
            problems.append(
                f"{name}: {label} edge reads {ratio:.2f}:1 against its "
                f"surround (need {MIN_CONTRAST}:1)"
            )
        want = ACCENT["light" if name.endswith("_light") else "dark"]
        if sample is not None and tuple(sample) != want:
            problems.append(
                f"{name}: {label} edge is rgb{tuple(sample)}, not the "
                f"library's own focus accent rgb{want}"
            )
        # Walk inward from the edge: one band, no second ring behind it.
        probe = probe_inward(p, label, box)
        run, seen = band_of(probe)
        if run > MAX_BAND_PX:
            problems.append(
                f"{name}: {label} edge is {run}px of accent (max "
                f"{MAX_BAND_PX}) — a second border is drawn behind the first"
            )
        if len(seen) > 1:
            problems.append(
                f"{name}: {label} edge carries {len(seen)} accent colours "
                f"{[tuple(c) for c in seen]} — two borders, two tokens"
            )

    cols = range(x0 + 8, x1 - 7)  # skip the rounded corners
    rows = range(y0 + 8, y1 - 7)
    scan("top", [[(x, y) for y in range(y0, y0 + 4)] for x in cols],
         (x0 + 40, y0 - 6))
    scan("bottom", [[(x, y) for y in range(y1 - 3, y1 + 1)] for x in cols],
         (x0 + 40, y1 + 6))
    scan("left", [[(x, y) for x in range(x0, x0 + 4)] for y in rows],
         (x0 - 6, y0 + 18))
    scan("right", [[(x, y) for x in range(x1 - 3, x1 + 1)] for y in rows],
         (x1 + 6, y0 + 18))
    return problems


def main() -> int:
    problems = []
    for name, box in SCREENS.items():
        problems.extend(check_screen(name, box))
    print("=== hanabi focus-edge gate ===")
    print(f"  {len(SCREENS)} focused fields, four edges each, dark and light")
    if problems:
        print("  FAIL")
        for problem in problems:
            print(f"    {problem}")
        return 1
    print(f"  PASS (every edge >= {MIN_EDGE_COVERAGE:.0%} covered and "
          f">= {MIN_CONTRAST}:1)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
