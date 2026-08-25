#!/usr/bin/env bash
# scripts/soak.sh — the LONG soak. `make soak`. Run it before a release.
#
# The gate inside `make test` (scripts/soak_gate.sh) is 2000 frames of an idle
# app: short enough that nobody minds it, strict enough to catch a leak of one
# small block a frame. This is the other end — every scenario the stress
# driver has, plus an arm at a catalog a hundred times bigger than the
# fixture.
#
#   make soak                        # every arm
#   make soak ARMS="churn open"      # just these
#   make soak FRAMES=20000           # a long night
#   make soak JOBS=1                 # serially (see PARALLELISM below)
#
# ---------------------------------------------------------------------------
# PARALLELISM, AND WHY IT IS SOUND NOW AND WAS NOT BEFORE
#
# The arms are independent processes and used to run one after another, which
# on this machine is the difference between a soak somebody runs and a soak
# somebody skips. They could not be parallelised while the frame-time gate was
# on the WALL clock: four arms sharing four cores make each other slower, and
# every arm would have been measuring the other three.
#
# Every gated metric is now load-insensitive. RSS, live heap bytes, live
# blocks and entity count are counts of things, not of time — a byte is a byte
# whoever else is running. Frame time is gated on CLOCK_THREAD_CPUTIME_ID,
# which counts only cycles this thread was actually given, so being descheduled
# by a sibling arm costs it nothing. Wall clock is still reported and is the
# one column a parallel run makes meaningless; it is report-only for exactly
# this reason.
#
# Verified rather than argued: JOBS=1 and JOBS=4 over the same arms produce
# the same verdicts and the same diffable report. `make soak JOBS=1` is there
# for when that has to be checked again.
# ---------------------------------------------------------------------------
#
# WHY EACH ARM IS HERE
#   idle     the control. Nothing is touched. Anything that grows here grows
#            for no reason at all, which is the strongest possible finding.
#   scroll   the reported symptom was "scroll the sidebar up and down until it
#            breaks". Row build + clip + layout, sixty frames down and sixty up.
#   read     the same for the TRANSCRIPT, which is the pane with the genuinely
#            large content.
#   threads  opening a thread is the heaviest thing the app does: a fetch, a
#            transcript rebuild, a tab. One every 30 frames, forever.
#   tabs     round-robin between previews. NOTE: this arm does not accumulate
#            tabs and never did — `requestOpenTab` opens a PREVIEW, which
#            reuses one slot. `open` is the arm that grows the strip.
#   search   type a query, hold it, clear it. The hold is the point: a filter
#            that re-derives every frame costs the same whether or not the
#            query changed.
#   churn    open a thread, leave it, close it, open the next. The motion that
#            found docs/perf/MEMORY.md entry 1 — five per-session maps that
#            nothing erased — done by hand, and never automated until now.
#   resize   drag the window narrower and wider. LAYOUT only by default:
#            afterhours_gaps.md #200 is a 4.8 MB-per-1000-frame leak in the
#            headless backend's render-target recreation, which would swamp
#            anything hanabi could do. HANABI_STRESS_RESIZE_BACKEND=1 puts it
#            back and reproduces #200.
#   mixed    all of the above interleaved, which is the only arm that
#            resembles use. Report-only; see GROWING ARMS.
#   open     open every thread as a KEPT tab and never close one. Report-only;
#            see GROWING ARMS.
#   bigidle  the control arm against a 2000-session catalog. A per-row leak is
#            100x more visible; a cache sized by the catalog shows as a higher
#            plateau rather than a slope.
#
# GROWING ARMS. `open` and `mixed` accumulate tabs on purpose, so memory growth
# in them is a COST PER TAB and not a leak — gating them on flatness would be
# asserting that opening a hundred tabs is free, which is both false and not
# what anyone wants. They run report-only, and what makes them worth running is
# the other two things this file produces: the diffable structure (widgets per
# tab is exact and a regression there is a one-line diff) and the break
# conditions (`make stress-break`, which says how many tabs the app survives).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_EXE:-$ROOT/output/hanabi.exe}"
# shellcheck source=scripts/watchdog.sh
. "$ROOT/scripts/watchdog.sh"
# shellcheck source=scripts/fresh.sh
. "$ROOT/scripts/fresh.sh"

