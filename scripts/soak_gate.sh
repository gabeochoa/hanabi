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
# WHERE THE THRESHOLDS COME FROM  (re-measured 2026-08-25 on this branch,
# gabeochoa-mac-GRQ7Y259H4 — a shared box; the three batches below were taken
# at load averages ~8, ~11 and ~27, deliberately, because a gate that only
# holds on a quiet machine is a gate that gets disabled)
#
# The run is 2000 frames in 250-frame buckets, so six buckets land past the
# 500-frame warm-up and the verdict is the median of fifteen pairwise slopes
# (src/util/trend.h). What changed from the previous tuning, and why:
#
#   * 1000 frames gave FOUR buckets, two of them inside the warm-up, so the
#     verdict was a two-point delta and the probe said so out loud ("ONLY TWO
#     BUCKETS"). 2000 frames costs 0.9 s more and buys a real slope, the
#     `rising` confidence column, and a median that one bad bucket cannot
#     move.
#   * SMALLER BUCKETS WERE TRIED FIRST AND ARE WORSE. 1000 frames in 100-frame
#     buckets gives five fit points and reads a clean RSS spread of 206.7 KB
#     per 1000 frames against 0.0 at 250-frame buckets. RSS noise is a whole
#     page arriving at once, so dividing it by a shorter window multiplies it;
#     more buckets only helps if they are not also noisier. The way to more
#     points on this metric is more FRAMES at the same bucket size.
#
#   34 clean runs, three load levels (12 quiet, 12 under 10 spinners, 10 under
#   24 spinners at load average 27), worst sample of each column:
#
#     RSS          +243.2 KB per 1000 frames
#     heap bytes     +42.3 KB
#     heap blocks    +16.0
#     cpu time        +0.1 ms
#     entities        +0.0
#
#   the same binary with ONE `const hanabi::AutoreleaseFrame framePool;` line
#   deleted from the soak loop — the shape the regression will actually take,
#   because that is what a refactor does to a four-line RAII object with no
#   callers:
#
#     RSS         +2880.0 KB per 1000 frames    rising 1.00
#     heap bytes  +2750.4 KB                    rising 1.00
#     heap blocks +9996.0                       rising 1.00
#     cpu time       -0.0 ms                    rising 0.33
#
#   budget      worst clean     defect     headroom over clean / under defect
#   RSS 512          +243.2    +2880.0            2.1x / 5.6x
#   heap 256          +42.3    +2750.4            6.1x / 10.7x
#   blocks 1000       +16.0    +9996.0           62.5x / 10.0x
#   cpu 1.0            +0.1        n/a           10.0x /  —
#   entities 25         +0.0        n/a
#
# HEAP BLOCKS IS GATED FOR THE FIRST TIME, at 1000 per 1000 frames — one leaked
# block a frame. It was report-only because a two-point delta over 1000 frames
# sawtoothed by thousands; the median over 2000 frames does not, and this is
# now the SHARPEST of the four. A leak of one 32-byte map node a frame is 32 KB
# per 1000 frames of heap and zero KB of RSS — under both memory budgets, over
# this one by a factor of two.
#
# CPU TIME, not wall clock. See src/util/soak.h::verdict: at load average 27
# the wall column's clean spread is 1.5 ms per 1000 frames and the CPU
# column's is 0.1. The old 3.0 ms budget was on wall and had 1.15x of headroom
# over a clean sample taken on a busy hour. 1.0 ms on the CPU clock has 10x.
#
# ENTITIES is +0 on every clean run ever measured — the ECS is torn down and
# rebuilt each frame — so 25 per 1000 frames means "not zero any more".
#
# RSS IS THE LOOSEST OF THE FOUR and stays that way. It is page-granular and
# the OS faults pages in for reasons that have nothing to do with the app: the
# +243.2 worst sample came from the load-27 batch with a heap delta of +42 KB,
# so ~200 KB of it was not the app allocating anything. It is kept because it
# is the metric the reported symptom is about, and it is kept alongside the
# other three rather than instead of them.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# Overridable so a gate can be pointed at a DELIBERATELY BROKEN build
# without rebuilding the tree — which is how every "verified red" claim in
# the commit log was taken. See docs/perf/GATES.md.
EXE="${HANABI_EXE:-$ROOT/output/hanabi.exe}"
# shellcheck source=scripts/watchdog.sh
. "$ROOT/scripts/watchdog.sh"
# shellcheck source=scripts/fresh.sh
. "$ROOT/scripts/fresh.sh"

