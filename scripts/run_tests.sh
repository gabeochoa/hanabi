#!/usr/bin/env bash
# scripts/run_tests.sh — the one-command hanabi test harness.
#
# Builds the app + all test binaries, runs:
#   1. unit tests      (make target: test_api)
#   2. headless e2e     (state model / glyphs / smart views / tabs / backend)
#   3. perf micro-bench (in-process thread-switch latency)
#   4. launch/RSS gate  (scripts/measure_launch.sh: FirstFrame < 250ms, RSS < 250MB)
# and prints a single PASS/FAIL summary with the measured perf numbers.
# Exits non-zero on ANY failure.
#
# The app is only ever run via measure_launch.sh, which backgrounds it with a
# hard timeout and a scoped kill of its own binary. This script ALSO kills on
# entry + exit so no stray render of THIS worktree is left behind. The kill is
# matched against this worktree's exe path, not the name: several checkouts
# test on one machine at a time, and a bare `pkill -f hanabi.exe` kills the
# other worktrees' runs.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

cleanup() { pkill -9 -f "^$ROOT/output/hanabi.exe" >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup  # kill any pre-existing stray from a prior aborted run

FAIL=0
step() { printf '\n\033[1m>>> %s\033[0m\n' "$1"; }

# --- Build (app + all test exes) ---
step "Build (make -j4 + test binaries)"
if ! make -j4 >/tmp/hb_build.log 2>&1; then
    echo "BUILD FAILED:"; tail -30 /tmp/hb_build.log; exit 1
fi
# Build the three test binaries without running them yet.
if ! make output/tests/test_api output/tests/test_e2e output/tests/test_perf \
        >/tmp/hb_build_tests.log 2>&1; then
    echo "TEST BUILD FAILED:"; tail -30 /tmp/hb_build_tests.log; exit 1
fi
echo "  build OK"

# --- Unit tests ---
step "Unit tests (test_api)"
if ./output/tests/test_api; then echo "  unit: PASS"; else echo "  unit: FAIL"; FAIL=1; fi

# --- Headless e2e ---
step "Headless e2e (test_e2e)"
if ./output/tests/test_e2e; then echo "  e2e: PASS"; else echo "  e2e: FAIL"; FAIL=1; fi

# --- Perf micro-benchmark ---
step "Perf micro-benchmark (test_perf: thread-switch latency)"
PERF_OUT="$(./output/tests/test_perf; echo "RC=$?")"
echo "$PERF_OUT" | sed '/^RC=/d'
if echo "$PERF_OUT" | grep -q '^RC=0$'; then echo "  perf-bench: PASS"; else echo "  perf-bench: FAIL"; FAIL=1; fi
SWITCH_MS="$(echo "$PERF_OUT" | grep -Eo 'per-switch \(avg\): [0-9.]+ ms' | grep -Eo '[0-9.]+' | head -1)"

# --- Launch / RSS gate ---
step "Launch + RSS gate (scripts/measure_launch.sh)"
LAUNCH_OUT="$(bash scripts/measure_launch.sh; echo "RC=$?")"
echo "$LAUNCH_OUT" | sed '/^RC=/d'
if echo "$LAUNCH_OUT" | grep -q '^RC=0$'; then echo "  launch-gate: PASS"; else echo "  launch-gate: FAIL"; FAIL=1; fi
STARTUP_MS="$(echo "$LAUNCH_OUT" | grep -Eo 'Startup: +[0-9]+ ms' | grep -Eo '[0-9]+' | head -1)"
FIRSTFRAME_MS="$(echo "$LAUNCH_OUT" | grep -Eo 'FirstFrame: +[0-9]+ ms' | grep -Eo '[0-9]+' | head -1)"
RSS_MB="$(echo "$LAUNCH_OUT" | grep -Eo 'Peak RSS: +[0-9]+ MB' | grep -Eo '[0-9]+' | head -1)"

# --- Source checks ---
# Cheap, and they catch a class of defect the pixel tests cannot: a silent
# no-op renders plausibly and asserts nothing.
step "Source checks"
for chk in scripts/check_label_padding.py; do
    OUT="$(/usr/bin/python3 "$chk"; echo "RC=$?")"
    echo "$OUT" | sed '/^RC=/d' | sed 's/^/  /'
    if echo "$OUT" | grep -q '^RC=0$'; then
        echo "  $(basename "$chk"): PASS"
    else
        echo "  $(basename "$chk"): FAIL"; FAIL=1
    fi
done

# The parity harness's own hermetic self-test, for the same reason: it has
# three deliberate-breakage cases and nothing was running them on a schedule.
OUT="$(/usr/bin/python3 scripts/compare.py --selftest; echo "RC=$?")"
echo "$OUT" | sed '/^RC=/d' | sed 's/^/  /'
if echo "$OUT" | grep -q '^RC=0$'; then echo "  compare.py --selftest: PASS"
else echo "  compare.py --selftest: FAIL"; FAIL=1; fi

# --- Summary ---
printf '\n\033[1m======== SUMMARY ========\033[0m\n'
echo "  Startup:          ${STARTUP_MS:-?} ms   (budget < 250 ms)"
echo "  FirstFrame:       ${FIRSTFRAME_MS:-?} ms   (budget < 250 ms)"
echo "  Peak RSS:         ${RSS_MB:-?} MB   (budget < 250 MB)"
echo "  Thread-switch:    ${SWITCH_MS:-?} ms/switch (current uncached path)"
echo "  Pending: transcript LRU cache + sub-ms cached-switch assertion (Phase X)"
if [ "$FAIL" -eq 0 ]; then
    printf '\033[1;32m  ALL CHECKS PASSED\033[0m\n'
    exit 0
fi
printf '\033[1;31m  SOME CHECKS FAILED\033[0m\n'
exit 1
