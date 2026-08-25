#!/bin/bash
# ---------------------------------------------------------------------------
# A gate on the SLOPE, because a gate on a total would not have caught this.
#
# The bug this exists to prevent: the transcript's measure pass re-measuring
# EVERY message in the thread on EVERY frame, when the memo that was supposed
# to make that free had a 34% hit rate. Nothing in the suite could see it.
# `make test` renders 45 frames of a 20-message mock; measure_launch.sh gates
# the first frame and peak RSS of a process under a second old. Both are
# budgets on a TOTAL, and a total on a short thread is exactly where a
# per-message cost is invisible -- 12 messages hid a cost that made 480
# messages allocate ninety thousand times a frame.
#
# So this measures the same work at two transcript lengths and gates the
# DIFFERENCE. A per-message cost shows up as a slope whatever the constant is,
# and a machine that is slow today moves both ends together.
#
# It gates COUNTS, not milliseconds, and that is deliberate. Measured on this
# laptop under load average 29, wall-clock frame time on the 480-message
# fixture read 4.5 ms at its best bucket and 10.6 ms at its median, and an A/B
# of a fast and a slow binary came out backwards. Counts are identical run to
# run on any machine at any load, so this gate can be believed on a laptop
# that is also building something else -- which is the only condition it will
# ever run under.
#
# Usage: scripts/perf_transcript_slope.sh [path-to-hanabi.exe]
# ---------------------------------------------------------------------------
set -u

EXE="${1:-output/hanabi.exe}"
SHORT_TURNS=15    # 60 messages
LONG_TURNS=120    # 480 messages
SHORT_MSGS=$((SHORT_TURNS * 4))
LONG_MSGS=$((LONG_TURNS * 4))
FRAMES=300

if [ ! -x "$EXE" ]; then
    echo "perf_transcript_slope: no binary at $EXE" >&2
    exit 1
fi

run() {  # run <turns> -> prof lines on stdout
    local turns=$1
    local h
    h=$(mktemp -d)
    mkdir -p "$h/Library/Application Support/hanabi"
    cat > "$h/Library/Application Support/hanabi/settings.json" <<J
{"window_width":1180,"window_height":949,"open_tabs":["rbig"],"active_tab":"rbig","theme":"dark"}
J
    env HOME="$h" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
        HANABI_CONFIG=/tmp/none HANABI_BIG_TRANSCRIPT=1 \
        HANABI_BIG_TURNS="$turns" HANABI_PROF=1 HANABI_SOAK="$FRAMES" \
        HANABI_STRESS=idle \
        "$EXE" --screenshot "$h/shot.png" 2>&1 | grep '^\[prof\]'
    rm -rf "$h"
}

# field(<prof output>, <row match>, <awk field>) -- blank when the row is absent
field() { printf '%s\n' "$1" | awk -v m="$2" -v f="$3" '$0 ~ m {print $f; exit}'; }

echo "=== transcript slope gate ==="
echo "    $SHORT_MSGS messages vs $LONG_MSGS messages, $FRAMES frames each"

SHORT_OUT=$(run "$SHORT_TURNS")
LONG_OUT=$(run "$LONG_TURNS")

if [ -z "$SHORT_OUT" ] || [ -z "$LONG_OUT" ]; then
    echo "  FAIL: no [prof] output -- is HANABI_PROF wired up?" >&2
    exit 1
fi

