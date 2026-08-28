#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"
WORK="$(mktemp -d -t hanabi_memory_scale)"
trap 'pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; rm -rf "$WORK"' EXIT

[ -x "$EXE" ] || { echo "memory-scaling-gate: build the app first" >&2; exit 2; }

printf '%8s %10s %10s %8s %8s %10s\n' threads rss_kb heap_kb tabs lru pane_states
for n in 20 100 500; do
  home="$WORK/home-$n"
  mkdir -p "$home/Library/Application Support/hanabi"
  printf '%s\n' '{"window_width":1180,"window_height":949,"open_tabs":[],"active_tab":"","theme":"dark"}' > "$home/Library/Application Support/hanabi/settings.json"
  log="$WORK/$n.log"
  env HOME="$home" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
    HANABI_CONFIG=/nonexistent/hanabi/memory-scale.json HANABI_MEMLADDER=1 \
    HANABI_STRESS_SESSIONS=520 HANABI_MEM_SESSIONS="$n" HANABI_MEM_CHURN="$n" \
    HANABI_MEM_SETTLE=8 HANABI_MEM_SAMPLES=3 \
    "$EXE" --screenshot "$WORK/$n.png" >"$log" 2>&1

  read -r rss heap <<<"$(grep 'two panes, find, index, outbox and streaming' "$log" | tail -1 | awk '{print $2, $7}')"
  held="$(awk '/two panes, find, index, outbox and streaming/{getline; if ($0 ~ /held:/) {print; exit}}' "$log")"
  released="$(awk '/transient holders released/{getline; if ($0 ~ /held:/) {print; exit}}' "$log")"
  tabs="$(printf '%s' "$held" | sed -nE 's/.*tabs=([0-9]+).*/\1/p')"
  lru="$(printf '%s' "$held" | sed -nE 's/.*lru=([0-9]+).*/\1/p')"
  panes="$(printf '%s' "$held" | sed -nE 's/.*paneStates=([0-9]+).*/\1/p')"
  items="$(printf '%s' "$held" | sed -nE 's/.*itemIndex=([0-9]+)\/.*/\1/p')"
  live="$(printf '%s' "$held" | sed -nE 's/.*liveSubs=([0-9]+).*/\1/p')"
  printf '%8d %10d %10d %8d %8d %10d\n' "$n" "$rss" "$heap" "$tabs" "$lru" "$panes"

  [ "$tabs" -eq "$n" ]
  [ "$lru" -le 5 ]
  [ "$panes" -le 64 ]
  [ "$items" -le 4 ]
  [ "$live" -le 5 ]
  printf '%s' "$released" | grep -q 'find=0'
  printf '%s' "$released" | grep -q 'outbox=0'
  printf '%s' "$released" | grep -q 'stream=0/0B'

  eval "RSS_$n=$rss"
  eval "HEAP_$n=$heap"
done

rss_ratio="$(awk "BEGIN{printf \"%.3f\", $RSS_500/$RSS_20}")"
heap_ratio="$(awk "BEGIN{printf \"%.3f\", $HEAP_500/$HEAP_20}")"
printf '500/20 ratio: RSS %sx, retained heap %sx\n' "$rss_ratio" "$heap_ratio"
awk "BEGIN{exit !($rss_ratio <= 1.15)}"
awk "BEGIN{exit !($heap_ratio <= 1.15)}"
echo "memory-scaling-gate: PASS"
