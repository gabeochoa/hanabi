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

# HANABI_UI_EXE points the runner at another executable. It exists for the
# runner's OWN policy tests (scripts/run_ui_policy_tests.sh), which drive this
# script against a stub that records what it was asked to launch and opens
# nothing; the manifest names the executable actually used, so a run against
# a stub cannot be mistaken for a run against the app.
EXE="${HANABI_UI_EXE:-$ROOT/output/hanabi_uitest.exe}"
DIR="${HANABI_UI_TESTS:-$ROOT/tests/ui}"

# The runner takes NO positional arguments: it runs every .e2e under $DIR.
# Naming scripts on the command line used to be silently ignored -- the whole
# suite ran instead, twice mistaken for a hang and killed mid-run. Select by
# copying the scripts you mean into a directory and pointing HANABI_UI_TESTS
# at it; a stray argument is refused here before anything launches.
if [ "$#" -gt 0 ]; then
    echo "run_ui_tests.sh: takes no script names ($# given: $*)." >&2
    echo "  select scripts with HANABI_UI_TESTS=<dir of .e2e files>" >&2
    exit 64
fi
TIMEOUT="${HANABI_UI_TIMEOUT:-60}"

# ---------------------------------------------------------------------------
# RUN-LEVEL LAUNCH POLICY -- decided ONCE, here, before any fixture is read,
# and never changed by a fixture.
#
#   HANABI_UI_POLICY=headless-only   (default) Every script runs against the
#       headless backend, in this process's own terms: a script that DECLARES
#       a windowed run (`# env: HANABI_E2E_WINDOWED=1`) is SKIPPED with that
#       reason -- never run headless in its place, never run windowed. The
#       app is launched with HANABI_E2E_HEADLESS_ONLY=1 so a windowed request
#       that somehow escaped this runner is refused inside the binary before
#       a window exists (main.cpp, the --e2e entry).
#   HANABI_UI_POLICY=all             Unrestricted: a script runs in the mode
#       it declares, INCLUDING windowed/native scripts, which open real
#       windows and post real input. Only under an explicit grant.
#
# Whatever the policy: the MODE of a script comes from the fixture's own
# `# env:` line and from nothing else. An inherited HANABI_E2E_WINDOWED in
# this shell would make every script windowed (or, stripped, would silently
# change what a script declared), so it is a configuration error: refused
# before any launch; so is an inherited HANABI_E2E_HEADLESS_ONLY, which is
# this runner's own variable to set. A fixture that sets a POLICY key
# (HANABI_UI_POLICY, HANABI_E2E_HEADLESS_ONLY, HANABI_UI_EXE, HANABI_UI_TESTS)
# is trying to move the boundary
# from inside it: refused, and the run fails. Two `# env:` lines, or the
# same key twice on one line, is ambiguous: refused likewise. Refusals are
# recorded in the manifest with their reason, and a run with any refusal
# exits non-zero even if every selected script passed.
POLICY="${HANABI_UI_POLICY:-headless-only}"
case "$POLICY" in
    headless-only|all) ;;
    *)
        echo "run_ui_tests.sh: HANABI_UI_POLICY='$POLICY' is not a policy (headless-only|all)" >&2
        exit 65
        ;;
esac
readonly POLICY
is_armed() { [ -n "${1:-}" ] && [ "$1" != "0" ]; }
if is_armed "${HANABI_E2E_WINDOWED:-}"; then
    echo "run_ui_tests.sh: refusing to run: HANABI_E2E_WINDOWED='$HANABI_E2E_WINDOWED' is set in this shell." >&2
    echo "  a script's mode is declared in its own '# env:' line and nowhere else; unset it and rerun." >&2
    exit 66
fi
if [ -n "${HANABI_E2E_HEADLESS_ONLY:-}" ]; then
    echo "run_ui_tests.sh: refusing to run: HANABI_E2E_HEADLESS_ONLY='$HANABI_E2E_HEADLESS_ONLY' is set in this shell." >&2
    echo "  that variable is this runner's to set, from HANABI_UI_POLICY; unset it and rerun." >&2
    exit 66
