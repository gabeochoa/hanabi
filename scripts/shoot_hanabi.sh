#!/usr/bin/env bash
# ===========================================================================
# scripts/shoot_hanabi.sh -- capture hanabi in the parity reference state.
#
#   scripts/shoot_hanabi.sh [out.png] [worktree_root]
#
# The reference (docs/visual-parity/ref/01_home.png) was shot at Puffin's own
# 1180x949 with TWO pinned tabs and the second one active. Shooting anything
# else and comparing it to that reference measures the fixture, not the design:
# a single unpinned tab scores the tab bar at 25.3% against 5.5% for the right
# one, and the Home digest scores `main` at 58% against 12%. See REFERENCE.md,
# "Compare LIKE FOR LIKE".
#
# Isolated HOME, mock backend, a config path that does not exist, and a scoped
# pkill afterward -- the same contract as screens.sh, so it never reads or
# writes the real settings.json and never leaves a process behind.
# ===========================================================================
set -uo pipefail

OUT="${1:-/tmp/hb.png}"
ROOT="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
EXE="$ROOT/output/hanabi.exe"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make'." >&2
    exit 2
fi

H="$(mktemp -d /tmp/hanabi_shoot_home.XXXXXX)"
trap 'rm -rf "$H"' EXIT
mkdir -p "$H/Library/Application Support/hanabi"
cat > "$H/Library/Application Support/hanabi/settings.json" <<'JSON'
{"window_width":1180,"window_height":949,"open_tabs":["t9","t2"],"active_tab":"t2","pinned_tabs":["t9","t2"],"theme":"dark"}
JSON

rm -f "$OUT"
env HOME="$H" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
    HANABI_CONFIG="/tmp/none_$$" "$EXE" --screenshot "$OUT" \
    > "/tmp/hanabi_shoot_$$.log" 2>&1 &
pid=$!
for _ in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
kill -9 "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
pkill -9 -f "^$EXE" >/dev/null 2>&1

dim="$(/usr/bin/file "$OUT" 2>/dev/null | sed -nE 's/.*, ([0-9]+ x [0-9]+),.*/\1/p')"
if [ "$dim" != "1180 x 949" ]; then
    echo "FAIL: expected 1180 x 949, got '${dim:-nothing}'" >&2
    tail -20 "/tmp/hanabi_shoot_$$.log" >&2
    exit 1
fi
echo "OK  $OUT  ($dim)"
