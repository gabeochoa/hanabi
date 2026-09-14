#!/usr/bin/env bash
# ===========================================================================
# scripts/lib/reaper.sh -- the runner's process reaper: decision + accounting
#
# Sourced by scripts/run_ui_tests.sh (which binds the REAL providers) and by
# tests/runner_policy/reaper_unit.sh (which binds SIMULATED ones). The
# providers are FUNCTION NAMES passed to `reaper_bind` by the caller -- never
# read from the environment, never selected by a variable -- so nothing a
# runtime env var or a fixture `# env:` line could set can change whom the
# shipped runner signals, and synthetic identity can never be combined with
# real signalling: the runner never names a sim_* provider and the unit never
# names a real_* one (both asserted by grep in scripts/run_ui_policy_tests.sh).
#
# WHAT MAKES REAPING SAFE, exactly as the code below proves it:
#
#   A ROOT handed to reap_pid is expected to be an UN-WAITED DIRECT CHILD of
#   the caller: `( exec env … ) &` makes it one, and the caller forgets it
#   only after `wait`. The kernel keeps an un-waited child's pid reserved (a
#   zombie at worst) until its parent waits, so a root's number cannot be
#   reused by anyone while the caller still holds it. The root predicate is
#   therefore `ppid == <caller pid>` -- REQUIRED, fail closed; the command
#   name is a logged sanity check and never sufficient (another checkout's or
#   another shell's binary has the same basename).
#
#   A DESCENDANT is a child of the binary, not of the caller: it can exit and
#   be reaped by the binary (or by init once reparented) at any moment,
#   freeing its number for a stranger, and liveness cannot tell. So a
#   descendant is signalled only while its parentage, RE-READ before that
#   very signal, still leads to the root through the snapshot; otherwise it
#   is left alone, counted and listed. Descendants are signalled deepest-first
#   while the root is still alive (the snapshot lists subtree before parent),
#   the root last.
#
#   What the reaper declines to touch is never hidden: REAPER_LEFT_ALONE and
#   REAPER_LEFT_ALONE_PIDS are the run-wide account, reaper_report prints it,
#   and a zero means "no observed unconfirmed processes" -- the snapshot knows
#   only what was in the lineage while the root lived; a process that left
#   the lineage BEFORE the snapshot is invisible to it, by design.
# ===========================================================================

# Providers, bound once by the caller. The reading providers (ppid,
# children, comm) are invoked inside `$(…)` -- a subshell -- so they must
# never mutate shared state; the acting providers (signal, sleep, alive) are
# invoked as plain statements in this shell, so a simulation may mutate its
# table from them. Signatures:
#   <ppid_fn>     <pid>        -> prints the parent pid (empty if gone)
#   <alive_fn>    <pid>        -> 0 iff the pid exists
#   <children_fn> <pid>        -> prints the pids whose parent is <pid>
#   <signal_fn>   <SIG> <pid>  -> sends SIG (TERM|KILL) to pid
#   <sleep_fn>                 -> one grace tick (the real one sleeps 0.1 s)
#   <comm_fn>     <pid>        -> prints the process's command basename (a
#                                 logged sanity check only, never a decision)
REAPER_PPID_FN=""
REAPER_ALIVE_FN=""
REAPER_CHILDREN_FN=""
REAPER_SIGNAL_FN=""
REAPER_SLEEP_FN=""
REAPER_COMM_FN=""
REAPER_SELF=""          # the caller's pid: what a root's parent must be
REAPER_EXE_BASE=""      # the binary's basename, for the logged sanity check
REAPER_LEFT_ALONE=0
REAPER_LEFT_ALONE_PIDS=""
REAPER_GRACE_TICKS=20

reaper_bind() {  # reaper_bind <ppid_fn> <alive_fn> <children_fn> <signal_fn> <sleep_fn> <comm_fn> <self_pid> <exe_base>
    REAPER_PPID_FN="$1"; REAPER_ALIVE_FN="$2"; REAPER_CHILDREN_FN="$3"
    REAPER_SIGNAL_FN="$4"; REAPER_SLEEP_FN="$5"; REAPER_COMM_FN="$6"
    REAPER_SELF="$7"; REAPER_EXE_BASE="$8"
}

