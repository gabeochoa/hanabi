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
#   Every screen the app can reach is covered: the smart views via HANABI_VIEW,
#   the folded sidebar via the persisted sidebar_collapsed setting, the
#   keypress-only overlays (settings / new task / device-code login) via
#   HANABI_TEST_OVERLAY and HANABI_AUTH_DEMO, and the transient states
#   (skeleton, thread-loading, load-older, thinking, streaming) via their
#   *_DEMO knobs. Add a state here rather than running the app by hand.
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

# HANABI_SCREENS_FILTER: extended regex; only matching state names are
# captured. The baseline suite (make validate-screenshots) passes the names it
# has baselines for so a three-screen check does not render all 32.
FILTER="${HANABI_SCREENS_FILTER:-}"

# `--list` prints the name of every state declared below, one per line, and
# exits without rendering anything. The baseline check needs it: validation
# only RE-captures the states that already have a baseline, so a state added
# here would otherwise never appear in a comparison and would go unnoticed
# forever. Listing is how the suite learns a screen exists before it has a
# baseline. It ignores HANABI_SCREENS_FILTER -- the point is the full set.
LIST_ONLY=0
[ "${1:-}" = "--list" ] && LIST_ONLY=1

listing() { [ "$LIST_ONLY" = "1" ]; }

screen_selected() {
    [ -z "$FILTER" ] && return 0
    printf '%s\n' "$1" | grep -Eq "$FILTER"
}

# Stray-render cleanup is scoped to THIS worktree's binary path: several
# checkouts capture on one machine at a time, and `pkill -f hanabi.exe` kills
# the other worktrees' renders mid-capture.
kill_own_renders() {
    pkill -9 -f "^$EXE" >/dev/null 2>&1
}

# Everything below this point is capture scaffolding -- a built binary, an
# output dir, the user's settings backed up, a throwaway HOME. `--list` needs
# none of it and must not have the side effects, so it skips the lot.
if ! listing; then

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
    # kill any stray render started by THIS worktree
    kill_own_renders
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

fi   # ! listing

# --- results accounting -----------------------------------------------------
FAILED=0
declare -a SUMMARY

# THE CAPTURE'S NOW, and its timezone.
#
# The mock seeds every stamp at now-N and the UI renders it against now, so an
# age reads "3h" on any day — but a DATE DIVIDER and a wall-clock stamp are
# absolute, and they move with the hour. Measured across three local hours on
# one machine and one build, 17 of these 35 states changed: every transcript
# screen gained or lost a date row as local midnight swept through the fixture.
# The set was reproducible for an hour or two after it was captured and rotted
# by the clock after that, which is why 28 of 30 baselines could fail on a tree
# that had not touched rendering.
#
# Pinning both ends removes it: HANABI_MOCK_NOW fixes the datum, and
# HANABI_CAPTURE_EPOCH fixes what the renderer measures it against
# (src/util/capture_clock.h) plus the frozen durations. TZ is pinned too, since
# a divider and a "14:05" are LOCAL, and a baseline should not belong to one
# machine's zone. Midday Monday keeps the fixture off a date boundary.
#
# Changing these three re-renders every screen: it is a baseline update, not a
# knob to tune.
export HANABI_CAPTURE_EPOCH="${HANABI_CAPTURE_EPOCH:-1781524800}"   # 2026-06-15 12:00:00Z
export HANABI_MOCK_NOW="$HANABI_CAPTURE_EPOCH"
export TZ="${HANABI_SHOT_TZ:-UTC}"

# write settings.json into the isolated HOME
write_settings() {
    # $1 = json body
    local dir="$ISO_HOME/Library/Application Support/hanabi"
    mkdir -p "$dir"
    rm -rf "$ISO_HOME/cache"
    printf '%s\n' "$1" > "$dir/settings.json"
}

