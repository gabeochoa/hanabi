#!/usr/bin/env bash
# Per-commit truth check for the numbers a Test Plan claims.
#
# A Test Plan that quotes stack-tip totals on a commit whose own tree cannot
# produce them is a false claim, and reading eight messages by eye is how one
# got through. This counts, from each commit's OWN tree, the three figures the
# messages quote -- scripted UI scripts, declared screens, committed baselines
# -- and prints them beside what that commit's message says.
#
# Counting only. It runs no suite and builds nothing, so it is cheap enough to
# re-run after every amend.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2

BASE="${1:-origin/main}"
FAIL=0

printf '%-9s  %-5s %-5s  %-5s %-5s  %-5s %-5s  %s\n' \
    COMMIT ui_n ui_say scr_n scr_say bl_n bl_say TITLE

for c in $(git rev-list --reverse "$BASE"..HEAD); do
    short="$(git rev-parse --short "$c")"
    title="$(git log -1 --format=%s "$c")"
    # Flattened to ONE line before matching. A wrapped message puts
    # "make validate-screenshots" and its "(154/154)" on separate lines, and a
    # line-at-a-time grep silently reports "no claim" for a claim that is
    # plainly there -- which is the same class of miss this script exists to
    # catch.
    msg="$(git log -1 --format=%B "$c" | tr '\n' ' ')"

    ui_n=$(git ls-tree -r --name-only "$c" -- tests/ui | grep -c '\.e2e$')
    bl_n=$(git ls-tree -r --name-only "$c" -- docs/screenshots/baselines \
           | grep -c '\.png$')
    scr_n=$(git show "$c":scripts/screens.sh 2>/dev/null \
            | grep -cE '^capture(_sized)? ')

    # What the message claims: "<n>/<n> scripted UI", "(<n>/<n>)" after
    # validate-screenshots, and "<n> passed" from a make uitest line.
    ui_say=$(printf '%s' "$msg" \
        | grep -oE '[0-9]+/[0-9]+ scripted UI' | grep -oE '^[0-9]+' | head -1)
    [ -z "$ui_say" ] && ui_say=$(printf '%s' "$msg" \
        | grep -oE 'uitest: [0-9]+ passed' | grep -oE '[0-9]+' | head -1)
    scr_say=$(printf '%s' "$msg" \
        | grep -oE 'validate-screenshots[^0-9]{0,40}\(?([0-9]+)/[0-9]+' \
        | grep -oE '[0-9]+/[0-9]+' | grep -oE '^[0-9]+' | head -1)
    [ -z "$scr_say" ] && scr_say=$(printf '%s' "$msg" \
        | grep -oE '[0-9]+/[0-9]+ screens within threshold' \
        | grep -oE '^[0-9]+' | head -1)
    bl_say="$scr_say"

    printf '%-9s  %-5s %-5s  %-5s %-5s  %-5s %-5s  %s\n' \
        "$short" "$ui_n" "${ui_say:--}" "$scr_n" "${scr_say:--}" \
        "$bl_n" "${bl_say:--}" "$title"

    [ -n "$ui_say" ] && [ "$ui_say" != "$ui_n" ] && {
        echo "  MISMATCH $short: says $ui_say scripted UI, tree has $ui_n"
        FAIL=1
    }
    [ -n "$scr_say" ] && [ "$scr_say" != "$scr_n" ] && {
        echo "  MISMATCH $short: says $scr_say screens, tree declares $scr_n"
        FAIL=1
    }
    [ -n "$bl_say" ] && [ "$bl_say" != "$bl_n" ] && {
        echo "  MISMATCH $short: says $bl_say screens, tree has $bl_n baselines"
        FAIL=1
    }
done

echo
if [ "$FAIL" -eq 0 ]; then
    echo "PASS: every quoted total matches the commit's own tree."
else
    echo "FAIL: a Test Plan quotes a total its own tree cannot produce."
fi
exit "$FAIL"
