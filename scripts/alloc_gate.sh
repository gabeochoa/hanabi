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
# THE ARMS, AND WHY THESE FIVE
#
#   home20     the hand-written 20-session fixture, Home view. The floor: what
#              an idle app costs with almost nothing on screen.
#   home2000   the same view over a 2000-session catalog. The sidebar is
#              virtualized, so this is NOT 100x home20 — the gap between the
#              two is what still scales with the catalog.
#   thread480  a 480-message thread open over that same catalog. The transcript
#              is where the per-frame text work lives, and it is the arm that
#              the wrap memo moved most.
#   tabs20     twenty open tabs in overflow. HEAD e249747 measured 774/f; this
#              branch measures 640/f after removing render/menu vectors,
#              caching semantic labels, and moving the ComponentConfig into
#              each widget. The 770 ceiling keeps 20% headroom.
#   search2000 the sidebar's SEARCH FILTER live over the 2000-session catalog.
#              Every arm above has an EMPTY query, and an empty query is the
#              one state of this filter that costs nothing: SidebarBuckets
#              reuses its kept answer and visits no session at all. With a
#              query it rescans the catalog every frame by design (the disk
#              cache publishes no revision), so this is the arm that gates what
#              ONE VISIT costs — and a visit that allocates is a malloc per
#              session per frame.
#   palette2000 the command palette open over the same catalog with a query
#              that matches nothing, so build_rows walks every session instead
#              of stopping at kMaxRows. Same filter shape as the row above and
#              a different call site; the two moved together and are gated
#              together, because a primitive with one guarded caller grows a
#              second unguarded one.
#
# ---------------------------------------------------------------------------
# WHERE THE CEILINGS COME FROM
#
# First set, SUPERSEDED and kept only for provenance: home20, home2000 and
# thread480 — tabs20 and draft6 are not in it. Measured 2026-08-25 on
# gabeochoa-mac-GRQ7Y259H4, each figure the steady-state bucket of a 600-frame
# run, reproduced across runs to the unit. The ceilings it set are the ones the
# current set below replaces:
#
#   arm         main @ ddb391c   that branch   ceiling then
#   home20              2550.0         827.0           1000
#   home2000            3535.0        1197.0           1450
#   thread480           6687.0        2740.0           3300
#
# Current set, measured 2026-09-02 on boulder-KF74T3NW36, same 600-frame runs,
# reproduced to the unit, against a frozen base binary built at 97c567e. What
# moved is src/ui/div.h: the app's div now MOVES its ComponentConfig into
# afterhours instead of handing it an lvalue for the by-value parameter to
# copy (docs/perf/ALLOCATIONS.md, tests/unit/test_div_move.cpp):
#
#   arm          97c567e    that branch   ceiling   headroom
#   home20          829.0         740.0       890      +20%
#   home2000       1181.0        1034.0      1250      +21%
#   tabs20          680.0         640.0       770      +20%
#   thread480      2707.0        2599.0      3120      +20%
#   draft6         1044.0         955.0      1150      +20%
#
# Current set, measured 2026-09-02 on boulder-KF74T3NW36, same 600-frame runs,
# reproduced to the unit over three repetitions, against main at 14312fe. What
# moved is render_home: Home's four capped sections are now WINDOWED, so the
# pane builds the cards a viewport holds instead of every card the cap allows
# (docs/perf/DIGEST.md, scripts/digest_gate.sh's home arm). Home built 75.7
# cards a frame and now builds 16.8; the three arms that open Home move and
# the two that do not are unchanged, to the allocation:
#
#   arm           14312fe    this branch   ceiling   headroom
#   home20          740.0         556.0       670      +20%
#   home2000       1034.0         608.0       730      +20%
#   tabs20          640.0         640.0       770      unchanged arm
#   thread480      2599.0        2599.0      3120      unchanged arm
#   draft6          955.0         766.0       920      +20%
#
# The two FILTER arms, added 2026-09-02 on boulder-KF74T3NW36 against main at
# 1e3626c, same 600-frame runs, each figure reproduced to the tenth over three
# repetitions. What moved is fmtutil::contains_lower: a case-insensitive
# substring test that folds the haystack IN PLACE instead of building and
# freeing a lowercased copy of it per candidate (docs/perf/ALLOCATIONS.md
# entry 7, tests/unit/test_contains_lower.cpp):
#
#   arm           1e3626c    this branch   ceiling   headroom
#   search2000     3190.0        1380.1      1660      +20%
#   palette2000    2658.0         643.0       780      +21%
#
# Both are ~1 allocation per session per frame before and ~0 after, so the
# ceilings hold a LEVEL that no longer scales with the catalog at all. The
# same two arms at a 20,020-session catalog read 20514.7 -> 2504.8 and
# 20689.0 -> 674.0; they are gated at 2,020 because that costs 8 s instead of
# 40 s and the defect is just as loud there (192% and 341% of ceiling).
#
# `gate_audit.py alloc.ci_copies_haystack` reads 2656.0 on the palette arm
# rather than the 2658.0 above, and the two allocations are worth knowing:
# the defect restores the HAYSTACK copy only, while this branch also stopped
# lowering the needle once per candidate. Same arm, two different defects.
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
EXE="${HANABI_ALLOC_EXE:-$ROOT/output/hanabi.exe}"

