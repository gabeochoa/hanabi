#!/usr/bin/env bash
# scripts/pointer_gate.sh -- the pointer the UI hit-tests is the pointer the
# paint is under, after a NATIVE window resize.
#
# What runs: output/hanabi.exe, windowed, mock backend, isolated HOME, with
# HANABI_RESIZE_DRIVE dragging the window's corner through AppKit's own
# resize tracking loop (src/resize_drive.mm) and HANABI_POINTER_PROBE posting
# a real mouseMoved at one content point every frame and printing, from the
# same frame, the pointer in paint space (sokol's framebuffer position over
# the backing scale), the pointer afterhours hit-tests with (after its
# letterbox), and the rect of the widget it called hot (src/pointer_probe.mm).
# Three arms: a point in the sidebar, a point in the transcript, and a drag
# that changes the width and the aspect, not only the height.
#
# What is asserted, per arm, over every frame after the drive settled:
#   liveness   live_starts == 1 and live_ends == 1, and the settled window is
#              not the starting size: the drag happened.
#   agreement  mapped == window (the two pointers agree to 0.5 px) and the
#              recorded resolution equals the window, so the hit test and the
#              paint share one space.
#   target     the pointer in paint space is inside the hot widget's rect --
#              the widget under the pointer is the widget the UI called hot.
#   did run    at least 3 settled frames were read; zero reads as "did not
#              run", not as "aligned".
#
# The defect this pins (c031a6d): the resolution afterhours letterboxed the
# pointer against was written once at launch and never refreshed, so a window
# 60 px taller than the recorded size shifted every hit test 30 px up and the
# hot row was the row above the pointer, in every pane.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
EXE="${HANABI_POINTER_GATE_EXE:-$ROOT/output/hanabi.exe}"
if [ ! -x "$EXE" ]; then
    echo "pointer_gate: $EXE not found -- run 'zig build' first." >&2
    exit 2
fi

overall=0
run_arm() {
    local name=$1 point=$2 drive=$3
    local H; H="$(mktemp -d)"
    mkdir -p "$H/Library/Application Support/hanabi" "$H/cache"
    printf '{"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark"}\n' \
        > "$H/Library/Application Support/hanabi/settings.json"
    local LOG="$H/run.log"
    env HOME="$H" HANABI_CONFIG="$H/no-such-config.json" HANABI_CACHE_DIR="$H/cache" \
        HANABI_TOKEN_FILE="$H/token.json" HANABI_BACKEND=mock HANABI_MOCK_NOW=1781524800 \
        HANABI_OPEN=t2 HANABI_POINTER_PROBE="$point" HANABI_RESIZE_DRIVE="$drive" \
        "$EXE" > "$LOG" 2>&1
    local rc=$?
    echo "  --- $name  point=$point drive=$drive rc=$rc"
    local fail=0
    if [ "$rc" -ne 0 ]; then echo "  FAIL exit $rc"; fail=1; fi
    local summary; summary=$(grep -m1 '^\[resize-drive\] SUMMARY' "$LOG")
    local starts ends final
    starts=$(sed -nE 's/.*live_starts=([0-9]+).*/\1/p' <<<"$summary")
    ends=$(sed -nE 's/.*live_ends=([0-9]+).*/\1/p' <<<"$summary")
    final=$(sed -nE 's/.*final=([0-9]+x[0-9]+).*/\1/p' <<<"$summary")
    printf '  %-10s live_starts=%s live_ends=%s final=%s\n' liveness "${starts:-none}" "${ends:-none}" "${final:-none}"
    [ "${starts:-0}" = 1 ] && [ "${ends:-0}" = 1 ] || { echo "  FAIL liveness: the drag did not run through AppKit's tracking loop"; fail=1; }
    [ -n "$final" ] && [ "$final" != "1100x760" ] || { echo "  FAIL liveness: the window is still the starting size"; fail=1; }
    # Settled frames: those at the final size, after the drive.
    local settled; settled=$(grep '^\[pointer-probe\]' "$LOG" | grep " win=${final:-x} ")
    local n; n=$(printf '%s\n' "$settled" | grep -c pointer-probe)
    printf '  %-10s settled_frames=%s\n' did-run "$n"
    [ "$n" -ge 3 ] || { echo "  FAIL did-run: fewer than 3 settled frames read"; fail=1; }
    local bad_agree bad_target bad_res
    bad_agree=$(printf '%s\n' "$settled" | awk '{
        for (i=1;i<=NF;i++) { if ($i ~ /^window=/) w=$i; if ($i ~ /^mapped=/) m=$i }
        split(substr(w,8),a,","); split(substr(m,8),b,",");
        dx=a[1]-b[1]; dy=a[2]-b[2]; if (dx<0) dx=-dx; if (dy<0) dy=-dy;
        if (dx>0.5 || dy>0.5) print }' | wc -l | tr -d ' ')
    bad_res=$(printf '%s\n' "$settled" | awk '{
        for (i=1;i<=NF;i++) { if ($i ~ /^win=/) w=substr($i,5); if ($i ~ /^R=/) r=substr($i,3) }
        if (w != r) print }' | wc -l | tr -d ' ')
    bad_target=$(printf '%s\n' "$settled" | grep -c 'window_in_hot=0')
    local last; last=$(printf '%s\n' "$settled" | tail -1 | sed -E 's/.*(window=[^ ]+).*(mapped=[^ ]+).*(hot=[^ ]+ rect=[^ ]+).*/\1 \2 \3/')
    printf '  %-10s %s\n' last "$last"
    [ "$bad_agree" = 0 ] || { echo "  FAIL agreement: $bad_agree settled frames where the hit-test pointer != the paint pointer"; fail=1; }
    [ "$bad_res" = 0 ] || { echo "  FAIL agreement: $bad_res settled frames where the recorded resolution != the window"; fail=1; }
    [ "$bad_target" = 0 ] || { echo "  FAIL target: $bad_target settled frames where the hot widget is not under the pointer"; fail=1; }
    if [ "$fail" -ne 0 ]; then
        overall=1
        grep '^\[pointer-probe\]' "$LOG" | tail -3 | sed 's/^/        /'
    fi
    rm -rf "$H"
}

# The sidebar row list, taller window (the reported shape).
run_arm sidebar-taller  140,400 "hold:40,grow:0x60:20,hold:60"
# The tab strip over the transcript (x past the sidebar), same drag: one
# shared transform, not a sidebar rule. A tab, because it is the one hoverable
# widget on the chat side whose rect does not move with the window height.
run_arm chat-taller     400,40  "hold:40,grow:0x60:20,hold:60"
# Width and aspect change: a scale, not only a bar, if the resolution lags.
run_arm chat-wider      400,40  "hold:40,grow:200x-40:20,hold:60"

if [ "$overall" -ne 0 ]; then echo "  pointer gate: FAIL"; exit 1; fi
echo "  pointer gate: PASS"
