#!/usr/bin/env bash
# scripts/perf_scale_probe.sh — the four scale probes, at HANABI_MOCK_SCALE 1 / 10 / 100.
#
#   cold start        FirstFrame ms, N headless launches, min / median / max
#   switch-to-thread  HANABI_STRESS=threads under HANABI_SWITCH_PROBE: ms from the
#                     open request to the first frame that drew the transcript's
#                     rows, p50 / p95 / max over the opens
#   scroll fps        HANABI_STRESS=read through the longest thread, wall ms/frame
#                     from the soak samples -> fps, p50 / p95
#   resident memory   max RSS KB seen by the soak samples of the scroll run
#
# Everything is the app's own headless path (zig build; output/hanabi.exe) on the
# mock backend; numbers are wall clock and move with host load, so the script
# prints the 1-minute load with each block and the reader keeps it beside the
# numbers. Usage:
#   scripts/perf_scale_probe.sh                 # scales 1 10
#   HANABI_PROBE_SCALES="1 10 100" scripts/perf_scale_probe.sh
#   HANABI_PROBE_LAUNCHES=5 HANABI_PROBE_FRAMES=1200 scripts/perf_scale_probe.sh
# Output: a markdown block per scale on stdout; HANABI_PROBE_OUT=<file> appends it.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_PROBE_EXE:-$ROOT/output/hanabi.exe}"
SCALES="${HANABI_PROBE_SCALES:-1 10}"
LAUNCHES="${HANABI_PROBE_LAUNCHES:-5}"
FRAMES="${HANABI_PROBE_FRAMES:-900}"
OUT="${HANABI_PROBE_OUT:-}"
RUN_TIMEOUT="${HANABI_PROBE_TIMEOUT:-600}"
export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/perf-scale-probe.json"
export HANABI_MOCK_NOW="${HANABI_MOCK_NOW:-1792000000}"

if [ ! -x "$EXE" ]; then
    echo "perf_scale_probe: $EXE not found — run 'zig build' first." >&2
    exit 2
fi
TMP="$(mktemp -d -t hanabi_probe_XXXX)"
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -rf "$TMP"; }
trap cleanup EXIT

load1() { uptime | sed -E 's/.*load averages?: *([0-9.]+).*/\1/'; }
host() { echo "$(hostname -s) $(uname -m) $(sw_vers -productVersion 2>/dev/null || uname -r)"; }
build() { (git -C "$ROOT" rev-parse --short=12 HEAD 2>/dev/null || echo unknown); }

# min / median / max of whitespace-separated numbers on stdin
stats() {
    sort -n | awk '{a[NR]=$1} END{ if (NR==0) {print "n/a n/a n/a"; exit}
        m=(NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2; printf "%.1f %.1f %.1f\n", a[1], m, a[NR]}'
}
pct() {  # $1 = percentile 0..1
    sort -n | awk -v p="$1" '{a[NR]=$1} END{ if (NR==0) {print "n/a"; exit}
        pos=p*(NR-1); lo=int(pos); hi=lo+1; if (hi>NR-1) hi=NR-1; f=pos-lo;
        printf "%.2f\n", a[lo+1]+(a[hi+1]-a[lo+1])*f }'
}

emit() { echo "$*"; [ -n "$OUT" ] && echo "$*" >>"$OUT"; }

emit "## perf_scale_probe — $(date -u +%Y-%m-%dT%H:%MZ)"
emit ""
emit "host: $(host) · build: $(build) · exe: $EXE · launches: $LAUNCHES · soak frames: $FRAMES"
emit ""
emit "| scale | load | cold start ms (min / med / max) | switch ms (p50 / p95 / max, n) | scroll fps (p50 / p95) | RSS KB (max) |"
emit "|---:|---:|---|---|---|---:|"

for SCALE in $SCALES; do
    export HANABI_MOCK_SCALE="$SCALE"
    LOAD="$(load1)"

    # ---- cold start --------------------------------------------------------
    : >"$TMP/ff.txt"
    for i in $(seq 1 "$LAUNCHES"); do
        LOG="$TMP/launch-$SCALE-$i.log"
        timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$TMP/shot.png" >"$LOG" 2>&1
        grep -Eo 'FirstFrame: [0-9]+ ms' "$LOG" | grep -Eo '[0-9]+' | head -1 >>"$TMP/ff.txt"
    done
    read -r FF_MIN FF_MED FF_MAX < <(stats <"$TMP/ff.txt")

    # ---- switch-to-thread -----------------------------------------------------
    LOG="$TMP/switch-$SCALE.log"
    HANABI_STRESS=threads HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY="$FRAMES" \
        HANABI_STRESS_SETTLE=120 HANABI_SWITCH_PROBE=1 \
        timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$TMP/shot.png" >"$LOG" 2>&1
    grep -E '^\[switch\] open ' "$LOG" | awk '{print $4}' >"$TMP/sw.txt"
    SW_N="$(wc -l <"$TMP/sw.txt" | tr -d ' ')"
    SW_P50="$(pct 0.5 <"$TMP/sw.txt")"
    SW_P95="$(pct 0.95 <"$TMP/sw.txt")"
    SW_MAX="$(sort -n "$TMP/sw.txt" | tail -1)"
    [ -n "$SW_MAX" ] || SW_MAX="n/a"

    # ---- scroll fps + RSS through the longest thread --------------------------
    if [ "$SCALE" -gt 1 ]; then OPEN_ID="t2x1"; else OPEN_ID="t2"; fi
    LOG="$TMP/scroll-$SCALE.log"
    HANABI_OPEN="$OPEN_ID" HANABI_STRESS=read HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY=100 \
        HANABI_STRESS_SETTLE=120 \
        timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$TMP/shot.png" >"$LOG" 2>&1
    grep -E '^\[soak\] frame ' "$LOG" | sed -E 's/.* ([0-9.]+) ms\/f wall .*/\1/' >"$TMP/msf.txt"
    awk '{ if ($1 > 0) printf "%.2f\n", 1000.0/$1 }' "$TMP/msf.txt" >"$TMP/fps.txt"
    FPS_P50="$(pct 0.5 <"$TMP/fps.txt")"
    FPS_P05="$(pct 0.05 <"$TMP/fps.txt")"
    RSS_MAX="$(grep -E '^\[soak\] frame ' "$LOG" | sed -E 's/.*RSS +([0-9]+) KB.*/\1/' | sort -n | tail -1)"
    [ -n "$RSS_MAX" ] || RSS_MAX="n/a"

    emit "| $SCALE | $LOAD | $FF_MIN / $FF_MED / $FF_MAX | $SW_P50 / $SW_P95 / $SW_MAX (n=$SW_N) | $FPS_P50 / $FPS_P05 | $RSS_MAX |"
    cp "$TMP/switch-$SCALE.log" "$TMP/scroll-$SCALE.log" "${HANABI_PROBE_KEEP:-/tmp}/" 2>/dev/null || true
done
emit ""
emit "scroll fps p95 column is the 5th percentile of fps (the slow tail); switch n is the number of opens the probe saw land."
