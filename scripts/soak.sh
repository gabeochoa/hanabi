#!/usr/bin/env bash
# scripts/soak.sh — the LONG soak. `make soak`. Run it before a release.
#
# The gate inside `make test` (scripts/soak_gate.sh) is 1000 frames of an idle
# app: short enough that nobody minds it, strict enough to catch a leak the
# size of the Metal one. This is the other end. Every scenario the stress
# driver has, several thousand frames each, plus an arm at a catalog a hundred
# times bigger than the fixture — the runs that are too slow to put in front of
# every commit, and the only ones that can see a slow drift or a leak that only
# a particular interaction reaches.
#
# It exists so that "soak it before you ship" is one command and not an
# incantation. Each arm prints its own full table; the summary at the end is
# one line per arm.
#
#   make soak                    # every arm, 4000 frames each
#   make soak FRAMES=20000       # a long night
#   make soak ARMS="scroll tabs" # just these
#
# WHY EACH ARM IS HERE
#   idle     the control. Nothing is touched. Anything that grows here grows
#            for no reason at all, which is the strongest possible finding.
#   scroll   the reported symptom was "scroll the sidebar up and down until it
#            breaks". Row build + clip + layout, sixty frames down and sixty up.
#   threads  opening a thread is the heaviest thing the app does: a fetch, a
#            transcript rebuild, a tab. One every 30 frames, forever.
#   tabs     eight tabs, then round-robin between them. Catches anything the
#            tab strip or a per-tab cache keeps hold of.
#   bigidle  the same control arm against a 2000-session catalog. A per-row
#            leak is 100x more visible here, and a cache sized by the catalog
#            shows up as a plateau at a different height rather than a slope.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

FRAMES="${HANABI_SOAK_LONG_FRAMES:-4000}"
ARMS="${HANABI_SOAK_ARMS:-idle scroll threads tabs bigidle}"
EVERY="${HANABI_SOAK_LONG_EVERY:-500}"

# The long form gates HARDER than the short one, and can: over 4000 frames the
# settling and the page-quantisation that dominate a 1000-frame window are
# amortised away. Measured on main @ 3bb921d (2026-08-25), the five arms below
# read -21 to +58 KB per 1000 frames of RSS and -30 to +36 KB of heap; the same
# binary with the autorelease pool deleted reads about +2800 on both. 256 KB is
# ~4x the worst clean arm and ~11x under the defect.
#
# Frame time keeps soak.h's own tight default (0.5 ms per 1000 frames) rather
# than the short gate's 3.0: 4000 frames is enough resolution to mean it, and a
# drift that survives that many frames is not the box being busy.
export HANABI_SOAK_MAX_RSS_KB_PER1K="${HANABI_SOAK_MAX_RSS_KB_PER1K:-256}"
export HANABI_SOAK_MAX_HEAP_KB_PER1K="${HANABI_SOAK_MAX_HEAP_KB_PER1K:-256}"
export HANABI_SOAK_MAX_ENT_PER1K="${HANABI_SOAK_MAX_ENT_PER1K:-25}"
# Frame time is gated LOOSELY here (2.0 ms per 1000 frames) and not at
# soak.h's own 0.5 default, for a reason worth knowing: the bigidle arm pegs a
# core for 40 seconds, and on this shared laptop that is long enough to
# thermally throttle. Measured on clean main, bigidle read +1.4 ms per 1000
# frames over 3000 frames and +0.4 ms over 7000 — a real algorithmic slope does
# not SHRINK as the window grows, and the per-bucket times wandered
# non-monotonically (8.6, 8.3, 8.5, 9.2, 9.8, 8.9, 10.7, 10.5) while the heap
# stayed flat to the byte. That is the machine, not the app. 0.5 would fail
# this arm on a clean tree, and a gate that is red on main is a gate somebody
# deletes.
export HANABI_SOAK_MAX_MS_PER1K="${HANABI_SOAK_MAX_MS_PER1K:-2.0}"

export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/soak-long.json"
export HANABI_SOAK_EVERY="$EVERY"

