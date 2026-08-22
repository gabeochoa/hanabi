#!/usr/bin/env bash
# ===========================================================================
# scripts/review_shots.sh  --  before/after PNGs for a pull request
#
#   bash scripts/review_shots.sh <base-ref> <name> [ENV=VAL ...]
#
# Builds <base-ref> and the current HEAD and captures the SAME app state from
# each, writing docs/screenshots/review/<name>-{before,after}.png. Those get
# committed and referenced from the PR body, so a change can be reviewed from
# the PR page without building anything.
#
# The base build happens in a SEPARATE git worktree under /tmp. Your checkout
# is never switched, nothing is stashed, and an interrupted run cannot leave
# you on the wrong branch with your work parked in a stash.
#
# The state is whatever the env pairs select — the same knobs scripts/screens.sh
# uses (HANABI_VIEW, HANABI_TEST_OVERLAY, the *_DEMO forcings), plus:
#
#   HANABI_SHOT_SETTINGS  the settings.json body (open tabs / theme / size)
#   HANABI_SHOT_CROP      "WxH+X+Y" — also write a 2x detail pair, so a small
#                         change is legible in a PR without downloading the
#                         whole 1100x760 window
#   HANABI_SHOT_SCRIPT    a .e2e script, for states a settings file cannot
#                         reach: hover, a click, typing. Its `screenshot` line
#                         is rewritten to the output path. Runs on the uitest
#                         binary.
#
# ISOLATION: same contract as screens.sh — throwaway HOME, mock backend forced,
# runtime config pointed at a path that does not exist. The user's real
# settings.json is never read or written.
# ===========================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

BASE="${1:-}"
NAME="${2:-}"
if [ -z "$BASE" ] || [ -z "$NAME" ]; then
    echo "usage: bash scripts/review_shots.sh <base-ref> <name> [ENV=VAL ...]" >&2
    echo "  e.g. HANABI_SHOT_CROP=440x74+620+652 \\" >&2
    echo "       bash scripts/review_shots.sh main composer-footer HANABI_OPEN=t2" >&2
    exit 2
fi
shift 2

OUTDIR="$ROOT/docs/screenshots/review"
STAGE="$(mktemp -d /tmp/hanabi_shot_stage.XXXXXX)"
ISO_HOME="$(mktemp -d /tmp/hanabi_shot_home.XXXXXX)"
mkdir -p "$ISO_HOME/Library/Application Support/hanabi"
BASE_WT="/tmp/hanabi_shot_base_$$"

SETTINGS="${HANABI_SHOT_SETTINGS:-{\"window_width\":1100,\"window_height\":760,\"open_tabs\":[],\"active_tab\":\"\",\"theme\":\"dark\"}}"
SHOT_SCRIPT="${HANABI_SHOT_SCRIPT:-}"

cleanup() {
    pkill -9 -f hanabi.exe >/dev/null 2>&1
    pkill -9 -f hanabi_uitest.exe >/dev/null 2>&1
    git worktree remove --force "$BASE_WT" >/dev/null 2>&1
    rm -rf "$ISO_HOME" "$STAGE" "$BASE_WT"
}
trap cleanup EXIT

