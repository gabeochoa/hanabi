#!/usr/bin/env bash
# scripts/soak_spread.sh — how noisy is the soak verdict, on THIS machine?
#
# Not a gate. The tool that sets one: it runs the same clean configuration N
# times and prints the spread of each slope column, which is the only honest
# way to choose a budget. Every threshold in scripts/soak_gate.sh and
# scripts/soak.sh came out of this, and re-running it is how the next person
# checks whether they still hold on a different box.
#
#   bash scripts/soak_spread.sh 8 1000 100      # runs, frames, bucket size
#
# Print the raw samples as well as the summary, deliberately: a max that is
# one outlier over seven tight readings is a different fact from a max that is
# the top of an even spread, and a summary line cannot tell them apart.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

RUNS="${1:-8}"
FRAMES="${2:-1000}"
EVERY="${3:-100}"
SCENARIO="${HANABI_STRESS:-idle}"

if [ ! -x "$EXE" ]; then
    echo "soak_spread: $EXE not found — run 'make' first." >&2
    exit 2
fi

SHOT="$(mktemp -t hanabi_spread_XXXX).png"
LOG="$(mktemp -t hanabi_spread_XXXX).log"
trap 'rm -f "$SHOT" "$LOG"' EXIT

echo "=== soak spread: ${RUNS} runs, ${FRAMES} frames, buckets of ${EVERY}, scenario ${SCENARIO} ==="
echo "  load at start: $(uptime | sed -nE 's/.*load averages?: //p')"
printf '  %-5s %10s %10s %10s %10s %8s\n' run "RSS/1k" "heap/1k" "blocks/1k" "ms/1k" "rising"

rss_all=""; heap_all=""; blocks_all=""; ms_all=""; incomplete=0
for i in $(seq 1 "$RUNS"); do
    env HANABI_BACKEND=mock HANABI_CONFIG=/nonexistent/hanabi/spread.json \
        HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY="$EVERY" \
        HANABI_STRESS="$SCENARIO" \
        HANABI_SOAK_MAX_RSS_KB_PER1K=1000000 \
        HANABI_SOAK_MAX_HEAP_KB_PER1K=1000000 \
        HANABI_SOAK_MAX_MS_PER1K=1000000 \
        HANABI_SOAK_MAX_ENT_PER1K=1000000 \
        "$EXE" --screenshot "$SHOT" >"$LOG" 2>&1
    read -r r h b m rise <<<"$(python3 - "$LOG" <<'PARSE'
import re, sys
# Parse the verdict table by LABEL, not by field index: the label is one or
# two words and the verdict cell is one or four, so a positional parse reads
# a different column depending on whether the row passed. That bug produced a
# summary of the string "KB" the first time this was run.
rows = {}
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"\[soak\]\s+(RSS|heap bytes|heap blocks|entities|frame time)\s+"
                 r"([-+][\d.]+)\s+(?:KB|ms\s)?\s*[-+][\d.]+\s+(?:KB|ms\s)?\s*"
                 r"(\S+)\s+(\S+)", line)
    if m:
        rows[m.group(1)] = (m.group(2), m.group(4))
if len(rows) < 5:
    print("")
else:
    print(rows["RSS"][0], rows["heap bytes"][0], rows["heap blocks"][0],
          rows["frame time"][0], rows["RSS"][1])
PARSE
)"
    if [ -z "$r" ] || [ -z "$h" ]; then
        printf '  %-5s %10s\n' "$i" "DID NOT REACH A VERDICT"
        incomplete=$((incomplete + 1))
        continue
    fi
    printf '  %-5s %10s %10s %10s %10s %8s\n' "$i" "$r" "$h" "$b" "$m" "$rise"
    rss_all="$rss_all $r"; heap_all="$heap_all $h"
    blocks_all="$blocks_all $b"; ms_all="$ms_all $m"
done

echo
# A spread over zero samples is not a spread. Say so instead of printing a
# summary of nothing, which is the failure mode this whole file exists to
# help avoid.
if [ -z "$rss_all" ]; then
    echo "  NO RUN REACHED A VERDICT — nothing was measured. Not a clean tree," >&2
    echo "  a broken harness. Run the binary by hand and read its output." >&2
    exit 2
fi
[ "$incomplete" -gt 0 ] && echo "  WARNING: ${incomplete} of ${RUNS} runs produced no verdict; the summary" \
    "below is over the ${RUNS} minus ${incomplete} that did."

summarize() {
    printf '  %-12s' "$1"; shift
    python3 -c '
import sys
xs = sorted(float(v) for v in sys.argv[1:])
n = len(xs)
med = xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2
print(f"n={n:<3} min {xs[0]:+9.1f}  median {med:+9.1f}  max {xs[-1]:+9.1f}  spread {xs[-1]-xs[0]:9.1f}")
' "$@"
}
summarize "RSS KB" $rss_all
summarize "heap KB" $heap_all
summarize "blocks" $blocks_all
summarize "ms" $ms_all
echo "  load at end:   $(uptime | sed -nE 's/.*load averages?: //p')"
