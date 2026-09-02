#!/usr/bin/env bash
# scripts/digest_gate.sh — do the four digest screens build a viewport, or a
# catalog? Runs inside `make test`.
#
# WHY A SEPARATE GATE. Three gates already measure this app's scaling and none
# of them can see these four screens:
#
#   scaling_gate.sh  opens ONE screen (Home, the landing view) at two catalog
#                    sizes and never navigates. Home was capped (not windowed)
#                    since perf/sidebar-scaling, so the gate has been green
#                    over a Blocked screen building 506 cards the whole time.
#                    Home is windowed now and is the fifth arm below: its cap
#                    still bounds MATCHED at four sections of twenty, so its
#                    matched floor is 60 where the digest views' is 100.
#   scroll_gate.sh   expands the SIDEBAR's list and sweeps it. Different pane,
#                    different question.
#   soak_gate.sh     measures a slope. This was never a slope: a digest screen
#                    is as expensive on its first frame as its ten-thousandth
#                    (docs/perf/SCROLL.md section 4, the same lesson again).
#
# WHAT IT MEASURES, and both arms are COUNTS rather than milliseconds, because
# this box is shared with several other agents and its load average during
# these runs ran 8 to 124:
#
#   BUILT vs MATCHED — how many cards one frame of each digest view builds,
#     against how many sessions matched its predicate. This is the property
#     itself: the list must stay whole (matched grows with the catalog) while
#     the build stays bounded (built does not). It is exact — the same run
#     gives the same integer every time, no spread at all.
#
#   WIDGETS at 200 vs 2000 — the scaling gate's own question, asked of the four
#     screens it never opens. A backstop for a regression that adds per-catalog
#     widgets somewhere other than the cards.
#
#     The small size is 200 and not the scaling gate's 20, and the reason is
#     the gate's own sharpness. At 20 sessions Review matches five, so the
#     screen does not fill a viewport and the ratio measures how EMPTY the
#     small arm is rather than how bounded the big one is: 337 -> 501 reads
#     1.49x on a screen whose card count never moved, one widget from a red
#     board over nothing at all. At 200 every one of the four fills its
#     viewport on both arms and all four read 0.99x, to the widget, run after
#     run -- so the ceiling can be 1.20x instead of 1.50x and mean something.
#
# ---------------------------------------------------------------------------
# WHY THE PINNED / ARCHIVED PERCENTAGES ARE HERE
#
# The synthetic catalog put NOTHING in Starred or Archived at any size, so two
# of the five screens could not be measured at all: `HANABI_VIEW=starred
# HANABI_STRESS_SESSIONS=2000` rendered an empty state and read as a pass.
# HANABI_STRESS_PINNED / HANABI_STRESS_ARCHIVED fill them, deterministically,
# and are off by default so no existing script's row counts move.
#
# That is worth stating plainly: before this gate, Starred and Archived passed
# `make scaling-gate` for the same reason a screen nobody renders passes it.
#
# ---------------------------------------------------------------------------
# WHERE THE THRESHOLDS COME FROM
#
# Measured 2026-08-25 on gabeochoa-mac-GRQ7Y259H4, HANABI_STRESS_PINNED=10
# HANABI_STRESS_ARCHIVED=10, at a 1180x949 window:
#
#   screen      matched@2000   built   widgets 200 -> 2000   ratio
#   Blocked              506      13           504 ->  501   0.99x
#   Review               303      13           504 ->  501   0.99x
#   Starred              200      13           511 ->  508   0.99x
#   Archived             200      13           511 ->  508   0.99x
#
# Home, measured 2026-09-02 on boulder-KF74T3NW36, same flags:
#
#   Home                  63      11           214 ->  214   1.00x
#
# Home's MATCHED is its four capped sections, not the catalog, so 63 is what a
# 2000-session catalog puts in the column and 11 is what the viewport holds.
# Before the window it built all 63, which is the rehearsal below.
#
# built is 13 on every one of them, at every catalog size above a viewport,
# because 13 is what a viewport holds. The ceiling is 40 rather than 13: the
# window widens to cover a scroll that is about to happen (up to
# kFlingOverscanPx), and a gate that fails when somebody changes the overscan
# constant is a gate about a constant rather than about the property.
#
# 0.99x, with zero spread across runs -- the big arm is a WIDGET or three
# cheaper than the small one, because the sidebar's count badges gain a digit
# and lose nothing. Every number on this board is an integer count and none of
# them is a millisecond, which is why it can be this tight on a box whose load
# average ran 8 to 124 while it was being written.
#
# WHAT IT CATCHES, rehearsed by breaking it on purpose:
#
#   render_digest's window removed (main's behaviour before perf/digest)
#       built 13 -> 506 on Blocked, and 2472 widgets, 6.40x.
#   render_home's window removed (main's behaviour at 14312fe)
#       built 11 -> 63 on Home, over the 40 ceiling.
#   the window kept but the frame-one fallback left as "build everything"
#       built stays 13 and WIDGETS stay 2473 -- nothing retires a widget
#       (gap #115), so one uncapped frame is a permanent plateau. This is why
#       the gate has a widget arm at all and not only a card arm.
# ---------------------------------------------------------------------------
set -uo pipefail

