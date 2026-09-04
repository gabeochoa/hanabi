#!/usr/bin/env python3
"""Contrast of the ask card's action labels, measured off the committed baselines.

This validates the BASELINES, which validate-screenshots then holds the build
to at 0.0000%: a source change that moves a label lands here one step later,
when its baseline is re-captured.

A token-only check certifies arithmetic, not the screen. The disabled fill goes
through the engine's disabled compositing on its way to the framebuffer, which
moved it 19 levels darker than the token said -- so the tokens computed 4.86:1
while the label shipped at 3.82:1, below the bar, with the gate green. This
reads the baselines instead: whatever the render does, this sees it.
"""

import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BASELINES = ROOT / "docs" / "screenshots" / "baselines"
MIN_RATIO = 4.5

# Button interiors, inset from the border so the sample is fill and glyph only.
SAMPLES = [
    ("59_ask_approval_dark.png", (334, 626, 418, 644),
     "dark Approve enabled", False),
    ("59_ask_approval_dark.png", (444, 626, 516, 644),
     "dark Deny enabled", False),
    ("57_ask_card_narrow_dark.png", (258, 483, 348, 505),
     "narrow Submit disabled", True),
    ("57_ask_card_narrow_dark.png", (360, 483, 452, 505),
     "narrow Decline enabled", False),
    ("65_ask_two_questions_narrow_dark.png", (258, 483, 330, 507),
     "two-ask Submit disabled", True),
    ("65_ask_two_questions_narrow_dark.png", (410, 483, 470, 507),
     "two-ask Decline enabled", False),
    ("66_ask_two_questions_tiny_dark.png", (200, 483, 250, 507),
     "tiny Send disabled", True),
    ("66_ask_two_questions_tiny_dark.png", (292, 483, 330, 507),
     "tiny Skip enabled", False),
    ("61_ask_long_approval_dark.png", (334, 626, 418, 644),
     "long-approval Approve enabled", False),
    ("56_ask_card_light.png", (334, 626, 418, 644), "light Submit disabled", True),
    ("56_ask_card_light.png", (444, 626, 516, 644), "light Decline enabled", False),
    ("55_ask_card_dark.png", (334, 626, 418, 644), "dark Submit disabled", True),
    ("55_ask_card_dark.png", (444, 626, 516, 644), "dark Decline enabled", False),
    ("63_ask_unanswerable_backend_dark.png", (334, 626, 418, 644),
     "dark Approve disabled", True),
    ("63_ask_unanswerable_backend_dark.png", (444, 626, 516, 644),
     "dark Deny disabled", True),
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


def measure(name, box):
    path = BASELINES / name
    if not path.exists():
        raise SystemExit(f"ask-contrast: missing baseline {name}")
    image = Image.open(path).convert("RGB")
    pixels = [image.getpixel((x, y))
              for x in range(box[0], box[2])
              for y in range(box[1], box[3])]
    fill = Counter(pixels).most_common(1)[0][0]
    ink = max(pixels, key=lambda p: abs(luminance(p) - luminance(fill)))
    return fill, ink, contrast(ink, fill)


def main():
    failures = []
    seen = {}
    for name, box, label, disabled in SAMPLES:
        fill, ink, ratio = measure(name, box)
        print(f"  {label:<26} fill={fill} ink={ink} -> {ratio:.2f}:1")
        if ratio < MIN_RATIO:
            failures.append(f"{label} is {ratio:.2f}:1, below {MIN_RATIO}:1")
        theme = label.split()[0]
        seen.setdefault(theme, {})[disabled] = (label, ratio)

    # A disabled control that out-shouts its enabled neighbour is the wrong
    # signal even when both clear the bar.
    for theme, rows in seen.items():
        if True in rows and False in rows:
            dis_label, dis = rows[True]
            en_label, en = rows[False]
            if dis >= en:
                failures.append(
                    f"{dis_label} ({dis:.2f}) is not dimmer than "
                    f"{en_label} ({en:.2f})")

    if failures:
        print("\nask-contrast: FAIL", file=sys.stderr)
        for text in failures:
            print(f"  {text}", file=sys.stderr)
        raise SystemExit(1)
    print(f"ask-contrast: PASS ({len(SAMPLES)} labels, all >= {MIN_RATIO}:1, "
          "measured off the shipped baselines)")


if __name__ == "__main__":
    main()
