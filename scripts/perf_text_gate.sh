#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# A gate on how many times the app MEASURES TEXT, and on how big the caches
# that stop it get.
#
# WHY A SEPARATE GATE. scripts/perf_transcript_slope.sh gates the cost of a
# LONGER THREAD and the soak/scaling gates the cost of a longer run and a
# bigger catalog. None of them can see the failure this branch was about,
# which is orthogonal to all three: the app measuring the SAME text over and
# over within one frame. A memo that stops working does not change the shape
# of any curve those gates plot -- it raises a constant -- and the transcript
# memo sat at a 34% hit rate for months with every gate green.
#
# EVERYTHING HERE IS A COUNT OR A RATIO. This box is shared and its load
# average has been observed at 29; a millisecond measured on it is a
# measurement of the machine (docs/perf/GATES.md). The counts below are
# identical to the CALL between runs -- verified by running each size twice
# and diffing -- which is what lets a ceiling be tight instead of decorative.
#
# THE THREE THINGS IT HOLDS
#
#   1. measurements per frame, and their SLOPE in thread length. A transcript
#      standing still should measure almost nothing, and a longer one should
#      not measure more: both passes are memoized and the render pass only
#      builds what is on screen.
#   2. the HIT RATE of each memo. A cache that misses because it is cold is
#      working; a cache that misses because it evicted itself is worse than no
#      cache, and only the rate tells them apart.
#   3. the BOUND on each cache, in the running app. The unit test
#      (tests/unit/test_text_cache.cpp) proves the eviction works; this proves
#      the app never asks for more than the budget it was sized for. The
#      ceilings below MUST match the constants in the source -- they are
#      named beside each one.
#
# Usage: scripts/perf_text_gate.sh [path-to-hanabi.exe]
# ---------------------------------------------------------------------------
set -u

EXE="${1:-output/hanabi.exe}"
SHORT_TURNS=15    # 60 messages
LONG_TURNS=120    # 480 messages
# A third arm, and it is short because it exists for ONE assertion. The bound
# arm below is worthless if it runs at a size where the cap is not reached: at
# 480 messages the line-count memo holds 488 of its 512, so deleting the
# eviction entirely leaves the gate GREEN -- checked, by deleting it. 1,200
# messages engages every cap, so the arm that claims the caps hold is run
# somewhere they are actually doing something.
HUGE_TURNS=300    # 1200 messages
HUGE_FRAMES=150
SHORT_MSGS=$((SHORT_TURNS * 4))
LONG_MSGS=$((LONG_TURNS * 4))
FRAMES=300

if [ ! -x "$EXE" ]; then
    echo "perf_text_gate: no binary at $EXE" >&2
    exit 1
fi

run_ask() {
    local h
    h=$(mktemp -d)
    mkdir -p "$h/Library/Application Support/hanabi"
    cat > "$h/Library/Application Support/hanabi/settings.json" <<J
{"window_width":1180,"window_height":949,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}
J
    env HOME="$h" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
        HANABI_CONFIG=/tmp/none HANABI_ASK_DEMO=big HANABI_PROF=1 \
        HANABI_SOAK="${FRAMES}" HANABI_STRESS=idle \
        "$EXE" --screenshot "$h/shot.png" 2>&1 | grep '^\[prof\]'
    rm -rf "$h"
}

run() {
    local turns=$1 h
    h=$(mktemp -d)
    mkdir -p "$h/Library/Application Support/hanabi"
    cat > "$h/Library/Application Support/hanabi/settings.json" <<J
{"window_width":1180,"window_height":949,"open_tabs":["rbig"],"active_tab":"rbig","theme":"dark"}
J
    local frames=${FRAMES}
    env HOME="$h" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
        HANABI_CONFIG=/tmp/none HANABI_BIG_TRANSCRIPT=1 \
        HANABI_BIG_TURNS="$turns" HANABI_PROF=1 HANABI_SOAK="$frames" \
        HANABI_STRESS=idle \
        "$EXE" --screenshot "$h/shot.png" 2>&1 | grep '^\[prof\]'
    rm -rf "$h"
}

# field <out> <row match> <awk field> -- 0 when the row is absent, because a
# counter that never fired prints no row and that is a legitimate zero.
field() {
    local v
    v=$(printf '%s\n' "$1" | awk -v m="$2" -v f="$3" '$0 ~ m {print $f; exit}')
    printf '%s' "${v:-0}"
}

echo "=== text measurement gate ==="
echo "    $SHORT_MSGS messages vs $LONG_MSGS messages, $FRAMES frames each"

SHORT_OUT=$(run "$SHORT_TURNS")
LONG_OUT=$(run "$LONG_TURNS")
HUGE_OUT=$(FRAMES=$HUGE_FRAMES run "$HUGE_TURNS")

if [ -z "$SHORT_OUT" ] || [ -z "$LONG_OUT" ]; then
    echo "  FAIL: no [prof] output -- did the run finish? A killed process is" >&2
    echo "        not a measurement (docs/perf/GATES.md)." >&2
    exit 1
fi

fail=0

