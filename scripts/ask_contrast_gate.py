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
MAX_DISABLED_RATIO = 6.0

# One row per baseline: the action row's vertical band, and the labels it
# carries left to right with whether each is disabled. Disabled-vs-enabled is
# compared WITHIN a row -- same card, same theme, same state -- so a disabled
# label can never be checked against an unrelated control's enabled one.
ROWS = [
    ("55_ask_card_dark.png", 599, 629, 320, 1040,
     [("Submit", True), ("Decline", False)]),
    ("56_ask_card_light.png", 599, 629, 320, 1040,
     [("Submit", True), ("Decline", False)]),
    ("59_ask_approval_dark.png", 631, 661, 320, 1040,
     [("Approve", False), ("Deny", False)]),
    ("61_ask_long_approval_dark.png", 631, 661, 320, 1040,
     [("Approve", False), ("Deny", False)]),
    ("63_ask_unanswerable_backend_dark.png", 631, 661, 320, 1040,
     [("Approve", True), ("Deny", True)]),
    ("57_ask_card_narrow_dark.png", 459, 489, 250, 740,
     [("Submit", True), ("Decline", False)]),
    ("65_ask_two_questions_narrow_dark.png", 492, 522, 250, 470,
     [("Submit", True), ("Next", False), ("Decline", False)]),
    ("66_ask_two_questions_tiny_dark.png", 491, 521, 190, 325,
     [("Send", True), ("Next", False), ("Skip", False)]),
]


# Rows whose own card cannot show both states get their pair from the baseline
# that shows the same control in the other state: an all-disabled row still has
# to be dimmer than the same button enabled elsewhere.
PAIRED = [
    ("63_ask_unanswerable_backend_dark.png", "59_ask_approval_dark.png"),
    ("63_ask_unanswerable_backend_dark.png", "61_ask_long_approval_dark.png"),
]


def luminance(color):
    def channel(value):
        v = value / 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

    return (0.2126 * channel(color[0]) + 0.7152 * channel(color[1]) +
            0.0722 * channel(color[2]))


def stands_out(fill, card):
    return sum(abs(fill[c] - card[c]) for c in range(3))


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


def button_extent(image, box, seed_y, card_fill):
    def differs(y):
        return any(
            sum(abs(image.getpixel((x, y))[c] - card_fill[c])
                for c in range(3)) > 18
            for x in range(box[0] + 2, box[1] - 2))

    top = seed_y
    while top > 0 and differs(top - 1):
        top -= 1
    bottom = seed_y
    while bottom + 1 < image.size[1] and differs(bottom + 1):
        bottom += 1
    return top, bottom + 1


def measure(image, box, y0, y1):
    pixels = [image.getpixel((x, y))
              for x in range(box[0] + 2, box[1] - 2)
              for y in range(y0 + 2, min(y1, image.size[1]) - 2)]
    fill = Counter(pixels).most_common(1)[0][0]
    ink = max(pixels, key=lambda p: abs(luminance(p) - luminance(fill)))
    return fill, ink, contrast(ink, fill)


def self_check():
    """The ceiling must reject a disabled label repainted bright."""
    row = "63_ask_unanswerable_backend_dark.png"
    y0, y1, x0, card_right = 620, 650, 320, 1040
    image = Image.open(BASELINES / row).convert("RGB").copy()
    card_fill = Counter(
        image.getpixel((x, y0 + 1)) for x in range(x0, image.size[0])
    ).most_common(1)[0][0]
    boxes = [b for b in button_rects(image, y0, y1, x0, card_fill)
             if b[1] <= card_right]
    if not boxes:
        raise SystemExit("ask-contrast self-check: no button to tamper")
    painted = 0
    for x in range(boxes[0][0] + 2, boxes[0][1] - 2):
        for y in range(y0 + 2, y1 - 2):
            pixel = image.getpixel((x, y))
            if sum(abs(pixel[c] - card_fill[c]) for c in range(3)) > 90:
                image.putpixel((x, y), (255, 255, 255))
                painted += 1
    if painted == 0:
        raise SystemExit("ask-contrast self-check: found no label to tamper")
    _, _, ratio = measure(image, boxes[0], y0, y1)
    if ratio <= MAX_DISABLED_RATIO:
        raise SystemExit(
            f"ask-contrast self-check: a white disabled label measured "
            f"{ratio:.2f}:1, which the {MAX_DISABLED_RATIO}:1 ceiling would "
            "accept")