reaper_left_alone() { REAPER_LEFT_ALONE=$((REAPER_LEFT_ALONE + 1)); REAPER_LEFT_ALONE_PIDS="$REAPER_LEFT_ALONE_PIDS $1"; }

reaper_confirm_root() {  # 0 iff <pid> is still a direct child of the caller
    [ "$("$REAPER_PPID_FN" "$1")" = "$REAPER_SELF" ] || return 1
    local comm
    comm="$("$REAPER_COMM_FN" "$1")"
    case "$comm" in
        ""|"$REAPER_EXE_BASE"|env) ;;
        *) echo "  reaper: pid $1 is our child but its command is '$comm' (expected $REAPER_EXE_BASE); reaping it anyway as our own" >&2 ;;
    esac
    return 0
}

reaper_descendants_of() {  # every descendant of <pid>, deepest first
    local c
    for c in $("$REAPER_CHILDREN_FN" "$1"); do
        reaper_descendants_of "$c"
        printf '%s\n' "$c"
    done
}

reaper_in_set() {  # reaper_in_set <needle> <words...>
    local n="$1" w; shift
    for w in "$@"; do [ "$w" = "$n" ] && return 0; done
    return 1
}

reaper_confirm_descendant() {  # <pid> <root> <snapshot...>: parentage still leads to the root
    local d="$1" root="$2"; shift 2
    local pp
    pp="$("$REAPER_PPID_FN" "$d")"
    [ -n "$pp" ] || return 1
    [ "$pp" = "$root" ] && return 0
    reaper_in_set "$pp" "$@"
}

reaper_signal_lineage() {  # <SIG> <root> <snapshot...>: confirmed descendants (deepest first), then the root
    local sig="$1" root="$2"; shift 2
    local d
    for d in "$@"; do
        "$REAPER_ALIVE_FN" "$d" || continue
        if reaper_confirm_descendant "$d" "$root" "$@"; then
            "$REAPER_SIGNAL_FN" "$sig" "$d"
        else
            echo "  reaper: pid $d no longer descends from $root; not confirmed, left alone" >&2
            reaper_left_alone "$d"
        fi
    done
    "$REAPER_ALIVE_FN" "$root" && "$REAPER_SIGNAL_FN" "$sig" "$root"
    return 0
}

reap_pid() {  # reap_pid <root>: confirm root; snapshot lineage; TERM (deepest first, root last); grace; KILL survivors
    local root="$1" d i set=()
    "$REAPER_ALIVE_FN" "$root" || return 0
    if ! reaper_confirm_root "$root"; then
        echo "  reaper: pid $root is not a direct child of this shell; not ours, left alone" >&2
        reaper_left_alone "$root"
        return 0
    fi
    # Lineage is snapshotted WHILE the root is alive: once it dies its
    # children are reparented and cannot be found by parent pid.
    for d in $(reaper_descendants_of "$root"); do set+=("$d"); done
    reaper_signal_lineage TERM "$root" ${set[@]+"${set[@]}"}
    for ((i = 0; i < REAPER_GRACE_TICKS; i++)); do
        local alive=0
        for d in ${set[@]+"${set[@]}"} "$root"; do "$REAPER_ALIVE_FN" "$d" && alive=1; done
        [ "$alive" = 0 ] && break
        "$REAPER_SLEEP_FN"
    done
    reaper_signal_lineage KILL "$root" ${set[@]+"${set[@]}"}
    return 0
}

reaper_report() {  # reaper_report <reaped_at_exit>: the summary lines
    if [ "$REAPER_LEFT_ALONE" -eq 0 ]; then
        echo "  reaper: reaped_at_exit=$1 left_alone=0 (no observed unconfirmed processes; the snapshot knows only what was in the lineage while the root lived)"
    else
        echo "  reaper: reaped_at_exit=$1 left_alone=$REAPER_LEFT_ALONE [pids$REAPER_LEFT_ALONE_PIDS ]"
        echo "  CLEANUP UNCERTAIN: $REAPER_LEFT_ALONE unconfirmed process(es) left alive:$REAPER_LEFT_ALONE_PIDS"
    fi
}

reaper_left_alone_json() {  # the pids as a JSON array body
    local out="" p
    for p in $REAPER_LEFT_ALONE_PIDS; do out="$out${out:+,}$p"; done
    printf '%s' "$out"
}
