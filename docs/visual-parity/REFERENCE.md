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
