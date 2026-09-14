#!/usr/bin/env bash
# ===========================================================================
# tests/runner_policy/reaper_unit.sh -- SIMULATION: the reaper's decision and
# accounting (scripts/lib/reaper.sh) driven over a synthetic process table.
#
# This file binds SIMULATED providers defined below -- a pid/ppid table in two
# parallel arrays (bash 3.2: no associative arrays), a signal LOG instead of
# kill, and a grace tick that mutates the table -- to the SAME library the
# runner uses. It never names a production provider, sends no signal, reads no
# process, launches nothing. It proves, deterministically, the one behaviour
# a live race cannot: a snapshot member whose parentage breaks BETWEEN the
# TERM and KILL passes is not signalled again, and is counted, listed and
# printed as CLEANUP UNCERTAIN. Live mid-grace reparenting on the Mac is not
# separately observed.
#
# EXIT: non-zero if any assertion fails.
# ===========================================================================
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/scripts/lib/reaper.sh"

SELF=90000; R=90001; D1=90002; D2=90003
# The synthetic process table: SIM_PIDS[i] has parent SIM_PPIDS[i].
SIM_PIDS=("$R" "$D1" "$D2")
SIM_PPIDS=("$SELF" "$R" "$D1")
SIM_LOG=""
SIM_TICKS=0
sim_index() {  # sim_index <pid> -> prints the table index, or nothing
    local i
    for ((i = 0; i < ${#SIM_PIDS[@]}; i++)); do
        [ "${SIM_PIDS[$i]}" = "$1" ] && { printf '%s' "$i"; return 0; }
    done
    return 1
}
sim_ppid() { local i; i="$(sim_index "$1")" && printf '%s' "${SIM_PPIDS[$i]}"; return 0; }
sim_alive() { sim_index "$1" >/dev/null; }
sim_children() {
    local i
    for ((i = 0; i < ${#SIM_PIDS[@]}; i++)); do
        [ "${SIM_PPIDS[$i]}" = "$1" ] && printf '%s\n' "${SIM_PIDS[$i]}"
    done
    return 0
}
sim_signal() {  # SIMULATION: log it; a KILL removes the pid from the table
    SIM_LOG="$SIM_LOG$1 $2;"
    if [ "$1" = "KILL" ]; then
        local i; i="$(sim_index "$2")" || return 0
        SIM_PIDS[$i]=""; SIM_PPIDS[$i]=""
    fi
    return 0
}
sim_comm() { printf 'stub_exe.sh'; }
sim_sleep() {  # SIMULATION: the grace tick -- on the first tick D2 reparents to init
    SIM_TICKS=$((SIM_TICKS + 1))
    if [ "$SIM_TICKS" = 1 ]; then
        local i; i="$(sim_index "$D2")" && SIM_PPIDS[$i]=1
    fi
    return 0
}

bad=0
ok() { printf '  ok   SIMULATION %s\n' "$1"; }
ng() { printf '  FAIL SIMULATION %s\n' "$1"; bad=1; }
check() { if eval "$2"; then ok "$1"; else ng "$1"; fi; }

echo "=== SIMULATION: reaper decision + accounting over a synthetic process table ==="
reaper_bind sim_ppid sim_alive sim_children sim_signal sim_sleep sim_comm "$SELF" "stub_exe.sh"
REAPER_GRACE_TICKS=3

# reap_pid mutates the SIMULATION's variables (SIM_LOG, the table, the
# reaper's own accounting), so it must run in THIS shell -- a command
# substitution would run it in a subshell and lose every mutation. Its
# stderr goes to a file instead.
OUT="$(mktemp /tmp/hanabi_reaper_unit.XXXXXX)"
COMPLETED=0
on_exit() {
    local st=$?
    rm -f "$OUT"
    if [ "$COMPLETED" != 1 ]; then
        echo "SIMULATION reaper unit ABORTED before its final marker (exit $st)" >&2
        exit 70
    fi
    exit "$st"
}
trap on_exit EXIT

# --- case A: a snapshot member reparents between the passes -----------------
reap_pid "$R" 2>"$OUT" >/dev/null
check "TERM pass hits D2, D1, R in that order (deepest first, root last)" '[ "${SIM_LOG%%KILL*}" = "TERM $D2;TERM $D1;TERM $R;" ]'
check "KILL pass hits D1 and R only -- D2, reparented during grace, is NOT signalled" '[ "${SIM_LOG##*TERM $R;}" = "KILL $D1;KILL $R;" ]'
check "D2 is reported not confirmed, left alone" 'grep -q "pid $D2 no longer descends from $R; not confirmed, left alone" "$OUT"'
check "accounting: left_alone=1 with D2 listed" '[ "$REAPER_LEFT_ALONE" = 1 ] && [ "$(printf "%s" "$REAPER_LEFT_ALONE_PIDS" | tr -d " ")" = "$D2" ] && [ "$(reaper_left_alone_json)" = "$D2" ]'
reaper_report 1 >"$OUT"   # reaper_report only reads state, so a file capture is enough
check "reaper_report prints the count, the pid and the CLEANUP UNCERTAIN line" 'grep -q "left_alone=1 \[pids $D2 \]" "$OUT" && grep -q "CLEANUP UNCERTAIN: 1 unconfirmed process(es) left alive: $D2" "$OUT"'

# --- case B: a root that is not the caller's direct child --------------------
SIM_PIDS=("$R"); SIM_PPIDS=(4242); SIM_LOG=""; SIM_TICKS=0
REAPER_LEFT_ALONE=0; REAPER_LEFT_ALONE_PIDS=""
reap_pid "$R" 2>"$OUT" >/dev/null
check "a root whose parent is not the caller: no signal at all" '[ -z "$SIM_LOG" ]'
check "it is reported not ours and counted" 'grep -q "pid $R is not a direct child of this shell; not ours, left alone" "$OUT" && [ "$REAPER_LEFT_ALONE" = 1 ] && [ "$(reaper_left_alone_json)" = "$R" ]'

# --- case C: nothing declined -> the zero wording -----------------------------
SIM_PIDS=("$R" "$D1"); SIM_PPIDS=("$SELF" "$R"); SIM_LOG=""; SIM_TICKS=0
REAPER_LEFT_ALONE=0; REAPER_LEFT_ALONE_PIDS=""
sim_sleep() { SIM_TICKS=$((SIM_TICKS + 1)); return 0; }   # no mutation this time
reap_pid "$R" >/dev/null 2>&1
reaper_report 0 >"$OUT"
check "a clean reap: TERM D1 R, KILL D1 R, zero left alone" '[ "$SIM_LOG" = "TERM $D1;TERM $R;KILL $D1;KILL $R;" ] && [ "$REAPER_LEFT_ALONE" = 0 ]'
check "zero is worded as observation, not absence: 'no observed unconfirmed processes'" 'grep -q "left_alone=0 (no observed unconfirmed processes; the snapshot knows only what was in the lineage while the root lived)" "$OUT" && ! grep -q "UNCERTAIN" "$OUT"'

COMPLETED=1
if [ "$bad" -ne 0 ]; then echo "SIMULATION reaper unit FAILED"; exit 1; fi
echo "SIMULATION reaper unit ok"
exit 0