FRAMES="${HANABI_SOAK_LONG_FRAMES:-3000}"
# bigidle used to get a shorter count, on the reasoning that a per-row leak is
# a hundred times more visible at a 2000-session catalog and so needs fewer
# frames to see. THAT WAS WRONG, and the arm caught it: 1500 frames leaves four
# buckets past the warm-up, and Theil-Sen's ~29% breakdown point needs more
# than four points to absorb one bad bucket. Six runs at each length, block
# slope per 1000 frames:
#
#   1500 frames (4 fit points)   +48.7  +44.0  +789.3  +45.3  +50.7  +42.7
#   3000 frames (10 fit points)   +0.0   +2.0    +0.0   +2.4  +13.0   +0.0
#
# One run in six over the 500 budget by 1.6x, on a clean tree. The robustness
# of a median is a function of how many points it has, and shortening a run to
# save two seconds spent it. It costs about three seconds to put back, and
# after sidebar virtualization a 2000-row catalog is no longer the slow arm it
# was when this file was written.
BIG_FRAMES="${HANABI_SOAK_BIG_FRAMES:-$FRAMES}"
ARMS="${HANABI_SOAK_ARMS:-idle scroll read threads tabs search churn resize mixed open bigidle}"
EVERY="${HANABI_SOAK_LONG_EVERY:-250}"
JOBS="${HANABI_SOAK_JOBS:-4}"
REPORT="${HANABI_SOAK_REPORT_OUT:-}"

# Arms whose memory is REPORTED and not gated. See GROWING ARMS above.
GROWING=" open mixed "

# The long form gates harder than the short one and can: over 3000 frames the
# settling and the page quantisation that dominate a shorter window have
# amortised away. The numbers are the short gate's, halved where the extra
# frames earned it — see scripts/soak_gate.sh for the 34-run table they come
# from.
export HANABI_SOAK_MAX_RSS_KB_PER1K="${HANABI_SOAK_MAX_RSS_KB_PER1K:-256}"
export HANABI_SOAK_MAX_HEAP_KB_PER1K="${HANABI_SOAK_MAX_HEAP_KB_PER1K:-128}"
export HANABI_SOAK_MAX_BLOCKS_PER1K="${HANABI_SOAK_MAX_BLOCKS_PER1K:-500}"
export HANABI_SOAK_MAX_ENT_PER1K="${HANABI_SOAK_MAX_ENT_PER1K:-25}"
# CPU time, on the thread clock, so a parallel run does not move it.
export HANABI_SOAK_MAX_MS_PER1K="${HANABI_SOAK_MAX_MS_PER1K:-1.0}"

# Pin the mock's clock. Without it a message twelve hours old lands on a
# different calendar day depending on what time of night the run happened, the
# transcript grows or loses a date divider, and the diffable report shows a
# four-line diff about nothing. 1787000000 is 2026-08-17T00:53:20Z, chosen only
# for being fixed. See api::mock_now.
export HANABI_MOCK_NOW="${HANABI_MOCK_NOW:-1787000000}"
export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/soak-long.json"
export HANABI_SOAK_EVERY="$EVERY"

WORK="$(mktemp -d -t hanabi_soaklong)"
kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -rf "$WORK"; }
trap cleanup EXIT

if [ ! -x "$EXE" ]; then
    echo "soak: $EXE not found — run 'make' first." >&2
    exit 2
fi

require_fresh_build "$EXE" || exit 2

echo "=== hanabi long soak ==="
echo "  ${FRAMES} frames per arm (${BIG_FRAMES} for bigidle), buckets of ${EVERY}, ${JOBS} at a time"
echo "  arms: ${ARMS}"
echo "  budget per 1000 frames: RSS +${HANABI_SOAK_MAX_RSS_KB_PER1K} KB, heap +${HANABI_SOAK_MAX_HEAP_KB_PER1K} KB, blocks +${HANABI_SOAK_MAX_BLOCKS_PER1K}, entities +${HANABI_SOAK_MAX_ENT_PER1K}, cpu +${HANABI_SOAK_MAX_MS_PER1K} ms"
echo "  started $(date '+%H:%M:%S'), load $(uptime | sed -nE 's/.*load averages?: //p')"

ARM_TIMEOUT="${HANABI_SOAK_ARM_TIMEOUT:-900}"

run_arm() {
    local arm="$1"
    local scenario="$arm" sessions="" frames="$FRAMES"
    case "$arm" in
        bigidle) scenario="idle"; sessions=2000; frames="$BIG_FRAMES" ;;
    esac
    local log="$WORK/$arm.log"
    local rep="$WORK/$arm.report"
    local started
    started=$(date +%s)
    (
        export HANABI_SOAK="$frames"
        export HANABI_STRESS="$scenario"
        export HANABI_SOAK_REPORT="$rep"
        [ -n "$sessions" ] && export HANABI_STRESS_SESSIONS="$sessions"
        "$EXE" --screenshot "$WORK/$arm.png" >"$log" 2>&1
    ) &
    local pid=$!
    watchdog_start "$pid" "$ARM_TIMEOUT" kill_own_runs
    wait "$pid" 2>/dev/null
    local rc=$?
    watchdog_stop
    echo "$rc" > "$WORK/$arm.rc"
    echo "$(( $(date +%s) - started ))" > "$WORK/$arm.secs"
}

