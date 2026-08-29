#!/usr/bin/env bash
# ===========================================================================
# scripts/composer_chrome_gate.sh -- the composer's INTERIOR and its FOCUS
# EDGE, in pixels.
#
# Two properties of the reply field that every other gate is blind to, and
# that no scripted-UI assertion can reach: `assert_ui` reads x/y/w/h/text and
# nothing about colour, and the screenshot baselines under
# docs/screenshots/baselines are stale enough that 28 of 30 screens differ at
# ~100% -- so "the composer still looks right" has been an opinion.
#
#   1. THE INTERIOR IS THE WINDOW COLOUR. Puffin's input is an outline on the
#      window plane, not a filled pill: the 1px border is the only thing that
#      says where the field is (main_pane_system.h, composer_input_wrap). A
#      widget that paints its own Theme::Usage::Secondary fill over the field
#      rect breaks that, and it breaks it silently -- the layout does not move
#      and every geometric assertion still passes.
#
#   2. A FOCUSED FIELD HAS A COLOURED EDGE. One accent row on the top of the
#      field and one on the bottom, and neither of them when it is not
#      focused. This is the field's OWN focused border, which is not the
#      app's :focus-visible ring (src/ui/focus_visible.h) and is not gated on
#      arming -- the caret and this edge are what say "typing goes here", and
#      the ring is what says "the keyboard walked here".
#
# Both are captured at 1100x760 with the standard three-tab dark fixture, the
# same one scripts/screens.sh uses, in an isolated HOME with the mock backend.
# The field is LOCATED rather than hardcoded: the wrap's own 1px border rows
# are found by scanning a column upward from the window floor, so a composer
# that moves vertically still gets measured instead of silently passing.
# ===========================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="$ROOT/output/hanabi.exe"

if [ ! -x "$EXE" ]; then
    echo "ERROR: $EXE not found. Build it with 'make'." >&2
    exit 2
fi

OUTDIR="$(mktemp -d /tmp/hanabi_chrome_gate.XXXXXX)"
trap 'rm -rf "$OUTDIR" /tmp/hanabi_chrome_home.*; pkill -9 -f "^$EXE" >/dev/null 2>&1' EXIT

# A FRESH HOME per capture, not one shared by both. The app persists settings
# on exit, so a second run out of the same HOME reads back what the first one
# wrote -- which moved the transcript and made the two frames differ hundreds
# of rows above the composer, in a gate whose whole claim is that focus
# repaints nothing else.
shoot() {  # shoot <out.png> [extra env assignments...]
    local out="$1"; shift
    local h
    h="$(mktemp -d /tmp/hanabi_chrome_home.XXXXXX)"
    mkdir -p "$h/Library/Application Support/hanabi"
    cat > "$h/Library/Application Support/hanabi/settings.json" <<'JSON'
{"window_width":1100,"window_height":760,"open_tabs":["t2","t6","t1"],"active_tab":"t2","theme":"dark"}
JSON
    rm -f "$out"
    env HOME="$h" HANABI_WIN_W=1100 HANABI_WIN_H=760 HANABI_BACKEND=mock \
        HANABI_CONFIG="/tmp/none_chrome_$$" "$@" \
        "$EXE" --screenshot "$out" > "$OUTDIR/shot.log" 2>&1 &
    local pid=$!
    local i
    for i in $(seq 1 40); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    pkill -9 -f "^$EXE" >/dev/null 2>&1
    [ -s "$out" ] || { echo "FAIL: no capture at $out" >&2
                       tail -20 "$OUTDIR/shot.log" >&2; return 1; }
    return 0
}

shoot "$OUTDIR/rest.png"  env                              || exit 1
shoot "$OUTDIR/focus.png" env HANABI_TEST_FOCUS_COMPOSER=1 || exit 1

/usr/bin/python3 - "$OUTDIR/rest.png" "$OUTDIR/focus.png" <<'PY'
import sys
from PIL import Image

rest_p, focus_p = sys.argv[1], sys.argv[2]

# The three colours this gate is about, from src/ui/theme.h (dark) and
# preload.cpp's ThemeDefaults Accent.
WINDOW = (23, 23, 35)
WRAP_BORDER = (41, 41, 52)
ACCENT = (0, 122, 204)

# Columns to sample across the field's own width. All well clear of the
# placeholder's ink (which starts at the field's left inset) and of the send
# control on the right.
COLS = (600, 700, 800, 900, 1000)
PROBE = 600

failures = []


def find_wrap(px, h, x):
    """The composer wrap's two border rows, scanning up from the floor."""
    rows = [y for y in range(h - 1, h - 140, -1) if px[x, y] == WRAP_BORDER]
    if len(rows) < 2:
        return None
    return min(rows), max(rows)


def load(path):
    im = Image.open(path).convert("RGB")
    return im, im.load(), im.size


rest, rpx, (W, H) = load(rest_p)
focus, fpx, _ = load(focus_p)

wrap = find_wrap(rpx, H, PROBE)
if wrap is None:
    print("FAIL  could not find the composer wrap's border rows at x=%d" % PROBE)
    print("      (nothing below is measurable; this is a broken capture or a")
    print("       composer that no longer draws its 1px outline)")
    sys.exit(1)
top, bot = wrap
print("composer wrap  y=%d..%d  (border %s)" % (top, bot, WRAP_BORDER))

