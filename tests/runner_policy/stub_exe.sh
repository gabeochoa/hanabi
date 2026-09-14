#!/usr/bin/env bash
# The STUB executable the runner-policy tests point run_ui_tests.sh at
# (HANABI_UI_EXE). It opens nothing, reads nothing, and exits 0: it records
# ONE line per invocation to $HANABI_STUB_RECORD -- its own path (so a test
# can prove the real binary never ran), the arguments it was given, and the
# whitelisted launch variables as it received them -- and prints what the
# app would print for the marker the runner cross-checks, on request:
#   HANABI_STUB_PRINT_GFX=1   print "Gfx init:" (a WINDOWED app's marker), so
#                             a test can prove the runner flags a headless
#                             launch whose log says a window opened.
#   HANABI_STUB_EXIT=<n>      exit with n (a failing script).
#   HANABI_STUB_SLEEP=<s>     sleep s seconds before exiting (a hung script,
#                             for the timeout / ownership tests).
#   HANABI_STUB_GRANDCHILD=<file>  before sleeping, start a background
#                             `sleep` (a grandchild of the runner) and write
#                             its pid to <file>, so a test can prove the
#                             reaper takes the lineage, not just the pid.
#   HANABI_STUB_ORPHAN=<file> before sleeping, double-fork a `sleep` so it is
#                             reparented to init and write its pid, so a test
#                             can prove the reaper leaves an UNCONFIRMED
#                             lineage alone (fail closed).
# Never a real window, never the app: this file is a test fixture.
set -u
record="${HANABI_STUB_RECORD:?HANABI_STUB_RECORD must name the record file}"
printf 'exe=%s argv=%s WINDOWED=%s HEADLESS_ONLY=%s BACKEND=%s POLICY=%s\n' \
    "$0" "$*" "${HANABI_E2E_WINDOWED-<unset>}" "${HANABI_E2E_HEADLESS_ONLY-<unset>}" \
    "${HANABI_BACKEND-<unset>}" "${HANABI_UI_POLICY-<unset>}" >> "$record"
echo "[INFO] stub: E2E finished in 0.00 seconds"
if [ "${HANABI_STUB_PRINT_GFX:-0}" = "1" ]; then
    echo "  Gfx init: 1 ms (stub pretending a window opened)"
fi
if [ -n "${HANABI_STUB_GRANDCHILD:-}" ]; then
    sleep "${HANABI_STUB_SLEEP:-30}" &
    printf '%s\n' "$!" > "$HANABI_STUB_GRANDCHILD"
fi
if [ -n "${HANABI_STUB_ORPHAN:-}" ]; then
    # A double fork: the middle shell exits at once, so its sleeper is
    # reparented to init BEFORE the reaper looks -- a process whose parentage
    # no longer leads to the stub. The reaper must leave it alone.
    ( sleep "${HANABI_STUB_SLEEP:-30}" & printf '%s\n' "$!" > "$HANABI_STUB_ORPHAN" ) &
    wait $! 2>/dev/null
fi
if [ -n "${HANABI_STUB_SLEEP:-}" ]; then
    # `exec` so the sleeper IS this pid: what the runner recorded is what
    # must die, and nothing it did not record may.
    exec sleep "$HANABI_STUB_SLEEP"
fi
exit "${HANABI_STUB_EXIT:-0}"
