#!/usr/bin/env bash
# The makefile's `stress` / `stress-break` recipes. Knobs through the
# environment: SCENARIO FRAMES EVERY SESSIONS UNTIL (and HANABI_MOCK_NOW).
set -u
cd "$(dirname "$0")/.."
if [ "${1:-}" = "--break" ]; then
    SCENARIO="${SCENARIO:-open}"; FRAMES="${FRAMES:-30000}"; EVERY="${EVERY:-1000}"
    SESSIONS="${SESSIONS:-2000}"; UNTIL="${UNTIL:-cpu:3.0}"
fi
env HANABI_BACKEND=mock HANABI_CONFIG=/nonexistent/hanabi/stress.json \
    HANABI_MOCK_NOW="${HANABI_MOCK_NOW:-1787000000}" \
    HANABI_STRESS="${SCENARIO:-idle}" \
    HANABI_SOAK="${FRAMES:-3000}" \
    HANABI_SOAK_EVERY="${EVERY:-250}" \
    ${SESSIONS:+HANABI_STRESS_SESSIONS="$SESSIONS"} \
    ${UNTIL:+HANABI_STRESS_UNTIL="$UNTIL"} \
    ./output/hanabi.exe --screenshot /tmp/hanabi_stress.png