FRAMES="${HANABI_ALLOC_GATE_FRAMES:-600}"
EVERY="${HANABI_ALLOC_GATE_EVERY:-200}"
CEIL_HOME20="${HANABI_ALLOC_CEIL_HOME20:-670}"
CEIL_HOME2000="${HANABI_ALLOC_CEIL_HOME2000:-730}"
CEIL_THREAD480="${HANABI_ALLOC_CEIL_THREAD480:-3120}"
CEIL_TABS20="${HANABI_ALLOC_CEIL_TABS20:-770}"
# The composer holding a SIX-LINE DRAFT, standing still.
#
# The other four arms have an empty composer, and an empty composer is the one
# state of this widget that costs nothing. text_area re-wraps its whole text
# every frame -- unconditionally, with no memo, and through the raw backend
# measure rather than the shared TextMeasureCache (afterhours_gaps.md #305) --
# so the bill is proportional to what is typed and is paid again at 60Hz for a
# draft nobody is touching. Measured here: 811/f empty, 1025/f with six short
# lines in it, 1007/f with one 130-character line that does not even wrap.
#
# 1150 is where the PINNED vendor puts it plus the same ~20% headroom the other
# four carry. vendor_patches/305-text-area-wraps-every-frame.patch takes the
# same arm to 847/f, and when that lands upstream and the submodule pointer
# moves, this ceiling should come down with it. Leaving it high is
# what stops the number growing further in the meantime; it is not an
# endorsement of it.
CEIL_DRAFT6="${HANABI_ALLOC_CEIL_DRAFT6:-920}"
CEIL_SEARCH2000="${HANABI_ALLOC_CEIL_SEARCH2000:-1660}"
CEIL_PALETTE2000="${HANABI_ALLOC_CEIL_PALETTE2000:-780}"
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
ONLY="${HANABI_ALLOC_ONLY:-}"
case "$ONLY" in
    ""|home20|home2000|tabs20|thread480|draft6|search2000|palette2000) ;;
    *)
        echo "alloc_gate: unknown HANABI_ALLOC_ONLY '$ONLY'" >&2
        exit 2
        ;;
esac

