#!/usr/bin/env bash
# scripts/retire_gate.sh — does the app still hold widgets it stopped drawing?
#
# THE BUG THIS HOLDS DOWN. afterhours' `imm::mk()` hands back the same entity
# for a call site forever and nothing ever retires one, so a screen the app has
# navigated away from stays in the collection and every UI system walks it,
# every frame, for the life of the process (afterhours_gaps.md #115). Measured
# before the fix, at a 2000-session catalog after visiting five screens: 2844
# entities and 4.57 ms/frame, for screens nobody was looking at.
#
# WHY THE OTHER GATES CANNOT SEE IT, which is the whole reason this file
# exists:
#
#   * soak_gate.sh measures the SLOPE of an idle app. This is not a slope. It
#     is a PLATEAU — the app accumulates the union of the screens you visited
#     and then sits there, flat, forever. Measured with the sweep off, the
#     entity count over three navigation cycles reads 1020, 1247, 1270: rising
#     to a high-water mark and stopping. A slope gate reads that as settling.
#   * scaling_gate.sh measures one screen at two catalog sizes and never
#     navigates. With the sweep off it reads 1.31x widgets; with it on, 1.32x.
#     It is blind to this by construction.
#
# WHAT IT MEASURES. One run of the `views` arm — Home, Blocked, Review,
# Starred, Archived, a thread, repeat — and then two COUNTS off the soak
# census. No milliseconds anywhere: this box is shared and an absolute
# threshold would flake, but a count of entities is exact and identical run to
# run.
#
#   stale widgets     widgets `mk` still owns that nothing has built for
#                     longer than the grace and that the sweep has not taken.
#                     Budget 0 — the grace is turned down to 2 frames and the
#                     sweep to every frame for this run, so anything older is
#                     something the sweep failed to take.
#   live / built      the whole claim in one ratio: what the frame HOLDS
#                     against what the frame BUILT. 1.0 is "you pay for what
#                     is on screen".
#
# Measured on this branch, 500 sessions, 1200 frames, both ways:
#
#                  live   built   stale   live/built
#     sweep on      168     159       0         1.06
#     sweep off    1251     159    1083         7.87
#
# The budgets below sit in the clear air between those two columns.
#
# WHY 1200 FRAMES AND NOT 1000. The census is taken on the LAST frame, so the
# run has to stop on a cheap screen for the live/built ratio to have any range
# in it: four of the five digest views are uncapped (docs/perf/RETIRE.md), and
# a run that stops on one of those reads 692 live against 683 built with the
# sweep ON and 1251 against 683 with it OFF -- 1.01x against 1.83x, which is
# too little clear air under a 1.50x ceiling. Stopping on a thread reads 1.06x
# against 7.87x.
#
# That is a sensitivity knob, not a correctness one, and it is worth knowing
# which: the sweep holds live/built at ~1.0 on EVERY screen, so a phase shift
# can never make this gate fail wrongly -- it can only make the ratio arm less
# sensitive. The stale arm has the same enormous separation at every phase and
# is the one to trust.
#
# REPRODUCING A FAILURE. The same run with the sweep switched off, which is
# also the exact shape of the regression (someone deletes the system
# registration in main.cpp, or the `mk` in ui_imports.h goes back to
# afterhours'):
#
#     HANABI_RETIRE=0 bash scripts/retire_gate.sh
#
# and it reads:
#
#     stale widgets     1083        budget 0      FAIL
#     live / built      7.87x       budget 1.50x  FAIL
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

STALE_BUDGET="${HANABI_RETIRE_STALE_BUDGET:-0}"
HELD_RATIO_CEILING="${HANABI_RETIRE_HELD_CEILING:-1.50}"
SESSIONS="${HANABI_RETIRE_GATE_SESSIONS:-500}"
FRAMES="${HANABI_RETIRE_GATE_FRAMES:-1200}"

[ -x "$EXE" ] || { echo "no such binary: $EXE (make first)" >&2; exit 2; }

echo "=== hanabi widget-retirement gate ==="

LOG="$(mktemp /tmp/hanabi_retire_gate.XXXXXX)"
HOME_DIR="$(mktemp -d /tmp/hanabi_retire_home.XXXXXX)"
mkdir -p "$HOME_DIR/Library/Application Support/hanabi"
cleanup() { rm -rf "$LOG" "$HOME_DIR"; }
trap cleanup EXIT

# The grace is 2 frames and the sweep runs every frame, so "stale" means the
# sweep FAILED to take it rather than "the sweep has not come round yet". The
# shipping defaults (90 / 15) are a second and a half of not-being-built, which
# is right for a person flicking between two screens and would put a whole
# screen inside the grace here.
env HOME="$HOME_DIR" HANABI_BACKEND=mock HANABI_CONFIG=/tmp/hanabi_retire_gate_no_config \
    HANABI_STRESS=views HANABI_STRESS_SESSIONS="$SESSIONS" \
    HANABI_RETIRE_GRACE=2 HANABI_RETIRE_EVERY=1 \
    HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY=$((FRAMES / 3)) HANABI_SOAK_CENSUS=1 \
    "$EXE" --screenshot "$HOME_DIR/shot.png" >"$LOG" 2>&1
