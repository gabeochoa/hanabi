#!/usr/bin/env python3
"""Downsample a supersampled capture to the reference's size, with LANCZOS.

  usage: downsample.py <in.png> <out.png> <width> <height>

The filter is not a detail. `ref/*.png` are Puffin captured at 2x on a retina
panel and reduced to 1180x949 with macOS `sips`, whose default reduction is a
windowed-sinc resample; LANCZOS is the closest thing Pillow offers and is what
`compare.py`'s own header discusses when it says the choice of filter is worth
2.27% on the raw metric all by itself. BILINEAR and BOX both soften differently
from `sips`, and a capture reduced with either is being scored against a
reference reduced with neither.

RGB, not RGBA: the reference frames are RGB and `compare.py` converts anyway,
and reducing with an alpha channel present lets fully-transparent pixels drag
their (undefined) colour into their neighbours.
"""
import sys

from PIL import Image


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    src, dst, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    im = Image.open(src).convert("RGB")
    if im.width % w or im.height % h:
        # Not fatal -- LANCZOS handles any ratio -- but an integer ratio is the
        # only one that reproduces what the reference went through, and a
        # non-integer one is almost always a window-size typo.
        print(f"WARNING: {im.size} is not an integer multiple of ({w}, {h})",
              file=sys.stderr)
    im.resize((w, h), Image.LANCZOS).save(dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
