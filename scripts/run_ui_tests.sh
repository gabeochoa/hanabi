#!/usr/bin/env bash
# ===========================================================================
# scripts/run_ui_tests.sh  --  run every scripted UI test (tests/ui/*.e2e)
#
# Each script drives the REAL app through output/hanabi_uitest.exe: synthetic
# mouse and keyboard into the actual widget tree, assertions against the text
# that actually rendered. One process per script so a hang or a crash is
# attributed to the script that caused it and cannot poison the next one.
#
# ISOLATION: every script gets its OWN home directory, its own cache dir and
# its own token file, all inside one temp root that is removed at the end.
# The user's real settings.json is never read or written, the mock backend is
# forced, and no script can see a file another script wrote. One process per
# script, one directory per script: order cannot change a verdict.
# HANABI_UI_KEEP_HOMES=1 keeps the directories for inspection
# (scripts/harness_gate.sh reads them back).
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

# ---------------------------------------------------------------------------
# THE FIXTURE'S CLOCK, PINNED.
#
# The mock seeds every message time relative to std::time(nullptr)
# (api/mock_client.h:59-67), so the transcript's LAYOUT depends on the wall
# clock: whether two adjacent messages fall on the same local date decides
# whether a 26px date divider sits between them, and everything below it moves
# by 26px when that flips. Any script that addresses the transcript by
# coordinate is therefore red for part of every day, on a tree nobody touched.
#
# Measured, same tree, same fixture, 67 minutes apart: at 08:01 a "Today"
# divider was above the assistant turn in t2 and at 09:08 it was not. Three
# different people re-measured the same two selection scripts in one day,
# to 218/234/250, then 244/260/276, then back, and each was correct when it
# was taken.
#
# scripts/screens.sh already solved this for the screenshot path and says why
# in full ("rotted by the clock ... 28 of 30 baselines could fail on a tree
# that had not touched rendering"). Same instant, same zone, for the same
# reason: a fixed point in the PAST, so no message is ever "today" and no date
# boundary ever falls between two of them.
#
# A script that is ABOUT time overrides it in its own `# env:` line, which
# wins because the per-script assignments are applied after these.
PIN_NOW="${HANABI_UI_MOCK_NOW:-1781524800}"   # 2026-06-15 12:00:00Z
PIN_TZ="${HANABI_UI_TZ:-UTC}"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make uitest'." >&2
    exit 2
fi