# capture <worktree> <label> [ENV=VAL ...]
capture() {
    local wt="$1"; shift
    local label="$1"; shift
    local png="$STAGE/${NAME}-${label}.png"
    local log="/tmp/review_shot_${NAME}_${label}.log"
    rm -f "$png"
    printf '%s\n' "$SETTINGS" > "$ISO_HOME/Library/Application Support/hanabi/settings.json"

    local -a envv=(HOME="$ISO_HOME" HANABI_CONFIG="/tmp/none_$$" HANABI_BACKEND=mock)
    local kv
    for kv in "$@"; do envv+=("$kv"); done

    if [ -n "$SHOT_SCRIPT" ]; then
        # uitest-build is recent; an older base ref only has the exe target.
        ( cd "$wt" && { make uitest-build || make output/hanabi_uitest.exe copy-resources; } ) \
            >"${log%.log}_build.log" 2>&1
        local tmp="$STAGE/${label}.e2e"
        sed "s#^screenshot .*#screenshot $png#" "$SHOT_SCRIPT" > "$tmp"
        ( env "${envv[@]}" "$wt/output/hanabi_uitest.exe" --e2e "$tmp" >"$log" 2>&1 ) &
    else
        ( cd "$wt" && make ) >"${log%.log}_build.log" 2>&1
        ( env "${envv[@]}" "$wt/output/hanabi.exe" --screenshot "$png" >"$log" 2>&1 ) &
    fi
    local pid=$! i
    for ((i=0; i<60; i++)); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    pkill -9 -f hanabi.exe >/dev/null 2>&1
    pkill -9 -f hanabi_uitest.exe >/dev/null 2>&1

    if [ -f "$png" ]; then
        printf '  %-8s captured\n' "$label"
    else
        printf '  %-8s MISSING — see %s\n' "$label" "$log" >&2
        tail -4 "$log" 2>/dev/null | sed 's/^/      /' >&2
        tail -4 "${log%.log}_build.log" 2>/dev/null | sed 's/^/      /' >&2
        return 1
    fi
}

FAILED=0

echo "=== $NAME: before ($BASE) ==="
git worktree add --detach --quiet "$BASE_WT" "$BASE" || exit 2
# A worktree gets no submodules of its own. The vendored UI library is pinned
# per commit, so link it when the pin matches and copy the checkout in when it
# does not — either way the base builds against the library IT expects.
BASE_PIN="$(git -C "$ROOT" rev-parse "$BASE:vendor/afterhours" 2>/dev/null || echo x)"
HEAD_PIN="$(git -C "$ROOT" rev-parse "HEAD:vendor/afterhours" 2>/dev/null || echo y)"
rmdir "$BASE_WT/vendor/afterhours" 2>/dev/null
if [ "$BASE_PIN" = "$HEAD_PIN" ]; then
    ln -s "$ROOT/vendor/afterhours" "$BASE_WT/vendor/afterhours"
else
    echo "  (base pins a different vendor/afterhours — checking it out)"
    cp -R "$ROOT/vendor/afterhours" "$BASE_WT/vendor/afterhours"
    git -C "$BASE_WT/vendor/afterhours" checkout -q "$BASE_PIN"
fi
cp -R "$ROOT/resources" "$BASE_WT/" 2>/dev/null
capture "$BASE_WT" before "$@" || FAILED=1

echo "=== $NAME: after (HEAD) ==="
capture "$ROOT" after "$@" || FAILED=1

if [ "$FAILED" = "0" ]; then
    # An identical pair means the capture never reached the change — the wrong
    # state, or a change that needs an interaction script. Say so rather than
    # committing two copies of the same picture.
    if cmp -s "$STAGE/${NAME}-before.png" "$STAGE/${NAME}-after.png"; then
        echo "  WARNING: before and after are IDENTICAL — this state does not show the change" >&2
        FAILED=1
    else
        DIFF="$(magick compare -metric AE "$STAGE/${NAME}-before.png" \
                    "$STAGE/${NAME}-after.png" null: 2>&1 || true)"
        echo "  the pair differs by ${DIFF} pixels"
    fi
fi

CROP="${HANABI_SHOT_CROP:-}"
if [ "$FAILED" = "0" ] && [ -n "$CROP" ]; then
    for label in before after; do
        magick "$STAGE/${NAME}-${label}.png" -crop "$CROP" +repage \
               -resize 200% "$STAGE/${NAME}-detail-${label}.png"
    done
fi

if [ "$FAILED" = "0" ]; then
    mkdir -p "$OUTDIR"
    for f in "$STAGE/${NAME}"-*.png; do
        cp "$f" "$OUTDIR/" && printf '  wrote docs/screenshots/review/%s\n' "$(basename "$f")"
    done
fi
exit "$FAILED"