# Fan out, bounded. `wait -n` is bash 4.3+; macOS ships bash 3.2 as /bin/bash,
# so this file is #!/usr/bin/env bash and needs a modern one on PATH. If it is
# not there we fall back to serial rather than to a silently wrong fan-out.
if [ "$JOBS" -gt 1 ] && { wait -n 2>/dev/null; [ $? -ne 2 ]; }; then
    running=0
    for arm in $ARMS; do
        run_arm "$arm" &
        running=$((running + 1))
        if [ "$running" -ge "$JOBS" ]; then
            wait -n
            running=$((running - 1))
        fi
    done
    wait
else
    [ "$JOBS" -gt 1 ] && echo "  (this bash has no 'wait -n'; running serially)"
    for arm in $ARMS; do run_arm "$arm"; done
fi

SUMMARY=""
FAIL=0
for arm in $ARMS; do
    log="$WORK/$arm.log"
    rc="$(cat "$WORK/$arm.rc" 2>/dev/null || echo 99)"
    took="$(cat "$WORK/$arm.secs" 2>/dev/null || echo '?')"
    echo
    echo "--- arm: ${arm} (${took}s) ---"
    grep -E '^\[soak\]|^\[break\]' "$log" 2>/dev/null | sed 's/^/  /'

    rss="$(grep -E '^\[soak\]   RSS ' "$log" 2>/dev/null | awk '{print $3, $4}')"
    heap="$(grep -E '^\[soak\]   heap bytes' "$log" 2>/dev/null | awk '{print $4, $5}')"
    # Three outcomes, not two. An arm that never reached its verdict did not
    # measure anything, and saying FAIL about it is the same unearned verdict
    # in the other direction. On this machine the usual cause is another
    # worktree: scripts/review_shots.sh kills output/hanabi.exe in EVERY
    # worktree it can find, not just its own.
    if ! grep -qE '^\[soak\] (PASS|-+ SOAK)' "$log" 2>/dev/null; then
        state=INCOMPLETE
        FAIL=1
        echo "  the run ended (rc=${rc}) before it reached a verdict — nothing was" >&2
        echo "  measured. Something killed the process, or it crashed; this is not" >&2
        echo "  a leak. Re-run the arm on its own:" >&2
        echo "      make soak ARMS=${arm} JOBS=1" >&2
        rss="not measured"; heap="not measured"
    elif grep -q 'SCENARIO DROVE NOTHING' "$log" 2>/dev/null; then
        # A scenario that drove nothing is the flattest run anybody ever took.
        # It must not be able to read as a pass.
        state=DROVE-NOTHING
        FAIL=1
    elif [[ "$GROWING" == *" $arm "* ]]; then
        # Growth here is a cost per tab, not a leak. Reported, never gated.
        state=REPORTED
    elif [ "$rc" -eq 0 ]; then
        state=PASS
    else
        state=FAIL
        FAIL=1
    fi
    SUMMARY="${SUMMARY}$(printf '  %-9s %-14s RSS %-14s heap %-14s %4ss\n' \
        "$arm" "$state" "${rss:-?}" "${heap:-?}" "$took")"$'\n'
done

# The diffable artifact: every arm's report, concatenated, arm-prefixed.
if [ -n "$REPORT" ]; then
    : > "$REPORT"
    for arm in $ARMS; do
        if [ -s "$WORK/$arm.report" ]; then
            sed "s/^/${arm} /" "$WORK/$arm.report" >> "$REPORT"
        else
            # An absent report must show up in the diff as an absent report,
            # not as an unchanged file.
            echo "${arm} NO_REPORT the arm produced none" >> "$REPORT"
        fi
    done
    echo
    echo "  diffable report: $REPORT"
fi

echo
echo "=== soak summary (per 1000 frames) ==="
printf '%s' "$SUMMARY"
if [ "$FAIL" -eq 0 ]; then
    echo "  ALL GATED ARMS FLAT"
    exit 0
fi
if printf '%s' "$SUMMARY" | grep -q INCOMPLETE; then
    echo "  AN ARM DID NOT FINISH — that is a killed or crashed process, not a" >&2
    echo "  measurement. Re-run it on its own before believing anything here." >&2
    exit 1
fi
if printf '%s' "$SUMMARY" | grep -q DROVE-NOTHING; then
    echo "  AN ARM DROVE NOTHING — it measured an idle app under another name." >&2
    echo "  Its flatness is not evidence about the scenario it is called." >&2
    exit 1
fi
echo "  SOME ARMS GREW — see the failing arm's table above, and docs/perf/GATES.md" >&2
exit 1
