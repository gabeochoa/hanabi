# Puffin visual reference — measured, not guessed

The target: hanabi renders pixel-identical to Puffin. Everything here was read
off a real Puffin window with `probe.py`, not eyeballed. If you disagree with a
number, re-measure — do not adjust by eye.

## How to capture and score

```bash
cd ~/w/vis
./shoot_puffin.sh puffin_home.png                  # Puffin window -> 1x PNG
# hanabi, same size, same backend:
env HOME=$ISO HANABI_BACKEND=mock HANABI_WIN_W=1180 HANABI_WIN_H=949 \
    ./output/hanabi.exe --screenshot hanabi_home.png
/usr/bin/python3 compare.py puffin_home.png hanabi_home.png --regions --diff d.png
```

`compare.py` counts pixels differing by more than 12/255 on any channel, so
antialiasing noise between two renderers does not count and a wrong colour,
position or glyph does. `--regions` says WHERE, which is the only part you can
act on. `--diff` paints the differing pixels red over hanabi's shot.

Puffin runs on the mock backend (`defaults write com.meta.puffin mockBackend
-bool true`) and past its onboarding (`hasSeenWelcome -bool true`), so its
content is deterministic and can be matched.

## Window

| | |
|---|---|
| logical size | 1180 x 949 (Puffin's own window; hanabi is told to match) |
| captured at | 2x, downsampled to 1x — never upscale hanabi to meet it |

## Palette

Puffin uses **one background** for the whole window. There is no separate panel
tint: the sidebar and the main pane are the same colour, parted by a single
hairline. hanabi currently paints them differently, which is the first thing a
side-by-side shows.

| surface | RGB | hex |
|---|---|---|
| window / sidebar / main / tab strip | (23, 23, 35) | `#171723` |
| sidebar section header strip | (34, 34, 45) | `#22222D` |
| selected view row, active tab | (46, 58, 88) | `#2E3A58` |
| vertical divider, sidebar to main | (42, 42, 57) | `#2A2A39` |
| search field fill | (36, 36, 48) | `#242430` |

## Geometry

| | value |
|---|---|
| sidebar width | 280, then a **1px** divider at x=279 |
| section header strip | y=0..67 region, fill `#22222D` |
| view row pitch | 32 (selected row spans y=68..98, so 31 tall + 1 gap) |
| search field | y=270..295, height 26 |
| session list first row | glyph centre y=315 |
| **session list row pitch** | **32** (measured across 14 rows: 32,32,32,31,32,33,32,32,32,31) |
| footer bar | bottom ~24, version string left, three icons right |

## Sidebar structure, top to bottom

Order matters and hanabi's is different:

1. Traffic lights inline at the very top left (no brand row, no toolbar buttons)
2. `⌄ VIEWS` header strip, with a panel-toggle icon at its right edge
3. Six view rows: **Home, Settings, Blocked, Review, Pinned, Archived** —
   each an icon, a label, and a right-aligned count when non-zero
4. Search field with a magnifier, and a filter icon to its right
5. A **flat** session list — glyph, title, optional right-aligned count.
   **No timestamps.** hanabi shows a time on every row; Puffin shows none.
   **No selection fill on the open thread.** Puffin's session row takes a
   persistent `#2E3A58` fill only for the KEYBOARD CURSOR
   (`SessionRowView.isKeyboardSelected`); "this thread is already a tab"
   (`isOpen`) reaches nothing but the context menu, where it disables the
   split items. Verified in `ref/01_home.png` (the only `#2E3A58` block in the
   sidebar is the selected VIEW row, y=69..97) and in Puffin's own source.
   A session row's only other fill is HOVER — near-white at ~0.09 over the
   chrome surface, drawn as a pill inset 4px with a 5px radius, not full bleed.
6. Footer: version string (`v0.5.5`) left, three small icons right

hanabi today: a brand row with three buttons, search ABOVE the views, a FOLDERS
section, and a time on every row. All of that has to go or move.

## Tab bar

- Tabs sit in the main pane's top strip, not across the whole window
- A pinned tab carries a pin glyph before its title
- The active tab is filled `#2E3A58`, inactive is the window colour
- `+` at the far right of the strip

## Transcript

- User message: right-aligned bubble, indigo, with a circular avatar to its LEFT
- Assistant message: left-aligned bubble, dark grey, no avatar
- Both corners ~10px

## Composer

Bottom of the main pane, and structurally different from hanabi's:

- A thin progress bar with a `0%` label at the **top left** of the composer area
- Three pill chips at the **top right**: `Tools`, `Thinking`, `Deliveries`
- A rounded multi-line input, placeholder `Message Agentcloud… (↵)`
- A **circular** send button with an up-arrow, at the input's right

hanabi has a rectangular `Send` button, chips at the bottom left, and the meter
at the bottom right — every element is on the wrong side.

## Rules for this work

- `vendor/afterhours` is READ-ONLY. Work around it in hanabi and write the gap
  into `afterhours_gaps.md` (numbers go up to #63; do not reuse one).
- Do not chase a number you cannot explain. If a region will not converge, say
  what the library will not do rather than nudging pixels until it looks close.
- Every change keeps `make test` green.

---

## Composer, measured off ref/01_home.png

- horizontal edge at y=850: (22, 22, 34) -> (33, 33, 45)
- horizontal edge at y=851: (33, 33, 45) -> (57, 57, 70)
- horizontal edge at y=852: (57, 57, 70) -> (22, 22, 34)
- horizontal edge at y=884: (22, 22, 34) -> (45, 45, 59)
- horizontal edge at y=885: (45, 45, 59) -> (32, 32, 45)
- horizontal edge at y=886: (32, 32, 45) -> (22, 22, 34)
- horizontal edge at y=930: (22, 22, 34) -> (45, 45, 60)
- horizontal edge at y=931: (45, 45, 60) -> (31, 31, 44)
- horizontal edge at y=932: (31, 31, 44) -> (22, 22, 34)

- input fill: (23, 23, 35)
- send circle: (82, 82, 100)
- chip fill: (23, 23, 35)
- meter track: (53, 53, 68)

**Read from the numbers above:**

- The composer strip starts at a 1px rule at **y=851**, colour `#393946`
- The input box is **y=885..930** (45 tall), fill is the WINDOW colour with a
  1px border `#2D2D3C` — it is an outlined box, not a filled one
- The meter track is `#353544`, a few pixels tall, at the strip's top LEFT
- The chips are OUTLINED too — their interior is the window colour
- The send button is a circle, fill `#525264`, at the input's right

The pattern worth noticing: Puffin's controls are mostly **outlines on the
window colour**, where hanabi's are **filled panels**. That one difference
accounts for a large share of the composer's diff, and it is a theme-level
decision rather than a per-widget one.

---

## Session list, measured off ref/01_home.png

Row geometry (19 rows visible, first glyph centre y=314.5):

| | value |
|---|---|
| row pitch | **32.0** exactly (first title centre 316, 18th 891.5; 575.5/18 = 31.97) |
| glyph centre | x=15.0-15.5, and **1.5px ABOVE** the row's midline |
| title first ink | x=28-29 |
| title size | **~16.5px** (ink bbox 102/125/99/148 for rows 0/1/4/10; ascender-to-descender 13px) |
| title colour | **(238,238,247) on EVERY row** — attention is not encoded in title brightness |
| title weight | semibold; the reference's title ink is 17% denser than Roboto Regular at the same size |
| count column | right-aligned, right edge x=269 |

Row markers — seven shapes, three hues, all 8-10px:

| shape | colour | rows in the reference |
|---|---|---|
| arc, open at the LOWER LEFT (ink runs ~190deg over the top and round to ~85deg), r=4.8, stroke 1.8 | (150,192,255) | 0-3, running |
| filled dot r=4.0 | (155,195,255) | 4, 5 |
| bang `!` — 2.3px stem cy-6..cy+1, dot r=1.4 at cy+4.5 | (164,208,255) | 6, 8, 9, 11, 12, 13 |
| cross `✕` 8x8, stroke 2 | (226,93,97) | 7, 15 |
| filled dot r=3.7 | (145,145,170) | 10, 14 |
| filled dot r=3.7 | (221,91,95) | 17 |
| chevron `>` 5x8, stroke 1.9 | (148,148,173) | 16, 18 |

**CORRECTED 2026-08-24 (feat/vis-glyphs).** This section used to end "the
mapping is not a function of anything hanabi stores... four shapes is the
ceiling on `(ThreadState, ThreadTag)`". That was true of the FIXTURE, not of
the mapping: the seven markers are a function of state, and the reason six rows
looked indistinguishable is that hanabi's fixture gave six different things the
same `(Attention, Blocked)` pair. The rule, verified against Puffin's own
source and its own fixture row by row:

| what the thread is doing | mark |
|---|---|
| a run is LIVE (`running` / resolved kind `running`) | arc, live blue |
| testimony says `working`, no live run | filled dot, live blue |
| blocked, or waiting/review — anything that wants you | bang, live blue |
| testimony says `failed` | cross, alert red |
| a failed OUTCOME with no testimony behind it | filled dot, alert red |
| settled, and it has sub-agents | chevron, calm grey |
| settled | filled dot, calm grey |

Blocked and waiting share one mark, shape and colour both: all six of the
reference's waiting rows measure to the same (164,208,255) bang. The run always
owns the slot — the chevron appears only when the run has nothing left to say,
which is why `coordinating 3 shard workers` (3 sub-agents, running) draws the
arc and `Navi PRs: oak + juno` (1 sub-agent, settled) draws the chevron. See
the long note on `ecs::model::mark_for`.

Two of those seven needed states hanabi's model did not hold — `Working` (a
`working` claim with no run behind it, which Puffin calls a corpse and buckets
as finished) and `Failed` (the fifth member of the wire vocabulary
`working/waiting/blocked/done/failed`, which hanabi's own agentcloud adapter
was dropping on the floor). Both are now in `api/types.h` and both are reachable
from the live backend, not just the mock.

**Counts** appear on 7 of 19 rows and are sub-agent counts: `1` where the
session has one sub-agent, `1/3` for `coordinating 3 shard workers`, which has
three of which one is Done. The mock already carries this on `Session`;
`api::SessionSummary`, which is all the sidebar sees, does not.
