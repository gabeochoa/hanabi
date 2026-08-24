#!/usr/bin/env python3
"""Score how far hanabi is from Puffin, and say WHERE.

One number is not enough to work from: "8% different" does not tell you
whether the sidebar is the wrong width or the font is the wrong size. So this
reports the overall figure plus a per-region breakdown, and writes a diff
image with the changed pixels in red.

The figure is the fraction of pixels whose colour differs by more than a small
per-channel tolerance. Antialiasing noise between two different renderers is
real and uninteresting; a tolerance of 12/255 ignores it and still catches a
wrong colour, a wrong position or a wrong glyph.

  usage: compare.py <puffin.png> <hanabi.png> [--diff out.png] [--regions]
                    [--no-exclusions] [--selftest]
"""
import sys
from PIL import Image, ImageChops, ImageFilter

TOL = 12

# Puffin renders at 2x on a retina screen and is downsampled to meet hanabi's
# 1x capture. That asymmetry has a floor, and it is not small: downsampling ONE
# Puffin frame with two different filters and comparing the results against
# each other gives 2.27% overall and 10% in the text-dense session list. No
# design change can get under that on the raw metric, because it is resampling,
# not disagreement.
#
# So there are two numbers. RAW is the literal per-pixel answer. STRUCTURAL
# blurs both by 0.8px first, which forgives sub-pixel glyph edges while still
# catching a wrong colour, a wrong position or a wrong size -- on that same
# identical-source pair it reads 0.23%, so it has room to mean something.
# Quote both; a claim of parity needs the structural number to be small AND
# the raw number to have stopped falling.
STRUCT_BLUR = 0.8


# --- Declared divergences ---------------------------------------------------
#
# Some of what this script measures is not a design difference and never will
# be. hanabi paints a status strip along the bottom of its main pane and Puffin
# has no equivalent surface at all; the reference frame's open thread has no
# fixture in Puffin's mock backend, so Puffin draws a one-line "no transcript"
# placeholder where hanabi draws a real conversation; Puffin's reference came
# through the macOS window server and carries the traffic lights and the
# window's rounded corners, and hanabi's parity capture is an offscreen render
# with no window at all. Each of those is worth points that no amount of design
# work can spend, and while they sit in the total the total means "how far apart
# the two apps are, plus a constant" -- which is a number you cannot steer by.
#
# So they are declared here, by rectangle, with the reason, and subtracted from
# BOTH the numerator and the denominator: the headline becomes a fraction of
# the surface the two apps actually share. Nothing is hidden -- every entry is
# printed with what it cost on this pair, and `--no-exclusions` reproduces the
# historical figure exactly, so the change in the metric can always be told
# apart from a change in the app.
#
# The bar for adding an entry is deliberately high, and it is NOT "hanabi looks
# different here". It is: **one app draws chrome the other structurally lacks,
# or the two frames are showing different content, and no change to hanabi's
# design can close it.** A band where both draw something and the two disagree
# stays in the score, however badly it reads -- the sidebar footer is the
# example to hold onto. hanabi puts "N blocked on you" and a session count
# there and Puffin puts its version label and three glyph buttons, which is a
# real, arguable, closeable difference of what belongs at the foot of a
# sidebar. It is not declared. (The version STRING is, because v0.5.5 is not a
# thing hanabi can become; the row it sits in is not.)
#
# The rectangles are pixels in the REFERENCE frame's own coordinates, because
# that is what they were measured against; `DIVERGENCE_FRAME` pins the size
# they are true for, and a reference of any other size skips them loudly rather
# than excluding the wrong band.
DIVERGENCE_FRAME = (1180, 949)

