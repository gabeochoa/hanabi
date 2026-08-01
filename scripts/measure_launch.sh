#!/usr/bin/env bash
# scripts/measure_launch.sh — standalone launch-perf gate for hanabi.
#
# Launches the headless one-shot render path (the SAME code path the windowed
# app uses to build state + first frame), captures:
#   * Startup: N ms     (process start -> systems ready; app's own log)
#   * FirstFrame: N ms   (process start -> first frame rendered; test hook)
#   * Peak RSS           (macOS: /usr/bin/time -l "maximum resident set size")
# and PASSes only if FirstFrame < STARTUP_CEILING_MS and RSS < RSS_CEILING_MB.
#
# Referenced by docs/phased-plan.md Phase P (cold launch < 250 ms) and Phase X
# (RAM budget < 250 MB). Thresholds are the project's HARD budgets; they are
# deliberately generous now and easy to tighten (edit the two constants below).
#
# Runs the app in the BACKGROUND with a hard timeout + guaranteed
# `pkill -9 -f hanabi.exe` cleanup, so it never hangs a 5s foreground shell and
# never leaves a stray process.
set -uo pipefail

# ---- TUNABLE THRESHOLDS (project hard budgets; tighten as it gets faster) ----
STARTUP_CEILING_MS=250   # Phase P: cold launch to first frame < 250 ms
RSS_CEILING_MB=250       # Phase X: peak RSS < 250 MB
# -----------------------------------------------------------------------------

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="./output/hanabi.exe"
SHOT="$(mktemp -t hanabi_launch_XXXX).png"
LOG="$(mktemp -t hanabi_launch_XXXX).log"
TIMELOG="$(mktemp -t hanabi_launch_XXXX).time"
RUN_TIMEOUT=15  # seconds; headless one-shot is ~0.2s, this is a safety net

cleanup() { pkill -9 -f hanabi.exe >/dev/null 2>&1 || true; rm -f "$SHOT" "$LOG" "$TIMELOG"; }
trap cleanup EXIT

if [ ! -x "$EXE" ]; then
    echo "measure_launch: $EXE not found — run 'make -j4' first." >&2
    exit 2
fi

# Launch headless under /usr/bin/time -l (macOS) to capture peak RSS.
# Background it with a watchdog kill so a stuck GPU init can never hang us.
# Force the zero-config MOCK backend and isolate from any real ~/.config/hanabi/
# config.json: the perf gate must measure the deterministic offline path, not a
# real https backend whose network fetch would inflate FirstFrame and make the
# gate pass/fail depending on the dev machine's config.
export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/perf-gate.json"
( /usr/bin/time -l "$EXE" --screenshot "$SHOT" >"$LOG" 2>"$TIMELOG" ) &
APP_PID=$!
( sleep "$RUN_TIMEOUT"; kill -9 "$APP_PID" >/dev/null 2>&1; pkill -9 -f hanabi.exe >/dev/null 2>&1 ) &
WATCH_PID=$!
wait "$APP_PID" 2>/dev/null
APP_RC=$?
kill "$WATCH_PID" >/dev/null 2>&1 || true
wait "$WATCH_PID" 2>/dev/null || true

# Parse metrics.
STARTUP_MS=$(grep -Eo 'Startup: [0-9]+ ms' "$LOG" | grep -Eo '[0-9]+' | head -1)
FIRSTFRAME_MS=$(grep -Eo 'FirstFrame: [0-9]+ ms' "$LOG" | grep -Eo '[0-9]+' | head -1)
# /usr/bin/time -l reports "maximum resident set size" in BYTES on modern macOS.
RSS_BYTES=$(grep -E 'maximum resident set size' "$TIMELOG" | grep -Eo '[0-9]+' | head -1)

# The gating latency metric is FirstFrame if present, else Startup.
LAUNCH_MS="${FIRSTFRAME_MS:-$STARTUP_MS}"
LAUNCH_METRIC="FirstFrame"
[ -z "${FIRSTFRAME_MS:-}" ] && LAUNCH_METRIC="Startup"

echo "=== hanabi launch perf ==="
echo "  Startup:    ${STARTUP_MS:-?} ms"
echo "  FirstFrame: ${FIRSTFRAME_MS:-?} ms"
if [ -n "${RSS_BYTES:-}" ]; then
    RSS_MB=$(( RSS_BYTES / 1024 / 1024 ))
    echo "  Peak RSS:   ${RSS_MB} MB (${RSS_BYTES} bytes, /usr/bin/time -l)"
else
    echo "  Peak RSS:   ? (could not parse /usr/bin/time -l)"
fi
echo "  App exit:   ${APP_RC}"
echo "  Gate: ${LAUNCH_METRIC} < ${STARTUP_CEILING_MS} ms, RSS < ${RSS_CEILING_MB} MB"

FAIL=0
if [ -z "${LAUNCH_MS:-}" ]; then
    echo "  FAIL: could not parse a launch time from app output" >&2; FAIL=1
elif [ "$LAUNCH_MS" -ge "$STARTUP_CEILING_MS" ]; then
    echo "  FAIL: ${LAUNCH_METRIC} ${LAUNCH_MS} ms >= ${STARTUP_CEILING_MS} ms" >&2; FAIL=1
fi
if [ -n "${RSS_BYTES:-}" ]; then
    if [ "$RSS_MB" -ge "$RSS_CEILING_MB" ]; then
        echo "  FAIL: peak RSS ${RSS_MB} MB >= ${RSS_CEILING_MB} MB" >&2; FAIL=1
    fi
fi
if [ "$APP_RC" -ne 0 ]; then
    echo "  FAIL: app exited non-zero (${APP_RC})" >&2; FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "  PASS (<${STARTUP_CEILING_MS}ms, <${RSS_CEILING_MB}MB)"
    exit 0
fi
echo "  FAILED launch-perf gate"
exit 1
