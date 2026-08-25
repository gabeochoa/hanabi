#!/usr/bin/env bash
# scripts/soak_gate.sh — the SHORT soak gate. Runs inside `make test`.
#
# WHAT IT IS FOR. `make test` renders 45 frames and asserts on the 45th;
# measure_launch.sh gates the first frame and the peak RSS of a process under a
# second old; `make perf` times thread switches in-process. All three are
# budgets on a young idle app, and a leak IS a young idle app looking fine.
# The app shipped a ~9 MB/minute Metal leak with every one of those green.
#
# So this one measures the SLOPE. It runs the real render loop for a few
# hundred frames through the headless one-shot path and fails if resident size
# or live heap bytes are trending up. It costs about a second and a half.
#
# ---------------------------------------------------------------------------
# WHERE THE THRESHOLDS COME FROM  (measured 2026-08-25, main @ 3bb921d,
# gabeochoa-mac-GRQ7Y259H4 — a shared box, load averages 10 to 34 across the
# samples below, which is the point: none of these numbers moved with it)
#
# The run is HANABI_SOAK=1000 HANABI_SOAK_EVERY=250, so the verdict compares
# the bucket ending at frame 500 with the one ending at frame 1000 — a
# 500-frame window, 8.3 seconds of wall clock at 60fps, after 120 unmeasured
# settle frames and two further buckets thrown away.
#
#   clean main, nine consecutive runs, load averages 10 to 34
#     RSS  per 1000 frames:   +0, +0, +32, +32, +64, +96, +96, +192 KB
#     heap per 1000 frames:  -19, -19, -1, +13, +13, +45, +61, +77 KB
#
#   the same binary with the autorelease pool gone — twice: once with the
#   pool class compiled out wholesale, once with a SINGLE
#   `const hanabi::AutoreleaseFrame framePool;` line deleted from the soak
#   loop, which is the shape the refactor will actually take
#     RSS  per 1000 frames:  +2784, +2816, +2848, +2912, +3040 KB
#     heap per 1000 frames:  +2739, +2759, +2775, +2790, +2915 KB
#
# 512 KB per 1000 frames sits 2.7x above the worst clean sample and 5.4x below
# the smallest defective one. The two clouds are 15x apart at their nearest
# edges. RSS is the noisier of the two metrics by a long way — that +192 KB
# sample came with a heap delta of -0.8 KB, so it was pages faulted in by
# something other than the app's own allocations — which is what the single
# retry below is for, and why heap bytes is gated alongside it rather than
# instead of it. Growth in either is load-INSENSITIVE: a leak leaks at the same
# rate whatever else the box is doing.
#
# A SHORTER RUN WAS TRIED AND REJECTED. At 600 frames / 150-frame buckets the
# window lands at frames 300-600, where the app has not finished settling: five
# clean runs there read up to +267 KB per 1000, half the budget, for no defect
# at all. 1000 frames costs about a second and a half more and moves the worst
# clean sample from +267 to +96.
#
# ENTITIES is +0 on every clean run ever measured — the ECS is torn down and
# rebuilt each frame — so 25 per 1000 frames means "not zero any more".
#
# FRAME TIME is deliberately loose: 3 ms per 1000 frames against clean samples
# spanning -0.9 to +0.3. This box runs three other agents' builds, and frame
# time is the one metric here that a busy machine moves. A gate that flakes
# gets disabled, and a disabled gate is worse than no gate. The long-form
# `make soak` runs the tight 0.5 ms/1000 default instead, because a
# several-thousand-frame run has the resolution to mean it.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

SOAK_FRAMES="${HANABI_SOAK_GATE_FRAMES:-1000}"
SOAK_EVERY="${HANABI_SOAK_GATE_EVERY:-250}"
export HANABI_SOAK_MAX_RSS_KB_PER1K="${HANABI_SOAK_MAX_RSS_KB_PER1K:-512}"
export HANABI_SOAK_MAX_HEAP_KB_PER1K="${HANABI_SOAK_MAX_HEAP_KB_PER1K:-512}"
export HANABI_SOAK_MAX_ENT_PER1K="${HANABI_SOAK_MAX_ENT_PER1K:-25}"
export HANABI_SOAK_MAX_MS_PER1K="${HANABI_SOAK_MAX_MS_PER1K:-3.0}"

# Same isolation as measure_launch.sh: the deterministic offline catalog, and
# no chance of picking up a real backend from someone's ~/.config/hanabi.
export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/soak-gate.json"
export HANABI_SOAK="$SOAK_FRAMES"
export HANABI_SOAK_EVERY="$SOAK_EVERY"

SHOT="$(mktemp -t hanabi_soak_XXXX).png"
LOG="$(mktemp -t hanabi_soak_XXXX).log"
RUN_TIMEOUT=120

# Scoped to THIS worktree's binary path, not the name: several checkouts test
# on this machine at once and a bare `pkill -f hanabi.exe` kills their runs.
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -f "$SHOT" "$LOG"; }
trap cleanup EXIT

if [ ! -x "$EXE" ]; then
    echo "soak_gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

echo "=== hanabi soak gate ==="
echo "  ${SOAK_FRAMES} frames, buckets of ${SOAK_EVERY}, mock catalog, headless"
echo "  budget: RSS +${HANABI_SOAK_MAX_RSS_KB_PER1K} KB, heap +${HANABI_SOAK_MAX_HEAP_KB_PER1K} KB, entities +${HANABI_SOAK_MAX_ENT_PER1K}, frame time +${HANABI_SOAK_MAX_MS_PER1K} ms — all per 1000 frames"

# One retry, and ONLY one. A leak leaks on every run (measured: 2848, 2912 and
# 3040 KB per 1000 frames on three consecutive defective runs), so retrying
# cannot launder a real failure; what it does absorb is the one-in-many run
# where this box's allocator happens to fault in a couple of extra pages inside
# the window. Two independent failures is the signal.
ATTEMPTS="${HANABI_SOAK_GATE_ATTEMPTS:-2}"
rc=1
for attempt in $(seq 1 "$ATTEMPTS"); do
    ( "$EXE" --screenshot "$SHOT" >"$LOG" 2>&1 ) &
    APP_PID=$!
    ( sleep "$RUN_TIMEOUT"; kill -9 "$APP_PID" >/dev/null 2>&1; kill_own_runs ) &
    WATCH_PID=$!
    wait "$APP_PID" 2>/dev/null
    rc=$?
    kill "$WATCH_PID" >/dev/null 2>&1 || true
    wait "$WATCH_PID" 2>/dev/null || true

    grep -E '^\[soak\]' "$LOG" | sed 's/^/  /'
    if [ "$rc" -eq 0 ] && grep -q '^\[soak\] PASS' "$LOG"; then
        echo "  PASS (attempt ${attempt})"
        exit 0
    fi
    if [ "$attempt" -lt "$ATTEMPTS" ]; then
        echo "  attempt ${attempt} failed (rc=${rc}); re-running once to rule out a"
        echo "  one-off — a real leak fails every attempt."
    fi
done

if ! grep -q '^\[soak\]' "$LOG"; then
    echo "  FAIL: the app produced no [soak] output at all (rc=${rc})." >&2
    echo "        That is a crash or a build without src/util/soak.h wired in," >&2
    echo "        not a leak. Last 20 lines:" >&2
    tail -20 "$LOG" | sed 's/^/        /' >&2
    exit 1
fi
echo "  FAILED soak gate (rc=${rc}) — see docs/perf/GATES.md" >&2
exit 1