fi
POLICY_KEYS="HANABI_UI_POLICY HANABI_E2E_HEADLESS_ONLY HANABI_UI_EXE HANABI_UI_TESTS"

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
    echo "ERROR: $EXE not found. Build it with 'zig build uitest-build'." >&2
    exit 2
fi

# ---------------------------------------------------------------------------
# THE MANIFEST: one JSON record per script, written BEFORE its launch (the
# decision) and completed AFTER it (the result), so the file is the account
# of what this run selected, skipped, refused, launched and observed -- and
# the totals at the end are COUNTED FROM IT, never from a running tally.
# Fields are the whitelisted launch facts only: never HOME, never a token
# path, never the environment wholesale.
MANIFEST="${HANABI_UI_MANIFEST:-/tmp/hanabi_uitest_manifest.jsonl}"
: > "$MANIFEST"
sha256_of() {  # portable: macOS ships shasum, most Linux ships sha256sum
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" 2>/dev/null | cut -c1-64
    elif command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" 2>/dev/null | cut -c1-64
    else echo unknown; fi
}
EXE_SHA="$(sha256_of "$EXE")"
if SRC_HEAD="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null)"; then
    SRC_DIRTY="$(git -C "$ROOT" status --porcelain 2>/dev/null | grep -cv '^??' || true)"
else
    SRC_HEAD=unknown; SRC_DIRTY=null
fi
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$$"
json_str() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

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

# ---------------------------------------------------------------------------
# PROCESS OWNERSHIP: BY PID, NEVER BY NAME.
#
# This runner kills only processes it launched -- the pids it recorded at
# launch and their descendants -- and never anything matched by command
# line. The previous `pkill -9 -f "^$EXE"` on exit killed every process
# whose argv began with this worktree's binary path: a concurrent suite from
# the same checkout, a manual run, a copy held under a debugger; and it
# fired on every exit, a refusal before any launch included. Path match is
# not ownership.
#
# Each launch is `( exec env … "$EXE" … ) &`: the subshell becomes env,
# env becomes the binary, and `$!` IS the binary's pid. The decision and
# accounting live in scripts/lib/reaper.sh; the PROVIDERS -- how a parent
# pid, liveness, children, a signal and a grace tick are done -- are bound
# HERE, ONCE, UNCONDITIONALLY, to the real thing. No environment variable
# and no fixture line can rebind them (scripts/run_ui_policy_tests.sh
# asserts by grep that this file's one `reaper_bind` names only real_*).
. "$SCRIPT_DIR/lib/reaper.sh"
real_ppid() { ps -o ppid= -p "$1" 2>/dev/null | tr -d ' '; }
real_alive() { kill -0 "$1" 2>/dev/null; }
real_children() { pgrep -P "$1" 2>/dev/null; }
real_signal() { kill "-$1" "$2" 2>/dev/null; }
real_sleep() { sleep 0.1; }
real_comm() { ps -o comm= -p "$1" 2>/dev/null | sed 's#.*/##; s/^ *//; s/ *$//'; }
reaper_bind real_ppid real_alive real_children real_signal real_sleep real_comm "$$" "$(basename "$EXE")"

