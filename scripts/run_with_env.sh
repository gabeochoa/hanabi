#!/usr/bin/env bash
# Source .env (gitignored) and exec the given command. `zig build
# test-agentcloud-real` uses it, the way the makefile's recipe did.
set -u
cd "$(dirname "$0")/.."
set -a; [ -f .env ] && . ./.env; set +a
exec "$@"
