#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2
source "$SCRIPT_DIR/watchdog.sh"

EXE="${HANABI_HOME_SCAN_EXE:-$ROOT/output/hanabi.exe}"
FRAMES="${HANABI_HOME_SCAN_FRAMES:-600}"
RUN_TIMEOUT="${HANABI_HOME_SCAN_TIMEOUT:-180}"
SELFTEST=0
if [ "${1:-}" = "--selftest" ]; then SELFTEST=1; fi

counter() { grep -E "^\[prof\] $2 " "$1" | awk '{print $3}' | tail -1; }
gauge() { grep -E "^\[prof\] $2 " "$1" | awk '{print $NF}' | tail -1; }

read_arm() {
    local log="$1" arm="$2" mode="${3:-active}" missing=""
    HOME_FRAMES="$(counter "$log" 'home\.frames')"
    REBUILDS="$(counter "$log" 'home\.scan_rebuild')"
    REUSE="$(counter "$log" 'home\.scan_reuse')"
    VISITS="$(counter "$log" 'home\.scan_visits')"
    CATALOG="$(gauge "$log" 'home\.catalog')"
    FRAMES_DONE="$(grep -E '^\[prof\] [0-9]+ frames' "$log" | awk '{print $2}' | tail -1)"
    REFRESHES="$(sed -n 's/.*catalog_refreshes=\([0-9][0-9]*\).*/\1/p' "$log" | tail -1)"
    if [ "$mode" = inactive ]; then
        if ! [[ "$HOME_FRAMES" =~ ^[0-9]+$ ]] || [ "$HOME_FRAMES" -lt "$FRAMES" ]; then
            echo "home-scan-gate: INCOMPLETE — arm $arm left Home inactive (${HOME_FRAMES:-0}/$FRAMES frames)" >&2
            return 2
        fi
        echo "home-scan-gate: FAIL — inactive arm rendered Home for $HOME_FRAMES/$FRAMES frames" >&2
        return 1
    fi
    for pair in "home_frames:$HOME_FRAMES" "rebuilds:$REBUILDS" \
                "reuse:$REUSE" "visits:$VISITS" "catalog:$CATALOG" \
                "frames:$FRAMES_DONE" "catalog_refreshes:$REFRESHES"; do
        local name="${pair%%:*}" value="${pair#*:}"
        if ! [[ "$value" =~ ^[0-9]+$ ]]; then
            missing="${missing}${missing:+, }$name"
        fi
    done
    if [ -n "$missing" ]; then
        echo "home-scan-gate: INCOMPLETE — arm $arm missing $missing" >&2
        return 2
    fi
    if [ "$FRAMES_DONE" -lt "$FRAMES" ]; then
        echo "home-scan-gate: INCOMPLETE — arm $arm ran $FRAMES_DONE/$FRAMES frames" >&2
        return 2
    fi
    if [ "$HOME_FRAMES" -lt "$FRAMES" ]; then
        echo "home-scan-gate: INCOMPLETE — arm $arm left Home inactive ($HOME_FRAMES/$FRAMES frames)" >&2
        return 2
    fi
    return 0
}

if [ ! -x "$EXE" ]; then
    echo "home-scan-gate: $EXE not found — run 'make' first." >&2
    exit 2
fi

run_arm() {
    local scenario="$1" log="$2" view="${3:-}" home pid rc every=200
    if [ "$FRAMES" -lt 600 ]; then every=20; fi
    local view_env=()
    if [ -n "$view" ]; then view_env=("HANABI_VIEW=$view"); fi
    home="$(mktemp -d /tmp/hanabi_home_scan.XXXXXX)"
    mkdir -p "$home/Library/Application Support/hanabi"
    printf '%s\n' '{"window_width":1180,"window_height":949,"open_tabs":[],"active_tab":"","theme":"dark","subagent_sidebar_open":false}' \
        > "$home/Library/Application Support/hanabi/settings.json"
    env HOME="$home" HANABI_CACHE_DIR="$home/cache" \
        HANABI_CONFIG=/nonexistent/hanabi/home-scan.json \
        HANABI_BACKEND=mock HANABI_WIN_W=1180 HANABI_WIN_H=949 \
        HANABI_STRESS="$scenario" HANABI_STRESS_SESSIONS=20000 \
        HANABI_PROF=1 HANABI_SOAK="$FRAMES" HANABI_SOAK_EVERY="$every" \
        HANABI_SOAK_WARM_FRAMES=0 \
        HANABI_SOAK_MAX_RSS_KB_PER1K=999999 \
        HANABI_SOAK_MAX_HEAP_KB_PER1K=999999 \
        HANABI_SOAK_MAX_BLOCK_SLOPE_PER1K=999999 \
        HANABI_SOAK_MAX_GPU_KB_PER1K=999999 \
        HANABI_SOAK_MAX_ENT_PER1K=999999 HANABI_SOAK_MAX_MS_PER1K=999999 \
        "${view_env[@]}" \
        "$EXE" --screenshot "$home/shot.png" >"$log" 2>&1 &
    pid=$!
    watchdog_start "$pid" "$RUN_TIMEOUT"
    wait "$pid"
    rc=$?
    watchdog_stop
    rm -rf "$home"
    if [ "$rc" -ne 0 ]; then
        echo "home-scan-gate: INCOMPLETE — $scenario exited $rc" >&2
        tail -20 "$log" >&2
        return 2
    fi
}