# --- 1. measurements per frame, and the slope -----------------------------
#
# Two counters, added, because they are the two ways this app measures text:
# afterhours' TextMeasureCache (every lookup, hit or miss -- a hit is still a
# hash over a string that had to be built) and theme::text_px's own fontstash
# calls. Before this branch the pair read 474.3 + 17.1 per idle frame at 120
# messages.
measures_per_frame() {  # <out>
    local tm px
    tm=$(printf '%s\n' "$1" | awk '/TextMeasureCache:/ {print $3 + $6; exit}')
    px=$(field "$1" 'text\.text_px_uncached' 4)
    awk -v t="${tm:-0}" -v p="$px" -v f="$FRAMES" \
        'BEGIN{printf "%.2f", t / f + p}'
}
S_M=$(measures_per_frame "$SHORT_OUT")
L_M=$(measures_per_frame "$LONG_OUT")
SLOPE=$(awk -v s="$S_M" -v l="$L_M" -v d="$((LONG_MSGS - SHORT_MSGS))" \
            'BEGIN{printf "%.4f", (l - s) / d}')

# 20.0 against a measured 7.0 at 480 messages. Loose enough that adding a
# widget or a label does not trip it, tight enough that any RETURN of
# per-paragraph measurement -- the smallest of which was 61.8 a frame -- is
# caught immediately.
CEIL=20.0
verdict="ok"
awk -v x="$L_M" -v m="$CEIL" 'BEGIN{exit !(x > m)}' && { verdict="FAIL"; fail=1; }
printf "  %-30s %8s -> %-8s                    limit %6s   %s\n" \
    "measures/frame" "$S_M" "$L_M" "$CEIL" "$verdict"

# 0.05 per message against a measured 0.0157 at this window size. Most of
# that 0.0157 is not a slope at all -- it is the ONE-TIME cold measurement of
# 420 more messages, divided by the frames in the window -- and it shrinks as
# the window grows: the identical binary reads 0.0052 at 900 frames. 300
# frames is what the gate can afford (docs/perf/GATES.md: a suite people stop
# running is worth nothing), so the limit is set for 300. A per-message
# measurement that escaped the memo would read at least 1.0 here, twenty times
# over.
ASK_OUT=$(run_ask)
ASK_CARDS=$(field "$ASK_OUT" 'ask\.cards_drawn' 3)
ASK_MISS=$(field "$ASK_OUT" 'cache\.ask_spans_miss' 3)
ASK_HIT=$(field "$ASK_OUT" 'cache\.ask_spans_hit' 3)
ask_verdict="ok"
if [ "${ASK_CARDS:-0}" -lt 1 ]; then
    ask_verdict="NOT MEASURED"
    fail=1
elif awk -v m="$ASK_MISS" -v c="$ASK_CARDS" 'BEGIN{exit !(m+0 > c+0)}'; then
    ask_verdict="FAIL"
    fail=1
fi
printf "  %-30s %8s -> %-8s                    limit %6s   %s\n" \
    "ask card wraps (miss/hit)" "$ASK_MISS" "$ASK_HIT" "<=cards" "$ask_verdict"

SLOPE_LIMIT=0.05
verdict="ok"
awk -v x="$SLOPE" -v m="$SLOPE_LIMIT" 'BEGIN{exit !(x > m)}' && { verdict="FAIL"; fail=1; }
printf "  %-30s %8s per message                       limit %6s   %s\n" \
    "  ...slope" "$SLOPE" "$SLOPE_LIMIT" "$verdict"

# --- 2. hit rates ---------------------------------------------------------
rate_gate() {  # <label> <out> <hit-row> <miss-row> <floor>
    local label=$1 out=$2 hrow=$3 mrow=$4 floor=$5
    local h m r v
    h=$(field "$out" "$hrow" 4)
    m=$(field "$out" "$mrow" 4)
    r=$(awk -v h="$h" -v m="$m" 'BEGIN{t=h+m; x=100; if (t>0) x=100*h/t; printf "%.2f", x}')
    v="ok"
    awk -v x="$r" -v f="$floor" 'BEGIN{exit !(x < f)}' && { v="FAIL"; fail=1; }
    printf "  %-30s %8s hit / %-8s miss = %6s%%  limit %6s   %s\n" \
        "$label" "$h" "$m" "$r" "$floor" "$v"
}
rate_gate "line-count memo @ $SHORT_MSGS" "$SHORT_OUT" 'cache\.lines_hit' 'cache\.lines_miss' 95.0
rate_gate "line-count memo @ $LONG_MSGS" "$LONG_OUT" 'cache\.lines_hit' 'cache\.lines_miss' 95.0
rate_gate "advance memo @ $LONG_MSGS" "$LONG_OUT" 'cache\.advance_hit' 'text\.text_px_uncached' 95.0

# --- 3. the bounds, in the running app, at a size that ENGAGES them --------
#
# The peak entry count of each cache against the cap it was sized with. These
# ceilings are the CONSTANTS in the source; if one is raised there and not
# here, this fails, which is the point -- a bound that lives in one file and
# is argued for in another has a way of drifting.
bound_gate() {  # <label> <gauge-row> <cap> <where>
    local label=$1 row=$2 cap=$3 where=$4 peak v
    peak=$(field "$HUGE_OUT" "$row" 3)
    v="ok"
    awk -v x="$peak" -v c="$cap" 'BEGIN{exit !(x > c)}' && { v="FAIL"; fail=1; }
    printf "  %-30s %8s entries peak                      cap  %6s   %s   (%s)\n" \
        "$label" "$peak" "$cap" "$v" "$where"
}
bound_gate "line-count memo bound" 'cache\.lines_entries' 512 "kLineCountEntries"
bound_gate "advance memo bound" 'cache\.advance_entries' 1024 "kAdvanceEntries"
bound_gate "ellipsis memo bound" 'cache\.fit_entries' 512 "kFitEntries"

if [ "$fail" -ne 0 ]; then
    echo "  text measurement gate: FAIL"
    exit 1
fi
echo "  text measurement gate: PASS"
