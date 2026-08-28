#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT_FILE="$(mktemp /tmp/hanabi_agentcloud_port.XXXXXX)"
LOG_FILE="$(mktemp /tmp/hanabi_agentcloud_server.XXXXXX)"
cleanup() {
    if [ -n "${SERVER_PID:-}" ]; then kill "$SERVER_PID" >/dev/null 2>&1 || true; fi
    /bin/rm -f "$PORT_FILE" "$LOG_FILE"
}
trap cleanup EXIT
python3 "$ROOT/tests/harness/agentcloud_local_server.py" --port-file "$PORT_FILE" >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 100); do
    [ -s "$PORT_FILE" ] && break
    sleep 0.02
done
[ -s "$PORT_FILE" ] || { cat "$LOG_FILE" >&2; exit 1; }
HANABI_AC_LOCAL_PORT="$(cat "$PORT_FILE")" "$ROOT/output/tests/test_agentcloud_local"
wait "$SERVER_PID"
