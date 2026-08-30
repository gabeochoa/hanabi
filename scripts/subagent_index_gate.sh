#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

EXE="${1:-$ROOT/output/hanabi.exe}"
FRAMES="${HANABI_SUBAGENT_INDEX_FRAMES:-600}"
MAX_REBUILDS="${HANABI_SUBAGENT_INDEX_MAX_REBUILDS:-4}"
MAX_FRAME_MS="${HANABI_SUBAGENT_INDEX_MAX_FRAME_MS:-1.8}"
HOME_DIR="$(mktemp -d /tmp/hanabi_subagent_index.XXXXXX)"
LOG="$(mktemp -t hanabi_subagent_index_XXXX).log"
SHOT="$(mktemp -t hanabi_subagent_index_XXXX).png"
cleanup() { rm -rf "$HOME_DIR"; rm -f "$LOG" "$SHOT"; }
trap cleanup EXIT

mkdir -p "$HOME_DIR/Library/Application Support/hanabi"
printf '%s\n' '{"window_width":1180,"window_height":949,"theme":"dark","subagent_sidebar_open":true}' \
    > "$HOME_DIR/Library/Application Support/hanabi/settings.json"

if ! env HOME="$HOME_DIR" HANABI_CACHE_DIR="$HOME_DIR/cache" \
    HANABI_CONFIG=/nonexistent/hanabi/subagent-index.json \
    HANABI_BACKEND=mock HANABI_WIN_W=1180 HANABI_WIN_H=949 \
    HANABI_STRESS_SESSIONS=2000 HANABI_PROF=1 HANABI_SOAK="$FRAMES" \
    HANABI_SOAK_WARM_FRAMES=0 HANABI_SOAK_EVERY=200 \
    HANABI_STRESS=idle timeout 120 "$EXE" --screenshot "$SHOT" >"$LOG" 2>&1; then
    echo "subagent-index-gate: INCOMPLETE — app failed or timed out" >&2
    tail -30 "$LOG" >&2
    exit 2
fi

counter() { grep -E "^\[prof\] $1 " "$LOG" | awk '{print $3}' | tail -1; }
gauge() { grep -E "^\[prof\] $1 " "$LOG" | awk '{print $NF}' | tail -1; }
phase_ms() { grep -E "^\[prof\] $1 " "$LOG" | awk '{print $NF}' | tail -1; }

HITS="$(counter sidebar.subagent_index_hit)"
REBUILDS="$(counter sidebar.subagent_index_rebuild)"
FRAME_MS="$(phase_ms 'FRAME \(cpu\)')"
MATCHES="$(gauge sidebar.subagent_matches)"
FRAMES_DONE="$(grep -E '^\[prof\] [0-9]+ frames' "$LOG" | awk '{print $2}' | tail -1)"

if ! [[ "${HITS:-}" =~ ^[0-9]+$ &&
        "${REBUILDS:-}" =~ ^[0-9]+$ &&
        "${MATCHES:-}" =~ ^[0-9]+$ &&
        "${FRAMES_DONE:-}" =~ ^[0-9]+$ &&
        "${FRAME_MS:-}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "subagent-index-gate: INCOMPLETE — malformed profiler counters" >&2
    printf 'hits=%q rebuilds=%q matches=%q frames=%q frame_ms=%q\n' \
        "${HITS:-}" "${REBUILDS:-}" "${MATCHES:-}" "${FRAMES_DONE:-}" \
        "${FRAME_MS:-}" >&2
    exit 2
fi
if [ "$FRAMES_DONE" -lt "$FRAMES" ]; then
    echo "subagent-index-gate: INCOMPLETE — only $FRAMES_DONE/$FRAMES frames" >&2
    exit 2
fi

FAIL=0
printf '%-30s %10s %10s %8s\n' metric measured ceiling verdict
verdict=ok
if [ "$MATCHES" -lt 400 ]; then verdict=FAIL; FAIL=1; fi
printf '%-30s %10s %10s %8s\n' subagent_matches "$MATCHES" '>=400' "$verdict"
verdict=ok
if [ "$REBUILDS" -gt "$MAX_REBUILDS" ]; then verdict=FAIL; FAIL=1; fi
printf '%-30s %10s %10s %8s\n' index_rebuilds "$REBUILDS" "$MAX_REBUILDS" "$verdict"
verdict=ok
if [ "$HITS" -le "$REBUILDS" ]; then verdict=FAIL; FAIL=1; fi
printf '%-30s %10s %10s %8s\n' index_cache_hits "$HITS" ">$REBUILDS" "$verdict"
verdict=ok
if awk "BEGIN{exit !($FRAME_MS > $MAX_FRAME_MS)}"; then verdict=FAIL; FAIL=1; fi
printf '%-30s %10s %10s %8s\n' frame_cpu_ms "$FRAME_MS" "$MAX_FRAME_MS" "$verdict"

if [ "$FAIL" -ne 0 ]; then
    echo "subagent-index-gate: FAIL"
    exit 1
fi
echo "subagent-index-gate: PASS"
