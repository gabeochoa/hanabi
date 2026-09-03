#!/usr/bin/env bash
# scripts/bounds_gate.sh — the CONTAINMENT gate. Runs inside `make test`.
#
# WHAT IT IS FOR. The report is one sentence: "many buttons are going outside
# the bounds." Nothing in this repo could have caught one. `make test` reads
# text and named geometry; `compare.py` scores a picture against a reference
# and cannot tell a button 59px past its row from a button that is meant to be
# there; and afterhours' own `assert_no_overflow` measures every element
# against the VIEWPORT, so a widget can escape its parent by any amount at all
# and still pass as long as it lands inside the window (see afterhours_gaps.md
# — the parent-relative question is not one the library asks).
#
# Containment is a relationship between two rects that both exist after
# autolayout, so it can be read off the tree exactly rather than looked for in
# a screenshot. `HANABI_BOUNDS_AUDIT=1` (src/util/bounds_audit.h) prints every
# flow-positioned element whose rect escapes its parent's content box; this
# gate runs the app over the states a person actually reaches and fails on
# anything not in the baseline.
#
# WHY THERE IS A BASELINE RATHER THAN A CLEAN SWEEP — the same argument
# scripts/check_label_padding.py makes, and for the same reason. The survivors
# are eleven, they are 1.0–1.5px, they are all VERTICAL, and they are all a
# label box a few pixels taller than the row it sits in, centred, so it hangs
# equally off the top and the bottom and nothing is visible. Wherever one is,
# somebody has already tuned the geometry around it against a frozen pixel
# reference: making the six view rows' content box 22 tall to fit their 22px
# label moves every row in the sidebar's Views section. So the existing set is
# frozen and this fails only on a NEW one. The point is to stop the twelfth.
#
#   scripts/bounds_gate.sh            fail on anything not in the baseline
#   scripts/bounds_gate.sh --update   rewrite the baseline (say why in the
#                                     commit message)
#
# The baseline records "<element> in <parent>" and the direction, NOT the
# number of pixels: a design change that moves a known overflow from 1.5 to 2.0
# is not a new defect, and a gate that fails on it teaches people to run
# --update. A NEW pair, or a known pair escaping on a new SIDE, fails.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="$ROOT/output/hanabi.exe"
BASELINE="$ROOT/scripts/bounds_baseline.txt"

if [ ! -x "$EXE" ]; then
    echo "bounds-gate: ERROR: $EXE not found. Build it with 'make'." >&2
    exit 2
fi

# The states a person reaches, and the ones that carry the widgets whose size
# changes with their content — which is where an overflow comes from. Each is
# one headless capture at the parity window.
STATES=(
    ""
    "HANABI_VIEW=home"
    "HANABI_VIEW=blocked"
    "HANABI_VIEW=review"
    "HANABI_VIEW=starred"
    "HANABI_VIEW=archived"
    "HANABI_OPEN=r9"
    "HANABI_THINK_DEMO=1"
    "HANABI_STREAM_DEMO=1"
    "HANABI_EXPAND=1"
    "HANABI_BIG_TRANSCRIPT=1 HANABI_OPEN=rbig"
    "HANABI_TEST_OVERLAY=settings"
    "HANABI_ASK_DEMO=1"
    "HANABI_ASK_DEMO=big"
    "HANABI_ASK_DEMO=approval"
    "HANABI_ASK_DEMO=longapproval"
    "HANABI_ASK_DEMO=big HANABI_ATTACH_DEMO=/tmp/none.png"
)

WORK="$(mktemp -d /tmp/hanabi_bounds.XXXXXX)"
trap 'rm -rf "$WORK"; pkill -9 -f "^$EXE" >/dev/null 2>&1' EXIT

FOUND="$WORK/found.txt"
: > "$FOUND"

for st in "${STATES[@]}"; do
    H="$WORK/home"
    rm -rf "$H"
    mkdir -p "$H/Library/Application Support/hanabi"
    cat > "$H/Library/Application Support/hanabi/settings.json" <<JSON
{"window_width":1180,"window_height":949,"open_tabs":["t9","t2"],"active_tab":"t2","pinned_tabs":["t9","t2"],"theme":"dark"}
JSON
    # shellcheck disable=SC2086
    env HOME="$H" HANABI_WIN_W=1180 HANABI_WIN_H=949 \
        HANABI_BACKEND=mock HANABI_CONFIG="/tmp/none_bounds_$$" \
        HANABI_BOUNDS_AUDIT=1 $st \
        "$EXE" --screenshot "$WORK/shot.png" > "$WORK/log.txt" 2>&1 &
    pid=$!
    for _ in $(seq 1 40); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    pkill -9 -f "^$EXE" >/dev/null 2>&1
    # "  <name>   in <parent>   over by left=.. right=.. ..  [rects]" ->
    # "<name> in <parent> left right", the identity without the magnitudes.
    /usr/bin/python3 - "$WORK/log.txt" >> "$FOUND" <<'PY'
import re, sys
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"\[bounds\]\s+(\S+)\s+in\s+(\S+)\s+over by (.*?)(?:\s+\[|$)", line)
    if not m:
        continue
    sides = " ".join(sorted(s for s in ("left", "right", "top", "bottom")
                            if s + "=" in m.group(3)))
    print(f"{m.group(1)} in {m.group(2)} {sides}")
PY
done

sort -u "$FOUND" -o "$FOUND"

if [ "${1:-}" = "--update" ]; then
    # Keep the baseline's leading comment block: it is the argument for the
    # freeze, and an --update that silently deleted it would leave the next
    # reader a bare list with no reason attached to it.
    { [ -f "$BASELINE" ] && sed -n '/^#/p;/^#/!q' "$BASELINE"; cat "$FOUND"; } \
        > "$BASELINE.new" && mv "$BASELINE.new" "$BASELINE"
    echo "bounds-gate: baseline rewritten, $(wc -l < "$BASELINE" | tr -d ' ') entries."
    exit 0
fi

if [ ! -f "$BASELINE" ]; then
    echo "bounds-gate: FAIL: no baseline at $BASELINE (run --update)." >&2
    exit 1
fi

NEW="$(comm -23 "$FOUND" <(grep -v '^#' "$BASELINE" | grep -v '^$' | sort -u))"
if [ -n "$NEW" ]; then
    echo "bounds-gate: FAIL: element(s) escaping their parent's content box" \
         "that the baseline does not know about:" >&2
    echo "$NEW" | sed 's/^/  /' >&2
    echo "" >&2
    echo "  A flow child outside its parent draws over the parent's padding," >&2
    echo "  its border, or its siblings. Size the parent for what the child" >&2
    echo "  actually becomes, not for its smallest state. If the overflow is" >&2
    echo "  deliberate, the element should be absolutely positioned (this" >&2
    echo "  audit skips those on purpose) -- not left in flow and forgiven" >&2
    echo "  here." >&2
    exit 1
fi

GONE="$(comm -13 "$FOUND" <(grep -v '^#' "$BASELINE" | grep -v '^$' | sort -u))"
if [ -n "$GONE" ]; then
    echo "bounds-gate: PASS ($(wc -l < "$FOUND" | tr -d ' ') known)," \
         "and $(echo "$GONE" | wc -l | tr -d ' ') baseline entr(y|ies) no" \
         "longer overflow -- run --update to retire them:"
    echo "$GONE" | sed 's/^/  /'
    exit 0
fi

echo "bounds-gate: PASS ($(wc -l < "$FOUND" | tr -d ' ') known, 0 new)."
