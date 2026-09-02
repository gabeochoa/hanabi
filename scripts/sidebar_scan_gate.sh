#!/usr/bin/env bash
# scripts/sidebar_scan_gate.sh — the sidebar must not re-derive its rows from
# the whole catalog on a frame where nothing changed.
#
# WHAT IT GATES, AND WHY IT IS A COUNT. `render_folder` used to collect its own
# members, so the panel walked `app.sessions` once per folder, once for the
# Recent catch-all, and once more to discover the folders -- (F+2) traversals a
# frame. `ecs::model::SidebarBuckets` walks once and, with no query, keeps the
# answer until `AppComponent::sessionCatalogRevision` moves. Both properties
# are counts the app already publishes, so this gate reads counts:
# milliseconds on a shared laptop would have to clear a noise floor wider than
# the whole saving at a 2,000-session catalog.
#
# FOUR ARMS, and the first one is the one that keeps the other three honest.
#
#   1. rows_built > 0        -- it DREW a sidebar. A run that renders nothing
#                               scans nothing, and every other arm here is
#                               perfect on a screen with no rows on it. See
#                               --selftest: `subagent_sidebar_open` in an
#                               inherited settings.json takes the other branch
#                               and produces exactly that false pass.
#   2. rebuilds <= ceiling   -- LEVEL: the collection ran a bounded number of
#                               times over the whole run, not once a frame.
#   3. reuse > rebuilds      -- the kept answer is actually being used, rather
#                               than the counter existing and the memo never
#                               hitting.
#   4. buckets == folders    -- the retained per-folder lists are pruned to the
#                               folders that exist now. Append-only, they would
#                               hold every key the process ever saw and slot()
#                               scans that list once per named session.
#   5. folder ratio <= 1.20  -- TREND: visits with two folders over visits with
#                               none. Per-folder collection makes that (F+2)/2
#                               = 2.0x; one pass makes it 1.0x. A level arm
#                               alone would pass a build that scanned per
#                               folder but only twice a run (docs/perf/
#                               SCROLL.md 4: a slope gate passed a 17 ms defect
#                               at ratio 1.017, so neither arm goes in alone).
#
# WHAT THIS GATE CANNOT SEE, and what does. Every counter here is published by
# SidebarBuckets about itself, so a raw loop over `app.sessions` put back into
# `render_folder` -- a loop that never enters SidebarBuckets -- leaves all four
# arms green over a panel walking the catalog (F+2) times a frame. That half is
# decidable in the source and is checked there: scripts/check_sidebar_scan.py,
# in `make source-checks`, with `scripts/gate_audit.py sidebar.raw_rescan` as
# its defect. Neither check is sufficient alone.
#
#   scripts/sidebar_scan_gate.sh [exe]
#   scripts/sidebar_scan_gate.sh --selftest
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2
# shellcheck source=scripts/watchdog.sh
source "$SCRIPT_DIR/watchdog.sh"

SELFTEST=0
if [ "${1:-}" = "--selftest" ]; then SELFTEST=1; shift; fi
EXE="${1:-$ROOT/output/hanabi.exe}"
FRAMES="${HANABI_SIDEBAR_SCAN_FRAMES:-600}"
MAX_REBUILDS="${HANABI_SIDEBAR_SCAN_MAX_REBUILDS:-8}"
MAX_FOLDER_RATIO="${HANABI_SIDEBAR_SCAN_MAX_FOLDER_RATIO:-1.20}"
RUN_TIMEOUT="${HANABI_SIDEBAR_SCAN_TIMEOUT:-180}"

if [ ! -x "$EXE" ]; then
    echo "sidebar-scan-gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

