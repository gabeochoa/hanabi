#!/usr/bin/env bash
# scripts/scroll_gate.sh — the SCROLL gate. Runs inside `make test`.
#
# WHAT IT IS FOR. The bug report is one sentence: "what I did was just open the
# program and scroll the sidebar up and down until it broke." Every gate in
# this repo was green while that was true, and the scroll ARM of the soak was
# green too — because it was not scrolling anything. The sidebar caps its list
# at two viewports, so wheeling that list slides a clip rectangle over rows
# that were going to be built anyway: at a 2000-session catalog, `idle` and
# `scroll` allocated 7,422,071 and 7,422,153 times over the same 2000 frames.
# 82 apart in 7.4 million. The arm named after the report was a second idle
# arm, and had been for as long as it existed.
#
# The list a person scrolls is the one they asked to see all of. `scrollall`
# clicks the "Show N more…" row that sits at the bottom of a long sidebar —
# which is exactly where scrolling down lands you — and then sweeps.
#
# TWO ARMS, because the failure this branch fixed had NO SLOPE. An expanded
# list cost 17.2 ms of CPU a frame at 2000 sessions, flat, from the first frame
# to the last: a trend gate would have shrugged at it forever. So:
#
#   1. LEVEL — does the expanded list cost what the catalog is, or what the
#      viewport is? Two runs of the same binary, identical but for the catalog
#      size, gated on the ratio of the ENTITY counts.
#   2. TREND — does a long scroll get more expensive as it goes? One run,
#      halved, gated on the ratio of the two halves' minimum frame CPU and on
#      the live-block slope.
#
# NOTHING HERE IS AN ABSOLUTE MILLISECOND, deliberately. This box is shared —
# load averages between 7 and 34 across the samples below — and the same binary
# has read 8.27 ms on a quiet minute and 16.07 ms on a busy one. Arm 1 is a
# count, which is exact. Arm 2 is a ratio of two minima on CLOCK_THREAD_CPUTIME
# _ID, measured minutes apart inside one process: contention only ever ADDS
# time to a bucket, so the minimum of a half is the least-polluted estimate of
# what the app itself cost over that half, and dividing two of them removes
# what is left. Both scenarios terminate on a fixed frame COUNT, so a slow
# machine runs the same work as a fast one.
#
# ---------------------------------------------------------------------------
# WHERE THE THRESHOLDS COME FROM  (measured 2026-08-25 on
# gabeochoa-mac-GRQ7Y259H4, a shared box, load averages 7 to 17)
#
# ARM 1, entity count with the list expanded, three runs at each size:
#
#      20 sessions:    364  364  364
#    2000 sessions:    472  472  472     1.30x
#   20000 sessions:    483  483  483     1.33x
#
# Zero spread, to the entity, because a count is not a measurement of the
# machine — and flat across a 1000x catalog, which is the property. The ceiling
# is 1.60x. With row virtualization reverted (row_window() returning the whole
# list) the same pair reads 400 and 6645 — 16.61x, ten times the ceiling.
#
# ARM 2, eight consecutive clean runs:
#   frame cpu, min-of-half ratio:
#     1.019  0.986  1.007  1.027  0.992  1.001  1.004  0.998
#   live blocks per 1000 frames:
#     -147.5  -18.3  -149.2  -14.2  -149.2  -147.5  0.0  -18.3
#
# All three arms have been made to fail on purpose, which is the only way to
# know a gate bites:
#
#   level       row_window() returns the whole list       16.61x  (vs 1.60)
#   blocks      row ids keyed on the row INDEX rather
#               than the window slot, so mk() mints an
#               entity per row scrolled past (#115)      +180/1k  (vs 150)
#   frame cpu   a per-frame walk over an index of rows
#               visited, which is the "work proportional
#               to something that grows" shape            1.223x  (vs 1.15)
#
# Note what the FIRST of those did to the other two arms: 17.040 ms then
# 17.326 ms, ratio 1.017, blocks +1.9 — a clean pass. The bug this gate was
# written for HAS NO SLOPE. It is eleven times too expensive from the first
# frame to the last, and a trend gate on its own would have shrugged at it
# forever. That is why arm 1 exists and why it runs first.
#
# The frame-cpu arm fires at about +0.47 ms of drift across the halves of a
# 1600-frame run. Anything a person would describe as "it gets slower the
# longer I scroll" is far larger than that.
#
# 1.15x sits 12% above the worst clean sample. 150 blocks per 1000 frames sits
# above every clean sample (the worst of which is zero) by a margin the
# allocator's own sawtooth needs: the same eight runs measured with an
# UNDRIVEN settle pass spanned -148 to +847, which is what a title memo filling
# with 1800 entries looks like from here, and is why the settle below drives
# the scenario rather than merely rendering.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

ENTITY_RATIO_CEILING="${HANABI_SCROLL_ENTITY_CEILING:-1.60}"
export HANABI_SOAK_MAX_FRAME_RATIO="${HANABI_SOAK_MAX_FRAME_RATIO:-1.15}"
export HANABI_SOAK_MAX_BLOCKS_PER1K="${HANABI_SOAK_MAX_BLOCKS_PER1K:-150}"