BUILT_CEILING="${HANABI_DIGEST_BUILT_CEILING:-40}"
WIDGET_RATIO_CEILING="${HANABI_DIGEST_WIDGET_CEILING:-1.20}"

SMALL="${HANABI_DIGEST_SMALL:-200}"
BIG="${HANABI_DIGEST_BIG:-2000}"
FRAMES="${HANABI_DIGEST_FRAMES:-60}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="$ROOT/output/hanabi.exe"
SHOT="$(mktemp -t hanabi_digest_XXXX).png"
LOG="$(mktemp -t hanabi_digest_XXXX).log"
HOME_DIR="$(mktemp -d)"
RUN_TIMEOUT=120

kill_own_runs() { pkill -9 -f "^$EXE" >/dev/null 2>&1 || true; }
cleanup() { kill_own_runs; rm -f "$SHOT" "$LOG"; rm -rf "$HOME_DIR"; }
trap cleanup EXIT

# The gate runs against ITS OWN settings, never the invoking user's.
#
# Home's four sections are foldable, and a folded shelf renders no cards at
# all -- so a developer who had collapsed them, or any HOME carrying a
# hanabi settings.json, made the Home arm read `built=0 matched=0` and the
# gate exit 2 without ever measuring the window. The four digest views do not
# fold, which is why this went unnoticed until Home became the fifth arm.
# `collapsed_shelves` is therefore stated EMPTY rather than merely omitted:
# the value the gate depends on is the one it should say out loud.
mkdir -p "$HOME_DIR/Library/Application Support/hanabi"
printf '%s\n' '{"window_width":1180,"window_height":949,"open_tabs":[],"active_tab":"","theme":"dark","collapsed_shelves":[]}' \
    > "$HOME_DIR/Library/Application Support/hanabi/settings.json"
export HOME="$HOME_DIR"

if [ ! -x "$EXE" ]; then
    echo "digest_gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

VIEWS="${HANABI_DIGEST_ONLY:-blocked review starred archived home}"
for v in $VIEWS; do
    case "$v" in
        blocked|review|starred|archived|home) ;;
        *)
            echo "digest_gate: unknown HANABI_DIGEST_ONLY '$v'" >&2
            exit 2
            ;;
    esac
done

