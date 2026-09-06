#!/usr/bin/env bash
# scripts/latency_delay_sweep.sh — OBSERVER CALIBRATION, plus one planted
# over-budget control.
#
# Be precise about what the sweep proves. HANABI_LATENCY_DELAY holds the
# OBSERVATION back N frames after the watched ink appears; it does not slow the
# app down. So the sweep proves the instrument's arithmetic -- that each arm
# reports (settle - input) from its own watched target, and that no arm is
# reading a constant or another arm's number. It does NOT prove the app is
# slow, and it must not be described as delaying app ink.
#
# The last section is the part that proves a budget can FAIL: one arm run at
# its real, unchanged budget with a delay large enough to cross it. Without
# that, every arm's budget is only ever satisfied and the ceiling is decorative.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"

# One scratch root for everything this script writes, removed on EVERY exit
# path -- success, failure, or interrupt. This runs inside `make test`, so a
# per-control `mktemp -d` that is only cleaned on the happy path leaks a
# directory into /tmp on each run.
SCRATCH="$(mktemp -d)"
cleanup() { rm -rf "$SCRATCH"; }
trap cleanup EXIT INT TERM
N="${HANABI_LATENCY_SWEEP_DELAY:-7}"
SCRIPTS=(interaction_latency_budgets interaction_latency_budgets_two
         interaction_latency_scroll)

# The nine arms the user asked for, by name. Defined here so a vanishing arm is
# a FAILURE rather than a smaller set: an arm whose script errors emits no
# [latency] row, and a comparison that iterates only what it found would skip
# it and still pass. That is the shape this gate exists to refuse.
EXPECTED_ARMS="attachment click_feedback focus popup_open scroll session_switch sheet_open submit typing_search"

