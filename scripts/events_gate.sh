#!/usr/bin/env bash
# scripts/events_gate.sh — what a transcript costs PER MESSAGE, on a thread
# that has the row kinds a real session actually emits.
#
# ---------------------------------------------------------------------------
# WHY THIS GATE EXISTS, which is the only interesting thing about it.
#
# `feat/event-model` added six row kinds to the transcript — thinking, tool
# call, sub-agent, delivery, node, skill — where before there were four roles.
# Every gate in `make test` was green before it landed and green after, and
# every count they report was IDENTICAL to the unit across the merge:
#
#     arm             before      after
#     home20           810.0      810.0     allocations/frame
#     home2000        1162.0     1162.0
#     thread480       2740.0     2740.0
#     scroll gate      327/453    327/453   entities, 20 vs 2000 sessions
#     scaling gate     320/426    320/426   widgets
#     retire gate      221/212    221/212   live / built
#     digest gate      13/506     13/506    cards built / matched
#
# Not because the event model is free. Because **no fixture in this repo could
# produce one of those rows.** `stress_turn` (the synthetic catalog) leaves
# `Message::kind` at its `Text` default, and the `rbig` long-transcript fixture
# emits User / Assistant / Tool / Tool and no thinking row at all. The five
# event rows that DO exist live in one hand-written mock thread (`r8`), which
# nothing measures. So the gates were reading a transcript with zero of the new
# kinds in it, correctly, and reporting no change.
#
# That is `docs/perf/STRESS.md`'s own finding arriving a second time: *"the
# synthetic stress catalog rendered the cheapest path this app has."* It was
# fixed once, for tool rows and code fences, in `stress_turn`. The fixture it
# was NOT fixed in is the one the per-message gate uses.
#
# ---------------------------------------------------------------------------
# THE FIRST ARM IS "DID IT DRAW ANY", and it is first for that reason. A gate
# whose fixture produces none of the thing it measures cannot fail, and the
# four such gates `docs/perf/GATES.md` §0 found were all found by luck. This
# one asks the app, through `HANABI_PROF=1` gauges the transcript publishes
# (`items.event`, `items.delivery`, `items.spawn`, `items.thinking`), and
# fails loudly at zero rather than reporting a clean per-message cost for a
# thread with nothing in it.
#
# LEVEL AND TREND, BOTH, per `docs/perf/GATES.md` §0's four sleeping gates: a
# defect that costs from the first message has no slope, and a defect that
# grows has no level worth gating at the short arm. Three properties, each
# read twice:
#
#   marks     the rail's mark count      LEVEL only — it is a bound, not a rate
#   widgets   entities holding a UIComponent  LEVEL at the long arm + per-turn
#   allocs    operator new per frame          LEVEL at the long arm + per-turn
#
# Counts, never milliseconds: this box is shared and its load average has been
# observed at 134. Every number below repeats to the unit between runs.
#
# ---------------------------------------------------------------------------
# WHAT SET THE CEILINGS. Measured 2026-08-26 on gabeochoa-mac-GRQ7Y259H4,
# branch perf/post-merge, 1180x949, 400 frames, three runs each, zero spread:
#
#     turns      items   marks   widgets   allocs/f
#        15         68      68       323     2326.0
#       240       1123     241       498     2362.0
#
# and against the two defects this gate is for, same fixture:
#
#     the rail ungrouped (minimap::group_marks' guard forced true)
#       minimap marks       69   1121    400   FAIL
#       widgets, level     322   1378    700   FAIL
#         ...per turn        -   4.69    2.0   FAIL
#       allocs/frame      2326   2365   2900     ok   <- and correctly so:
#     the rail's cost is widgets and CPU, not malloc traffic, and a gate that
#     went red on every arm would be telling you less rather than more.
#
#     the fixture eventless (the HANABI_BIG_EVENTS lambda forced false)
#       items built   event 0  deliv 0  spawn 0  think 0  -> arm 0 FAILs and
#     the run stops there, because every number under it would have been a
#     clean per-message cost for a transcript with no event rows in it.
#
# ~20-30% of headroom on each ceiling, which is not for noise — there is none —
# but for a row kind that legitimately adds a widget.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2
EXE="$ROOT/output/hanabi.exe"

SHORT_TURNS="${HANABI_EVENTS_SHORT_TURNS:-15}"
LONG_TURNS="${HANABI_EVENTS_LONG_TURNS:-240}"
FRAMES="${HANABI_EVENTS_FRAMES:-400}"
# Two buckets, so the reading is the SECOND one. The allocation counter is a
# per-bucket average and the first bucket carries the lazy-init a fresh
# process pays once: at one bucket of 400 the long arm reads 3225.5 and at the
# second bucket of 200 it reads 2362.0, and only the second is what the frame
# costs forever.
BUCKET="$(( FRAMES / 2 ))"