rc=$?

CENSUS="$(grep -E '^\[soak\] widgets: ' "$LOG" | tail -1)"

# Three outcomes, not two. A run that never reached its census measured
# nothing, and calling that a failure is an unearned verdict in the other
# direction — on this machine the usual cause is another worktree's suite
# killing the process (docs/perf/GATES.md).
if [ -z "$CENSUS" ]; then
    echo "  INCOMPLETE: the run ended (rc=${rc}) without printing a census." >&2
    echo "  Nothing was measured. Something killed the process, or it crashed;" >&2
    echo "  this is not a retirement failure. Re-run it on its own." >&2
    exit 2
fi

# The epoch is checked FIRST and it is not a perf number: it is the guard
# against the gate's own blind spot. If ecs::WidgetRetireSystem stops being
# registered — the likeliest shape of this regression — the epoch never
# advances, every widget's stamp stays current, and "stale" reads 0 for the
# best possible reason and the worst possible cause. An epoch below the frame
# count means the frame boundary is gone.
EPOCH="$(sed -E 's/.*epoch ([0-9]+).*/\1/' <<<"$CENSUS")"
LIVE="$(sed -E 's/.*widgets: ([0-9]+) live.*/\1/' <<<"$CENSUS")"
BUILT="$(sed -E 's/.*live, ([0-9]+) built.*/\1/' <<<"$CENSUS")"
STALE="$(sed -E 's/.*built this frame, ([0-9]+) stale.*/\1/' <<<"$CENSUS")"

if [ "$BUILT" -le 0 ]; then
    echo "  INCOMPLETE: the census says 0 widgets were built this frame." >&2
    echo "  That is not a retirement result, it is a run that rendered nothing." >&2
    exit 2
fi

RATIO="$(awk -v l="$LIVE" -v b="$BUILT" 'BEGIN{printf "%.2f", l/b}')"

printf '  %-16s %10s %10s\n' "" "measured" "budget"
printf '  %-16s %10s %10s\n' "live widgets" "$LIVE" "-"
printf '  %-16s %10s %10s\n' "built / frame" "$BUILT" "-"
printf '  %-16s %10s %10s\n' "stale widgets" "$STALE" "$STALE_BUDGET"
printf '  %-16s %10s %10s\n' "live / built" "${RATIO}x" "${HELD_RATIO_CEILING}x"
printf '  %-16s %10s %10s\n' "epoch" "$EPOCH" ">=${FRAMES}"
echo "  ${SESSIONS} sessions, ${FRAMES} frames of the views arm (grace 2, sweep every frame)"

FAIL=0
if [ "${EPOCH:-0}" -lt "$FRAMES" ]; then
    FAIL=1
    echo
    echo "  FAIL: the widget epoch is ${EPOCH} after ${FRAMES} frames." >&2
    echo "        The epoch advances once per frame in ecs::WidgetRetireSystem." >&2
    echo "        If it did not, that system is no longer registered in" >&2
    echo "        build_systems() — and then every stamp reads as current," >&2
    echo "        'stale' reads 0, and nothing below this line means anything." >&2
fi
if [ "$STALE" -gt "$STALE_BUDGET" ]; then
    FAIL=1
    echo
    echo "  FAIL: ${STALE} widgets are stale, budget ${STALE_BUDGET}." >&2
    echo "        These are widgets imm::mk() still owns, that nothing has" >&2
    echo "        built for longer than the grace, and that the sweep did not" >&2
    echo "        take. Every UI system walks each of them every frame." >&2
    echo "        Look at src/ecs/widget_retire_system.h (is it still" >&2
    echo "        registered in build_systems?) and at the \`mk\` in" >&2
    echo "        src/ecs/ui_imports.h (is it still hanabi's?)." >&2
fi
if awk -v r="$RATIO" -v c="$HELD_RATIO_CEILING" 'BEGIN{exit !(r > c)}'; then
    FAIL=1
    echo
    echo "  FAIL: the frame HOLDS ${RATIO}x what it BUILDS, ceiling ${HELD_RATIO_CEILING}x." >&2
    echo "        The widget set has stopped tracking what is on screen. If" >&2
    echo "        stale is 0 above, the sweep is running and something else is" >&2
    echo "        keeping widgets alive — a screen being built every frame" >&2
    echo "        while off-screen would do it." >&2
fi

if [ "$FAIL" -ne 0 ]; then
    echo
    echo "  RETIRE GATE: FAIL" >&2
    exit 1
fi
echo "  PASS"
exit 0
