#!/usr/bin/env bash
# scripts/watchdog.sh — bound a child's run time without wedging the script.
#
# WHY THIS IS ITS OWN FILE. Two gates needed a per-run timeout and both wrote
# the same four lines:
#
#     ( sleep "$RUN_TIMEOUT"; kill -9 "$APP_PID"; kill_own_runs ) &
#     WATCH_PID=$!
#     wait "$APP_PID"
#     kill "$WATCH_PID"
#
# It reads as obviously correct and it costs the whole timeout, every run, in
# any pipeline. Two independent things are wrong with it:
#
#   1. `kill "$WATCH_PID"` kills the SUBSHELL, not the `sleep` it is blocked
#      in. The sleep is reparented to init and keeps running for the full
#      RUN_TIMEOUT after the script has exited.
#   2. That orphan inherited the script's stdout. A pipe stays open until
#      EVERY writer closes it, so `make`, `tee`, `| cat`, and every CI log
#      capture block on an orphan that is doing nothing but sleeping.
#
# Measured on gabeochoa-mac-GRQ7Y259H4, 2026-08-25, both gates on a clean
# tree, `>file` against `2>&1 | cat`:
#
#     scripts/soak_gate.sh        4 s  ->  120 s     (RUN_TIMEOUT=120)
#     scripts/measure_launch.sh   0 s  ->   16 s     (RUN_TIMEOUT=15)
#
# The stall is exactly the timeout, to the second, on every run — it is not
# load, it is not the app, and it does not vary. `make test` runs both, so a
# piped suite was paying 135 seconds of pure sleep, and docs/perf/GATES.md's
# "~4 s" for the soak gate was true only when someone watched it on a tty.
#
# This version fixes both halves independently, because either alone would
# have fixed the symptom and left the other trap armed for the next script:
#
#   * it POLLS, so it returns within a second of the child exiting and there
#     is no orphan to leave behind;
#   * it is started with its own stdout and stderr on /dev/null, so even if it
#     were orphaned it would hold no descriptor of the caller's.
#
# scripts/check_watchdogs.py enforces the second one on every script in this
# directory, because the pattern is easy to retype and its cost is invisible
# on a terminal.
#
# Usage — source it, then:
#
#     "$EXE" args... &
#     APP_PID=$!
#     watchdog_start "$APP_PID" 120 my_cleanup_fn
#     wait "$APP_PID"; rc=$?
#     watchdog_stop
#
# `watchdog_start`'s third argument is optional and names a function to run
# after the kill, for a caller that has more to tear down than the one pid.

# Poll `pid` for at most `secs` seconds; kill it and run `on_kill` if it is
# still alive at the end. Returns as soon as the pid is gone.
_watchdog_wait() {
    local pid="$1" secs="$2" on_kill="${3:-}" i=0
    while [ "$i" -lt "$secs" ]; do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 1
        i=$((i + 1))
    done
    kill -9 "$pid" 2>/dev/null || true
    if [ -n "$on_kill" ]; then "$on_kill" || true; fi
}

# Start a watchdog over `pid`. Sets WATCHDOG_PID.
watchdog_start() {
    local pid="$1" secs="$2" on_kill="${3:-}"
    # The redirect is the load-bearing part and must be on the JOB, not inside
    # it: a descriptor inherited before the job redirects is already held.
    _watchdog_wait "$pid" "$secs" "$on_kill" >/dev/null 2>&1 &
    WATCHDOG_PID=$!
}

# Stop the watchdog started by the last watchdog_start.
watchdog_stop() {
    [ -n "${WATCHDOG_PID:-}" ] || return 0
    kill "$WATCHDOG_PID" >/dev/null 2>&1 || true
    wait "$WATCHDOG_PID" 2>/dev/null || true
    WATCHDOG_PID=""
}