# ACTIVE owned pids only: a pid is added at launch and REMOVED once it has
# been waited for (normal exit or after a timeout reap), so the exit reaper
# sees only launches still in flight when the runner dies -- never a
# finished child's number, which the OS may already have reused.
ACTIVE_PIDS=()
forget_pid() {  # forget_pid <pid>: drop it from ACTIVE_PIDS
    local keep=() p
    for p in ${ACTIVE_PIDS[@]+"${ACTIVE_PIDS[@]}"}; do
        [ "$p" = "$1" ] || keep+=("$p")
    done
    ACTIVE_PIDS=(${keep[@]+"${keep[@]}"})
}
REAPED_AT_EXIT=0
cleanup() {
    # A pure reaper over launches still in flight; never the manifest,
    # never the exit status (the trap restores it).
    local p
    for p in ${ACTIVE_PIDS[@]+"${ACTIVE_PIDS[@]}"}; do
        if real_alive "$p"; then
            reap_pid "$p"
            REAPED_AT_EXIT=$((REAPED_AT_EXIT + 1))
            wait "$p" 2>/dev/null
        fi
    done
    # The cleanup's own evidence: a per-run record in the manifest and the
    # summary lines. An unconfirmed live process is never hidden and never
    # changes a script's verdict -- it is printed on its own line so a green
    # script tally cannot read as a green cleanup claim.
    # (The manifest exists only once the run got past its preflight; a
    # refusal before that reaped nothing and has no manifest to write to.)
    [ -n "${MANIFEST:-}" ] && [ -f "$MANIFEST" ] && \
        printf '{"run_id":"%s","record":"cleanup","active_at_exit":%s,"reaped_at_exit":%s,"left_alone":%s,"left_alone_pids":[%s]}\n' \
            "${RUN_ID:-}" "${#ACTIVE_PIDS[@]}" "$REAPED_AT_EXIT" "$REAPER_LEFT_ALONE" "$(reaper_left_alone_json)" >> "$MANIFEST"
    reaper_report "$REAPED_AT_EXIT"
    [ -n "${HANABI_UI_DEBUG_PIDS:-}" ] && \
        printf 'active_at_exit=%s reaped_at_exit=%s left_alone=%s\n' "${#ACTIVE_PIDS[@]}" "$REAPED_AT_EXIT" "$REAPER_LEFT_ALONE" >> "$HANABI_UI_DEBUG_PIDS"
    if [ "$KEEP_HOMES" = "1" ]; then
        echo "kept per-test homes under $SUITE_TMP"
    else
        rm -rf "$SUITE_TMP"
    fi
}
trap 'st=$?; cleanup; exit $st' EXIT

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