# --- 1. the interior is the window colour, at rest ------------------------
#
# Every interior row of the wrap, at every sample column, on the RESTING
# capture. A forced Secondary fill shows up as a solid block of one wrong
# colour over most of these.
#
# At rest and not focused, because a FOCUSED composer legitimately draws two
# more things into this band -- the field's own accent edge and, since the
# capture hook learned to arm :focus-visible, the app's focus ring outside it.
# The focused case is checked in arm 3 over the rows BETWEEN the accent edges,
# which is the same question with the chrome excluded.
wrong = {}
for y in range(top + 1, bot):
    for x in COLS:
        c = rpx[x, y]
        if c == WINDOW:
            continue
        wrong[c] = wrong.get(c, 0) + 1
total = (bot - top - 1) * len(COLS)
bad = sum(wrong.values())
if bad:
    worst = sorted(wrong.items(), key=lambda kv: -kv[1])[:3]
    failures.append(
        "interior fill (rest): %d/%d sampled pixels are not the window "
        "colour %s -- %s"
        % (bad, total, WINDOW,
           ", ".join("%s x%d" % (c, n) for c, n in worst)))
else:
    print("OK    interior (rest): %d sampled pixels, all %s" % (total, WINDOW))

# --- 2. the wrap's outline survives the field ------------------------------
#
# The field is percent(1.0f) of the wrap, and the wrap's 1px border draws ON
# its box edge, so a field that paints an opaque background of its own paints
# over that border wherever the two meet. This is what a forced
# Theme::Usage::Secondary fill costs on THIS composer -- not the interior
# colour (the composer already points Secondary at the strip colour), but the
# outline: both rounded top corners' arcs and the whole right-hand border
# column down the field's rows. The box stops being a rounded outlined box.
#
# Every row inside the wrap must still carry at least a left and a right
# border pixel. A row with fewer has had its edge eaten.
mid_cols = [x for x in range(W) if rpx[x, top] == WRAP_BORDER]
if len(mid_cols) < 2:
    failures.append(
        "the composer wrap's top border row y=%d has no border pixels -- "
        "there is no outline left to measure" % top)
else:
    # The top row is inset by the corner radius at each end, so the box's own
    # left and right columns sit a few pixels outside it. Widen by more than
    # any plausible radius rather than by the exact one.
    left, right = min(mid_cols) - 10, max(mid_cols) + 10
    print("composer wrap  x=%d..%d (searched)" % (left, right))
    thin = []
    for y in range(top + 1, bot):
        n = sum(1 for x in range(left, right + 1)
                if rpx[x, y] == WRAP_BORDER)
        if n < 2:
            thin.append((y, n))
    if thin:
        failures.append(
            "the wrap's outline is painted over on %d of its %d interior "
            "rows (first: y=%d has %d border pixels, wants >= 2) -- a field "
            "with an opaque background of its own is covering the box edge "
            "it sits flush against (afterhours_gaps.md #262)"
            % (len(thin), bot - top - 1, thin[0][0], thin[0][1]))
    else:
        print("OK    wrap outline intact on all %d interior rows"
              % (bot - top - 1))

# --- 3. the focus edge -----------------------------------------------------
#
# Focus belongs to the outlined input box, not to the shorter text-area child
# inside it. A matched accent pair must replace the wrap's top and bottom
# borders; the interior remains the window colour. The global keyboard ring is
# suppressed while this text field owns focus because the caret and focused
# edge already identify the destination.
def accent_rows(px):
    out = []
    for y in range(top, bot + 1):
        if all(px[x, y] == ACCENT for x in COLS):
            out.append(y)
    return out

rest_rows = accent_rows(rpx)
focus_rows = accent_rows(fpx)

if rest_rows:
    failures.append(
        "an UNFOCUSED composer is drawing accent rows at y=%s -- the focused "
        "edge must not be on when nothing is focused" % (rest_rows,))
else:
    print("OK    rest: no accent rows on the wrap")

if len(focus_rows) < 2:
    failures.append(
        "a FOCUSED composer draws %d accent row(s) %s on the wrap; the focused "
        "edge needs both a top and bottom" % (len(focus_rows), focus_rows))
elif focus_rows[0] != top or focus_rows[-1] != bot:
    failures.append(
        "the focused edge covers y=%s instead of the full wrap %d..%d"
        % (focus_rows, top, bot))
else:
    print("OK    focus: accent edge spans the full wrap y=%d..%d" % (top, bot))
    fwrong = {}
    for y in range(top + 1, bot):
        for x in COLS:
            c = fpx[x, y]
            if c == WINDOW:
                continue
            fwrong[c] = fwrong.get(c, 0) + 1
    if fwrong:
        worst = sorted(fwrong.items(), key=lambda kv: -kv[1])[:3]
        failures.append(
            "interior fill/ring (focused): %s inside the wrap"
            % ", ".join("%s x%d" % (c, n) for c, n in worst))
    else:
        print("OK    interior (focus): all %s between the accent edges"
              % (WINDOW,))

# --- 4. focus changes NOTHING else ----------------------------------------
#
# Outside the composer band the two frames must be byte-identical: a focus
# state that repaints the transcript is a focus state that will show up in
# every screenshot comparison the app has.
from PIL import ImageChops
# The tolerance is the wrap plus a few rows: the app's focus ring is drawn
# OUTSIDE the field and, at one row, outside the wrap's own top border.
BAND_SLOP = 4
d = ImageChops.difference(rest, focus).getbbox()
if d is not None and (d[1] < top - BAND_SLOP or d[3] > bot + 1 + BAND_SLOP):
    failures.append(
        "focusing the composer changed pixels outside the composer band: "
        "diff bbox %s, composer y=%d..%d (+/-%d)" % (d, top, bot, BAND_SLOP))
else:
    print("OK    focus repaints only the composer band (bbox %s)" % (d,))

print()
if failures:
    print("FAIL  composer chrome gate")
    for f in failures:
        print("  - " + f)
    sys.exit(1)
print("PASS  composer chrome gate")
PY
rc=$?
exit $rc
