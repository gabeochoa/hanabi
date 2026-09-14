#!/usr/bin/env bash
# ===========================================================================
# capture_receipt_hold.sh -- the sidecar: capture the window DURING the frame a
# receipt describes, matching the receipt's pid and picking the occurrence.
#
#   usage: capture_receipt_hold.sh <uitest.log> <name> <occurrence> <out_capture.png> [timeout_s]
#
# Start it BEFORE the run, in the background. It tails the log for the
# <occurrence>th `[capture-receipt] name=<name>` line (1 = first, 2 = the one
# after a resize, ...), then -- while the script's `wait` keeps that frame on
# screen -- checks the receipt's window number is owned by the receipt's pid
# per the window server (CGWindowListCopyWindowInfo via Swift-free `osascript`
# is not used; `lsappinfo`/`ps` are: the pid must be a live process whose
# executable is hanabi's), and captures that window by number, no shadow,
# once. Writes <out>.pid beside the PNG so the check can match receipt and
# capture. Never captures a screen or a window the receipt did not name.
# ===========================================================================
set -u
LOG="${1:?usage: capture_receipt_hold.sh <uitest.log> <name> <occurrence> <out_capture.png> [timeout_s]}"
NAME="${2:?name}"; OCC="${3:?occurrence}"; OUT="${4:?out capture}"; TIMEOUT="${5:-120}"
deadline=$(( $(date +%s) + TIMEOUT ))
while [ "$(date +%s)" -lt "$deadline" ]; do
    if [ -f "$LOG" ]; then
        line="$(grep -F "[capture-receipt] name=$NAME " "$LOG" 2>/dev/null | sed -n "${OCC}p")"
        if [ -n "$line" ]; then
            win="$(echo "$line" | sed -nE 's/.*[[:space:]]window=([^ ]+).*/\1/p')"
            pid="$(echo "$line" | sed -nE 's/.* pid=([^ ]+).*/\1/p')"
            owned="$(echo "$line" | sed -nE 's/.* owned=([^ ]+).*/\1/p')"
            if [ "$owned" != "1" ]; then echo "capture_receipt_hold: receipt not owned; refusing" >&2; exit 3; fi
            exe="$(ps -o comm= -p "$pid" 2>/dev/null)"
            case "$exe" in *hanabi*) ;; *) echo "capture_receipt_hold: pid $pid is '$exe', not a hanabi process; refusing" >&2; exit 3;; esac
            if screencapture -x -o -l"$win" "$OUT"; then
                echo "$pid" > "$OUT.pid"
                echo "captured window $win of pid $pid (receipt #$OCC for $NAME) -> $OUT"; exit 0
            fi
            echo "capture_receipt_hold: screencapture failed for window $win" >&2; exit 2
        fi
    fi
    sleep 0.05
done
echo "capture_receipt_hold: no receipt #$OCC for '$NAME' within ${TIMEOUT}s" >&2
exit 4
