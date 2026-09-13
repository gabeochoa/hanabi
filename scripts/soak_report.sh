#!/usr/bin/env bash
# The makefile's `soak-report` (default) and `soak-baseline` (--baseline).
# Knobs through the environment: HANABI_SOAK_ARMS HANABI_SOAK_JOBS.
set -u
cd "$(dirname "$0")/.."
BASELINE=docs/perf/soak-baseline.txt
mkdir -p output
if [ "${1:-}" = "--baseline" ]; then
    HANABI_SOAK_REPORT_OUT="$BASELINE" bash scripts/soak.sh > output/soak-baseline.log 2>&1 || true
    echo "  wrote $BASELINE ($(wc -l < "$BASELINE" | tr -d ' ') lines)"
    exit 0
fi
HANABI_SOAK_REPORT_OUT=output/soak-report.txt bash scripts/soak.sh > output/soak-report.log 2>&1
echo "  full log: output/soak-report.log"
if [ ! -s output/soak-report.txt ]; then
    echo "  ! the run produced no report at all, so there is nothing to"
    echo "    compare. That is a broken run, not a clean one."; exit 2
fi
if diff -u "$BASELINE" output/soak-report.txt; then
    echo "  soak report matches $BASELINE"
else
    echo ""
    echo "  The soak report moved. Every line above is a property that"
    echo "  changed, not a number that drifted -- the measured columns are"
    echo "  budget BANDS and do not move unless a budget was crossed."
    echo "  If the change is intended, 'zig build soak-baseline' and commit it"
    echo "  with a message saying what moved and why."; exit 1
fi
