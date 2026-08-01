#!/usr/bin/env bash
# ===========================================================================
# scripts/screens.sh  --  end-to-end screenshot harness for hanabi
#
# Captures a PNG of every screen / notable UI state the headless
# `--screenshot` path can reach, into an output dir (default
# /tmp/hanabi_screens). One command, review-them-all-at-once.
#
# WHAT IT DOES per state:
#   * writes the appropriate settings.json (window geometry / open_tabs /
#     active_tab / theme) into an ISOLATED HOME so it never depends on or
#     races the user's real config,
#   * runs the app headless (`--screenshot <png>`) with a per-shot timeout,
#     always pkill'ing any stray hanabi.exe afterward,
#   * verifies the PNG is exactly 1100x760.
#
# ISOLATION / SAFETY:
#   * The app resolves settings.json under $HOME/Library/Application Support/
#     hanabi (macOS). We run each capture with HOME pointed at a throwaway temp
#     dir, so we write OUR settings.json there and the app reads it there --
#     the user's real ~/Library/Application Support/hanabi/settings.json is
#     never touched by the render.
#   * DEFENSIVELY, per the harness contract, we ALSO back up the user's real
#     settings.json up front and restore it byte-for-byte on EXIT (trap),
#     asserting an md5 match. (With the temp-HOME isolation this is belt-and-
#     suspenders, but it guarantees the file is intact even if the isolation
#     ever regresses.)
#   * Mock backend is forced (HANABI_BACKEND=mock) and the runtime backend
#     config is isolated to a nonexistent path (HANABI_CONFIG=/tmp/none_$$),
#     so this never talks to a real backend and stays zero-config.
#
# HOVER STATES:
#   The headless capture has no mouse, so true hover states cannot arise from
#   hit-testing. We use a minimal, clearly test-only render hook
#   (src/test_hooks.h, gated behind the HANABI_TEST_HOVER env var -- a hard
#   no-op when unset) to FORCE one named widget into its hover branch for a
#   single capture. This lets us photograph e.g. a chat row's star-on-hover
#   and a hovered content tab.
#
# EXIT: non-zero if any capture failed or produced a non-1100x760 PNG.
# ===========================================================================
set -u

# --- locate the worktree root (this script lives in <root>/scripts) ---------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || { echo "cannot cd to repo root $ROOT" >&2; exit 2; }

EXE="$ROOT/output/hanabi.exe"
OUTDIR="${HANABI_SCREENS_OUT:-/tmp/hanabi_screens}"
SHOT_TIMEOUT="${HANABI_SHOT_TIMEOUT:-25}"   # seconds per capture
EXPECT_DIM="1100 x 760"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found or not executable. Build first (make -j4)." >&2
    exit 2
fi

mkdir -p "$OUTDIR"
rm -f "$OUTDIR"/*.png 2>/dev/null

# --- real settings backup + guaranteed restore (trap EXIT) ------------------
REAL_SETTINGS="$HOME/Library/Application Support/hanabi/settings.json"
BACKUP=""
BACKUP_MD5=""
had_real=0
if [ -f "$REAL_SETTINGS" ]; then
    had_real=1
    BACKUP="$(mktemp /tmp/hanabi_settings_backup.XXXXXX)"
    cp -p "$REAL_SETTINGS" "$BACKUP"
    BACKUP_MD5="$(md5 -q "$BACKUP")"
    echo "backed up real settings.json (md5 $BACKUP_MD5) -> $BACKUP"
else
    echo "no real settings.json present; nothing to back up"
fi

# temp HOME used for isolated captures
ISO_HOME="$(mktemp -d /tmp/hanabi_iso_home.XXXXXX)"

cleanup() {
    # kill any stray render
    pkill -9 -f hanabi.exe >/dev/null 2>&1
    # restore real settings byte-for-byte and assert md5 match
    if [ "$had_real" = "1" ]; then
        mkdir -p "$(dirname "$REAL_SETTINGS")"
        cp -p "$BACKUP" "$REAL_SETTINGS"
        local now
        now="$(md5 -q "$REAL_SETTINGS" 2>/dev/null || echo MISSING)"
        if [ "$now" = "$BACKUP_MD5" ]; then
            echo "restored real settings.json (md5 match $now)"
        else
            echo "WARNING: settings.json restore MISMATCH (expected $BACKUP_MD5 got $now)" >&2
        fi
        rm -f "$BACKUP"
    else
        # never existed -- make sure we didn't create one (we shouldn't have,
        # since we ran with an isolated HOME)
        if [ -f "$REAL_SETTINGS" ]; then
            echo "WARNING: a settings.json appeared at the real path though none existed; removing" >&2
            rm -f "$REAL_SETTINGS"
        fi
    fi
    rm -rf "$ISO_HOME"
}
trap cleanup EXIT

# --- results accounting -----------------------------------------------------
FAILED=0
declare -a SUMMARY

# write settings.json into the isolated HOME
write_settings() {
    # $1 = json body
    local dir="$ISO_HOME/Library/Application Support/hanabi"
    mkdir -p "$dir"
    printf '%s\n' "$1" > "$dir/settings.json"
}

# capture NN_name  <settings-json>  [EXTRA_ENV...]
# EXTRA_ENV entries are KEY=VALUE strings exported only for this run.
capture() {
    local name="$1"; shift
    local json="$1"; shift
    local png="$OUTDIR/${name}.png"
    rm -f "$png"
    write_settings "$json"

    # build env array
    local -a envv=(HOME="$ISO_HOME" HANABI_CONFIG="/tmp/none_$$" HANABI_BACKEND=mock)
    local kv
    for kv in "$@"; do envv+=("$kv"); done

    # run backgrounded with a timeout; never foreground (5s shell cap)
    ( env "${envv[@]}" "$EXE" --screenshot "$png" >"$OUTDIR/${name}.log" 2>&1 ) &
    local pid=$!
    local i
    for ((i=0; i<SHOT_TIMEOUT; i++)); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null
        echo "  TIMEOUT after ${SHOT_TIMEOUT}s"
    fi
    wait "$pid" 2>/dev/null
    pkill -9 -f hanabi.exe >/dev/null 2>&1

    # verify
    local dim status
    if [ -f "$png" ]; then
        dim="$(/usr/bin/file "$png" | sed -nE 's/.*, ([0-9]+ x [0-9]+),.*/\1/p')"
        if [ "$dim" = "$EXPECT_DIM" ]; then
            status="OK"
        else
            status="BAD_DIM($dim)"; FAILED=1
        fi
    else
        dim="-"; status="MISSING"; FAILED=1
    fi
    printf '  %-22s %-14s %s\n' "$name" "$status" "$png"
    SUMMARY+=("$(printf '%-22s %-14s %s' "$name" "$status" "$png")")
}

