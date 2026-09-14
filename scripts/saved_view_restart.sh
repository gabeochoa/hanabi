#!/bin/bash
# Two-process proof that a saved view survives a RESTART: process 1 saves a
# view from a filter and exits; process 2 is a plain production startup on
# the SAME private HOME/config/cache -- the settings file process 1 wrote, not
# a fixture -- and must find the view on the shelf, lit, filtering, with the
# search box empty. `reload_settings` inside one process proves only the
# bytes; this proves the startup path. Mock backend; nothing outside $ISO.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXE="$ROOT/output/hanabi_uitest.exe"
[ -x "$EXE" ] || { echo "no $EXE -- zig build uitest-build"; exit 2; }
ISO="$(mktemp -d /tmp/hanabi_restart.XXXXXX)"
# The private HOME is removed on success and KEPT on failure, so a red run
# leaves its settings.json and logs for diagnosis.
trap '[ "${KEEP_ISO:-0}" = 1 ] && echo "kept $ISO for diagnosis" || rm -rf "$ISO"' EXIT
fail() { KEEP_ISO=1; grep -E 'E2E ERROR|TIMEOUT\]' "/tmp/hanabi_restart_$1.log" | head -3; exit 1; }
SUP="$ISO/Library/Application Support/hanabi"; mkdir -p "$SUP" "$ISO/cache"
FIRST="$ROOT/tests/restart/saved_view_first.e2e"
SECOND_TMPL="$ROOT/tests/restart/saved_view_second.e2e"
sed -nE 's/^# settings:[[:space:]]*//p' "$FIRST" | head -1 > "$SUP/settings.json"
run() { # name script
  env HOME="$ISO" HANABI_CONFIG="$ISO/no-such-config.json" HANABI_CACHE_DIR="$ISO/cache" \
      TZ=UTC HANABI_MOCK_NOW=1781524800 HANABI_TOKEN_FILE="$ISO/token.json" HANABI_BACKEND=mock \
      "$EXE" --e2e "$2" > "/tmp/hanabi_restart_$1.log" 2>&1
  local rc=$?; echo "$1 rc=$rc"; return $rc
}
run first "$FIRST" || fail first
# process 1 has exited; what it left on disk is the whole input to process 2
ID=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('selected_view',''))" "$SUP/settings.json")
[ -n "$ID" ] || { echo "process 1 left no selected_view in settings.json"; KEEP_ISO=1; exit 1; }
echo "settings.json after process 1: selected_view=$ID, saved_views(user)=$(python3 -c "import json,sys; print(len([v for v in json.load(open(sys.argv[1]))['saved_views'] if not v.get('built_in')]))" "$SUP/settings.json")"
SECOND="$ISO/saved_view_second.e2e"; sed "s/@VIEW_ID@/$ID/" "$SECOND_TMPL" > "$SECOND"
run second "$SECOND" || fail second
echo "saved view survived a restart: shelf, selection, filter, empty search"
