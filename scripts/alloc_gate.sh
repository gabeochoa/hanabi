#!/usr/bin/env bash
# scripts/alloc_gate.sh — the ALLOCATION gate. Runs inside `make test`.
#
# WHAT IT IS FOR. Every other memory gate in this repo measures a SLOPE: does
# the app grow while it sits still. A frame that allocates four thousand times
# and frees all four thousand is flat on every one of them, and it is the
# reported symptom ("gets slower and slower until it freezes") just as surely
# as a leak is — thousands of mallocs a frame, sixty times a second, forever.
# soak_gate.sh cannot see it by construction. This gates the LEVEL.
#
# WHY A COUNT AND NOT A TIME. This box runs several agents' builds at once;
# load averages of 10 to 34 were normal while these numbers were taken, and the
# same binary's frame time swung between 1.6 ms and 6.5 ms during them. The
# allocation count did not move at all. Every arm below reads the same number
# on every run and in every bucket of a run, to within one allocation — not
# "close", equal — which is what makes an absolute ceiling honest here where a
# millisecond one would be a coin flip. (The one unit of slack is a bucket
# boundary: a 600-frame run and a 1000-frame run of the same binary can differ
# by 1, because a one-off allocation lands in a different bucket.)
#
# ---------------------------------------------------------------------------
# THE ARMS, AND WHY THESE THREE
#
#   home20     the hand-written 20-session fixture, Home view. The floor: what
#              an idle app costs with almost nothing on screen.
#   home2000   the same view over a 2000-session catalog. The sidebar is
#              virtualized, so this is NOT 100x home20 — the gap between the
#              two is what still scales with the catalog.
#   thread480  a 480-message thread open over that same catalog. The transcript
#              is where the per-frame text work lives, and it is the arm that
#              the wrap memo moved most.
#
# ---------------------------------------------------------------------------
# WHERE THE CEILINGS COME FROM (measured 2026-08-25 on gabeochoa-mac-GRQ7Y259H4,
# each figure the steady-state bucket of a 600-frame run, reproduced across
# runs to the unit)
#
#   arm         main @ ddb391c   this branch    ceiling   headroom
#   home20              2550.0         827.0       1000      +21%
#   home2000            3535.0        1197.0       1450      +21%
#   thread480           6687.0        2740.0       3300      +20%
#
# ~20% of headroom over the measured value on each arm. The number is exact,
# so headroom is not for noise — there is none. It is for the honest drift of
# a feature that legitimately adds a widget or a label, which should cost a
# few allocations and not five hundred. A change that needs more than 20% is a
# change whose allocation cost somebody should look at, which is the entire
# point.
#
# Text MEASUREMENT is not gated here even though it is a large share of the
# number: scripts/perf_text_gate.sh already gates it directly, and two gates
# on one property is one gate nobody maintains.
#
# ---------------------------------------------------------------------------
# REPRODUCING A FAILURE, AND CHECKING IT STILL BITES
#
# Two rehearsals were run, both real reverts rather than a lowered ceiling.
#
# 1. The whole branch. Build at the commit that adds only the instrument and
#    every arm is red at 2.4x to 3.0x of its ceiling:
#
#      arm             allocs/f    ceiling  of ceil   verdict
#      home20            2550.0       1000     255%   FAIL
#      home2000          3535.0       1450     244%   FAIL
#      thread480         6681.0       3300     202%   FAIL
#
# 2. ONE LINE, which is the shape the regression will actually take. In
#    src/ui/widget_epoch.h, point the app's mk wrapper back at the library's:
#
#      -        hanabi::ui::mk(parent, otherID, location);
#      +        afterhours::ui::imm::mk(parent, otherID, location);
#
#    Nothing else changes; nothing fails to compile; every pixel is identical.
#
#      home20            2466.0       1000     247%   FAIL
#      home2000          3361.0       1450     232%   FAIL
#      thread480         5803.0       3300     176%   FAIL
#
#    That is what this gate exists for: a one-line change with no visible
#    effect, inside a wrapper whose stated job is something else entirely
#    (stamping the frame that built a widget), that costs the app two thousand
#    mallocs a frame forever.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"