# Its own HOME, and the settings that decide WHICH sidebar renders are written
# rather than inherited. `subagent_sidebar_open` is the one that matters: it
# swaps the whole panel for the sub-agent list, which collects nothing, and a
# developer who left it on would hand this gate a green run over an empty
# screen. `open_tabs` is empty so Home renders and the sidebar is the subject.
run_arm() {  # run_arm <sessions> <folders 0|1> <outfile> [settings-json]
    local sessions="$1" folders="$2" out="$3" settings="${4:-}"
    local home log
    home="$(mktemp -d /tmp/hanabi_sidebar_scan.XXXXXX)"
    log="$out"
    mkdir -p "$home/Library/Application Support/hanabi"
    if [ -n "$settings" ]; then
        printf '%s\n' "$settings" \
            > "$home/Library/Application Support/hanabi/settings.json"
    else
        printf '%s\n' '{"window_width":1180,"window_height":949,"open_tabs":[],"active_tab":"","theme":"dark","subagent_sidebar_open":false}' \
            > "$home/Library/Application Support/hanabi/settings.json"
    fi
    local folderenv=()
    [ "$folders" = "1" ] && folderenv=(HANABI_FOLDER_DEMO=1)
    env HOME="$home" HANABI_CACHE_DIR="$home/cache" \
        HANABI_CONFIG=/nonexistent/hanabi/sidebar-scan.json \
        HANABI_BACKEND=mock HANABI_WIN_W=1180 HANABI_WIN_H=949 \
        HANABI_STRESS=idle HANABI_STRESS_SESSIONS="$sessions" \
        HANABI_PROF=1 HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY=200 \
        HANABI_SOAK_WARM_FRAMES=0 \
        "${folderenv[@]}" \
        "$EXE" --screenshot "$home/shot.png" >"$log" 2>&1 &
    local pid=$!
    watchdog_start "$pid" "$RUN_TIMEOUT"
    wait "$pid"
    local rc=$?
    watchdog_stop
    rm -rf "$home"
    return $rc
}

# A field, never an exit: a subshell that exits cannot stop this script, so an
# absent counter has to travel back as an empty string and be judged in the
# parent (scripts/digest_gate.sh learned this printing PASS over blank rows).
#
# And a COUNTER THAT IS ABSENT IS A ZERO, not a broken run. hanabi::prof only
# prints a counter that fired, so the reading that proves the memo is dead --
# `sidebar.scan_reuse` never ticking -- arrives as a missing line. Reading that
# as INCOMPLETE is how a gate reports "I could not measure" about the one
# defect it exists to catch (`scripts/gate_audit.py sidebar.no_memo` did
# exactly that until this was split). What must be PRESENT is the gauge that
# says the collection ran at all, and the frame count.
counter() { grep -E "^\[prof\] $2 " "$1" | awk '{print $3}' | tail -1; }
gauge()   { grep -E "^\[prof\] $2 " "$1" | awk '{print $NF}' | tail -1; }
zero_if_absent() { local v="$1"; [[ "$v" =~ ^[0-9]+$ ]] && echo "$v" || echo 0; }

LOG_A="$(mktemp -t hanabi_sbscan_a_XXXX).log"   # 2000 sessions, no folders
LOG_B="$(mktemp -t hanabi_sbscan_b_XXXX).log"   # 2000 sessions, two folders
LOG_C="$(mktemp -t hanabi_sbscan_c_XXXX).log"   # 20000 sessions, two folders
LOG_S="$(mktemp -t hanabi_sbscan_s_XXXX).log"   # --selftest
cleanup() { rm -f "$LOG_A" "$LOG_B" "$LOG_C" "$LOG_S"; }
trap cleanup EXIT

if [ "$SELFTEST" = "1" ]; then
    # The isolation this gate depends on, exercised: the same arm under the
    # settings a developer's own HOME might carry. With the sub-agent sidebar
    # open the panel renders no chat rows at all, so rows_built is 0 and arm 1
    # must fail. If this run PASSES, the gate can be green over a blank screen.
    echo "sidebar-scan-gate --selftest: running the folders arm under"
    echo "  subagent_sidebar_open=true (the polluted-HOME case)"
    run_arm 2000 1 "$LOG_S" \
        '{"window_width":1180,"window_height":949,"open_tabs":[],"active_tab":"","theme":"dark","subagent_sidebar_open":true}'
    rows="$(counter "$LOG_S" 'sidebar\.rows_built')"
    echo "  rows_built=${rows:-<absent, i.e. zero>}"
    if [ "$(zero_if_absent "${rows:-}")" -eq 0 ]; then
        echo "  OK: arm 1 would fail — the gate cannot pass over an empty sidebar"
        exit 0
    fi
    echo "  FAIL: rows_built=$rows with the sub-agent sidebar open;" >&2
    echo "        arm 1 no longer detects a run that drew no chat rows." >&2
    exit 1
fi

run_arm 2000 0 "$LOG_A"  || { echo "sidebar-scan-gate: INCOMPLETE — arm A failed" >&2; tail -20 "$LOG_A" >&2; exit 2; }
run_arm 2000 1 "$LOG_B"  || { echo "sidebar-scan-gate: INCOMPLETE — arm B failed" >&2; tail -20 "$LOG_B" >&2; exit 2; }
run_arm 20000 1 "$LOG_C" || { echo "sidebar-scan-gate: INCOMPLETE — arm C failed" >&2; tail -20 "$LOG_C" >&2; exit 2; }