run() {  # $1 = delay ; prints "<arm> <frames>" lines ; nonzero if the suite failed
    local d=$1 dir rc=0
    dir="$(mktemp -d "$SCRATCH/sweep.XXXXXX")"
    for s in "${SCRIPTS[@]}"; do cp "tests/ui/$s.e2e" "$dir/"; done
    # Budgets are sized for delay 0; raise them so the sweep MEASURES rather
    # than trips them. The over-budget control below is what proves a budget
    # can fail.
    if [ "$d" -ne 0 ]; then
        perl -0pi -e "s/^(expect_latency \\w+ )(\\d+)/\$1.(\$2+$d+40)/gme" "$dir"/*.e2e
    fi
    HANABI_LATENCY_DELAY="$d" HANABI_UI_TESTS="$dir" \
        bash scripts/run_ui_tests.sh >/dev/null 2>&1 || rc=$?
    for s in "${SCRIPTS[@]}"; do
        grep -ohE "\\[latency\\] [a-z_]+ [0-9-]+" "/tmp/hanabi_uitest_$s.log" 2>/dev/null
    done | awk '{print $2, $3}' | sort -u
    return $rc
}

# Every expected arm appears exactly once, with a number, and nothing else does.
check_arm_set() {  # $1 = label  $2 = measurements
    local label="$1" data="$2" bad=0 arm n
    for arm in $EXPECTED_ARMS; do
        n="$(echo "$data" | awk -v a="$arm" '$1==a{c++} END{print c+0}')"
        if [ "$n" -eq 0 ]; then
            echo "  FAIL — $label: arm '$arm' produced no measurement" >&2
            bad=1
        elif [ "$n" -gt 1 ]; then
            echo "  FAIL — $label: arm '$arm' measured $n times" >&2
            bad=1
        fi
    done
    while read -r arm _; do
        [ -n "$arm" ] || continue
        case " $EXPECTED_ARMS " in
            *" $arm "*) ;;
            *) echo "  FAIL — $label: unexpected arm '$arm'" >&2; bad=1 ;;
        esac
    done <<< "$data"
    return $bad
}

echo "=== hanabi latency delay sweep (injected delay ${N} frames) ==="
fail=0
# Both calibration runs must EXIT 0. Ignoring the suite's status is how a
# failing script became an absent arm instead of a failed gate.
base="$(run 0)"    || { echo "  FAIL — the baseline suite exited nonzero" >&2; fail=1; }
delayed="$(run "$N")" || { echo "  FAIL — the delayed suite exited nonzero" >&2; fail=1; }
check_arm_set "baseline" "$base"    || fail=1
check_arm_set "delayed"  "$delayed" || fail=1
printf '  %-16s %8s %8s %8s   %s\n' arm base delayed delta verdict
# Iterate the EXPECTED set, not what was found.
for arm in $EXPECTED_ARMS; do
    b=$(echo "$base"    | awk -v a="$arm" '$1==a{print $2}')
    d=$(echo "$delayed" | awk -v a="$arm" '$1==a{print $2}')
    if [ -z "$b" ]; then
        printf '  %-16s %8s %8s %8s   FAIL — no baseline measurement\n' "$arm" "-" "-" "-"
        fail=1; continue
    fi
    if [ -z "$d" ]; then
        printf '  %-16s %8s %8s %8s   FAIL — vanished under delay\n' "$arm" "$b" "-" "-"
        fail=1; continue
    fi
    delta=$((d - b))
    # +/-1 frame. The base and delayed runs are separate runs, and an arm whose
    # ink lands on a frame boundary measures 6 or 7 either time -- `submit`
    # does exactly that, observed both across runs. An exact-equality rule
    # fails on the arm's own honest variance rather than on a broken
    # instrument. The signal that matters is that the number MOVES WITH THE
    # DELAY: a stuck arm reads +0 and still fails, as does one that vanishes.
    if [ "$delta" -ge $((N - 1)) ] && [ "$delta" -le $((N + 1)) ]; then
        v="ok"
    else
        v="FAIL — expected +$N (+/-1)"; fail=1
    fi
    printf '  %-16s %8s %8s %8s   %s\n' "$arm" "$b" "$d" "+$delta" "$v"
done
[ "$fail" -eq 0 ] || { echo "  FAILED: an arm did not move with what it observes." >&2; exit 1; }
echo "  calibration ok — every arm moved with the injected ${N} frames"

# --- planted controls ---------------------------------------------------
# Three plants, each asserting the harness's EXIT STATUS as well as its
# diagnostic. Status matters on its own: a harness that prints the error and
# exits 0 turns every one of these into a silent pass.
#
# The delay used below must stay under each arm's own wait_frames. Past that
# the target has not been observed yet and the failure is "never appeared",
# which proves nothing about budgets.

plant() {  # $1 = label  $2 = script body file  $3 = expected substring  $4 = env
    local label="$1" body="$2" want="$3" env="$4"
    local dir; dir="$(mktemp -d "$SCRATCH/plant.XXXXXX")"
    cp "$body" "$dir/"
    local name; name="$(basename "$body" .e2e)"
    rm -f "/tmp/hanabi_uitest_${name}.log"
    ( eval "$env" HANABI_UI_TESTS="$dir" bash scripts/run_ui_tests.sh ) \
        >/dev/null 2>&1
    local rc=$?
    rm -rf "$dir"
    local line
    line="$(grep -oE "$want" "/tmp/hanabi_uitest_${name}.log" 2>/dev/null | tail -1)"
    if [ "$rc" -eq 0 ]; then
        echo "  FAIL — $label: the harness exited 0; the control cannot fail" >&2
        return 1
    fi
    if [ -z "$line" ]; then
        echo "  FAIL — $label: exit $rc but no matching diagnostic" >&2
        return 1
    fi
    echo "    $label: exit $rc — $line"
    return 0
}

echo
echo "  planted controls"
plants_fail=0

# 1. OVER BUDGET. submit measures 6 against a real budget of 12 and waits 240
#    frames, so +10 crosses the ceiling inside the wait window.
plant "over budget" tests/ui/interaction_latency_budgets.e2e \
    "expect_latency submit: [0-9]+ frames from input to first ink, budget [0-9]+" \
    "HANABI_LATENCY_DELAY=10" || plants_fail=1

# 2. NO OUTCOME. An input that produces nothing the arm watches.
noop="$SCRATCH/a_noop_input_has_no_ink.e2e"
cat > "$noop" <<'PLANT'
# settings: {"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}
require_thread t2
wait_frames 20
watch_ink noop ui zzz-this-ink-never-appears
click 5 5
wait_frames 12
expect_latency noop 8
PLANT
plant "no outcome" "$noop" \
    "expect_latency noop: .zzz-this-ink-never-appears. never appeared" "" \
    || plants_fail=1

# 3. PREEXISTING TARGET. The structural control: arming on something already on
#    screen is what made the first instrument report the runner's pacing.
pre="$SCRATCH/a_preexisting_target_is_refused.e2e"
cat > "$pre" <<'PLANT'
# settings: {"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}
require_thread t2
wait_frames 20
watch_ink bogus ui composer_reply_input
click_ui composer_reply_input
wait_frames 8
expect_latency bogus 8
PLANT
plant "preexisting target" "$pre" \
    "is ALREADY present" "" || plants_fail=1

# 4. MISSING ARM. The calibration's own escape hatch: an arm whose script
#    errors emits no [latency] row, and a comparison that iterated only what it
#    FOUND would quietly drop it and still pass. Plant that exact shape -- a
#    script whose watched target never appears, so `scroll` produces no
#    measurement -- and require the arm-set check to reject it by name.
echo "  planted missing-arm control"
miss_dir="$SCRATCH/missing"
mkdir -p "$miss_dir"
sed 's/watch_ink scroll ui jump_to_bottom/watch_ink scroll ui zzz-no-such-element/' \
    tests/ui/interaction_latency_scroll.e2e > "$miss_dir/interaction_latency_scroll.e2e"
rm -f /tmp/hanabi_uitest_interaction_latency_scroll.log
HANABI_UI_TESTS="$miss_dir" bash scripts/run_ui_tests.sh >/dev/null 2>&1
missing_out="$(grep -ohE "\[latency\] [a-z_]+ [0-9-]+" \
    /tmp/hanabi_uitest_interaction_latency_scroll.log 2>/dev/null \
    | awk '{print $2, $3}' | sort -u)"
if check_arm_set "missing-arm control" "$missing_out" 2>/dev/null; then
    echo "  FAIL — a run with no 'scroll' measurement passed the arm-set check" >&2
    plants_fail=1
else
    echo "    missing arm: rejected, as designed"
fi
# The real scroll log was overwritten by the plant; restore it so a later
# reader of that path is not looking at a deliberately broken run.
cp tests/ui/interaction_latency_scroll.e2e "$miss_dir/" \
    && HANABI_UI_TESTS="$miss_dir" bash scripts/run_ui_tests.sh >/dev/null 2>&1

[ "$plants_fail" -eq 0 ] || {
    echo "  FAILED: a planted control did not fail as designed." >&2; exit 1; }
echo "  PASS — every planted control fails with the right diagnostic and a nonzero exit"
