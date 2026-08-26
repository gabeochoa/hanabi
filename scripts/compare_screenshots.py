#!/usr/bin/env python3
"""Compare freshly captured screens against the committed baselines.

    python3 scripts/compare_screenshots.py [--baselines DIR] [--current DIR]
                                           [--declared FILE] [--lenient-new]
                                           [--failures-dir DIR] [--json PATH]

Every PNG in the baseline dir must have a same-named counterpart in the
current dir and differ by no more than its threshold in the manifest
(docs/screenshots/baselines/manifest.json).

--declared takes the output of `bash scripts/screens.sh --list` (or - for
stdin): every state the capture harness can produce. A declared state with no
baseline is a hole in the suite -- nothing checks it -- so it fails the run
unless the manifest's "unbaselined" map records why it is left out.

A failing run leaves its evidence in --failures-dir (default test-failures/):
the baseline, the fresh capture and a diff image per failure, plus --json's
machine-readable summary. The fresh captures live under output/ and the next
run wipes them, so a failure that is not written down cannot be looked at
afterwards.

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
DEFAULT_FAILURES = "test-failures"
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

    def write_diff(self, baseline, current, out_path):
        """Write a picture of WHERE the two differ. Returns the path, or None
        if this backend could not draw one."""
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

    def write_diff(self, baseline, current, out_path):
        # The baseline, dimmed, with every changed pixel painted red. A plain
        # subtraction is unreadable on a dark UI — most real diffs are a few
        # anti-aliased pixels on a near-black background, and they disappear.
        with self.Image.open(baseline) as a, self.Image.open(current) as b:
            a = a.convert("RGB")
            b = b.convert("RGB")
            if a.size != b.size:
                return None
            diff = self.ImageChops.difference(a, b)
            bands = diff.split()
            worst = bands[0]
            for band in bands[1:]:
                worst = self.ImageChops.lighter(worst, band)
            mask = worst.point(lambda v: 255 if v > 0 else 0).convert("L")
            faded = self.Image.blend(a, self.Image.new("RGB", a.size, (0, 0, 0)), 0.65)
            marked = self.Image.new("RGB", a.size, (255, 40, 40))
            faded.paste(marked, mask=mask)
            faded.save(out_path)
        return out_path


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

    def write_diff(self, baseline, current, out_path):
        proc = subprocess.run(
            [self.magick, "compare", "-highlight-color", "red",
             baseline, current, out_path],
            capture_output=True, text=True,
        )
        # `magick compare` exits 1 when the images differ, which is the case we
        # are drawing; only a worse code means it failed to write anything.
        if proc.returncode > 1 or not os.path.exists(out_path):
            return None
        return out_path


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


def write_failure_artifacts(failures, failures_dir, backend):
    """Everything needed to judge a failure after the run is gone: the two
    frames and a picture of where they differ. The current frames live under
    output/ and are wiped by the next capture, so they are COPIED here."""
    os.makedirs(failures_dir, exist_ok=True)
    for f in failures:
        name = f["name"]
        if f["current"] is None:
            continue
        f["baseline_copy"] = os.path.join(failures_dir, name + "-baseline.png")
        f["current_copy"] = os.path.join(failures_dir, name + "-current.png")
        shutil.copyfile(f["baseline"], f["baseline_copy"])
        shutil.copyfile(f["current"], f["current_copy"])
        drawn = backend.write_diff(f["baseline"], f["current"],
                                   os.path.join(failures_dir, name + "-diff.png"))
        f["diff_image"] = drawn
    print("wrote {} for {} failing screen(s)".format(
        failures_dir, sum(1 for f in failures if f["current"] is not None)))


def write_json_summary(path, baselines, failures, new, excluded, backend):
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    summary = {
        "backend": backend.name,
        "total": len(baselines),
        "passed": len(baselines) - len(failures),
        "failed": len(failures),
        "unbaselined_new": new,
        "unbaselined_recorded": sorted(excluded),
        "failures": failures,
    }
    with open(path, "w") as fh:
        json.dump(summary, fh, indent=2)
        fh.write("\n")
    print("wrote {}".format(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baselines", default=DEFAULT_BASELINES)
    parser.add_argument("--current", default=DEFAULT_CURRENT)
    parser.add_argument("--manifest", default=None)
    parser.add_argument("--declared", default=None, help=(
        "file listing every capturable state, one per line "
        "(bash scripts/screens.sh --list); - reads stdin"))
    parser.add_argument("--only", default=None, help=(
        "compare just these baselines (comma-separated state names). For the "
        "fast subset in `make test`: the states outside the list are neither "
        "captured nor compared, so the run says nothing about them and the "
        "declared/unbaselined accounting is skipped"))
    parser.add_argument("--lenient-new", action="store_true", help=(
        "report unbaselined states without failing the run"))
    parser.add_argument("--print-new", action="store_true", help=(
        "print the unbaselined state names and exit; compares nothing"))
    parser.add_argument("--failures-dir", default=DEFAULT_FAILURES, help=(
        "where to write the artifacts for a failing run (default {})".format(
            DEFAULT_FAILURES)))
    parser.add_argument("--no-save-diffs", dest="save_diffs",
                        action="store_false", help=(
        "do not write the diff PNG and the copies of the two frames"))
    parser.add_argument("--json", default=None, help=(
        "write the run summary as JSON to this path"))
    args = parser.parse_args()

    manifest_path = args.manifest or os.path.join(args.baselines, MANIFEST_NAME)
    default_threshold, per_screen, excluded = load_manifest(manifest_path)

    baselines = screens_in(args.baselines)
    if baselines is None:
        print("ERROR: no baseline directory at {}".format(args.baselines), file=sys.stderr)
        return 2

    only = None
    if args.only:
        only = [n for n in (s.strip() for s in args.only.split(",")) if n]
        missing = [n for n in only if n not in baselines]
        if missing:
            print("ERROR: --only names {} which has no baseline in {}".format(
                ", ".join(missing), args.baselines), file=sys.stderr)
            return 2
        baselines = only

    declared = read_declared(args.declared)
    if only is not None:
        declared = None

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
            failures.append({"name": name, "reason": "missing capture",
                             "threshold_pct": threshold, "diff_pct": None,
                             "baseline": baseline_png, "current": None})
            continue
        result = backend.diff_fraction(baseline_png, current_png)
        if isinstance(result, str):
            print("  {:<24} FAIL      {}".format(name, result))
            failures.append({"name": name, "reason": result,
                             "threshold_pct": threshold, "diff_pct": None,
                             "baseline": baseline_png, "current": current_png})
            continue
        pct = result * 100.0
        if pct > threshold:
            print("  {:<24} FAIL      {:.4f}% differs (threshold {:.4f}%)".format(
                name, pct, threshold))
            failures.append({"name": name, "reason": "over threshold",
                             "threshold_pct": threshold, "diff_pct": pct,
                             "baseline": baseline_png, "current": current_png})
        else:
            print("  {:<24} ok        {:.4f}% (threshold {:.4f}%)".format(
                name, pct, threshold))

    print("{}/{} screens within threshold".format(len(baselines) - len(failures), len(baselines)))

    if excluded and only is None:
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

    if failures and args.save_diffs:
        write_failure_artifacts(failures, args.failures_dir, backend)
    if args.json:
        write_json_summary(args.json, baselines, failures, new, excluded, backend)

    if failures:
        print("FAILED: {}".format(", ".join(f["name"] for f in failures)), file=sys.stderr)
        print("If the change is intentional: make update-baselines, then review "
              "the PNG diff before committing.", file=sys.stderr)
        return 1
    if new and not args.lenient_new:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
