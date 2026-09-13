#!/usr/bin/env bash
# scripts/resize_drive_gate.sh — a LIVE window resize, driven through
# AppKit's own resize tracking loop, has to keep painting every size and end
# exactly where the pointer left it.
#
# What runs: output/hanabi.exe, windowed, mock backend, isolated HOME, with
# HANABI_RESIZE_DRIVE posting real NSEvents at the window's resize corner
# (src/resize_drive.mm has the mechanism and its one limitation: the events
# enter through NSApp's queue, not the window server, so presentation timing
# against a real cursor is not what is measured here). Two scenes: the
# typical thread (t2) and the 3,672-message fixture; each grows, shrinks past
# the start, grows back, and settles -- net zero, so the settled size is the
# starting size to the pixel.
#
# What is asserted, all counts (this box is shared; a millisecond budget on
# it is a coin flip -- docs/perf/GATES.md):
#   liveness      live_starts == 1 and live_ends == 1: AppKit's tracking loop
#                 actually ran (NSWindowWillStart/DidEndLiveResize), so the
#                 drag reached the path a person's drag takes.
#   progression   frames_live >= 90% of the sizes AppKit applied, and every
#                 applied size was painted: sizes_skipped == 0 (a skipped size
#                 is a frame change the content lagged behind by a step).
#   settle        the last frame drawn is the window's settled size (the content
#                 is the window, exactly), at least one frame was drawn after
#                 the live resize ended, and the window is within 4 px of the
#                 pointer's final position. Not exactly AT it: AppKit's
#                 tracking loop sizes the frame from the last drag it
#                 processed and a mouse-up moves nothing, and under posted
#                 events it lands 1-2 px short now and then (the window
#                 server delivers a real pointer's last event). That slack is
#                 AppKit's, and it is printed; the content-equals-window
#                 check is the app's and it is exact.
#   exactness     HANABI_NATURAL_AUDIT=1 re-measures every render-cache hit
#                 served at a width the pair does not hold -- by the
#                 natural-width rule or by the entry's fit interval:
#                 natural_mismatch must be absent and, on the big fixture,
#                 natural_audited must be > 0 so the rules were exercised,
#                 not merely not wrong.
#   reuse         on the big fixture, whose assistant turns all WRAP, the fit
#                 interval must be what serves them: cache.interval_hit >= 100
#                 and cache.msgrender_miss < cache.interval_hit over the run.
#                 A build without the interval reads 0 hits against ~380
#                 misses a frame (measured, b6d82ef); with it, ~330 hits
#                 against ~60 misses. Counts, not milliseconds, so it holds
#                 on a loaded box.
#   fail closed   a non-zero exit, a missing SUMMARY, or a missing prof table
#                 is a FAIL, not a skip.
# It also PRINTS the timing percentiles (app frame CPU/wall, inter-frame gap,
# resize-step gap) for the log, ungated.
#
# Planted control: HANABI_RESIZE_SYNC_DRAW=0 turns off the same-step draw and
# sizes_skipped goes to 7-24 per 170 steps on this machine (three runs each
# arm); `--selftest` runs that arm and requires the gate to FAIL on it.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_RESIZE_DRIVE_EXE:-$ROOT/output/hanabi.exe}"
PATTERN="${HANABI_RESIZE_DRIVE_PATTERN:-grow:300x150:150,grow:-500x-250:150,grow:200x100:80}"
if [ ! -x "$EXE" ]; then
    echo "resize_drive_gate: $EXE not found — run 'zig build' first." >&2
    exit 2
fi

selftest=0
[ "${1:-}" = "--selftest" ] && selftest=1

run_scene() {  # name tab extra-env... ; sets LOG
    local name=$1 tab=$2; shift 2
    local H; H="$(mktemp -d)"
    mkdir -p "$H/Library/Application Support/hanabi"
    printf '{"window_width":1100,"window_height":760,"open_tabs":["%s"],"active_tab":"%s","theme":"dark"}\n' \
        "$tab" "$tab" > "$H/Library/Application Support/hanabi/settings.json"
    LOG="$H/run.log"
    env HOME="$H" HANABI_BACKEND=mock HANABI_CONFIG=/nonexistent/resize-drive.json \
        HANABI_MOCK_NOW=1787000000 HANABI_PROF=1 HANABI_NATURAL_AUDIT=1 \
        HANABI_RESIZE_DRIVE="$PATTERN" "$@" \
        timeout 240 "$EXE" > "$LOG" 2>&1
    RC=$?
    SCENE_HOME="$H"
}

field() { grep -oE "$2=[-0-9.x]+" <<< "$1" | head -1 | cut -d= -f2; }

