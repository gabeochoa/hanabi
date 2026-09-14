#!/usr/bin/env bash
# ===========================================================================
# run_ui_controls.sh -- the FAILURE CONTROLS for the semantic e2e helpers.
#
# A helper that passes its positive scripts proves nothing until it is shown
# to FAIL when the expectation is wrong. Every `control_*.e2e` under
# tests/ui_controls/ is written to fail for ONE reason at ONE line; this
# driver accepts a control only when its log shows EXACTLY that:
#   * the run reached the end ("E2E finished" -- no crash, no runner timeout)
#   * exactly one "[E2E ERROR|TIMEOUT] <cmd> (line N)" line, and it is the control's
#     declared asserting command at its declared line (a "# control:" header
#     names both) -- a startup refusal, an unknown command, a wrong target or
#     an earlier assertion failing first is a BROKEN control, not a pass
#   * the runner's verdict for it is FAIL
# The one positive control (tokeniser) must reach the end with NO error line.
# So a generic non-zero exit is never inverted into a success.
#
#   usage: scripts/run_ui_controls.sh
# ===========================================================================
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/tests/ui_controls"
out="$(HANABI_UI_TESTS="$DIR" bash "$ROOT/scripts/run_ui_tests.sh" 2>&1)"
echo "$out" | grep -E "^  control_"
bad=0; seen=0
for s in "$DIR"/control_*.e2e; do
    name="$(basename "$s" .e2e)"
    log="/tmp/hanabi_uitest_${name}.log"
    # "# control: fail <command> <line>" or "# control: pass"
    spec="$(sed -nE 's/^# control:[[:space:]]*//p' "$s" | head -1)"
    if [ -z "$spec" ]; then echo "CONTROL BROKEN: $name has no '# control:' header"; bad=1; continue; fi
    if [ ! -f "$log" ]; then echo "CONTROL BROKEN: $name never ran (no $log)"; bad=1; continue; fi
    seen=$((seen+1))
    verdict="$(echo "$out" | grep -E "^  $name " | awk '{print $2}')"
    finished="$(grep -c 'E2E finished' "$log")"
    # A command that retries until its frame budget fails as "[TIMEOUT] <cmd>
    # (line N)"; one that refuses outright fails as "[E2E ERROR] ...". Both are
    # the command's own verdict; both count.
    errors="$(grep -E '\[(E2E ERROR|TIMEOUT)\] [a-z_]+ \(line [0-9]+\)' "$log")"
    nerr="$(printf '%s' "$errors" | grep -c . )"
    if [ "$finished" != "1" ]; then
        echo "CONTROL BROKEN: $name did not reach the end (crash / runner timeout / startup refusal); verdict=$verdict"; bad=1; continue
    fi
    if [ "$spec" = "pass" ]; then
        if [ "$verdict" != "PASS" ] || [ "$nerr" != "0" ]; then
            echo "CONTROL BROKEN: $name should pass cleanly; verdict=$verdict errors=$nerr"; echo "$errors" | sed 's/^/      /'; bad=1
        fi
        continue
    fi
    want_cmd="$(echo "$spec" | awk '{print $2}')"; want_line="$(echo "$spec" | awk '{print $3}')"
    if [ "$verdict" != "FAIL" ]; then
        echo "CONTROL BROKEN: $name should FAIL at $want_cmd line $want_line, got verdict=$verdict -- the helper ACCEPTED a wrong expectation"; bad=1; continue
    fi
    if [ "$nerr" != "1" ]; then
        echo "CONTROL BROKEN: $name should fail at exactly one line ($want_cmd line $want_line); $nerr error lines:"; echo "$errors" | sed 's/^/      /'; bad=1; continue
    fi
    if ! printf '%s' "$errors" | grep -qE "\[(E2E ERROR|TIMEOUT)\] $want_cmd \(line $want_line\)"; then
        echo "CONTROL BROKEN: $name failed somewhere else than $want_cmd line $want_line:"; echo "$errors" | sed 's/^/      /'; bad=1; continue
    fi
    echo "  ok: $name refused at $want_cmd line $want_line"
done
[ "$seen" -ge 8 ] || { echo "CONTROL BROKEN: only $seen controls ran"; bad=1; }
[ "$bad" = 0 ] && echo "ui controls: every helper refused its wrong expectation at the declared line; tokeniser control clean"
exit $bad
