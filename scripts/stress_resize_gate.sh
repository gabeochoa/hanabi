#!/usr/bin/env bash
# scripts/stress_resize_gate.sh — the soak's resize reaches the BACKEND.
#
# hanabi::gfx defers a resize's render-target swap to a frame boundary
# (afterhours_gaps.md #374). A deferral is drained by hanabi::gfx::begin_frame,
# so a frame loop that opens its frame any other way arms resizes that are
# never applied — the scenario looks busy, the target never moves, and every
# RSS and slope number taken from it is measured against a window that never
# changed size. That is exactly what the soak loops did when the deferral first
# landed, and nothing caught it: `resizes=520` was printed either way.
#
# So this gate reads BOTH counters. `resizes` is what the scenario asked for;
# `resizes_applied` is what reached the backend, with the size it landed at.
#
# A COUNT, not a millisecond, for the reason every other gate here records:
# this box is shared and a time budget on it is a coin flip.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_STRESS_EXE:-$ROOT/output/hanabi.exe}"
FRAMES="${HANABI_RESIZE_GATE_FRAMES:-400}"
SESSIONS="${HANABI_RESIZE_GATE_SESSIONS:-40}"

if [ ! -x "$EXE" ]; then
    echo "stress_resize_gate: $EXE not found — run 'zig build' first." >&2
    exit 2
fi

kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
trap kill_own_runs EXIT

H="$(mktemp -d)"
LOG="$H/run.log"
env HOME="$H" HANABI_BACKEND=mock HANABI_CONFIG=/nonexistent/resize-gate.json \
    HANABI_STRESS=resize HANABI_SOAK="$FRAMES" \
    HANABI_STRESS_SESSIONS="$SESSIONS" \
    HANABI_SOAK_WARM_FRAMES=0 HANABI_SOAK_EVERY=100 \
    HANABI_SOAK_MAX_RSS_KB_PER1K=999999 \
    HANABI_SOAK_MAX_HEAP_KB_PER1K=999999 \
    HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K=999999 \
    HANABI_SOAK_MAX_ENT_PER1K=999999 HANABI_SOAK_MAX_MS_PER1K=999999 \
    "$EXE" --screenshot "$H/shot.png" > "$LOG" 2>&1
rc=$?

echo "=== hanabi stress-resize gate ==="
echo "  ${FRAMES} frames, ${SESSIONS} sessions, resize scenario, headless"

if [ "$rc" -ne 0 ]; then
    echo "  FAIL — the run exited $rc (a mid-pass teardown aborts here)"
    tail -5 "$LOG" | sed 's/^/        /' >&2
    rm -rf "$H"; exit 1
fi

line=$(grep -oE "resizes=[0-9]+ resizes_applied=[0-9]+ applied_size=[0-9]+x[0-9]+" "$LOG" | tail -1)
if [ -z "$line" ]; then
    echo "  FAIL — no work_done line; the run measured nothing"
    tail -5 "$LOG" | sed 's/^/        /' >&2
    rm -rf "$H"; exit 1
fi

asked=$(echo "$line"   | sed -E 's/.*resizes=([0-9]+).*/\1/')
applied=$(echo "$line" | sed -E 's/.*resizes_applied=([0-9]+).*/\1/')
size=$(echo "$line"    | sed -E 's/.*applied_size=([0-9]+x[0-9]+).*/\1/')

printf '  %-22s %s\n' "resizes asked" "$asked"
printf '  %-22s %s\n' "resizes applied" "$applied"
printf '  %-22s %s\n' "backend size now" "$size"

fail=0
# 1. The fixture happened at all.
if [ "$asked" -lt 10 ]; then
    echo "  FAIL — the scenario asked for only $asked resizes; it is not resizing"
    fail=1
fi
# 2. Every requested resize reached the backend. This is the arm that catches
#    a frame loop opening its frame outside hanabi::gfx::begin_frame.
if [ "$applied" -ne "$asked" ]; then
    echo "  FAIL — $asked resizes asked but $applied reached the backend."
    echo "         A frame loop is opening its frame without the deferral"
    echo "         drain (src/util/gfx_resize.h::begin_frame)."
    fail=1
fi
# 3. The backend REPORTS the size it was asked for. applied_size is read back
#    from graphics::get_screen_width/height after the call, not echoed from the
#    request, so a set_window_size that became a no-op fails HERE even while
#    the counter above still advances.
asked_size=$(grep -oE "resize_target=[0-9]+x[0-9]+" "$LOG" | tail -1 | cut -d= -f2)
if [ "$size" = "0x0" ]; then
    echo "  FAIL — the backend never reported a size"
    fail=1
elif [ -n "$asked_size" ] && [ "$size" != "$asked_size" ]; then
    echo "  FAIL — the last resize asked for $asked_size but the backend"
    echo "         reports $size. The call is not moving the target."
    fail=1
fi

rm -rf "$H"
[ "$fail" -eq 0 ] || exit 1
echo "  PASS"