echo "=== scripted UI tests ($DIR) === policy: $POLICY === manifest: $MANIFEST"
[ -n "$SEED" ] && echo "=== shuffled order, seed $SEED ==="
for s in "${SCRIPTS[@]}"; do
    name="$(basename "$s" .e2e)"
    log="/tmp/hanabi_uitest_${name}.log"
    script_sha="$(sha256_of "$s")"

    # --- the fixture's declared environment, RESOLVED before anything else ---
    # Every `# env:` line is read (the old head -1 silently dropped a second
    # one). One line, each key once; policy keys never. The declared mode is
    # HANABI_E2E_WINDOWED's value on that line: armed = windowed, absent or
    # "0" = headless.
    env_lines="$(sed -nE 's/^# env:[[:space:]]*//p' "$s")"
    env_line_count="$(printf '%s' "$env_lines" | grep -c . || true)"
    declared_env=()
    fixture_keys=""
    refuse_reason=""
    if [ "$env_line_count" -gt 1 ]; then
        refuse_reason="ambiguous: $env_line_count '# env:' lines (one is allowed)"
    elif [ "$env_line_count" -eq 1 ]; then
        while IFS= read -r kv; do
            [ -n "$kv" ] || continue
            key="${kv%%=*}"
            case " $POLICY_KEYS " in
                *" $key "*) refuse_reason="fixture sets policy key $key"; break ;;
            esac
            case " $fixture_keys " in
                *" $key "*) refuse_reason="ambiguous: $key declared twice"; break ;;
            esac
            fixture_keys="$fixture_keys $key"
            declared_env+=("$kv")
        done < <(printf '%s' "$env_lines" | xargs -n1 2>/dev/null)
    fi
    declared_windowed=""
    for kv in ${declared_env[@]+"${declared_env[@]}"}; do
        case "$kv" in HANABI_E2E_WINDOWED=*) declared_windowed="${kv#*=}" ;; esac
    done
    if is_armed "$declared_windowed"; then declared_mode=windowed; else declared_mode=headless; fi

    # --- the decision, from the immutable policy and the declared mode ---
    decision=selected; reason=""; effective_mode="$declared_mode"
    if [ -n "$refuse_reason" ]; then
        decision=refused; reason="$refuse_reason"; effective_mode=none
    elif [ "$POLICY" = "headless-only" ] && [ "$declared_mode" = "windowed" ]; then
        decision=skipped; reason="policy headless-only: script declares HANABI_E2E_WINDOWED=$declared_windowed"; effective_mode=none
    fi
    fixture_keys="$(printf '%s' "$fixture_keys" | sed 's/^ //')"

    if [ "$decision" != "selected" ]; then
        printf '{"run_id":"%s","script":"%s","script_sha256":"%s","exe":"%s","exe_sha256":"%s","source_head":"%s","source_dirty_files":%s,"policy":"%s","declared_mode":"%s","effective_mode":"%s","decision":"%s","reason":"%s","fixture_env_keys":"%s","launched":false,"rc":null,"result":"%s","gfx_init_logged":null}\n' \
            "$RUN_ID" "$(json_str "$name")" "$script_sha" "$(json_str "$EXE")" "$EXE_SHA" "$SRC_HEAD" "$SRC_DIRTY" "$POLICY" "$declared_mode" "$effective_mode" "$decision" "$(json_str "$reason")" "$fixture_keys" "$decision" >> "$MANIFEST"
        printf '  %-34s %s  (%s)\n' "$name" "$(printf '%s' "$decision" | tr a-z A-Z)" "$reason"
        continue
    fi

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

    # The fixture's `# env:` line adds environment for this script. Needed for
    # any state a click cannot reach — an overlay whose only binding is a Cmd
    # chord, for instance, which the injector cannot produce
    # (afterhours_gaps.md #49). Values may be single-quoted to hold spaces;
    # parsed with `xargs` above rather than word-splitting so they survive.
    # Rebuilt from THIS script's line alone every iteration: nothing declared
    # by an earlier script is in it.
    extra_env=(${declared_env[@]+"${declared_env[@]}"})
    # Under headless-only, the binary is told the policy too (main.cpp refuses
    # a windowed entry before a window exists); under `all` the variable is
    # absent and the script's own declaration decides.
    policy_env=()
    [ "$POLICY" = "headless-only" ] && policy_env+=("HANABI_E2E_HEADLESS_ONLY=1")

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
    # The pasteboard the app may read for a paste or a drop is a PRIVATE
    # named board, unique per script, set AFTER the fixture's env so no
    # `# env:` line can point a run at the user's general pasteboard. The
    # test binary refuses to start without it (native_extras.mm).
    private_pasteboard="hanabi-e2e-${name}-$$-$(date +%s)"
    # Same rule for sound: the binary logs audio verbs to a private per-script
    # file instead of playing; set after the fixture's env so no `# env:` line
    # can point it elsewhere.
    audio_log="$ISO_HOME/audio.log"
    # `env -u HANABI_E2E_WINDOWED`: the inherited value was refused above and
    # a declared one is in extra_env only when the policy selected it, so
    # this is belt to those braces -- the process starts from a known state.
    # `exec` so that $! is the binary itself (env execs it in place), which is
    # what the timeout and the exit reaper own -- see PROCESS OWNERSHIP above.
    ( exec env -u HANABI_E2E_WINDOWED -u HANABI_E2E_HEADLESS_ONLY -u HANABI_UI_POLICY \
        HOME="$ISO_HOME" HANABI_CONFIG="$ISO_HOME/no-such-config.json" \
        HANABI_CACHE_DIR="$script_cache" TZ="$PIN_TZ" \
        HANABI_MOCK_NOW="$PIN_NOW" \
        HANABI_TOKEN_FILE="$ISO_HOME/token.json" HANABI_BACKEND=mock \
        ${extra_env[@]+"${extra_env[@]}"} \
        ${policy_env[@]+"${policy_env[@]}"} \
        HANABI_PASTEBOARD_NAME="$private_pasteboard" HANABI_AUDIO_LOG="$audio_log" \
        "$EXE" --e2e "$s" ) >"$log" 2>&1 &
    pid=$!
    ACTIVE_PIDS+=("$pid")
    for ((i=0; i<TIMEOUT; i++)); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done
    if kill -0 "$pid" 2>/dev/null; then
        reap_pid "$pid"
        wait "$pid" 2>/dev/null   # join the zombie so the pid is freed
        rc=124
    else
        wait "$pid"; rc=$?
    fi
    forget_pid "$pid"   # waited for: no longer ours to reap, whoever holds the number next

    # OBSERVED mode, cross-checked against the effective one: "Gfx init:" is
    # logged only by app_init(), the windowed path; the headless run never
    # logs it. A headless launch whose log carries it opened a window it was
    # not selected to open -- a policy failure, not a pass, whatever rc says.
    if grep -q 'Gfx init:' "$log" 2>/dev/null; then gfx=true; else gfx=false; fi
    result=pass
    if [ "$rc" -eq 124 ]; then result=timeout
    elif [ "$rc" -ne 0 ]; then result=fail
    fi
    if [ "$effective_mode" = "headless" ] && [ "$gfx" = "true" ]; then
        result=mode_mismatch
    fi
    printf '{"run_id":"%s","script":"%s","script_sha256":"%s","exe":"%s","exe_sha256":"%s","source_head":"%s","source_dirty_files":%s,"policy":"%s","declared_mode":"%s","effective_mode":"%s","decision":"selected","reason":"","fixture_env_keys":"%s","launched":true,"pid":%s,"rc":%s,"result":"%s","gfx_init_logged":%s,"log":"%s"}\n' \
        "$RUN_ID" "$(json_str "$name")" "$script_sha" "$(json_str "$EXE")" "$EXE_SHA" "$SRC_HEAD" "$SRC_DIRTY" "$POLICY" "$declared_mode" "$effective_mode" "$fixture_keys" "$pid" "$rc" "$result" "$gfx" "$(json_str "$log")" >> "$MANIFEST"

    if [ "$result" = "pass" ]; then
        printf '  %-34s PASS\n' "$name"
    else
        printf '  %-34s %s (rc=%s)  %s\n' "$name" "$(printf '%s' "$result" | tr a-z A-Z)" "$rc" "$log"
        sed -n '/E2E ERROR\|TIMEOUT\|FAIL/p' "$log" | head -8 | sed 's/^/      /'
        FAILED_NAMES="$FAILED_NAMES $name"
    fi
