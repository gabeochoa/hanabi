#!/usr/bin/env bash
# scripts/alloc_sites.sh — WHERE does the frame allocate?
#
# `HANABI_PROF=1` says how many times the frame calls operator new.
# `HANABI_PROF_SITES=1` says from where: src/util/prof.h hashes the top three
# return addresses at every allocation into a fixed table and prints the
# busiest forty. This script runs that and pipes the raw addresses through
# `atos`, which is the only symbolizer that resolves the inlined header code
# this app is made of.
#
# WHY NOT `malloc_history`. It walks blocks that are LIVE at the moment it
# attaches. The frame frees everything it allocates, so on this app it reports
# Metal's device init and the mock catalog and says nothing at all about the
# frame. It is the right tool for a leak — it found the Metal one in a single
# run — and the wrong one for churn.
#
# Usage:
#   scripts/alloc_sites.sh [sessions] [frames] [scenario]
#   scripts/alloc_sites.sh 2000 300            # the default: big catalog, idle
#   scripts/alloc_sites.sh 20 300 threads      # small catalog, opening threads
#   HANABI_SITES_BIG=1 scripts/alloc_sites.sh  # with a 480-message transcript
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

SESSIONS="${1:-2000}"
FRAMES="${2:-300}"
SCENARIO="${3:-idle}"

if [ ! -x "$EXE" ]; then
    echo "alloc_sites: $EXE not found — run 'make' first." >&2
    exit 2
fi

H="$(mktemp -d)"
mkdir -p "$H/Library/Application Support/hanabi"
TABS='[]'
ACTIVE='""'
if [ -n "${HANABI_SITES_BIG:-}" ]; then
    TABS='["rbig"]'
    ACTIVE='"rbig"'
fi
cat > "$H/Library/Application Support/hanabi/settings.json" <<J
{"window_width":1180,"window_height":949,"open_tabs":${TABS},"active_tab":${ACTIVE},"theme":"dark"}
J

LOG="$(mktemp -t hanabi_sites_XXXX).log"
cleanup() { rm -rf "$H"; rm -f "$LOG"; }
trap cleanup EXIT

env HOME="$H" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
    HANABI_CONFIG=/tmp/none HANABI_PROF=1 HANABI_PROF_SITES=1 \
    HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY="$FRAMES" \
    HANABI_STRESS="$SCENARIO" HANABI_STRESS_SESSIONS="$SESSIONS" \
    ${HANABI_SITES_BIG:+HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_TURNS=${HANABI_BIG_TURNS:-120}} \
    "$EXE" --screenshot "$H/shot.png" > "$LOG" 2>&1

grep -E '^\[prof\] ALLOCATIONS' "$LOG" | sed 's/^/  /'
echo
python3 "$ROOT/scripts/alloc_sites.py" < "$LOG"
