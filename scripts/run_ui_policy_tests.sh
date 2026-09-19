#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNNER="$ROOT/scripts/run_ui_tests.sh"
STUB="$ROOT/tests/runner_policy/stub_exe.sh"
chmod +x "$STUB" 2>/dev/null

T="$(mktemp -d /tmp/hanabi_policy_tests.XXXXXX)"
EVIDENCE="${HANABI_POLICY_EVIDENCE:-$(mktemp -d /tmp/hanabi_policy_evidence.XXXXXX)}"
PY3=python3
[ "$(uname)" = Darwin ] && PY3=/usr/bin/python3
COMPLETED=0
on_exit() {
    local st=$?
    mkdir -p "$EVIDENCE/raw" && cp -Rp "$T"/. "$EVIDENCE/raw/" 2>/dev/null
    echo "evidence: $EVIDENCE"
    rm -rf "$T"
    if [ "$COMPLETED" != 1 ]; then
        echo "SUITE ABORTED: the policy tests did not reach their final marker (exit $st)" >&2
        exit 70
    fi
    exit "$st"
}
trap on_exit EXIT
bad=0
ok() { printf '  ok   %s\n' "$1"; }
ng() { printf '  FAIL %s\n' "$1"; bad=1; }
check() { if eval "$2"; then ok "$1"; else ng "$1"; fi; }

mk_fixtures() {
    local d="$1"; mkdir -p "$d"
    cat > "$d/headless_one.e2e" <<'EOF'
# a genuine headless fixture
# settings: {"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}
require_thread t2
EOF
    cat > "$d/windowed_one.e2e" <<'EOF'
# a windowed fixture, declared the way every native script declares it
# env: HANABI_E2E_WINDOWED=1 HANABI_NATIVE_MENU_LOG=1
require_thread t2
EOF
}

run_runner() {
    local dir="$1" rec="$2" man="$3"; shift 3
    ( env -u HANABI_E2E_WINDOWED -u HANABI_E2E_HEADLESS_ONLY -u HANABI_UI_POLICY -u HANABI_UI_SEED \
        HANABI_UI_EXE="$STUB" HANABI_UI_TESTS="$dir" HANABI_UI_MANIFEST="$man" \
        HANABI_STUB_RECORD="$rec" HANABI_UI_TIMEOUT=5 "$@" \
        bash "$RUNNER" ) >"$T/out.$(basename "$rec").txt" 2>&1
    local rc=$?
    cp "$T/out.$(basename "$rec").txt" "$T/out.txt"
    echo "$rc"
}
records() { [ -f "$1" ] && grep -c . "$1" || echo 0; }

echo "=== runner launch-policy tests (stub executable, no app, no window) ==="

mk_fixtures "$T/mixed"
rc="$(run_runner "$T/mixed" "$T/rec1" "$T/man1")"
check "1. default policy: run exits 0 with a windowed fixture present (skipped is not a failure)" '[ "$rc" = 0 ]'
check "1. windowed fixture: zero launches (stub record has exactly one line, the headless one)" '[ "$(records "$T/rec1")" = 1 ] && grep -q "argv=--e2e .*headless_one.e2e" "$T/rec1"'
check "1. manifest: windowed_one is skipped with the policy reason" 'grep -q "\"script\":\"windowed_one\".*\"decision\":\"skipped\".*policy headless-only" "$T/man1"'
check "2. headless fixture selected, effective mode headless, result pass" 'grep -q "\"script\":\"headless_one\".*\"effective_mode\":\"headless\".*\"decision\":\"selected\".*\"result\":\"pass\"" "$T/man1"'
check "2. the launch carried HANABI_E2E_HEADLESS_ONLY=1 and NO HANABI_E2E_WINDOWED" 'grep -q "WINDOWED=<unset> HEADLESS_ONLY=1 BACKEND=mock" "$T/rec1"'
check "2. the launched executable was the stub, never the app" '! grep -qv "exe=$STUB " "$T/rec1"'
check "7. totals reconcile: 2 records = 1 selected + 1 skipped + 0 refused (+ the cleanup record)" 'grep -q "2 scripts = 1 selected + 1 skipped + 0 refused" "$T/out.txt" && [ "$(grep -c "\"decision\":" "$T/man1")" = 2 ] && [ "$(grep -c "\"record\":\"cleanup\"" "$T/man1")" = 1 ]'
check "7. every script record names the stub as exe; every record carries the run id" '[ "$(grep -c "\"exe\":\"$STUB\"" "$T/man1")" = 2 ] && [ "$(grep -c "\"run_id\":\"" "$T/man1")" = 3 ]'

