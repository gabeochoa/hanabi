#!/usr/bin/env python3
"""Read exact geometry and colour off a Puffin screenshot.

Guessing at a design from a picture is how you end up 6px out everywhere. This
prints the numbers instead: where the vertical edges are, where the horizontal
rules are, the row pitch in the session list, and the colour of every surface
worth naming.

  usage: probe.py <shot.png>
"""
import sys
from collections import Counter
from PIL import Image


def col_runs(im, x, y0, y1):
    """Colours down a vertical line, collapsed into runs."""
    px = im.load()
    runs = []
    prev, start = None, y0
    for y in range(y0, y1):
        c = px[x, y]
        if c != prev:
            if prev is not None:
                runs.append((start, y - 1, prev))
            prev, start = c, y
    runs.append((start, y1 - 1, prev))
    return runs


def main():
    im = Image.open(sys.argv[1]).convert("RGB")
    W, H = im.size
    px = im.load()
    print(f"# {sys.argv[1]}  {W}x{H}")

    # --- vertical edges: scan a row well below the chrome for colour changes
    print("\n## vertical edges (row y=500)")
    prev = None
    for x in range(W):
        c = px[x, 500]
        if prev is not None and max(abs(a - b) for a, b in zip(c, prev)) > 10:
            print(f"  x={x:<5} {prev} -> {c}")
        prev = c

    # --- horizontal edges down the sidebar and down the main pane
    for label, x in (("sidebar", 140), ("main", 700)):
        print(f"\n## horizontal edges down {label} (x={x})")
        prev = None
        for y in range(H):
            c = px[x, y]
            if prev is not None and max(abs(a - b) for a, b in zip(c, prev)) > 10:
                print(f"  y={y:<5} {prev} -> {c}")
            prev = c

    # --- the surfaces themselves, by most common colour in a box
    print("\n## surface colours (most common in box)")
    boxes = {
        "sidebar bg":     (10, 400, 270, 900),
        "sidebar header": (10, 40, 250, 60),
        "selected row":   (10, 75, 270, 92),
        "main bg":        (400, 300, 1100, 800),
        "tabbar bg":      (300, 5, 1150, 30),
        "active tab":     (530, 38, 700, 60),
        "inactive tab":   (300, 38, 480, 60),
        "window edge":    (0, 0, 4, 949),
    }
    for name, (x0, y0, x1, y1) in boxes.items():
        c = Counter(px[x, y] for x in range(x0, x1) for y in range(y0, y1))
        top = c.most_common(2)
        print(f"  {name:<16} {top[0][0]}  ({100*top[0][1]//((x1-x0)*(y1-y0))}%)")


if __name__ == "__main__":
    main()
