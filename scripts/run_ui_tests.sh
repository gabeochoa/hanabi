#!/usr/bin/env bash
# ===========================================================================
# scripts/run_ui_tests.sh  --  run every scripted UI test (tests/ui/*.e2e)
#
# Each script drives the REAL app through output/hanabi_uitest.exe: synthetic
# mouse and keyboard into the actual widget tree, assertions against the text
# that actually rendered. One process per script so a hang or a crash is
# attributed to the script that caused it and cannot poison the next one.
#
# ISOLATION: same contract as scripts/screens.sh — an isolated HOME so the
# user's real settings.json is never read or written, the mock backend forced,
# and the runtime backend config pointed at a path that does not exist.
#
# EXIT: non-zero if any script failed or timed out.
# ===========================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

EXE="$ROOT/output/hanabi_uitest.exe"
DIR="${HANABI_UI_TESTS:-$ROOT/tests/ui}"
TIMEOUT="${HANABI_UI_TIMEOUT:-60}"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make uitest'." >&2
    exit 2
fi

ISO_HOME="$(mktemp -d /tmp/hanabi_uitest_home.XXXXXX)"
mkdir -p "$ISO_HOME/Library/Application Support/hanabi"
cleanup() {
    pkill -9 -f hanabi_uitest.exe >/dev/null 2>&1
    rm -rf "$ISO_HOME"
}
trap cleanup EXIT

PASS=0
FAIL=0
FAILED_NAMES=""

shopt -s nullglob
SCRIPTS=("$DIR"/*.e2e)
if [ ${#SCRIPTS[@]} -eq 0 ]; then
    echo "no .e2e scripts in $DIR"
    exit 0
fi

echo "=== scripted UI tests ($DIR) ==="
for s in "${SCRIPTS[@]}"; do
    name="$(basename "$s" .e2e)"
    log="/tmp/hanabi_uitest_${name}.log"

    # Per-script settings: a leading "# settings: {...}" line lets a script say
    # which tabs/theme it wants to start from. Default = Home, no tabs, dark.
    cfg="$(sed -nE 's/^# settings:[[:space:]]*//p' "$s" | head -1)"
    [ -n "$cfg" ] || cfg='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}'
    printf '%s\n' "$cfg" > "$ISO_HOME/Library/Application Support/hanabi/settings.json"

    # A leading "# env: KEY=VAL KEY=VAL" line adds environment for this script.
    # Needed for any state a click cannot reach — an overlay whose only binding
    # is a Cmd chord, for instance, which the injector cannot produce
    # (afterhours_gaps.md #49).
    read -r -a extra_env <<<"$(sed -nE 's/^# env:[[:space:]]*//p' "$s" | head -1)"

    ( env HOME="$ISO_HOME" HANABI_CONFIG="/tmp/none_$$" HANABI_BACKEND=mock \
        "${extra_env[@]}" "$EXE" --e2e "$s" >"$log" 2>&1 ) &
    pid=$!
    for ((i=0; i<TIMEOUT; i++)); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null
        rc=124
    else
        wait "$pid"; rc=$?
    fi

    if [ "$rc" -eq 0 ]; then
        printf '  %-34s PASS\n' "$name"
        PASS=$((PASS+1))
    else
        printf '  %-34s FAIL (rc=%s)  %s\n' "$name" "$rc" "$log"
        sed -n '/E2E ERROR\|TIMEOUT\|FAIL/p' "$log" | head -8 | sed 's/^/      /'
        FAIL=$((FAIL+1))
        FAILED_NAMES="$FAILED_NAMES $name"
    fi
done

echo "----------------------------------------"
echo "  $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
    echo "  failed:$FAILED_NAMES" >&2
    exit 1
fi
exit 0