def main():
    failures = []
    total = 0
    by_control = {}
    fills = {}
    inks = {}
    cards = {}
    for name, y0, y1, x0, card_right, labels in ROWS:
        path = BASELINES / name
        if not path.exists():
            raise SystemExit(f"ask-contrast: missing baseline {name}")
        image = Image.open(path).convert("RGB")
        card_fill = Counter(
            image.getpixel((x, y0 + 1)) for x in range(x0, image.size[0])
        ).most_common(1)[0][0]
        detected = button_rects(image, y0, y1, x0, card_fill)
        cards[name] = card_fill
        runs = [r for r in detected if r[1] <= card_right]
        if len(runs) != len(labels):
            failures.append(
                f"{name}: found {len(runs)} action buttons inside the card "
                f"(of {len(detected)} runs), expected {len(labels)} "
                f"({', '.join(l for l, _ in labels)})")
            continue

        measured = []
        for (label, disabled), box in zip(labels, runs):
            top, bottom = button_extent(image, box, y0 + 4, card_fill)
            fill, ink, ratio = measure(image, box, top, bottom)
            total += 1
            print(f"  {name.split('_')[0]:>3} {label:<9} "
                  f"x={box[0]}..{box[1]} fill={fill} ink={ink} -> {ratio:.2f}:1")
            if ratio < MIN_RATIO:
                failures.append(
                    f"{name} {label} is {ratio:.2f}:1, below {MIN_RATIO}:1")
            if disabled and ratio > MAX_DISABLED_RATIO:
                failures.append(
                    f"{name} disabled {label} is {ratio:.2f}:1, above "
                    f"{MAX_DISABLED_RATIO}:1 — it reads as enabled")
            measured.append((label, disabled, ratio))
            by_control[(name, label)] = ratio
            fills[(name, label)] = fill
            inks[(name, label)] = ink

        dim = [m for m in measured if m[1]]
        lit = [m for m in measured if not m[1]]

        for label, _, ratio in dim:
            for other, _, other_ratio in lit:
                if ratio >= other_ratio:
                    failures.append(
                        f"{name}: disabled {label} ({ratio:.2f}) is not dimmer "
                        f"than enabled {other} ({other_ratio:.2f})")

    for disabled_row, enabled_row in PAIRED:
        dim = {label for (row, label) in by_control if row == disabled_row}
        lit = {label for (row, label) in by_control if row == enabled_row}
        shared = sorted(dim & lit)
        if not shared:
            failures.append(
                f"{disabled_row}: no control in common with {enabled_row}")
            continue
        for label in shared:
            off_fill = fills[(disabled_row, label)]
            on_fill = fills[(enabled_row, label)]
            if stands_out(on_fill, cards[enabled_row]) > 24:
                if stands_out(off_fill, cards[disabled_row]) > 24:
                    failures.append(
                        f"{label} disabled on {disabled_row} keeps the filled "
                        f"look it has enabled on {enabled_row}")
            elif luminance(inks[(disabled_row, label)]) >= luminance(
                    inks[(enabled_row, label)]):
                failures.append(
                    f"{label} disabled on {disabled_row} is not dimmer than "
                    f"enabled on {enabled_row}")

    if failures:
        print("\nask-contrast: FAIL", file=sys.stderr)
        for text in failures:
            print(f"  {text}", file=sys.stderr)
        raise SystemExit(1)
    print(f"ask-contrast: PASS ({total} labels, all >= {MIN_RATIO}:1, boxes "
          "derived from each button rect)")


if __name__ == "__main__":
    self_check()
    main()