# `scripts/digest_gate.sh --selftest` proves the isolation above, by running
# the gate under the exact HOME that used to break it: every Home shelf
# collapsed. A folded shelf builds no cards, so before the fix this read
# `built=0 matched=0` and exited 2 -- a gate reporting a failure it had not
# measured. Restricted to the Home arm, which is the only one that folds, so
# it costs two runs rather than ten. Run by `make digest-gate` ahead of the
# gate itself, which is where its subject is and where the binary it needs
# already exists -- `make source-checks` takes no binary and must not grow
# one.
if [ "${1:-}" = "--selftest" ]; then
    dirty="$(mktemp -d)"
    mkdir -p "$dirty/Library/Application Support/hanabi"
    printf '%s\n' '{"window_width":640,"window_height":480,"theme":"light","collapsed_shelves":["waiting","finished","self_running","recent"]}' \
        > "$dirty/Library/Application Support/hanabi/settings.json"
    echo "digest_gate --selftest: the gate ignores the caller's settings"
    out="$(env HOME="$dirty" HANABI_DIGEST_ONLY=home bash "$0" 2>&1)"
    rc=$?
    rm -rf "$dirty"
    row="$(printf '%s' "$out" | grep -E '^  home ' || true)"
    built="$(printf '%s' "$row" | awk '{print $2}')"
    matched="$(printf '%s' "$row" | awk '{print $3}')"
    if [ "$rc" -ne 0 ] || [ -z "${built:-}" ] || [ "${built:-0}" -le 0 ]; then
        echo "  FAIL  every shelf collapsed in HOME -> home built='${built:-}'" >&2
        echo "        matched='${matched:-}' rc=$rc" >&2
        echo "        The gate is reading the caller's settings.json, so a" >&2
        echo "        folded shelf makes it report a failure it never" >&2
        echo "        measured. It must seed its own HOME." >&2
        printf '%s\n' "$out" | sed 's/^/        /' >&2
        exit 1
    fi
    echo "  ok    home built=$built matched=$matched rc=$rc"
    echo "digest_gate --selftest: PASS"
    exit 0
fi

export HANABI_BACKEND=mock
export HANABI_CONFIG="/nonexistent/hanabi/digest-gate.json"
export HANABI_FRAME_TIMING="$FRAMES"
export HANABI_STRESS_PINNED=10
export HANABI_STRESS_ARCHIVED=10

# echoes "<widgets> <built> <matched>"
#
# Every field is read from the ONE line that owns it, never from the log at
# large. `built=` appears on the FrameTiming line too -- widget_epoch counts
# what a frame built, DigestCards counts what the card list built -- and a
# `head -1` over the whole log silently read the wrong one the day the two
# landed together, reporting 214 cards on a screen that built 13.
measure() {  # $1 = view, $2 = session count
    local view="$1" n="$2" w b m cards
    ( HANABI_VIEW="$view" HANABI_STRESS_SESSIONS="$n" \
          timeout "$RUN_TIMEOUT" "$EXE" --screenshot "$SHOT" >"$LOG" 2>&1 ) || true
    cards="$(grep -E 'DigestCards:' "$LOG" | head -1)"
    w="$(grep -Eo 'widgets=[0-9]+' "$LOG" | head -1 | cut -d= -f2)"
    b="$(printf '%s' "$cards" | grep -Eo 'built=[0-9]+' | cut -d= -f2)"
    m="$(printf '%s' "$cards" | grep -Eo 'matched=[0-9]+' | cut -d= -f2)"
    printf '%s %s %s' "${w:-0}" "${b:-0}" "${m:-0}"
}

# A gate that cannot find its subject must FAIL, and saying so inside measure()
# does not do that: measure() is only ever called in $(...), so an `exit` there
# kills the subshell and the parent reads on. This script sets -uo pipefail but
# not -e, so the empty fields that follow make every integer test error to
# false and the run prints PASS -- which is what it did, with four blank rows,
# until this was caught by the commit audit. The subshell reports absence in
# the FIELDS, and the parent is the only place that may exit.
measured_or_die() {  # $1 = session count, $2 = view, $3 = built, $4 = matched
    if [ -z "${3:-}" ] || [ -z "${4:-}" ] || \
       { [ "${3:-0}" = "0" ] && [ "${4:-0}" = "0" ]; }; then
        echo "" >&2
        echo "  FAIL: no DigestCards line for '$2' at $1 sessions." >&2
        echo "        The app did not report card counts, so this gate has" >&2
        echo "        nothing to assert. That is a crash, a killed run, or a" >&2
        echo "        binary built without the counters -- never a pass." >&2
        tail -20 "$LOG" | sed 's/^/        /' >&2
        exit 2
    fi
}