mk_fixtures "$T/inh"
rc="$(run_runner "$T/inh" "$T/rec3" "$T/man3" HANABI_E2E_WINDOWED=1)"
check "3. inherited HANABI_E2E_WINDOWED=1: runner refuses (rc 66) before any launch" '[ "$rc" = 66 ] && [ ! -f "$T/rec3" ] && grep -q "HANABI_E2E_WINDOWED=.1. is set in this shell" "$T/out.txt"'
rc="$(run_runner "$T/inh" "$T/rec3b" "$T/man3b" HANABI_E2E_WINDOWED=0)"
check "3. inherited HANABI_E2E_WINDOWED=0 is unarmed: the run proceeds" '[ "$rc" = 0 ] && [ "$(records "$T/rec3b")" = 1 ]'
rc="$(run_runner "$T/inh" "$T/rec3c" "$T/man3c" HANABI_E2E_HEADLESS_ONLY=1)"
check "3. inherited HANABI_E2E_HEADLESS_ONLY: refused (the runner's own variable)" '[ "$rc" = 66 ] && [ ! -f "$T/rec3c" ]'
rc="$(run_runner "$T/inh" "$T/rec3d" "$T/man3d" HANABI_UI_POLICY=windowed-please)"
check "3. an unknown HANABI_UI_POLICY value is refused (rc 65), nothing launched" '[ "$rc" = 65 ] && [ ! -f "$T/rec3d" ]'

mk_fixtures "$T/pol"
cat > "$T/pol/policy_unset.e2e" <<'EOF'
# a fixture trying to move the boundary from inside it
# env: HANABI_E2E_HEADLESS_ONLY=0 HANABI_E2E_WINDOWED=1
require_thread t2
EOF
cat > "$T/pol/policy_all.e2e" <<'EOF'
# env: HANABI_UI_POLICY=all
require_thread t2
EOF
rc="$(run_runner "$T/pol" "$T/rec4" "$T/man4")"
check "4. policy-key fixtures are REFUSED and the run exits 2 even though the selected script passed" '[ "$rc" = 2 ]'
check "4. policy_unset refused with the key named, not launched" 'grep -q "\"script\":\"policy_unset\".*\"decision\":\"refused\".*fixture sets policy key HANABI_E2E_HEADLESS_ONLY.*\"launched\":false" "$T/man4"'
check "4. policy_all refused with the key named" 'grep -q "\"script\":\"policy_all\".*\"decision\":\"refused\".*HANABI_UI_POLICY" "$T/man4"'
check "4. only the headless fixture launched; no launch carried HEADLESS_ONLY unset or WINDOWED armed" '[ "$(records "$T/rec4")" = 1 ] && ! grep -q "WINDOWED=1" "$T/rec4" && ! grep -q "HEADLESS_ONLY=<unset>\|HEADLESS_ONLY=0" "$T/rec4"'
check "4. totals: 4 scripts = 1 selected + 1 skipped + 2 refused" 'grep -q "4 scripts = 1 selected + 1 skipped + 2 refused" "$T/out.txt"'

mkdir -p "$T/amb"
cat > "$T/amb/two_env_lines.e2e" <<'EOF'
# env: HANABI_MOCK_NOW=1
# env: HANABI_E2E_WINDOWED=1
require_thread t2
EOF
cat > "$T/amb/key_twice.e2e" <<'EOF'
# env: HANABI_E2E_WINDOWED=0 HANABI_E2E_WINDOWED=1
require_thread t2
EOF
rc="$(run_runner "$T/amb" "$T/rec5" "$T/man5")"
check "5. two '# env:' lines: refused as ambiguous, not launched" 'grep -q "\"script\":\"two_env_lines\".*\"decision\":\"refused\".*ambiguous: 2 .# env:. lines" "$T/man5"'
check "5. the same key twice: refused as ambiguous, not launched" 'grep -q "\"script\":\"key_twice\".*\"decision\":\"refused\".*declared twice" "$T/man5"'
check "5. zero launches, run exits 2" '[ "$rc" = 2 ] && [ ! -f "$T/rec5" ]'

