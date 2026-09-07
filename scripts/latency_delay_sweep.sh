#!/usr/bin/env bash
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SCRATCH="$(mktemp -d)"
cleanup() { rm -rf "$SCRATCH"; }
trap cleanup EXIT INT TERM

N="${HANABI_LATENCY_SWEEP_DELAY:-7}"
SCRIPTS=(interaction_latency_budgets interaction_latency_budgets_two interaction_latency_scroll)
EXPECTED_ARMS="attachment_stage click_feedback focus_gain focus_loss popup_close popup_open scroll_away scroll_back session_switch_back session_switch_forward sheet_close sheet_open submit typing_search"
EXPECTED_COUNT=14

measure() {
    local delay=$1 dir rc=0 s
    dir="$(mktemp -d "$SCRATCH/measure.XXXXXX")"
    for s in "${SCRIPTS[@]}"; do cp "tests/ui/$s.e2e" "$dir/"; done
    if [ "$delay" -ne 0 ]; then
        perl -0pi -e "s/^(expect_latency \\w+ )(\\d+)/\$1.(\$2+$delay+40)/gme" "$dir"/*.e2e
    fi
    HANABI_LATENCY_APP_DELAY_FRAMES="$delay" HANABI_UI_TESTS="$dir" \
        bash scripts/run_ui_tests.sh >/dev/null 2>&1 || rc=$?
    for s in "${SCRIPTS[@]}"; do
        grep -ohE '\[latency\] [a-z_]+ event_to_ink_us=[0-9]+ frames=[0-9]+' "/tmp/hanabi_uitest_$s.log" 2>/dev/null || true
    done | sed -E 's/^\[latency\] ([^ ]+) event_to_ink_us=([0-9]+) frames=([0-9]+)$/\1 \2 \3/'
    return "$rc"
}

check_arm_set() {
    local label=$1 data=$2 bad=0 arm count total
    total="$(printf '%s\n' "$data" | awk 'NF {n++} END {print n+0}')"
    if [ "$total" -ne "$EXPECTED_COUNT" ]; then
        echo "  FAIL - $label: measured $total arms, expected $EXPECTED_COUNT" >&2
        bad=1
    fi
    for arm in $EXPECTED_ARMS; do
        count="$(printf '%s\n' "$data" | awk -v a="$arm" '$1==a{n++} END{print n+0}')"
        if [ "$count" -ne 1 ]; then
            echo "  FAIL - $label: arm '$arm' measured $count times" >&2
            bad=1
        fi
    done
    while read -r arm _; do
        [ -n "$arm" ] || continue
        case " $EXPECTED_ARMS " in
            *" $arm "*) ;;
            *) echo "  FAIL - $label: unexpected arm '$arm'" >&2; bad=1 ;;
        esac
    done <<< "$data"
    return "$bad"
}

plant() {
    local label=$1 body=$2 want=$3 envspec=$4
    local dir name rc line
    dir="$(mktemp -d "$SCRATCH/plant.XXXXXX")"
    cp "$body" "$dir/"
    name="$(basename "$body" .e2e)"
    rm -f "/tmp/hanabi_uitest_${name}.log"
    ( eval "$envspec" HANABI_UI_TESTS="$dir" bash scripts/run_ui_tests.sh ) \
        >/dev/null 2>&1
    rc=$?
    line="$(grep -oE "$want" "/tmp/hanabi_uitest_${name}.log" 2>/dev/null | tail -1)"
    if [ "$rc" -eq 0 ] || [ -z "$line" ]; then
        echo "  FAIL - $label: exit=$rc diagnostic='${line:-missing}'" >&2
        return 1
    fi
    echo "    $label: exit $rc - $line"
}

expect_set_rejection() {
    local label=$1 data=$2
    if check_arm_set "$label" "$data" >/dev/null 2>&1; then
        echo "  FAIL - $label was accepted" >&2
        return 1
    fi
    echo "    $label: rejected"
}

echo "=== hanabi event-to-first-ink gate (app delay ${N} frames) ==="
fail=0
base="$(measure 0)" || { echo "  FAIL - baseline suite exited nonzero" >&2; fail=1; }
delayed="$(measure "$N")" || { echo "  FAIL - delayed suite exited nonzero" >&2; fail=1; }
check_arm_set baseline "$base" || fail=1
check_arm_set delayed "$delayed" || fail=1
printf '  %-24s %9s %9s %8s %12s %12s\n' arm base_f delayed_f delta base_us delayed_us
for arm in $EXPECTED_ARMS; do
    b="$(printf '%s\n' "$base" | awk -v a="$arm" '$1==a{print $2, $3}')"
    d="$(printf '%s\n' "$delayed" | awk -v a="$arm" '$1==a{print $2, $3}')"
    if [ -z "$b" ] || [ -z "$d" ]; then
        printf '  %-24s %9s %9s %8s %12s %12s  FAIL\n' "$arm" - - - - -
        fail=1
        continue
    fi
    set -- $b; bus=$1; bf=$2
    set -- $d; dus=$1; df=$2
    delta=$((df - bf))
    verdict=ok
    if [ "$delta" -ne "$N" ]; then
        verdict="FAIL"
        fail=1
    fi
    printf '  %-24s %9s %9s %+8d %12s %12s  %s\n' \
        "$arm" "$bf" "$df" "$delta" "$bus" "$dus" "$verdict"
done
[ "$fail" -eq 0 ] || exit 1

echo
echo "  planted controls"
plants_fail=0

over="$SCRATCH/interaction_latency_budgets.e2e"
sed 's/expect_latency submit 4/expect_latency submit 0/' \
    tests/ui/interaction_latency_budgets.e2e > "$over"
plant "over budget" "$over" \
    'expect_latency submit: [0-9]+ frames from event timestamp to first ink, budget 0' "" || plants_fail=1

noop="$SCRATCH/no_outcome.e2e"
cat > "$noop" <<'PLANT'
# settings: {"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}
require_thread t2
click_ui composer_reply_input
wait_frames 6
watch_ink noop ui zzz-this-outcome-never-appears
type "x"
wait_frames 12
expect_latency noop 8
PLANT
plant "no outcome" "$noop" 'first ink rendered but the required outcome did not' "" || plants_fail=1

noink="$SCRATCH/no_ink.e2e"
cat > "$noink" <<'PLANT'
# settings: {"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}
require_thread t2
mouse_move 5 5
wait_frames 8
watch_ink no_ink ui zzz-this-outcome-never-appears
click 5 5
wait_frames 12
expect_latency no_ink 8
PLANT
plant "no-op input" "$noink" 'no first ink was rendered' "" || plants_fail=1

pre="$SCRATCH/preexisting.e2e"
cat > "$pre" <<'PLANT'
# settings: {"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}
require_thread t2
wait_frames 8
watch_ink preexisting ui composer_reply_input
PLANT
plant "preexisting target" "$pre" 'is ALREADY present' "" || plants_fail=1

dup="$SCRATCH/duplicate.e2e"
cat > "$dup" <<'PLANT'
# settings: {"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}
require_thread t2
watch_ink duplicate focus composer_reply_input
click_ui composer_reply_input
wait_frames 6
expect_latency duplicate 4
watch_ink duplicate blur composer_reply_input
PLANT
plant "duplicate arm" "$dup" "duplicate arm 'duplicate'" "" || plants_fail=1

missing="$(printf '%s\n' "$base" | awk '$1!="scroll_back"')"
duplicate="$base
$(printf '%s\n' "$base" | awk '$1=="submit"{print; exit}')"
extra="$base
extra_arm 1 1"
expect_set_rejection "missing arm" "$missing" || plants_fail=1
expect_set_rejection "duplicate measurement" "$duplicate" || plants_fail=1
expect_set_rejection "extra arm" "$extra" || plants_fail=1

if strings output/hanabi.exe | grep -Eq 'HANABI_LATENCY_APP_DELAY_FRAMES|event_to_ink_us=|ink_gained=|required_direction=|watch_ink requires'; then
    echo "  FAIL - latency instrumentation strings leaked into the shipping binary" >&2
    plants_fail=1
else
    echo "    shipping binary: no latency instrumentation strings"
fi
if nm -j output/hanabi.exe 2>/dev/null | c++filt | grep -Eq 'hanabi::latency::'; then
    echo "  FAIL - latency instrumentation symbols leaked into the shipping binary" >&2
    plants_fail=1
else
    echo "    shipping binary: no latency instrumentation symbols"
fi

[ "$plants_fail" -eq 0 ] || exit 1
echo "  PASS - exact 14-arm set, real app-delay shift, failure exits, and shipping elimination"
