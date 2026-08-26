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
# ARM 2, twelve consecutive clean runs (1600 frames, 200-frame buckets, 1300
# frames of driven settle, load averages 8 to 14):
#   frame cpu, min-of-half ratio:
#     1.002 0.980 1.000 1.027 0.968 1.045 0.983 1.016 1.027 1.000 1.000 0.994
#   live blocks per 1000 frames:
#     +5.0 -1.9 0.0 +2.5 0.0 0.0 0.0 +5.0 +5.0 0.0 +0.6 -0.6
#
# The block column used to span 845 on the same tree and had a retry over it.
# It spans 6.9 now, and the change was not to the app: the metric was the
# allocator's own tally, which drifts by a thousand blocks over a run that
# allocates nothing net. src/util/heap_walk.h has the two runs side by side and
# tests/unit/test_heap_walk.cpp pins it. The budget comes down from 150 to 40,
# which is 3.75x the sensitivity for free.
#
# TWO OTHER THINGS CHANGED WITH IT, both about giving each half more than two
# buckets to be reduced over:
#
#   * The settle is 1300 frames rather than 700. The driven triangle's period
#     is 1200, so 700 was not one full sweep -- the first measured bucket was
#     still watching the title memo fill, worth +184 blocks, and with only two
#     buckets a half the median could not absorb it. Every clean run read
#     +110 to +117 against a budget of 150. It now reads under +9.
#   * The buckets are 200 frames rather than 400, so each half is reduced over
#     four rather than two. The frame-cpu arm's protection against a busy box
#     IS that minimum: contention only ever adds time, so a half with one
#     clean bucket in it reads clean. With two buckets a half, a busy patch
#     covering both of the last half's buckets is a FAIL, and that is a flake
#     this gate had as well -- one run in ten at 400-frame buckets read 1.722x
#     on a tree whose block column was flat. Four buckets a half needs the box
#     to be busy for the whole half.
#
# All three arms have been made to fail on purpose, which is the only way to
# know a gate bites. The full table, with the defect for every gate in this
# repo, is docs/perf/GATES.md.
#
#   level       row_window() returns the whole list       16.61x  (vs 1.60)
#   blocks      row ids keyed on the row INDEX rather
#               than the window slot, so mk() mints an
#               entity per row scrolled past (#115)     +5288/1k  (vs 40)
#   frame cpu   a per-frame walk over an index of rows
#               visited, which is the "work proportional
#               to something that grows" shape            1.223x  (vs 1.15)
#
# Note what the FIRST of those did to the other two arms: 17.040 ms then
# 17.326 ms, ratio 1.017, blocks +1.9 -- a clean pass. The bug this gate was
# written for HAS NO SLOPE. It is eleven times too expensive from the first
# frame to the last, and a trend gate on its own would have shrugged at it
# forever. That is why arm 1 exists and why it runs first.
#
# The frame-cpu arm fires at about +0.47 ms of drift across the halves of a
# 1600-frame run. Anything a person would describe as "it gets slower the
# longer I scroll" is far larger than that.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

ENTITY_RATIO_CEILING="${HANABI_SCROLL_ENTITY_CEILING:-1.60}"
export HANABI_SOAK_MAX_FRAME_RATIO="${HANABI_SOAK_MAX_FRAME_RATIO:-1.15}"
export HANABI_SOAK_MAX_BLOCKS_PER1K="${HANABI_SOAK_MAX_BLOCKS_PER1K:-40}"

SMALL="${HANABI_SCROLL_SMALL:-20}"
BIG="${HANABI_SCROLL_BIG:-2000}"
TREND_FRAMES="${HANABI_SCROLL_TREND_FRAMES:-1600}"
TREND_EVERY="${HANABI_SCROLL_TREND_EVERY:-200}"
# ONE FULL PERIOD of the driven triangle (1200 frames) plus a little, so the
# run is measured against a title memo that has already seen every row rather
# than against its own first sweep. At 700 it had seen half of them, which was
# worth +184 blocks in the first measured bucket and most of the headroom the
# block budget used to need.
TREND_SETTLE="${HANABI_SCROLL_TREND_SETTLE:-1300}"

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
#
# NO RETRY. There was one, and it was hiding a measurement bug rather than a
# flake: the live-block column was the allocator's own tally, which drifts
# ~930 blocks in a lump on a run that allocates nothing net, and whether the
# lump landed inside a bucket half decided the verdict. Twelve consecutive
# clean runs now span 6.9 blocks per 1000 frames against a budget of 40.
#
# A retry over a real 400-block step would have hidden the next real leak too,
# and it very nearly hid this: the note it replaced called the step "real,
# occasional... something filling late", which was three wrong guesses about
# the app in a row because nobody looked at the number underneath.
TREND_ATTEMPTS="${HANABI_SCROLL_TREND_ATTEMPTS:-1}"
trend_ok=0
for attempt in $(seq 1 "$TREND_ATTEMPTS"); do
    HANABI_SOAK_TREND=1 run "$BIG" "$TREND_FRAMES" "$TREND_EVERY" "$TREND_SETTLE"
    rc=$?
    grep -E '^\[soak\] +(frame cpu, min|live blocks|\(min-of-half)' "$LOG" | sed 's/^\[soak\]/ /'

    if ! grep -q 'TREND PASS\|SCROLL TREND: FAIL' "$LOG"; then
        echo "  FAIL: the trend run ended before it reached a verdict (rc=${rc})." >&2
        echo "        Nothing was measured, so this is not a regression — something" >&2
        echo "        killed the process. Re-run: make scroll-gate" >&2
        exit 1
    fi
    if grep -q 'TREND PASS' "$LOG"; then
        trend_ok=1
        [ "$attempt" -gt 1 ] && echo "  (trend passed on attempt ${attempt})"
        break
    fi
    if [ "$attempt" -lt "$TREND_ATTEMPTS" ]; then
        echo "  trend attempt ${attempt} failed; re-running once — this arm is"
        echo "  known to step once in about five runs on a busy box, and a real"
        echo "  regression fails every attempt. See the note in this script."
    fi
done
if [ "$trend_ok" -eq 0 ]; then
    grep -E '^\[soak\]' "$LOG" | sed -n '/SCROLL TREND: FAIL/,$p' | sed 's/^/  /' >&2
    echo "  the trend arm failed. This gate does not retry: its block column" >&2
    echo "  spans single digits across a dozen runs (src/util/heap_walk.h)," >&2
    echo "  so a red here is a reading, not a coin." >&2
    FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "  PASS"
    exit 0
fi
echo "  FAILED scroll gate — see docs/perf/SCROLL.md" >&2
exit 1