# run_arm <name> <sessions> <ceiling> <open_tabs-json> <active-json> [extra env...]
#
# ARM_LIVE_COUNTER / ARM_LIVE_FLOOR, set immediately before a call, make the
# arm say whether its FIXTURE happened. Every arm here gates a per-something
# cost, and a fixture that produced no somethings allocates nothing and passes
# — the failure mode docs/perf/GATES.md section 0 found four times. The two
# filter arms are the ones exposed to it: a search field that stopped being
# typed into, or a palette that stopped opening, is a green arm measuring an
# empty room.
run_arm() {
    local name=$1 sessions=$2 ceiling=$3 tabs=$4 active=$5
    local liveCounter="${ARM_LIVE_COUNTER:-}" liveFloor="${ARM_LIVE_FLOOR:-0}"
    ARM_LIVE_COUNTER=""; ARM_LIVE_FLOOR=0
    shift 5
    [ -z "$ONLY" ] || [ "$ONLY" = "$name" ] || return 0
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
    local run_rc=0
    env HOME="$h" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
        HANABI_CONFIG=/nonexistent/hanabi/alloc-gate.json HANABI_PROF=1 \
        HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY="$EVERY" \
        HANABI_SOAK_WARM_FRAMES=0 HANABI_STRESS_SESSIONS="$sessions" \
        HANABI_SOAK_MAX_RSS_KB_PER1K=999999 \
        HANABI_SOAK_MAX_HEAP_KB_PER1K=999999 \
        HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K=999999 \
        HANABI_SOAK_MAX_ENT_PER1K=999999 HANABI_SOAK_MAX_MS_PER1K=999999 "$@" \
        "$EXE" --screenshot "$h/shot.png" > "$log" 2>&1 || run_rc=$?
    if [ "$run_rc" -ne 0 ]; then
        printf '  %-11s %12s  FAIL — executable exited %s\n' \
            "$name" "?" "$run_rc"
        tail -5 "$log" | sed 's/^/        /' >&2
        rm -rf "$h"
        fail=1
        return
    fi

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

    # The fixture check, BEFORE the number is believed. HANABI_PROF prints one
    # counter row per label as `[prof] <label> <calls> <calls/f>`; the floor is
    # on calls per frame.
    if [ -n "$liveCounter" ]; then
        local live
        live=$(awk -v k="$liveCounter" '$2==k {print $4}' "$log" | tail -1)
        if [ -z "$live" ] ||
           awk -v v="$live" -v f="$liveFloor" 'BEGIN{exit !(v+0 < f+0)}'; then
            printf '  %-11s %12s %10s %8s   NOT MEASURED — %s %s, floor %s/f\n' \
                "$name" "$allocs" "$ceiling" "$pct%" \
                "$liveCounter" \
                "${live:+$live/f}${live:-never counted}" "$liveFloor"
            echo "        The arm ran and allocated little, because its FIXTURE" >&2
            echo "        did not happen — not because the cost went away." >&2
            rm -rf "$h"
            fail=1
            return
        fi
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
run_arm tabs20       20   "$CEIL_TABS20"     '["t1","t2","t3","t4","t5","t6","t7","t8","t9","t10","r1","r2","r4","r5","r6","r7","r8","r9","r10","r11"]' '"r11"'
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

# The sidebar filter with a live query, and the palette filter with one that
# matches nothing. Both walk the whole catalog every frame on purpose; what is
# gated is that walking it does not allocate per session. Each carries a floor
# on the counter that says the walk HAPPENED WITH A QUERY IN IT — see run_arm.
# `sidebar.scan_visits` would NOT do: an empty query still rebuilds when the
# catalog revision moves, so flooring that one passes over the empty room it
# is supposed to catch.
ARM_LIVE_COUNTER=sidebar.query_visits ARM_LIVE_FLOOR=1000 \
    run_arm search2000  2000 "$CEIL_SEARCH2000"  '[]' '""' HANABI_STRESS=search
ARM_LIVE_COUNTER=palette.candidates ARM_LIVE_FLOOR=1000 \
    run_arm palette2000 2000 "$CEIL_PALETTE2000" '[]' '""' HANABI_PALETTE_DEMO=zzq

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
