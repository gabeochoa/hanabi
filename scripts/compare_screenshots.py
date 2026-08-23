#!/usr/bin/env python3
"""Compare freshly captured screens against the committed baselines.

    python3 scripts/compare_screenshots.py [--baselines DIR] [--current DIR]
                                           [--declared FILE] [--lenient-new]

Every PNG in the baseline dir must have a same-named counterpart in the
current dir and differ by no more than its threshold in the manifest
(docs/screenshots/baselines/manifest.json).

--declared takes the output of `bash scripts/screens.sh --list` (or - for
stdin): every state the capture harness can produce. A declared state with no
baseline is a hole in the suite -- nothing checks it -- so it fails the run
unless the manifest's "unbaselined" map records why it is left out.

Exit 0 = all within threshold and every state accounted for, 1 = at least one
screen out of threshold, missing, or unbaselined, 2 = the comparison could not
run at all (no image backend, bad paths).
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
        return 0.0, {}, {}
    with open(path) as f:
        data = json.load(f)
    return (float(data.get("default_threshold_pct", 0.0)),
            data.get("screens", {}),
            data.get("unbaselined", {}))


def read_declared(path):
    """The state names scripts/screens.sh can capture, from `--list`."""
    if path is None:
        return None
    text = sys.stdin.read() if path == "-" else open(path).read()
    return sorted({
        line.strip() for line in text.splitlines()
        if line.strip() and not line.startswith("#")
    })


def screens_in(directory):
    if not os.path.isdir(directory):
        return None
    return sorted(
        f[:-4] for f in os.listdir(directory)
        if f.endswith(".png") and not f.startswith(".")
    )


def report_new(new, manifest_path, lenient):
    print("NEW (no baseline): {}".format(", ".join(new)))
    print("  scripts/screens.sh captures these; nothing checks them.")
    print("  Adopt them:  make update-baselines   (then review the PNGs and commit)")
    print("  Or record why they stay out, in {}:".format(manifest_path))
    print('      "unbaselined": {{"{}": "<why this state has no baseline>"}}'.format(new[0]))
    if lenient:
        print("  (--lenient-new: reported, not failed)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baselines", default=DEFAULT_BASELINES)
    parser.add_argument("--current", default=DEFAULT_CURRENT)
    parser.add_argument("--manifest", default=None)
    parser.add_argument("--declared", default=None, help=(
        "file listing every capturable state, one per line "
        "(bash scripts/screens.sh --list); - reads stdin"))
    parser.add_argument("--lenient-new", action="store_true", help=(
        "report unbaselined states without failing the run"))
    parser.add_argument("--print-new", action="store_true", help=(
        "print the unbaselined state names and exit; compares nothing"))
    args = parser.parse_args()

    manifest_path = args.manifest or os.path.join(args.baselines, MANIFEST_NAME)
    default_threshold, per_screen, excluded = load_manifest(manifest_path)

    baselines = screens_in(args.baselines)
    if baselines is None:
        print("ERROR: no baseline directory at {}".format(args.baselines), file=sys.stderr)
        return 2

    declared = read_declared(args.declared)

    # Unbaselined = capturable (declared by the harness, or already sitting in
    # the current dir) with nothing committed to compare against, minus the
    # ones the manifest says are out on purpose.
    known = set(baselines) | set(excluded)
    current = screens_in(args.current)
    seen = set(declared or []) | set(current or [])
    new = sorted(seen - known)

    if args.print_new:
        for name in new:
            print(name)
        return 0

    if not baselines:
        print("ERROR: no baseline PNGs in {} — run 'make update-baselines'".format(
            args.baselines), file=sys.stderr)
        return 2
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

    print("{}/{} screens within threshold".format(len(baselines) - len(failures), len(baselines)))

    if excluded:
        print("unbaselined on purpose: {} state(s) — see 'unbaselined' in {}".format(
            len(excluded), manifest_path))
        # An entry that names a baselined state, or one the harness no longer
        # captures, is a reason nobody will ever read again.
        for name in sorted(excluded):
            if name in baselines:
                print("  STALE   {} has a baseline; drop its 'unbaselined' entry".format(name))
            elif declared is not None and name not in declared:
                print("  STALE   {} is no longer captured by screens.sh".format(name))

    if new:
        report_new(new, manifest_path, args.lenient_new)

    if failures:
        print("FAILED: {}".format(", ".join(failures)), file=sys.stderr)
        print("If the change is intentional: make update-baselines, then review "
              "the PNG diff before committing.", file=sys.stderr)
        return 1
    if new and not args.lenient_new:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