# WRAPS PER FRAME, AND WHY THIS IS A SUM OF TWO ROWS NOW. This gate used to
# read text.wrap_text alone. That row counts calls to afterhours' ui::wrap_text
# -- and the transcript stopped calling it: "how many lines is this?" goes
# through hanabi's own counting wrapper (text.count_lines,
# src/util/wrap_count.h) and the hug goes through wrapped_line_spans. Reading
# only the old row would have left this gate watching a function with 0.03
# calls a frame, permanently green and permanently blind, which is a worse
# state than not having it. Both rows, added, is the same question the gate
# has always asked: how many times per frame does the transcript work out
# where a line breaks.
wrap_calls() {  # <prof output>
    local a b
    a=$(printf '%s\n' "$1" | awk '/text\.wrap_text /  {print $4; exit}')
    b=$(printf '%s\n' "$1" | awk '/text\.count_lines/ {print $4; exit}')
    awk -v a="${a:-0}" -v b="${b:-0}" 'BEGIN{printf "%.1f", a + b}'
}
S_WRAP=$(wrap_calls "$SHORT_OUT")
L_WRAP=$(wrap_calls "$LONG_OUT")
S_ALLOC=$(field "$SHORT_OUT" 'ALLOCATIONS' 6)
L_ALLOC=$(field "$LONG_OUT"  'ALLOCATIONS' 6)
S_HIT=$(field "$SHORT_OUT" 'cache\.msgrender_hit' 4)
S_MISS=$(field "$SHORT_OUT" 'cache\.msgrender_miss' 4)
L_HIT=$(field "$LONG_OUT" 'cache\.msgrender_hit' 4)
L_MISS=$(field "$LONG_OUT" 'cache\.msgrender_miss' 4)
: "${S_MISS:=0}" "${L_MISS:=0}"

fail=0
report() {  # report <name> <short> <long> <limit-per-1000-msgs> <unit>
    local name=$1 s=$2 l=$3 limit=$4 unit=$5
    local slope
    slope=$(awk -v s="$s" -v l="$l" -v d="$((LONG_MSGS - SHORT_MSGS))" \
                'BEGIN{printf "%.3f", (l - s) / d}')
    local verdict="ok"
    if awk -v x="$slope" -v m="$limit" 'BEGIN{exit !(x > m)}'; then
        verdict="FAIL"
        fail=1
    fi
    printf "  %-28s %10s -> %-10s  slope %8s %s/message   limit %s   %s\n" \
        "$name" "$s" "$l" "$slope" "$unit" "$limit" "$verdict"
}

# --- The gates ------------------------------------------------------------
# Wraps per frame: the transcript works out line breaks for what is ON
# SCREEN, so this is flat in thread length. It was 89.7 -> 753.4 across this
# same pair before the render-cache fix, a slope of 1.58 calls per message; a
# slope of 0.05 leaves room for the item-list walk to grow a little and still
# catches any return of a per-message wrap.
report "wrap calls/frame" "$S_WRAP" "$L_WRAP" 0.05 "calls"

# Allocations per frame: was 167 per message per frame. The minimap builds one
# mark entity per item by design and costs ~4.6 allocations each, so the floor
# here is not zero; 12 catches a regression of any real size well short of it.
report "allocations/frame" "$S_ALLOC" "$L_ALLOC" 12 "allocs"

# --- Per-item gate: the memo must actually memoize -------------------------
# Not a slope but a RATE, and the same kind of statement: the cost of one more
# message must be one cold measure, not one measure per frame forever. Below
# 95% means entries are being evicted while still in use, which is how the
# original bug looked from the outside (34%).
for pair in "$SHORT_MSGS:$S_HIT:$S_MISS" "$LONG_MSGS:$L_HIT:$L_MISS"; do
    IFS=: read -r n h m <<< "$pair"
    rate=$(awk -v h="$h" -v m="$m" 'BEGIN{t=h+m; r=0; if (t>0) r=100*h/t; printf "%.1f", r}')
    verdict="ok"
    if awk -v r="$rate" 'BEGIN{exit !(r < 95.0)}'; then verdict="FAIL"; fail=1; fi
    printf "  %-28s %10s hit / %-8s miss = %5s%%          limit 95.0%%    %s\n" \
        "render-cache @ ${n} msgs" "$h" "$m" "$rate" "$verdict"
done

if [ "$fail" -ne 0 ]; then
    echo "  transcript slope gate: FAIL"
    exit 1
fi
echo "  transcript slope gate: PASS"
