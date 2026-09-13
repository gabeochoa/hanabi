#!/usr/bin/env bash
cd "$(dirname "$0")/.."
exec python3 tools/mock_server/server.py --port "${MOCK_PORT:-8787}"