# One temp ROOT for the whole run, one SUBDIRECTORY per script inside it. The
# per-script dir is what makes a script hermetic; the shared root is what
# keeps it cheap (a mkdir each, not a fresh anything -- the suite's runtime is
# unchanged to the second).
#
# It used to be one home for all 105 scripts. Nothing leaked through it today,
# because the mock backend disables the disk cache (loader_system.h:
# disk_cache_enabled) and the settings file is rewritten whole before every
# script -- but "nothing leaks" was a property of which backend the suite
# happens to run, not of the harness. A script with `# env: HANABI_BACKEND=http`
# would have written a session list and every transcript it opened into the
# next 104 scripts' home, and the failure that produced would have looked like
# a flake.
SUITE_TMP="$(mktemp -d /tmp/hanabi_uitest_home.XXXXXX)"
KEEP_HOMES="${HANABI_UI_KEEP_HOMES:-0}"
cleanup() {
    # scoped to THIS worktree's binary: other checkouts run their suites on the
    # same machine and a bare `pkill -f hanabi_uitest.exe` kills theirs
    pkill -9 -f "^$EXE" >/dev/null 2>&1
    if [ "$KEEP_HOMES" = "1" ]; then
        echo "kept per-test homes under $SUITE_TMP"
    else
        rm -rf "$SUITE_TMP"
    fi
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

# ORDER IS NOT A PRECONDITION. A suite that only passes in one order is one
# edit away from lying about it, so HANABI_UI_SEED=<n> runs the same scripts
# in a shuffled order and the seed is printed with the result -- a failure is
# reproducible by re-running with the seed it names. `make uitest-shuffle`
# picks a seed for you.
SEED="${HANABI_UI_SEED:-}"
if [ -n "$SEED" ]; then
    ORDERED=()
    while IFS= read -r line; do
        ORDERED+=("${line#* }")
    done < <(
        i=0
        for s in "${SCRIPTS[@]}"; do
            # awk rather than $RANDOM: bash 3.2 cannot seed $RANDOM, and a
            # shuffle nobody can reproduce is worse than no shuffle at all.
            printf '%s %s\n' \
                "$(awk -v s="$SEED" -v i="$i" 'BEGIN{srand(s+i);printf "%.9f", rand()}')" \
                "$s"
            i=$((i+1))
        done | sort
    )
    SCRIPTS=("${ORDERED[@]}")
fi

echo "=== scripted UI tests ($DIR) ==="
[ -n "$SEED" ] && echo "=== shuffled order, seed $SEED ==="
for s in "${SCRIPTS[@]}"; do
    name="$(basename "$s" .e2e)"
    log="/tmp/hanabi_uitest_${name}.log"

    # THIS SCRIPT'S OWN HOME. Everything the app can persist -- the settings
    # file, the disk cache, the token store -- is addressed relative to HOME
    # or to an explicit env override, so pointing all three inside a
    # per-script directory is the whole of the isolation. A script cannot
    # read what another one wrote, and it cannot be made to pass by what ran
    # before it.
    ISO_HOME="$SUITE_TMP/$name"
    mkdir -p "$ISO_HOME/Library/Application Support/hanabi"

    # Per-script settings: a leading "# settings: {...}" line lets a script say
    # which tabs/theme it wants to start from. Default = Home, no tabs, dark.
    cfg="$(sed -nE 's/^# settings:[[:space:]]*//p' "$s" | head -1)"
    [ -n "$cfg" ] || cfg='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}'
    printf '%s\n' "$cfg" > "$ISO_HOME/Library/Application Support/hanabi/settings.json"

    # A leading "# env: KEY=VAL KEY='two words'" line adds environment for this
    # script. Needed for any state a click cannot reach — an overlay whose only
    # binding is a Cmd chord, for instance, which the injector cannot produce
    # (afterhours_gaps.md #49). Values may be single-quoted to hold spaces;
    # parsed with `xargs` rather than word-splitting so they survive.
    env_line="$(sed -nE 's/^# env:[[:space:]]*//p' "$s" | head -1)"
    extra_env=()
    if [ -n "$env_line" ]; then
        while IFS= read -r kv; do
            [ -n "$kv" ] && extra_env+=("$kv")
        done < <(printf '%s' "$env_line" | xargs -n1 2>/dev/null)
    fi

    # A FRESH on-disk cache per script, INSIDE this script's own home. The
    # cache is where an unconfirmed local-first OUTBOX entry lives, and the
    # next launch restores and retries it by design -- so with one cache dir
    # for the suite it arrived in the NEXT script as a bubble that script
    # never sent. That is the one leak this harness was measured to have, and
    # it is why the boundary is drawn around everything durable rather than
    # around the cache alone.
    script_cache="$ISO_HOME/cache/$name"
    rm -rf "$script_cache"
    mkdir -p "$script_cache"

    # ${arr[@]+"${arr[@]}"} rather than "${arr[@]}": macOS ships bash 3.2, where
    # expanding an EMPTY array under `set -u` is an unbound-variable error. Bash
    # 4.4 fixed that, so the plain form works for anyone on a newer bash and
    # fails every script without an "# env:" line on a stock Mac.
    ( env HOME="$ISO_HOME" HANABI_CONFIG="$ISO_HOME/no-such-config.json" \
        HANABI_CACHE_DIR="$script_cache" TZ="$PIN_TZ" \
        HANABI_MOCK_NOW="$PIN_NOW" \
        HANABI_TOKEN_FILE="$ISO_HOME/token.json" HANABI_BACKEND=mock \
        ${extra_env[@]+"${extra_env[@]}"} "$EXE" --e2e "$s" >"$log" 2>&1 ) &
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
    [ -n "$SEED" ] && echo "  reproduce this order with HANABI_UI_SEED=$SEED" >&2
    exit 1
fi
exit 0