read_arm() {  # sets ROWS REBUILDS REUSE VISITS CATALOG FOLDERS BUCKETS FRAMES_DONE
    ROWS="$(zero_if_absent "$(counter "$1" 'sidebar\.rows_built')")"
    REBUILDS="$(zero_if_absent "$(counter "$1" 'sidebar\.scan_rebuild')")"
    REUSE="$(zero_if_absent "$(counter "$1" 'sidebar\.scan_reuse')")"
    VISITS="$(zero_if_absent "$(counter "$1" 'sidebar\.scan_visits')")"
    CATALOG="$(gauge "$1" 'sidebar\.catalog')"
    FOLDERS="$(zero_if_absent "$(gauge "$1" 'sidebar\.folders')")"
    BUCKETS="$(zero_if_absent "$(gauge "$1" 'sidebar\.buckets')")"
    FRAMES_DONE="$(grep -E '^\[prof\] [0-9]+ frames' "$1" | awk '{print $2}' | tail -1)"
}

FAIL=0
printf '%-8s %8s %10s %8s %8s %9s %8s %8s\n' arm rows rebuilds reuse visits catalog folders buckets
for arm in A B C; do
    case "$arm" in
        A) read_arm "$LOG_A" ;;
        B) read_arm "$LOG_B" ;;
        C) read_arm "$LOG_C" ;;
    esac
    printf '%-8s %8s %10s %8s %8s %9s %8s %8s\n' "$arm" "${ROWS:-—}" \
        "${REBUILDS:-—}" "${REUSE:-—}" "${VISITS:-—}" "${CATALOG:-—}" \
        "${FOLDERS:-—}" "${BUCKETS:-—}"
    # `sidebar.catalog` is gauged inside the collection, so its presence is
    # the proof that the sidebar collected at least once in this run. That,
    # and the frame count, are the only two that may not be absent.
    if ! [[ "${CATALOG:-}" =~ ^[0-9]+$ && "${FRAMES_DONE:-}" =~ ^[0-9]+$ ]]; then
        echo "sidebar-scan-gate: INCOMPLETE — arm $arm never collected" >&2
        exit 2
    fi
    if [ "$FRAMES_DONE" -lt "$FRAMES" ]; then
        echo "sidebar-scan-gate: INCOMPLETE — arm $arm ran $FRAMES_DONE/$FRAMES frames" >&2
        exit 2
    fi
    if [ "$ROWS" -eq 0 ]; then
        echo "  FAIL: arm $arm drew no chat rows — it measured nothing" >&2
        FAIL=1
    fi
    if [ "$REBUILDS" -gt "$MAX_REBUILDS" ]; then
        echo "  FAIL: arm $arm collected $REBUILDS times over $FRAMES frames, over $MAX_REBUILDS" >&2
        FAIL=1
    fi
    if [ "$REUSE" -le "$REBUILDS" ]; then
        echo "  FAIL: arm $arm reused the collection $REUSE times against $REBUILDS rebuilds" >&2
        FAIL=1
    fi
    if [ "$BUCKETS" -ne "$FOLDERS" ]; then
        echo "  FAIL: arm $arm retained $BUCKETS folder buckets for $FOLDERS folders" >&2
        FAIL=1
    fi
done

read_arm "$LOG_A"; VISITS_A="$VISITS"
read_arm "$LOG_B"; VISITS_B="$VISITS"; FOLDERS_B="$FOLDERS"
RATIO="$(awk -v a="$VISITS_A" -v b="$VISITS_B" 'BEGIN{ if (a+0==0) print "inf"; else printf "%.3f", b/a }')"
printf '\n%-34s %10s %10s\n' metric measured ceiling
printf '%-34s %10s %10s\n' 'folder scan ratio (B/A)' "$RATIO" "$MAX_FOLDER_RATIO"
if [ "${FOLDERS_B:-0}" -lt 2 ] 2>/dev/null; then
    echo "  FAIL: arm B saw ${FOLDERS_B:-0} folders — the folder fixture did not take," >&2
    echo "        so the ratio arm compared two identical runs." >&2
    FAIL=1
fi
if [ "$RATIO" = "inf" ] ||
   awk -v r="$RATIO" -v c="$MAX_FOLDER_RATIO" 'BEGIN{exit !(r>c)}'; then
    echo "  FAIL: two folders cost ${RATIO}x the catalog visits of none (ceiling $MAX_FOLDER_RATIO)" >&2
    FAIL=1
fi

if [ "$FAIL" -ne 0 ]; then
    echo "sidebar-scan-gate: FAIL"
    exit 1
fi
echo "sidebar-scan-gate: PASS"
exit 0
