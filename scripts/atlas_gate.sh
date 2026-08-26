#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# scripts/atlas_gate.sh -- the glyph-atlas detector, proved against a REAL
# overflow, and the app proved clear of one.
#
# WHY THIS GATE EXISTS. afterhours_gaps.md #211: the font atlas is a fixed
# 2048x2048, nothing registers fontstash's FONS_ATLAS_FULL callback, and the
# symptom of filling it is that `measure_text` returns a WRONG NUMBER and then
# 0.0, with no error, no log and no return code. That is not a rendering
# glitch. `measure_text` is what every wrap, hug, ellipsis and virtualization
# spacer in this app is computed from, and hanabi memoizes measurement in four
# places, so a poisoned number is both used and remembered.
#
# hanabi cannot fix it (vendor/afterhours is read-only and the FONScontext is
# a backend-private static). What it can do is refuse to trust the answer --
# src/util/atlas_guard.h -- and that guard has the failure mode every guard for
# a rare condition has: it is written, it is shipped, and nobody ever sees it
# work. So this gate does not check that the app is fine. It checks that the
# DETECTOR FIRES, by filling the atlas on purpose.
#
# TWO ARMS.
#
#   1. OVERFLOW. `hanabi.exe --atlas-stress` drives the printable ASCII set
#      through the app's own measurement seam (theme::text_px) at escalating
#      point sizes until the atlas cannot take another glyph. It exits 0 only
#      if the atlas overflowed AND the detector raised a fault; 1 if it
#      overflowed silently (the regression); 2 if it never overflowed (the run
#      proves nothing).
#   2. CLEAN. A normal headless launch of the real UI must raise ZERO faults.
#      #211's own measurement says hanabi has headroom -- four faces, fourteen
#      sizes, again at 2x/3x/4x/6x -- and this is the assertion that keeps that
#      true as sizes and scripts are added. It is also the arm that would catch
#      a detector so trigger-happy it fires on ordinary text.
#
# NO MILLISECONDS. Both arms are counts and exit codes, so this box being busy
# cannot move the verdict (docs/perf/GATES.md).
#
# Usage: scripts/atlas_gate.sh [path-to-hanabi.exe]
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${1:-$ROOT/output/hanabi.exe}"
# shellcheck source=scripts/watchdog.sh
. "$ROOT/scripts/watchdog.sh"

if [ ! -x "$EXE" ]; then
    echo "atlas_gate: no binary at $EXE — run 'make' first." >&2
    exit 2
fi

STRESS_LOG="$(mktemp -t hanabi_atlas_XXXX).log"
CLEAN_LOG="$(mktemp -t hanabi_atlas_XXXX).log"
SHOT="$(mktemp -t hanabi_atlas_XXXX).png"
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -f "$STRESS_LOG" "$CLEAN_LOG" "$SHOT"; }
trap cleanup EXIT

export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/atlas-gate.json"

echo "=== glyph atlas gate ==="
FAIL=0

# --- arm 1: the detector under a real overflow ------------------------------
"$EXE" --atlas-stress >"$STRESS_LOG" 2>&1
RC=$?
FIRST_BAD="$(sed -nE 's/^  first bad measurement: +(.*)$/\1/p' "$STRESS_LOG" | head -1)"
FIRED="$(sed -nE 's/^  detector fired: +(.*)$/\1/p' "$STRESS_LOG" | head -1)"
REF="$(sed -nE 's/^  cached reference held: +(.*)$/\1/p' "$STRESS_LOG" | head -1)"
echo "  overflow arm:"
echo "    first bad measurement:  ${FIRST_BAD:-(none)}"
echo "    detector:               ${FIRED:-(no result line)}"
echo "    cached reference:       ${REF:-(none)}"
case "$RC" in
    0) echo "    ok — the atlas overflowed and the detector said so" ;;
    1) echo "    FAIL: the atlas overflowed and NOTHING SAID SO." >&2
       echo "          That silence is the whole of afterhours_gaps.md #211." >&2
       FAIL=1 ;;
    2) echo "    FAIL: the atlas never overflowed, so this run proves nothing" >&2
       echo "          about the detector. The sweep's ceiling needs raising." >&2
       FAIL=1 ;;
    *) echo "    FAIL: --atlas-stress exited ${RC}" >&2
       tail -5 "$STRESS_LOG" | sed 's/^/          /' >&2
       FAIL=1 ;;
esac

# --- arm 2: an ordinary run raises nothing ----------------------------------
# HANABI_ATLAS_STRICT=1 turns the first fault into an abort, so this arm needs
# no parsing: a normal headless render either exits cleanly or it does not.
env HANABI_ATLAS_STRICT=1 "$EXE" --screenshot "$SHOT" >"$CLEAN_LOG" 2>&1 &
APP_PID=$!
watchdog_start "$APP_PID" 30 kill_own_runs
wait "$APP_PID"; RC2=$?
watchdog_stop
# `grep -c` prints 0 and exits 1 on no match, so a `|| echo 0` fallback
# appends a SECOND zero and the count becomes the two-line string "0\n0".
FAULTS="$(grep -c 'GLYPH ATLAS FAULT' "$CLEAN_LOG" 2>/dev/null)"
FAULTS="${FAULTS:-0}"
echo "  clean arm:"
echo "    headless render exit:   ${RC2}"
echo "    faults raised:          ${FAULTS}"
if [ "$RC2" -ne 0 ] || [ "$FAULTS" -ne 0 ]; then
    echo "    FAIL: a normal render raised a glyph-atlas fault. Either the app" >&2
    echo "          has reached the 2048x2048 ceiling (#211 is now live and the" >&2
    echo "          layout is being computed from wrong widths), or the detector" >&2
    echo "          fires on ordinary text." >&2
    tail -6 "$CLEAN_LOG" | sed 's/^/          /' >&2
    FAIL=1
else
    echo "    ok — the real UI measures every string it draws without a fault"
fi

if [ "$FAIL" -eq 0 ]; then
    echo "  PASS"
    exit 0
fi
echo "  FAIL" >&2
exit 1
