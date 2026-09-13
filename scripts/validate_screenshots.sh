#!/usr/bin/env bash
# The screenshot verbs the makefile owned, in one place:
#   (default)        validate-screenshots: capture every baselined scene, compare
#   --fast           the 10-scene subset `zig build test` runs
#   --update         update-baselines: recapture (SHOT_FILTER=<regex> narrows;
#                    otherwise every baselined scene plus every declared scene
#                    that has no baseline yet) and copy into the baselines dir
#   --determinism    capture 01_home_dark twice, require byte-identical output
# The compare thresholds live in scripts/compare_screenshots.py, unchanged.
set -u
cd "$(dirname "$0")/.."
BASELINES=docs/screenshots/baselines
CURRENT=output/screenshots/current
FAST_CURRENT=output/screenshots/fast
DETERMINISM=output/screenshots/determinism
DECLARED=output/screenshots/declared.txt
FAILURES=test-failures
FAST="01_home_dark 02_home_light 03_transcript_dark 14_sidebar_folded_dark 14b_subagent_sidebar_dark 15_settings_dark 18_auth_dark 22_split_view_dark 28_composer_focus_dark 63_ask_unanswerable_backend_dark"

PY=""
for p in python3 /usr/bin/python3 /opt/homebrew/bin/python3; do
    if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import PIL' >/dev/null 2>&1; then PY="$p"; break; fi
done
PY="${PY:-python3}"

filter_of() {  # names on stdin -> ^(a|b|c)$
    grep -v '^$' | sort -u | paste -sd'|' - | sed 's/^/^(/; s/$/)$/'
}
baselined() { ls "$BASELINES"/*.png 2>/dev/null | xargs -n1 basename 2>/dev/null | sed 's/\.png$//'; }
capture() {  # dir filter
    rm -rf "$1"; mkdir -p "$1"
    HANABI_SCREENS_OUT="$(cd "$1" && pwd)" HANABI_SCREENS_FILTER="$2" bash scripts/screens.sh
}

mode="${1:-}"
case "$mode" in
    --fast)
        n=$(echo "$FAST" | wc -w | tr -d ' ')
        echo "=== screenshots: the $n-screen subset ==="
        capture "$FAST_CURRENT" "$(printf '%s\n' $FAST | filter_of)" || exit 1
        mkdir -p "$FAILURES"
        exec "$PY" scripts/compare_screenshots.py \
            --baselines "$BASELINES" --current "$FAST_CURRENT" \
            --only "$(echo "$FAST" | tr ' ' ',')" \
            --failures-dir "$FAILURES" --json "$FAILURES/summary-fast.json"
        ;;
    --update)
        echo "=== recapturing baselines ==="
        if [ -n "${SHOT_FILTER:-}" ]; then
            filter="$SHOT_FILTER"
        else
            new=$(bash scripts/screens.sh --list 2>/dev/null | \
                "$PY" scripts/compare_screenshots.py --baselines "$BASELINES" --declared - --print-new 2>/dev/null)
            filter="$( { baselined; printf '%s\n' $new; } | filter_of)"
        fi
        capture "$CURRENT" "$filter" || exit 1
        cp "$CURRENT"/*.png "$BASELINES"/
        echo
        git diff --stat "$BASELINES" || true
        git status --short "$BASELINES" | grep '^??' || true
        echo "Baselines updated. Review the PNGs before committing."
        ;;
    --determinism)
        echo "=== screenshot determinism: capturing 01_home_dark twice ==="
        rm -rf "$DETERMINISM"; mkdir -p "$DETERMINISM/a" "$DETERMINISM/b"
        HANABI_SCREENS_OUT="$(pwd)/$DETERMINISM/a" HANABI_SCREENS_FILTER='^01_home_dark$' bash scripts/screens.sh || exit 1
        HANABI_SCREENS_OUT="$(pwd)/$DETERMINISM/b" HANABI_SCREENS_FILTER='^01_home_dark$' bash scripts/screens.sh || exit 1
        A=$DETERMINISM/a/01_home_dark.png; B=$DETERMINISM/b/01_home_dark.png
        for f in $A $B; do [ -s "$f" ] || { echo "FAIL: $f missing or empty" >&2; exit 1; }; done
        SA=$(wc -c < $A | tr -d ' '); SB=$(wc -c < $B | tr -d ' ')
        MA=$(md5 -q $A 2>/dev/null || md5sum $A | cut -d' ' -f1)
        MB=$(md5 -q $B 2>/dev/null || md5sum $B | cut -d' ' -f1)
        echo "  capture A: $SA bytes  md5 $MA"
        echo "  capture B: $SB bytes  md5 $MB"
        if [ "$SA" = "$SB" ] && [ "$MA" = "$MB" ]; then
            echo "PASS: two captures of 01_home_dark are byte-identical"
        else
            echo "FAIL: captures differ -- the render is not deterministic, so" >&2
            echo "      baselines cannot be trusted. Check for absolute-epoch mock" >&2
            echo "      seeding (src/api/mock_client.h) or an unfrozen animation." >&2
            exit 1
        fi
        ;;
    "")
        echo "=== capturing current screens for comparison ==="
        rm -rf "$FAILURES"
        capture "$CURRENT" "$(baselined | filter_of)" || exit 1
        bash scripts/screens.sh --list > "$DECLARED"
        echo
        exec "$PY" scripts/compare_screenshots.py \
            --baselines "$BASELINES" --current "$CURRENT" --declared "$DECLARED" \
            --failures-dir "$FAILURES" --json "$FAILURES/summary.json"
        ;;
    *) echo "validate_screenshots.sh: unknown mode $mode" >&2; exit 2 ;;
esac
