#!/usr/bin/env bash
# The two notification switches obey their readers, proven headlessly.
#
# The decision this asserts is the one the windowed frame makes: same
# notify::cue_for, same Settings, same native boundary. HANABI_CHIME_LOG makes
# that boundary append a line instead of playing, so the gate can count calls
# rather than listen for them.
#
# It asserts the DECISION and the CALL, because those are two different bugs: a
# reader that decides correctly and never reaches the boundary is as silent as
# one that decides wrong.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2
# shellcheck source=scripts/watchdog.sh
. "$ROOT/scripts/watchdog.sh"

EXE="$ROOT/output/hanabi.exe"
[ -x "$EXE" ] || { echo "chime-gate: $EXE not built" >&2; exit 2; }

STORE="$ROOT/hanabi/settings.json"
BACKUP=""
if [ -f "$STORE" ]; then
    BACKUP="$(mktemp)"
    cp "$STORE" "$BACKUP"
fi
LOG="$(mktemp)"
OUT="$(mktemp)"
restore() {
    if [ -n "$BACKUP" ]; then cp "$BACKUP" "$STORE"; rm -f "$BACKUP";
    else rm -f "$STORE"; fi
    rm -f "$LOG" "$OUT"
}
trap restore EXIT

mkdir -p "$ROOT/hanabi"
rc=0
probe_rc=0
probe() {
    : > "$LOG"
    : > "$OUT"
    HANABI_CONFIG=/tmp/none_$$ HANABI_BACKEND=mock HANABI_CHIME_LOG="$LOG" \
        watchdog_run 25 "$EXE" "$@" >"$OUT" 2>/dev/null
    probe_rc=$?
}
probe_note() {
    [ "$probe_rc" = 0 ] || printf ' (probe exit %s)' "$probe_rc"
}
check() {
    local desc="$1" json="$2" spec="$3" want="$4" wantcalls="$5"
    printf '%s' "$json" > "$STORE"
    probe --chime-probe "$spec"
    local out calls
    out="$(awk '{print $1}' "$OUT")"
    calls="$(wc -l < "$LOG" | tr -d ' ')"
    if [ "$out" = "$want" ] && [ "$calls" = "$wantcalls" ]; then
        printf '  ok    %-34s %s (%s native call(s))\n' "$desc" "$out" "$calls"
    else
        printf '  FAIL  %-34s got %s/%s calls, want %s/%s%s\n' \
               "$desc" "$out" "$calls" "$want" "$wantcalls" "$(probe_note)" >&2
        rc=1
    fi
}

echo "=== run-finished cue ==="
check "both on, run finished"   '{"notifications_enabled":true,"run_chime":true}'   finished chime  1
check "chime off"               '{"notifications_enabled":true,"run_chime":false}'  finished silent 0
check "notifications off"       '{"notifications_enabled":false,"run_chime":true}'  finished silent 0
check "both off"                '{"notifications_enabled":false,"run_chime":false}' finished silent 0
check "both on, thread blocked" '{"notifications_enabled":true,"run_chime":true}'   blocked  silent 0

# The sub-agent filter, through the same product path: a child's transition is
# silent until the reader asks for it, and a top-level thread is never filtered.
probe_notify() {
    local desc="$1" json="$2" spec="$3" want="$4"
    printf '%s' "$json" > "$STORE"
    probe --notify-probe "$spec"
    local out
    out="$(cat "$OUT")"
    if [ "$out" = "$want" ]; then
        printf '  ok    %-34s %s\n' "$desc" "$out"
    else
        printf '  FAIL  %-34s got %s, want %s%s\n' \
               "$desc" "$out" "$want" "$(probe_note)" >&2
        rc=1
    fi
}

echo "=== sub-agent notifications ==="
probe_notify "sub-agents off, child blocks"  '{"notify_subagents":false}' child none
probe_notify "sub-agents on, child blocks"   '{"notify_subagents":true}'  child child
probe_notify "sub-agents off, parent blocks" '{"notify_subagents":false}' top   parent
probe_notify "sub-agents on, parent blocks"  '{"notify_subagents":true}'  top   parent

if [ "$rc" = 0 ]; then echo "notification-gate: PASS"; else echo "notification-gate: FAIL" >&2; fi
exit $rc
