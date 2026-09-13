#!/usr/bin/env bash
# scripts/viewport_bound_gate.sh -- the transcript's per-frame layout work is
# bounded by the viewport, not by the thread.
#
# What runs: output/hanabi_uitest.exe on tests/perf/viewport_bound_stimulus.e2e
# (jump to top, back, page up, three resizes while scrolled up, a streamed
# reply while scrolled up, wheel both ways, resize while following) with
# HANABI_PROF=1, on the heterogeneous fixture (HANABI_BIG_EVENTS=1: bubbles,
# thinking, tool piles, sub-agents, deliveries, events) at 300, 3,000 and
# 30,000 messages, same window, same script.
#
# What is asserted, all counts from the [prof] gauges (high-water marks):
#   bound        ledger.frame_visited_max, frame_measured_max, frame_built_max
#                and minimap.frame_probes_max at 30,000 messages are within
#                kSlack of the same at 300: the visible content is the same
#                rows, so the work must be the same work. A reinstated
#                whole-thread pass fails this by two orders of magnitude.
#   did run      every one of those is > 0 at every length, ledger.rows is the
#                whole thread, and ledger.reindexed is the two O(N) metadata
#                events the product has -- the open (newest 40) and the one
#                load-older that prepends the rest when the reader nears the
#                top -- and nothing more: zero work reads as "did not run",
#                not as "free"; extra reindexing reads as a rebuild.
#   honest       ledger.unfilled is absent: no frame ended with an estimate
#                inside the viewport.
#   fail closed  a non-zero exit, a missing prof table or a missing gauge is a
#                FAIL, not a skip.
# It PRINTS the per-length numbers so a regression is legible, and it does
# not time anything: the box is shared.
#
# Planted control: HANABI_LEDGER_EAGER=1 makes the app measure EVERY row every
# frame (the shape this replaced); `--selftest` runs that arm and requires the
# gate to FAIL its bound on it.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_VIEWPORT_GATE_EXE:-$ROOT/output/hanabi_uitest.exe}"
SCRIPT="$ROOT/tests/perf/viewport_bound_stimulus.e2e"
if [ ! -x "$EXE" ]; then
    echo "viewport_bound_gate: $EXE not found -- run 'zig build uitest-build' first." >&2
    exit 2
fi
# Slack for the bound: the window holds a few more or fewer rows when a
# different row happens to straddle its edge, and the streamed reply lands on
# different rows. Generous against noise, hopeless against a full pass.
SLACK_ROWS=${HANABI_VIEWPORT_GATE_SLACK:-24}
TURNS=(75 750 7500)   # ~575 / 5,736 / 57,345 messages with HANABI_BIG_EVENTS=1
selftest=0
if [ "${1:-}" = "--selftest" ]; then
    selftest=1
    echo "  selftest: measuring every row every frame must fail the bound"
    export HANABI_LEDGER_EAGER=1
    TURNS=(75 750)