echo "=== hanabi digest-screen gate ==="
printf '  %-10s %8s %8s %9s %9s %8s\n' \
    "screen" "built" "matched" "w@$SMALL" "w@$BIG" "ratio"

FAIL=0
for view in $VIEWS; do
    MATCH_FLOOR=100
    if [ "$view" = "home" ]; then MATCH_FLOOR=60; fi
    read -r W_SMALL S_BUILT S_MATCHED <<<"$(measure "$view" "$SMALL")"
    measured_or_die "$SMALL" "$view" "$S_BUILT" "$S_MATCHED"
    read -r W_BIG BUILT MATCHED <<<"$(measure "$view" "$BIG")"
    measured_or_die "$BIG" "$view" "$BUILT" "$MATCHED"

    if [ "$W_SMALL" = "0" ] || [ "$W_BIG" = "0" ]; then
        echo "" >&2
        echo "  FAIL: could not read a FrameTiming line for '$view'." >&2
        echo "        That is a crash, a killed run, or a build without" >&2
        echo "        HANABI_FRAME_TIMING — not a scaling regression. On this" >&2
        echo "        machine the usual cause of a killed run is another" >&2
        echo "        worktree: scripts/review_shots.sh kills output/hanabi.exe" >&2
        echo "        in EVERY worktree it finds, not just its own." >&2
        tail -20 "$LOG" | sed 's/^/        /' >&2
        exit 1
    fi

    WR="$(awk "BEGIN{printf \"%.2f\", $W_BIG/$W_SMALL}")"
    printf '  %-10s %8s %8s %9s %9s %8s\n' \
        "$view" "$BUILT" "$MATCHED" "$W_SMALL" "$W_BIG" "${WR}x"

    # An empty screen proves nothing, and reads exactly like a bounded one.
    # This is the check that would have caught Starred and Archived passing
    # the scaling gate while holding nothing at all.
    if [ "$MATCHED" -lt "$MATCH_FLOOR" ]; then
        echo "" >&2
        echo "  FAIL: '$view' matched only $MATCHED sessions at $BIG," >&2
        echo "        under its floor of $MATCH_FLOOR." >&2
        echo "        A digest view with nothing in it is not bounded, it is" >&2
        echo "        empty, and an empty screen passes every arm below for" >&2
        echo "        the wrong reason. Check HANABI_STRESS_PINNED /" >&2
        echo "        HANABI_STRESS_ARCHIVED still fill the synthetic catalog" >&2
        echo "        (src/api/mock_client.h), and that the predicate in" >&2
        echo "        src/ecs/thread_model.h still matches what they set." >&2
        FAIL=1
        continue
    fi
    if [ "$BUILT" -gt "$BUILT_CEILING" ]; then
        echo "" >&2
        echo "  FAIL: '$view' built $BUILT cards of $MATCHED, over $BUILT_CEILING." >&2
        echo "        The frame is building the list rather than the viewport." >&2
        echo "        Look at render_digest's card_window call and at" >&2
        echo "        digest_layout.h. docs/perf/DIGEST.md." >&2
        FAIL=1
    fi
    if awk "BEGIN{exit !($WR > $WIDGET_RATIO_CEILING)}"; then
        echo "" >&2
        echo "  FAIL: '$view' widget ratio ${WR}x exceeds ${WIDGET_RATIO_CEILING}x," >&2
        echo "        with only $BUILT cards built. Something OTHER than the" >&2
        echo "        cards is growing with the catalog — or a single uncapped" >&2
        echo "        frame minted widgets that nothing retires (gap #115), in" >&2
        echo "        which case the card count is right and the plateau is" >&2
        echo "        permanent. docs/perf/DIGEST.md." >&2
        FAIL=1
    fi
done

echo "  budget: <= ${BUILT_CEILING} cards built whatever matched, <= ${WIDGET_RATIO_CEILING}x widgets over a $((BIG / SMALL))x catalog"
if [ "$FAIL" -eq 0 ]; then
    echo "  PASS"
    exit 0
fi
echo "  FAILED digest-screen gate" >&2
exit 1
