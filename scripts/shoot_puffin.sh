#!/usr/bin/env bash
# ===========================================================================
# shoot_puffin.sh -- capture ONE Puffin window as a 1x PNG.
#
# Puffin is a real AppKit app on a retina screen, so its window buffer is 2x.
# hanabi's headless capture is 1x. Everything here downsamples Puffin to 1x
# rather than upscaling hanabi: upscaling invents pixels and would flatter the
# comparison.
#
# The window is captured BY ID (-l), not by screen region, so whatever else is
# on Gabe's desktop -- a dialog, another app -- cannot land in the reference.
#
#   usage: shoot_puffin.sh <out.png>
# ===========================================================================
set -u
OUT="${1:?usage: shoot_puffin.sh <out.png>}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

read -r WIN X Y W H REST < <("$DIR/winlist" Puffin | head -1)
if [ -z "${WIN:-}" ]; then
    echo "shoot_puffin: no Puffin window on screen" >&2
    exit 2
fi

TMP="$(mktemp /tmp/pf_shot.XXXX).png"
screencapture -x -o -l"$WIN" "$TMP" || exit 2
# -Z takes the LONGEST side; the window is wider than it is tall here, but pass
# the width explicitly so a future portrait window cannot silently flip it.
sips -z "$H" "$W" "$TMP" --out "$OUT" >/dev/null || exit 2
rm -f "$TMP"
echo "$OUT  (${W}x${H} logical, from window $WIN)"
