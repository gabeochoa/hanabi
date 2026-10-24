#!/usr/bin/env bash
# scripts/thinking_idle_gate.sh -- a thinking turn must not hold the frame loop.
#
# kt-s4vb: while a reply was being collected, the loop ran at the thinking
# Pulse cadence (294 frames / 10 s, ~19k allocs/s) with a pending_future
# IdleWhy entry on every frame -- the `streamCollecting` bool re-asserted
# the pending wake for the whole collect instead of once per state. The
# thinking indicator is wall-clock (dot + elapsed timer), so it renders
# correctly on frames drawn for another reason, and the collect finishing
# still wakes a frame at once through async_ready's ready-edge. This gate
# runs the audit's thinking arm (a held stream + send) and requires
# home-like frames with no thinking/pending_future IdleWhy entry.
# Reverting the fix fails it (294 frames, thinking=292 pending_future=292).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_AUDIT_EXE:-$ROOT/output/hanabi.exe}"
CALLBACKS="${HANABI_IDLE_CALLBACKS:-1200}"
MAX_FRAMES="${HANABI_THINKING_IDLE_MAX_FRAMES:-30}"

if [ ! -x "$EXE" ]; then
    echo "thinking_idle_gate: $EXE not found — run 'zig build' first." >&2
    exit 2
fi

OUT="$(HANABI_AUDIT_ARMS=thinking HANABI_AUDIT_WHY=1 bash scripts/cpu_audit.sh "$CALLBACKS" 2>&1)"
echo "$OUT"
THINKING_LINE="$(printf '%s\n' "$OUT" | grep -E '^  thinking ' | tail -1)"
WHY="$(printf '%s\n' "$OUT" | grep '^                 IdleWhy:' | tail -1)"
if [ -z "$THINKING_LINE" ]; then
    echo "thinking_idle_gate: FAIL — no thinking audit line" >&2
    exit 1
fi
FRAMES="$(printf '%s\n' "$THINKING_LINE" | awk '{for (i=1;i<=NF;++i){split($i,p,"="); if (p[1]=="frames") print p[2]}}')"
FAIL=0
[ -n "$FRAMES" ] && [ "$FRAMES" -le "$MAX_FRAMES" ] || FAIL=1
case "$WHY" in
    *thinking=*|*pending_future=*) FAIL=1 ;;
esac
if [ "$FAIL" -ne 0 ]; then
    echo "thinking_idle_gate: FAIL — thinking held the frame loop (frames=$FRAMES, ceiling $MAX_FRAMES)." >&2
    exit 1
fi
echo "thinking_idle_gate: PASS (frames=$FRAMES)"