mk_fixtures "$T/all"
rc="$(run_runner "$T/all" "$T/rec6" "$T/man6" HANABI_UI_POLICY=all)"
check "6. policy all: both fixtures selected and launched (stub), run exits 0" '[ "$rc" = 0 ] && [ "$(records "$T/rec6")" = 2 ]'
check "6. the windowed fixture's launch carried HANABI_E2E_WINDOWED=1 and NO HANABI_E2E_HEADLESS_ONLY" 'grep -q "windowed_one.e2e WINDOWED=1 HEADLESS_ONLY=<unset>" "$T/rec6"'
check "6. the headless fixture's launch under all: no WINDOWED, no HEADLESS_ONLY" 'grep -q "headless_one.e2e WINDOWED=<unset> HEADLESS_ONLY=<unset>" "$T/rec6"'
check "6. manifest: windowed_one effective_mode windowed, selected" 'grep -q "\"script\":\"windowed_one\".*\"effective_mode\":\"windowed\".*\"decision\":\"selected\"" "$T/man6"'
check "6. the policy value the stub saw was NOT forwarded (env -u HANABI_UI_POLICY)" '! grep -q "POLICY=all" "$T/rec6"'

mk_fixtures "$T/gfx"
rm "$T/gfx/windowed_one.e2e"
rc="$(run_runner "$T/gfx" "$T/rec7" "$T/man7" HANABI_STUB_PRINT_GFX=1)"
check "7. a headless launch whose log carries 'Gfx init:' is a mode_mismatch and the run FAILS" '[ "$rc" = 1 ] && grep -q "\"effective_mode\":\"headless\".*\"result\":\"mode_mismatch\".*\"gfx_init_logged\":true" "$T/man7"'
rc="$(run_runner "$T/gfx" "$T/rec7b" "$T/man7b" HANABI_STUB_EXIT=7)"
check "7. a failing script is result fail with its rc, run exits 1" '[ "$rc" = 1 ] && grep -q "\"rc\":7,\"result\":\"fail\"" "$T/man7b"'

mk_fixtures "$T/own"
rm "$T/own/windowed_one.e2e"
BYSTANDER_LIFETIME=30
( exec -a "$STUB" sleep "$BYSTANDER_LIFETIME" ) >/dev/null 2>&1 &
bystander=$!
bystander_t0=$(date +%s)
bystander_alive() {
    if [ $(( $(date +%s) - bystander_t0 )) -ge $(( BYSTANDER_LIFETIME - 3 )) ]; then
        echo "bystander $bystander: lifetime elapsed before the check; liveness not asserted" >> "$T/leftover.txt"
        return 1
    fi
    kill -0 "$bystander" 2>/dev/null
}
sleep 0.2
bystander_alive || ng "8. setup: bystander did not start"
rc="$(run_runner "$T/own" "$T/rec8" "$T/man8" HANABI_STUB_SLEEP=30 HANABI_UI_TIMEOUT=2)"
check "8. a hung script is reaped at the timeout: result timeout, rc 124, run exits 1" '[ "$rc" = 1 ] && grep -q "\"rc\":124,\"result\":\"timeout\"" "$T/man8"'
launched_pid="$(sed -nE 's/.*"pid":([0-9]+),.*/\1/p' "$T/man8" | head -1)"
check "8. the recorded pid is gone after the run" '[ -n "$launched_pid" ] && ! kill -0 "$launched_pid" 2>/dev/null'
check "8. the unrelated same-path process is STILL alive (no kill by name)" 'bystander_alive'
rc="$(run_runner "$T/own" "$T/rec8b" "$T/man8b" HANABI_E2E_WINDOWED=1)"
check "8. a refusal (rc 66) launches nothing and leaves the unrelated process alive" '[ "$rc" = 66 ] && bystander_alive'
rc="$(run_runner "$T/own" "$T/rec8c" "$T/man8c" HANABI_STUB_SLEEP=30 HANABI_STUB_GRANDCHILD="$T/grandchild.pid" HANABI_UI_TIMEOUT=2)"
gc="$(cat "$T/grandchild.pid" 2>/dev/null)"
launched_pid="$(sed -nE 's/.*"pid":([0-9]+),.*/\1/p' "$T/man8c" | head -1)"
if [ -n "$gc" ] && ! kill -0 "$gc" 2>/dev/null; then
    ok "8. the grandchild is in the group: dead after the timeout teardown"
else
    ng "8. the grandchild is in the group: dead after the timeout teardown -- UNCERTAIN leftover pid ${gc:-<none>} recorded, not signalled"
    echo "grandchild ${gc:-<none>}: still observed after the timeout teardown; uncertain leftover, not signalled" >> "$T/leftover.txt"
