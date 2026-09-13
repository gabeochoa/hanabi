#!/usr/bin/env bash
# `zig build run`: the makefile's `run` recipe. The binary is already built and
# installed by the step this script depends on.
set -u
cd "$(dirname "$0")/.."
set -a; [ -f .env ] && . ./.env; set +a
export HANABI_BACKEND="${HANABI_BACKEND:-agentcloud}"
if [ "$HANABI_BACKEND" = "agentcloud" ] && [ -z "${HANABI_AC_HOST:-}" ]; then
    echo "==> HANABI_BACKEND=agentcloud, but HANABI_AC_HOST is unset."
    echo "    Running the OFFLINE MOCK. Set the HANABI_AC_* values in .env"
    echo "    (gitignored) to talk to the real orchestrator."
else
    echo "==> backend: $HANABI_BACKEND"
fi
exec ./output/hanabi.exe
