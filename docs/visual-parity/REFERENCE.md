# The frozen reference set

`ref/*.png` are the ONLY reference. Do not re-shoot Puffin: five agents share
one desktop and one app, and an agent that relaunches it (or one that flips its
backend with `--args -mockBackend NO`, which already happened) moves the target
under everyone else. These files do not move.

- `ref/01_home.png` — 1180x949, mock backend, welcome dismissed, two pinned
  tabs, the second active. **Its open thread has no transcript.** The id in that
  tab is a real session uuid, not a `mock-*` fixture, so Puffin's mock falls
  through to `MockBackend.swift:936` and renders one line — *"No fixture
  transcript for <id> yet."* — where hanabi renders a whole conversation.
  That is 3.18 structural points, 43% of the score, and no design change can
  spend it; `compare.py` now declares the viewport and reports it separately.
  Use 01 for the **chrome**: sidebar, tab bar, search, footer.
- `ref/02_thread.png` — 1180x949, same backend and window, **one unpinned tab,
  a thread with a real transcript in it**: a right-aligned user bubble with an
  avatar, an assistant bubble containing a fenced code block, and a run-outcome
  divider. Use 02 for the **transcript and composer**. Measured off it:

  | | |
  |---|---|
  | user turn | avatar x 791..811, bubble x 817..1097, y 95..129 — right-aligned, 35px tall, shrink-to-fit |
  | avatar | a 20px disc, x 791..811 y 101..121, same fill as the bubble |
  | user bubble fill | **(62,56,111)** — the same indigo as 01's |
  | user bubble TEXT | (237,237,245) — near-white text ON the indigo |
  | assistant bubble | x 362..1031, y 153..276, fill (33,33,54) |
  | fenced code fill | (19,19,27) — **not** the (51,68,60) this row read until 2026-08-24, which was a sample taken on a green syntax run rather than on the surface |
  | fenced code chips | **per LINE, and only the LAST one hugs its text**: x 384..1018 y 204..224, then x 374..435 y 225..245. 21px each, stacked with no gap. 1018 is the bubble's own inner edge, so every line but the last runs the full width — see below |
  | run-outcome divider | x 362..1097, rule on y 299, peak (48,48,62), which is `mutedText` at `.opacity(0.25)` (~(53,53,65)) spread over three rows by the downsample |
  | composer meta row | x 358..1099, y 861..889 |
  | composer input | x 357..1100, y 885..930 |

  **Why you can trust it, given the rule below.** It was NOT captured by
  driving the app: a 13-hour `buck2 test PuffinTestsMac` job of Gabe's owns
  Puffin right now and is opening and closing tabs of its own, so two captures
  8s apart differ by 13%. This one is a coherent frame caught between test
  steps, and it is verifiable as coherent rather than torn: **its sidebar is
  identical to `01_home.png`'s to within 0.13%, and its VIEWS block to 0.00%.**
  A torn frame does not agree with a reference taken six hours earlier to two
  decimal places. Do not try to re-shoot it while that job is running.

  **Correction, 2026-08-24 (feat/vis-turns).** The row above read "user bubble
  fill (237,237,245) — near-white, dark text" when this section was written.
  It is not: (237,237,245) is the TEXT, and the fill is (62,56,111), the same
  indigo 01 draws. Counted over the bubble's own rect (x 818..1097, y 95..129,
  9800px): the indigo is **61.4%** of it and (237,237,245) is **3 pixels**.
  Left in as a warning rather than quietly fixed, because the misreading is an
  easy one — a single-pixel sample inside a bubble lands on a glyph about as
  often as on the fill — and acting on it would have inverted hanabi's user
  bubble to white-on-dark, away from the reference rather than towards it.
  Sample a region and take the mode, never a pixel.

  **The other three numbers in that table were right, and they confirm
  Puffin's source independently.** The avatar disc measures 20px across
  (`BubbleAvatar.diameter = 20`), it sits 6px left of the bubble (the user
  row's `HStack(spacing: 6)`) and 6px below its top (`.padding(.top, 6)`).
  Three constants read out of `AgentcloudTranscriptView.swift`, three
  confirmations off a frame the source was not consulted for. When the source
  and a measurement agree to the pixel, the constant is settled.

  **02's version string reads v0.5.6 where 01's reads v0.5.5** — Puffin was
  rebuilt between the two captures. Nothing else in the two frames' shared
  chrome differs: the traffic lights, all four window corners and the status
  band are byte-identical, which is a second, independent check that 02 is a
  coherent frame and not a torn one.

Shoot hanabi in each state with the script that matches it — `shoot_hanabi.sh`
writes 01's two-pinned-tab blob and `shoot_hanabi_02.sh` writes 02's single
unpinned one. Do not edit either to shoot the other state; several agents run
them at once.

```bash
scripts/shoot_hanabi.sh    /tmp/hb01.png          # 01: t9 + t2, both pinned
scripts/shoot_hanabi_02.sh /tmp/hb02.png          # 02: r9, one unpinned tab
/usr/bin/python3 scripts/compare.py docs/visual-parity/ref/01_home.png /tmp/hb01.png --regions --diff /tmp/d.png
```

**02's transcript is like-for-like as of `feat/vis-fixture`, and that is a
divergence CLOSED rather than declared.** `ref/02_thread.png` has Puffin's
`mock-outcome-2` open — "row 133 banyan diff gate" — and hanabi's r9 row is
now that fixture ported turn for turn from `MockBackend.swift:752`: the same
question, the same reply, the same fenced two lines, the same relative stamps
and the same `failed` outcome. `shoot_hanabi_02.sh` defaults to it. Before
that the script shot r5, the nearest-SHAPED thread hanabi had, and `main`
measured how much prose each fixture happened to carry rather than how the two
apps draw a transcript.

Two consequences, both worth knowing before quoting a number:

- **Shoot 02 with r9.** Any other thread is measuring content again. Passing
  another id is still supported and still useful — deliberately, to compare a
  different pair — but it is not the parity number.
- **A per-thread score is a property of the PAIR.** The changes on
  `feat/vis-fixture` moved r9 from 4.53% to 3.80% and moved r5, on the same
  binary, from 4.49% to 5.09%. Nothing regressed: r5's rows happened to land
  closer under the old rhythm. Always say which thread a `main` figure was
  shot on.

Both scripts also take `HANABI_SHOOT_2X=1`, which renders at 2360x1898 with
`theme.ui_scale = 2.0` and reduces to 1180x949 with LANCZOS. **It is not the
default and should not become one — it scores worse.** See the floor section
below.


`02_thread.png` declares the four capture divergences and **not** the
transcript one — that is what it was captured for. Its declared cost is 0.25
structural points against 01's 4.75, so on 02 the transcript is a surface the
two apps genuinely share and every pixel of it is scoreable.

**Shoot both sides of an A/B back to back.** The mock fixture's timestamps are
wall-clock-relative and the transcript inserts a date divider where the
calendar day changes between two messages, so around local midnight the
divider moves — or vanishes — between one capture and the next, shifting every
row below it. Two shots of one unmodified binary three minutes apart measured
4.19% and 4.66% on `main`. Diff the two hanabi PNGs against each other before
you believe any difference between them.

## The score has a FLOOR, and for text it is most of the score

Run `compare.py --regions --floor`. It resamples the reference onto a grid
offset by half a pixel — **identical design, different rasterization phase** —
and prints what that scores, per region, beside your number. That is what two
different renderers cost you before anybody has drawn anything differently.

Measured on `02_thread.png`: overall **2.2–3.3%**, and in the session list
**8.4–11.8%**. So a list sitting at 14% is not thirteen points of unfinished
work; it is two. Three separate investigations concluded "the rest is the
rasterizer" and none could say how much of it was, because this file and
`compare.py` both claimed the structural floor was 0.2%. That figure came from
downsampling one frame two ways, which changes edge coverage but keeps every
glyph on the same grid — it measured the wrong thing.

The column prints `AT FLOOR` when a region is at or under it. **A region at
floor is done.** Do not spend a day proving otherwise; two people already have.

### The floor is computable for ANY rectangle, and that is how you know what to work on

`--floor` prints one per named REGION, and that framing hid the useful half for
a while: the same arithmetic works on any rectangle you care to name.
`scripts/ceiling.py` prints, per rectangle, what this shot scores, what the
REFERENCE'S OWN PIXELS score pasted in (the ceiling — `feat/vis-list2`'s paste
test), and that rectangle's floor.

