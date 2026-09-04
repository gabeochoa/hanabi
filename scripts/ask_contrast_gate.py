#!/usr/bin/env python3
"""Contrast of the ask card's action labels, measured off the committed baselines.

This validates the BASELINES, which validate-screenshots then holds the build
to at 0.0000%: a source change that moves a label lands here one step later,
when its baseline is re-captured.

Boxes are DERIVED, not written down. Hand-placed rectangles drifted onto
neighbouring buttons, and because the ratio is taken from the extreme pixel in
the box, a spill into a brighter neighbour reads BETTER than the label is --
the direction that hides a regression. Each label's glyph run is found by
scanning the action row, so a box cannot name one button and sample another.
"""

import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BASELINES = ROOT / "docs" / "screenshots" / "baselines"
MIN_RATIO = 4.5

# One row per baseline: the action row's vertical band, and the labels it
# carries left to right with whether each is disabled. Disabled-vs-enabled is
# compared WITHIN a row -- same card, same theme, same state -- so a disabled
# label can never be checked against an unrelated control's enabled one.
ROWS = [
    ("55_ask_card_dark.png", 620, 650, 320,
     [("Submit", True), ("Decline", False)]),
    ("56_ask_card_light.png", 620, 650, 320,
     [("Submit", True), ("Decline", False)]),
    ("59_ask_approval_dark.png", 620, 650, 320,
     [("Approve", False), ("Deny", False)]),
    ("61_ask_long_approval_dark.png", 620, 650, 320,
     [("Approve", False), ("Deny", False)]),
    ("63_ask_unanswerable_backend_dark.png", 620, 650, 320,
     [("Approve", True), ("Deny", True)]),
    ("57_ask_card_narrow_dark.png", 480, 510, 250,
     [("Submit", True), ("Decline", False)]),
    ("65_ask_two_questions_narrow_dark.png", 480, 510, 250,
     [("Submit", True), ("Next", False), ("Decline", False)]),
    ("66_ask_two_questions_tiny_dark.png", 480, 510, 190,
     [("Send", True), ("Next", False), ("Skip", False)]),
]


def luminance(color):
    def channel(value):
        v = value / 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

    return (0.2126 * channel(color[0]) + 0.7152 * channel(color[1]) +
            0.0722 * channel(color[2]))


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def button_rects(image, y0, y1, x0, card_fill):
    """The action row's buttons: columns differing from the card's own fill."""
    width, height = image.size
    y1 = min(y1, height)
    hit = []
    for x in range(x0, width):
        column = [image.getpixel((x, y)) for y in range(y0, y1)]
        hit.append(any(
            sum(abs(p[c] - card_fill[c]) for c in range(3)) > 18
            for p in column))

    runs = []
    start = None
    for offset, inked in enumerate(hit):
        x = x0 + offset
        if inked and start is None:
            start = x
        if not inked and start is not None:
            runs.append([start, x])
            start = None
    if start is not None:
        runs.append([start, width])

    merged = []
    for run in runs:
        if merged and run[0] - merged[-1][1] < 6:
            merged[-1][1] = run[1]
        else:
            merged.append(run)
    return [tuple(r) for r in merged if r[1] - r[0] >= 28]


def measure(image, box, y0, y1):
    pixels = [image.getpixel((x, y))
              for x in range(box[0] + 2, box[1] - 2)
              for y in range(y0 + 2, min(y1, image.size[1]) - 2)]
    fill = Counter(pixels).most_common(1)[0][0]
    ink = max(pixels, key=lambda p: abs(luminance(p) - luminance(fill)))
    return fill, ink, contrast(ink, fill)


def main():
    failures = []
    total = 0
    for name, y0, y1, x0, labels in ROWS:
        path = BASELINES / name
        if not path.exists():
            raise SystemExit(f"ask-contrast: missing baseline {name}")
        image = Image.open(path).convert("RGB")
        card_fill = Counter(
            image.getpixel((x, y0 + 1)) for x in range(x0, image.size[0])
        ).most_common(1)[0][0]
        runs = button_rects(image, y0, y1, x0, card_fill)[:len(labels)]
        if len(runs) != len(labels):
            failures.append(
                f"{name}: found {len(runs)} action buttons, expected "
                f"{len(labels)} ({', '.join(l for l, _ in labels)})")
            continue

        measured = []
        for (label, disabled), box in zip(labels, runs):
            fill, ink, ratio = measure(image, box, y0, y1)
            total += 1
            print(f"  {name.split('_')[0]:>3} {label:<9} "
                  f"x={box[0]}..{box[1]} fill={fill} ink={ink} -> {ratio:.2f}:1")
            if ratio < MIN_RATIO:
                failures.append(
                    f"{name} {label} is {ratio:.2f}:1, below {MIN_RATIO}:1")
            measured.append((label, disabled, ratio))

        dim = [m for m in measured if m[1]]
        lit = [m for m in measured if not m[1]]
        for label, _, ratio in dim:
            for other, _, other_ratio in lit:
                if ratio >= other_ratio:
                    failures.append(
                        f"{name}: disabled {label} ({ratio:.2f}) is not dimmer "
                        f"than enabled {other} ({other_ratio:.2f})")

    if failures:
        print("\nask-contrast: FAIL", file=sys.stderr)
        for text in failures:
            print(f"  {text}", file=sys.stderr)
        raise SystemExit(1)
    print(f"ask-contrast: PASS ({total} labels, all >= {MIN_RATIO}:1, boxes "
          "derived from each button rect)")


if __name__ == "__main__":
    main()
