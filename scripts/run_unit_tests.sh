#!/usr/bin/env bash
# Run each test executable given on the command line, from the repo root, and
# print the one summary line the makefile's RUN_TESTS macro printed. Exit
# non-zero when any failed. Called by `zig build unit` (and `zig build test`).
set -u
cd "$(dirname "$0")/.."
PASS=0; FAIL=0
for t in "$@"; do
    if "$t"; then PASS=$((PASS + 1)); else FAIL=$((FAIL + 1)); fi
done
echo "========================================"
echo "Results: $PASS/$((PASS + FAIL)) passed, $FAIL failed"
echo "========================================"
[ "$FAIL" -eq 0 ]