CEIL_MARKS="${HANABI_EVENTS_CEIL_MARKS:-400}"
CEIL_WIDGETS="${HANABI_EVENTS_CEIL_WIDGETS:-700}"
CEIL_ALLOCS="${HANABI_EVENTS_CEIL_ALLOCS:-2900}"
CEIL_WIDGET_SLOPE="${HANABI_EVENTS_CEIL_WIDGET_SLOPE:-2.0}"
CEIL_ALLOC_SLOPE="${HANABI_EVENTS_CEIL_ALLOC_SLOPE:-2.0}"

SHOT="$(mktemp -t hanabi_events_XXXX).png"
LOG="$(mktemp -t hanabi_events_XXXX).log"
HOMEDIR="$(mktemp -d /tmp/hanabi_events_home.XXXXXX)"
mkdir -p "$HOMEDIR/Library/Application Support/hanabi"
RUN_TIMEOUT=180

# Scoped to THIS worktree's binary path, not the name: several checkouts test
# on this machine at once and a bare `pkill -f hanabi.exe` kills their runs.
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -f "$SHOT" "$LOG"; rm -rf "$HOMEDIR"; }
trap cleanup EXIT

if [ ! -x "$EXE" ]; then
    echo "events_gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

run() {  # $1 = turns; leaves output in $LOG
    env HOME="$HOMEDIR" HANABI_WIN_W=1180 HANABI_WIN_H=949 \
        HANABI_BACKEND=mock HANABI_CONFIG=/nonexistent/hanabi/events-gate.json \
        HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_TURNS="$1" HANABI_BIG_EVENTS=1 \
        HANABI_OPEN=rbig HANABI_PROF=1 HANABI_STRESS=idle \
        HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY="$BUCKET" \
        HANABI_SOAK_CENSUS=1 \
        timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$SHOT" >"$LOG" 2>&1
}

gauge() { grep -E "^\[prof\] $1 " "$LOG" | awk '{print $NF}' | tail -1; }
widgets() { grep -oE 'widgets: [0-9]+' "$LOG" | tail -1 | awk '{print $2}'; }
allocs() {
    grep -oE 'allocs +[0-9.]+ /f' "$LOG" | tail -1 | awk '{print $2}'
}

echo "=== hanabi event-transcript gate ==="
echo "  the rbig fixture with HANABI_BIG_EVENTS=1: thinking, tool calls,"
echo "  sub-agents, deliveries, nodes, skills — the mix one real session read"
echo "  (32 Text · 13 Thinking · 68 ToolCall · 6 SubAgent · 2 Delivery)."
echo "  docs/perf/EVENTS.md"

run "$SHORT_TURNS"
S_W="$(widgets)"; S_A="$(allocs)"; S_ITEMS="$(gauge items.total)"
S_MARKS="$(gauge minimap.marks)"
run "$LONG_TURNS"
L_W="$(widgets)"; L_A="$(allocs)"; L_ITEMS="$(gauge items.total)"
L_MARKS="$(gauge minimap.marks)"
L_EV="$(gauge items.event)"; L_DE="$(gauge items.delivery)"
L_SP="$(gauge items.spawn)"; L_TH="$(gauge items.thinking)"

# A run that produced no numbers is not a pass and not a failure — it is a
# killed or crashed process, and saying so is the difference between a
# re-run and a bisect. (docs/perf/GATES.md: three outcomes, not two.)
if [ -z "${S_W:-}" ] || [ -z "${L_W:-}" ] || [ -z "${S_A:-}" ] || \
   [ -z "${L_A:-}" ] || [ -z "${L_MARKS:-}" ]; then
    echo "  INCOMPLETE: the app produced no [soak]/[prof] reading." >&2
    echo "        That is a crash or a killed run, not a regression. On this" >&2
    echo "        machine the usual cause is another worktree:" >&2
    echo "        scripts/review_shots.sh kills output/hanabi.exe in EVERY" >&2
    echo "        worktree it finds. Last 20 lines:" >&2
    tail -20 "$LOG" | sed 's/^/        /' >&2
    exit 2
fi

FAIL=0

# ---- arm 0: did the fixture draw any of them at all? -----------------------
printf '  %-22s %8s %8s %8s %8s\n' "row kind (long arm)" "event" "deliv" "spawn" "think"
printf '  %-22s %8s %8s %8s %8s\n' "items built" \
    "${L_EV:-0}" "${L_DE:-0}" "${L_SP:-0}" "${L_TH:-0}"
