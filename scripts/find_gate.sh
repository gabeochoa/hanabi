#!/bin/bash
set -u

EXE="${1:-output/hanabi.exe}"
FRAMES="${HANABI_FIND_GATE_FRAMES:-180}"
SHORT_TURNS=120
LONG_TURNS=918
SHORT_MSGS=$((SHORT_TURNS * 4))
LONG_MSGS=$((LONG_TURNS * 4))

if [ ! -x "$EXE" ]; then
    echo "find_gate: no binary at $EXE" >&2
    exit 1
fi

run() {
    local turns=$1 mode=$2 h
    h=$(mktemp -d)
    mkdir -p "$h/Library/Application Support/hanabi"
    cat > "$h/Library/Application Support/hanabi/settings.json" <<J
{"window_width":1180,"window_height":949,"open_tabs":["rbig"],"active_tab":"rbig","theme":"dark"}
J
    if [ "$mode" = open ]; then
        env HOME="$h" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
            HANABI_CONFIG=/tmp/none HANABI_BIG_TRANSCRIPT=1 \
            HANABI_BIG_TURNS="$turns" HANABI_FIND_DEMO=regression \
            HANABI_PROF=1 HANABI_SOAK="$FRAMES" HANABI_STRESS=idle \
            "$EXE" --screenshot "$h/shot.png" 2>&1 | grep '^\[prof\]'
    else
        env HOME="$h" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
            HANABI_CONFIG=/tmp/none HANABI_BIG_TRANSCRIPT=1 \
            HANABI_BIG_TURNS="$turns" HANABI_PROF=1 HANABI_SOAK="$FRAMES" \
            HANABI_STRESS=idle "$EXE" --screenshot "$h/shot.png" 2>&1 | \
            grep '^\[prof\]'
    fi
    rm -rf "$h"
}

field() {
    local value
    value=$(printf '%s\n' "$1" | awk -v pattern="$2" -v column="$3" \
        '$0 ~ pattern {print $column; exit}')
    printf '%s' "${value:-0}"
}

SHORT_CLOSED=$(run "$SHORT_TURNS" closed)
SHORT_OPEN=$(run "$SHORT_TURNS" open)
LONG_CLOSED=$(run "$LONG_TURNS" closed)
LONG_OPEN=$(run "$LONG_TURNS" open)

if [ -z "$SHORT_OPEN" ] || [ -z "$LONG_OPEN" ]; then
    echo "find level gate: FAIL: no profiler output" >&2
    exit 1
fi

S_ROWS=$(field "$SHORT_OPEN" 'find\.rows_visited' 4)
L_ROWS=$(field "$LONG_OPEN" 'find\.rows_visited' 4)
S_HIT=$(field "$SHORT_OPEN" 'find\.memo_hit' 3)
S_MISS=$(field "$SHORT_OPEN" 'find\.memo_miss' 3)
L_HIT=$(field "$LONG_OPEN" 'find\.memo_hit' 3)
L_MISS=$(field "$LONG_OPEN" 'find\.memo_miss' 3)
S_ENTRIES=$(field "$SHORT_OPEN" 'find\.memo_entries' 3)
L_ENTRIES=$(field "$LONG_OPEN" 'find\.memo_entries' 3)
S_CLOSED_ALLOC=$(field "$SHORT_CLOSED" 'ALLOCATIONS' 6)
S_OPEN_ALLOC=$(field "$SHORT_OPEN" 'ALLOCATIONS' 6)
L_CLOSED_ALLOC=$(field "$LONG_CLOSED" 'ALLOCATIONS' 6)
L_OPEN_ALLOC=$(field "$LONG_OPEN" 'ALLOCATIONS' 6)
S_RATE=$(awk -v h="$S_HIT" -v m="$S_MISS" 'BEGIN { t=h+m; if (t==0) print 0; else printf "%.2f", 100*h/t }')
L_RATE=$(awk -v h="$L_HIT" -v m="$L_MISS" 'BEGIN { t=h+m; if (t==0) print 0; else printf "%.2f", 100*h/t }')
S_ALLOC_RATIO=$(awk -v a="$S_OPEN_ALLOC" -v b="$S_CLOSED_ALLOC" 'BEGIN { printf "%.3f", a/b }')
L_ALLOC_RATIO=$(awk -v a="$L_OPEN_ALLOC" -v b="$L_CLOSED_ALLOC" 'BEGIN { printf "%.3f", a/b }')
ROW_LEVEL_MAX="${HANABI_FIND_ROWS_PER_FRAME_MAX:-30.0}"
HIT_RATE_MIN="${HANABI_FIND_HIT_RATE_MIN:-95.0}"
ALLOC_RATIO_MAX="${HANABI_FIND_ALLOC_RATIO_MAX:-2.0}"
CAP=16384
fail=0

printf '=== find level gate ===\n'
printf '  %-12s %12s %12s %12s %12s\n' messages rows/f hit-rate alloc-ratio entries
for row in "$SHORT_MSGS:$S_ROWS:$S_RATE:$S_ALLOC_RATIO:$S_ENTRIES" \
           "$LONG_MSGS:$L_ROWS:$L_RATE:$L_ALLOC_RATIO:$L_ENTRIES"; do
    IFS=: read -r messages rows rate ratio entries <<< "$row"
    verdict=ok
    awk -v x="$rows" -v m="$ROW_LEVEL_MAX" 'BEGIN { exit !(x > m) }' && verdict=FAIL
    awk -v x="$rate" -v m="$HIT_RATE_MIN" 'BEGIN { exit !(x < m) }' && verdict=FAIL
    awk -v x="$ratio" -v m="$ALLOC_RATIO_MAX" 'BEGIN { exit !(x > m) }' && verdict=FAIL
    [ "$entries" -gt "$CAP" ] && verdict=FAIL
    [ "$verdict" = FAIL ] && fail=1
    printf '  %-12s %12s %11s%% %12s %12s  %s\n' \
        "$messages" "$rows" "$rate" "$ratio" "$entries" "$verdict"
done

if [ "$fail" -ne 0 ]; then
    echo "  find level gate: FAIL"
    echo "  unchanged find frames must not revisit the loaded transcript; inspect find.memo_hit and find.rows_visited"
    exit 1
fi

echo "  find level gate: PASS"
