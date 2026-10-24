#!/usr/bin/env bash
# scripts/mm3_idle_gate.sh -- the MM3 icon set must not wake the frame loop.
#
# kt-47nf: with icon_set=mm3, the faces' next-change deadline used to admit
# a full frame per face change (~25 fps: 276 frames / 10 s, ~18 ms/s CPU on
# Boulder, ~17k allocs/s) for a few rectangles in a 10pt slot. Faces now
# animate only on frames drawn for another reason (input, streaming, the
# 2 fps idle pulse), so the mm3 arm must read like home: ~20 frames, no
# animation IdleWhy entry. Re-adding an animation wake fails this gate
# (frames jump back to ~276 with animation=~276).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_IDLE_EXE:-$ROOT/output/hanabi.exe}"
CALLBACKS="${HANABI_IDLE_CALLBACKS:-1200}"
MAX_FRAMES="${HANABI_MM3_IDLE_MAX_FRAMES:-30}"
MAX_CPU_MS_PER_SEC="${HANABI_MM3_IDLE_MAX_CPU_MS_PER_SEC:-8.0}"
MAX_ALLOCS_PER_SEC="${HANABI_MM3_IDLE_MAX_ALLOCS_PER_SEC:-3000}"

if [ ! -x "$EXE" ]; then
    echo "mm3_idle_gate: $EXE not found — run 'zig build' first." >&2
    exit 2
fi

HOME_DIR="$(mktemp -d)"
LOG="$(mktemp -t hanabi_mm3_idle_XXXX).log"
cleanup() {
    rm -rf "$HOME_DIR"
    rm -f "$LOG"
}
trap cleanup EXIT INT TERM
mkdir -p "$HOME_DIR/Library/Application Support/hanabi"
printf '%s\n' '{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark","icon_set":"mm3"}' > "$HOME_DIR/Library/Application Support/hanabi/settings.json"

env HOME="$HOME_DIR" HANABI_BACKEND=mock \
    HANABI_CONFIG=/nonexistent/hanabi/mm3-idle-gate.json HANABI_PROF=1 \
    HANABI_IDLE_TIMING="$CALLBACKS" HANABI_IDLE_WHY=1 "$EXE" --screenshot "$HOME_DIR/mm3-idle.png" \
    >"$LOG" 2>&1

LINE="$(grep '^IdleTiming:' "$LOG" | tail -1)"
if [ -z "$LINE" ]; then
    echo "mm3_idle_gate: FAIL — no IdleTiming result" >&2
    tail -20 "$LOG" >&2
    exit 1
fi

field() {
    printf '%s\n' "$LINE" | awk -v key="$1" '{
        for (i=1; i<=NF; ++i) {
            split($i, pair, "=")
            if (pair[1] == key) { print pair[2]; exit }
        }
    }'
}

FRAMES="$(field frames)"
CPU="$(field cpu_ms_per_sec)"
ALLOCS="$(field allocs_per_sec)"
WHY="$(grep '^IdleWhy:' "$LOG" | tail -1)"
FAIL=0

printf '%s\n' '=== hanabi mm3 idle gate (kt-47nf) ==='
printf '  callbacks:       %s\n' "$CALLBACKS"
printf '  full frames:     %s  ceiling %s\n' "$FRAMES" "$MAX_FRAMES"
printf '  thread CPU:      %s ms/s  ceiling %s\n' "$CPU" "$MAX_CPU_MS_PER_SEC"
printf '  allocations:     %s /s  ceiling %s\n' "$ALLOCS" "$MAX_ALLOCS_PER_SEC"
printf '  IdleWhy:         %s\n' "$WHY"

[ "$FRAMES" -le "$MAX_FRAMES" ] || FAIL=1
awk -v value="$CPU" -v max="$MAX_CPU_MS_PER_SEC" 'BEGIN { exit !(value <= max) }' || FAIL=1
awk -v value="$ALLOCS" -v max="$MAX_ALLOCS_PER_SEC" 'BEGIN { exit !(value <= max) }' || FAIL=1
case "$WHY" in
    *animation=*) FAIL=1 ;;
esac

if [ "$FAIL" -ne 0 ]; then
    echo "  FAIL: an MM3 face woke the frame loop (want no animation IdleWhy entry)." >&2
    exit 1
fi

echo "  PASS"
