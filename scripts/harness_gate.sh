#!/usr/bin/env bash
# ===========================================================================
# scripts/harness_gate.sh  --  the scripted-UI harness, tested
#
# tests/ui tests the app. Nothing tested the harness, and the harness is what
# three investigations this session were actually arguing with: a suite whose
# tests can see each other's state, and scripts that could not say what state
# they needed. Two properties, one gate:
#
#   1. EVERY SCRIPT GETS ITS OWN HOME. Two fixture scripts declare different
#      start states; both directories must survive the run holding their own
#      declaration. One home for the whole suite fails this: there is one
#      directory and it holds whichever script ran last.
#
#   2. A SCRIPT SAYS WHAT IT NEEDS, AND THE HARNESS ENFORCES IT. A script that
#      requires a thread nobody opened must fail SAYING SO, not run on and
#      fail an assertion about text.
#
# EXIT: non-zero on any failure.
# ===========================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

EXE="$ROOT/output/hanabi_uitest.exe"
if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make uitest-build'." >&2
    exit 2
fi

FAIL=0
note_fail() {
    echo "  FAIL: $1"
    FAIL=1
}

echo "=== harness gate ==="

# --- 1. per-script homes ---------------------------------------------------
FIX="$ROOT/tests/harness/state_pair"
rm -rf "$FIX"
mkdir -p "$FIX"
ln -s "$ROOT/tests/harness/state_a.e2e" "$FIX/state_a.e2e"
ln -s "$ROOT/tests/harness/state_b.e2e" "$FIX/state_b.e2e"

out="$(HANABI_UI_TESTS="$FIX" HANABI_UI_KEEP_HOMES=1 \
    bash "$ROOT/scripts/run_ui_tests.sh" 2>&1)"
rc=$?
rm -rf "$FIX"
root="$(printf '%s\n' "$out" | sed -nE 's/^kept per-test homes under (.*)$/\1/p' | head -1)"

if [ "$rc" -ne 0 ]; then
    note_fail "the two fixture scripts did not pass:"
    printf '%s\n' "$out" | sed 's/^/      /'
elif [ -z "$root" ] || [ ! -d "$root" ]; then
    note_fail "the harness kept no per-test homes (HANABI_UI_KEEP_HOMES ignored)"
else
    a="$root/state_a/Library/Application Support/hanabi/settings.json"
    b="$root/state_b/Library/Application Support/hanabi/settings.json"
    if [ ! -f "$a" ] || [ ! -f "$b" ]; then
        note_fail "each script must get its own home; found: $(find "$root" -name settings.json | wc -l | tr -d ' ') settings file(s) under $root"
    elif ! grep -q '"theme":"dark"' "$a"; then
        note_fail "state_a's home does not hold state_a's declared settings: $(cat "$a")"
    elif ! grep -q '"theme":"light"' "$b"; then
        note_fail "state_b's home does not hold state_b's declared settings: $(cat "$b")"
    else
        echo "  per-script homes                   PASS"
    fi
fi
[ -n "$root" ] && rm -rf "$root"

# --- 2. an unmet precondition is named -------------------------------------
HOME_DIR="$(mktemp -d /tmp/hanabi_harness_gate.XXXXXX)"
mkdir -p "$HOME_DIR/Library/Application Support/hanabi"
printf '%s\n' '{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}' \
    > "$HOME_DIR/Library/Application Support/hanabi/settings.json"
log="/tmp/hanabi_harness_gate_precondition.log"
( env HOME="$HOME_DIR" HANABI_CONFIG="$HOME_DIR/no-such-config.json" \
    HANABI_CACHE_DIR="$HOME_DIR/cache" HANABI_BACKEND=mock \
    "$EXE" --e2e "$ROOT/tests/harness/precondition_not_met.e2e" \
    >"$log" 2>&1 )
prc=$?
rm -rf "$HOME_DIR"

if [ "$prc" -eq 0 ]; then
    note_fail "a script requiring a thread nobody opened PASSED (rc=0)"
elif ! grep -q "precondition not met" "$log"; then
    note_fail "the failure does not name the precondition; it said:"
    sed -n '/ERROR\|TIMEOUT\|FAIL/p' "$log" | head -4 | sed 's/^/      /'
else
    echo "  unmet precondition is named        PASS"
fi

echo "----------------------------------------"
if [ "$FAIL" -ne 0 ]; then
    echo "  harness gate FAILED" >&2
    exit 1
fi
echo "  harness gate passed"
exit 0