fi
check "8. and the recorded pid is dead, the bystander alive" '[ -n "$launched_pid" ] && ! kill -0 "$launched_pid" 2>/dev/null && bystander_alive'
wait "$bystander" 2>/dev/null
echo "bystander $bystander: collected by wait after its own lifetime, never signalled" >> "$T/leftover.txt"
rc="$(run_runner "$T/own" "$T/rec8f" "$T/man8f" HANABI_STUB_SLEEP=30 HANABI_STUB_ORPHAN="$T/orphan.pid" HANABI_UI_TIMEOUT=2)"
orphan="$(cat "$T/orphan.pid" 2>/dev/null)"
launched_pid="$(sed -nE 's/.*"pid":([0-9]+),.*/\1/p' "$T/man8f" | head -1)"
check "8. the recorded root is dead (it was ours)" '[ -n "$launched_pid" ] && ! kill -0 "$launched_pid" 2>/dev/null'
if [ -n "$orphan" ] && ! kill -0 "$orphan" 2>/dev/null; then
    ok "8. the double-forked process stayed in our GROUP and the group signal took it"
else
    ng "8. the double-forked process stayed in our GROUP and the group signal took it -- UNCERTAIN leftover pid ${orphan:-<none>} recorded, not signalled"
    echo "orphan ${orphan:-<none>}: still observed after the group teardown; uncertain leftover, not signalled" >> "$T/leftover.txt"
fi
check "8. the script record carries the supervisor testimony: group signalled, census observed" 'grep -q "\"supervisor\":{" "$T/man8f" && grep -q "\"term_sent\": true" "$T/man8f" && grep -q "\"census\": \"observed\"" "$T/man8f"'
if "$PY3" "$ROOT/tests/runner_policy/test_supervise.py" >"$T/unit.txt" 2>&1; then
    ok "8. supervisor unit passed (see its own labels)"; sed 's/^/      /' "$T/unit.txt"
else
    ng "8. supervisor unit FAILED"; sed 's/^/      /' "$T/unit.txt"