# Four of these are properties of how the two apps were CAPTURED, not of what
# either was showing, so they are the same rectangles in every reference shot
# at this size and are shared rather than copied per entry. Verified rather
# than assumed: all four rects are byte-identical between 01_home.png and
# 02_thread.png. (The version string's rect is shared; its CONTENT is not --
# 01 reads v0.5.5 and 02 reads v0.5.6, because Puffin was rebuilt between the
# two captures. Neither is a number hanabi can become, which is the point.)
_CAPTURE_DIVERGENCES = [
    (
        "bottom status bar",
        [(283, 922, 1180, 949)],
        "hanabi paints a status strip along the bottom of the MAIN pane "
        "(not the window -- see layout_system.h); Puffin's only bottom "
        "chrome is the sidebar-width SidebarColumn.sidebarFooter, so "
        "right of the sidebar there is nothing to compare it against",
    ),
    (
        "sidebar footer version string",
        [(8, 926, 60, 944)],
        "both apps print their own version at the foot of the sidebar; "
        "the reference says v0.5.x and hanabi says v0.1.0, and no design "
        "change makes one app's version number the other's",
    ),
    (
        "titlebar traffic lights",
        [(0, 0, 72, 34)],
        "Puffin's reference came through the window server and includes "
        "the macOS close/minimise/zoom buttons; hanabi's parity capture "
        "is run_headless_screenshot, an offscreen render of the client "
        "area with no window and therefore no decoration",
    ),
    (
        "window top bevel",
        [(0, 0, 1180, 1)],
        "the reference's single top row is the macOS window's own light "
        "border -- (59..80) grey across the full width, where row 1 is "
        "already back to (28,28,40) and the left, right and bottom edges "
        "carry nothing like it. Same cause as the traffic lights and the "
        "corners, and byte-identical between 01 and 02: it is the window "
        "frame, and hanabi's offscreen capture has no window. It was left "
        "in the score by the two entries either side of it, which mask "
        "only x<72 and x>1163, so 1092 pixels of pure window decoration "
        "were being charged to the tab strip -- a third of that region",
    ),
    (
        "rounded window corners",
        [(0, 0, 16, 16), (1164, 0, 1180, 16),
         (0, 933, 16, 949), (1164, 933, 1180, 949)],
        "same cause as the traffic lights: the reference is masked to the "
        "window's corner radius and hanabi's offscreen render is square",
    ),
]

KNOWN_DIVERGENCES = {
    "01_home.png": [
        (
            "transcript viewport",
            [(283, 71, 1180, 820)],
            "the reference's open thread has no Puffin mock fixture -- "
            "MockBackend.swift:936 renders one line, 'No fixture transcript "
            "for <id> yet.', where hanabi renders a full conversation",
        ),
    ] + _CAPTURE_DIVERGENCES,
    # 02 declares the four capture divergences and NOT the transcript one.
    # That is the whole reason it was captured: its open thread has a real
    # Puffin fixture in it, so the transcript viewport is a surface the two
    # apps genuinely share and every pixel of it is scoreable. Use 02 for the
    # transcript and the composer, 01 for the sidebar and the tab bar.
    "02_thread.png": list(_CAPTURE_DIVERGENCES),
}


def load(path):
    return Image.open(path).convert("RGB")


def diff_mask(a, b, tol=TOL):
    """Per-pixel: True where the two images differ beyond tolerance."""
    d = ImageChops.difference(a, b)
    # Max of the three channel deltas, thresholded.
    return d.convert("L").point(lambda v: 255 if v > tol else 0)


def pct(mask):
    hist = mask.histogram()
    changed = hist[255]
    return 100.0 * changed / (mask.width * mask.height)


def divergences_for(ref_path, size):
    """The declared list for this reference, or [] with a reason printed.

    Matched on the reference's BASENAME rather than its full path, so the same
    table serves a checkout, a worktree and the copy under /tmp that an agent
    shot into. Matched on its SIZE as well, because the rectangles are pixels:
    a reference that has been re-cropped or re-shot at another size would have
    them land on whatever happens to be at those coordinates now, which is a
    worse failure than not excluding at all -- it would be silent.
    """
    name = ref_path.replace("\\", "/").rsplit("/", 1)[-1]
    entries = KNOWN_DIVERGENCES.get(name)
    if not entries:
        return []
    if tuple(size) != DIVERGENCE_FRAME:
        w, h = size
        dw, dh = DIVERGENCE_FRAME
        print(f"NOTE: {name} is {w}x{h}, but its declared divergences were "
              f"measured on {dw}x{dh} -- skipping all of them.")
        return []
    return entries


def rect_mask(size, entries):
    """One mask over every declared rectangle; overlaps count once.

    A union rather than a sum: the traffic lights and the top-left corner
    overlap, and adding their costs would over-report what the exclusions
    remove -- by exactly the pixels that are in both.
    """
    m = Image.new("L", size, 0)
    for _, rects, _ in entries:
        for r in rects:
            m.paste(255, r)
    return m


def masked_pct(diff, excl):
    """The diff as a fraction of the surface left after the exclusions.

    Out of BOTH halves of the fraction. Dropping the excluded pixels from the
    numerator alone would keep the denominator claiming credit for surface the
    comparison has stopped looking at, which flatters every number by the same
    invisible amount.
    """
    kept = ImageChops.subtract(diff, excl)
    total = diff.width * diff.height - excl.histogram()[255]
    if total <= 0:
        return 0.0, 0
    n = kept.histogram()[255]
    return 100.0 * n / total, n