if [ "${L_EV:-0}" -eq 0 ] || [ "${L_DE:-0}" -eq 0 ] || \
   [ "${L_SP:-0}" -eq 0 ] || [ "${L_TH:-0}" -eq 0 ]; then
    echo "" >&2
    echo "  FAIL: the fixture drew none of a row kind this gate measures." >&2
    echo "        Nothing below this line means anything: a per-message cost" >&2
    echo "        measured over a transcript with no event rows in it is the" >&2
    echo "        exact defect this gate was written for (see the header)." >&2
    echo "        Look at MockClient::build_seed's rbig block and its" >&2
    echo "        HANABI_BIG_EVENTS branch, and at kFixtureEnv — a knob the" >&2
    echo "        seed reads and that list does not name is cached away on" >&2
    echo "        the first call and never read again." >&2
    exit 1
fi

echo ""
printf '  %-22s %10s %10s %10s %8s\n' "turns" "$SHORT_TURNS" "$LONG_TURNS" "ceiling" "verdict"
printf '  %-22s %10s %10s %10s\n' "items" "${S_ITEMS:-?}" "${L_ITEMS:-?}" "-"

# ---- arm 1: the rail's mark count is a BOUND, not a rate -------------------
# It cannot be a per-message number: the point of hanabi::minimap::group_marks
# is that the count stops tracking the thread and starts tracking the RAIL.
v=ok
if [ "${L_MARKS:-0}" -gt "$CEIL_MARKS" ]; then v=FAIL; FAIL=1; fi
printf '  %-22s %10s %10s %10s %8s\n' "minimap marks" "${S_MARKS:-?}" "$L_MARKS" "$CEIL_MARKS" "$v"
if [ "$v" = FAIL ]; then
    echo "" >&2
    echo "  FAIL: the rail drew $L_MARKS marks for $L_ITEMS items, over a ceiling of $CEIL_MARKS." >&2
    echo "        A rail cannot show more marks than it has pixels: at" >&2
    echo "        kMinDotH=2 every dot past railH/2 is drawn on top of its" >&2
    echo "        neighbour, so the minimap paints a solid stripe and stops" >&2
    echo "        being a map — and each one is a button entity rebuilt every" >&2
    echo "        frame with its own std::to_string debug name." >&2
    echo "        Look at hanabi::minimap::group_marks and its guard." >&2
fi

# ---- arm 2: widgets, level and per-turn ------------------------------------
W_SLOPE="$(awk "BEGIN{printf \"%.2f\", ($L_W-$S_W)/($LONG_TURNS-$SHORT_TURNS)}")"
v=ok
if [ "$L_W" -gt "$CEIL_WIDGETS" ]; then v=FAIL; FAIL=1; fi
printf '  %-22s %10s %10s %10s %8s\n' "widgets, level" "$S_W" "$L_W" "$CEIL_WIDGETS" "$v"
v=ok
if awk "BEGIN{exit !($W_SLOPE > $CEIL_WIDGET_SLOPE)}"; then v=FAIL; FAIL=1; fi
printf '  %-22s %10s %10s %10s %8s\n' "  ...per turn" "-" "$W_SLOPE" "$CEIL_WIDGET_SLOPE" "$v"

# ---- arm 3: allocations, level and per-turn --------------------------------
A_SLOPE="$(awk "BEGIN{printf \"%.2f\", ($L_A-$S_A)/($LONG_TURNS-$SHORT_TURNS)}")"
v=ok
if awk "BEGIN{exit !($L_A > $CEIL_ALLOCS)}"; then v=FAIL; FAIL=1; fi
printf '  %-22s %10s %10s %10s %8s\n' "allocs/frame, level" "$S_A" "$L_A" "$CEIL_ALLOCS" "$v"
v=ok
if awk "BEGIN{exit !($A_SLOPE > $CEIL_ALLOC_SLOPE)}"; then v=FAIL; FAIL=1; fi
printf '  %-22s %10s %10s %10s %8s\n' "  ...per turn" "-" "$A_SLOPE" "$CEIL_ALLOC_SLOPE" "$v"

if [ "$FAIL" -ne 0 ]; then
    echo "" >&2
    echo "  A per-turn number over budget with a flat level is a cost that" >&2
    echo "  GROWS with the thread — a mark, a widget or a string minted per" >&2
    echo "  message and never windowed (afterhours_gaps.md #138). A level over" >&2
    echo "  budget with a flat per-turn number is a cost that arrived and" >&2
    echo "  stayed, which every slope gate in this repo reads as perfectly" >&2
    echo "  fine. They are different bugs; the table says which." >&2
    echo "  ---------------- EVENT GATE: FAIL ----------------" >&2
    exit 1
fi
echo "  PASS"