SOAK_FRAMES="${HANABI_SOAK_GATE_FRAMES:-2000}"
SOAK_EVERY="${HANABI_SOAK_GATE_EVERY:-250}"
export HANABI_SOAK_MAX_RSS_KB_PER1K="${HANABI_SOAK_MAX_RSS_KB_PER1K:-512}"
export HANABI_SOAK_MAX_HEAP_KB_PER1K="${HANABI_SOAK_MAX_HEAP_KB_PER1K:-256}"
export HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K="${HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K:-1000}"
export HANABI_SOAK_MAX_ENT_PER1K="${HANABI_SOAK_MAX_ENT_PER1K:-25}"
export HANABI_SOAK_MAX_MS_PER1K="${HANABI_SOAK_MAX_MS_PER1K:-1.0}"
# GPU bytes, from -[MTLDevice currentAllocatedSize] via the device sokol
# created. The tightest budget here by two orders of magnitude, and it can be:
# nothing in a steady-state frame allocates on the GPU, so this column has no
# noise for a budget to absorb. Measured flat to the KILOBYTE across every
# arm -- idle, threads, tabs, read and search, 1500 frames each over a
# 2000-session catalog, GPU 43,600 KB in every bucket of every one. 64 KB is
# one 128x128 RGBA texture, the smallest thing whose appearance every frame
# would be a real defect. See docs/perf/GATES.md, including what this gate
# CANNOT see: sokol's pools are fixed, so a runaway texture leak plateaus.
export HANABI_SOAK_MAX_GPU_KB_PER1K="${HANABI_SOAK_MAX_GPU_KB_PER1K:-64}"

# Same isolation as measure_launch.sh: the deterministic offline catalog, and
# no chance of picking up a real backend from someone's ~/.config/hanabi.
# Pin the mock's clock. Without it a message twelve hours old lands on a
# different calendar day depending on what time of night the run happened, the
# transcript grows or loses a date divider, and the diffable report shows a
# four-line diff about nothing. 1787000000 is 2026-08-17T00:53:20Z, chosen only
# for being fixed. See api::mock_now.
export HANABI_MOCK_NOW="${HANABI_MOCK_NOW:-1787000000}"
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

require_fresh_build "$EXE" || exit 2

echo "=== hanabi soak gate ==="
echo "  ${SOAK_FRAMES} frames, buckets of ${SOAK_EVERY}, mock catalog, headless"
echo "  budget: RSS +${HANABI_SOAK_MAX_RSS_KB_PER1K} KB, heap +${HANABI_SOAK_MAX_HEAP_KB_PER1K} KB, blocks +${HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K}, GPU +${HANABI_SOAK_MAX_GPU_KB_PER1K} KB, entities +${HANABI_SOAK_MAX_ENT_PER1K}, cpu +${HANABI_SOAK_MAX_MS_PER1K} ms — all per 1000 frames"

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
    watchdog_start "$APP_PID" "$RUN_TIMEOUT" kill_own_runs
    wait "$APP_PID" 2>/dev/null
    rc=$?
    watchdog_stop

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
if ! grep -qE '^\[soak\] (PASS|-+ SOAK GATE)' "$LOG"; then
    echo "  FAIL: the run ended before it reached a verdict (rc=${rc}), twice." >&2
    echo "        Nothing was measured, so this is not a leak — something killed" >&2
    echo "        the process. On this machine the usual cause is another" >&2
    echo "        worktree: scripts/review_shots.sh kills output/hanabi.exe in" >&2
    echo "        EVERY worktree it finds, not just its own. Re-run:" >&2
    echo "            make soak-gate" >&2
    exit 1
fi
echo "  FAILED soak gate (rc=${rc}) — see docs/perf/GATES.md" >&2
exit 1