echo "=== capturing into $OUTDIR (timeout ${SHOT_TIMEOUT}s/shot) ==="

# ---------------------------------------------------------------------------
# States. NN prefix keeps them ordered on disk for at-a-glance review.
# ---------------------------------------------------------------------------

# Home digest (no tabs) -- dark + light. `view` defaults to Home when no tabs.
capture 01_home_dark  '{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}'
capture 02_home_light '{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"light"}'

# Transcript with tabs + sub-agent panel. t2 (Stars payout, blocked, has sub-
# agents running+done) active; t6 (Backfill, 3 sub-agents) and t1 (Multi-tier,
# blocked) also open so the tab strip shows multiple tabs.
capture 03_transcript_dark  '{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t2","theme":"dark"}'
capture 04_transcript_light '{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t2","theme":"light"}'

# A different active transcript: t6 (Backfill, running + 3 sub-agents) so the
# sub-agent panel content differs from t2.
capture 05_transcript_t6_dark '{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t6","theme":"dark"}'

# HOVER STATES (test-only hook). Forces one widget's hover branch.
#  - a hovered UNSTARRED chat row revealing its faint hollow star. t2 is
#    unstarred in the mock (folder "stars"), so HANABI_TEST_HOVER=row:t2 shows
#    the hollow-star affordance a real mouse hover would reveal.
capture 06_hover_row_star_dark  '{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}'  HANABI_TEST_HOVER=row:t2
#  - a hovered content TAB (non-active) showing the tab-hover background. With
#    t2 active, hovering t6 lights its hover bg.
capture 07_hover_tab_dark '{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t2","theme":"dark"}'  HANABI_TEST_HOVER=tab:t6

echo
echo "=== SUMMARY ==="
for line in "${SUMMARY[@]}"; do echo "$line"; done

# ---------------------------------------------------------------------------
# States that CANNOT be captured via headless --screenshot today, and why.
# (Documented, not silently skipped.)
# ---------------------------------------------------------------------------
cat <<'NOTE'

=== NOT capturable headlessly today (documented) ===
  blocked_dark / review / starred smart views
      The active smart view (AppComponent::view) is in-app state, set by a
      SIDEBAR CLICK -- it is NOT persisted in or loaded from settings.json
      (only Home vs Chat are reachable: Home when open_tabs is empty, Chat when
      tabs restore). No headless mouse => cannot click a smart view. Needs an
      in-app click, or a new settings/env key to preselect AppComponent::view.

  sidebar_folded
      LayoutComponent::sidebarCollapsed is not persisted in settings and is
      toggled only by Cmd+B / a click. No headless keyboard/mouse => cannot
      fold. Needs a persisted "sidebar_collapsed" setting or a test env knob.

  settings_overlay (Cmd+,) / composer_overlay (Cmd+N)
      AppComponent::showSettings / composerOpen are toggled by keypress only,
      never read from settings.json. No headless key events => cannot open.
      A test hook (e.g. HANABI_TEST_OVERLAY=settings|composer read once in
      setup_app_state) would be needed to open one for a capture.

  transcript_empty (a thread with 0 messages)
      The mock backend is a zero-config compile-time fixture and EVERY sample
      session (t1..t14) has >=1 message; there is no empty thread to open. The
      mock must stay zero-config, so no empty fixture is added. Would require
      either an http backend serving an empty session or a dedicated fixture.
NOTE

if [ "$FAILED" -ne 0 ]; then
    echo
    echo "RESULT: FAILED -- one or more captures missing or not ${EXPECT_DIM// /}." >&2
    exit 1
fi
echo
echo "RESULT: OK -- all captures present and $EXPECT_DIM."
exit 0