SHOT="$(mktemp -t hanabi_soaklong_XXXX).png"
LOG="$(mktemp -t hanabi_soaklong_XXXX).log"
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -f "$SHOT" "$LOG"; }
trap cleanup EXIT

if [ ! -x "$EXE" ]; then
    echo "soak: $EXE not found — run 'make' first." >&2
    exit 2
fi

echo "=== hanabi long soak ==="
echo "  ${FRAMES} frames per arm, buckets of ${EVERY}, arms: ${ARMS}"
echo "  budget per 1000 frames: RSS +${HANABI_SOAK_MAX_RSS_KB_PER1K} KB, heap +${HANABI_SOAK_MAX_HEAP_KB_PER1K} KB, entities +${HANABI_SOAK_MAX_ENT_PER1K}"
echo "  started $(date '+%H:%M:%S'), load $(uptime | sed -nE 's/.*load averages?: //p')"

SUMMARY=""
FAIL=0
# A per-arm watchdog. Not for the app's sake — for the report's. Without it a
# killed run shows up as an arm with no numbers, which reads like a leak.
ARM_TIMEOUT="${HANABI_SOAK_ARM_TIMEOUT:-600}"
for arm in $ARMS; do
    scenario="$arm"
    sessions=""
    case "$arm" in
        bigidle) scenario="idle"; sessions=2000 ;;
    esac

    echo
    echo "--- arm: ${arm} (scenario=${scenario}${sessions:+, catalog=${sessions}}) ---"
    started=$(date +%s)
    (
        export HANABI_SOAK="$FRAMES"
        export HANABI_STRESS="$scenario"
        [ -n "$sessions" ] && export HANABI_STRESS_SESSIONS="$sessions"
        timeout "$ARM_TIMEOUT" "$EXE" --screenshot "$SHOT" >"$LOG" 2>&1
    )
    rc=$?
    took=$(( $(date +%s) - started ))
    grep -E '^\[soak\]' "$LOG" | sed 's/^/  /'

    rss="$(grep -E '^\[soak\]   RSS ' "$LOG" | awk '{print $3, $4}')"
    heap="$(grep -E '^\[soak\]   heap bytes' "$LOG" | awk '{print $4, $5}')"
    # Three outcomes, not two. An arm that never reached its verdict did not
    # measure anything, and saying FAIL about it is the same unearned verdict
    # in the other direction. On this machine the usual cause is another
    # worktree: scripts/review_shots.sh kills output/hanabi.exe in EVERY
    # worktree it can find, not just its own.
    if ! grep -qE '^\[soak\] (PASS|-+ SOAK GATE)' "$LOG"; then
        state=INCOMPLETE
        FAIL=1
        echo "  the run ended (rc=${rc}) before it reached a verdict — nothing was" >&2
        echo "  measured. Something killed the process, or it crashed; this is not" >&2
        echo "  a leak. Re-run the arm on its own:" >&2
        echo "      HANABI_SOAK_ARMS=${arm} bash scripts/soak.sh" >&2
        rss="not measured"; heap="not measured"
    elif [ "$rc" -eq 0 ]; then
        state=PASS
    else
        state=FAIL
        FAIL=1
    fi
    SUMMARY="${SUMMARY}$(printf '  %-9s %-10s RSS %-14s heap %-14s %4ss\n' \
        "$arm" "$state" "${rss:-?}" "${heap:-?}" "$took")"$'\n'
done

echo
echo "=== soak summary (per 1000 frames) ==="
printf '%s' "$SUMMARY"
if [ "$FAIL" -eq 0 ]; then
    echo "  ALL ARMS FLAT"
    exit 0
fi
if printf '%s' "$SUMMARY" | grep -q INCOMPLETE; then
    echo "  AN ARM DID NOT FINISH — that is a killed or crashed process, not a" >&2
    echo "  measurement. Re-run it on its own before believing anything here." >&2
    exit 1
fi
echo "  SOME ARMS GREW — see the failing arm's table above, and docs/perf/GATES.md" >&2
exit 1