FRAMES="${HANABI_ALLOC_GATE_FRAMES:-600}"
EVERY="${HANABI_ALLOC_GATE_EVERY:-200}"
CEIL_HOME20="${HANABI_ALLOC_CEIL_HOME20:-1000}"
CEIL_HOME2000="${HANABI_ALLOC_CEIL_HOME2000:-1450}"
CEIL_THREAD480="${HANABI_ALLOC_CEIL_THREAD480:-3300}"
# The composer holding a SIX-LINE DRAFT, standing still.
#
# The other three arms have an empty composer, and an empty composer is the one
# state of this widget that costs nothing. text_area re-wraps its whole text
# every frame -- unconditionally, with no memo, and through the raw backend
# measure rather than the shared TextMeasureCache (afterhours_gaps.md #305) --
# so the bill is proportional to what is typed and is paid again at 60Hz for a
# draft nobody is touching. Measured here: 811/f empty, 1025/f with six short
# lines in it, 1007/f with one 130-character line that does not even wrap.
#
# 1250 is where the PINNED vendor puts it plus the same ~20% headroom the other
# three carry. vendor_patches/305-text-area-wraps-every-frame.patch takes the
# same arm to 847/f, and when that lands upstream and the submodule pointer
# moves, this ceiling should come down to ~1050 with it. Leaving it high is
# what stops the number growing further in the meantime; it is not an
# endorsement of it.
CEIL_DRAFT6="${HANABI_ALLOC_CEIL_DRAFT6:-1250}"
REPORT_ONLY="${HANABI_ALLOC_GATE_REPORT:-0}"

if [ ! -x "$EXE" ]; then
    echo "alloc_gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

# Scoped to THIS worktree's binary path, not the name: several checkouts test
# on this machine at once and a bare `pkill -f hanabi.exe` kills their runs.
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
trap kill_own_runs EXIT

fail=0