You need all three, and the sidebar is the case that shows why. Its session
list is 13.6 points and its eighteen row titles are 12.8 of them, so every
round of work started there. But the titles are only **+2.57 over their own
floor**, while a 34x26 filter icon nobody had looked at was **+1.09** — 83% of
an entire region's headroom — and a column of eighteen 9px marks was +2.25. A
ceiling ranks by size; a ceiling beside a floor ranks by *reachable* size, and
the two orders are not the same.

One more rule the same tool earns: **shapes pay their ceiling and strings pay a
tenth of it.** A ceiling assumes the reference's own rasterization arrives with
the fix, which for a string it never does — `feat/vis-list2` got one seventieth
of its ceiling on three row titles. A rule, a ring or a fill is a shape hanabi
can put exactly where Puffin put one: the filter icon delivered **88%** of
its ceiling. Sort candidates by which they are before you sort them by size.

### The floor is real, but its explanation was wrong — and rendering at 2x does not remove it

The section above says the floor is *rasterization phase*, and that a hanabi
capture sampled the way Puffin's was would collapse it. That was the natural
reading of the half-pixel-offset experiment, it was the highest-value lead left
in this workstream, and **it is wrong**. `feat/vis-hidpi` built the capture and
measured it.

hanabi CAN render at 2x. It is `theme.ui_scale`, not `Config.hidpi`: hanabi
runs afterhours in Adaptive scaling mode, where `ui_scale` multiplies every
`pixels()` value including explicit font sizes, so a 2360x1898 render at 2.0 is
the same UI at twice the size. `HANABI_SHOOT_2X=1` on either shoot script does
it and reduces the PNG with LANCZOS, the way the reference was reduced.

It does not help. Shot back to back on one binary, structural over shared
surfaces:

