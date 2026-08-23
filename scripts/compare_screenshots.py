#!/usr/bin/env python3
"""Compare freshly captured screens against the committed baselines.

    python3 scripts/compare_screenshots.py [--baselines DIR] [--current DIR]

Every PNG in the baseline dir must have a same-named counterpart in the
current dir and differ by no more than its threshold in the manifest
(docs/screenshots/baselines/manifest.json). Exit 0 = all within threshold,
1 = at least one screen out of threshold or missing, 2 = the comparison could
not run at all (no image backend, bad paths).
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

DEFAULT_BASELINES = os.path.join("docs", "screenshots", "baselines")
DEFAULT_CURRENT = os.path.join("output", "screenshots", "current")
MANIFEST_NAME = "manifest.json"

NO_BACKEND_MESSAGE = """\
ERROR: no image comparison backend available.

compare_screenshots.py needs one of:

  * Pillow for the python3 that runs this script
        {py} -m pip install --user Pillow
  * ImageMagick 7 on PATH (the `magick` binary)
        brew install imagemagick

Install either one and re-run. Nothing was compared."""


class Backend:
    name = "none"

    def diff_fraction(self, baseline, current):
        """Fraction (0.0-1.0) of pixels that differ, or a str describing why
        the two images are not comparable at all."""
        raise NotImplementedError


class PillowBackend(Backend):
    name = "Pillow"

    def __init__(self, image_mod, chops_mod):
        self.Image = image_mod
        self.ImageChops = chops_mod

    def diff_fraction(self, baseline, current):
        with self.Image.open(baseline) as a, self.Image.open(current) as b:
            if a.size != b.size:
                return "size {}x{} vs {}x{}".format(*a.size, *b.size)
            diff = self.ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
            bands = diff.split()
            worst = bands[0]
            for band in bands[1:]:
                worst = self.ImageChops.lighter(worst, band)
            histogram = worst.histogram()
            identical = histogram[0]
            total = a.size[0] * a.size[1]
        return (total - identical) / total


class ImageMagickBackend(Backend):
    name = "ImageMagick"

    def __init__(self, magick):
        self.magick = magick

    def _size(self, path):
        out = subprocess.run(
            [self.magick, "identify", "-format", "%w %h", path],
            capture_output=True, text=True, check=True,
        ).stdout.split()
        return int(out[0]), int(out[1])

    def diff_fraction(self, baseline, current):
        a, b = self._size(baseline), self._size(current)
        if a != b:
            return "size {}x{} vs {}x{}".format(*a, *b)
        proc = subprocess.run(
            [self.magick, "compare", "-metric", "AE", baseline, current, "null:"],
            capture_output=True, text=True,
        )
        if proc.returncode > 1:
            return "magick compare failed: {}".format(proc.stderr.strip())
        differing = float(proc.stderr.strip().split()[0])
        return differing / (a[0] * a[1])


def pick_backend():
    try:
        from PIL import Image, ImageChops
    except ImportError:
        pass
    else:
        return PillowBackend(Image, ImageChops)
    magick = shutil.which("magick")
    if magick:
        return ImageMagickBackend(magick)
    return None


def load_manifest(path):
    if not os.path.exists(path):
        return 0.0, {}
    with open(path) as f:
        data = json.load(f)
    return float(data.get("default_threshold_pct", 0.0)), data.get("screens", {})


def screens_in(directory):
    if not os.path.isdir(directory):
        return None
    return sorted(
        f[:-4] for f in os.listdir(directory)
        if f.endswith(".png") and not f.startswith(".")
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baselines", default=DEFAULT_BASELINES)
    parser.add_argument("--current", default=DEFAULT_CURRENT)
    parser.add_argument("--manifest", default=None)
    args = parser.parse_args()

    manifest_path = args.manifest or os.path.join(args.baselines, MANIFEST_NAME)
    default_threshold, per_screen = load_manifest(manifest_path)

    baselines = screens_in(args.baselines)
    if baselines is None:
        print("ERROR: no baseline directory at {}".format(args.baselines), file=sys.stderr)
        return 2
    if not baselines:
        print("ERROR: no baseline PNGs in {} — run 'make update-baselines'".format(
            args.baselines), file=sys.stderr)
        return 2

    current = screens_in(args.current)
    if current is None:
        print("ERROR: no current directory at {} — run 'make validate-screenshots'".format(
            args.current), file=sys.stderr)
        return 2

    backend = pick_backend()
    if backend is None:
        print(NO_BACKEND_MESSAGE.format(py=sys.executable), file=sys.stderr)
        return 2

    print("comparing {} baseline(s) against {} using {}".format(
        len(baselines), args.current, backend.name))

    failures = []
    for name in baselines:
        threshold = float(per_screen.get(name, {}).get("threshold_pct", default_threshold))
        baseline_png = os.path.join(args.baselines, name + ".png")
        current_png = os.path.join(args.current, name + ".png")
        if not os.path.exists(current_png):
            print("  {:<24} MISSING   no capture in {}".format(name, args.current))
            failures.append(name)
            continue
        result = backend.diff_fraction(baseline_png, current_png)
        if isinstance(result, str):
            print("  {:<24} FAIL      {}".format(name, result))
            failures.append(name)
            continue
        pct = result * 100.0
        if pct > threshold:
            print("  {:<24} FAIL      {:.4f}% differs (threshold {:.4f}%)".format(
                name, pct, threshold))
            failures.append(name)
        else:
            print("  {:<24} ok        {:.4f}% (threshold {:.4f}%)".format(
                name, pct, threshold))

    unbaselined = [n for n in current if n not in baselines]
    if unbaselined:
        print("NEW (no baseline): {}".format(", ".join(unbaselined)))
        print("  run 'make update-baselines' to add them")

    print("{}/{} screens within threshold".format(len(baselines) - len(failures), len(baselines)))
    if failures:
        print("FAILED: {}".format(", ".join(failures)), file=sys.stderr)
        print("If the change is intentional: make update-baselines, then review "
              "the PNG diff before committing.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