check_scene() {  # name expect_audited(0|1) -> prints, returns 0/1
    local name=$1 want_audited=$2 fail=0
    local summary; summary=$(grep -m1 '\[resize-drive\] SUMMARY' "$LOG" || true)
    local cadence; cadence=$(grep -m1 '\[resize-drive\] CADENCE' "$LOG" || true)
    echo "  --- $name"
    if [ "$RC" -ne 0 ]; then echo "  FAIL exit $RC"; tail -5 "$LOG" | sed 's/^/        /'; return 1; fi
    if [ -z "$summary" ]; then echo "  FAIL no SUMMARY line: the drive measured nothing"; return 1; fi
    if ! grep -q '^\[prof\]' "$LOG"; then echo "  FAIL no prof table: the audit could not be read"; return 1; fi
    local starts ends frames notes skipped final settle
    starts=$(field "$summary" live_starts); ends=$(field "$summary" live_ends)
    frames=$(field "$summary" frames_live); notes=$(field "$summary" notes)
    skipped=$(field "$summary" sizes_skipped); final=$(field "$summary" final)
    settle=$(field "$summary" settle_frames); lastframe=$(field "$summary" last_frame)
    local audited mismatch interval misses
    audited=$(grep -oE 'cache\.natural_audited +[0-9]+' "$LOG" | awk '{print $2}'); audited=${audited:-0}
    mismatch=$(grep -oE 'cache\.natural_mismatch +[0-9]+' "$LOG" | awk '{print $2}'); mismatch=${mismatch:-0}
    interval=$(grep -oE 'cache\.interval_hit +[0-9]+' "$LOG" | awk '{print $2}'); interval=${interval:-0}
    misses=$(grep -oE 'cache\.msgrender_miss +[0-9]+' "$LOG" | awk '{print $2}'); misses=${misses:-0}
    printf '  %-14s live_starts=%s live_ends=%s\n' liveness "$starts" "$ends"
    printf '  %-14s frames=%s applied_sizes=%s sizes_skipped=%s\n' progression "$frames" "$notes" "$skipped"
    printf '  %-14s window=%s last_frame=%s settle_frames=%s (pointer left it at 1100x760)\n' settle "$final" "$lastframe" "$settle"
    printf '  %-14s natural_audited=%s natural_mismatch=%s\n' exactness "$audited" "$mismatch"
    printf '  %-14s interval_hits=%s render_misses=%s\n' reuse "$interval" "$misses"
    printf '  %-14s %s\n' timing "$(grep -oE 'cpu_p50=.* gap_max=[^ ]*' <<< "$summary")"
    printf '  %-14s %s\n' cadence "$(sed 's/\[resize-drive\] CADENCE //' <<< "$cadence")"
    [ "$starts" = 1 ] && [ "$ends" = 1 ] || { echo "  FAIL liveness: AppKit's live resize did not start and end once"; fail=1; }
    [ -n "$notes" ] && [ "$notes" -ge 20 ] || { echo "  FAIL progression: fewer than 20 sizes applied (${notes:-none}) -- the drag did not happen"; fail=1; }
    if [ -n "$frames" ] && [ -n "$notes" ]; then
        awk -v f="$frames" -v n="$notes" 'BEGIN { exit !(f * 10 < n * 9) }' && { echo "  FAIL progression: frames ($frames) under 90% of applied sizes ($notes)"; fail=1; }
    fi
    [ "$skipped" = 0 ] || { echo "  FAIL progression: $skipped applied sizes were never painted"; fail=1; }
    [ "$final" = "$lastframe" ] || { echo "  FAIL settle: the last frame ($lastframe) is not the settled window ($final)"; fail=1; }
    local fw fh; fw=${final%x*}; fh=${final#*x}
    if [ -z "$fw" ] || [ -z "$fh" ] || [ $((fw - 1100)) -gt 4 ] || [ $((1100 - fw)) -gt 4 ] || [ $((fh - 760)) -gt 4 ] || [ $((760 - fh)) -gt 4 ]; then
        echo "  FAIL settle: window ended at $final, more than 4 px from 1100x760"; fail=1
    fi
    [ -n "$settle" ] && [ "$settle" -ge 1 ] || { echo "  FAIL settle: no frame after the live resize ended"; fail=1; }
    [ "$mismatch" = 0 ] || { echo "  FAIL exactness: $mismatch natural-width hits disagreed with a direct measure"; fail=1; }
    if [ "$want_audited" = 1 ] && [ "$audited" -lt 100 ]; then echo "  FAIL exactness: the off-width rules were barely exercised ($audited hits)"; fail=1; fi
    if [ "$want_audited" = 1 ]; then
        [ "$interval" -ge 100 ] || { echo "  FAIL reuse: the fit interval served $interval hits on a fixture whose every assistant turn wraps"; fail=1; }
        [ "$misses" -lt "$interval" ] || { echo "  FAIL reuse: render-cache misses ($misses) are not under interval hits ($interval) -- wrapped messages are re-measuring per step"; fail=1; }
    fi
    return $fail
}

echo "=== hanabi resize-drive gate ==="
echo "  $EXE"
echo "  pattern $PATTERN (net zero: settles at 1100x760)"

if [ "$selftest" = 1 ]; then
    echo "  selftest: the same-step draw OFF must fail progression"
    run_scene typical t2 HANABI_RESIZE_SYNC_DRAW=0
    if check_scene "typical, sync draw off (planted)" 0; then
        echo "  selftest FAIL: the planted regression passed the gate"; rm -rf "$SCENE_HOME"; exit 1
    fi
    rm -rf "$SCENE_HOME"
    echo "  selftest PASS: the planted regression was caught"
    exit 0
fi

overall=0
run_scene typical t2
check_scene "typical thread (t2)" 0 || overall=1
rm -rf "$SCENE_HOME"
run_scene big rbig HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_TURNS=918
check_scene "3,672-message fixture" 1 || overall=1
rm -rf "$SCENE_HOME"
if [ "$overall" -ne 0 ]; then echo "  resize-drive gate: FAIL"; exit 1; fi
echo "  resize-drive gate: PASS"