def report_divergences(entries, excl, smask, mask, frame_px):
    """Print every declared rectangle and what it actually cost on THIS pair.

    The cost is printed per entry, in points of the whole frame, for both
    masks. That is the check on the table itself: an entry that reads 0.000 is
    either a divergence somebody has since closed or a rectangle that has
    drifted off the thing it was drawn around, and either way it is now
    excluding surface for no reason. It says so rather than leaving the reader
    to notice.
    """
    print()
    print(f"DECLARED DIVERGENCES  ({len(entries)} entries, "
          f"{100.0 * excl.histogram()[255] / frame_px:.1f}% of the frame's area)")
    for name, rects, reason in entries:
        one = Image.new("L", smask.size, 0)
        for r in rects:
            one.paste(255, r)
        s = ImageChops.multiply(smask, one).histogram()[255]
        r_ = ImageChops.multiply(mask, one).histogram()[255]
        sp, rp = 100.0 * s / frame_px, 100.0 * r_ / frame_px
        flag = "   <-- STALE? excludes nothing" if s == 0 and r_ == 0 else ""
        print(f"  {name:<24} {sp:5.3f} struct / {rp:5.3f} raw pts{flag}")
        print(f"  {'':24} {reason}")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--selftest" in sys.argv:
        return selftest()
    if len(args) < 2:
        print(__doc__)
        return 2
    ap, bp = args[0], args[1]
    a, b = load(ap), load(bp)

    if a.size != b.size:
        print(f"SIZE MISMATCH  puffin={a.size}  hanabi={b.size}")
        print("  (resizing hanabi to compare anyway -- fix the window size)")
        b = b.resize(a.size)

    mask = diff_mask(a, b)
    overall = pct(mask)
    sa = a.filter(ImageFilter.GaussianBlur(STRUCT_BLUR))
    sb = b.filter(ImageFilter.GaussianBlur(STRUCT_BLUR))
    smask = diff_mask(sa, sb)
    struct = pct(smask)
    print(f"WHOLE FRAME  raw {overall:.2f}%   structural {struct:.2f}%")
    print("  (floors: raw ~2.3%, structural ~0.2% -- retina downsample)")

    entries = [] if "--no-exclusions" in sys.argv else divergences_for(ap, a.size)
    excl = rect_mask(a.size, entries)
    if entries:
        report_divergences(entries, excl, smask, mask, a.width * a.height)
        s_shared, s_kept = masked_pct(smask, excl)
        r_shared, r_kept = masked_pct(mask, excl)
        frame_px = a.width * a.height
        s_gone = struct - 100.0 * s_kept / frame_px
        r_gone = overall - 100.0 * r_kept / frame_px
        print()
        print(f"  declared cost    raw -{r_gone:.2f}     structural -{s_gone:.2f} "
              f"points of the frame -- unspendable, whatever hanabi does")
        print(f"SHARED SURFACES  raw {r_shared:.2f}%   "
              f"structural {s_shared:.2f}%   <-- drive this")
        # Said out loud, because the direction surprises people the first time.
        # These are RATES over what is left, and the declared surface is mostly
        # empty in BOTH frames -- 700k pixels of black agreeing with black. Take
        # it out and the rate over the remainder goes UP even though the
        # unspendable points went DOWN. That is the metric getting honest, not
        # the app getting worse: the old figure was being held down by a region
        # that matched by accident and could never be worked on.
        if s_shared > struct:
            print("  (the rate rises because the declared surface was mostly "
                  "empty in both frames;")
            print("   it was diluting the score, not passing it)")
    elif "--no-exclusions" in sys.argv:
        print("  (--no-exclusions: nothing declared was removed)")

    if "--regions" in sys.argv:
        # Regions are fractions of the frame, not hardcoded pixel boxes, so
        # they still mean something when the window size changes.
        W, H = a.size
        regions = {
            "sidebar":      (0, 0, int(W * 0.24), H),
            "  views":      (0, 0, int(W * 0.24), int(H * 0.28)),
            "  search":     (0, int(H * 0.28), int(W * 0.24), int(H * 0.33)),
            "  list":       (0, int(H * 0.33), int(W * 0.24), int(H * 0.96)),
            "  footer":     (0, int(H * 0.96), int(W * 0.24), H),
            "tabbar":       (int(W * 0.24), 0, W, int(H * 0.075)),
            "main":         (int(W * 0.24), int(H * 0.075), W, H),
        }
        print()
        # The leading column is STRUCTURAL, because that is the number the two
        # lines above tell you to drive. It used to be RAW, which meant every
        # per-region figure quoted in this workstream carried the downsample
        # floor the header says no design change can get under -- and the floor
        # is not spread evenly: it lands almost entirely on text, so the
        # text-heavy regions read several points worse than they are.
        #
        # The figures are over the region's SHARED surface, on the same
        # arithmetic as the headline: a region that is mostly declared
        # divergence would otherwise keep reporting a number nobody can act on,
        # and `main` is exactly that region. The trailing note says how much of
        # the region was declared, so a small figure over a nearly-excluded
        # region can never read as parity.
        print(f"  {'region':<12} {'STRUCT':>7}  {'RAW':>7}")
        for name, box in regions.items():
            e = excl.crop(box)
            gone = e.histogram()[255]
            area = e.width * e.height
            sub = ImageChops.subtract(smask.crop(box), e)
            rsub = ImageChops.subtract(mask.crop(box), e)
            left = max(1, area - gone)
            p = 100.0 * sub.histogram()[255] / left
            rp = 100.0 * rsub.histogram()[255] / left
            bar = "#" * int(p / 2)
            share = 100.0 * gone / area
            note = f"  ({share:.1f}% declared)" if share >= 0.1 else ""
            print(f"  {name:<12} {p:6.2f}%  {rp:6.2f}%  {bar}{note}")

    if "--diff" in sys.argv:
        out = sys.argv[sys.argv.index("--diff") + 1]
        red = Image.new("RGB", a.size, (255, 0, 0))
        composed = b.copy()
        composed.paste(red, (0, 0), mask)
        # Declared surface is greyed rather than dropped: an exclusion you
        # cannot see on the diff image is an exclusion nobody audits, and the
        # first question anyone asks of one of these rectangles is whether it
        # is drawn around the right thing.
        if entries:
            grey = Image.new("RGB", a.size, (60, 60, 60))
            composed.paste(grey, (0, 0), excl.point(lambda v: 110 if v else 0))
        composed.save(out)
        print(f"\nwrote {out}")

    return 0


