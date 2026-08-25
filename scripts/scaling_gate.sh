#!/usr/bin/env bash
# scripts/scaling_gate.sh — does the app's per-frame cost depend on how big the
# catalog is? Runs inside `make test`.
#
# WHY A RATIO AND NOT A MILLISECOND. This box is shared with three other agents
# running builds; measured on it, the same binary's median frame time at a
# 2000-session catalog read 8.27 ms on a quiet minute and 16.07 ms on a busy
# one. An absolute threshold anywhere between those two numbers is a coin flip,
# and a gate that flips gets disabled. A RATIO between two sizes measured
# back-to-back on the SAME machine in the SAME minute divides the machine out:
# both halves are slowed by the same contention.
#
# WHAT IT MEASURES. Two headless runs of the real UI, identical but for
# HANABI_STRESS_SESSIONS (the mock catalog size), reporting two ratios:
#
#   WIDGET ratio — how many UI widgets the frame builds at 2000 sessions
#     divided by how many at 20. This is the primary gate, because it is
#     DETERMINISTIC: five consecutive runs gave 348 and 2985 widgets, exactly,
#     every time. It is also the property itself rather than a symptom of it —
#     the sidebar builds a widget per row whether or not that row is on screen,
#     and every one of those widgets is torn down and re-laid-out each frame.
#
#   FRAME-TIME ratio — the best (minimum) per-frame time over 120 frames at
#     2000 divided by the same at 20. Reported and gated loosely, as a backstop
#     for a regression that costs time without costing widgets.
#
# ---------------------------------------------------------------------------
# WHERE THE THRESHOLDS COME FROM  (measured 2026-08-25, main @ 3bb921d,
# gabeochoa-mac-GRQ7Y259H4)
#
#   sessions   widgets   min ms (quiet)   min ms (load ~20)
#         20       348             1.41                1.40
#        100       454             1.65                1.65
#        500       986             2.83                4.22
#       2000      2985             7.86               11.32
#
#   widget ratio 2985/348 = 8.58, on every one of five runs
#   frame ratio  7.86/1.41 = 5.57 quiet, 11.32/1.40 = 8.09 under load
#
# THE GATE AS IT SHIPS ASSERTS WHAT IS TRUE TODAY, NOT WHAT SHOULD BE. Frame
# time is linear in the catalog on main right now; a gate demanding otherwise
# would fail on main, and a gate that is red on main is a gate somebody
# deletes. So the ceilings below sit just above today's measurements: they
# catch a regression that makes the scaling WORSE (a second per-row pass, an
# O(n^2) sort, a per-row layout that stops being shared), which is the failure
# this project has actually had.
#
# WHEN perf/sidebar-scaling LANDS — the branch virtualizing the sidebar so only
# visible rows are built — the widget ratio should collapse to about 1.0 and
# the frame ratio to under 2.0. That branch should lower these two constants to
# 1.50 and 2.50 in the SAME commit that makes them true; the gate is worth very
# little at 10.00, and the whole point of it is to hold the fix in place. Both
# numbers are one line each, right here, deliberately.
# ---------------------------------------------------------------------------
set -uo pipefail

WIDGET_RATIO_CEILING="${HANABI_SCALE_WIDGET_CEILING:-1.50}"
FRAME_RATIO_CEILING="${HANABI_SCALE_FRAME_CEILING:-2.50}"

SMALL="${HANABI_SCALE_SMALL:-20}"
BIG="${HANABI_SCALE_BIG:-2000}"
FRAMES="${HANABI_SCALE_FRAMES:-120}"
REPEATS="${HANABI_SCALE_REPEATS:-2}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"
SHOT="$(mktemp -t hanabi_scale_XXXX).png"
LOG="$(mktemp -t hanabi_scale_XXXX).log"
RUN_TIMEOUT=120

kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -f "$SHOT" "$LOG"; }
trap cleanup EXIT

if [ ! -x "$EXE" ]; then
    echo "scaling_gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/scaling-gate.json"
export HANABI_FRAME_TIMING="$FRAMES"

