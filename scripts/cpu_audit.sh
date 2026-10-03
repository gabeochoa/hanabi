#!/usr/bin/env bash
# scripts/cpu_audit.sh -- idle CPU across the states a person leaves Hanabi in.
#
# The idle gate (scripts/idle_gate.sh) measures ONE state: Home, nothing open.
# A reader leaves the app in many others -- a thread open, two panes split,
# Settings up, the MM3 icon set with a row waiting on them, a 2000-thread
# catalog -- and any one of them can hold the frame loop awake. This runs the
# same deterministic harness (HANABI_IDLE_TIMING: N simulated 120 Hz display
# callbacks, the real system tree, the real frame-admission policy) in each,
# and prints one row per arm: full frames, thread CPU ms per logical second,
# allocations per logical second.
#
# Usage: scripts/cpu_audit.sh [callbacks]     (default 1200 = 10 logical s)
# HANABI_AUDIT_ARMS="home thread" limits the arms.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_AUDIT_EXE:-$ROOT/output/hanabi.exe}"
CALLBACKS="${1:-1200}"
[ -x "$EXE" ] || { echo "cpu_audit: $EXE not found" >&2; exit 2; }

S_HOME='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}'
S_THREAD='{"window_width":1100,"window_height":760,"open_tabs":["t1"],"active_tab":"t1","theme":"dark"}'
S_TABS='{"window_width":1100,"window_height":760,"open_tabs":["t1","t2","t3","t4","t5"],"active_tab":"t1","theme":"dark"}'
S_SPLIT='{"window_width":1400,"window_height":860,"open_tabs":["t1","t2"],"active_tab":"t2","split_open":true,"split_ratio":0.5,"split_panes":["t1","t2"],"split_focused_pane":1,"theme":"dark"}'
S_MM3='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark","icon_set":"mm3"}'
S_MM3T='{"window_width":1100,"window_height":760,"open_tabs":["t1"],"active_tab":"t1","theme":"dark","icon_set":"mm3"}'

arm() {  # name settings env...
    local name="$1" settings="$2"
    shift 2
    local home log
    home="$(mktemp -d)"
    log="$(mktemp -t hanabi_audit_XXXX)"
    mkdir -p "$home/Library/Application Support/hanabi"
    printf '%s\n' "$settings" >"$home/Library/Application Support/hanabi/settings.json"
    env HOME="$home" HANABI_BACKEND=mock HANABI_CONFIG=/nonexistent/hanabi/audit.json HANABI_PROF=1 \
        HANABI_IDLE_TIMING="$CALLBACKS" HANABI_IDLE_WHY=1 "$@" timeout 180 "$EXE" --screenshot "$home/a.png" >"$log" 2>&1
    local line
    line="$(grep '^IdleTiming:' "$log" | tail -1)"
    if [ -z "$line" ]; then
        printf '  %-14s  (no IdleTiming line)\n' "$name"
    else
        printf '  %-14s %s\n' "$name" "$(printf '%s\n' "$line" | awk '{
            for (i=1;i<=NF;++i){split($i,p,"="); v[p[1]]=p[2]}
            printf "frames=%-5s cpu_ms/s=%-9s allocs/s=%s", v["frames"], v["cpu_ms_per_sec"], v["allocs_per_sec"]}')"
    fi
    if [ -n "${HANABI_AUDIT_WHY:-}" ]; then grep '^IdleWhy:' "$log" | sed 's/^/                 /'; fi
    rm -rf "$home" "$log"
}

ARMS="${HANABI_AUDIT_ARMS:-home home2000 thread tabs5 split settings mm3 mm3thread thinking list1hz list1hz2000}"
echo "=== hanabi idle CPU audit ($CALLBACKS callbacks) ==="
for a in $ARMS; do
    case "$a" in
        home) arm home "$S_HOME" ;;
        home2000) arm home2000 "$S_HOME" HANABI_STRESS_SESSIONS=2000 ;;
        thread) arm thread "$S_THREAD" HANABI_OPEN=t1 ;;
        tabs5) arm tabs5 "$S_TABS" HANABI_OPEN=t1 ;;
        split) arm split "$S_SPLIT" HANABI_OPEN=t1 HANABI_SPLIT=t2 ;;
        settings) arm settings "$S_HOME" HANABI_TEST_OVERLAY=settings ;;
        mm3) arm mm3 "$S_MM3" ;;
        mm3thread) arm mm3thread "$S_MM3T" HANABI_OPEN=t1 ;;
        # A reply in flight on the open thread (the thinking state).
        thinking) arm thinking "$S_THREAD" HANABI_OPEN=t1 HANABI_MOCK_STREAM_HOLD=1 HANABI_AUDIT_SEND=hello ;;
        # The list landing once a second, unchanged (a live run's events).
        list1hz) arm list1hz "$S_HOME" HANABI_AUDIT_LIST_EVERY=120 ;;
        list1hz2000) arm list1hz2000 "$S_HOME" HANABI_STRESS_SESSIONS=2000 HANABI_AUDIT_LIST_EVERY=120 ;;
        *) echo "  unknown arm $a" ;;
    esac
done