fi
SUP="$ROOT/scripts/lib/supervise.py"
CLS="$ROOT/scripts/lib/classify_record.py"
printf '%s' '{"token":"planted:x:1:1","ownership":"established","exit_status":0,"reap_timed_out":true,"cleanup":"none observed"}' > "$T/rec_rt.json"
check "8. classifier: a passed fixture whose reap timed out is UNSUPERVISED and uncertain, not a pass" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_rt.json" planted:x:1:1) $("$PY3" -I "$CLS" --field cleanup "$T/rec_rt.json" planted:x:1:1)" = "unsupervised uncertain" ]'
printf '%s' '{not json' > "$T/rec_bad.json"
check "8. classifier: a malformed record is UNSUPERVISED and uncertain" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_bad.json" planted:x:1:1) $("$PY3" -I "$CLS" --field reason "$T/rec_bad.json" planted:x:1:1) $("$PY3" -I "$CLS" --field cleanup "$T/rec_bad.json" planted:x:1:1)" = "unsupervised no_record uncertain" ]'
check "8. classifier: an absent record is UNSUPERVISED and uncertain" '[ "$("$PY3" -I "$CLS" --field result "$T/does-not-exist.json" planted:x:1:1) $("$PY3" -I "$CLS" --field cleanup "$T/does-not-exist.json" planted:x:1:1)" = "unsupervised uncertain" ]'
printf '%s' '{"token":"planted:x:1:1","ownership":"unestablished","exit_status":null}' > "$T/rec_un.json"
check "8. classifier: ownership unestablished is UNSUPERVISED" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_un.json" planted:x:1:1)" = unsupervised ]'
printf '%s' '{"token":"planted:x:1:1","ownership":"established","wall_hit":true,"exit_status":null,"term_signal":15,"cleanup":"none observed"}' > "$T/rec_wall.json"
check "8. classifier: wall hit is TIMEOUT even though the child died of a signal" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_wall.json" planted:x:1:1) $("$PY3" -I "$CLS" --field cleanup "$T/rec_wall.json" planted:x:1:1)" = "timeout certain" ]'
printf '%s' '{"token":"planted:x:1:1","ownership":"established","interrupted":true,"exit_status":null,"term_signal":15,"cleanup":"none observed"}' > "$T/rec_int.json"
check "8. classifier: supervisor interrupted is ABORTED" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_int.json" planted:x:1:1)" = aborted ]'
printf '%s' '{"token":"planted:x:1:1","ownership":"established","exit_status":null,"term_signal":11,"cleanup":"none observed"}' > "$T/rec_sig.json"
check "8. classifier: a child killed by a signal is FAIL with the signal named" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_sig.json" planted:x:1:1) $("$PY3" -I "$CLS" --field reason "$T/rec_sig.json" planted:x:1:1)" = "fail signal 11" ]'
printf '%s' '{"token":"planted:x:1:1","ownership":"established","exit_status":5,"cleanup":"uncertain","left_in_group":[7]}' > "$T/rec_f.json"
check "8. classifier: exit 5 is FAIL, and a non-empty leftover is uncertain" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_f.json" planted:x:1:1) $("$PY3" -I "$CLS" --field reason "$T/rec_f.json" planted:x:1:1) $("$PY3" -I "$CLS" --field cleanup "$T/rec_f.json" planted:x:1:1)" = "fail exit 5 uncertain" ]'
printf '%s' '{"token":"planted:x:1:1","ownership":"established","exit_status":0,"cleanup":"none observed","root_pid":4242}' > "$T/rec_ok.json"
check "8. classifier: exit 0 with nothing observed is PASS, certain, root pid carried" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_ok.json" planted:x:1:1) $("$PY3" -I "$CLS" --field reason "$T/rec_ok.json" planted:x:1:1) $("$PY3" -I "$CLS" --field cleanup "$T/rec_ok.json" planted:x:1:1) $("$PY3" -I "$CLS" --field root_pid "$T/rec_ok.json" planted:x:1:1)" = "pass exit 0 certain 4242" ]'
printf '%s' '{"token":"old-run:x:1:1","ownership":"established","exit_status":0,"cleanup":"none observed","observer":"ok"}' > "$T/rec_stale.json"
check "8. classifier: a PASS record with a foreign token is UNSUPERVISED token_mismatch, uncertain" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_stale.json" this-run:x:2:2) $("$PY3" -I "$CLS" --field reason "$T/rec_stale.json" this-run:x:2:2) $("$PY3" -I "$CLS" --field cleanup "$T/rec_stale.json" this-run:x:2:2)" = "unsupervised token_mismatch uncertain" ]'
printf '%s' '{"token":"this-run:x:2:2","ownership":"established","exit_st' > "$T/rec_trunc.json"
check "8. classifier: a truncated record is UNSUPERVISED no_record" '[ "$("$PY3" -I "$CLS" --field result "$T/rec_trunc.json" this-run:x:2:2) $("$PY3" -I "$CLS" --field reason "$T/rec_trunc.json" this-run:x:2:2)" = "unsupervised no_record" ]'
if "$PY3" "$ROOT/tests/runner_policy/test_classify_record.py" >"$T/cls_unit.txt" 2>&1; then
    ok "8. classify_record unit passed (see its own labels)"; sed 's/^/      /' "$T/cls_unit.txt"
else
    ng "8. classify_record unit FAILED"; sed 's/^/      /' "$T/cls_unit.txt"