# Best-of-N on the MINIMUM frame time. The minimum is the run's cleanest frame
# — the one that got a whole timeslice — so it is the sample least polluted by
# whatever else the box is doing, and taking the best across repeats makes the
# two sizes comparable even if the load changed between them.
measure() {  # $1 = session count; echoes "<widgets> <best_min_ms>"
    local n="$1" widgets="" best=""
    local i out w m
    for i in $(seq 1 "$REPEATS"); do
        ( HANABI_STRESS_SESSIONS="$n" timeout "$RUN_TIMEOUT" "$EXE" \
              --screenshot "$SHOT" >"$LOG" 2>&1 ) || true
        out="$(grep -Eo 'widgets=[0-9]+ min=[0-9.]+ms' "$LOG" | head -1)"
        [ -n "$out" ] || continue
        w="${out#widgets=}"; w="${w%% *}"
        m="$(printf '%s' "$out" | sed -E 's/.*min=([0-9.]+)ms/\1/')"
        widgets="$w"
        if [ -z "$best" ] || awk "BEGIN{exit !($m < $best)}"; then best="$m"; fi
    done
    printf '%s %s' "${widgets:-0}" "${best:-0}"
}

echo "=== hanabi catalog-scaling gate ==="
read -r W_SMALL T_SMALL <<<"$(measure "$SMALL")"
read -r W_BIG T_BIG <<<"$(measure "$BIG")"

if [ "$W_SMALL" = "0" ] || [ "$W_BIG" = "0" ]; then
    echo "  FAIL: could not read a FrameTiming line from the app." >&2
    echo "        That is a crash, a killed run, or a build without" >&2
    echo "        HANABI_FRAME_TIMING — not a scaling regression. On this" >&2
    echo "        machine the usual cause of a killed run is another worktree:" >&2
    echo "        scripts/review_shots.sh kills output/hanabi.exe in EVERY" >&2
    echo "        worktree it finds, not just its own. Last 20 lines:" >&2
    tail -20 "$LOG" | sed 's/^/        /' >&2
    exit 1
fi

WR="$(awk "BEGIN{printf \"%.2f\", $W_BIG/$W_SMALL}")"
FR="$(awk "BEGIN{printf \"%.2f\", $T_BIG/$T_SMALL}")"
SESSION_RATIO="$(awk "BEGIN{printf \"%.0f\", $BIG/$SMALL}")"

printf '  %-10s %10s %10s %10s\n' "sessions" "$SMALL" "$BIG" "ratio"
printf '  %-10s %10s %10s %10s\n' "widgets" "$W_SMALL" "$W_BIG" "${WR}x"
printf '  %-10s %10s %10s %10s\n' "min ms/f" "$T_SMALL" "$T_BIG" "${FR}x"
echo "  a ${SESSION_RATIO}x bigger catalog; budget ${WIDGET_RATIO_CEILING}x widgets, ${FRAME_RATIO_CEILING}x frame time"

FAIL=0
if awk "BEGIN{exit !($WR > $WIDGET_RATIO_CEILING)}"; then
    echo "" >&2
    echo "  FAIL: widget ratio ${WR}x exceeds ${WIDGET_RATIO_CEILING}x." >&2
    echo "        A ${SESSION_RATIO}x catalog is building ${WR}x the widgets, so the frame's" >&2
    echo "        work is growing with the catalog faster than it did when this" >&2
    echo "        ceiling was set (${SESSION_RATIO}x sessions -> 8.58x widgets, 2026-08-25)." >&2
    echo "        Look for a per-row widget added to the sidebar or the tab strip," >&2
    echo "        or a row that stopped being culled. docs/perf/GATES.md." >&2
    FAIL=1
fi
if awk "BEGIN{exit !($FR > $FRAME_RATIO_CEILING)}"; then
    echo "" >&2
    echo "  FAIL: frame-time ratio ${FR}x exceeds ${FRAME_RATIO_CEILING}x, with widgets at ${WR}x." >&2
    echo "        Time is growing faster than the widget count, so this is not" >&2
    echo "        just more rows: something is doing per-row work that is more" >&2
    echo "        than constant per row — a scan, a sort, or a lookup that walks" >&2
    echo "        the catalog. docs/perf/GATES.md." >&2
    FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "  PASS"
    exit 0
fi
echo "  FAILED catalog-scaling gate" >&2
exit 1