if [ "$SELFTEST" = "1" ]; then
    TMP="$(mktemp -t hanabi_home_scan_selftest_XXXX).log"
    INACTIVE="$(mktemp -t hanabi_home_scan_inactive_XXXX).log"
    trap 'rm -f "$TMP" "$INACTIVE"' EXIT
    keys=(home_frames rebuilds reuse visits catalog frames catalog_refreshes)
    failed=0
    for missing in "${keys[@]}"; do
        : > "$TMP"
        [ "$missing" = home_frames ] || echo '[prof] home.frames 600 1.0' >> "$TMP"
        [ "$missing" = rebuilds ] || echo '[prof] home.scan_rebuild 1 0.0' >> "$TMP"
        [ "$missing" = reuse ] || echo '[prof] home.scan_reuse 10 1.0' >> "$TMP"
        [ "$missing" = visits ] || echo '[prof] home.scan_visits 20020 2002.0' >> "$TMP"
        [ "$missing" = catalog ] || echo '[prof] home.catalog 20020' >> "$TMP"
        [ "$missing" = frames ] || echo '[prof] 600 frames' >> "$TMP"
        [ "$missing" = catalog_refreshes ] || \
            echo '[soak] scenario idle did: catalog_refreshes=0 tabs_now=0' >> "$TMP"
        read_arm "$TMP" selftest >/dev/null 2>&1
        rc=$?
        if [ "$rc" -ne 2 ]; then
            echo "home-scan-gate --selftest: FAIL — missing $missing returned $rc" >&2
            failed=1
        fi
    done
    ORIGINAL_FRAMES="$FRAMES"
    FRAMES=60
    run_arm idle "$INACTIVE" chat || failed=1
    inactive_message="$(read_arm "$INACTIVE" inactive inactive 2>&1)"
    rc=$?
    FRAMES="$ORIGINAL_FRAMES"
    if [ "$rc" -ne 2 ] || [[ "$inactive_message" != *"INCOMPLETE"* ]] ||
       [[ "$inactive_message" == *"PASS"* ]]; then
        echo "home-scan-gate --selftest: FAIL — inactive Home was not INCOMPLETE" >&2
        failed=1
    fi
    if [ "$failed" -ne 0 ]; then exit 1; fi
    echo "home-scan-gate --selftest: PASS — missing metrics and inactive Home are INCOMPLETE"
    exit 0
fi

LOG_A="$(mktemp -t hanabi_home_scan_frozen_XXXX).log"
LOG_B="$(mktemp -t hanabi_home_scan_refresh_XXXX).log"
cleanup() { rm -f "$LOG_A" "$LOG_B"; }
trap cleanup EXIT

run_arm idle "$LOG_A" || exit $?
run_arm home-refresh "$LOG_B" || exit $?
read_arm "$LOG_A" frozen || exit $?
A_HOME_FRAMES="$HOME_FRAMES" A_REBUILDS="$REBUILDS" A_REUSE="$REUSE"
A_VISITS="$VISITS" A_CATALOG="$CATALOG" A_REFRESHES="$REFRESHES"
read_arm "$LOG_B" refresh || exit $?
B_HOME_FRAMES="$HOME_FRAMES" B_REBUILDS="$REBUILDS" B_REUSE="$REUSE"
B_VISITS="$VISITS" B_CATALOG="$CATALOG" B_REFRESHES="$REFRESHES"

printf '%-10s %8s %10s %10s %12s %10s %10s\n' arm home rebuilds reuse visits catalog refreshes
printf '%-10s %8s %10s %10s %12s %10s %10s\n' frozen "$A_HOME_FRAMES" "$A_REBUILDS" "$A_REUSE" "$A_VISITS" "$A_CATALOG" "$A_REFRESHES"
printf '%-10s %8s %10s %10s %12s %10s %10s\n' refresh "$B_HOME_FRAMES" "$B_REBUILDS" "$B_REUSE" "$B_VISITS" "$B_CATALOG" "$B_REFRESHES"

fail=0
if [ "$A_REBUILDS" -ne 1 ]; then
    echo "  FAIL: frozen Home rebuilt $A_REBUILDS times, expected 1" >&2
    fail=1
fi
if [ "$A_REUSE" -le "$A_REBUILDS" ]; then
    echo "  FAIL: frozen Home reused $A_REUSE times against $A_REBUILDS rebuilds" >&2
    fail=1
fi
if [ "$A_VISITS" -ne "$A_CATALOG" ]; then
    echo "  FAIL: frozen Home visited $A_VISITS sessions for a $A_CATALOG-session catalog" >&2
    fail=1
fi
if [ "$A_REFRESHES" -ne 0 ]; then
    echo "  FAIL: frozen arm refreshed the catalog $A_REFRESHES times" >&2
    fail=1
fi
if [ "$B_CATALOG" -ne "$A_CATALOG" ]; then
    echo "  FAIL: refresh arm changed catalog size $A_CATALOG -> $B_CATALOG" >&2
    fail=1
fi
if [ "$B_REFRESHES" -ne 1 ]; then
    echo "  FAIL: refresh arm refreshed the catalog $B_REFRESHES times, expected 1" >&2
    fail=1
fi
if [ "$B_REBUILDS" -ne $((A_REBUILDS + 1)) ]; then
    echo "  FAIL: one catalog refresh changed rebuilds $A_REBUILDS -> $B_REBUILDS" >&2
    fail=1
fi
if [ "$B_REUSE" -le "$B_REBUILDS" ]; then
    echo "  FAIL: refresh Home reused $B_REUSE times against $B_REBUILDS rebuilds" >&2
    fail=1
fi
if [ "$B_VISITS" -ne $((A_VISITS + B_CATALOG)) ]; then
    echo "  FAIL: one refresh changed visits $A_VISITS -> $B_VISITS, expected one additional catalog" >&2
    fail=1
fi
if [ "$fail" -ne 0 ]; then
    echo "home-scan-gate: FAIL"
    exit 1
fi
echo "home-scan-gate: PASS"