fi
mk_fixtures "$T/stale"
rm "$T/stale/windowed_one.e2e"
rc="$(run_runner "$T/stale" "$T/rec8s" "$T/man8s" HANABI_STUB_EXIT=3)"
check "8. every script record's supervisor token is this run's (RUN_ID prefix), none foreign" '[ "$(grep -c "\"token\": \"" "$T/man8s")" -ge 1 ] && ! grep -q "\"token\": \"old-run" "$T/man8s" && [ "$(grep -o "\"token\": \"[^:]*" "$T/man8s" | sort -u | wc -l | tr -d " ")" = 1 ]'
check "8. the failing child (exit 3) classifies as FAIL from the record, reason exit 3" 'grep -q "\"result\":\"fail\",\"reason\":\"exit 3\"" "$T/man8s"'
check "8. invariant: one launch line: python -I supervise.py … -- env -u <the three policy vars> …; nothing else backgrounds the binary" '[ "$(grep -c "\"\$PYTHON3\" -I \"\$SUPERVISE\" --wall" "$RUNNER")" = 1 ] && grep -A1 "\"\$SUPERVISE\" --wall" "$RUNNER" | grep -q "env -u HANABI_E2E_WINDOWED -u HANABI_E2E_HEADLESS_ONLY -u HANABI_UI_POLICY" && ! grep -qE "exec env .*\"\$EXE\"" "$RUNNER"'
check "8. invariant: the runner has no kill-by-name, no pgrep, no pid reaper (code lines; comments may name what was retired)" '! grep -vE "^[[:space:]]*#" "$RUNNER" | grep -qE "pkill|pgrep|reap_pid|ACTIVE_PIDS|reaper"'
check "8. invariant: the runner sends no signal to a pid number (kill -0 probes only)" '! grep -E "^[[:space:]]*kill( -[A-Za-z0-9]+)? " "$RUNNER" | grep -qv "kill -0"'
check "8. invariant: this suite sends no signal to a pid number either" '! grep -E "^[[:space:]]*kill( -[A-Za-z0-9]+)? " "$0" | grep -qv "kill -0"'
check "8. invariant: the runner reads every classifier field through --field, never by splitting its output" '[ "$(grep -c "classify_record.py\" --field" "$RUNNER")" = 5 ] && ! grep -q "verdict%% " "$RUNNER"'
check "8. invariant: the runner derives result from the record (classify_record.py), not from an rc ladder" 'grep -q "classify_record.py" "$RUNNER" && ! grep -qE "rc.* -eq 124.*result=timeout|result=timeout$" "$RUNNER"'
check "8. invariant: the runner clears the record path before launch, mints a token and passes it to both supervisor and classifier" 'grep -q "rm -f \"\$record\" \"\$record.tmp\"" "$RUNNER" && grep -q "\-\-token \"\$token\"" "$RUNNER" && grep -q "classify_record.py\" --field result \"\$record\" \"\$token\"" "$RUNNER"'
check "8. invariant: the supervisor writes its record atomically (tmp + os.replace) with the token first" 'grep -q "os.replace(tmp, path)" "$SUP" && grep -q "\"token\": token" "$SUP"'
check "8. invariant: the supervisor has exactly one os.waitpid site" '[ "$(grep -c "os.waitpid(" "$SUP")" = 1 ]'
check "8. invariant: the supervisor never signals a pid number (killpg only)" '! grep -q "os\.kill(" "$SUP" && grep -q "os.killpg(" "$SUP"'
check "8. invariant: the supervisor installs no SIGCHLD handler (only the SIG_DFL reset)" '[ "$(grep -c "signal.signal(signal.SIGCHLD" "$SUP")" = 1 ] && grep -q "signal.SIGCHLD, signal.SIG_DFL" "$SUP"'
check "8. invariant: the supervisor reads no environment knob" '! grep -q "HANABI_" "$SUP" && ! grep -q "os.getenv(" "$SUP"'
check "8. invariant: the supervisor runs no shell (no os.system, no shell=True)" '! grep -q "os.system(" "$SUP" && ! grep -q "shell=True" "$SUP"'
check "8. invariant: on Darwin the runner pins /usr/bin/python3" 'grep -q "PYTHON3=/usr/bin/python3" "$RUNNER"'
mk_fixtures "$T/done"
rm "$T/done/windowed_one.e2e"
rc="$(run_runner "$T/done" "$T/rec8d" "$T/man8d")"
check "8. a completed launch: run exits 0, its record says none observed, the cleanup record counts 0 uncertain" '[ "$rc" = 0 ] && grep -q "\"cleanup\": \"none observed\"" "$T/man8d" && grep -q "\"record\":\"cleanup\",\"supervisors_at_exit\":0,\"supervisors_joined\":0,\"signals_sent\":0,\"uncertain_cleanups\":0" "$T/man8d"'
check "8. every manifest line of that run is valid JSON, its pass reason is whole (\"exit 0\"), and the cleanup record joined nothing and sent no signal" '"$PY3" -c "import json,sys; [json.loads(l) for l in open(sys.argv[1]) if l.strip()]" "$T/man8d" && grep -q "\"result\":\"pass\",\"reason\":\"exit 0\"" "$T/man8d" && grep -q "\"supervisors_at_exit\":0,\"supervisors_joined\":0,\"signals_sent\":0" "$T/man8d"'
rc="$(run_runner "$T/done" "$T/rec8e" "$T/man8e" HANABI_STUB_EXIT=5)"
check "8. a failing script: run exits 1 (status preserved through the EXIT trap); the child's real exit is in the record" '[ "$rc" = 1 ] && grep -q "\"exit_status\": 5" "$T/man8e" && grep -q "\"uncertain_cleanups\":0" "$T/man8e"'

COMPLETED=1
if [ "$bad" -ne 0 ]; then
    echo "policy tests FAILED; runner output of the last case:" >&2
    sed 's/^/    /' "$T/out.txt" >&2
    exit 1
fi
echo "policy tests ok"
exit 0