fi
declare -A VIS MEAS BUILT PROBES ROWS REIDX RECAL UNFILLED
overall=0
gauge() { grep -oE "\[prof\] $1 +[0-9]+" "$2" | awk '{print $3}' | head -1; }
for turns in "${TURNS[@]}"; do
    H="$(mktemp -d)"
    mkdir -p "$H/Library/Application Support/hanabi" "$H/cache"
    printf '{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}\n' \
        > "$H/Library/Application Support/hanabi/settings.json"
    LOG="$H/run.log"
    env HOME="$H" HANABI_CONFIG="$H/no-such-config.json" HANABI_CACHE_DIR="$H/cache" \
        TZ=UTC HANABI_MOCK_NOW=1781524800 HANABI_TOKEN_FILE="$H/token.json" \
        HANABI_BACKEND=mock HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_EVENTS=1 \
        HANABI_BIG_TURNS="$turns" HANABI_OPEN=rbig \
        HANABI_PROF=1 timeout 300 "$EXE" --e2e "$SCRIPT" > "$LOG" 2>&1
    RC=$?
    n=$(gauge 'ledger\.rows' "$LOG")
    echo "  --- HANABI_BIG_TURNS=$turns (messages=${n:-?}) rc=$RC"
    if [ "$RC" -ne 0 ]; then echo "  FAIL exit $RC"; sed -n '/E2E ERROR\|TIMEOUT\|FAIL/p' "$LOG" | head -5 | sed 's/^/        /'; overall=1; rm -rf "$H"; continue; fi
    if ! grep -q '^\[prof\]' "$LOG"; then echo "  FAIL no prof table"; overall=1; rm -rf "$H"; continue; fi
    VIS[$turns]=$(gauge 'ledger\.frame_visited_max' "$LOG")
    MEAS[$turns]=$(gauge 'ledger\.frame_measured_max' "$LOG")
    BUILT[$turns]=$(gauge 'ledger\.frame_built_max' "$LOG")
    PROBES[$turns]=$(gauge 'minimap\.frame_probes_max' "$LOG")
    ROWS[$turns]=$n
    REIDX[$turns]=$(grep -oE '\[prof\] ledger\.reindexed +[0-9]+' "$LOG" | awk '{print $3}' | head -1)
    RECAL[$turns]=$(grep -oE '\[prof\] ledger\.recalibrated +[0-9]+' "$LOG" | awk '{print $3}' | head -1)
    UNFILLED[$turns]=$(grep -oE '\[prof\] ledger\.unfilled +[0-9]+' "$LOG" | awk '{print $3}' | head -1)
    printf '  %-14s visited_max=%s measured_max=%s built_max=%s minimap_probes_max=%s\n' bound "${VIS[$turns]:-none}" "${MEAS[$turns]:-none}" "${BUILT[$turns]:-none}" "${PROBES[$turns]:-none}"
    printf '  %-14s rows=%s reindexed=%s recalibrated=%s unfilled=%s\n' ledger "${ROWS[$turns]:-none}" "${REIDX[$turns]:-none}" "${RECAL[$turns]:-none}" "${UNFILLED[$turns]:-0}"
    printf '  %-14s %s\n' frames "$(grep -oE '\[prof\] [0-9]+ frames' "$LOG" | head -1)"
    for v in "${VIS[$turns]}" "${MEAS[$turns]}" "${BUILT[$turns]}" "${PROBES[$turns]}"; do
        if [ -z "$v" ] || [ "$v" -le 0 ]; then echo "  FAIL did-run: a bound gauge is missing or zero"; overall=1; fi
    done
    [ -n "$n" ] && [ "$n" -ge $((turns * 4)) ] || { echo "  FAIL did-run: ledger.rows=${n:-none} under $((turns * 4))"; overall=1; }
    # open (<= 40 rows) + one full prepend (n rows), and nothing else.
    [ -n "${REIDX[$turns]}" ] && [ "${REIDX[$turns]}" -ge "$n" ] && [ "${REIDX[$turns]}" -le $((n + 48)) ] \
        || { echo "  FAIL reindex: ${REIDX[$turns]:-none} rows reindexed against $n messages (must be the open + one load-older)"; overall=1; }
    # Estimates are rescaled on those same frames and no others: at most
    # twice the thread (open + prepend), never zero.
    [ -n "${RECAL[$turns]}" ] && [ "${RECAL[$turns]}" -ge 1 ] && [ "${RECAL[$turns]}" -le $((2 * n + 96)) ] \
        || { echo "  FAIL recalibrate: ${RECAL[$turns]:-none} rows rescaled against $n messages (must be the open + one load-older)"; overall=1; }
    [ -z "${UNFILLED[$turns]}" ] || { echo "  FAIL honest: ${UNFILLED[$turns]} frames ended with an estimate in the viewport"; overall=1; }
    rm -rf "$H"
done
small=${TURNS[0]}; big=${TURNS[${#TURNS[@]}-1]}
if [ -n "${VIS[$small]:-}" ] && [ -n "${VIS[$big]:-}" ]; then
    within() { local a=$1 b=$2; [ $((a > b ? a - b : b - a)) -le "$SLACK_ROWS" ]; }
    within "${VIS[$big]}" "${VIS[$small]}"   || { echo "  FAIL bound: visited_max grew $small->$big turns: ${VIS[$small]} -> ${VIS[$big]}"; overall=1; }
    within "${MEAS[$big]}" "${MEAS[$small]}" || { echo "  FAIL bound: measured_max grew: ${MEAS[$small]} -> ${MEAS[$big]}"; overall=1; }
    within "${BUILT[$big]}" "${BUILT[$small]}" || { echo "  FAIL bound: built_max grew: ${BUILT[$small]} -> ${BUILT[$big]}"; overall=1; }
    # The rail's probes are bounded by the rail (cap x probes), not the thread.
    [ "${PROBES[$big]}" -le $(( ${PROBES[$small]} * 2 + 1600 )) ] || { echo "  FAIL bound: minimap probes grew with the thread: ${PROBES[$small]} -> ${PROBES[$big]}"; overall=1; }
fi
if [ "$selftest" = 1 ]; then
    if [ "$overall" -ne 0 ]; then echo "  selftest PASS: the planted full pass was caught"; exit 0; fi
    echo "  selftest FAIL: a full pass every frame passed the gate"; exit 1
fi
if [ "$overall" -ne 0 ]; then echo "  viewport-bound gate: FAIL"; exit 1; fi
echo "  viewport-bound gate: PASS"
