#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_IDLE_EXE:-$ROOT/output/hanabi.exe}"
CALLBACKS="${HANABI_IDLE_CALLBACKS:-1200}"
MAX_FRAMES="${HANABI_IDLE_MAX_FRAMES:-30}"
MAX_CPU_MS_PER_SEC="${HANABI_IDLE_MAX_CPU_MS_PER_SEC:-8.0}"
MAX_ALLOCS_PER_SEC="${HANABI_IDLE_MAX_ALLOCS_PER_SEC:-3000}"

if [ ! -x "$EXE" ]; then
    echo "idle_gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

HOME_DIR="$(mktemp -d)"
LOG="$(mktemp -t hanabi_idle_XXXX).log"
cleanup() {
    rm -rf "$HOME_DIR"
    rm -f "$LOG"
}
trap cleanup EXIT INT TERM
mkdir -p "$HOME_DIR/Library/Application Support/hanabi"
printf '%s\n' '{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}' > "$HOME_DIR/Library/Application Support/hanabi/settings.json"

env HOME="$HOME_DIR" HANABI_BACKEND=mock \
    HANABI_CONFIG=/nonexistent/hanabi/idle-gate.json HANABI_PROF=1 \
    HANABI_IDLE_TIMING="$CALLBACKS" "$EXE" --screenshot "$HOME_DIR/idle.png" \
    >"$LOG" 2>&1

LINE="$(grep '^IdleTiming:' "$LOG" | tail -1)"
if [ -z "$LINE" ]; then
    echo "idle_gate: FAIL — no IdleTiming result" >&2
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
FAIL=0

printf '%s\n' '=== hanabi idle activity gate ==='
printf '  callbacks:       %s\n' "$CALLBACKS"
printf '  full frames:     %s  ceiling %s\n' "$FRAMES" "$MAX_FRAMES"
printf '  thread CPU:      %s ms/s  ceiling %s\n' "$CPU" "$MAX_CPU_MS_PER_SEC"
printf '  allocations:     %s /s  ceiling %s\n' "$ALLOCS" "$MAX_ALLOCS_PER_SEC"

[ "$FRAMES" -le "$MAX_FRAMES" ] || FAIL=1
awk -v value="$CPU" -v max="$MAX_CPU_MS_PER_SEC" 'BEGIN { exit !(value <= max) }' || FAIL=1
awk -v value="$ALLOCS" -v max="$MAX_ALLOCS_PER_SEC" 'BEGIN { exit !(value <= max) }' || FAIL=1

if [ "$FAIL" -ne 0 ]; then
    echo "  FAIL: idle work exceeded an absolute per-second ceiling." >&2
    exit 1
fi

echo "  PASS"
