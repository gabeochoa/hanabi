#!/usr/bin/env bash
# ===========================================================================
# scripts/perf_text.sh -- what the app spends on TEXT, per frame, as counts.
#
#   scripts/perf_text.sh [exe] [frames]
#
# WHY A SCRIPT AND NOT A PARAGRAPH IN A DOC. Three separate caches sit in
# front of hanabi's text measurement -- the transcript's per-message render
# memo, the sidebar's ellipsis memo, and afterhours' own TextMeasureCache --
# and until this script existed exactly one of them reported a hit rate. A
# cache whose hit rate nobody measured is not a cache, it is a hope: the
# transcript's ran at 34% for months and the only symptom was that long
# threads were slow (docs/perf/TRANSCRIPT.md, entry 3).
#
# EVERYTHING HERE IS A COUNT, deliberately. This box is shared with several
# other agents and its load average has been observed at 29; a millisecond
# measured on it is a measurement of the machine (docs/perf/GATES.md). Counts
# are identical to the call across runs at any load, so an A/B of two builds
# is a comparison of the builds. The one time figure printed is CPU time
# (CLOCK_THREAD_CPUTIME_ID), and it is there for scale, not for gating.
#
# THREE SCENARIOS, because the text work is in three different places:
#
#   idle    a 120-message transcript standing still. Everything here should
#           be served from a memo; a number that grows with thread length is
#           a memo that is not working.
#   read    the same transcript being scrolled. The window moves, so the
#           memo is asked for messages it has not seen -- the case that
#           tells you whether the hit rate survives motion.
#   rows    a 2000-session sidebar being scrolled. This is the ELLIPSIS
#           path: one fit_to_width per visible row per frame, which `sample`
#           once put at 34% of the main thread (afterhours_gaps.md #116).
#
# Read `text.*` as work and `cache.*` as whether it was avoided.
# ===========================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="${1:-$ROOT/output/hanabi.exe}"
FRAMES="${2:-900}"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make'." >&2
    exit 2
fi

# The counters worth reading, in the order they make a story. Anything else
# the binary emits is still in the raw output; this is the summary.
KEEP='text\.|cache\.|TextMeasureCache|FRAME \(cpu\)|ALLOCATIONS|transcript\.pass|gauge'

run_one() {
    local name="$1" stress="$2" turns="$3" sessions="$4" tab="$5"
    local H
    H="$(mktemp -d /tmp/hanabi_perftext.XXXXXX)"
    mkdir -p "$H/Library/Application Support/hanabi"
    cat > "$H/Library/Application Support/hanabi/settings.json" <<JSON
{"window_width":1180,"window_height":949,"open_tabs":["$tab"],"active_tab":"$tab","theme":"dark"}
JSON
    echo "--- $name  (stress=$stress turns=$turns sessions=$sessions) ---"
    env HOME="$H" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
        HANABI_CONFIG="/tmp/none_$$" HANABI_BIG_TRANSCRIPT=1 \
        HANABI_BIG_TURNS="$turns" HANABI_STRESS_SESSIONS="$sessions" \
        HANABI_PROF=1 HANABI_SOAK="$FRAMES" HANABI_STRESS="$stress" \
        "$EXE" --screenshot "$H/o.png" 2>&1 |
        grep -E '^\[prof\]' | grep -E "$KEEP"
    rm -rf "$H"
}

echo "=== text measurement, $FRAMES frames per scenario, counts per frame ==="
run_one idle idle   30 20   rbig
run_one read read   30 20   rbig
run_one rows scroll 1  2000 rbig