def selftest():
    """Prove the exclusion arithmetic on frames whose answer is known.

    Synthetic, not the reference pair: the point is to pin the behaviour, and a
    test that reads its expected numbers off the same images the tool is
    measuring cannot fail for the reason it exists. Two 100x100 frames, one
    block of difference inside a declared rectangle and one outside it, and the
    three things that have to be true of any honest exclusion:

      1. the whole-frame number counts both blocks;
      2. the shared number counts the outside block and not the inside one,
         over the reduced denominator -- not the full one, which is the error
         that would flatter every score by the size of the exclusion;
      3. a change OUTSIDE the rectangle still moves the shared number. An
         exclusion that swallows a difference it was not drawn around is the
         failure mode worth a test, because it is the silent one.
    """
    size = (100, 100)
    base = Image.new("RGB", size, (0, 0, 0))
    other = base.copy()
    other.paste((255, 255, 255), (0, 0, 10, 10))    # inside the rect: 100 px
    other.paste((255, 255, 255), (50, 50, 60, 60))  # outside it:      100 px
    entries = [("t", [(0, 0, 20, 20)], "test rect")]
    excl = rect_mask(size, entries)

    d = diff_mask(base, other)
    fails = []

    def check(name, got, want, eps=1e-9):
        if abs(got - want) > eps:
            fails.append(f"{name}: got {got!r}, want {want!r}")

    check("whole frame", pct(d), 200 / 10000 * 100)
    shared, n = masked_pct(d, excl)
    check("shared count", n, 100)
    # 100 differing px over 10000 - 400 excluded, NOT over 10000.
    check("shared pct", shared, 100 / 9600 * 100)

    moved = other.copy()
    moved.paste((255, 255, 255), (70, 70, 80, 80))  # another 100 px outside
    shared2, _ = masked_pct(diff_mask(base, moved), excl)
    if not shared2 > shared:
        fails.append(f"a change outside the rect did not move the shared "
                     f"number: {shared2} vs {shared}")

    # And the rectangles the tool ships with must be inside the frame they
    # claim, or they silently clip.
    W, H = DIVERGENCE_FRAME
    for ref, decls in KNOWN_DIVERGENCES.items():
        for name, rects, reason in decls:
            if not reason.strip():
                fails.append(f"{ref}/{name}: declared with no reason")
            for (x0, y0, x1, y1) in rects:
                if not (0 <= x0 < x1 <= W and 0 <= y0 < y1 <= H):
                    fails.append(f"{ref}/{name}: {(x0, y0, x1, y1)} is not "
                                 f"inside {W}x{H}")

    for f in fails:
        print(f"FAIL  {f}")
    print("selftest: " + ("FAIL" if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
