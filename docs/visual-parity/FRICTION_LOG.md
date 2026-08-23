# Visual-parity friction log

**Why this file exists.** Making hanabi look like Puffin is the forcing
function, not the goal. The goal is to find out what afterhours makes hard or
impossible when you have a specific design in hand and no freedom to negotiate
with it. A UI you draw freehand never finds these; a UI you must MATCH finds
them in the first hour.

`afterhours_gaps.md` is for a gap written up properly — what was tried, what
happened, the workaround and its cost, and the smallest upstream change that
would fix it. **This file is the wider net**: annoyances, footguns, papercuts,
and things that are merely tedious. A thing too small for a gap entry still
belongs here, because ten of them in a row is itself a finding.

## How to add an entry

Keep it short and concrete. What you wanted, what the library did, what it cost
you. A number is worth more than an adjective.

- **What I wanted** — in design terms, from the Puffin reference
- **What happened** — the actual behaviour, not your theory about it
- **Cost** — lines of workaround, pixels still wrong, or time
- **Class** — `IMPOSSIBLE` / `WORKAROUND` / `FOOTGUN` / `TEDIOUS`
- **Gap filed?** — the `afterhours_gaps.md` number, or why it does not warrant one

`IMPOSSIBLE` and `WORKAROUND` should also get a real gap entry. `FOOTGUN` and
`TEDIOUS` usually should not — they live here and get counted.

---

## Entries

### 1. A 1x app cannot be compared to a 2x app without a resampling floor

- **What I wanted** — one honest number for "how far apart are these two UIs".
- **What happened** — Puffin renders at 2x on a retina display; hanabi's
  headless capture is 1x, and rendering into a 2x-sized texture does not help
  (the adaptive UI just lays out at the larger logical size — a thin sidebar in
  a big canvas — rather than supersampling). Taking ONE Puffin frame and
  downsampling it two different ways gives **2.27% differing pixels overall and
  10.03% in the text-dense session list**, against itself, with no design
  difference whatsoever.
- **Cost** — the raw pixel metric is unusable below ~2.3%, so the comparison
  needs a second, blurred measure (0.8px, which reads 0.23% on that same
  identical-source pair) to mean anything at the end.
- **Class** — `WORKAROUND`
- **Gap filed?** — the underlying "no crisp @2x headless capture" is already
  noted in the repo; this quantifies what it costs a comparison. Worth
  attaching the numbers to that entry.

### 2. Two clicks a few frames apart are a double-click, in real time

- **What I wanted** — a scripted test that clicks a field, then clicks it again
  later to refocus it.
- **What happened** — the harness's frames are much faster than wall-clock, so
  two clicks separated by several `wait_frames` are still inside the OS
  double-click window. The test passed on a loaded machine and failed on an
  idle one — the worst possible direction.
- **Cost** — one flaky test that survived a full day of green runs before it
  was caught, and a rewrite to avoid ever clicking the same element twice.
- **Class** — `FOOTGUN`
- **Gap filed?** — no. Worth one if the harness ever grows a virtual clock.

---

## Palette and window frame

### 3. A rule that spans panels belongs to no panel

- **What I wanted** — Puffin's one hairline at x=279, `#2A2A39`, full window
  height: over the tab strip, over the footer, over the selected row it
  crosses.
- **What happened** — `with_border_right` exists and cannot do it. A border is
  clipped to its own panel, and hanabi's four frame panels do not tile the
  window (the status bar spans the full width *under* the sidebar, so the
  sidebar's rect is 24px short of the bottom). It is also painted under its own
  children, so every full-width sidebar row erases it — the same draw ordering
  as gap #63.
- **Cost** — a fifth entity that belongs to no panel: a 1px x window-height
  absolutely-positioned div parented to the UI root, 27 lines. The lines are
  not the cost; the **render layer** is. It had to land above the status bar
  and the tab strip and below the row menu, and the only way to learn those
  numbers was to grep four unrelated systems for `with_render_layer` and pick
  an unused integer by hand. Layers are bare ints with no registry, so a global
  ordering decision gets made locally and a future collision fails silently.
- **Class** — `WORKAROUND`
- **Gap filed?** — #64.

### 4. There is no way to say "no border"

- **What I wanted** — the search field at rest as Puffin draws it: a bare fill,
  no ring. Focused, an accent ring. One widget, two states.
- **What happened** — the builder chain has no `with_no_border()` and no way to
  omit a property conditionally, and a transparent border colour is not an
  option because rect fills cannot alpha-blend (gap #13) — it renders as an
  opaque black outline. The two escapes are to duplicate the whole 14-line
  `ComponentConfig` behind an `if`, or to draw an invisible border.
- **Cost** — 1 line and 8 lines of comment explaining why the border colour is
  the fill colour. Every frame draws a 1px rect nobody can see. Small, but it
  is a lie in the code and the next reader will "fix" it.
- **Class** — `FOOTGUN`
- **Gap filed?** — no. Would be closed by an optional-valued setter, or by
  `with_border(Color, Size)` treating `a == 0` as "none".

### 5. hanabi's palette must be constants because fills cannot blend

- **What I wanted** — the five named surfaces, matched exactly. Got all five.
  But while reading Puffin's source to find them, the interesting thing was
  **how it gets them**: it does not store them. `PuffinTheme` holds ELEVEN
  colours per theme, and every surface is composited from those at draw time —
  the section-header strip is the text colour at 5% over the background, the
  selected row is the accent at ~20% over the strip, the hairline is the muted
  text at 20%. Ten themes, eleven numbers each, and every surface in the app
  falls out of them.
- **What happened** — hanabi cannot express that. `theme::over()` exists
  precisely because the sgl fill path has alpha blending off (gap #13), so a
  tint has to be pre-blended against a backdrop the caller must know. That is
  workable for one chip, and it collapses for a palette: you cannot pre-blend
  against "whatever surface this lands on" at token-definition time. So
  hanabi's palette is ~40 hand-set constants x 2 modes, each of which has to be
  re-derived by hand whenever a background moves — which is exactly what this
  task was.
- **Cost** — not lines; **leverage**. Puffin retheming is 11 numbers. hanabi
  retheming was 7 tokens today and would be ~40 to add a third palette, with no
  guarantee the relationships between them survive. This is the largest
  single consequence of #13 and it is invisible until you try to match a
  generative palette with a static one.
- **Class** — `WORKAROUND`
- **Gap filed?** — #13 already exists; this is the consequence worth attaching
  to it, not a new number.

### 6. Puffin has no light theme, and the premise that it does costs an hour

- **What I wanted** — Puffin's light values, to check hanabi's light palette
  against.
- **What happened** — there is no light theme and no dark theme. Puffin does
  not follow the system appearance at all: it wears one of ten NAMED palettes
  (`LinkTheme`, `Models/Models.swift`), stored under the `theme` default as a
  name, and an unrecognised value there resolves to Hyrule — so
  `defaults write com.meta.puffin theme -string light` would have produced
  Hyrule, not a light Puffin. Every number in this task's spec is the **Navi**
  preset's chrome. Three presets are pale (Light World, Daylight, Puffin Day)
  but choosing one as "the light Puffin" would be a decision, not a
  measurement.
- **Cost** — ~20 minutes to establish, and it is the reason hanabi's light
  palette is unchanged. Worth writing down so the next four agents do not each
  spend the same 20 minutes.
- **Class** — `TEDIOUS`
- **Gap filed?** — no; not an afterhours issue.
