# The frozen reference set

`ref/*.png` are the ONLY reference. Do not re-shoot Puffin: five agents share
one desktop and one app, and an agent that relaunches it (or one that flips its
backend with `--args -mockBackend NO`, which already happened) moves the target
under everyone else. These files do not move.

- `ref/01_home.png` — 1180x949, mock backend, welcome dismissed, one thread open

Compare against them:

```bash
/usr/bin/python3 ~/w/vis/compare.py ~/w/vis/ref/01_home.png <your_shot.png> --regions --diff /tmp/d.png
```

`--regions` prints two columns per region: **STRUCT first — that is the one to
drive** — and RAW beside it. Until 2026-08-24 the table printed RAW only, while
the two summary lines above it said to drive STRUCTURAL, so every per-region
figure quoted before that date is the raw mask. The two do not always agree on
direction: a face can score better on one and worse on the other, so quote the
column you are deciding from.

If you genuinely need a state that is not here — a settings sheet, a light
theme, an expanded tool pile — ASK ME rather than driving the live app. I will
capture it, freeze it, and add it to this list.

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

### The row mark's vocabulary is Puffin's, not `docs/state-model.md`'s

`docs/state-model.md` specifies a colour-blind-safe legend of its own — red
up-triangle for blocked, green diamond for review, blue dot for done — derived
from an analysis of ~200 real threads. The shipped sidebar has not drawn that
legend for some time, and as of `feat/vis-glyphs` the shared model does not
either: `ecs::model::mark_for` speaks the reference's vocabulary. Shape still
carries the status independently of colour, so the colour-blind property the
doc was protecting survives; the shapes themselves are the reference's.