| region | floor | 1x | 2x |
|---|---|---|---|
| list | 8.41–11.83 | **14.02** | 14.60 |
| views | 2.31–3.88 | **4.53** | 6.02 |
| sidebar | 6.25–8.93 | **10.61** | 11.35 |
| tabbar | 1.25–3.02 | **2.81** | 3.53 |
| footer | 1.61–5.87 | 5.26 | **4.40** |
| search | 1.54–3.17 | 4.24 | **4.21** |
| whole (shared) | | **9.12** | 9.68 |

**No region moves toward its floor.** The split is not random and it is not a
registration artefact — measured at each capture's own best sub-pixel offset,
the hand-drawn row marks improve (6.92% → 5.67%) and the row titles get worse
(14.57% → 15.86%). Supersampling helps every drawn SHAPE and hurts every
STRING.

The reason is that afterhours has no supersample, only a layout zoom (gap #101).
A 2x render re-measures, re-fits and re-advances every string at 33px instead
of sampling the 16.5px layout more finely. Over the eighteen visible row
titles that closes half the ink deficit against Puffin (640 → 662 px of ink,
against the reference's 685) and makes the strings 2.3px wider on average.
Extra ink along a string whose advances are drifting increases non-overlap on
both sides of every glyph — which is the same result the dilation and
gamma-lift experiments reached from the other direction, now confirmed a third
time by a different route.

**So: the residual under every text region is per-glyph PLACEMENT, not phase,
not coverage, not weight, and not sampling.** The floor numbers are still the
right thing to compare against and `AT FLOOR` still means done. What has
changed is that nobody should expect to get under them by changing how hanabi
is captured. Two remedies were already killed with numbers; this is the third,
and it was the last plausible one.

(A separate finding from the same branch, worth knowing before you read any
2x number: afterhours' private 5px text margin is in DEVICE pixels, so at
`ui_scale 2` every label sits 2.5px left of where the 1x build puts it —
gap #100. That is worth +1.49 points on VIEWS all by itself, and the sub-pixel
sweep above is how it was held apart from the rest.)


`--regions` prints two columns per region: **STRUCT first — that is the one to
drive** — and RAW beside it. Until 2026-08-24 the table printed RAW only, while
the two summary lines above it said to drive STRUCTURAL, so every per-region
figure quoted before that date is the raw mask. The two do not always agree on
direction: a face can score better on one and worse on the other, so quote the
column you are deciding from.

Also since 2026-08-24 the script prints three summary blocks rather than two
lines — the whole-frame figure, a list of declared divergences with what each
cost, and the figure over the surfaces the two apps share. See "Declared
divergences" below; `--no-exclusions` prints exactly what the old script did.

If you genuinely need a state that is not here — a settings sheet, a light
theme, an expanded tool pile — ASK ME rather than driving the live app. I will
capture it, freeze it, and add it to this list.

### The session list is DONE, and here are the per-glyph numbers so nobody re-opens it

**list 11.82% structural against a floor of 8.41–11.83 — AT FLOOR**, as of
`feat/vis-titles`. It joins the tab bar and the row-mark column. Six rounds
concluded the eighteen row titles were the rasterizer and left them alone; the
seventh found that the last +1.73 was one pixel of horizontal position
(afterhours ignores a label's padding — gaps #85, #91, #109), fixed it, and
then measured what remained DIRECTLY rather than inferring it. This section is
that measurement, so the eighth round does not happen.

**1. Every title now starts on the reference's column.** First-ink x, all
nineteen visible rows, hanabi against `ref/02_thread.png`: exact, including the
reference's own 28/29 alternation, which is the first letter's side bearing and
which hanabi reproduces. Before the fix it was `ref − 1` on every row.

**2. The ink deficit is uniform, and it is 11.5%.** Coverage-weighted ink,
hanabi over reference, per row: 0.895 0.886 0.904 0.896 0.870 0.886 0.875 0.892
0.858 0.890 0.889 0.887 0.893 0.885 0.882 0.888 0.895 0.877 0.875. Mean
**0.885**, range 0.858–0.904. There is no outlier row and therefore no second
bug hiding in the average. (Scan the full ink band: row 0's starts at y309,
above the list rectangle's own y313, and crops to a false 0.77.)

**3. Puffin's title is the REGULAR face.** `HomeSessionList.swift:1212` is
`.font(PuffinTheme.Font.message)`, and `PuffinTheme.Font.message =
face(Size.message)` with `face`'s weight parameter defaulting to `.regular`.
`messageEmphasis` exists and is used in three places, none of them a session
row, so Puffin bolds nothing in this list — no unread weight, no selected
weight. `FRICTION_LOG.md`'s `## The typeface question, settled` says *"Puffin
renders semibold through CoreText"*; it does not. The 11.5% is Regular against
Regular, which means there is no heavier face to switch to and the deficit is
CoreText's stem darkening alone.

**4. The advances drift, both ways, up to ±4px — this is the finished answer.**
Slide hanabi's per-column ink-coverage profile against the reference's inside a
24px window, take the best-correlating sub-pixel offset, and step the window
along the string. Every row starts registered (first-window dx between −0.5 and
+0.6, which is the fix landing) and then diverges on its own schedule:

| row title | dx, first window → last |
|---|---|
| `row 133 banyan diff gate` | −0.2 → **+4.0** |
| `coordinating 3 shard workers` | −0.2 → +1.9 |
| `two shards died` | −0.5 → +1.5 |
| `needs a decision before it can go on` | −0.3 → −0.3 |
| `Navi PRs: oak + juno` | −0.3 → −0.1 |
| `import failed twice` | −0.4 → −2.0 |
| `style guide written` | −0.3 → −2.2 |
| `parent — nothing to report` | −0.2 → −2.5 |
| `SKU backfill — my name for it` | −0.8 → **−3.7** |

Mean end-drift over all eighteen is −0.4px, so there is no second global
constant in it — the signs are mixed and a shift that helps one row hurts
another. And the per-row residual tracks |drift|: the rows still at their own
floor are exactly the rows whose drift ends under 0.5px. **Two text engines
advancing differently along a string, measured per glyph rather than argued
from ink totals.**

**5. The largest single named contributor is the em-dash, and it is priced at
0.22 points.** Four titles carry one. Measured as an isolated horizontal bar:
the reference draws 13px (x106–118, x96–108, x163–175, x72–84) and hanabi draws
**10px** on all four — Roboto's em-dash at 16.5px against the reference face's.
It shows up in the drift curves as a step at exactly those columns. Widening
all four synthetically to 13px and sliding each tail 3px right — better than
any real fix could do, since it moves already-drawn glyphs into place — takes
the title column 14.94% → 14.72%. There is no per-glyph advance override to
spend that with, and it is two tenths of a point.

**What this means for anyone arriving here.** The list is finished. So is the
tab bar, the search row and the row-mark column. If you are about to work a
text region, the one test worth running first is the rigid-shift sweep: crop
the region out of hanabi's capture, translate it by ±1px in each axis, paste it
back and re-score. It takes five minutes, the `dx=0,dy=0` cell is its own
control, and it is the only cheap way to tell a placement bug from a
rasterizer residual. A rasterizer residual is flat under that sweep. This one
was 2.27 points deep at +1.0.

## Read Puffin's source before you probe its pixels

**Puffin's own Swift source is checked out on this machine:
`~/kt-ng2w-puffin`** (`Sources/Views/`, `Sources/Agentcloud/`). It is the
authority on every question a screenshot can only be argued about, and it
answers in minutes:

- *Is this fill a selection or a hover?* — `SessionRowView` reads
  `.hoverHighlight(inset: 4, ..., isSelected: isKeyboardSelected)`, which settles
  that Puffin's persistent accent fill follows the **arrow-key cursor** and not
  the open thread. A static PNG cannot tell those two apart, and an agent
  spent a round measuring bands to reach a weaker version of that answer.
- *Is this element missing or just off-screen in this state?*
- *What is the real constant?* — an inset of 4 and a corner radius of 5 are in
  the source; off a 1x downsample they are a guess.

Use it. The pixels say WHAT; the source says WHY, and only the second one tells
you whether a difference is a bug in hanabi or a feature hanabi does not have.

**With one rider: the checkout is not the build that shot the references.**
`BubbleAvatar` in `~/kt-ng2w-puffin` draws `person.fill`; the frozen frames
unambiguously show a **G**. So the source is authoritative about *rules* — what
a fill means, which count goes in a badge, what the real inset is — and NOT
about what any particular pixel in `ref/` is. When the two disagree, the PNG is
the reference and the checkout has moved on. Say which one you used.

**The rider has now cost 12,000 pixels once, so here is the expensive
instance.** `CodeBlockView` in the checkout is
`VStack { … }.frame(maxWidth: .infinity).background(codeBackground)
.clipShape(RoundedRectangle(cornerRadius: 8))` — one full-width rounded panel
behind a fence — and `SyntaxHighlighter.highlight` sets no background attribute
at all, so on the source's evidence there is nothing else it could be.
`ref/02_thread.png` draws no panel: at y230, x374..435 is (19,19,27) and
x436..1018 is the bubble's (33,33,54). What it draws is one band per LINE, full
width for every line but the last and hugging the last — TextKit's line-fragment
background, stretched to the container edge by each line's terminating newline
and not by the last line, which has none. Building the panel the source
describes is **12,495 px wrong** against that frame; building what the frame
shows is nearly exact. Both readings are faithful to an authority. Only the PNG
says which authority is this build's. (feat/vis-pane, FRICTION_LOG
`## The transcript pane, round two`.)

### ...but the checkout is v0.5.2 and the reference is v0.5.5

`~/kt-ng2w-puffin/VERSION` says **0.5.2**. `ref/01_home.png`'s own footer says
**v0.5.5**, and no newer Puffin source exists on this machine (every `~/kt-*`
and `~/w/puffin-*` tree with a `VERSION` reads 0.5.2 or older). Three versions
is enough to move real things, and it has:

- **The session row's leading mark.** The checkout's `SessionRowView` draws a
  7pt `Circle()` on every childless row and a `chevron` on every row with
  sub-agents — two shapes, with the state carried entirely in the COLOUR. The
  reference draws five shapes (arc, dot, bang, cross, chevron) in three
  colours. Read the checkout's `statusDot` as gospel and you will conclude
  hanabi should throw its glyph vocabulary away.
- **The mock catalog.** Five of the reference's twenty rows are not in the
  checkout's `Mock/MockBackend.swift` at all.

So read the source for the **rule**, not for the **shapes**. The rule is stable
across the gap and the two agree wherever they overlap — `TabStatus.swift`'s "a
live status always wins ... only `idle` reaches for the thread's own colour" is
exactly the precedence the v0.5.5 capture draws, one axis up. And when you need
a fact the checkout cannot show you, pair the capture's rows against the
checkout's fixture: 15 of the 20 rows are in both, each with its state declared
in Swift, which turns "what does this glyph mean" into a lookup instead of a
guess.


## Never read a colour off the reference by its brightest pixel

Small text has no solid interior: a sidebar count is 3–4px of antialiased
stroke, so its peak pixel is nowhere near its real colour. Measured on
`ref/01_home.png`, a running count peaks at (114,161,243) while the running
*glyph* on the same row — same colour in Puffin's source — peaks at
(154,197,255). Reuse one for the other and you ship a visibly wrong colour.

What works is the **ratio of (pixel − background)** across several samples,
which is coverage-independent. It agreed to three decimal places across two
samples and recovered (120,169,255), which turned out to be a constant hanabi
already had.

## Write your notes in YOUR worktree, never in `~/w/vis/`

`docs/visual-parity/FRICTION_LOG.md` **inside your own worktree** is the log.
The copy at `~/w/vis/FRICTION_LOG.md` is a leftover from before this directory
existed and it is shared by every agent on this machine: it has no merge, so
two agents appending to it means the second one's `cp` erases the first one's
entries. That has already happened once and cost six entries. Same for
`~/w/vis/compare.py` and `~/w/vis/REFERENCE.md` — read them if you like, write
to `scripts/` and `docs/visual-parity/` in your branch, and I will merge.

## Compare LIKE FOR LIKE — this is worth 45 percentage points

`ref/01_home.png` has a **thread open**. If you shoot hanabi on the Home
digest and compare it to that, the `main` region reads ~58% and tells you
nothing: you are measuring "a list of cards versus a transcript", not a design
difference.

Shoot hanabi with the same tab open:

```json
{"window_width":1180,"window_height":949,"open_tabs":["t9","t2"],"active_tab":"t2","pinned_tabs":["t9","t2"],"theme":"dark"}
```

Measured on the same binary, same minute: Home digest **47.9% structural**, one
thread open **12.5%**. Same code, same palette — the whole difference was what
was on screen.

The same trap, one region down: `ref/01_home.png` has **two** tabs open and
**both are pinned**. Shooting a single unpinned tab scores the tab bar at
25.3% — a second tab's worth of empty strip, plus a filled active tab sitting
where the reference has an outlined inactive one, plus two missing pin glyphs.
Restoring the second pinned tab in the blob above takes the region to 5.5% with
no code change at all. Anything you measure in the tab bar before that is
measuring the fixture.

## Declared divergences — points no design change can spend

`compare.py` now carries a short, named list of rectangles it excludes from the
score, and prints every one of them with what it cost on the pair you just
compared. It is never silent, and `--no-exclusions` reproduces the historical
number exactly, so a change in the metric can always be told apart from a
change in the app.

The reason the list exists: some of what the harness measures is not a design
difference and never will be, and while those points sit in the total the total
means "how far apart the two apps are, **plus a constant nobody can spend**".
That constant was 3.50 structural points of 7.39 — **47% of the score** — and
the largest single piece of it is not hanabi's doing at all.

Measured on `ref/01_home.png` against a `main`-built hanabi on 2026-08-24:

| declared | struct pts | why it can never close |
| --- | --- | --- |
| transcript viewport | **3.183** | the reference's open thread has no Puffin mock fixture |
| titlebar traffic lights | 0.063 | the reference has window decoration; hanabi's capture is offscreen |
| window top bevel | 0.105 | same cause |
| rounded window corners | 0.031 | same cause |
| sidebar footer version string | 0.014 | v0.5.5 against v0.1.0 |

**`bottom status bar` (0.233 pts) was in this table until 2026-08-24 and is
CLOSED, not moved** — see "The status bar's 26px" below. The numbers in the
block that follows are the ones it was measured with; re-read them with the
entry gone.

```
WHOLE FRAME      raw 5.93%   structural 7.39%     <- the historical number
  declared cost      -2.48        -3.50 points
SHARED SURFACES  raw 9.20%   structural 10.37%    <- drive this
```

**The rate goes UP, and that is the point.** These are fractions, and the
declared surface is mostly empty in both frames — 700k pixels of black agreeing
with black. Taking it out removes far more denominator than numerator. The old
7.39% was being held down by a region that matched by accident and that no
amount of design work could ever have been spent on; 10.37% is what the
surfaces the two apps actually share look like.

### The bar for adding an entry

**One app draws chrome the other structurally lacks, or the two frames are
showing different content, and no change to hanabi's design can close it.**

That bar is deliberately high, and it is NOT "hanabi looks different here". A
band where both apps draw something and the two disagree stays in the score,
however badly it reads. The sidebar footer is the example to hold onto: hanabi
puts "6 blocked on you" and a session count there, Puffin puts `v0.5.5` and
three glyph buttons (`SidebarColumn.sidebarFooter`), and that is a real,
arguable, closeable difference about what belongs at the foot of a sidebar. It
is not declared, and the `footer` region still scores it.

Two things follow from that rule, and both are worth knowing before you read a
number:

- **A rectangle over chrome does not cover the DISPLACEMENT that chrome
  causes, and the displacement is the expensive half.** This is the lesson the
  status bar taught, and it generalises: the declared rectangle covered the
  strip's own 27 rows for 0.233 points while the 26px it *reserved* pushed the
  composer, its meta row, its pills and its rule out of register for **0.63**
  points — nearly three times as much, none of it inside any rectangle, and
  none of it declarable, because a difference you can design your way out of is
  not unspendable. Before declaring chrome, ask what the chrome MOVES.
- **The status bar was cheaper than it looked, and that was the trap.** Its
  fill was `theme::sidebar_bg()` — (23,23,35), the identical colour Puffin's
  empty window paints there — so the fill cost literally zero and the entry
  read as small. The 0.233 points were one hairline row and seven rows of "● 20
  sessions". Everything expensive about the strip was somewhere else.

### The big one: this reference cannot measure the transcript

The reference frame's open thread is `6cb2dacc-3bb3-455b-beba-71055c8d5065` —
a real session id, not one of Puffin's `mock-*` fixtures. Puffin's mock backend
has no transcript for it and falls through to
`Sources/Agentcloud/Mock/MockBackend.swift:936`, which emits a single line:

> No fixture transcript for 6cb2dacc-3bb3-455b-beba-71055c8d5065 yet.

So in the whole 897x749 transcript viewport the reference holds one user bubble
and one placeholder, and hanabi holds a full conversation. **That is the
"compare unlike states" trap from the section above, one level down**, and it
was worth 3.18 of the 7.39 points every number in this workstream has quoted.
Nothing hanabi does to bubble geometry, gutters or type can move it.

If you want the transcript measured, we need a reference whose open thread has
a Puffin fixture — any of the `mock-*` ids in `MockBackend.swift` will do
(`mock-blocked-1` and `mock-subagents-1` are the richest). That is a re-capture,
which is Gabe's to run; ask, do not drive the app.

## The status bar's 26px — CLOSED, 2026-08-24 (feat/vis-statusmove)

hanabi no longer paints a status strip. `layout_system.h` reserves nothing at
the window's floor, the composer runs to it exactly as Puffin's does, and
`src/ecs/status_bar_system.h` is deleted. What the strip carried lives in
`src/ecs/sidebar_footer_status.h`, drawn into `SidebarColumn`'s counterpart —
the sidebar footer, which is the only bottom-anchored chrome Puffin has.

**Why this was a design decision and not a pixel chase.** The strip's own 27
rows were declared in `compare.py` and worth 0.233 points. The 26px it
RESERVED was worth 0.64, was not declared, and could not honestly have been:
the composer band it displaced is full of ordinary closeable differences, and
a rectangle over the lot would have hidden them. So the choice was between
declaring something undeclarable and designing the strip away. Puffin's answer
was already on the table — put it in the sidebar footer — so hanabi took it.

**Nothing was deleted, and one thing was fixed.**

| what the strip carried | where it is now |
| --- | --- |
| `● N sessions` + the network activity light | the sidebar footer, right-aligned against the action cluster |
| `N blocked on you` | already on the sidebar's Blocked row as a badge (and rolled into Home's), which is where Puffin puts it; and stated in words on the macOS menu bar by `menubar.mm`'s `status_for_blocked` |
| `backend: mock` under `HANABI_DEBUG` | the sidebar footer, beside the version label, same gate. Nothing else in the UI prints `backend_label`, so it would have been lost with the strip |

The blocked phrase was the SECOND and THIRD rendering of one fact, and the
copies disagreed. The strip counted `s.tag == ThreadTag::Blocked` and drew
**3**; the badge counts `ecs::model::in_blocked_view` — Blocked OR Failed, the
rule Puffin's own `case .blocked` filter uses and the rule the reference's own
badge of six confirms — and drew **6**. Both numbers were on screen at once,
800px apart, in every capture this workstream has taken. `main.cpp` had
inherited the wrong copy for the menu bar too; it reads the model now. A
private re-derivation of a rule the shared model owns is the exact defect
`sidebar_system.h` already calls out one file over: *"two rules for one
question, and the one the reader saw was the one the tests did not cover."*

**What it bought, and what it cost**, shot back to back on one binary against
`ref/02_thread.png` (both sides on the post-change `compare.py`, so the metric
is held still):

| | before | after |
| --- | --- | --- |
| whole frame, structural | 5.00% | **4.38%** |
| shared surfaces | 4.82% | **4.21%** |
| `main` | 3.10% (+1.68 over floor) | **2.20%** (+0.78) |
| `footer` | 5.26% (AT FLOOR) | **8.19%** (+2.32) |
| the composer band alone (y≥845) | 0.825 frame pts | **0.342** |

**Read the footer row as points, not as a rate.** In the one currency that
adds, the footer went 0.064 → 0.089 frame points and `main` went 2.186 →
1.550: the count costs **0.025** and the register buys **0.636**, a 25:1 trade.
The footer's *rate* nearly doubled only because that region is 1.1% of the
frame, so 270 pixels of new ink move it three points. This is the "points and
rates are different currencies" note from the divergences work, and it is the
first time it has mattered in the direction that looks like a regression.

**The footer's +2.31 over floor is honest and it stays in the score.** Puffin
draws nothing between its version label and its buttons; hanabi now draws
`● 20 sessions` there. That is a real, arguable difference about what belongs
at the foot of a sidebar — exactly the kind the bar for a `compare.py` entry
says must keep being scored. It is not declared.

## Where Puffin puts "N blocked on you"

Worth writing down because hanabi's status bar has been assumed to have no
counterpart, and it half does. (This section is what the decision above was
made from — it was already in this file, and reading it was most of the work.)

- **The blocked count: Puffin shows it once, as a badge.**
  `SmartView.attentionCounts` (`Views/HomeSessionList.swift`) rolls every
  session into `[.home: blocked + review, .blocked: blocked, .review: review]`,
  `SidebarColumn` computes it at line 108 and hands it to `SmartViewSidebar`,
  which draws it as a count badge on the Blocked, Review and Home rows —
  outlined pills that deliberately hang past the sidebar's trailing edge. Home's
  band headers say "Waiting on you" over the same set. hanabi already draws
  those numbers on the same rows, so its status-bar phrase is the **second**
  place the same fact appears.
- **The session count: Puffin never shows it.** No total, anywhere — not in the
  sidebar, not in the search row, not on the menu-bar item (`AppDelegate`'s
  `NSStatusItem` carries the cloud mark and no badge), not in the window title
  (`window.title = "Puffin"`, `WindowManager.swift:1079`).
- **And there is no bottom strip to move anything into.** `MainWindowShell`'s
  root is an `HStack` (line 210): sidebar beside content, nothing spanning the
  window's floor. The only bottom-anchored chrome in the app is
  `SidebarColumn.sidebarFooter`, a 28pt `safeAreaInset` on the sidebar column
  alone, holding the version label and three glyph buttons — and its comment
  records that those moved there **from a titlebar accessory**, not from a
  status bar Puffin used to have.
- **Its footer has room, and Puffin leaves it empty.** `sidebarFooter` is
  `HStack { Text(version); Spacer(minLength: 8); three 20pt buttons }` at
  `.padding(.horizontal, 10)`, so in a 280px column the version ends near x=50
  and the buttons begin near x=210: ~160px of nothing in the middle. That is
  where hanabi's session count went.


## Declared divergences — differences that are NOT bugs

Things hanabi does deliberately that the reference does not do, or does
differently. Do not "fix" these toward the capture; they are the product
deciding something, and the parity score is expected to pay for them.

### The "hide automated rows" filter (hanabi has it; Puffin has none)

A real catalog mixes human conversations with scheduled ones ("Schedule: …",
"…-tick"). hanabi's search row carries an opt-in toggle that hides them, keyed
on the title's SHAPE (`is_automated` in `sidebar_system.h`). Puffin has no such
control and its list always shows everything.

This is a divergence in the FILTER only. It used to be a divergence in the ROW
too — an automated row drew a different, quieter glyph and a dimmed title, with
nobody asking for it — and that part is gone as of `feat/vis-glyphs`. The
reference draws `kicker-tick` exactly like the two live runs on either side of
it (same blue arc, same title colour), because a scheduled thread that is
running is a thread that is running: how it was STARTED is not its status. The
removed rule also decided a row's whole appearance from a string match on its
title, so renaming a thread changed what the list said about it, and a blocked
cron tick — the row that most wants a human — read as the quietest thing on
screen.

Keep the filter. It is the honest form of the idea: the reader says "not now",
rather than the client deciding some threads matter less than others.

### The sidebar footer's three buttons are not Puffin's three buttons

Both apps end the sidebar with a version label on the left and three glyph
buttons on the right, over a hairline, in a 28px band. The buttons are not the
same buttons and should not be made so.

- **Puffin**: `info.circle` (About), `ant` (`BugReport.icon` — file a bug),
  `gearshape` (Settings). `SidebarColumn.sidebarFooter`, and its own comment
  records that these moved there from a titlebar accessory.
- **hanabi**: `plus` (new thread), `search` (command palette), `gear`
  (Settings).

Only the third pair matches, and the score says so: measured on
`ref/01_home.png`, the gear column carries 44 structural diff pixels against 87
and 90 for the two either side of it — the matched one costs half what the
mismatched ones do, on the same ink, at the same size, in the same colour.

This is a product difference, not a defect. hanabi has no bug-report flow for an
`ant` to open and no About pane distinct from Settings, and closing the gap
would mean deleting a reader's two fastest paths to a new thread and the
palette in exchange for one button that does nothing and one that duplicates
the gear. It does NOT meet the bar for a `compare.py` exclusion either — a
different icon is closeable in principle, and the rectangle would hide the
colour and position work that is genuinely scoreable in that band. So the
footer region keeps scoring it, and 221 of its 643 diff pixels are these three
buttons with two of them drawing different things on purpose.

Corollary, measured while establishing the above and worth knowing before
anyone reaches for the footer again: **the footer's whole colour axis is worth
0.11 points.** Puffin's footer ink is `mutedText` (140,140,166) and hanabi's is
`text_faint` (100,100,112), which looks like an easy point — and moving to
`text_secondary` (142,142,154), the nearest token, makes the region WORSE
(4.08% -> 4.58%), because Puffin's 9pt glyphs never reach their own colour and
hanabi's sprite blits do. Swept across every plausible ink, the best available
scores 4.32% against text_faint's 4.44% in the same harness. The full working is
in FRICTION_LOG.md under `## Tab strip and sidebar footer (feat/vis-tabs3)`.

### The row mark's vocabulary is Puffin's, not `docs/state-model.md`'s
`docs/state-model.md` specifies a colour-blind-safe legend of its own — red
up-triangle for blocked, green diamond for review, blue dot for done — derived
from an analysis of ~200 real threads. The shipped sidebar has not drawn that
legend for some time, and as of `feat/vis-glyphs` the shared model does not
either: `ecs::model::mark_for` speaks the reference's vocabulary. Shape still
carries the status independently of colour, so the colour-blind property the
doc was protecting survives; the shapes themselves are the reference's.


### hanabi paints an icon and the label beside it in TWO colours where Puffin uses one

`SmartViewSidebar` line 297 hands `Chrome.mutedText` to the whole smart-view
row — icon and label together — and hanabi deliberately does not. The icon
takes (140,140,166), which is that token; the label takes (150,150,175), ten
levels above it.

This is not drift and it must not be "corrected" back to one constant, which is
what the source reads like an instruction to do. **A sprite blit reaches its
colour and antialiased 9pt text never does.** The label's ten extra levels are
what it takes for hanabi's text to read like the reference's text; handed to a
blit they arrive in full, and every view icon peaked 15-22 levels above the
reference's for as long as one constant served both.

The same fact is already in this file with the opposite sign: the sidebar
footer's ink is measurably better in `text_faint` than in the nearest real
token, "because Puffin's 9pt glyphs never reach their own colour and hanabi's
sprite blits do". One rule, two consequences — move a BLIT to the source's
token, and leave TEXT wherever it measures.

Corollary worth holding onto: the score keeps improving as an icon's ink darkens
past the measured value (4.42% at the true token, 4.31% at 125, monotonically).
That is the metric paying for less ink, exactly as it pays for smaller text.
The source's own constant is the only defensible place to stop.

### The session row's star (hanabi has it; Puffin's row has no equivalent)

hanabi pins a thread from its sidebar row: a star at the row's trailing edge,
shown when the thread is pinned and on hover when it is not. Puffin's
`SessionRowView` has nothing of the kind — pinning lives on the row's context
menu and on the tab, and the row's trailing items are the theme mark, the mute
bell, the sub-agent count and the age, every one of them conditional.

So the reference will always have a little more title, and on a hovered row
hanabi will always have something the reference does not. Not a bug and not
declared: it is drawn out of flow (`feat/vis-list2`), so at rest it costs the
title nothing and the two lists measure like for like. It only diverges under
the pointer, which is a state no reference captures.

What this section exists to stop is the obvious "fix": giving the star a
reserved column so it stops overlapping the title on hover. That is what hanabi
did until `feat/vis-list2`, it cost every one of twenty titles 18px forever,
and it ellipsized three of the reference's own titles a word early.

### hanabi's list is capped and Puffin's is not — but the cap must exceed the viewport

Puffin's sidebar is a `LazyVStack` that builds rows on demand, so its list is
simply as long as the catalog. hanabi renders a bounded number and offers the
rest behind a "Show N more…" expander, which is a real divergence and a
deliberate one: it is the guard that keeps a 2000-session catalog cheap.

The bound must be bigger than the viewport, though, and for a long time it was
`viewportRows - 1`. That makes the scroll panel's content shorter than the
panel by construction — the list cannot scroll at any catalog size, and the
expander is not an escape hatch but the only way to see row nineteen. Against
the reference it also costs a row: Puffin draws nineteen and clips a twentieth
where hanabi drew eighteen and a button.

It is two viewports now. Keep it above one.

---

### The user bubble is 45px narrower than the reference's, and that is the typeface

Measured on the ported thread, where both apps hold the same 42-character
sentence: Puffin's bubble is x 817..1097 and hanabi's is x 863..1097. Same
nominal 13px body, same 12/13px horizontal padding, same right edge — the
string simply sets narrower in Roboto than in SF. The same fact runs the other
way inside the fence: the reference's mono line inks 419px wide against
hanabi's 280.

**This row read "20px" until 2026-08-24 (feat/vis-pane) and the number was
stale**, taken before the fixture was ported. Re-measured on r9 it is 45, which
is a quarter of everything left in the transcript rather than a handful of
pixels down one edge. The conclusion is unchanged; what changed is what it is
worth, and anyone re-opening the typeface question should weigh it against 45.

Do not close this by widening the bubble. A bubble is shrink-to-fit, so its
width is its text's width, and forcing it to Puffin's number would mean padding
the box away from its own content — the one property the shape exists to have.
The typeface itself was measured and discarded as a change on its own terms
(FRICTION_LOG, "The typeface question, settled"). It is not a declared
divergence in `compare.py` either: declaring rectangles over text is how a
metric stops measuring text.

