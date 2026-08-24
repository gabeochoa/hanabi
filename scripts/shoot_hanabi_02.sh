#!/usr/bin/env bash
# ===========================================================================
# scripts/shoot_hanabi_02.sh -- capture hanabi in ref/02_thread.png's state.
#
#   scripts/shoot_hanabi_02.sh [out.png] [thread_id] [worktree_root]
#
# A SECOND script rather than a flag on shoot_hanabi.sh, because that one is
# the 01 state and several agents shoot with it at once; a flag that defaults
# wrong is a worse failure than two files.
#
# 02 differs from 01 in the fixture, not the window: same 1180x949, same mock
# backend, but ONE UNPINNED tab instead of two pinned ones. Shooting 01's blob
# against 02 measures the tab strip's fixture, the same trap REFERENCE.md
# describes in the other direction.
#
# The default thread is r9, and the choice is the whole point of the script.
# r9 IS the reference's thread: `ref/02_thread.png` has Puffin's
# `mock-outcome-2` open ("row 133 banyan diff gate"), and hanabi's r9 is that
# fixture ported turn for turn -- same question, same reply, same fenced two
# lines, same `failed` outcome (src/api/mock_client.h, feat/vis-fixture).
#
# It used to default to r5, and the reasoning is worth keeping because it says
# what changed: with hanabi's transcript holding its OWN words, no thread could
# be compared with the reference at all, so the script picked the nearest-
# SHAPED one -- r5 is a short question and one paragraph back, and t2 scored
# 4.5 structural points worse on `main` purely for having more text on screen.
# That made `main` a measure of how much prose each fixture happened to carry.
# With the words shared, what the number measures is design. Pass another id to
# compare a different pair deliberately.
#
# WARNING, and it is not hypothetical: the mock fixture's timestamps are
# relative to the wall clock (`mins_ago`/`hrs_ago` in src/api/mock_client.h),
# and the transcript inserts a date divider where the calendar day changes
# between two messages. Around local midnight that divider MOVES between one
# capture and the next, which shifts every row below it. Two shots of the same
# binary taken either side of 00:00 differ by 0.6 structural points on `main`
# with no code change at all. If you are A/B-ing two builds, shoot both back
# to back and diff the two hanabi PNGs against each other first: if they
# differ anywhere you did not touch, the clock moved and the comparison is
# void.
#
# ---------------------------------------------------------------------------
# 2x CAPTURE (HANABI_SHOOT_2X=1) -- opt-in, off by default, and identical in
# meaning to the same knob on shoot_hanabi.sh: render the SAME UI into a
# 2360x1898 texture at theme.ui_scale = 2.0 (afterhours' Adaptive scaling --
# a zoom, not a bigger canvas) and reduce to 1180x949 with LANCZOS, the way
# the reference was reduced. Both paths write the same 1180x949 file.
# ---------------------------------------------------------------------------
# ===========================================================================
set -uo pipefail

OUT="${1:-/tmp/hb02.png}"
TAB="${2:-r9}"
ROOT="${3:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
EXE="$ROOT/output/hanabi.exe"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make'." >&2
    exit 2
fi

H="$(mktemp -d /tmp/hanabi_shoot02_home.XXXXXX)"
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
{"window_width":$WPX,"window_height":$HPX,"open_tabs":["$TAB"],"active_tab":"$TAB","pinned_tabs":[],"theme":"dark"}
JSON

rm -f "$OUT" "$RAW"
env HOME="$H" HANABI_WIN_W="$WPX" HANABI_WIN_H="$HPX" HANABI_UI_SCALE="$UIS" \
    HANABI_BACKEND=mock \
    HANABI_CONFIG="/tmp/none_$$" "$EXE" --screenshot "$RAW" \
    > "/tmp/hanabi_shoot02_$$.log" 2>&1 &
pid=$!
for _ in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
kill -9 "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
pkill -9 -f "^$EXE" >/dev/null 2>&1

if [ "${HANABI_SHOOT_2X:-0}" = "1" ]; then
    if ! /usr/bin/python3 "$ROOT/scripts/downsample.py" "$RAW" "$OUT" \
            "$W1X" "$H1X"; then
        echo "FAIL: 2x downsample" >&2
        tail -20 "/tmp/hanabi_shoot02_$$.log" >&2
        exit 1
    fi
    rm -f "$RAW"
fi

dim="$(/usr/bin/file "$OUT" 2>/dev/null | sed -nE 's/.*, ([0-9]+ x [0-9]+),.*/\1/p')"
if [ "$dim" != "1180 x 949" ]; then
    echo "FAIL: expected 1180 x 949, got '${dim:-nothing}'" >&2
    tail -20 "/tmp/hanabi_shoot02_$$.log" >&2
    exit 1
fi
echo "OK  $OUT  ($dim)  thread=$TAB"
