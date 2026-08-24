#!/usr/bin/env bash
# ===========================================================================
# scripts/shoot_hanabi.sh -- capture hanabi in the parity reference state.
#
#   scripts/shoot_hanabi.sh [out.png] [worktree_root]
#
# The reference (docs/visual-parity/ref/01_home.png) was shot at Puffin's own
# 1180x949 with TWO pinned tabs and the second one active. Shooting anything
# else and comparing it to that reference measures the fixture, not the design:
# a single unpinned tab scores the tab bar at 25.3% against 5.5% for the right
# one, and the Home digest scores `main` at 58% against 12%. See REFERENCE.md,
# "Compare LIKE FOR LIKE".
#
# Isolated HOME, mock backend, a config path that does not exist, and a scoped
# pkill afterward -- the same contract as screens.sh, so it never reads or
# writes the real settings.json and never leaves a process behind.
#
# ---------------------------------------------------------------------------
# 2x CAPTURE (HANABI_SHOOT_2X=1) -- opt-in, and off by default on purpose.
#
# The references are Puffin captured at 2x on a retina panel and downsampled to
# 1x. hanabi's capture renders into a 1x offscreen texture, so every comparison
# has been a 2x-downsampled glyph against a 1x-rasterized one, and the metric
# carries an 8-12% floor in the text regions that no design change can reach
# (REFERENCE.md, "The score has a FLOOR").
#
# With HANABI_SHOOT_2X=1 the app renders the SAME UI into a 2360x1898 texture
# at theme.ui_scale = 2.0 -- afterhours' Adaptive scaling, which multiplies
# every pixels() value including explicit font sizes, so it is a zoom and not
# a bigger canvas -- and the PNG is downsampled to 1180x949 with LANCZOS. That
# is the same operation `sips` performed on the reference; LANCZOS is the
# closest filter Pillow offers.
#
# Both paths write the same 1180x949 file, so nothing downstream changes.
# ---------------------------------------------------------------------------
# ===========================================================================
set -uo pipefail

OUT="${1:-/tmp/hb.png}"
ROOT="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
EXE="$ROOT/output/hanabi.exe"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make'." >&2
    exit 2
fi

H="$(mktemp -d /tmp/hanabi_shoot_home.XXXXXX)"
trap 'rm -rf "$H"' EXIT
mkdir -p "$H/Library/Application Support/hanabi"

# Render size: 1x by default, 2x when HANABI_SHOOT_2X=1 (see the header).
W1X=1180
H1X=949
if [ "${HANABI_SHOOT_2X:-0}" = "1" ]; then
    WPX=$((W1X * 2)); HPX=$((H1X * 2)); UIS=2.0
    RAW="${OUT%.png}.raw2x.png"
else
    WPX=$W1X; HPX=$H1X; UIS=1.0
    RAW="$OUT"
fi

cat > "$H/Library/Application Support/hanabi/settings.json" <<JSON
{"window_width":$WPX,"window_height":$HPX,"open_tabs":["t9","t2"],"active_tab":"t2","pinned_tabs":["t9","t2"],"theme":"dark"}
JSON

rm -f "$OUT" "$RAW"
env HOME="$H" HANABI_WIN_W="$WPX" HANABI_WIN_H="$HPX" HANABI_UI_SCALE="$UIS" \
    HANABI_BACKEND=mock \
    HANABI_CONFIG="/tmp/none_$$" "$EXE" --screenshot "$RAW" \
    > "/tmp/hanabi_shoot_$$.log" 2>&1 &
pid=$!
for _ in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
kill -9 "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
pkill -9 -f "^$EXE" >/dev/null 2>&1

if [ "${HANABI_SHOOT_2X:-0}" = "1" ]; then
    if ! /usr/bin/python3 "$ROOT/scripts/downsample.py" "$RAW" "$OUT" \
            "$W1X" "$H1X"; then
        echo "FAIL: 2x downsample" >&2
        tail -20 "/tmp/hanabi_shoot_$$.log" >&2
        exit 1
    fi
    rm -f "$RAW"
fi

dim="$(/usr/bin/file "$OUT" 2>/dev/null | sed -nE 's/.*, ([0-9]+ x [0-9]+),.*/\1/p')"
if [ "$dim" != "1180 x 949" ]; then
    echo "FAIL: expected 1180 x 949, got '${dim:-nothing}'" >&2
    tail -20 "/tmp/hanabi_shoot_$$.log" >&2
    exit 1
fi
echo "OK  $OUT  ($dim)"
