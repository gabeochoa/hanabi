#!/usr/bin/env bash
# ===========================================================================
# scripts/run_ui_tests_alone.sh  --  every scripted test ALONE
#
# The census behind the claim that no test passes for the wrong reason: each
# .e2e is the ONLY script in its own suite invocation, so nothing has run
# before it in that process, in that home directory, or on that machine's
# clipboard. Any test whose verdict differs from `make uitest` passes (or
# fails) because of what ran before it, and this prints exactly that
# difference.
#
# `make uitest-shuffle` is the cheap standing defence; this is the audit.
#
# EXIT: non-zero if any script failed alone.
# ===========================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

DIR="${HANABI_UI_TESTS:-$ROOT/tests/ui}"
ONE="$(mktemp -d /tmp/hanabi_uitest_alone.XXXXXX)"
trap 'rm -rf "$ONE"' EXIT

shopt -s nullglob
SCRIPTS=("$DIR"/*.e2e)
if [ ${#SCRIPTS[@]} -eq 0 ]; then
    echo "no .e2e scripts in $DIR"
    exit 0
fi

PASS=0
FAIL=0
FAILED_NAMES=""

echo "=== scripted UI tests, one per suite run ($DIR) ==="
for s in "${SCRIPTS[@]}"; do
    name="$(basename "$s" .e2e)"
    rm -rf "$ONE/only"
    mkdir -p "$ONE/only"
    ln -s "$s" "$ONE/only/$name.e2e"
    if HANABI_UI_TESTS="$ONE/only" bash "$ROOT/scripts/run_ui_tests.sh" \
        >"$ONE/out" 2>&1; then
        printf '  %-34s PASS\n' "$name"
        PASS=$((PASS+1))
    else
        printf '  %-34s FAIL  /tmp/hanabi_uitest_%s.log\n' "$name" "$name"
        FAIL=$((FAIL+1))
        FAILED_NAMES="$FAILED_NAMES $name"
    fi
done

echo "----------------------------------------"
echo "  $PASS passed alone, $FAIL failed alone"
if [ "$FAIL" -ne 0 ]; then
    echo "  failed alone:$FAILED_NAMES" >&2
    exit 1
fi
exit 0
