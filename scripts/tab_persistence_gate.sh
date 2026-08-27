#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="$ROOT/output/hanabi_uitest.exe"
HOME_DIR="$(mktemp -d /tmp/hanabi_tab_persist.XXXXXX)"
trap 'rm -rf "$HOME_DIR"' EXIT

SETTINGS_DIR="$HOME_DIR/Library/Application Support/hanabi"
SETTINGS="$SETTINGS_DIR/settings.json"
CACHE="$HOME_DIR/cache"
PREPARE="$ROOT/tests/harness/tab_persistence_prepare.e2e"
RELAUNCH="$ROOT/tests/harness/tab_persistence_relaunch.e2e"
mkdir -p "$SETTINGS_DIR" "$CACHE"
sed -nE 's/^# settings:[[:space:]]*//p' "$PREPARE" | head -1 > "$SETTINGS"

COMMON=(HOME="$HOME_DIR" HANABI_CONFIG="$HOME_DIR/no-such-config.json"
        HANABI_CACHE_DIR="$CACHE" HANABI_TOKEN_FILE="$HOME_DIR/token.json"
        HANABI_BACKEND=mock HANABI_MOCK_NOW=1781524800 TZ=UTC)

env "${COMMON[@]}" "$EXE" --e2e "$PREPARE"

python3 - "$SETTINGS" <<'PY'
import json
import sys

with open(sys.argv[1]) as f:
    settings = json.load(f)
assert settings["open_tabs"] == ["t1", "t4"], settings["open_tabs"]
assert settings["active_tab"] == "t4", settings["active_tab"]
assert settings["pinned_tabs"] == ["t1"], settings["pinned_tabs"]
assert settings["split_open"] is True, settings["split_open"]
assert settings["split_panes"] == ["t1", "t4"], settings["split_panes"]
assert settings["split_focused_pane"] == 1, settings["split_focused_pane"]
PY

env "${COMMON[@]}" "$EXE" --e2e "$RELAUNCH"

AMBIG_HOME="$HOME_DIR/ambiguous"
AMBIG_SETTINGS="$AMBIG_HOME/Library/Application Support/hanabi/settings.json"
AMBIG_SCRIPT="$ROOT/tests/harness/tab_persistence_same_thread.e2e"
mkdir -p "$(dirname "$AMBIG_SETTINGS")" "$AMBIG_HOME/cache"
sed -nE 's/^# settings:[[:space:]]*//p' "$AMBIG_SCRIPT" | head -1 > "$AMBIG_SETTINGS"
AMBIG_COMMON=(HOME="$AMBIG_HOME" HANABI_CONFIG="$AMBIG_HOME/no-such-config.json"
              HANABI_CACHE_DIR="$AMBIG_HOME/cache"
              HANABI_TOKEN_FILE="$AMBIG_HOME/token.json" HANABI_BACKEND=mock
              HANABI_MOCK_NOW=1781524800 TZ=UTC)
env "${AMBIG_COMMON[@]}" "$EXE" --e2e "$AMBIG_SCRIPT"
python3 - "$AMBIG_SETTINGS" <<'PY'
import json
import sys

with open(sys.argv[1]) as f:
    settings = json.load(f)
assert settings["split_panes"] == ["t2", "t2"], settings["split_panes"]
assert settings["split_focused_pane"] == 1, settings["split_focused_pane"]
PY
env "${AMBIG_COMMON[@]}" "$EXE" --e2e "$AMBIG_SCRIPT"

echo "tab persistence relaunch: PASS"
