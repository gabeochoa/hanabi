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

Also since 2026-08-24 the script prints three summary blocks rather than two
lines — the whole-frame figure, a list of declared divergences with what each
cost, and the figure over the surfaces the two apps share. See "Declared
divergences" below; `--no-exclusions` prints exactly what the old script did.

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
| bottom status bar | 0.233 | hanabi has a status strip; Puffin has no equivalent surface |
| titlebar traffic lights | 0.063 | the reference has window decoration; hanabi's capture is offscreen |
| rounded window corners | 0.031 | same cause |
| sidebar footer version string | 0.014 | v0.5.5 against v0.1.0 |

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

- **hanabi's composer sits ~27px higher than Puffin's** — its box bottom is at
  y=903 against Puffin's y=930 — because the status bar consumes the bottom
  26px of the main pane. Nothing but deleting the bar closes that, so by the
  letter of the rule it qualifies. It is **not** declared anyway: the same band
  carries the chip row, the placeholder and the send affordance, all of which
  are ordinary closeable differences, and a rectangle over the lot would hide
  them. Shifting hanabi's composer band down 27px takes it from 9.74% to 3.15%,
  so roughly two thirds of what that band reads is position and one third is
  design. Quote both halves or neither.
- **The status bar is cheaper than it looks.** It spans the main pane only
  (`layout_system.h` — a full-width bar would paint over the sidebar footer),
  and its fill is `theme::sidebar_bg()`, which is (23,23,35), the same colour
  Puffin's empty window paints there. The fill costs nothing. What the 0.233
  points actually buy is one hairline row at y=923 and seven rows of "● 20
  sessions" on the right.

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

## Where Puffin puts "N blocked on you"

Worth writing down because hanabi's status bar has been assumed to have no
counterpart, and it half does.

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