# run_arm <name> <sessions> <ceiling> <open_tabs-json> <active-json> [extra env...]
run_arm() {
    local name=$1 sessions=$2 ceiling=$3 tabs=$4 active=$5
    shift 5
    local h
    h="$(mktemp -d)"
    mkdir -p "$h/Library/Application Support/hanabi"
    cat > "$h/Library/Application Support/hanabi/settings.json" <<J
{"window_width":1180,"window_height":949,"open_tabs":${tabs},"active_tab":${active},"theme":"dark"}
J
    local log="$h/run.log"
    # The env assignments are passed as separate argv entries on purpose: a
    # single "A=1 B=2" string sets ONE variable named A to "1 B=2" under zsh,
    # which does not word-split unquoted expansions. That mistake silently
    # measured the wrong fixture for an afternoon.
    env HOME="$h" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
        HANABI_CONFIG=/nonexistent/hanabi/alloc-gate.json HANABI_PROF=1 \
        HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY="$EVERY" \
        HANABI_STRESS_SESSIONS="$sessions" "$@" \
        "$EXE" --screenshot "$h/shot.png" > "$log" 2>&1

    # Read the per-BUCKET column, not the verdict line. The verdict belongs to
    # soak.h's trend machinery, which refuses to report at all until enough
    # buckets land past its 500-frame warm-up -- a correct rule for a slope and
    # an irrelevant one for a level, and a gate that silently stops measuring
    # when an unrelated threshold moves is worse than no gate. The last three
    # buckets, medianed: they are equal to within one allocation, so this is
    # not smoothing noise, it is refusing to report a bucket that a stray
    # async load happened to land in.
    local allocs
    allocs=$(awk '/^\[soak\] frame .* allocs /{v[n++]=$(NF-1)}
                  END{ if (n==0) exit;
                       if (n<3) { print v[n-1]; exit }
                       a=v[n-3]; b=v[n-2]; c=v[n-1];
                       print (a<b) ? ((b<c)?b:((a<c)?c:a)) : ((a<c)?a:((b<c)?c:b)) }' "$log")

    if [ -z "$allocs" ]; then
        # A run that produced no verdict measured NOTHING. That is a killed or
        # crashed process, not a regression, and reporting it as one sends the
        # reader looking for an allocation that does not exist. On this machine
        # the usual cause is another worktree: scripts/review_shots.sh kills
        # output/hanabi.exe in EVERY worktree it can find.
        printf '  %-11s %12s  INCOMPLETE — no verdict line; re-run this arm alone\n' \
            "$name" "?"
        tail -5 "$log" | sed 's/^/        /' >&2
        rm -rf "$h"
        fail=1
        return
    fi

    local verdict pct
    pct=$(awk -v a="$allocs" -v c="$ceiling" 'BEGIN{printf "%.0f", 100*a/c}')
    verdict="ok"
    if awk -v a="$allocs" -v c="$ceiling" 'BEGIN{exit !(a > c)}'; then
        verdict="FAIL"
        fail=1
    fi
    printf '  %-11s %12s %10s %7s%%   %s\n' "$name" "$allocs" "$ceiling" \
        "$pct" "$verdict"

    rm -rf "$h"
}

echo "=== hanabi allocation gate ==="
echo "  ${FRAMES} frames per arm, buckets of ${EVERY}, mock catalog, headless"
echo "  steady-state operator new calls per frame — a LEVEL, not a slope"
printf '  %-11s %12s %10s %8s   %s\n' "arm" "allocs/f" "ceiling" "of ceil" "verdict"

run_arm home20      20   "$CEIL_HOME20"     '[]'       '""'
run_arm home2000  2000   "$CEIL_HOME2000"   '[]'       '""'
run_arm thread480 2000   "$CEIL_THREAD480"  '["rbig"]' '"rbig"' \
    HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_TURNS=120
# HANABI_REPLY_DEMO seeds the composer that is on screen with no thread open,
# which is the Home one -- the same widget, the same code path, and the arm the
# empty-composer figure above was taken on, so the two numbers subtract.
run_arm draft6      20   "$CEIL_DRAFT6"     '[]'       '""' \
    "HANABI_REPLY_DEMO=line one of six and it is long enough to wrap once on its own inside the field
line two
line three
line four
line five
line six"

if [ "$fail" -ne 0 ] && [ "$REPORT_ONLY" != "1" ]; then
    cat >&2 <<'MSG'

  FAILED allocation gate.

  Nothing leaked — these allocations are all freed before the next frame, so
  soak-gate is green and RSS is flat. What grew is malloc TRAFFIC, paid again
  every frame at 60 frames a second.

  To see where, with the call sites named:

      scripts/alloc_sites.sh 2000 300            # the home arm
      HANABI_SITES_BIG=1 scripts/alloc_sites.sh  # with a thread open

  The causes this project has actually had, in order:
    1. a std::string built per widget per frame for a label, an id or a debug
       name. libc++'s small-string buffer is 22 characters; one character over
       and it is a malloc, and afterhours copies a ComponentConfig three times
       on the way into a widget, so it is four (afterhours_gaps.md #181).
    2. a pure derivation recomputed per row per frame instead of memoized on
       its argument tuple (src/util/text_memo.h is the pattern).
    3. a std::function whose capture does not fit libc++'s 24-byte inline
       buffer, cloned once per config copy per widget per frame.
    4. a container rebuilt by value per frame, or grown without reserve() to a
       size that was already known.

  docs/perf/ALLOCATIONS.md has the full map.
MSG
    exit 1
fi
if [ "$fail" -ne 0 ]; then
    echo "  (report-only: HANABI_ALLOC_GATE_REPORT=1)"
fi
echo "  PASS"
