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
emit "| scale | load | cold start ms (min / med / max) | mock seed ms (med) | client cold ms | switch ms (p50 / p95 / max, n) | scroll fps (p50 / p95) | RSS KB (max) | mock seed KB | client KB |"
emit "|---:|---:|---|---:|---:|---|---|---:|---:|---:|"

for SCALE in $SCALES; do
    export HANABI_MOCK_SCALE="$SCALE"
    LOAD="$(load1)"

    # ---- cold start --------------------------------------------------------
    : >"$TMP/ff.txt"; : >"$TMP/seed.txt"
    for i in $(seq 1 "$LAUNCHES"); do
        LOG="$TMP/launch-$SCALE-$i.log"
        timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$TMP/shot.png" >"$LOG" 2>&1
        grep -Eo 'FirstFrame: [0-9]+ ms' "$LOG" | grep -Eo '[0-9]+' | head -1 >>"$TMP/ff.txt"
        grep -Eo 'FirstFrame mock seed: [0-9.]+ ms' "$LOG" | grep -Eo '[0-9.]+' | head -1 >>"$TMP/seed.txt"
    done
    read -r FF_MIN FF_MED FF_MAX < <(stats <"$TMP/ff.txt")
    read -r SEED_MIN SEED_MED SEED_MAX < <(stats <"$TMP/seed.txt")
    CLIENT_COLD="n/a"
    [ "$SEED_MED" != "n/a" ] && CLIENT_COLD="$(awk -v f="$FF_MED" -v s="$SEED_MED" 'BEGIN{printf "%.1f", f - s}')"

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
    MOCK_KB="$(grep -E '^\[mock\] seed ' "$LOG" | tail -1 | sed -E 's/.*rss_delta_kb=(-?[0-9]+).*/\1/')"
    CLIENT_KB="n/a"
    if [ -n "$MOCK_KB" ] && [ "$MOCK_KB" -ge 0 ] 2>/dev/null && [ "$RSS_MAX" != "n/a" ]; then
        CLIENT_KB=$((RSS_MAX - MOCK_KB))
    else
        MOCK_KB="n/a"
    fi

    emit "| $SCALE | $LOAD | $FF_MIN / $FF_MED / $FF_MAX | $SEED_MED | $CLIENT_COLD | $SW_P50 / $SW_P95 / $SW_MAX (n=$SW_N) | $FPS_P50 / $FPS_P05 | $RSS_MAX | $MOCK_KB | $CLIENT_KB |"
    cp "$TMP/switch-$SCALE.log" "$TMP/scroll-$SCALE.log" "${HANABI_PROBE_KEEP:-/tmp}/" 2>/dev/null || true
done
emit ""
emit "scroll fps p95 column is the 5th percentile of fps (the slow tail); switch n is the number of opens the probe saw land; mock seed KB / ms are the mock building its catalog (resident growth; wall time inside the launch), client cold ms = cold start median minus that, client KB = RSS max minus that."

# ---- relaunch: first open after a relaunch, full fetch vs since-delta --------
# HANABI_PROBE_RELAUNCH=1 adds one block at HANABI_PROBE_RELAUNCH_SCALE (10):
# the mock's attach costs HANABI_PROBE_OPEN_DELAY_MS (250) plus
# HANABI_PROBE_OPEN_ROW_US (1000) per row returned, the disk cache lives in a
# private HANABI_CACHE_DIR, a warm run opens the threads once, and the
# measured run reopens them -- twice: HANABI_INSTANT_THREADS=0 (the open path
# before instant threads: full newest-N fetch in parallel with the disk read)
# and =1 (wait for the disk read, fetch since the cached seq). The columns are
# the probe's `fresh` lines: ms from the open to the network landing, rows
# the network carried, how many opens were full vs since.
if [ "${HANABI_PROBE_RELAUNCH:-0}" = "1" ]; then
    RSCALE="${HANABI_PROBE_RELAUNCH_SCALE:-10}"
    ODELAY="${HANABI_PROBE_OPEN_DELAY_MS:-250}"
    OROW="${HANABI_PROBE_OPEN_ROW_US:-1000}"
    emit ""
    emit "### relaunch at ${RSCALE}x — attach costs ${ODELAY} ms + ${OROW} us/row"
    emit ""
    emit "| arm | opens | first paint p50 ms | fresh p50 / p95 ms | rows fetched | full / since |"
    emit "|---|---:|---:|---|---:|---|"
    for ARM in 0 1; do
        CACHE="$TMP/cache-$ARM"
        rm -rf "$CACHE"; mkdir -p "$CACHE"
        COMMON="HANABI_MOCK_SCALE=$RSCALE HANABI_MOCK_DISK_CACHE=1 HANABI_CACHE_DIR=$CACHE HANABI_INSTANT_THREADS=$ARM HANABI_STRESS=threads HANABI_SOAK=$FRAMES HANABI_SOAK_EVERY=$FRAMES HANABI_STRESS_SETTLE=120"
        env $COMMON timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$TMP/shot.png" >"$TMP/warm-$ARM.log" 2>&1
        env $COMMON HANABI_MOCK_OPEN_DELAY_MS="$ODELAY" HANABI_MOCK_OPEN_ROW_US="$OROW" HANABI_SWITCH_PROBE=1 \
            timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$TMP/shot.png" >"$TMP/relaunch-$ARM.log" 2>&1
        LOG="$TMP/relaunch-$ARM.log"
        grep -E '^\[switch\] open ' "$LOG" | awk '{print $4}' >"$TMP/rp.txt"
        PAINT="$(pct 0.5 <"$TMP/rp.txt")"
        N_OPEN="$(wc -l <"$TMP/rp.txt" | tr -d ' ')"
        SUMM="$(grep -E '^\[switch\] fresh landed=' "$LOG" | tail -1)"
        F_P50="$(echo "$SUMM" | sed -E 's/.*p50=([0-9.]+).*/\1/')"
        F_P95="$(echo "$SUMM" | sed -E 's/.*p95=([0-9.]+).*/\1/')"
        ROWS="$(echo "$SUMM" | sed -E 's/.*rows_fetched=([0-9]+).*/\1/')"
        FULL="$(echo "$SUMM" | sed -E 's/.*full=([0-9]+).*/\1/')"
        SINCE="$(echo "$SUMM" | sed -E 's/.*since=([0-9]+).*/\1/')"
        [ -n "$SUMM" ] || { F_P50=n/a; F_P95=n/a; ROWS=n/a; FULL=n/a; SINCE=n/a; }
        ARMNAME="before (full fetch)"; [ "$ARM" = "1" ] && ARMNAME="after (since-delta)"
        emit "| $ARMNAME | $N_OPEN | $PAINT | $F_P50 / $F_P95 | $ROWS | $FULL / $SINCE |"
        cp "$LOG" "${HANABI_PROBE_KEEP:-/tmp}/relaunch-$ARM.log" 2>/dev/null || true
    done
    emit ""
    emit "first paint is the open's first drawn frame (from the disk cache in both arms); fresh is when the network's answer landed."
fi
