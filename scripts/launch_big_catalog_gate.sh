#!/usr/bin/env bash
# scripts/launch_big_catalog_gate.sh -- launch at the kt-fqew knot shape.
#
# kt-fqew is the Puffin launch knot (median 5,023 ms of n=90 real launches,
# 98% unexplained). Re-timed on Hanabi at the same shape -- a ~9,000-session
# catalog with 9 restored conversations -- the headless FirstFrame is
# 266-348 ms on a loaded shared box (Startup ~40 ms), ~15x under the
# Puffin bar, with no 5-second launch to fix: Hanabi's own init is ~1 ms
# and the windowed launch is 84-89% OS/Metal Gfx init (docs/perf/STARTUP.md).
# This gate pins that: best-of-3 FirstFrame under a generous ceiling, so a
# launch regression toward Puffin territory fails loudly instead of
# creeping. Best-of-3, like measure_launch.sh, so transient box load
# cannot flake it. Red proof: HANABI_LAUNCH_BIG_FIRSTFRAME_CEILING_MS=100
# fails it (the gate reads FirstFrame and enforces the ceiling).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_LAUNCH_EXE:-$ROOT/output/hanabi.exe}"
ROUNDS="${HANABI_LAUNCH_BIG_ROUNDS:-3}"
CEILING="${HANABI_LAUNCH_BIG_FIRSTFRAME_CEILING_MS:-1500}"
TABS='["s0","s1","s2","s3","s4","s5","s6","s7","s8"]'

if [ ! -x "$EXE" ]; then
    echo "launch_big_catalog_gate: $EXE not found — run 'zig build' first." >&2
    exit 2
fi

BEST=""
for _ in $(seq 1 "$ROUNDS"); do
    HOME_DIR="$(mktemp -d)"
    LOG="$(mktemp -t hanabi_big_launch_XXXX).log"
    mkdir -p "$HOME_DIR/Library/Application Support/hanabi"
    printf '{"window_width":1100,"window_height":760,"open_tabs":%s,"active_tab":"s0","theme":"dark"}' "$TABS" > "$HOME_DIR/Library/Application Support/hanabi/settings.json"
    env HOME="$HOME_DIR" HANABI_BACKEND=mock \
        HANABI_CONFIG=/nonexistent/hanabi/big-launch-gate.json HANABI_PROF=1 \
        HANABI_STRESS_SESSIONS=9000 "$EXE" --screenshot "$HOME_DIR/big-launch.png" \
        >"$LOG" 2>&1
    FF="$(grep -Eo 'FirstFrame: [0-9]+ ms' "$LOG" | grep -Eo '[0-9]+' | head -1)"
    SU="$(grep -Eo 'Startup: [0-9]+ ms' "$LOG" | grep -Eo '[0-9]+' | head -1)"
    echo "  round: Startup=${SU:-?} ms  FirstFrame=${FF:-?} ms"
    rm -rf "$HOME_DIR" "$LOG"
    if [ -n "$FF" ]; then
        if [ -z "$BEST" ] || [ "$FF" -lt "$BEST" ]; then BEST="$FF"; fi
    fi
done

printf '%s\n' '=== hanabi big-catalog launch gate (kt-fqew) ==='
printf '  shape:         9000-session catalog + 9 restored tabs (mock)\n'
printf '  best FirstFrame: %s ms  ceiling %s ms\n' "${BEST:-?}" "$CEILING"
if [ -z "$BEST" ] || [ "$BEST" -gt "$CEILING" ]; then
    echo "  FAIL: big-catalog launch exceeded its ceiling." >&2
    exit 1
fi
echo "  PASS"