SMALL="${HANABI_SCROLL_SMALL:-20}"
BIG="${HANABI_SCROLL_BIG:-2000}"
TREND_FRAMES="${HANABI_SCROLL_TREND_FRAMES:-1600}"
TREND_EVERY="${HANABI_SCROLL_TREND_EVERY:-400}"
# One full sweep of the driven triangle, so the run is measured against a warm
# title memo and a warm layout rather than against its own first pass.
TREND_SETTLE="${HANABI_SCROLL_TREND_SETTLE:-700}"

export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/scroll-gate.json"
export HANABI_STRESS=scrollall

SHOT="$(mktemp -t hanabi_scroll_XXXX).png"
LOG="$(mktemp -t hanabi_scroll_XXXX).log"
RUN_TIMEOUT=180

# Scoped to THIS worktree's binary path, not the name: several checkouts test
# on this machine at once and a bare `pkill -f hanabi.exe` kills their runs.
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -f "$SHOT" "$LOG"; }
trap cleanup EXIT

if [ ! -x "$EXE" ]; then
    echo "scroll_gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

run() {  # $1=sessions $2=frames $3=every $4=settle ; leaves output in $LOG
    HANABI_STRESS_SESSIONS="$1" HANABI_SOAK="$2" HANABI_SOAK_EVERY="$3" \
        HANABI_STRESS_SETTLE="$4" \
        timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$SHOT" >"$LOG" 2>&1
}

# The LAST bucket's entity count. The first buckets carry lazy-init; the last
# one is the steady state, and the steady state is what a frame costs forever.
last_entities() {
    grep -Eo 'entities +[0-9]+' "$LOG" | tail -1 | grep -Eo '[0-9]+'
}

echo "=== hanabi scroll gate ==="
echo "  scenario 'scrollall': expand the sidebar's list the way the \"Show N"
echo "  more…\" row does, then sweep it. docs/perf/SCROLL.md"

# ---- arm 1: the level ------------------------------------------------------
run "$SMALL" 400 400 300
E_SMALL="$(last_entities)"
run "$BIG" 400 400 300
E_BIG="$(last_entities)"

if [ -z "$E_SMALL" ] || [ -z "$E_BIG" ]; then
    echo "  FAIL: no [soak] bucket line from the app." >&2
    echo "        That is a crash or a killed run, not a regression. On this" >&2
    echo "        machine the usual cause is another worktree:" >&2
    echo "        scripts/review_shots.sh kills output/hanabi.exe in EVERY" >&2
    echo "        worktree it finds. Last 20 lines:" >&2
    tail -20 "$LOG" | sed 's/^/        /' >&2
    exit 1
fi

ER="$(awk "BEGIN{printf \"%.2f\", $E_BIG/$E_SMALL}")"
SESSION_RATIO="$(awk "BEGIN{printf \"%.0f\", $BIG/$SMALL}")"
printf '  %-24s %10s %10s %9s\n' "sessions" "$SMALL" "$BIG" "ratio"
printf '  %-24s %10s %10s %8sx\n' "entities, list expanded" "$E_SMALL" "$E_BIG" "$ER"

FAIL=0
if awk "BEGIN{exit !($ER > $ENTITY_RATIO_CEILING)}"; then
    echo "" >&2
    echo "  FAIL: entity ratio ${ER}x exceeds ${ENTITY_RATIO_CEILING}x with the list expanded." >&2
    echo "        A ${SESSION_RATIO}x catalog is building ${ER}x the entities, so the sidebar is" >&2
    echo "        building rows it cannot show. That is the whole of the" >&2
    echo "        reported bug: the list is CAPPED, and a cap is a claim about" >&2
    echo "        what the user asked for, not about what the frame costs — the" >&2
    echo "        user is allowed to ask for all of it." >&2
    echo "        Look at SidebarSystem::row_window and its two spacers." >&2
    FAIL=1
fi

# ---- arm 2: the trend ------------------------------------------------------
HANABI_SOAK_TREND=1 run "$BIG" "$TREND_FRAMES" "$TREND_EVERY" "$TREND_SETTLE"
rc=$?
grep -E '^\[soak\] +(frame cpu, min|live blocks|\(min-of-half)' "$LOG" | sed 's/^\[soak\]/ /'

if ! grep -q 'TREND PASS\|SCROLL TREND: FAIL' "$LOG"; then
    echo "  FAIL: the trend run ended before it reached a verdict (rc=${rc})." >&2
    echo "        Nothing was measured, so this is not a regression — something" >&2
    echo "        killed the process. Re-run: make scroll-gate" >&2
    exit 1
fi
if grep -q 'SCROLL TREND: FAIL' "$LOG"; then
    grep -E '^\[soak\]' "$LOG" | sed -n '/SCROLL TREND: FAIL/,$p' | sed 's/^/  /' >&2
    FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "  PASS"
    exit 0
fi
echo "  FAILED scroll gate — see docs/perf/SCROLL.md" >&2
exit 1