# capture NN_name  <settings-json>  [EXTRA_ENV...]
# EXTRA_ENV entries are KEY=VALUE strings exported only for this run.
capture() {
    local name="$1"; shift
    if listing; then printf '%s\n' "$name"; return 0; fi
    local json="$1"; shift
    screen_selected "$name" || return 0
    local png="$OUTDIR/${name}.png"
    rm -f "$png"
    write_settings "$json"

    # build env array
    local -a envv=(HOME="$ISO_HOME" HANABI_CACHE_DIR="$ISO_HOME/cache" HANABI_CONFIG="/tmp/none_$$" HANABI_BACKEND=mock)
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
    kill_own_renders

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

capture_sized() {
    local name="$1"
    local dim="$2"
    shift 2
    local previous="$EXPECT_DIM"
    EXPECT_DIM="$dim"
    capture "$name" "$@"
    EXPECT_DIM="$previous"
}

listing || echo "=== capturing into $OUTDIR (timeout ${SHOT_TIMEOUT}s/shot) ==="

# ---------------------------------------------------------------------------
# States. NN prefix keeps them ordered on disk for at-a-glance review.
# ---------------------------------------------------------------------------

# Shorthand settings bodies. TABS opens three threads so the strip is real;
# NOTABS lands on the Home digest.
NOTABS_DARK='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}'
NOTABS_LIGHT='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"light"}'
PINNED_DARK='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark","starred":["t9"]}'
ARCHIVED_DARK='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark","archived":{"t3":true}}'
TABS_DARK='{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t2","theme":"dark"}'
TABS_LIGHT='{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t2","theme":"light"}'
TABS_T6='{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t6","theme":"dark"}'
MANYTABS='{"window_width":1100,"window_height":760,"open_tabs":["t1","t2","t3","t4","t5","t6","t7","t8","t9","t10","r1","r2","r4","r5","r6","r7","r8","r9","r10","r11"],"active_tab":"r11","pinned_tabs":["r10","r11"],"theme":"dark"}'
NARROW_DARK='{"window_width":760,"window_height":620,"open_tabs":["t2","t6","t1"],"active_tab":"t2","theme":"dark"}'
NARROW_MANY='{"window_width":760,"window_height":620,"open_tabs":["t1","t2","t3","t4","t5","t6","t7","t8","t9","t10"],"active_tab":"t5","theme":"dark"}'
SETTINGS_NARROW='{"window_width":600,"window_height":500,"open_tabs":[],"active_tab":"","theme":"dark"}'
NARROW_SPLIT='{"window_width":760,"window_height":620,"open_tabs":["r8","t2"],"active_tab":"r8","split_open":true,"split_ratio":0.2,"split_panes":["r8","t2"],"theme":"dark","sidebar_collapsed":true}'
FOLDED='{"window_width":1100,"window_height":760,"open_tabs":["t2"],"active_tab":"t2","theme":"dark","sidebar_collapsed":true}'
SUBAGENTS_DARK='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark","subagent_sidebar_open":true}'
# The brake fixtures (HANABI_BRAKES_DEMO): bz1 frozen, bz2 replies-paused,
# bz3 waiting on you, bz4 archived on the server, bz5 halted.
# bz2 is the ACTIVE tab in 39/40 on purpose -- see the note by those captures.
BRAKES_NOTABS_DARK='{"window_width":1100,"window_height":760,"open_tabs":["bz2"],"active_tab":"bz2","theme":"dark"}'
BRAKES_NOTABS_LIGHT='{"window_width":1100,"window_height":760,"open_tabs":["bz2"],"active_tab":"bz2","theme":"light"}'
BRAKES_ARCHIVED_DARK='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"dark"}'
BRAKES_ARCHIVED_LIGHT='{"window_width":1100,"window_height":760,"open_tabs":[],"active_tab":"","theme":"light"}'
BRAKES_FROZEN_DARK='{"window_width":1100,"window_height":760,"open_tabs":["bz1"],"active_tab":"bz1","theme":"dark"}'
BRAKES_FROZEN_LIGHT='{"window_width":1100,"window_height":760,"open_tabs":["bz1"],"active_tab":"bz1","theme":"light"}'
BRAKES_HALTED_DARK='{"window_width":1100,"window_height":760,"open_tabs":["bz5"],"active_tab":"bz5","theme":"dark"}'
BRAKES_HALTED_LIGHT='{"window_width":1100,"window_height":760,"open_tabs":["bz5"],"active_tab":"bz5","theme":"light"}'
BRAKES_TABS_DARK='{"window_width":1100,"window_height":760,"open_tabs":["bz1","bz3","bz2","t9","bz4","r11"],"active_tab":"bz3","theme":"dark"}'
BRAKES_TABS_LIGHT='{"window_width":1100,"window_height":760,"open_tabs":["bz1","bz3","bz2","t9","bz4","r11"],"active_tab":"bz3","theme":"light"}'
BRAKES_TABS_NARROW='{"window_width":760,"window_height":620,"open_tabs":["bz1","bz3","bz2","t9","bz4","r11"],"active_tab":"bz3","theme":"dark"}'
PLAN_SPLIT_DARK='{"window_width":1100,"window_height":760,"open_tabs":["t2","t6"],"active_tab":"t2","split_open":true,"split_ratio":0.5,"split_panes":["t2","t6"],"split_focused_pane":0,"theme":"dark"}'

# --- Home digest ------------------------------------------------------------
capture 01_home_dark  "$NOTABS_DARK"
capture 02_home_light "$NOTABS_LIGHT"

# --- Transcript. t2 (Stars payout, blocked, sub-agents running+done) active;
#     t6 (Backfill, 3 sub-agents) and t1 (Multi-tier, blocked) also open.
capture 03_transcript_dark  "$TABS_DARK"
capture 04_transcript_light "$TABS_LIGHT"
capture 05_transcript_t6_dark "$TABS_T6"

# --- HOVER STATES (test-only hook). Forces one widget's hover branch.
#  - a hovered UNSTARRED chat row revealing its faint hollow star. t2 is
#    unstarred in the mock (folder "stars"), so HANABI_TEST_HOVER=row:t2 shows
#    the hollow-star affordance a real mouse hover would reveal.
capture 06_hover_row_star_dark "$NOTABS_DARK" HANABI_TEST_HOVER=row:t2
#  - a hovered content TAB (non-active) showing the tab-hover background. With
#    t2 active, hovering t6 lights its hover bg.
capture 07_hover_tab_dark "$TABS_DARK" HANABI_TEST_HOVER=tab:t6
#  - a hovered MESSAGE revealing its Copy action. m2 is the first assistant
#    message of t2, so the bar appears under its body.
capture 07b_hover_msg_copy_dark "$TABS_DARK" HANABI_TEST_HOVER=msg:m2
capture 07c_hover_user_actions_dark "$TABS_DARK" HANABI_TEST_HOVER=msg:m1

# --- Smart views. HANABI_VIEW forces the landing view (no click needed).
capture 08_view_blocked_dark  "$NOTABS_DARK"  HANABI_VIEW=blocked
capture 09_view_review_dark   "$NOTABS_DARK"  HANABI_VIEW=review
capture 10_view_starred_dark  "$NOTABS_DARK"  HANABI_VIEW=starred
capture 10b_view_pinned_row_dark "$PINNED_DARK" HANABI_VIEW=starred
capture 11_view_archived_dark "$NOTABS_DARK"  HANABI_VIEW=archived
capture 11b_view_archived_row_dark "$ARCHIVED_DARK" HANABI_VIEW=archived
capture 12_view_blocked_light "$NOTABS_LIGHT" HANABI_VIEW=blocked

# --- Chat with no thread open: the welcome / start-a-conversation screen.
capture 13_chat_welcome_dark "$NOTABS_DARK" HANABI_VIEW=chat
capture 13a_chat_welcome_light "$NOTABS_LIGHT" HANABI_VIEW=chat
capture 13b_empty_transcript_dark "$TABS_DARK" HANABI_EMPTY_TRANSCRIPT_DEMO=1
capture 13c_empty_transcript_light "$TABS_LIGHT" HANABI_EMPTY_TRANSCRIPT_DEMO=1

# --- Sidebar folded to the icon rail (persisted setting, no Cmd+B needed).
capture 14_sidebar_folded_dark "$FOLDED"
capture 14b_subagent_sidebar_dark "$SUBAGENTS_DARK"

# --- Overlays that are otherwise keypress-only.
capture 15_settings_dark  "$NOTABS_DARK"  HANABI_TEST_OVERLAY=settings
capture 16_settings_light "$NOTABS_LIGHT" HANABI_TEST_OVERLAY=settings
capture 17_newtask_dark    "$NOTABS_DARK"  HANABI_TEST_OVERLAY=composer
capture 17d_newtask_light  "$NOTABS_LIGHT" HANABI_TEST_OVERLAY=composer
capture 17b_shortcuts_dark  "$NOTABS_DARK"  HANABI_TEST_OVERLAY=shortcuts
capture 17c_shortcuts_light "$NOTABS_LIGHT" HANABI_TEST_OVERLAY=shortcuts
capture 17e_shortcut_recorder_dark  "$NOTABS_DARK"  HANABI_TEST_OVERLAY=shortcuts-recording
capture 17f_shortcut_recorder_light "$NOTABS_LIGHT" HANABI_TEST_OVERLAY=shortcuts-recording
capture 18_auth_dark       "$NOTABS_DARK"  HANABI_AUTH_DEMO=1
capture 18a_auth_light     "$NOTABS_LIGHT" HANABI_AUTH_DEMO=1
capture 18b_palette_dark   "$NOTABS_DARK"  HANABI_TEST_OVERLAY=palette
capture 18c_palette_light  "$NOTABS_LIGHT" HANABI_TEST_OVERLAY=palette
capture 18d_palette_empty_dark "$NOTABS_DARK" HANABI_PALETTE_DEMO=zzzz-no-match
capture 18e_palette_empty_light "$NOTABS_LIGHT" HANABI_PALETTE_DEMO=zzzz-no-match
capture 18f_search_dark "$NOTABS_DARK" HANABI_XSEARCH_DEMO=decision
capture 18g_search_light "$NOTABS_LIGHT" HANABI_XSEARCH_DEMO=decision
capture 18h_search_empty_dark "$NOTABS_DARK" HANABI_XSEARCH_DEMO=zzzz-no-match
capture 18i_search_empty_light "$NOTABS_LIGHT" HANABI_XSEARCH_DEMO=zzzz-no-match
capture 18j_model_picker_dark "$TABS_DARK" HANABI_TEST_OVERLAY=model
capture 18k_model_picker_light "$TABS_LIGHT" HANABI_TEST_OVERLAY=model
capture 18l_effort_picker_dark "$TABS_DARK" HANABI_TEST_OVERLAY=effort
capture 18m_effort_picker_light "$TABS_LIGHT" HANABI_TEST_OVERLAY=effort
capture 18n_slash_menu_dark "$TABS_DARK" HANABI_TEST_OVERLAY=slash
capture 18o_slash_menu_light "$TABS_LIGHT" HANABI_TEST_OVERLAY=slash
capture 18p_context_menu_dark "$NOTABS_DARK" HANABI_TEST_OVERLAY=row-menu
capture 18q_context_menu_light "$NOTABS_LIGHT" HANABI_TEST_OVERLAY=row-menu
capture 18p2_tab_menu_dark "$TABS_DARK" HANABI_TEST_OVERLAY=tab-menu
capture 18q2_tab_menu_light "$TABS_LIGHT" HANABI_TEST_OVERLAY=tab-menu
capture 18r_toast_dark "$NOTABS_DARK" HANABI_TEST_OVERLAY=toast
capture 18s_toast_light "$NOTABS_LIGHT" HANABI_TEST_OVERLAY=toast
capture 18t_auth_failed_dark "$NOTABS_DARK" HANABI_AUTH_DEMO=failed
capture 18u_auth_failed_light "$NOTABS_LIGHT" HANABI_AUTH_DEMO=failed
capture 18v_transcript_error_dark "$TABS_DARK" 'HANABI_TRANSCRIPT_ERROR_DEMO=The conversation could not be refreshed.'
capture 18w_transcript_error_light "$TABS_LIGHT" 'HANABI_TRANSCRIPT_ERROR_DEMO=The conversation could not be refreshed.'

# --- Tab strip under pressure: ten open tabs (overflow / shrink-to-fit).
capture 19_many_tabs_dark "$MANYTABS"

# --- Long transcript (perf fixture) + every tool row pre-expanded.
capture 20_big_transcript_dark "$TABS_DARK" HANABI_BIG_TRANSCRIPT=1 HANABI_OPEN=rbig
capture 21_tools_expanded_dark "$TABS_DARK" HANABI_EXPAND=1
capture 21b_tools_expanded_detail_dark "$TABS_DARK" HANABI_FOLD_DEMO=1 HANABI_OPEN=rfold HANABI_EXPAND=1
capture 22_split_view_dark     "$TABS_DARK" HANABI_OPEN=t2 HANABI_SPLIT=t6

# --- Transient states: the ones a user only sees for a second.
capture 23_skeleton_dark      "$NOTABS_DARK" HANABI_SKELETON_DEMO=1
capture 24_thread_loading_dark "$TABS_DARK"  HANABI_LOADING_DEMO=1
capture 25_load_older_dark    "$TABS_DARK"   HANABI_OLDER_DEMO=1
capture 26_thinking_dark      "$TABS_DARK"   HANABI_THINK_DEMO=1
capture 27_streaming_dark     "$TABS_DARK"   HANABI_STREAM_DEMO=1

# --- Composer focused (caret + focus ring).
capture 28_composer_focus_dark "$TABS_DARK" HANABI_TEST_FOCUS_COMPOSER=1

# --- Find in conversation, with matches highlighted.
capture 29_find_dark  "$TABS_DARK"  HANABI_FIND_DEMO=ledger
capture 30_find_light "$TABS_LIGHT" HANABI_FIND_DEMO=ledger

# --- A run of transcript text selected.
capture 31_selection_dark "$TABS_DARK" 'HANABI_SELECT_DEMO=4,810 match to the cent'

# --- Reopening a thread that gained messages while you were away.
capture 32_new_messages_dark "$NOTABS_DARK" HANABI_BIG_TRANSCRIPT=1 HANABI_OPEN=rbig HANABI_UNREAD_DEMO=4

capture_sized 33_narrow_dark "760 x 620" "$NARROW_DARK" HANABI_WIN_W=760 HANABI_WIN_H=620
capture_sized 34_narrow_many_tabs_dark "760 x 620" "$NARROW_MANY" HANABI_WIN_W=760 HANABI_WIN_H=620
capture_sized 35_settings_narrow_dark "600 x 500" "$SETTINGS_NARROW" HANABI_WIN_W=600 HANABI_WIN_H=500 HANABI_TEST_OVERLAY=settings
capture_sized 36_shortcuts_narrow_dark "600 x 500" "$SETTINGS_NARROW" HANABI_WIN_W=600 HANABI_WIN_H=500 HANABI_TEST_OVERLAY=shortcuts
capture_sized 37_narrow_split_dark "760 x 620" "$NARROW_SPLIT" HANABI_WIN_W=760 HANABI_WIN_H=620
capture 38_workspace_groups_dark "$NOTABS_DARK" HANABI_FOLDER_DEMO=1

# --- Brakes and the two new marks (HANABI_BRAKES_DEMO). Six fixture threads:
#     bz1 frozen on the catalog row, bz2 replies-paused (attach only -- no
#     catalog row can carry that key), bz3 waiting on you, bz4 archived by the
#     server, bz5 halted by an ancestor, and bz6 frozen ONLY on the attach
#     while its row still reads as running -- the freeze-between-polls race.
#     Eleven captures, 39-49:
#
#  39/40 — the SIDEBAR MARKS, dark and light, where the freeze, the pause and
#          the plain wait each wear a mark of their own. Before this they were
#          a bang, a dot and a bang.
#          bz2 is the ACTIVE tab on purpose: `channel_replies_paused` is an
#          attach-only key that no catalog row can carry, so the pause mark is
#          only real once the thread has been opened. Opening it is what makes
#          this capture evidence of the shipped path (attach ->
#          apply_attach_brakes -> the catalog row the sidebar reads) rather
#          than of a fixture that set a field the wire never sends.
#  41/42 — the frozen COMPOSER, dark and light: it says why and refuses.
#  43/44 — the halted composer, dark and light, which warns and still takes a
#          draft. Separate from 41/42 because "which brake refuses" is the
#          distinction the wave turns on and one picture cannot hold both.
#  45/46 — the TAB STRIP carrying status, dark and light, which it never did
#          before. Six tabs so the marks sit side by side: bz1 frozen, bz3
#          waiting (the active one), t9 working, bz4 -- which is tagged Done
#          and so draws the green TICK, not nothing -- and r11, a genuinely
#          IDLE thread, which is the one tab that correctly draws no mark at
#          all. `status_shows_on_tab` in tests/e2e/test_e2e.cpp pins that rule.
#          bz2 is open too and draws NO mark here, which is also correct: its
#          pause is attach-only and only the active tab is attached. 39/40 are
#          where the pause mark is shown, with bz2 active.
#     47 — the same strip at 760x620, where the tabs are at their narrowest.
#          Its right edge shows a CLIPPED tab: one × for the last whole tab and
#          one for the clipped one, whose × is deliberately clamped inside the
#          edge ("a tab you can see is a tab you can close" -- tab_bar_system.h,
#          and the same pair is in the pre-wave 34 baseline, so it is not this
#          wave's). What IS this wave's is that the clipped tab draws no status
#          mark: on a sliver the left-gutter mark lands beside the previous
#          tab's chrome and reads as a stray glyph. This capture is the
#          assertion for both halves.
#  48/49 — the ARCHIVED view, dark and light: bz4 carries only the server's
#          `archived_at_unix_ms` and no local override, so its presence here is
#          the whole proof that a thread archived elsewhere reads as archived.
capture 39_brakes_sidebar_dark  "$BRAKES_NOTABS_DARK"  HANABI_BRAKES_DEMO=1
capture 40_brakes_sidebar_light "$BRAKES_NOTABS_LIGHT" HANABI_BRAKES_DEMO=1
capture 41_frozen_composer_dark  "$BRAKES_FROZEN_DARK"  HANABI_BRAKES_DEMO=1
capture 42_frozen_composer_light "$BRAKES_FROZEN_LIGHT" HANABI_BRAKES_DEMO=1
capture 43_halted_composer_dark  "$BRAKES_HALTED_DARK"  HANABI_BRAKES_DEMO=1
capture 44_halted_composer_light "$BRAKES_HALTED_LIGHT" HANABI_BRAKES_DEMO=1
capture 45_tab_status_dark  "$BRAKES_TABS_DARK"  HANABI_BRAKES_DEMO=1
capture 46_tab_status_light "$BRAKES_TABS_LIGHT" HANABI_BRAKES_DEMO=1
capture_sized 47_tab_status_narrow_dark "760 x 620" "$BRAKES_TABS_NARROW" \
    HANABI_WIN_W=760 HANABI_WIN_H=620 HANABI_BRAKES_DEMO=1
capture 48_server_archived_dark  "$BRAKES_ARCHIVED_DARK"  HANABI_BRAKES_DEMO=1 HANABI_VIEW=archived
capture 49_server_archived_light "$BRAKES_ARCHIVED_LIGHT" HANABI_BRAKES_DEMO=1 HANABI_VIEW=archived

capture 50_plan_goal_dark "$TABS_DARK" HANABI_PLAN_DEMO=1 HANABI_TEST_OVERLAY=plan
capture 51_plan_goal_light "$TABS_LIGHT" HANABI_PLAN_DEMO=1 HANABI_TEST_OVERLAY=plan
capture_sized 52_plan_goal_narrow_dark "760 x 620" "$NARROW_DARK" \
    HANABI_WIN_W=760 HANABI_WIN_H=620 HANABI_PLAN_DEMO=1 HANABI_TEST_OVERLAY=plan
capture 53_plan_goal_split_dark "$PLAN_SPLIT_DARK" HANABI_PLAN_DEMO=1 HANABI_TEST_OVERLAY=plan
capture 54_plan_cancelled_blocked_dark "$TABS_DARK" HANABI_PLAN_DEMO=blocked HANABI_TEST_OVERLAY=plan

capture 55_ask_card_dark  "$TABS_DARK"  HANABI_ASK_DEMO=1
capture 56_ask_card_light "$TABS_LIGHT" HANABI_ASK_DEMO=1
capture_sized 57_ask_card_narrow_dark "760 x 620" "$NARROW_DARK" \
    HANABI_WIN_W=760 HANABI_WIN_H=620 HANABI_ASK_DEMO=1
capture 58_ask_card_split_dark "$PLAN_SPLIT_DARK" HANABI_ASK_DEMO=1
capture 59_ask_approval_dark   "$TABS_DARK"       HANABI_ASK_DEMO=approval
capture_sized 60_ask_full_form_narrow_dark "760 x 620" "$NARROW_DARK" \
    HANABI_WIN_W=760 HANABI_WIN_H=620 HANABI_ASK_DEMO=big
capture 61_ask_long_approval_dark "$TABS_DARK" HANABI_ASK_DEMO=longapproval
capture_sized 62_ask_with_attachment_narrow_dark "760 x 620" "$NARROW_DARK" \
    HANABI_WIN_W=760 HANABI_WIN_H=620 HANABI_ASK_DEMO=big \
    HANABI_ATTACH_DEMO="$ROOT/resources/icons/icons.png"

listing && exit 0

echo
echo "=== SUMMARY ==="
if [ "${#SUMMARY[@]}" -eq 0 ]; then
    echo "no states matched HANABI_SCREENS_FILTER='$FILTER'" >&2
    exit 1
fi
for line in "${SUMMARY[@]}"; do echo "$line"; done

if [ "$FAILED" -ne 0 ]; then
    echo
    echo "RESULT: FAILED -- one or more captures missing or not ${EXPECT_DIM// /}." >&2
    exit 1
fi
echo
echo "RESULT: OK -- all captures present at their declared dimensions."
exit 0