done

# TOTALS FROM THE RECORDS. Each count is a grep over the manifest; nothing
# here is a running tally or a subtraction, and selected + skipped + refused
# must equal the number of records, which must equal the number of scripts
# found -- or the run is broken and says so.
# Script records only: the cleanup record is appended by the EXIT trap after
# these totals and carries no "decision".
count() { grep -c "$1" "$MANIFEST" || true; }
N_RECORDS="$(count '"decision":"')"
N_SELECTED="$(count '"decision":"selected"')"
N_SKIPPED="$(count '"decision":"skipped"')"
N_REFUSED="$(count '"decision":"refused"')"
N_PASS="$(count '"result":"pass"')"
N_FAIL="$(count '"result":"fail"')"
N_TIMEOUT="$(count '"result":"timeout"')"
N_MISMATCH="$(count '"result":"mode_mismatch"')"
echo "----------------------------------------"
echo "  policy $POLICY: $N_RECORDS scripts = $N_SELECTED selected + $N_SKIPPED skipped + $N_REFUSED refused"
echo "  selected: $N_PASS passed, $N_FAIL failed, $N_TIMEOUT timed out, $N_MISMATCH mode-mismatch"
echo "  manifest: $MANIFEST"
rc_all=0
if [ "$N_RECORDS" -ne "${#SCRIPTS[@]}" ] || [ $((N_SELECTED + N_SKIPPED + N_REFUSED)) -ne "$N_RECORDS" ] \
   || [ $((N_PASS + N_FAIL + N_TIMEOUT + N_MISMATCH)) -ne "$N_SELECTED" ]; then
    echo "  MANIFEST BROKEN: records do not reconcile with the scripts found" >&2
    rc_all=3
fi
if [ "$N_REFUSED" -ne 0 ]; then
    echo "  refused fixtures are a configuration error; see the manifest reasons" >&2
    rc_all=2
fi
if [ "$N_FAIL" -ne 0 ] || [ "$N_TIMEOUT" -ne 0 ] || [ "$N_MISMATCH" -ne 0 ]; then
    echo "  failed:$FAILED_NAMES" >&2
    [ -n "$SEED" ] && echo "  reproduce this order with HANABI_UI_SEED=$SEED" >&2
    [ "$rc_all" -eq 0 ] && rc_all=1
fi
exit "$rc_all"
