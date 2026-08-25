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

---

## Sidebar (Puffin parity)

### 12. A measured pixel pitch is not a pitch the library will honour

- **What I wanted** — Puffin's session rows sit on a 32px pitch and its view
  rows on the same 32. The spec measured it off a real window; every row in the
  sidebar is `pixels(32)` tall in a NoWrap column.
- **What happened** — the rows render **30px apart at a 949px-tall window and
  exactly 32 apart at 760**. Same binary, same code, same sidebar width. The
  cause is grid snapping (`UIStylingDefaults::set_grid_snapping(true)`, on in
  `preload.cpp`): `snap_to_8pt_grid` derives a grid unit from the WINDOW HEIGHT
  — `round(4 * screen_h / 720)` — which is 4 at 760px and **5 at 949px**, and
  the column's running offset is snapped to it after every child
  (`autolayout.h`, "Snap only the final position, not the inter-child spacing
  accumulator"). 32 snapped to a 5px grid is 30. There is a `skip_grid_snap`
  flag, but it is only consulted for a widget's SIZE; the position snap has no
  per-widget escape.
- **Cost** — 2px per row, accumulating to **36px of drift by row 18** — the
  bottom of the list is a row and a half out of place. Unfixable from inside the
  widget: the only lever is turning grid snapping off for the whole app, which
  moves every panel in the window by up to 5px and is not a call a
  single-component change can make. The design geometry is now correct at the
  window size the test suite uses (760) and wrong at the one the reference was
  shot at (949).
- **Class** — `IMPOSSIBLE`
- **Gap filed?** — #65.

### 13. Deleting a button moved a blue ring onto a design element

- **What I wanted** — the reference sidebar has no focus ring anywhere: nothing
  has been clicked or tabbed to, so nothing is ringed.
- **What happened** — afterhours parks focus on the first focusable element at
  startup and paints its ring immediately, with no "focus-visible" notion (a
  ring only after keyboard navigation). Before this change the ring sat on the
  header's `+` button, which read as a deliberate highlight and nobody noticed.
  Removing the header moved the ring onto the VIEWS header strip — a 280px-wide
  blue box across the top of the sidebar. `with_skip_tabbing(true)` took it off
  the strip and put it on the first view row instead. The ring does not
  disappear; it only relocates, so the app cannot say "no ring until the
  keyboard is used" without skip-tabbing every clickable in the window.
- **Cost** — ~600 wrong pixels that no amount of geometry work removes, and a
  60-line detour through the library to find out that `visual_focus_id` is
  assigned unconditionally.
- **Class** — `WORKAROUND`
- **Gap filed?** — #66.

### 14. Every gap in a measured design is an empty div

- **What I wanted** — the reference's column has real spacing in it: 36px of
  window-drag space at the top, 3px above the rule, 6px between the rule and the
  search field, 4px before the list.
- **What happened** — a column child cannot say "start 4px below the previous
  one". `with_margin` on a `percent(1.0f)` child overflows the parent (the
  content-box model is documented as such in the layout code), and there is no
  gap-between-two-specific-children. So each measured gap became an empty
  `spacer()` div that exists only to occupy height.
- **Cost** — 3 spacer divs and a 12-line helper in a 6-element column; the
  column's real structure is now half padding widgets. Small, but it is the
  reason `scroll_top_offset()` has to be a hand-maintained SUM of six constants:
  the list's top edge is not readable from any one widget.
- **Class** — `TEDIOUS`
- **Gap filed?** — no. Would be fixed by either child margins that do not
  overflow, or a `gap` that applies between children of a Column.

### 15. Right-aligning a count is still arithmetic, three times over (gap #18)

- **What I wanted** — Puffin's counts (`9`, `6`, `3`) sit flush to one right
  edge in the view rows, and the session titles fill whatever is left.
- **What happened** — known: no flex-grow (#18). What the parity work added is
  the count of call sites — the view row label, the view row count, the settings
  row label, the strip's VIEWS label, the search field, the session title, and
  the snippet all size themselves in pixels off `panelW` minus every fixed
  sibling. Seven independent subtractions that must agree, in one file, all of
  which have to be re-derived by hand when an inset changes. Two of them
  additionally need a "drop this column rather than overflow" branch, because a
  NoWrap row that overflows warns and runs `solve_violations` EVERY FRAME.
- **Cost** — ~40 lines of width arithmetic and its explanatory comments, and
  every design change to the sidebar's insets is a seven-place edit.
- **Class** — `TEDIOUS`
- **Gap filed?** — #18 already; this is evidence for its severity.

### 16. The test harness can confirm a rect but never report one

- **What I wanted** — the y of a rendered row, to find out why the pitch was
  wrong.
- **What happened** — `assert_ui_text "…" y=999` does not fail with "got 315";
  the assertion RETRIES for 30 frames and then reports a timeout with no actual
  value. So the harness can only verify a number you already know. I read the
  row positions off a PNG with a Python script instead — which works, but means
  the answer to "where did this land" is only available to whoever is holding a
  screenshot.
- **Cost** — ~25 minutes, and a scratch probe script that has to exist because
  the harness will not print a rect.
- **Class** — `FOOTGUN`
- **Gap filed?** — no. One line in the harness (print the actual value on a
  property mismatch, as it presumably already does for a non-timeout failure)
  would remove the whole detour.

### 17. Two of the five surfaces in the sidebar had no token, and one had a wrong one

- **What I wanted** — the section-header strip (`#22222D`) and the hairline
  under the views (`#2A2A39`), both measured.
- **What happened** — this is a hanabi note rather than an afterhours one, but
  it cost the same kind of time: `theme::border()` and `theme::panel_bg_2()`
  were the closest existing tokens and both are used for other things, so the
  first version of the strip was painted with the search-field colour and the
  rule with the input-border colour. They looked right and meant nothing. The
  palette work landing mid-flight added `section_header_bg()` and `divider()`,
  and the fix was to re-point three call sites.
- **Cost** — one merge conflict and three re-pointed call sites. The lesson is
  that a measured design needs its own named surfaces BEFORE the widgets are
  written, or every widget invents one.
- **Class** — `TEDIOUS`
- **Gap filed?** — no.

## Composer strip (feat/vis-composer)

### 3. `with_roundness(0.5f)` is a QUARTER-round corner, not a pill

- **What I wanted** — Puffin's chips are capsules: 18px tall, 9px corners.
- **What happened** — the chips came out with ~4.5px corners. `roundness` is
  documented as a 0..1 fraction, and 0.5 reads like "half way to round"; it
  isn't. `resolve_roundness()` is `2 * radius_px / short_side`, so the fraction
  is of the *diameter*: a capsule is **1.0**, and 0.5 is a quarter of the short
  side. Every chip and pill in this app was written `with_roundness(0.5f)`
  believing it meant stadium — the send button's own comment in the source said
  "0.5 made a fully-rounded lozenge", which is how a wrong mental model gets
  written down as a fact and outlives the person who had it.
- **Cost** — 4.5px per corner on 3 chips, wrong for however long they have
  existed, and found only because a reference photograph disagreed. Fixed by
  switching to `with_corner_radius(9.0f)`, which takes pixels and means what it
  says.
- **Class** — `FOOTGUN`
- **Gap filed?** — no. The pixel API already exists and is correct; the fix is
  to stop offering the fraction, or to name it `with_roundness_fraction`. Worth
  a deprecation, not a gap.

### 4. `mk(parent, index)` looks like an ordering index and is an identity hash

- **What I wanted** — to move the composer's meter/chips row from below the
  input to above it. In a retained tree that is a reparent; in flexbox it is one
  `order:` property.
- **What happened** — child order is *creation* order. The `index` argument to
  `mk()` is only fed into a hash (with the source file, line and column) to give
  the element a stable identity across frames; it does not order anything. So
  moving a row up the strip means physically moving its source code up the
  function — 229 lines, past everything it was written after, which is how a
  variable declared beside the send button (`steerMode`) ends up needing to be
  hoisted 600 lines because a caption that reads it moved.
- **Cost** — a 229-line pure code move inside one function, one hoisted
  variable, and one brace-level bug I introduced doing it by script (the caption
  block landed inside `if (canSend && app.openSession)`, which would have hidden
  the read-only notice on exactly the backend it exists to explain). Caught by
  reading the diff, not by the compiler or the tests.
- **Class** — `TEDIOUS`, with a `FOOTGUN` edge: the parameter's name invites
  precisely the wrong belief.
- **Gap filed?** — no. An `order` on `ComponentConfig`, or sorting children by
  the `mk` index, would remove the whole category.

### 5. An icon-only button has no icon path that takes a vector

- **What I wanted** — Puffin's send button: a 19px circle with an up-arrow and
  no text.
- **What happened** — the circle itself was easy once I stopped using the
  fraction API (`with_corner_radius(9.5f)` on a 19x19 box). The arrow was not.
  `ComponentConfig` has `with_icon_texture` / `with_icon_source_rect`, which
  want an atlas cell; there is no "draw this callback as my icon". Typing the
  glyph is out — Roboto has no U+2191 and paints nothing (gap #48). So the
  button carries `.with_label("")` and the arrow is painted in `on_draw_fg`,
  which means the label machinery, the alignment options and the text colour on
  that button are all inert decoration in the call.
- **Cost** — 8 lines of `on_draw_fg` plus a 16-line `hanabi::glyph::arrow_up`
  helper, and a test that can no longer assert what the button says because it
  no longer says anything (`assert_ui` reads x/y/w/h/hidden/text and never a
  pixel — gap #61 — so "is the arrow actually painted?" is unassertable and
  falls to the visual diff).
- **Class** — `TEDIOUS`
- **Gap filed?** — no; it is #48 and #61 meeting. Worth one if a third icon
  button hits it.

### 6. Half a gap being fixed makes the other half harder to see

- **What I wanted** — a 45px input with 13px text and a 10px inset, i.e. the
  three properties of Puffin's box.
- **What happened** — gap #17 says `text_input` derives its font size from the
  field height and ignores `with_font_size`. That is no longer true: the widget
  now honours an explicitly-set font size. The padding beside it, computed two
  lines later in the same block from the same height, still does not — and
  because the font size now works, the padding failing reads as a bug in your
  own code rather than a known gap. I lost time measuring text insets before I
  went back to the source.
- **Cost** — ~15 minutes, and gap #17's text in this repo is now half wrong,
  which is its own hazard.
- **Class** — `FOOTGUN`
- **Gap filed?** — yes, `#65`, which supersedes half of #17.

### 7. A composer cannot reach the bottom of the window (not afterhours' fault)

- **What I wanted** — Puffin's composer runs to the window's last pixel; its
  version string and icons live inside the SIDEBAR's bottom, not in a bar across
  the app.
- **What happened** — hanabi's status bar is full-width and sits under both
  panes, so the composer strip stops 26px short and every element in it is 26px
  higher than Puffin's. Nothing in the library caused this and nothing in the
  composer can fix it: the bar belongs to the shell.
- **Cost** — the composer's like-for-like score against the reference at the
  SAME absolute coordinates is 11.31% structural, against 6.99% when each strip
  is compared from its own top edge. The whole of that 4.3-point gap is the
  26px offset.
- **Class** — not a library finding. Recorded here so the next person does not
  spend the afternoon chasing it in the composer.

---

## Tab bar + transcript bubbles

Reference: `ref/01_home.png`. Everything below came out of matching two things —
the main pane's tab strip and the two message bubbles — against measured
geometry rather than drawing them freehand.

### 3. Nothing will tell you how tall the thing you just drew came out

- **What I wanted** — a virtualized transcript whose spacers match its items,
  while changing the bubbles' padding and corners.
- **What happened** — the height an element resolves to is not readable in any
  form the app can hold against its own measure pass, so the two are kept in
  step by hand. I wrote a probe to correlate them (`src/ui/measure_probe.h`,
  `HANABI_PROBE_MEASURE=1`, ~90 lines, ~45 comparisons per frame). It found a
  **pre-existing 3px drift on every user message** (`kUserPadV = 14` in the
  measure vs `8 + 9` padding in the draw) that had been shipping silently, and
  a **flat +1px per turn** that is not in the app's arithmetic at all — the
  parts sum to 59, the engine resolves 60.
- **Cost** — 90 lines of probe; a 3px/message bug found; 1px/turn residual
  left unexplained rather than papered over (100px of scroll error on a
  100-message thread).
- **Class** — `WORKAROUND`
- **Gap filed?** — #64.

### 4. An explicit `pixels(h)` on a wrapped label is a hint, not a height

- **What I wanted** — to prove the probe above was not vacuous, by breaking the
  draw on purpose (`+1.0f` on the drawn segment height).
- **What happened** — the probe caught it exactly where the injected pixel went
  (the `richbody` key went from 0 drifts to 46, all `+1.00`), so the probe
  works. But the drawn TURN did not grow: still 108. The line element carries
  `pixels(segH)`, segH grew by one, and the resolved element did not — a
  wrapped label's height comes from its text and the size you hand it does not
  win.
- **Cost** — one build cycle to learn it; it also means the app's whole
  measure/draw discipline governs only the SPACERS, never the text elements.
- **Class** — `FOOTGUN`
- **Gap filed?** — recorded inside #64 as evidence.

### 5. A bubble cannot hug its own text

- **What I wanted** — the reference's user bubble: as wide as its longest
  wrapped line plus 13px either side, right-aligned.
- **What happened** — no content-sizing for a wrapped label, so the app wraps
  the text itself, measures every line with the active font, takes the widest
  and sets the box. Then: a label's text is inset horizontally by an amount
  nothing exposes (wrap width = rect − ~10px, first glyph ~6px in), so a tab
  padded 12 draws its title 18 in. The 13px reference gap is coded as a 7px
  padding plus a `kLabelInsetX = 6.0f` fudge measured off a screenshot.
- **Cost** — ~25 lines (`user_box()`), plus one to-the-pixel constant that is
  right for Roboto at 13px and silently wrong for the Hyperlegible setting.
- **Class** — `WORKAROUND`
- **Gap filed?** — #65.

### 6. Create-then-configure quietly does nothing

- **What I wanted** — restore pinned tabs: open each persisted tab, then mark
  the pinned ones.
- **What happened** — an entity created this frame is not yet in the collection
  `getEntityForID` searches, so the loop resolved nothing and pinned nothing.
  No error; the symptom was a missing 10x12px glyph. `pinned` had to become a
  parameter of the creation call.
- **Cost** — one build cycle (~5 min) to see the flag was not arriving. The
  "focus the persisted active tab" loop directly below it has the same shape
  and is presumably equally inert.
- **Class** — `WORKAROUND`
- **Gap filed?** — #66.

### 7. A drawn mark is invisible to every assertion

- **What I wanted** — the reference's pin glyph before a pinned tab's title,
  and a circular avatar left of the user bubble.
- **What happened** — as gap #48 says, the font drops a pushpin codepoint and
  paints nothing, so both are shapes drawn in `on_draw_fg`
  (`hanabi::glyph::pin`, `draw_user_avatar`). Anything drawn that way is
  invisible to `assert_ui`, to `expect_text` and to the visible-text registry,
  so neither mark can be asserted in a scripted test — the only evidence they
  render is a screenshot.
- **Cost** — ~30 lines of drawing for two marks totalling ~500 px², and two
  visual features with no test coverage.
- **Class** — `TEDIOUS` (the drawing) + `IMPOSSIBLE` (asserting it) — the
  second half is already gap #61's family.
- **Gap filed?** — no; #48 and #61 between them cover it.

### 8. Moving one strip by 29px broke three tests that are not about layout

- **What I wanted** — the reference's tab strip is 67px tall with 34px tabs at
  its bottom edge, against hanabi's 38.
- **What happened** — the transcript moved down, and three scripted tests that
  click a word, a link and a card BY COORDINATE failed. They are coordinate
  clicks because the harness cannot name a piece of text or a byte range
  (gaps #51, #55), so tests about selection and hit-testing are pinned to
  pixels and any layout change breaks them.
- **Cost** — 6 coordinates re-measured off screenshots. And because the app's
  harness never registered `dump_ui`, the way to read a real rect was to
  `assert_ui <name> y=-1` and read the number back out of the failure message.
- **Class** — `TEDIOUS`
- **Gap filed?** — no; #51/#55 already say it. The new part is the price: a
  layout change costs a screenshot-measuring session per pinned test.

---

## Content / mock fixture — porting the reference catalog row for row

Nothing in this section is an afterhours finding. They are all findings about
`api::SessionSummary`, the row model every sidebar row is drawn from: the
reference client states things about a conversation that hanabi's model has no
field for, so the port had to leave them unsaid. That is worth writing down
precisely because the other four areas are being judged against a list whose
words now match — anything still different in the list is one of these.

### 1. The row has no child count, so the reference's right-hand column cannot exist

- **What I wanted** — six of the twenty reference rows carry a sub-agent count
  on the right: `1` on four of them and `1/3` on one (one of three shard
  workers still running).
- **What happened** — `api::SessionSummary` has no children field at all.
  `Session::sub_agents` exists, but it hangs off the FULL transcript, which the
  sidebar never fetches, and `types.h` says out loud that sub-agents are
  "Visualized ONLY in the transcript sub-agent panel (never the sidebar)". So
  the count is not merely unstyled, it is unreachable from a list row. The
  `1/3` form is a second thing again: it is *running of total*, which needs
  per-child state, not a number.
- **Cost** — left unmatched, deliberately. I did seed the sub-agents onto the
  Session objects so the data is as faithful as the model allows, but six rows
  will read as missing a column until the row model can carry one. Adding a
  count to the fixture some other way would have been a fake field.
- **Class** — `IMPOSSIBLE` (hanabi model)
- **Gap filed?** — no; this is hanabi's own model, not the library's.

### 2. "It is running" and "it says it is working" are one state

- **What I wanted** — the reference draws four rows with a running glyph
  (a live process) and two with a filled dot (the agent's own testimony that it
  is working). Sampled: `(115,192,162)` and `(151,190,250)` — two families.
- **What happened** — `ThreadState` has exactly one `Running`. Both kinds of
  row are honestly `Running` and both get the same ring, so six rows collapse
  to one glyph where the reference has two.
- **Cost** — two rows visibly wrong; no honest way to split them.
- **Class** — `IMPOSSIBLE` (hanabi model)

### 3. There is no failure state, so failures land in Blocked

- **What I wanted** — three reference rows are failures: two red ✕ (a run that
  died) and one red ● (an outcome that failed), all `(212,88,93)`.
- **What happened** — neither `ThreadState` nor `ThreadTag` has a failure
  member. The nearest honest thing is `Blocked` — those rows do want you — and
  `Blocked` draws a red triangle, which is at least the right colour family.
- **Cost** — Blocked reaches the reference's count of 6 by a different route
  than the reference does (3 blocked + 3 failed, versus the reference's own
  mix). The number matches; the reasoning behind it does not. Anyone reading
  hanabi's Blocked view is being shown three failures filed as blockers.
- **Class** — `WORKAROUND` (hanabi model)

### 4. "Automated" is guessed from the title string, and a fixture cannot opt out

- **What I wanted** — the row `kicker-tick` drawn as a running row, which is
  what the reference does, because the reference is TOLD the state.
- **What happened** — `SidebarSystem::is_automated()` matches a `-tick` suffix
  or a `Schedule:` prefix on the TITLE and swaps in a faint repeat glyph plus a
  dimmed title. The row is `ThreadState::Running` in the fixture and still
  renders as a muted cron job, because the heuristic outranks the state. The
  only way for a fixture to opt out is to rename the row — which is exactly
  what a content port may not do.
- **Cost** — one of twenty rows has the wrong glyph and the wrong text weight,
  and it is unfixable from the fixture.
- **Class** — `FOOTGUN` (hanabi model: no `kind`/`automated` field, so a
  display decision is taken from a string)

### 5. Settled and done look different here and identical there

- **What I wanted** — the reference draws its idle rows and its done row with
  the same `(140,140,164)` 8px dot.
- **What happened** — hanabi draws `ThreadTag::Done` as an 8px slate dot and an
  untagged calm row as a 2.4px faint one. Both readings are defensible; they
  are just not the same reading, so four rows differ from their neighbours in a
  way the reference's do not.
- **Class** — `TEDIOUS` (a real difference, not a defect)

### 6. The reference's fixture has no pinned, archived, parked or foldered row

- **What happened** — the reference's row builder has no field for any of the
  four. Porting it therefore empties hanabi's Starred and Archived views and
  removes the only mock coverage of `Parked` and of folders. Four unit
  assertions that asserted "the mock exercises these" had to move onto
  constructed summaries.
- **Cost** — the demo app no longer shows a starred or archived thread until
  the user makes one. That is faithful to the reference and a real loss to the
  fixture; both are true.
- **Class** — `TEDIOUS`

### 7. A "FOLDERS" heading rendered over zero folders

- **What happened** — the sidebar emitted its FOLDERS section header
  unconditionally. The reference's fixture is entirely folderless, so the port
  left a heading with nothing under it, pushing every row of the list down by
  28px. One-line guard (`if (!folders.empty())`).
- **Worth knowing for whoever owns sidebar geometry**: removing it moved the
  whole list band UP 28px, so the list now starts at y=267 instead of 295
  against the reference's first row at 315. The comparison's `search` region
  reads 45.6% instead of 11.7% purely because of that shift — that band holds
  Puffin's search box and hanabi's list rows either way, so the number is
  measuring which rows happen to sit there, not a design difference.
- **Class** — `FOOTGUN` (hanabi's own sidebar)

### 8. The list barely scrolls once there are no folders

- **What happened** — the headerless catch-all caps itself at roughly one
  viewport of rows and puts the rest behind "Show N more…", so with a flat
  20-row list in a 760px window it renders 18 rows and there is nothing to
  scroll past. The scroll regression test that exercised this used to work only
  because collapsed folder headers added height the cap does not count.
- **Cost** — that test now has to click the expander before it can scroll.
- **Class** — `FOOTGUN` (hanabi's own sidebar)

### 9. The reference's attention glyph is not amber

- Three of us were briefed that the `!` rows are "amber". Sampled off the
  frozen reference at every one of the six rows, the glyph is `(159,201,255)`
  — a light blue, a shade brighter than the working dot's `(151,190,250)`.
  The only amber-adjacent thing in that column is nothing at all. Recorded so
  nobody spends an afternoon making a red triangle orange.

---

## Tab bar (round 2) — feat/vis-tabs-round2

Region went 25.30% -> 3.90%. The headline finding is that most of 25.3% was not
a design gap at all: the capture opened ONE tab where the reference has TWO,
both pinned. Fixing the settings blob (`open_tabs` + `pinned_tabs`, active =
the second) took the region to 5.54% with no code change. Everything below is
what the remaining 1.6 points cost.

### Per-corner rounding is index-mirrored — FOOTGUN (gap #74)

Wanted: the reference's folder-tab shape, top corners rounded and bottom
corners square so the tab stands on the strip hairline. `RoundedCorners` numbers
its corners TOP_LEFT=0..BOTTOM_RIGHT=3; the sokol backend reads the same bitset
as 3=TL, 2=TR, 1=BL, 0=BR. Every corner you name is applied to its diagonal
opposite. `top_round()` is separately wrong on its own terms — it sets
BOTTOM_RIGHT round too — so the two faults compose into a lopsided bracket that
reads as a rasterizer glitch.

COST: a previous round diagnosed that bracket as "the outline/edge path
glitches on sharp bottom corners", gave up, and shipped fully-rounded pills —
the tab shape was wrong for a whole round. Rediscovery: ~35 min reading the
backend. Workaround: name the BOTTOM two corners to round the TOP two.

### Boxes rasterize 1px bigger and 1px up-left — WORKAROUND (gap #73)

Wanted: a tab whose outer edge lands on the measured 220x34 at (284,32).
A `w x h` box at `(x,y)` paints `(w+1) x (h+1)` at `(x-1,y-1)`.

COST: 416 diff pixels on the single row y=31 = 0.65 points of the region, from
one off-by-one. Compensated with a named `kRasterGrow` constant; hit-testing and
the drawn rect now disagree by a pixel on every edge.

### Text carries a hardcoded 5px margin — WORKAROUND (gap #75)

Wanted: title 26px from the tab's left edge. `draw_text_in_rect` hardcodes
`margin_px{5.f, 5.f}`; no config field reaches it.

COST: 7px drift on every tab title, ~25 min to find. Every left pad in the file
is now authored as `design_inset - kTextMarginPx`, so the number in the source
is not the number in the design. Same renderer-only inset #69 names from the
wrapped-label side.

### An unpadded child is not unpadded — FOOTGUN (gap #76)

Wanted: a label child that fills its parent's content box. When every padding
side is `Dim::None`, `component_init` substitutes `Spacing::sm` =
`screen_pct(0.02f)` — a fraction of the WINDOW.

COST: ~15 min chasing the wrong suspect above, plus a latent resize bug in every
nested element that never set padding. Sibling of #71. Zeroing needs all four
sides given explicitly.

### No bold face, and the weight API no-ops silently — IMPOSSIBLE (gap #77)

Wanted: the reference's active tab title, which is bold white where the
inactive ones are regular — weight is the whole active-tab signal.
`with_font_weight` resolves `"<font>@<weight>"` and falls back to the base font
when unregistered; hanabi ships three Regular faces and no Bold, so the call
compiles, runs, logs nothing, and draws regular text. No synthetic bold, no
stroke weight to fake it with.

COST: unconvergeable. The active tab's text zone is 1072 diff px = 1.68 points
of the region and cannot be driven down. Same silent-fallback shape as #48, one
level up: #48 drops a GLYPH, this drops a WEIGHT. Escaping it means shipping a
Bold TTF, which is an asset decision, so it was left alone.

### Cited, not re-filed

#61 — nothing in the tab-bar change is testable. The outline moved from
`theme::border()` to `theme::divider()` and the bottom corners went square;
a test can assert the tab's rect and its label, neither of which changed.
#68 — the 1px raster error above would have been a one-line assertion if an
element could report the height it actually came out at.

### Where the remaining 3.90% is

2481 diff pixels: 897 are the row y=0, the macOS window bevel around the
captured Puffin window, which hanabi's headless capture has no equivalent of;
1383 are the two tab titles, which are different STRINGS (Puffin's fixture says
"TODO" / "Oncall triage tick", hanabi's mock has neither). That leaves ~200
pixels of real design difference — the `+` glyph is a hair thin, and the pin's
antialiasing. The region is at its floor short of renaming mock sessions to
match Puffin's fixture.

---

## Session list (feat/vis-list)

Region 19.21% -> 16.58%. Row pitch is the headline: it was drifting 2px a row
and is now exact.

### Grid snapping: the global lever was never actually tested — WORKAROUND (gap #71, now resolved)

Wanted: Puffin's measured 32px session-row pitch, at the 949-tall window the
reference was shot in. hanabi asks for `pixels(32)` and got 30, drifting 33.5px
by row 18.

#71 diagnosed this correctly and then closed with "the only lever is
`set_grid_snapping(false)` in preload.cpp, which is global; it moves every panel
in the window by up to 5px, so it is not a change a single component can make
while four others are being matched in parallel." That last clause is an
assumption, and it is wrong. Flipping it and re-scoring every region:

| region | snap on | snap off |
|---|---|---|
| views | 8.89% | 8.85% |
| search | 8.02% | 7.59% |
| list | 19.21% | 16.50% |
| footer | 5.26% | 5.26% |
| tabbar | 25.30% | 25.30% |
| main | 4.75% | 4.74% |
| STRUCTURAL | 10.71% | 9.84% |

Nothing regressed. Four regions improved. The 5px moves are real but they are
moves AWAY from a grid nobody designed to and TOWARD the pixel numbers already
written in the code, so every region that had a measured number in it got
closer. It is off now, and pitch is exactly 32 with row centres inside 1px of
the reference on all 18 rows.

One caveat the region scorer does NOT show: elements really do move. At
1100x760 the transcript body rose 12px and a link's ink bbox rose 8px, which
broke the two coordinate-addressed transcript tests (`select_word_and_line`,
`tracker_links`). Both say in their own comments to re-measure rather than
nudge, so both were re-measured; the suite's failure set is back to the nine
that fail on main. Anyone else flipping a global layout default should expect
the same and should grep for `assert_ui_text .* y=` and bare `click <x> <y>`
first — there are only three such files in tests/ui, which is why this was
cheap.

COST: the diagnosis was free (it was already filed); the retest was 2 rebuilds
and ~15 min, plus ~20 min re-measuring the two coordinate tests. What it cost
the PROJECT is that the largest single error in this region sat filed-and-
unfixed because the escape was assumed to be expensive without being run once.
Worth generalising: in this codebase a global styling default is one line and
two minutes, and the per-region scorer already exists to prove what it did.

### draw_circle_v truncates its centre to int — WORKAROUND (gap #78)

Wanted: Puffin's 7px resting dot, centred on x=15.5. `draw_circle_v` takes a
`Vector2Type` of floats and immediately does
`draw_circle(static_cast<int>(center.x), static_cast<int>(center.y), ...)`. At
r=3.7 with 32 unantialiased segments the half-pixel loss is visible: the dot
lands a pixel left of the glyph column's centre and reads as a lumpy polygon
next to Puffin's clean circle.

Workaround, 1 line: `draw_ring_segment(cx, cy, 0.0f, r, 0, 360, 28, c)` — same
shape, float centre, because the ring path never casts. Two primitives for one
shape, and the one with the obvious name is the broken one.

COST: ~40 px per dot across 2 rows plus the 20 rows a real backend would show;
20 min to find, since "the dot looks wrong" reads as a radius bug, not a cast.

### A label cannot be told to fit a width — WORKAROUND (gap #79)

Wanted: a row title that ellipsizes exactly where Puffin's does. afterhours
hard-clips a label at its widget width mid-glyph and offers no truncation, so
the caller must ellipsize before handing the string over — and the caller only
has a character count. hanabi's existing budget was
`chars = (width - pad) / 6.1f`, an average advance calibrated to Roboto at
12.5px, so changing the font size to the measured 16.5 silently clipped four
titles a word early ("coordinating 3 shard worke…").

Workaround, 18 lines: measure with `theme::text_px` (which wraps
`measure_text_internal`) and shrink on UTF-8 boundaries until it fits. Correct
at any size and any face — and it is the third place in this file that
re-derives text metrics the layout engine already has.

COST: 18 lines, ~25 min, and one wrong-looking screenshot that cost a rebuild
before the cause was clear.

### The parity metric rewards a font that is too small — TEDIOUS (no gap; this is our tooling)

hanabi's row title was 12.5px against Puffin's ~16.5 — measurably, from ink
bbox widths (102/125/99/148 ref vs 81/97/78/114 hanabi, ratio 1.27) and from
ascender-to-descender height (13 vs 10). Setting it to the correct 16.5 made
`compare.py` WORSE, by 0.14 points.

Why: the two renderers use different faces, so glyph ink overlaps only 53% even
when size and colour are exact. A too-small font simply paints less ink to be
wrong with. The metric cannot reward correct typography until the typeface
matches, and it will keep quietly paying for shrinking text.

Shipped 16.5 anyway. Worth knowing before someone "optimises" a region by
making its text smaller.

### Cited, not re-filed

#77 — no bold face, and the weight API no-ops silently. Same wall from the
sidebar side: Puffin's row titles are semibold near-white, hanabi's ink is 17%
lighter than the reference's at the same size and position. Of the 16.58% left
in this region, 6.11 points is title ink that does not overlap the reference's,
and weight is the biggest single component of it. It ends where #77 ends: a
Bold TTF is an asset decision.

### Where the remaining 16.58% is

- **5.18 points is one row band** — hanabi's mock puts session `t2` in the open
  tab and highlights its row; Puffin's reference fixture has a different session
  open, so its list has no selected row at all. A fixture difference owned by
  the tab-bar work, not a design difference. Excluding it the region is 11.39%.
- **1.08 points is the count column.** Puffin right-aligns a count on 7 of 19
  rows (`1`, and `1/3` for a session with 3 sub-agents of which 1 is done — the
  mock data matches exactly). hanabi draws nothing: `api::SessionSummary` has no
  sub-agent count, and the sub-agent list lives on the loaded `Session`. Real,
  cheap-ish, and a change to a shared type + 20 mock call sites, which is not a
  thing to land while four agents are in the same files.
- **6.11 points is typeface and weight** (above, #77).
- **1.05 points is glyphs**, and most of that is unreachable: Puffin draws seven
  distinct row markers (arc, blue dot, bang, cross, grey dot, red dot, chevron)
  and hanabi's model carries five `(state, tag)` pairs across these 19 rows. The
  mapping is not a function — six rows are all `Attention/Blocked` in hanabi and
  Puffin gives three of them a bang, two a cross and one a red dot. Four shapes
  is the ceiling on this data, and it is reached.

## Transcript pane — the furniture around the messages (header, per-turn times, anchoring)

Scope was the pane's own chrome, not the bubbles: delete the title header, drop
the per-turn relative times, and stop bottom-anchoring a short thread. `main`
region went 4.75% -> 4.55%.

### 10. The header removal itself cost nothing; proving it cost four rebuilds

- **What I wanted** — to know whether deleting the transcript header changed
  what the pane MEASURES, which is the thing todo.md warns desyncs the
  virtualization spacers.
- **What the library did** — nothing at all: afterhours will not tell you the
  height it resolved for anything, so the only witness is hanabi's own probe
  (`HANABI_PROBE_MEASURE=1`) reading `rect()` off elements you named in advance.
  Answering "is this drift mine or pre-existing" is therefore a build of `main`,
  a build of the change, and a build per hypothesis in between.
- **The answer, for whoever comes next** — header removal and anchor removal
  introduce **zero** drift. `main`: 182 comparisons, 45 drifts, all
  `turn#2 measured 89.00 drew 88.00 (-1.00)`. Same numbers with the header and
  the anchor gone. The drift only appears when the meta row is suppressed.
- **Cost** — 4 full rebuilds at ~2 min each (a one-line edit to
  `main_pane_system.h` recompiles all of `main.o`), plus 2 suite runs at ~13 min
  to separate my failures from main's nine.
- **Class** — `TEDIOUS`

### 11. A row that is 15+3 constants tall occupies 20px, and you cannot ask why

- **What I wanted** — measure == draw after suppressing the (now empty) meta row
  above an assistant turn.
- **What the library did** — `bubble_height` charges `kAuthorH + kAuthorGap` =
  18 for that row; removing it takes **20** off the drawn turn. Measured off two
  screenshots: the first assistant bubble's fill starts at y=189 with the row and
  y=169 without. `flex_gap` is 0 on that column, so the extra 2px is not a gap I
  set.
- **Why it stayed unexplained** — the obvious next step is to read the resolved
  heights of the turn's children and find the one that does not add up.
  `UIComponent::children` is cleared every frame before app code runs, so a
  resolved subtree cannot be walked at all — see `afterhours_gaps.md` **#73**.
- **What it is, precisely** — not a new bug: turns with no meta row were ALREADY
  measuring short on `main` (`turn#2`, -1.00, on 45 of 182 comparisons). My
  change moves one more turn onto that path, where it reads -2.00. A pre-existing
  over-measure of the no-meta-row case, now exercised twice instead of once.
  Instance of gap **#68**; not papered over with a fudge constant.
- **Cost** — 2px on one turn; drifting comparisons 45/182 -> 90/182.
- **Class** — `FOOTGUN`
- **Vacuity check, so the numbers above mean something** — `+1.0f` injected into
  the measure moved every reported drift by exactly -1.00 (turn#1 -2.00 -> -3.00,
  turn#2 -1.00 -> -2.00) and both measured values by +1. The probe is not vacuous.

### 12. Moving the pane's content broke three tests that address it by pixel

- **What I wanted** — a green suite after the content moved up.
- **What the library did** — there is still no way to address a text run or ask
  where one landed (gaps #47, #51), so `message_copy_on_hover`,
  `select_word_and_line` and `tracker_links` all reach into the transcript by raw
  coordinate. All three broke, and each had to be re-measured by rendering the
  thread at the test's own window size and scanning the PNG for text rows.
- **Cost** — 3 tests, 6 coordinates, 2 screenshots. The thread in `t2` moved
  273px up, the one in `t1` 321px — the shift is per-thread, so one delta does
  not fix them all.
- **Class** — `TEDIOUS`

### 13. `kHeaderH` was a named constant in one place and a bare `62.0f` in another

- The transcript's overlays position themselves below the header. Two of them
  read `kHeaderH`; the load-older pill hard-coded `62.0f + 6.0f`. Setting the
  constant to 0 left that pill floating 62px down over the first message. Pure
  hanabi, one line, but it is the exact failure mode a named constant is for.
- **Class** — `FOOTGUN` (hanabi's own)

## The resting focus ring (feat/focus-visible)

### The app wore a blue box before anyone touched it — IMPOSSIBLE-without-a-shim (gap #83)

- **What I wanted** — the launch screenshot to look like Puffin's launch
  screenshot. Puffin shows a selection fill on the current view and nothing
  else.
- **What the library did** — parked focus on the first focusable widget
  (`try_to_grab`, every frame, "whichever widget happens to be first") and drew
  the accent-blue ring around it. There is no `:focus-visible`: afterhours knows
  *what* has focus and never *why*, so it cannot tell "the user tabbed here"
  from "this was first in the tree".
- **The trap that cost the most time** — `FocusSource` exists, and it already
  names `Grab` vs `Pointer` vs `Explicit`. It reads like the answer. It is reset
  to `Grab` at the top of every frame, so by render time it describes this
  frame's claim, not the focus's history. Two reads of `context.h` to be sure.
- **Cost** — a 30-line shim in hanabi (`src/ui/focus_visible.h` + one system
  registered ahead of every UI system) reimplementing a browser heuristic, plus
  a test-only audit hook, because `assert_ui` can read a label and never an
  outline (gap #61) and the ring is the entire subject.
- **Worth** — 5 sidebar-wide rows at ~99% wrong each; VIEWS 8.85% → 7.66%,
  overall 8.18% → 7.99%.
- **Class** — `WORKAROUND`, and the kind that every other afterhours app will
  either write again or ship without.

---

## Bold / semibold face (feat/bold-face)

Testing whether shipping a bold TTF closes the title-ink gap. Short answer: the
weighted-resolution path works, and the bold buys nothing on the metric. The
friction is mostly in the measuring, not the library.

### 1. `compare.py --regions` scores the RAW mask, but the header tells you to drive STRUCTURAL

- **What I wanted** — to know whether "list 16.58%" is the number I should be
  moving, since the tool prints `STRUCTURAL ... this is the one to drive`
  directly above it.
- **What happened** — the region table is computed from `mask`, the RAW
  per-pixel diff, never from the blurred structural pair. So every per-region
  number quoted across this whole workstream — the 16.58 list, the 3.90 tab bar,
  the "6.1 of 16.6" that motivated this task — carries the ~10% retina
  downsample floor the header explicitly says no design change can get under.
  The two numbers do not even agree on direction: SF Semibold at 15.5px scores
  list 16.02 (worse than the 15.83 of SF Regular) while its STRUCTURAL is 7.99
  (better than 8.04). Pick a different mask and you make the opposite decision.
- **Cost** — every region figure in this report has to be quoted twice, and the
  regression I was sent to explain is partly an artifact of which mask got used.
- **Class** — `FOOTGUN` (the tooling's own)
- **Gap filed?** — no, this is `~/w/vis/compare.py`, not afterhours. Two lines:
  build a second mask from the blurred pair and print both columns.

### 2. The metric pays you to draw too little ink

- **What I wanted** — a number that says whether a semibold looks more like the
  reference than Regular does.
- **What happened** — the diff counts non-overlapping pixels on both sides, so a
  face that under-inks is cheap to be wrong with. Over the 18 list-title rows:
  the reference has 9442 ink px; SF Semibold at 15.5 puts down 9253 (−2.0%) and
  overlaps 53.9% of the reference's ink; SF Regular at 15.5 puts down 6562
  (−30.5%) and overlaps 34.8%. **The region score prefers the second one**
  (15.83 vs 16.02). The arm that draws a third less ink than the reference wins.
  Ranking font decisions on this metric will keep choosing the lighter, wronger
  face, and it very nearly did here.
- **Cost** — the headline finding of this task is "the metric disagrees with the
  picture", which took an ink-density/overlap probe of my own to see at all.
- **Class** — `FOOTGUN` (the tooling's own)
- **Gap filed?** — no. Suggest `compare.py` grow an ink-overlap mode
  (intersection / union over bright pixels) for text-dense regions.

### 3. No way to score a sub-band, so "how much of the list is titles?" needs a private script

- **What I wanted** — the title column's contribution to the list region.
- **What happened** — `--regions` has a fixed dict of seven fraction boxes and
  no flag to add one. Had to reimplement `diff_mask` in a throwaway script to
  learn that the title column is 14.83 of the list's 16.58 points. That is the
  single most useful number in this investigation and the tool cannot produce it.
- **Cost** — ~30 lines of duplicated masking logic, and a second implementation
  of the tolerance constant that can drift from `compare.py`'s.
- **Class** — `TEDIOUS`
- **Gap filed?** — no. `--region name=x0,y0,x1,y1` would do it.

### 4. Measured Regular, drew SemiBold — the ellipsizer cannot see the weight

- **What I wanted** — semibold titles ellipsized to their column.
- **What happened** — `theme::text_px` -> `measure_text_internal(text, size)`
  measures the backend's *global* active font, which is the base face; the
  renderer swaps the weighted variant in and back out around the draw, so app
  code can never observe it. Titles were ellipsized at Regular widths and the
  wider SemiBold glyphs ran past the column and hard-clipped mid-glyph: stray
  ink past x=248 went from 3 px (Regular) to 116-132 px (SemiBold). Invisible
  until a weight variant is registered, then every measured string in the app is
  wrong at once.
- **Cost** — not worked around; it is a precondition for shipping a bold at all.
- **Class** — `WORKAROUND`
- **Gap filed?** — **#82**.

### 5. Half of gap #77 was already fixed upstream and nobody re-read it

- **What I wanted** — to confirm `with_font_weight` "falls back silently".
- **What happened** — it does not. `resolve_weighted` has called `warn_once`
  since upstream 90f8ae8 ("stop two text features failing silently"), which the
  pinned submodule (428047e) includes; the very first headless run printed
  `No font registered for '__default@semibold'`. #77 was filed against an older
  pin, classed `IMPOSSIBLE`, and has been quoted as a blocker since.
- **Cost** — the cheap half of the finding was free and sat unclaimed. Worth a
  habit: re-run the reproduction before quoting a gap older than the last
  submodule bump.
- **Class** — `TEDIOUS`
- **Gap filed?** — #77 corrected in place, and re-classed `WORKAROUND`.

## row-selection agent (feat/no-open-row-highlight) — 2026-08-24

1. **`compare.py --regions` scores the RAW mask, not the structural one.** The
   headline prints both numbers, then the per-region table silently uses RAW.
   "list 16.58%" and "STRUCTURAL 8.18%" are not on the same scale, so a band
   improvement quoted from the table cannot be added to the structural figure.
   Worth one line in the table header.

2. **`ref/01_home.png` has one stray `#2E3A58` pixel in the list band**
   (x=270, y=408, on the sidebar's right edge). A "does the reference contain
   the selection colour anywhere below the views" scan returns 1, not 0, and
   reads as a hit until you print the coordinates. Any future colour-presence
   scan on this reference needs a run-length floor, not `> 0`.

3. **The scripted `.e2e` DSL has no colour assertion.** `assert_ui` /
   `expect_text` cover structure and text; there is no `expect_pixel`. A change
   whose whole content is "this surface is no longer painted" therefore cannot
   be locked by the suite — the only regression gate is the screenshot
   baselines, and those are the thing we are told not to regenerate. A
   `expect_pixel x y r g b [tol]` handler would have made this a one-line test.

4. **`run_ui_tests.sh` ignored my `# settings:` line.** A probe script whose
   header asked for 1180x949 ran at the 1100x760 default, so the first round of
   pixel sampling was aimed at the wrong rows and read "hover paints nothing".
   Diffing the two frames whole (`ImageChops.difference(...).getbbox()`) found
   the band immediately and is the better first move regardless.

5. **Puffin's source is on this box** (`~/kt-ng2w-puffin`) and answers questions
   the frozen screenshot cannot. `HomeSessionList.swift` settled the hover
   question and the "is the open thread highlighted" question in about two
   minutes, with no need to touch the live app. Nothing in `REFERENCE.md`
   mentions it; it should.

---

## The typeface question, settled (no branch — measured and discarded)

The bold-face run left an open lead: it measured SF Regular at 15.5px as worth
0.75 points on the list region, and recommended re-tuning `LIST_ROW` down from
16.5. That measurement was taken against the **RAW** region table, which was the
only one the tool printed at the time. Re-run against STRUCTURAL, the lead
closes: **the current Roboto at 16.5 is already the optimum.**

Nine arms, one binary, `HANABI_UI_FONT` + `HANABI_LIST_ROW_PX` patched in for
the sweep and then discarded:

| arm | overall STRUCT |
|---|---|
| **Roboto 16.5 (shipping)** | **7.39%** |
| SF 15.0 | 7.45% |
| SF 15.5 | 7.44% |
| SF 16.0 | 7.60% |
| SF 16.5 | 7.59% |
| SF 17.0 | 7.55% |
| Roboto 15.5 | 7.56% |
| Roboto 16.0 | 7.51% |
| Roboto 17.0 | 7.60% |

SF is better in every region except the list — search 7.94 → 7.41, main 6.10 →
5.98, tabbar 4.35 → 4.26 — and worse in the list by a full point (14.31 →
15.29), which is 63% of the sidebar's height and swamps the rest.

### Why the list punishes the reference's own typeface

Measured on the ten visible row titles, the reference's string widths and
hanabi's Roboto strings agree to within 1–2px (130/128, 152/150, 126/127,
124/124, 246/245, 231/230). **SF at 15.5 is consistently 10–20px short** on the
same strings (117, 134, 114, 113, 226, 207): fontstash takes SFNS.ttf's default
variable instance, which is not the optical size or weight Puffin renders, and
there is no way to select a named instance. Roboto at 16.5 was fitted to those
widths — it is a compensation fit, and it compensates well.

### What is actually left, and it is not a font

The reference's title column holds **10,125 ink pixels; hanabi's holds 8,180** —
a 19% deficit, on strings whose start, end and vertical extent all match. Puffin
renders semibold through CoreText with macOS stem darkening; fontstash draws
thinner glyphs from a Regular face.

> **Correction, 2026-08-24 (feat/vis-titles).** Two things in the paragraph
> above are wrong, and both were checkable. *"Puffin renders semibold"* — it
> does not: `HomeSessionList.swift:1212` is `.font(PuffinTheme.Font.message)`
> and `PuffinTheme.Font.message = face(Size.message)`, whose weight parameter
> defaults to `.regular`. `messageEmphasis` exists and is used in three places,
> none of them a session row. The deficit is Regular against Regular, so there
> is no heavier face to reach for and CoreText's stem darkening is the whole of
> it. *"strings whose start … match"* — they did not. Every one of the
> nineteen started exactly one pixel left of the reference's, which is a
> silently-ignored `Padding` (gaps #85, #91, #109) and not a rasterizer, and
> which was worth the region's entire remaining headroom. Re-measured with the
> full ink band, the deficit is **11.5%**, uniform across all nineteen rows
> (0.858–0.904). See `## The row titles (feat/vis-titles)`.

But **adding ink makes the score worse, monotonically**: dilating only the title
column takes the list from 14.31% to 16.11 / 17.98 / 19.80 / 21.66 at 25 / 40 /
60 / 100% blend. The ink is not in the same *places* — advances diverge across a
string, so extra weight lands on glyphs that are already a pixel or two out of
register and increases non-overlap on both sides.

**So: the last few points of every text region are a rasterizer difference, not
a typeface or a size or a weight.** No font choice available to us closes it,
the metric actively punishes the change that would look most correct, and the
right call is the one the bold-face run reached from the other direction — ship
a semibold because it is the correct render, never because of a parity number.

- **Class** — `TEDIOUS` (our metric) + `IMPOSSIBLE` (the rasterizer)
- **Gap filed?** — no new one. #82 (cannot measure text at a weight) and #77
  (no bundled bold) already cover the library's half. The rest is CoreText.

---

## sub-agent count column (feat/sidebar-counts) — 2026-08-24

1. **The brief's semantics for `1/3` were wrong, and only the source could say
   so.** The handoff read it as "three sub-agents of which one is done".
   Puffin's `ChildActivity.label(total:running:)` says the opposite: the
   numerator is the RUNNING count, the denominator only appears when some but
   not all are live, and a bare number is ambiguous between "all live" and
   "none live" — the COLOUR resolves it. Two of the seven reference rows are
   only explicable under the real rule. Reading `SessionRowView.swift` and
   `ChildActivity.swift` took five minutes and changed what got built; the
   screenshot alone would have produced a plausible, wrong feature.

2. **The reference client does not have this field at all, which is the whole
   design.** `AgentcloudSessionSummary` carries `parent` — one id — and Puffin
   derives counts by indexing the catalog (`indexChildren`, `childCounts`).
   Nothing is denormalized onto a row. Worth knowing before designing ours:
   hanabi's list type could not take that shape without changing which rows
   the sidebar shows, but the *wire* fact it rests on — every child is its own
   row carrying `parent` — turned out to be exactly what let hanabi's real
   backend fill a count too, instead of shipping a mock-only display.

3. **`docs/visual-parity/ref/01_home.png` cannot be trusted for colour by peak
   pixel, and thin glyphs make that bite.** A count is 3-4px of antialiased
   stroke with no solid interior, so its brightest pixel is nowhere near its
   true colour: the running count peaks at (114,161,243) where the running
   *glyph* on the same row peaks at (154,197,255), and reusing the glyph's
   constant would have been visibly wrong. What works is the RATIO of
   (pixel − background) across samples, which is coverage-independent and was
   consistent to three decimal places across two samples. That gave
   (120,169,255) for live and confirmed the settled colour is exactly the
   existing `kGlyphCalm`. This trick should be in `REFERENCE.md`; every future
   small-text colour match needs it.

4. **afterhours cannot right-align text flush to its box — filed as gap #84.**
   `rendering.h` insets every alignment by a hardcoded `kInset = 5.f` with no
   `ComponentConfig` knob reaching it, so `TextAlignment::Right` means "5px
   shy". The workaround is to LEFT-align in a slot sized to the text plus that
   inset. The cost of not knowing this earlier: **every right-aligned count
   already in hanabi's sidebar has the same 8px error** — the smart-view
   badges land at x=263 where the reference puts them at x=271, and have for
   the whole parity effort. Fixing those is a separate, larger change than
   this branch, but it is now measured and written down.

5. **The "worth 1.08pp" estimate was measured off the count COLUMN, and the
   column is not the digits.** Masking x=[238,278] out of the list region does
   move it 1.03pp — but restricting the diff to the reference's actual count
   ink shows only ~718 differing pixels, ~0.4pp of the list at absolute best,
   and most of that 1.03pp is truncated title tails and the selected row.
   Drawing the counts pixel-correctly recovered 4 of those 718. **A region's
   share of a diff is not the same as what filling that region can win**, and
   for thin text the difference is two orders of magnitude. Future estimates
   for text-sized elements should be made against the ink, not the bounding
   box, or they will keep promising points that are not there.

6. **`make` will not relink after you restore a binary over `output/`.** Doing
   `git stash; make; cp output/hanabi.exe /tmp/base.exe; git stash pop; make`
   silently produces nothing on the second `make` — the .exe is newer than
   every source. It looks exactly like a successful no-op build, and the next
   screenshot is of the OLD binary. `touch src/api/types.h` first. Cost me one
   round of measurements I nearly believed.

---

## Smart-view badges and labels (no branch — done on main)

### 1. The count is a badge, and reading the source said so before the pixels did

- **What I wanted** — to know why the VIEWS region would not go under 8%.
- **What happened** — I had been reading the counts as bare numerals with an
  alignment error. They are not. `SmartViewSidebar.badgeView` draws a
  `tintedPill`: *"the accent as a low-alpha fill, a hairline of the same accent
  as its border, and the digits in that accent rather than white"* — with a
  paragraph of history explaining that a solid pill was tried and reverted.
  The measurement agreed exactly: ring, fill and digit are the SAME hue at
  three coverages, ratio (0.596, 0.778, 1.000) at every sample.
- **Worth** — VIEWS 8.07% → 7.58% for the badge alone, and it is the difference
  between a control that looks designed and one that looks unstyled.
- **Class** — `TEDIOUS` avoided by reading the source first.

### 2. Home's badge had a comment describing it and a `-1` where the number goes

- hanabi's code said *"Home's count is what is WAITING: the blocked rows plus
  the ones done and unread"* and then passed `-1`, which draws nothing. The
  reference's rule is that comment verbatim —
  `[.home: blocked + review, .blocked: blocked, .review: review]` — and the
  reference badges Home with 9 over a Blocked of 6 and a Review of 3.
- The row that is **selected the moment the window opens** was missing its
  badge, and a comment two lines up said what it should be.
- **Class** — `FOOTGUN` (hanabi's own). Second time this pattern has cost us:
  see `kHeaderH` being a named constant in one place and a bare `62.0f` in
  another.

### 3. A `.with_padding(12)` that had never done anything — gap #85

- **What I wanted** — the label 6px further right.
- **What happened** — the row already asked for it, and had done for the whole
  effort. Padding on a label-only div is ignored: `pixels(12)` and `pixels(40)`
  render **byte-identical frames**. The comment beside it did the arithmetic
  (`9 + 16 + 12 = 37`) and shipped 31.
- **Cost** — the six pixels were cheap; the twenty minutes spent not believing
  the measurement, because the code said the opposite, were not.
- **Class** — `FOOTGUN`, filed as gap #85 with a request that it warn rather
  than obey silently.

### 4. The label was two sizes too small and nobody could see it

- The reference's view label is 11px tall over a 49px run for "Blocked";
  hanabi drew 9px over 39px at `theme::type::BODY` (13). The right size is
  16.8 — a 29% error, sitting in plain sight in every screenshot, invisible
  because "a bit small" is not a thing the eye reports and there was no
  measurement that would have caught it. It was **twice the ink** of the badge
  and the alignment put together: VIEWS 7.54% → 6.59%.
- **Class** — `TEDIOUS`, and an argument for measuring type against the
  reference rather than picking from a scale.

---

## Known divergences (feat/vis-divergences) — 2026-08-24

1. **Half the score was the reference's own empty state, and it had been quoted
   for days.** `ref/01_home.png` has thread `6cb2dacc-…` open — a real session
   id, not a `mock-*` fixture — so Puffin's mock backend falls through to
   `MockBackend.swift:936` and draws one line, "No fixture transcript for … yet."
   hanabi draws a full conversation in the same 897x749 viewport. That is 3.18
   of the 7.39 structural points. It is the exact trap `REFERENCE.md` already
   warns about under "Compare LIKE FOR LIKE", one level down: we fixed the
   *tab* state and never checked whether the open tab had anything in it. The
   general lesson is cheap and worth stealing: before scoring a region, look at
   what the REFERENCE has in it, not only at what hanabi has.

2. **The status bar was 3x cheaper than the brief assumed, because someone had
   already half-fixed it.** The premise handed to this branch was "a full-width
   painted bar, ~14 rows of near-100% difference across 1180px, landing in both
   `footer` and `main`". By the time it was measured, `6761336` had already
   narrowed `layout.statusBar` to the main pane, hanabi had grown a sidebar
   footer that mirrors Puffin's, and the bar's fill was `theme::sidebar_bg()` —
   (23,23,35), the identical colour Puffin's empty window paints there, so the
   fill costs literally zero. Real cost: 0.233 points, one hairline row plus
   seven rows of right-cluster text. **Measure the premise before you act on
   it**; three of the four claims in it had aged out inside a day.

3. **Excluding surface makes the rate go UP, and it will be read as a
   regression.** The declared rectangles cover 62.5% of the frame's *area* but
   only 47% of its *difference*, because most of that area is black agreeing
   with black. Take it out and structural goes 7.39% -> 10.37%. The arithmetic
   is right and the number is more honest, but nobody's first reaction is "ah,
   the denominator". `compare.py` now says so in its own output; the same
   sentence is in `REFERENCE.md`. Anyone adding a large mostly-empty rectangle
   to that table should expect the same and say it up front.

4. **Points and rates are different currencies and the table needs both.** An
   entry's cost is quoted in *points of the whole frame* (comparable across
   entries, addable, stable) while the headline is a *rate over what is left*
   (not addable, moves when any rectangle changes). Quoting one where the other
   is meant is the easiest mistake here. The per-entry costs also do not sum to
   the declared total — the traffic-light rectangle and the top-left corner
   overlap, and the mask counts the shared pixels once.

5. **A hand-measured rectangle needs a staleness alarm or it silently rots.**
   The rectangles are pixels in the reference's coordinates, so a closed
   divergence, a nudged layout or a re-shot reference all leave an exclusion
   sitting over live surface, hiding real signal. Two guards, both cheap: the
   table is skipped entirely (loudly) unless the reference is exactly
   1180x949, and any entry that turns out to exclude zero differing pixels
   prints `<-- STALE? excludes nothing`. Neither existed before; both should
   have been the first thing written, not the last.

6. **`--diff` greys the declared surface rather than dropping it.** An
   exclusion you cannot see on the diff image is an exclusion nobody audits,
   and the first question anyone asks of one of these rectangles is whether it
   is drawn around the right thing. Worth the four lines.

7. **`compare.py` has no home in `make test`.** The suite is C++ binaries plus
   the scripted `.e2e` DSL; there is nowhere for a Python assertion to live, so
   the exclusion arithmetic is pinned by `compare.py --selftest` instead — 
   hermetic 100x100 frames, run by hand. It is genuinely tested (three
   deliberate breakages all go red: numerator-only subtraction, a rectangle off
   the frame, an entry with no reason) but nothing runs it on a schedule. A
   `scripts/*.py --selftest` sweep at the end of `run_tests.sh` would be one
   line and would cover this and anything after it.


---

---

## Transcript turns (feat/vis-turns)

1. **The turn shape was already right; the score was hiding it.** The theme was
   "make the transcript render Puffin's turn shape" and most of it was already
   in `render_bubble` — right-aligned user bubble, avatar, shrink-to-fit,
   left-aligned assistant. What was missing was that the fixture never showed
   it: `t2` opened on a System caption, so the parity capture measured a shape
   the code could already draw but never drew. Half an hour of reading beat any
   amount of drawing. Worth checking before starting: is this a rendering gap
   or a fixture gap?

2. **The score is dominated by vertical registration, not by shape.** The 2x2,
   all on `main`, STRUCTURAL, all four cells taken on the **pre-divergence**
   `compare.py` against `01_home.png` (see entries 10 and 11 — the scorer and
   the reference both changed under this branch, and these four are not
   comparable with any figure quoted after them; they were also taken minutes
   apart rather than back to back, so read them as directional only):

---

   `compare.py` (see entry 10 — the scorer changed under this branch and these
   are not comparable with a figure quoted after it):


   |                    | opens on a System caption | opens on a user turn |
   |--------------------|---------------------------|----------------------|
   | sub-agent rollup on  | **6.10** (the baseline) | **7.88**             |
   | sub-agent rollup off | 5.60                    | 5.48                 |

   The correct shape scores 1.78 points *worse* than the wrong one, and the
   same change scores 0.12 points *better* once one unrelated 36px row is out
   of the way. Neither number is about the turns. The reference transcript is
   two short rows and then 600px of empty background, so anything that adds
   height above the fold pushes hanabi's content into a region where the
   reference has nothing, and costs several times its own area. **A per-region
   percentage cannot distinguish "wrong shape" from "right shape, 40px down".**
   Anyone driving this metric should re-shoot with the confound removed before
   concluding a change was bad.
   - **Cost** — four rebuild-and-shoot cycles to establish, and a headline
     regression to explain.
   - **Class** — `TEDIOUS` (our tooling)

3. **Reading Puffin's source was worth it, and it was worth 0.01 points.**
   Four constants were corrected against
   `Sources/Views/AgentcloudTranscriptView.swift` rather than the 1x PNG:
   avatar diameter 22→20 (`BubbleAvatar.diameter`), avatar gap 5→6 (the
   `HStack` spacing), corner radius 8→10 (`RoundedRectangle(cornerRadius: 10)`),
   bubble cap 620→644 (736 − 60 − 6 − 20 − 6, which is the `Spacer(minLength:
   60)` arithmetic and not a reading of anything). Measured against the same
   frame, the four together moved `main` from 7.89% to 7.88%. At 1x with a
   0.8px structural blur, 2px of diameter and 2px of corner radius are inside
   the noise. Read the source for *correctness* — the numbers are now derived
   and named rather than guessed, and the next person can check them — but do
   not expect the metric to notice.
   - **Class** — `TEDIOUS`

4. **The source checkout is not the build that shot the reference.** The brief
   described "a small round avatar with a G in it"; `~/kt-ng2w-puffin`'s
   `BubbleAvatar` draws `Image(systemName: "person.fill")`. Zoomed 3x, the
   reference is unambiguously a **G**. So `REFERENCE.md`'s "the source says
   WHY" rule needs a rider: the checkout can be ahead of or behind the frozen
   PNG, and where they disagree about *what is on screen* the PNG wins — the
   source only wins on *what a number is*. (Here it cost nothing: hanabi
   already draws `$USER`'s initial, which is a G on this box.)
   - **Class** — `FOOTGUN`

5. **Two numbers that look contradictory are the same number.** Puffin says
   `.padding(.horizontal, 12)`; hanabi's `kBubblePadX` is 13 and the reference
   measures 13 (bubble left edge 752, first lit pixel 765). Both are right: 12
   is text-origin to edge, 13 is first-ink to edge, and the 1px between them is
   the leading side bearing of a lowercase glyph at 13px. An hour is available
   to be lost here on any constant where the source measures from the layout
   box and the screenshot measures from the ink.
   - **Class** — `FOOTGUN`

6. **`expect_text` is a substring match over every visible label, and that
   silently weakens assertions when a fixture changes.** Turning `t2`'s opening
   caption into a user turn made "ledger" paintable three times instead of two
   (a System caption has no highlight path; a user bubble does), so four find
   tests moved from `1 of 2` to `1 of 3`. In `sidebar_search_snippet` that
   collides with a sidebar row's own snippet, "1 of 3 workers has reported" —
   the tally assertion would then have passed off the wrong element with find
   completely broken. There is no exact-label form of `expect_text` (the
   sibling complaint to gaps #73 about `assert_ui_text`). Worked around by
   giving that one script a find query whose count cannot collide.
   - **Cost** — one near-miss, caught by reading rather than by the suite.
   - **Class** — `FOOTGUN`
   - **Gap filed?** — no; #73 already covers the family. An
     `expect_text_exact` would close both.

7. **`assert_ui <debug_name> x= y= w= h=` is the right tool for a layout
   assertion and nothing points at it.** Every coordinate test in this repo
   drives the mouse to a pixel and asserts on text, with a paragraph of comment
   explaining how the pixel was derived and a warning that it will rot.
   `assert_ui` asserts the *computed rect* directly, prints the actual on
   failure (so deriving the expected value is one deliberately-wrong run), and
   is immune to everything except the thing under test. The new
   `user_turn_hugs_the_right_edge.e2e` uses it and is about a tenth the
   commentary of the tests either side of it. It is documented only in
   afterhours' own `ui_commands.h`.
   - **Class** — `TEDIOUS`

8. **hanabi puts sub-agents in the transcript; Puffin puts them in a popover.**
   `SubAgentPanel` / `StripListPopover` are opened from a chip and never appear
   inline. hanabi renders "▸ 2 sub-agents · running" as the first row of the
   scroll content, 36px above the first message. On the parity capture that row
   is worth **2.40 structural points on `main`** — by a distance the largest
   single item left in the region, and larger than everything this theme
   changed put together. Deliberately not touched: it is a different theme, and
   `subagent_toggle.e2e` is built on `t2` having sub-agents.
   - **Class** — `WORKAROUND` (not taken)
   - **Gap filed?** — no; a design divergence, not a library limitation.

9. **The always-reserved Copy/timestamp bar costs 19px per turn and zero
   points.** Puffin overlays its copy button (`.overlay(alignment:
   .topTrailing)`) and takes no layout space; hanabi reserves a 22px row plus a
   2px gap under every turn, which makes the inter-turn gap 43px against
   Puffin's 24px. afterhours can express the overlay —
   `with_absolute_position()` sets `computed_rel` from the parent and is skipped
   everywhere a child's size would feed into its parent's — so this is buildable
   and is a genuine rhythm difference. Measured before building it: setting
   `kMsgActionsH` and `kMsgActionsGap` to 0 moves every reply up 24px and
   changes `main` by **0.00%**, because the reference is empty background
   everywhere the rows land. Not built. A real difference that this reference
   cannot see is not worth the surgery on `bubble_height()` and three hover
   tests until there is a frame that can see it.
   - **Class** — `WORKAROUND` (not taken)

10. **The scorer changed mid-theme, and then the reference did.** Two things
    landed on `main` while this branch was building. First
    `feat/vis-divergences`: `compare.py` now subtracts declared divergences,
    the largest being **`transcript viewport`, the whole rect
    (283,71)–(1180,820)** — every pixel this theme touched. Then
    `ref/02_thread.png`: a second frozen frame whose open thread has a real
    Puffin fixture, on which that rect is NOT declared. So the theme went from
    unmeasurable to measurable inside one session. The measurable answer, taken
    cleanly (below), is that the four source-derived constants are worth
    **0.01 structural points** — on 01 and on 02 alike.
    - **Class** — `TEDIOUS` (our tooling)

11. **The mock fixture's clock moves the transcript, and it silently faked a
    0.60-point win.** `mins_ago`/`hrs_ago` in `mock_client.h` are relative to
    the wall clock, and the transcript inserts a date divider wherever the
    calendar day changes between two messages. It was 00:09 local. `r5`'s
    messages are 9, 6 and 1 minutes old, so they straddled midnight, and the
    "Today" divider was above the assistant reply in one capture, below it in
    the next, and gone in the one after that — moving every row under it.

    Measured the naive way, one build then the other a few minutes apart,
    `main` read **5.89% → 5.29%** and the constants looked like a 0.60-point
    win. Rebuilt both binaries first and shot them back to back, twice each:

    | | run 1 | run 2 |
    |---|---|---|
    | base constants | 4.19% | 4.19% |
    | source constants | 4.18% | 4.18% |

    Each binary is bit-reproducible against itself (`getbbox()` on the
    difference of its two runs is `None`), so the pair is trustworthy and the
    honest figure is **0.01 points**. The 0.60 was the divider. Two shots of
    one *unmodified* binary three minutes apart read 4.19% and 4.66%.

    **Any A/B on this harness must shoot both sides back to back and diff the
    two hanabi PNGs against each other before reading the score.** Nothing in
    the tooling warns you; the numbers look perfectly stable one at a time.
    Written into `REFERENCE.md` and the header of `scripts/shoot_hanabi_02.sh`.
    - **Cost** — a wrong result reported, then caught. It was caught only
      because the *geometry* dump disagreed with the score: two shots that
      should have differed in four constants had their whole message order
      changed. Dump the bands, not just the percentage.
    - **Class** — `FOOTGUN`
    - **Gap filed?** — no; ours, not afterhours'. The cheap fix is a
      `HANABI_MOCK_NOW` epoch override so the fixture is deterministic; the
      cheaper one is the back-to-back rule.

12. **The 02 brief's "near-white user bubble" was a single-pixel sample that
    landed on a glyph.** The handoff said 02's user bubble is
    `(237,237,245)` — near-white with dark text — and asked for hanabi's to
    match. Counted over the bubble's own rect (x 818..1097, y 95..129, 9800
    px), the fill is **(62,56,111) at 61.4%** — the same indigo as 01 — and
    `(237,237,245)` is **3 pixels**, which is the near-white TEXT. Acting on it
    would have inverted hanabi's user bubble away from the reference. Corrected
    in `REFERENCE.md` in place rather than quietly, because the mistake is easy
    and worth inoculating against: inside a text bubble a random pixel is on a
    glyph about as often as on the fill. **Sample a region and take the mode.**
    - **Class** — `FOOTGUN`

13. **02 answers the geometry questions 01 could only be argued about — and it
    confirms Puffin's source to the pixel.** The three constants this theme
    took from `AgentcloudTranscriptView.swift` are all directly measurable in
    02: the avatar disc is x 791..811 (**20px**, `BubbleAvatar.diameter = 20`),
    it sits **6px** left of the bubble (the row's `HStack(spacing: 6)`) and
    **6px** below its top (`.padding(.top, 6)`). Three source constants, three
    independent confirmations off a frame the source was not consulted for.
    That is the strongest argument for the read-the-source rule in this
    workstream so far — stronger than the 0.01 points it scored.

14. **Even on 02, the transcript score is content, not design.** Against 02,
    hanabi's user turn already has the right shape where shape is checkable:
    bubble right edge **1097**, assistant bubble **362..1031**, avatar 20px —
    all exact. Sweeping hanabi's whole main pane vertically against the
    reference shows the score is flat to ±0.04 points across an 8px shift, so
    vertical registration is not what the 4.1% is made of either. It is that
    `r5`'s reply is one 31px paragraph and 02's is a 124px bubble with a fenced
    code block in it. Choosing the thread is worth more than any geometry work:
    `r5` scores `main` at 4.66% and `t2` at 8.55%, same binary, same minute.
    - **Class** — `TEDIOUS`

15. **The declared-divergence table needed a second entry set for 02, and four
    of the five entries were identical.** `KNOWN_DIVERGENCES` is keyed on the
    reference's basename, so `02_thread.png` matched nothing and had *no*
    exclusions — including the four that are properties of how the two apps
    are captured (traffic lights, window corners, status band, version string)
    and are therefore true of every reference shot this way. Verified rather
    than assumed before sharing them: all four rects are byte-identical
    between 01 and 02. Lifted into a `_CAPTURE_DIVERGENCES` list both
    references splice in; 02 takes those four and **not** the transcript one,
    which is the entire reason it exists. 01's declared cost is 4.75 points,
    02's is 0.25. `--selftest` PASSes.
    - **Class** — `TEDIOUS`

---

10. **The scorer changed mid-theme, and this theme is now entirely inside a
    declared-unspendable region.** `feat/vis-divergences` landed while this
    branch was building: `compare.py` now subtracts five declared divergences,
    the largest of which is **`transcript viewport` — the whole rect
    (283, 71)–(1180, 820)** — on the grounds that the reference's open thread
    has no Puffin mock fixture and renders one placeholder line where hanabi
    renders a conversation. Every pixel this theme touched is inside that rect.
    Measured on the new scorer, baseline and this branch:

    | | whole frame STRUCT | SHARED `main` STRUCT | declared cost |
    |---|---|---|---|
    | baseline | 7.39% | 10.74% | −3.50 |
    | this branch | 8.64% | **10.72%** | −4.75 |

    On the surfaces the new scorer says to drive, the change is **−0.02
    points**: neutral. The 1.25-point rise in the whole-frame figure is
    entirely the declared cost growing, i.e. hanabi and the reference
    disagreeing *more* inside a box that was already written off. Two agents
    reached "this reference cannot measure the transcript" independently within
    the same hour — `REFERENCE.md` from Puffin's `MockBackend.swift:936`, and
    this branch from the 2x2 in entry 2. It is worth taking as settled, and the
    re-capture REFERENCE.md asks for (a `mock-*` thread with a real fixture) is
    the only thing that makes transcript work measurable at all.
    - **Class** — `TEDIOUS` (our tooling)


---

## The search pill (no branch — done on main)

### 1. The placeholder was half the contrast and two sizes small

- The reference's hint stands 10px tall and its ink peaks at (163,163,168).
  hanabi's stood 8px and peaked at (98,98,110) — **half the contrast, on the
  one label in the sidebar whose entire job is to be noticed by someone who
  has not found the field yet.**
- Its copy was "Search conversations" where the reference says "Search" — the
  field is already inside a sidebar of conversations.
- SEARCH 7.94% → 4.94% structural, the largest single move of the night per
  line changed.
- **Class** — `TEDIOUS`. Nothing was broken; it had simply never been measured.

### 2. Colouring one placeholder moved the main pane — gap #90 (theme)

- The placeholder's colour can only be set through `ctx.theme.font_muted`
  (text_input ignores per-widget colours, gap #17), and `ctx.theme` is one
  global struct the RENDERER reads. Setting it in the sidebar brightened every
  muted label in the frame: `main` moved 0.14 points on a change that touched
  no main-pane code.
- Caught only because a parity number moved in a region I had not touched.
  Without the harness this ships as "the timestamps look a bit brighter now"
  and nobody ever connects it to a search box.
- **Cost** — a line in `main_pane_system.h` that has no interest in the search
  field, plus the knowledge that every system must defensively re-assert every
  theme field it cares about, forever.
- **Class** — `FOOTGUN`, filed as #90.

### 3. Where the last 5% of the pill is, and why I stopped

- The magnifier is 3px left of the reference's (14..23 against 17..26). Moving
  it means trading the field's left padding against the icon slot's width,
  because the slot's width is also what positions the hint text — and the hint
  is now exact (33..72, both). Three pixels on a ten-pixel glyph, against a
  risk of moving a label that is currently correct.
- **Class** — noted, not taken.

---

## Composer strip (feat/vis-composer2)

1. **The region score cannot see this theme's work, and the reason is a
   feature.** hanabi carries a 26px status bar (`statusBarHeight`, "6 blocked on
   you" / "20 sessions") at the bottom of the main column; Puffin has none, and
   its composer runs to the window's bottom edge. So hanabi's composer sits
   exactly 26px high and *every* pixel in the band is compared against the wrong
   row. Measured: the band scores 12.40% as the harness scores it and 6.28% when
   hanabi's rule is aligned to the reference's, so the offset alone is 6.1 points
   of the band. Pasting the finished band 26px lower and re-running `compare.py`
   prices it in the real metric: `main` 5.92% → 5.30%, overall 7.26% → 6.82%.
   That number belongs to whoever owns the sidebar, not to this theme — Puffin
   puts the same information in its sidebar footer, which the reference has and
   hanabi's `footer` region already scores.

2. **Profile the band aligned to its own landmark, not as the harness scores
   it.** The whole first pass — full-width rule, Puffin's column, the input box
   landing on x=357..1072 to the pixel — moved the headline from 7.39% to 7.38%.
   The same pass moved the band from 9.65% to 8.09% once measured against
   itself. A 30-line numpy script that crops both frames from the hairline down
   and sweeps a vertical shift found the 26 immediately and then reported every
   change honestly for the rest of the session. Anything else in this band is
   measuring the status bar.

3. **A row-by-row diff finds colour bugs that eyes and region totals both
   miss.** After the geometry matched, two rows of the aligned band were 79%
   wrong across 710px: the input box's top and bottom borders. hanabi drew them
   in `theme::border()` (62,62,72) where the reference is (45,45,59) — Puffin
   uses `mutedText.opacity(0.25)` for the field and full-strength `hairline`
   only for the rule above, and hanabi had one colour doing both jobs.
   Switching that one element to `theme::border_soft()` took the band from
   7.98% to 6.28%: **one colour was 40% of everything left.** It is invisible
   side by side and it is the largest single term.

4. **PUFFIN_SPEC.md's composer numbers are right; the brief's are eyeballed.**
   The brief put the hairline "at about y=846"; it is at y=851, which is what
   the spec already said. Worth 5px of chasing before I profiled it myself.
   Conversely the spec's `track x=360..404, 45x6` for the meter is wrong — the
   reference's is 48x5 at x=441..489, which is also what Puffin's source says
   (`.frame(width: 48, height: 5)`). Trust the source, then the pixels, then
   the prose.

5. **Two rows of every 1px edge can never match, and it is not a bug.** The
   reference is a 2x capture downsampled, so its 1pt borders land as one strong
   row plus one half-strength row — (45,45,59) at y=884 then (32,32,45) at
   y=885. hanabi at 1x paints one crisp row. The second row is off by ~28 on a
   threshold of 24, so it counts as wrong, on all four edges of the input box
   and the top and bottom of every pill. That is ~1.6 points of this band that
   no code can close without drawing a fake antialiasing row. It is the same
   thing `compare.py`'s "floor ~2.3% — retina downsample" is warning about, and
   it should be said per-region, not only in the headline.

6. **A negative margin escapes a parent's padding, and I did not use it.** The
   composer bar owned the gutter, which made the hairline a child of the padded
   box and only as wide as the reading column. `with_margin(Margin{.left =
   pixels(-gutter)})` on the divider works — verified, `composer_divider` comes
   out x=280 w=820, identical to the restructure. I moved the gutter onto each
   content row instead, because a negative margin is undocumented behaviour in a
   vendored library we cannot patch and cannot pin, and because the restructure
   is what Puffin actually does (`Divider()` is a sibling of the padded
   composer). The cost was a signature change on `render_attachments` to carry
   the gutter down. Worth knowing the shortcut exists; worth not taking it.

7. **The mock fixture reports no context budget, so the meter never draws.**
   Puffin's strip shows a 48x5 track and `0%`; hanabi shows `~112 tokens` and no
   track at all, because `usage.has_denominator()` is false on the mock and
   `configuredContextBudget` is 0. The meter's geometry is now Puffin's, and no
   capture can show it. Anything that wants that part of the strip compared has
   to give the mock a budget first — and `context_bar_needs_a_denominator.e2e`
   exists precisely to stop us inventing one.

## Row state glyphs (feat/vis-glyphs)

### 1. The Puffin checkout on this machine is three versions behind the frozen reference

- **What I wanted** — the state→glyph rule, read out of Puffin's source the way
  REFERENCE.md says to, instead of guessed off the capture.
- **What happened** — `~/kt-ng2w-puffin` is **v0.5.2** (`VERSION`); the frozen
  reference's own footer reads **v0.5.5**. Between them the row mark changed
  shape vocabulary entirely — the checkout's `SessionRowView` draws a `Circle()`
  for every childless row and a `chevron` for every row with sub-agents, full
  stop, while the capture draws arcs, bangs, crosses, chevrons and dots in three
  colours. The checkout's mock fixture has changed too: five of the capture's
  twenty rows (`two workers still out`, `two shards died`, `needs a decision
  before it can go on`, `finished, and wants you to read it`, `parent — nothing
  to report`) are not in it at all. No newer Puffin source exists on this box —
  every other `~/kt-*` and `~/w/puffin-*` tree with a `VERSION` reads 0.5.2 or
  older.
- **Cost** — an hour, most of it spent doubting the pixels because the source
  said something else. And it nearly cost the whole finding: the obvious
  conclusion from `SessionRowView` alone is "Puffin has two shapes and hanabi
  should too", which is wrong.
  What rescued it was reading the source for the RULE rather than the SHAPES,
  and pairing the capture's rows against the checkout's fixture states row by
  row — 15 of the 20 rows are in both, each with its state declared in Swift,
  so the mapping falls out of the pair. Every conclusion below is one the
  v0.5.2 source and the v0.5.5 pixels agree on.
- **Class** — `FOOTGUN`
- **Gap filed?** — not an afterhours gap. Written into REFERENCE.md instead, so
  the next agent reads the caveat before spending the hour.

### 2. A screenshot cannot tell you a `working` claim from a live run — the source can, and it is emphatic

- **What I wanted** — to know why two rows draw a steady blue dot where four
  others draw a spinner, when hanabi's fixture calls all six `Running`.
- **What happened** — the answer is in Puffin's `HomeSessionList.swift`, and it
  is not subtle: *"A `working` claim with no run behind it is a corpse: spec 029
  measured a 925-session fleet at 132 claiming `working` against 3 that really
  were."* The wire carries both halves — a `status.state` of `working` and a
  separate `running` flag — and Puffin buckets the claim-without-a-run as
  *finished*, not running. hanabi's adapter mapped `state == "working"` straight
  to `ThreadState::Running` without ever reading `running`, so a stalled thread
  wore a live spinner. On the fleet number above that is ~14% of a catalog
  lying, not two fixture rows.
- **Cost** — one new state (`ThreadState::Working`), and a knock-on: the
  sub-agent count's "live" tally counted stalled children too, so a subtree
  where nothing had moved still read `1/3`. Fixed with the count's own test.
- **Class** — `FOOTGUN` (hanabi's, not afterhours')
- **Gap filed?** — no. This is a domain-model bug the parity work surfaced,
  which is the forcing function doing its job.

### 3. Drawn shapes have no antialiasing, and it is worth more of the diff than being right

- **What I wanted** — five small marks that match a client drawing the same
  shapes as vector glyphs.
- **What happened** — afterhours' sokol backend pins `sample_count = 1` in both
  its windowed and headless setup, and `sgl` antialiases nothing itself, so
  every mark has binary edges against a reference whose every curve carries two
  or three levels of partial coverage. Text is unaffected (it comes from an
  antialiased atlas), which is why the aliasing reads as "the shapes look like
  pixel art" rather than "the app looks low-res".
- **Cost** — the honest ceiling for this whole theme. Measured per row on the
  reference's twenty: a mark that is the RIGHT shape in the RIGHT place still
  differs over ~90 px, a wrong one over ~106. Nine wrong glyphs were therefore
  worth ~150 px of a 24,240 px list-region diff before I started. Result:
  **list 14.32% → 14.20%, overall 7.39% → 7.38%**, with 20 of 20 marks now
  correct. Placement tuning off the reference's own pixel rows (the bang was two
  rows short and a pixel right; the dot a pixel small) was worth more than the
  shape fixes: 2117 → 1884 px in the glyph column, of which ~1800 is the
  aliasing floor.
- **Class** — `WORKAROUND`
- **Gap filed?** — yes, `afterhours_gaps.md` #92.

### 4. `make test` runs a STALE test binary after a header-only change

- **What I wanted** — to watch a new assertion go red against a neutered fix,
  which is the only thing that makes it evidence.
- **What happened** — the first neuter came back green. The test targets depend
  on `tests/**/*.cpp` and `$(API_SRCS)` only (`makefile:430`), with no header
  dependency and no `-MD` for them, so gutting `src/ecs/thread_model.h` — the
  file the test exists to assert — leaves `output/tests/test_e2e` "up to date"
  and make silently re-runs yesterday's binary. This is the same family as the
  known "make will not relink after you restore a binary over output/" hazard,
  and it fails in the direction that costs you the most: it says PASS.
- **Cost** — nearly published a test I had "verified" against its own fix.
  `rm -f output/tests/test_e2e` before every neuter run. All four neuters
  (failure branch removed / chevron removed / working spins like a live run /
  fixture reverted) go red once the binary is actually rebuilt.
- **Class** — `FOOTGUN`
- **Gap filed?** — no; it is hanabi's makefile. Worth a `-MD`/`-MP` on the test
  rules, which is a two-line change in a file this branch has no business
  touching.

### 5. A CHECK macro and a braced initializer do not mix

- **What I wanted** — `CHECK(mark_for(s) == Mark{Glyph::Bang, Tone::Live})`.
- **What happened** — 29 instances of `error: too many arguments provided to
  function-like macro invocation`: the preprocessor splits on the comma inside
  the braces before C++ ever sees it. Double parens fix it.
- **Class** — `TEDIOUS`
- **Gap filed?** — no. Worth knowing before writing a test file full of them.

### 6. The capture is not reproducible across midnight

- **What I wanted** — to compare a before number taken at 23:48 with an after
  number taken at 00:25.
- **What happened** — the overall figure went UP (7.38% → 7.45%) on a change
  that touches nothing outside the sidebar, and the `main` region moved 6.10% →
  6.21%. The mock's message timestamps are relative to `now`, so crossing local
  midnight made the open transcript grow a **"Today" date divider** and pushed
  every bubble under it down a row. Re-measuring the baseline binary in the same
  minute put it at 7.47% / `main` 6.21% — identical `main`, and the real change
  is sidebar 11.90 → 11.83, list 14.32 → 14.20.
- **Cost** — one rebuild of the baseline to get a like-for-like pair, and a
  reminder that any before/after separated by hours is measuring the clock as
  well as the code.
- **Class** — `FOOTGUN`
- **Gap filed?** — no; it is the fixture's relative ages, which are deliberate
  (they keep "6m"/"5h" stable across runs *within* a day). Shoot the before and
  the after in the same session, or quote the region you changed.


---

## The selected view's fill (no branch — done on main)

### 1. A comment asserted the thing it was wrong about

- Over the smart-view row: *"Puffin's selected row is a full-bleed rectangle,
  edge to edge, with square corners."* Half right. It IS full-bleed — the fill
  really does run x0..278, the whole sidebar, which is why nobody questioned
  the sentence. The corners are **rounded**, radius ~5: the fill's top row
  spans x3..276 where its middle spans x0..278.
- That is the second comment on this workstream that documented an intention
  the code did not achieve and nobody re-read (see the `.with_padding(12)` that
  had never done anything, gap #85, and Home's badge whose own comment said
  what the number should be next to a `-1`). **A confident comment is where you
  should look first, not last** — it is the thing that stops the next reader
  measuring.

### 2. The fill was 32px in a 32px pitch; the reference's is 29

- Two full-width rows of pure difference under every selected view — the
  largest contiguous block left in the region once the focus ring went.
- The 3px comes back as **margin**, not off the pitch, and that distinction was
  worth measuring: `kSbViewRowH` also sets where the session list starts, so a
  pitch change moves twenty list rows to fix six view rows. Tried it: pitch
  32.2 fixed the one view row that 32 gets wrong and took **list 14.25% →
  17.18%, sidebar 10.89% → 13.01%**.

### 3. The reference's own pitch is not an integer, and cannot be matched

- Its rows sit at 83, 115, 147, 179, 211, 244 — 32, 32, 32, 32, **33**. It is a
  SwiftUI layout captured at 2x and halved, so its rows land on half-pixels. At
  pitch 32 Blocked and Review are exact and Archived is 1px high; at 32.2 the
  other way round. There is no integer pitch that matches all six, and the
  fractional one costs more elsewhere than it wins here.
- **Class** — `IMPOSSIBLE`, small. Worth knowing before someone spends an hour
  on the last row.

### 4. Shrinking the fill moved the text, and the text was already 1px high

- Content is centred in the row, so taking 3px off the fill rode the whole row
  up — VIEWS got *worse* (5.83% → 6.07%) on a change whose geometry was exact.
  Re-derived the padding by sweeping it against the reference's own label rows:
  6/4 rather than 4/5 puts Blocked at 142..152 and Review at 174..184, both
  exact.
- **The lesson is the order**: fix the box, then re-derive everything centred
  inside it. Measuring the box alone said the change was right while the region
  said it was wrong, and the region was right.
- Net: VIEWS **5.83% → 5.07%**, sidebar 11.10% → 10.89%.

---

## The VIEWS strip and its icons (no branch — done on main)

### 1. Three of the six view icons were the wrong drawing, and a comment explained why

- The reference names its glyphs as SF Symbols, right there in
  `SmartViewSidebar.systemImage`: `house`, **`hand.raised`**,
  **`checkmark.circle`**, **`pin`**, `archivebox`, `gearshape`. hanabi drew a
  hand-rolled **warning triangle** for Blocked, a **bare check** for Review and
  a **star** for Pinned.
- Blocked's was the interesting one. A twenty-line comment justified it:
  *"the atlas has NO better-fitting glyph… nothing that reads as
  waiting/attention (no clock, hourglass, inbox, bell, or hand)"* — and it was
  true when written. Nobody re-asked it. **The atlas is generated by a script
  in this repo** (`scripts/gen_icons.py`, Lucide, ISC); adding four icons was
  four lines and one command.
- **The lesson: a comment explaining why something cannot be done needs a
  date.** This one was a correct answer to a question whose answer had changed,
  and it read as authoritative for months.
- VIEWS **5.07% → 4.88%**.

### 2. The disclosure chevron was a filled triangle where the reference strokes one

- Measured, the reference's chevron is 8x5 with a ~1.5px stroke and open space
  inside it. hanabi drew a filled triangle of the same extent — twice the ink,
  and at a glance it reads as a play button rather than a disclosure arrow.
- Fixed in `hanabi::glyph::chevron`, so the transcript's four disclosure
  chevrons got it too. Ink 20 against the reference's 18.
- Stroke is 1.6 rather than 1.2 deliberately: afterhours does not antialias
  primitives (gap #92), so a thinner stroke does not get *lighter*, it drops to
  a hairline of hard pixels.

### 3. Moving a glyph without moving the label next to it is a trade, not an edit

- Twice in one sitting: the search magnifier was 3px left of the reference's,
  and the strip's chevron 3px right. In both cases the glyph is centred in its
  own slot and the label FOLLOWS that slot in flow, so any change to the slot
  moves both.
- The move is to trade a leading spacer against the slot's width at a constant
  sum: `6 + 7 == 13` puts the magnifier on the reference's x17..26 while
  leaving the hint on its x33. Same trick, opposite direction, for the strip:
  `7 + 10`.
- **This is gap #85/#91 wearing a different hat** — a label is not a layout
  participant, so it cannot be nudged; only the things around it can.

### 4. VIEWS itself was two sizes small and half the contrast

- 26px of ink where the reference has 37, at `theme::type::LABEL` (10.5) where
  the reference is ~14.5, and in `text_faint` (100,100,112) where the reference
  peaks at (151,151,176). Exactly the same pair of errors as the search
  placeholder, in the same file, found a day apart.
- **Both were section chrome.** Neither was ever measured because neither
  looked wrong on its own — "a bit small and a bit grey" is not a thing the eye
  reports. Worth a sweep of every remaining label against the reference rather
  than waiting to trip over them one at a time.

### 5. I measured the pill off stray pixels and rebuilt a control that was already right

- `inkdiff.py` flagged the filter affordance, so I measured the pill it sits
  beside by scanning for its fill colour across the whole band. It came back
  **x8..280, 32px tall** — full sidebar width, with the filter INSIDE it. I
  restructured the search field around that: pill to full width, filter moved
  in as its last child, every width recomputed.
- **Search went 4.72% → 16.39%.** The measurement was garbage: matching the
  fill colour anywhere in a 40-row band picked up antialiased pixels from the
  rows above and below, and `min(x)..max(x)` over a scatter is not an extent.
  The reference's pill is **x8..249, 25px tall**, and the filter sits outside
  it exactly as hanabi already had it.
- What found the error was the score going the wrong way by a factor of three
  — not the geometry, which "matched" beautifully afterwards.
- **The rule this earns**: an extent is only an extent if the pixels are
  CONTIGUOUS. Scan row by row, take the longest run in each row, and look at
  whether the runs agree with each other. Two lines more code and it would have
  said 8..249 the first time. `inkdiff.py` does it this way; my ad-hoc probe
  did not.
- Reverted, then took the two real findings on the original structure: Lucide's
  `sliders-horizontal` instead of three hand-drawn rules (39px of ink against
  the reference's 95, and 90 levels too dark), and the pill 2px wider and 2px
  shorter. **Search 4.72% → 4.23%.**

### 6. `inkdiff.py` — the lever, and what it found in one pass

- Three flagged runs across the whole sidebar, all real, none of which anyone
  had noticed by looking: the VIEWS panel toggle 3px too tall and 70% too
  inky, the selected row's label 27% short of the reference's ink, and the
  search filter 6px left, 3px narrow and 90 brightness levels dark.
- It works by segmenting both frames into ink bands against the **local**
  background — the most common colour in each row's own strip — so it crosses a
  selected row's fill, a section header's tint and the window colour without
  being told where they are.
- Worth running over any region before working on it. It is a sieve, not a
  measurement: it hands you a short list of places to go and measure properly.

---

## Session list, round two (feat/vis-list2)

Region **14.25% -> 14.11%** structural; sidebar 11.10% -> 11.05%. Two real
defects closed, and the number barely knows. That is the finding.

### The ceiling, stated before the result

The brief asked for the honest ceiling first, measured against ink rather than
boxes. Here is a stronger version of that measurement, and everyone working
this metric should steal it: **replace a rectangle of hanabi's capture with the
reference's own pixels and re-score.** Nothing hanabi can do to that element
beats copying the reference, so the delta is a true upper bound — no estimate,
no modelling, and it costs one `Image.paste`.

Run against `main`, on `ref/02_thread.png`:

| replace with the reference's pixels | list falls to | ceiling |
|---|---|---|
| every row title, x25..236, rows 0..17 | 2.15% | **12.10** |
| the row-19 band, y878..911 | 13.31% | 0.94 |
| the glyph column, x0..26 | 13.52% | 0.73 |
| the count column, x236..283 | 13.69% | 0.56 |
| the whole list region | 0.02% | 14.23 |

Those four are very nearly a partition (12.10 + 0.94 + 0.73 + 0.56 = 14.33
against a 14.25 total; the overlap is blur bleed at the seams). So:

**85% of the list region is the eighteen row titles, and on all eighteen the
string, the start x and the size already match.** Their ink bboxes agree to
±1px on the left and ±6px on the right, and hanabi's ink is 77–86% of the
reference's on every single one. That is `## The typeface question, settled`
and gap #92, arriving from a third direction. My honest ceiling before starting
work was **~1.3 points**, and it was still too optimistic.

### Four hypotheses from the brief, killed with numbers

Worth recording as dead so nobody spends another round on them.

- **Row pitch and cumulative drift — exact.** Ink-band midpoints down the whole
  list, reference against hanabi: 316/315.5, 347/346.5, 379/378.5, 412/411.5,
  444/443.5, 476/475.5, 507/507, 539/538.5, 571.5/571, 603.5/603.5,
  636/635.5, 667.5/667.5, 699.5/699.5, 731.5/731.5, 763.5/763.5, 795.5/795.5,
  828/827.5, 859.5/859.5. Eighteen rows, no drift, max error 0.5px. The
  `feat/vis-list` grid-snapping fix holds.
- **Row separators / hover surfaces — neither app has any.** The modal colour
  of every row band in both frames is (23,23,35), identically. Scanning for a
  y-line where more than 150 of the 283 columns are non-background finds the
  same seven text bands in the reference and four in hanabi — all of them
  glyph rows, no rules.
- **Group headers — neither app draws one.** Puffin's `SidebarSection` is real
  and this frame does not use it: `HomeSessionList.section()` takes the
  `bucket.title.isEmpty` branch for the flat Home bucket and draws a bare
  `LazyVStack`. hanabi's list is flat for the same reason.
- **Fixture drift — no.** Nineteen visible rows, same order, same strings, same
  glyph shape on every row. The `pf()` catalog in `src/api/mock_client.h` is a
  faithful port of the v0.5.5 reference (NOT of `~/kt-ng2w-puffin`'s v0.5.2
  `MockBackend.swift`, which is missing five of these rows entirely — see
  REFERENCE.md). One row differs and only one: `t2` carries two sub-agents, one
  running and one done, so `ChildActivity`'s rule renders `1/2` where the
  reference says `1`. Left alone deliberately — see below.

### The star was a column, and it cost every title 18px — gap #93

The one structural defect in the row, and it took reading Puffin's
`SessionRowView` to see it. Puffin's row has no star; its trailing items —
theme mark, mute bell, child count, age — are all conditional, and the title
takes the slack with `.frame(maxWidth: .infinity)` and a layout priority.
hanabi reserved 18px on **every** row for a star that is invisible at rest,
"so the row does not reflow under the pointer", which was a real concern and
the wrong trade.

Measured: the reference lets a title run to x≈270, hanabi cut at x≈250, and
three of the nineteen titles were ellipsized a word or two early because of it.

| row | reference | hanabi, before | hanabi, after |
|---|---|---|---|
| stickers broke — concluded, … | ends x269 | x249 | x265 |
| needs a decision before it can go on | ends x247 | x220 | x239 |
| oncall sweep finished — 3 rows need… | ends x265 | x251 | x264 |

The fix is the star as an absolutely positioned child floating over the title's
tail (gap #93 for what that cost). It reflows nothing now — an absolute child
*cannot* move a sibling, which is a stronger guarantee than the reserved slot
ever gave.

**It moved the score by 0.01 points.** Ceiling on those three rows' tails was
1.24 by the paste test, and 0.52 of that is the floor the same rectangle scores
on three rows whose strings already match — so ~0.7 points of headroom, and it
delivered one seventieth of it. The words are now the reference's words, in
the reference's places, and the glyphs land a pixel or two out of register, so
every one that arrives adds about as much disagreement as it removes. This is
the `sub-agent count column` finding — a region's share is not what filling it
can win — pushed one step further: **for text, even a correct fix's measured
ceiling is not what a correct fix wins.** The ceiling assumes the reference's
own rasterization comes with it, and it does not.

Ship it anyway. Three rows say what they mean now.

### The sidebar's scroll view could not scroll

`fillCap = viewportRows - 1` made the rendered content shorter than the panel
**by construction**, so the `ScrollPanel` had nothing to move, at any catalog
size. A twenty-session list showed eighteen rows and spent the nineteenth slot
on a button reading "Show 2 more…" — a row spent to save a row. The reference
just keeps going: nineteen rows and a twentieth clipped by the footer.

Rendering two viewports' worth restores that and keeps the only property the
cap was ever protecting (rendered rows bounded by viewport height, not by list
size). hanabi's list now matches the reference row for row, including the
clipped twentieth.

Costs, both worth knowing:

- **A permanent scrollbar appeared and cost 3 points** the moment the content
  overflowed — an 8px (100,100,112) stripe painted over the sidebar's own
  column rule at x=279, where the reference has nothing. Puffin asks for
  overlay scrollers by name; afterhours has a bare bool. Gap #94, worked around
  in three lines, and the list went 17.25% back to 14.11%. **Anyone who makes a
  list longer in this app should expect this and check for it** — it does not
  look like a scrollbar in a diff image, it looks like the sidebar's edge
  moving.
- **The footer region got worse, 4.08% -> 5.24%,** because the clipped
  twentieth row now exists there and lands ~1px low against the reference's.
  That is the correct trade — the reference draws that row and hanabi did not —
  but it is a region going up for a good reason, which the score cannot say.

Net across the sidebar: -0.05.

### The glyph nudge, measured and reverted

Every mark except the bang sits 1px right of the reference's, and the arc and
chevron 1px high; the dot is a pixel narrow and the cross a pixel wide. All
five re-measured off the reference and re-tuned: **0.01 points.** Reverted —
`feat/vis-glyphs` already tuned these against the same pixels, and 0.01 does
not buy churning a measured constant. Gap #92 predicted this in as many words:
the aliasing is worth more of the diff than the placement is, and the whole
glyph column can only ever be worth ~1.2 points (paste test says 0.73 on this
pair).

### The one fixture difference, and why it stays

`t2` — "needs a decision before it can go on" — has two sub-agents in hanabi's
mock (`t2s1` running, `t2s2` done) and renders `1/2`; the reference's row says
`1`. Under `ChildActivity.label(total:running:)` a bare number means all live
or none live, so the reference's row has a different sub-agent shape. Every
other counted row agrees exactly: 7 of 19 rows carry a count in both, and
`1/3` on "coordinating 3 shard workers" matches to the pixel.

Not changed. It is worth ~0.1 points and it is the only mixed-state parent in
the fixture — the one row that exercises the running/total branch of the label
rule and the sub-agent panel's mixed list. Editing a fixture to move a parity
number is fitting the test; the divergence is measured, it is one row, and it
is written down here instead.

### For the next person

- The paste test above is the tool to reach for first. Five minutes, and it
  tells you which of your ideas are worth an afternoon before you spend one.
- The list is finished as a *structural* surface. Pitch, columns, fills,
  separators, headers, row count, row content and glyph vocabulary all agree
  with the reference now. What is left is 12 points of CoreText-versus-
  fontstash on eighteen strings that already match, and the three files that
  say so (`## The typeface question, settled`, gap #92, gap #77) now have a
  fourth measurement agreeing with them from the layout side.
- **Do not shrink the text to chase it.** The metric will pay you for that and
  it is the only thing it will pay you for.


---

## Tab strip and sidebar footer (feat/vis-tabs3)

Two regions that were already worked and already stalled. Both stayed small.
The useful output of this round is not the 1.5 points it moved, it is **why the
two regions read as much bigger than they are**, and one finding about the
metric that applies to every drawn mark in this workstream.

### The ceiling, counted before any code was written

A region's percentage is a fraction of its whole rectangle, and both of these
rectangles are mostly empty in both frames. So the first thing done was a census
of the actual diff pixels — where they are, and what each one IS.

**Tab bar, `ref/01_home.png`: 4.28%, 2840 structural diff pixels.**

| | px | share | can design close it? |
| --- | ---: | ---: | --- |
| row y=0, the whole width | 897 | 32% | no — macOS window bevel |
| the two tab TITLES | 1687 | 59% | no — different fixture strings |
| two pin glyphs | 128 | 5% | yes |
| the `+` | 75 | 3% | partly |
| everything else | 53 | 2% | — |

**Tab bar, `ref/02_thread.png`: 3.39%, 2257 px.** Same shape: 897 bevel (40%),
1184 the single title (52%), 75 the `+`, ~100 the rest. No pins — 02 is the
unpinned state, which is what makes quoting both references worth the trouble.

**Sidebar footer: 4.08%, 643 px.** ~200 of it is already declared (the version
string, the window corner). 203 more sit in rows y917..921 — that is ABOVE the
footer's rule, and it is the last session-list row bleeding into a region cut at
`H * 0.96`, so it belongs to whoever owns the list, not to the footer. What is
left, and what the footer actually is, is **221 pixels: three glyph buttons.**

So the honest ceiling stated up front was: **~0.3 points on the tab bar and
~2.2 on the footer, before any floor**. The eventual result was 0.12 and 0.00
from design work, plus 1.35 and 0.00 from one declaration. The ceiling estimate
was right about the size and wrong about which half would pay.

### The reference's top row is window decoration, and it was being charged to the tab strip — DECLARED

- **What I found.** Row y=0 of both references is (59..80) grey across the full
  1180 — the macOS window's own top border. Row 1 is already back to (28,28,40),
  and the left, right and bottom edges of the frame carry nothing like it. It is
  byte-identical between 01 and 02.
- **Why it survived four rounds.** Two entries in `compare.py` already declare
  exactly this cause — "titlebar traffic lights" and "rounded window corners",
  both worded "hanabi's parity capture is an offscreen render of the client area
  with no window and therefore no decoration". Between them they mask x<72 and
  x>1163. The 1092 pixels of the same border in between were never covered, and
  the tabbar region starts at x=283, so **the tab strip was being charged a
  third of its score for a window frame hanabi does not have.**
- **Cost.** Every tab-bar figure quoted in this workstream before today included
  it, including the previous round's "the region is at its floor" — which was
  right in substance and 1.35 points pessimistic in number.
- **Result.** Declared. tabbar 4.16% -> 2.81% on 01 and 3.39% -> 2.03% on 02, a
  metric fix and not a design one, and `--no-exclusions` still reproduces the
  old number exactly.

### The pin: the source said the rule and the reference confirmed the constant

The one clean win, and the shape of it is worth copying.

- **What I wanted.** A pinned tab's pushpin to look like the reference's.
- **What the source says.** `TabStrip.swift:506` draws `pin.fill` with its OWN
  `.foregroundColor(mutedText)` and its own `.opacity(0.7)`, overriding the
  chip's `.foregroundColor(isSelected ? text : mutedText)`. So the pin is one
  colour regardless of selection — a rule a screenshot can only guess at.
- **What the pixels say.** mutedText on the navi theme is (140,140,166)
  (`Models.swift:593`; navi is `defaultValue` and its headerBg is the (23,23,35)
  the frame paints). At 0.7 that predicts (105,105,127) over the inactive tab
  and (112,115,143) over the active one. Measured: **(107,107,127)** and
  **(114,117,143)**. Two units, on both tabs at once, from a rule read out of
  Swift.
- **What hanabi was doing.** Passing the tab's title colour, which on the active
  tab is pure white — 209 above its own background where the reference is 71.
  Every pixel of that mark was a difference on brightness alone.
- **Result.** pin1 57 px -> 28, pin2 71 px -> 24. Half the tab bar's closeable
  surface, from reading nine lines of Swift.

### Correcting a shape made the score WORSE, and that is the finding of the round

The first pin attempt fixed the silhouette and left the colour alone. Traced off
the reference row by row, it came out **worse: 57 px -> 69 and 71 -> 80.**

The `+` then reproduced it exactly, and cleanly enough to tabulate. hanabi's
plus runs y42..55 with a two-row crossbar; the reference's runs y43..55 with a
three-row one. A half-pixel `y_bias` reproduces the reference's geometry
exactly — same span, same three rows — and:

| | hanabi's ink (141,141,153) | reference's ink (148,148,172) |
| --- | ---: | ---: |
| as shipped (2-row crossbar) | 75 px | 56 px |
| geometry corrected (3-row) | **84 px** | **54 px** |

**The metric's verdict on the shape inverts with the colour.** The reason is
that hanabi's marks have no partial coverage (gap #92) and its ink is nineteen
units off on blue, against a tolerance of twelve — so every pixel of the glyph
is already a difference, and the two rows the correction ADDS are two more of
them. Fix the colour and the same correction wins by 2.

Two things follow, and they are general:

1. **Never A/B a shape against this metric while its colour is out of
   tolerance.** You will measure the colour and conclude something about the
   shape. The pin only started paying once the colour was right; the two changes
   were tested in the wrong order first and the first result was a lie.
2. **On an aliased renderer the metric quietly prefers under-drawing.** The
   reference's outer ring of any small mark is coverage, not ink — the pin's
   widest row reads 8 pixels to the eye and 6 at half coverage, and its needle
   peaks at 42, 43 and 15 above background. Draw those solid and each one is a
   new diff pixel; leave them out and they mostly fall under tolerance. The
   silhouette that scores best is the one thresholded at half coverage, which is
   how the shipped pin was finally traced.

The `+`'s geometry was **left uncorrected**, deliberately. The claim is weaker
than it looks: hanabi's plus is Lucide's and the reference's is SF Symbols', and
one icon set is under no obligation to sit where another does. The ink is a real
defect; half a pixel between two different glyphs is not.

### hanabi's greys are neutral; Puffin's carry a violet cast

Not a tab-bar fact, and it is the reason two of the marks above cannot be
finished.

Every muted ink in the reference has more blue than red. Measured as
(pixel − background), which is coverage-independent and therefore the only safe
way to read a colour off antialiased ink: the footer's version string and its
glyph buttons both give (54.8, 54.8, 60.3), a blue:red ratio of **1.10**;
Puffin's `mutedText` (140,140,166) predicts 1.12. hanabi's `text_secondary` is
(142,142,154) and `text_faint` is (100,100,112) — both exactly **1.00**.

On a mark whose entire content is ink, that is the whole score. The `+` at its
own full coverage is (141,141,153) against the reference's (148,148,172):
red and green inside tolerance, blue nineteen out, so all 75 pixels of it are a
difference and no amount of shaping helps. Recoloured to the reference's ink and
re-scored, it drops to 56.

Nine to nineteen units of blue, on every muted mark in the app. Closing it means
touching `text_secondary` and `text_faint` in `theme.h`, which is every region
in this workstream at once and three other agents' numbers moving under them.
Left alone on purpose; flagged here because it is the single largest thing
standing between the small drawn marks and their floor, and it wants doing in
one deliberate change by whoever owns the palette, not in four.

### The sidebar footer's whole colour axis is worth 0.11 points — NEGATIVE RESULT

Worth the space because the reasoning that motivated it was good and the answer
was still no.

- **What I wanted.** The footer's ink to match. Puffin gives the version label
  and all three glyph buttons one token (`SidebarColumn.sidebarFooter` ->
  `Chrome.mutedText`, which is (140,140,166)); hanabi draws them in `text_faint`
  (100,100,112), a token dimmer by a whole step. The obvious move is
  `text_secondary` (142,142,154), two units off in luminance.
- **What happened.** The footer went **4.08% -> 4.58%**. A colour constant is not
  what lands on screen: Puffin's 9pt and 10pt SF Symbols never reach their own
  colour — the reference's brightest footer pixel is 119 above background and
  its mean is 54.8 — while hanabi's sprite blits and 11px text do. Setting
  (142,142,154) put hanabi's brightest at 139 and its mean at 93.6, overshooting
  by twice as much as `text_faint` undershoots.
- **What the whole axis is worth.** Swept analytically rather than by rebuilding
  nine times: the per-pixel coverage is recoverable from one render with a known
  colour, so any other colour can be re-synthesised exactly. Across the whole
  plausible range the best available scores 4.32% against `text_faint`'s 4.44%
  in the same harness. **0.11 points, total.**
- **Why so little.** Because what the footer costs is not its colour. Two of its
  three buttons are different ICONS — see REFERENCE.md, "The sidebar footer's
  three buttons" — and a wrong shape costs the same whether the ink is dim or
  bright. The gear column, the one that IS semantically matched, carries half
  the diff pixels of either of its neighbours; that ratio is the finding.
- **Result.** Reverted. `text_faint` stays, with the measurement written above
  the code so the next person does not spend the same afternoon.
- **Class** — `NEGATIVE RESULT` (hanabi's palette, not afterhours)

### The tab titles are different strings, and renaming the fixture is not on

1687 of 01's 2840 diff pixels and 1184 of 02's 2257 are two tab titles that say
different words: the reference's fixture holds "TODO" and "Oncall triage tick —
subs_ex…", hanabi's holds "kicker-tick" and "needs a decision before it…".
Renaming hanabi's mock sessions to match would move roughly 2.6 points on 01 and
1.9 on 02 — by a wide margin the largest number available in either region.

Declined, for two reasons and the second is the real one.

- The mock catalog feeds the sidebar list as well as the strip, and another
  agent is live in the list's code on `feat/vis-list2`. Renaming two sessions
  moves their region under them mid-round.
- It would be measuring the fixture. REFERENCE.md's "compare LIKE FOR LIKE" says
  to shoot hanabi in the reference's STATE — same tabs open, same ones pinned —
  because otherwise you measure what is on screen instead of how it is drawn.
  Matching the state is what makes the comparison mean something; matching the
  CONTENT is the same move continued one step past the point where it stops
  being honest, and it would retire 2.6 points that describe nothing about
  either app's design.

### Cited, not re-filed

- **#92** (no antialiasing on primitives) is the floor under every number in
  this entry, and this round sharpens its own cost paragraph: it is not just
  that a correct mark still differs, it is that a correct mark can differ MORE
  than an incorrect one, and the table above is the case in point.
- **#61** (a scripted assertion reads x/y/w/h/hidden/text and never a pixel) is
  why the pin's ink is guarded by `tests/unit/test_tab_colors.cpp` — arithmetic
  against constants sampled off the reference — rather than by a `.e2e` file.
  Getting to it needed gap #95's file split.

### Where the remaining 2.81% / 2.03% is

Tab bar, after the bevel declaration. On 01: 1687 px the two titles, 75 the `+`,
52 the two pins, ~50 elsewhere. On 02: 1184 the title, 75 the `+`, ~100 else.
The titles are fixture and the `+` is nineteen units of blue. **Everything
reachable in this region without touching the mock catalog or the palette is now
about 50 pixels, or 0.08 points.** The previous round's "the region is at its
floor short of renaming mock sessions" was correct; the pins were the one thing
left in it, and they are done.

Footer, unchanged at 4.08%: 221 px of glyph buttons, of which the two mismatched
icons are a product difference rather than a defect, and 203 px of session-list
row bleeding in above the footer's own rule, which belongs to the list.


---

## The floor (no branch — done on main)

### The metric's floor for text is 8–12%, not 0.2%, and that was the whole story

- **What I wanted** — to know whether the session list's 14% was thirteen
  points of work or two.
- **What I did** — resampled the reference onto a grid offset by half a pixel.
  Identical design, identical everything, one difference: the rasterization
  phase, which is precisely what differs between two renderers drawing the same
  letter. Then scored it against the original.

  | region | floor | hanabi | real headroom |
  |---|---|---|---|
  | list | 8.41–11.83 | 14.07 | **+2.24** |
  | sidebar | 6.25–8.93 | 10.64 | +1.71 |
  | search | 1.54–3.17 | 4.26 | +1.09 |
  | views | 2.31–3.88 | 4.53 | +0.65 |
  | main | 0.83–1.41 | 4.49 | +3.08 |
  | tabbar | 1.25–3.02 | 2.03 | **AT FLOOR** |
  | footer | 1.61–5.87 | 5.26 | **AT FLOOR** |

- **Why nobody had this.** Both `compare.py`'s header and `REFERENCE.md` said
  the structural floor was **0.23%**. That number came from downsampling one
  frame two ways — which changes edge coverage but keeps every glyph on the
  same grid. It measured the wrong thing, and it has been quoted at the top of
  every measurement in this workstream.
- **What it cost.** Three separate investigations — the bold-face sweep, the
  session-list round two, and my own — each independently concluded "the rest
  is the rasterizer" and each stopped there, because none could say how much of
  it was. Two of them spent a day proving it. Two more regions have since been
  worked on that were already at floor.
- **Two remedies tried and killed with numbers**, both of which sound right:
  dilating the ink (list 14.07 → 16.11 at 25% blend, monotonically worse) and
  gamma-lifting the antialiased coverage, which is what a 2x downsample
  actually does (14.07 → 14.19 → 14.34 → 14.96 as gamma drops). The deficit is
  not coverage weight. It is sub-pixel phase, and nothing hanabi can do reaches
  it.
- **The root cause was in a code comment the whole time.** `main.cpp`'s
  `run_headless_screenshot`: *"this HEADLESS capture path renders into a fixed
  w*h offscreen texture at 1x — the Metal backend does not supersample it… a
  crisp @2x headless capture needs an upstream change."* Nobody connected that
  paragraph to the parity numbers, in either direction, for the whole
  workstream.
- **Class** — `TEDIOUS` (our tooling), and the most expensive single line of
  documentation in this project.

---

## Transcript fixture (feat/vis-fixture)

1. **Matching the words was worth +0.04 points, and it was still the right
   call.** The theme was "stop measuring content and start measuring design":
   `ref/02_thread.png` has Puffin's `mock-outcome-2` open, hanabi's r9 is the
   same row with a reply hanabi wrote for itself, and `shoot_hanabi_02.sh`
   defaulted to r5 because r5 was the nearest-SHAPED thread hanabi had. Porting
   Puffin's transcript into r9 verbatim moved `main` from **4.49% (r5) to
   4.53% (r9)**. Not a win — and not a loss either. What it bought is that
   every point left is now attributable: before, "hanabi's reply is one
   paragraph and Puffin's is four lines with a fence" was an unanswerable
   confound sitting on top of everything else. Afterwards, four separate
   findings came out of the same pane in an afternoon (entries 2, 3, 6, 7),
   worth 0.73 points between them. **The fixture was not costing points, it was
   costing attribution.**
   - **Class** — `TEDIOUS`

2. **The hover Copy row was reserving 24px per turn, and this is the reference
   that could finally see it.** `feat/vis-turns` measured exactly this in its
   entry 9 — hanabi reserves `kMsgActionsH + kMsgActionsGap` under every
   message where Puffin overlays its copy button — and did NOT build the
   overlay, on the honest grounds that zeroing the two constants moved 01 by
   **0.00%** because the reference was empty everywhere hanabi's rows landed.
   On 02 the same experiment moves `main` from **4.53% to 3.91%**: 0.62
   points, the largest single item in the region and about six times the next
   one. The earlier decision was right on the evidence it had; the evidence
   changed. **Re-run the experiments a previous theme declined, when the
   reference under them changes.**
   - **Class** — `TEDIOUS` (our tooling)

3. **Two of the three wins were four-pixel constants, and both came out of
   Puffin's source rather than out of the diff.** The transcript's scroll inset
   was 8 against Puffin's `EdgeInsets(top: 12, ...)`, which put every row in
   the pane 4px high — a sweep of hanabi's whole main pane says that offset
   alone is **1.45 points across the turns band**. The air between two turns
   was 18-19px against Puffin's 24, which it states twice (`bubbleBreathing =
   9` a side, `itemSpacing = 6` between) and comments once ("two messages sit
   24pt apart"). Neither is visible as a *difference* in a diff image — the
   whole pane just sits wrong — and neither is guessable from the PNG, because
   an offset and a size read the same in a per-region percentage.
   - **Class** — `TEDIOUS`

4. **hanabi had no run-outcome divider, and building one was easy for a reason
   worth naming: it is the third one.** Puffin closes a run with
   `runSeparator` — rule / the server's own word / rule, red only for
   `failed`. hanabi drew nothing at all: a dead run and a quiet one ended the
   same way. The brief expected "a centred label in a rule" to be hard in
   afterhours. It is not, because `date_divider` and `new_divider` already
   solved it, and both carry the same two scars in their comments — no
   flex-grow (gap #18) so the rule widths are computed by hand, and
   `percent(1.0)` in a NoWrap row eats its siblings (gap #53). The third one
   took twenty minutes by copying the second. **A library gap that has already
   been paid for three times is cheap on the fourth and still costs the same
   every time.**

5. **The model, not the layout, was where the outcome had nowhere to go.**
   Puffin's transcript is a list of ROWS with kinds, one of which is
   `runFinished`; hanabi's is a list of `api::Message` with an author. A run
   ending is not a message and has no author, and adding `Role::RunOutcome`
   would have made every switch over Role — author label, bubble fill, tool
   piling, the find index, the minimap mark — grow a case for something that
   is not a speaker. It rides on the message it follows instead
   (`Message::run_outcome`, a free string, Puffin's own choice so an outcome
   this build predates still prints). **The cost is stated in the header and
   is real: a run that produced no message cannot be drawn.** Nothing emits
   one today.

6. **A change that provably moves hanabi ONTO Puffin's constants made the
   score worse.** hanabi's fenced code was 11px in `text_secondary`
   (142,142,154); Puffin's is `monospacedSystemFont(ofSize: 12)` in
   `palette.plain`, #E6EDF3, which is hanabi's `text_primary` to within 6/255.
   Making that change costs **+0.06 points** (3.76 → 3.82), because hanabi's
   code rows sit at y 221/240 and Puffin's at 207/235: brighter ink that is
   not registered disagrees with the background it lands on twice over. Kept
   anyway — the reference's code block is white-on-dark evidence and hanabi's
   was a grey footnote — and reported, because a metric that punishes a
   correct change is a metric to quote carefully. **The pixel score finds
   gaps; it does not adjudicate them.**
   - **Class** — `FOOTGUN`

7. **The mono font's size is quantized somewhere below the call, so a 1px type
   change is silently a no-op.** Same string, same element, same line of code,
   only `theme::type` changed, ink measured off the capture:

   | requested | rendered ink |
   |---|---|
   | `SM` (11) | x 387..668 |
   | `MD` (12) | x 387..668 |
   | `BODY` (13) | x 387..725 |
   | `LG` (14) | x 387..725 |

   Two pairs, each collapsing exactly. The colour change in the same edit DID
   render, so the rebuild is not in question. I could not find the mechanism:
   `with_font(name, Size)` sets `font_size_explicitly_set`, `rendering.h`'s
   explicit branch passes the size through untouched, and the sokol backend
   hands `font_size * dpi` straight to `fonsSetSize`. **Not filed as a gap,
   because a gap that misattributes is worse than none** — but anyone porting
   a type scale should measure the ink rather than trust the constant, and 12
   is left in the source with a comment saying it renders as 11 did.
   - **Cost** — one build cycle to notice, three more to characterize.
   - **Class** — `FOOTGUN`

8. **Adding a thread to the fixture broke three tests, and every one of them
   was a fair break.** `list_navigation_opens` asserted on a sentence of the
   reply this theme replaced — re-pointed at the ported reply, and deliberately
   at a phrase short enough to sit inside ONE rendered line, because the
   transcript emits one label per WRAPPED line and `expect_text` is a substring
   match per label, so a phrase that straddles a wrap can never match.
   `select_word_and_line` and `user_turn_hugs_the_right_edge` are coordinate
   tests that moved with the rhythm (−24 for the overlay, +10 turn air, +4
   inset). Both were re-derived by rendering the thread at the script's OWN
   window size and scanning the PNG, which is what their own comments tell the
   next person to do. **No find tally moved** — the ported text adds no new
   match for any query the suite searches for.

9. **The same binary scores r5 WORSE than before (4.49 → 5.09) and that is not
   a regression.** Puffin's 24pt of inter-turn air and 12px inset are right for
   any thread; r5's own rows happened to sit closer to the reference's rows
   under the old rhythm, because r5's content is a different shape. A per-thread
   number is a property of the PAIR, not of the app. Quote `main` with the
   thread it was shot on, always.

10. **Every A/B in this theme was shot back to back and every pair was
    bit-identical** (`ImageChops.difference(...).getbbox()` is `None` for two
    runs of one binary, on r5 and on r9). The clock trap `feat/vis-turns` entry
    11 documented did not fire — the ported stamps are 320 and 310 minutes,
    which straddle no midnight at the hour this ran — but it is one wall-clock
    hour from doing so, and the fixture is now the reference's own timings, so
    it will fire on someone. The cheap fix remains a `HANABI_MOCK_NOW` epoch
    override.

### Where the remaining 3.80% is

Measured on the final binary against `ref/02_thread.png`, r9 both sides.
The turns band (y 71..340) is 8.6% of it and everything below the divider,
down to the composer, is **0.00%**.

| | reference | hanabi | note |
|---|---|---|---|
| user bubble | x 817..1097, y 97..127 | x 837..1097, y 95..129 | 20px narrower for the SAME string, and 4px taller |
| assistant bubble | x 363..1030, y 154..276 | x 362..1031, y 154..290 | top exact; 14px taller, all of it the fence |
| fence, first code row | y 207 | y 221 | hanabi's block spends 18px more above its first line |
| fence, code rows | 28px apart | 17px apart | and 11px less between them |
| run-outcome rule | y 298 | y 311 | follows the bubble |
| composer band | — | — | 9.47%, unchanged by this theme and not its business |

The user bubble's width is the typeface (settled and discarded in "The
typeface question"), and the two fence numbers pull in opposite directions:
hanabi's block is chrome-heavy and line-tight, Puffin's is chrome-light and
line-loose, and they very nearly cancel. Nothing left in this pane is worth
more than ~0.2 points, and the next real one is the composer's 27px.

**One last number, for the harness.** The final pair differs from the
second-to-last build by **exactly one row of pixels** — y=210, x 369..1025,
the hairline under an unlabelled fence's now-empty header — and that row is
worth 0.02 structural points on `main` (3.82 → 3.80). Two shots of each
binary, `getbbox()` `None` within each pair. When the harness is used the way
`REFERENCE.md` says, it resolves a single hairline; when it is not, two shots
of one unmodified binary read 4.19% and 4.66%.


---

## The code fence (no branch — done on main)

### 1. The fence punched a hole through the bubble, and the metric said it was fine

- The block filled with `theme::window_bg()`. Inside an assistant bubble, which
  fills with something else, that is a **hole clean through the bubble** — the
  reader sees one bubble, a gap, another bubble.
- It survived because window_bg (23,23,35) is within the metric's 12/255
  tolerance of the bubble's (33,33,54). **A full-width rectangle of the wrong
  colour scored as a match.** Fixing it to the reference's (19,19,27) made the
  score WORSE — 3.80% → 6.63% — because the correct colour is out of tolerance
  and the shape was still wrong.
- **This is the tab agent's rule from the other side.** They found that
  correcting a shape while its colour is out of tolerance costs points. The
  same trap: correcting a COLOUR while the shape is wrong costs points too.
  Either half alone is punished; only both together pay.

### 2. The reference's fence is per-LINE, not a panel

- Measured row by row: the long error line's dark chip runs x384..1018 and the
  "exit 65" line's runs x374..435, in the same block. Puffin puts the surface
  on the highlighted text (`SyntaxHighlighter.highlight` returns an
  `AttributedString`), so what reaches the screen hugs each line.
- hanabi drew one rounded panel. Now each line is a chip sized to its own text
  plus 6px — measured, not flex-sized, because a box cannot be told to hug its
  text (gap #87, third caller).
- Both halves together: **6.63% → 4.50%**.

### 3. An unlabelled fence had a 20px strip with nothing on it

- 63px tall for two code lines against the reference's 41. Puffin's
  `CodeBlockView` emits its header `if !lang.isEmpty || hasCopy` and tightens
  the body's padding when it does (`padding(top: showsHeader ? 4 : 8)`).
- **A zero-height bar is worse than no bar**: its three children then overflow
  it every frame and each writes a layout warning — three per frame, forever.
  Caught only because a scripted test's log was 40 lines of it. The bar and its
  children are now skipped entirely when there is no language.

### 4. Puffin highlights an unlabelled fence; hanabi coloured nothing

- `scan()` returned early on `Lang::None` — "an honest 'I don't know this one'",
  which is right for KEYWORDS and wrong for the two things no language owns.
  The reference's bare fence has its quoted path in the string colour and its
  exit code in the number colour, and Puffin calls `SyntaxHighlighter.highlight`
  with an empty language to get it.
- One line: drop the early return. Strings and numbers now colour on any fence;
  keywords still need a declared language.

### 5. `expect_text` cannot see a STYLED label

- The renderer draws the runs INSTEAD of the label, so a syntax-highlighted
  line is not in the plain-text index `expect_text` searches. It reports the
  sidebar's rows and nothing from the transcript, which reads as "the
  transcript did not render".
- Same blind spot as gap #61 one layer over: the harness can read what a widget
  was told to say, and not what it drew. `assert_ui_text` finds them by label
  and works.

### 6. The mono face is 2.3x narrower than the reference's, and no size fixes it

- The same 53-character line is 635px in the reference and 276px in hanabi.
  Swept 16/18/19/20px: 742 / 785 / 801 / 831 — still 190px short at 20, and the
  score rises at every step. It is the face (JetBrains Mono against SF Mono),
  and the reference's own is ~20pt where Puffin's source says 12.
- Left at 12, which is Puffin's number and the metric's best. **Recorded so the
  next person does not sweep it again**; that is three sweeps now (UI face,
  bold weight, mono size) that all end at the shipping value.

## Capturing at 2x (feat/vis-hidpi)

**The verdict, first: hanabi CAN render its own UI at 2x, and it does not help.
The floor does not collapse — supersampling makes hanabi's TEXT score worse,
measurably and after correcting for registration, and it makes its hand-drawn
VECTOR chrome better. The premise this branch was opened on is disproved with
numbers, and the two things it turned up along the way are worth more than the
capture would have been.**

### What the theory was

`ref/*.png` are Puffin captured at 2x on a retina panel and reduced to 1x, and
hanabi's capture is a 1x offscreen render, so every glyph in every comparison
has been a 2x-downsampled glyph against a 1x-rasterized one. `compare.py
--floor` prices that asymmetry at 8.4–11.8% in the session list and 2.2–3.3%
overall, against a list sitting at 14.07%. If hanabi rendered at 2x and were
reduced the same way, the reasoning went, the floor would largely go and every
text region would become honestly measurable for the first time.

`main.cpp`'s own comment said this was blocked upstream:

> *the Metal backend does not supersample it (Config.hidpi is honored only by
> the raylib backend) … Rendering into a 2x-sized texture does NOT help: the
> adaptive UI just lays out at the larger logical size (thin sidebar in a big
> canvas), it doesn't supersample.*

Both halves of that are true, and the second half is exactly why
`theme.ui_scale` is the answer to it: in Adaptive mode — which is the mode
hanabi runs in, `main.cpp:149` — `ui_scale` multiplies every `pixels()` value
in the tree, **explicit font sizes included** (`rendering.h:1611` and `:2179`
both resolve `cmp.font_size` through the scaling-mode-aware overload). So a
2360x1898 texture at `ui_scale = 2.0` is not a bigger canvas. It is the same
UI, twice the size.

Four things could have killed it before it started. Three did not:

- **Adaptive or Proportional?** Adaptive. `main.cpp:149` sets it globally.
- **Do literal `with_font_size(<px>)` calls scale?** Yes. Both render paths
  resolve them with `resolve_to_pixels(size, screen_h, mode, ui_scale)`. This
  was the one that would have ended the branch in a paragraph, and it holds.
- **Do `h720()` sizes and `pixels()` sizes stay in step?** In this harness,
  yes — but only by coincidence. `h720()` is `ScreenPercent`, and
  `resolve_to_pixels` returns `value * screen_dimension` for it with no
  `ui_scale` term anywhere (`layout_types.h:200-207`): it scales with the
  framebuffer, not with the zoom. The capture doubles the framebuffer as well
  as `ui_scale`, so both units land on exactly 2x and nothing is out of step.
  Zoom the app WITHOUT resizing the window and they part company by the zoom
  factor — hanabi has 19 `h720()`/`w1280()` sizes and 37 `FontSize::` tiers
  (the tier API resolves to `h720()`, `component_config.h:685`), and every one
  of them would hold still while the `pixels()` around it grew. Not measured
  here, because this branch never had a reason to zoom without resizing; noted
  because anyone shipping `HANABI_UI_SCALE` as a user-facing zoom will hit it
  first thing.
- **Anything hanabi computes in raw pixels itself.** This one did not hold, and
  it is finding #1 below.

### Finding 1 — hanabi's own panel arithmetic was in device pixels, so every rectangle it computed was scaled twice

At `ui_scale 2.0` the first 2x capture came out with **no composer, no status
bar, a sidebar whose footer had fallen off the bottom edge, and a transcript
laid out in a pane twice as wide as the one it was drawn into** — bubbles
running off the right of the frame. The app reported nothing wrong.

Nineteen call sites across ten systems read
`graphics::get_screen_width()/get_screen_height()` — the real framebuffer — and
fed the result straight back to the tree as `pixels(sw)` or
`with_absolute_position(x, y)`, which afterhours then multiplies by `ui_scale`
again (`component_init.h:433`). `layout_system.h` is the worst of them: it
computes the sidebar, tab strip, main pane, composer strip and status bar from
the window size every frame, and at scale 2 the composer's y doubles to 2792 in
a 1520-tall window.

afterhours already draws the distinction this needs — `LayoutInfo::make`
divides the screen size by `ui_scale` for precisely this reason — but only
hands it to code that asks for a `LayoutInfo`. `src/ui/viewport.h` is hanabi's
version: `width()`/`height()` are the window in LOGICAL pixels, and every one
of the nineteen sites now reads them. At `ui_scale 1.0` the divisor is exactly
`1.0f`, so it is bit-identical to what was there before.

Held by `tests/ui/ui_scale_is_a_zoom_not_a_bigger_canvas.e2e`, which asserts
the five rectangles at scale 2 and goes red on four of them without the fix.

- **Class** — `FOOTGUN` (ours, not the library's — but see gap #102 for the half
  that is the library's)
- **Gap filed?** — no. This is hanabi's bug, and the property that makes it
  invisible (an off-window widget renders silently) is #86's territory.

### Finding 2 — everything hanabi draws by hand stayed 1x inside the 2x frame

Every escape hatch this project uses for a shape afterhours cannot draw — the
five row-state marks, the mute ring, the attention triangle, the disclosure
chevron, the send arrow, the pushpin, the radio, and every icon-atlas blit —
goes through `on_draw_fg` or an immediate-mode helper. Those are handed a
widget rect afterhours has already scaled, and then every radius, stroke width
and sprite size **inside** them is a literal the library never sees. At 2x they
came out half size in a correctly-scaled frame.

Fixed in hanabi with `viewport::px()` — a multiply by the active scale, exactly
`v` at 1.0 — at 25 sites in `icons.h` and `sidebar_system.h`. Filed as gap #102,
because the reachable-only-through-a-global workaround is also wrong for any
widget that overrides its own scaling mode.

### Finding 3 — the renderer's private 5px text margin is in DEVICE pixels, so every label slides left as you zoom

Gap #75 named `position_text_ex`'s hardcoded `Vector2Type margin_px{5.f, 5.f}`
as an inset no caller can turn off. It is also not scaled, which means it is
not a constant inset at all — it is `5/ui_scale` logical pixels, and every
left-aligned label in the app slides toward its leading edge as the app zooms
in. Measured on the "Settings" nav label, taking its ink start in device pixels
and dividing by the scale:

| ui_scale | label starts at (logical px) | 5/s |
|---|---|---|
| 1.0 | 37.000 | 5.000 |
| 1.5 | 34.667 | 3.333 |
| 2.0 | 34.000 | 2.500 |
| 3.0 | 33.333 | 1.667 |

The residual after subtracting `5/s` is 31.3–32.0 across all four — flat to
within a pixel, which is the first-glyph bearing. Across the eighteen visible
row titles the same drift reads 5.5px → 2.8px of left inset, against the
reference's 6.6px.

That is a 2.7px misregistration on every string in a 2x capture, and it is
worth more than everything supersampling gains. Filed as gap #100. **It is not
the reason the experiment failed, though — see below; I measured past it.**

### The numbers, both references, all four shot back to back

Same binary, four captures in one run (the fixture-clock rule); a 1x re-shoot
afterwards differs from the first by **0 pixels**, so nothing below is the
clock moving. STRUCT over shared surfaces, RAW in brackets.

`ref/01_home.png`:

| region | floor | 1x | 2x | Δ |
|---|---|---|---|---|
| sidebar | 6.25–8.93 | 10.61 (9.72) | 11.35 (11.07) | **+0.74** |
| &nbsp;&nbsp;views | 2.31–3.88 | 4.53 (5.27) | 6.02 (6.40) | **+1.49** |
| &nbsp;&nbsp;search | 1.54–3.17 | 4.24 (4.22) | 4.21 (4.13) | −0.03 |
| &nbsp;&nbsp;list | 8.41–11.83 | 14.02 (12.36) | 14.60 (14.00) | **+0.58** |
| &nbsp;&nbsp;footer | 1.61–5.87 | 5.26 (4.68) | 4.40 (4.41) | **−0.86** |
| tabbar | 1.25–3.02 | 2.81 (2.40) | 3.53 (3.92) | **+0.72** |
| main | 0.83–1.41 | 9.13 (9.53) | 9.03 (8.09) | −0.10 |
| **SHARED** | | **9.12 (8.59)** | **9.68 (9.35)** | **+0.56** |

`ref/02_thread.png`:

| region | floor | 1x | 2x | Δ |
|---|---|---|---|---|
| sidebar | 6.25–8.93 | 10.64 (9.75) | 11.38 (11.09) | +0.74 |
| &nbsp;&nbsp;views | 2.31–3.88 | 4.53 (5.27) | 6.02 (6.40) | +1.49 |
| &nbsp;&nbsp;search | 1.54–3.17 | 4.26 (4.22) | 4.23 (4.13) | −0.03 |
| &nbsp;&nbsp;list | 8.41–11.83 | 14.07 (12.40) | 14.63 (14.03) | +0.56 |
| &nbsp;&nbsp;footer | 1.61–5.87 | 5.26 (4.68) | 4.40 (4.41) | −0.86 |
| tabbar | 1.25–3.02 | 2.03 (1.66) | 2.41 (3.17) | +0.38 |
| main | 0.83–1.41 | 4.49 (3.73) | 4.43 (3.68) | −0.06 |
| **SHARED** | | **5.84 (5.08)** | **6.00 (5.45)** | **+0.16** |

**No region drops toward its floor. The list moves the wrong way.**

### Why, and this is the part worth keeping

The obvious objection to the table is finding 3: of course 2x scores worse, the
text is 2.7px out of register. So I removed the registration. Sub-pixel x-shift
sweep of each capture against the reference, per column, taking each one at its
own best offset:

| column | 1x best | at dx | 2x best | at dx | verdict |
|---|---|---|---|---|---|
| row marks, x 0–20 | 6.92 | −0.5 | **5.67** | 0.0 | **2x wins by 1.25** |
| footer buttons, x 195–275 | 11.75 | +0.5 | **11.19** | +0.5 | 2x wins by 0.56 |
| views icons, x 0–32 | 12.05 | 0 | 11.94 | 0 | flat |
| sub-agent counts, x 240–280 | **2.61** | +1.5 | 3.37 | +3.0 | 1x wins by 0.76 |
| row titles, x 20–240 | **14.57** | +1.0 | 15.86 | +3.5 | **1x wins by 1.29** |

So the split is clean and it is not a registration artefact: **supersampling
helps every drawn shape and hurts every string.** Fixing gap #100 upstream would
recover the registration and leave that split exactly where it is.

The reason text loses is the same mechanism the dilation and gamma experiments
found from the other side (`## The floor`, above). Measured over the eighteen
visible row titles:

| | reference | 1x | 2x |
|---|---|---|---|
| mean ink px per title | 685 | 640 (−6.6%) | 662 (−3.4%) |
| mean string width | 148.5 | 148.3 | 150.6 |
| mean left inset | 6.6 | 5.5 | 2.8 |

2x closes **half** the ink deficit — the thing three investigations correctly
identified as the visible difference between fontstash's Regular and CoreText's
stem-darkened semibold. It also makes the strings **2.3px wider on average**,
because fontstash measures and advances at 33px and the result is halved, which
is not the same as advancing at 16.5px. Extra ink laid down along a string
whose advances are drifting increases non-overlap on both sides of every glyph,
and the metric charges for it. That is the dilation result again, arrived at by
a completely different route, and it is now the third independent confirmation
that **the last few points of every text region are per-glyph placement, not
coverage, not weight, and not sampling.**

The floor's premise — that the residual is rasterization PHASE, and that
matching Puffin's sampling would collapse it — is therefore wrong. Two
renderers laying the same string down at different advances do not have a
phase difference you can sample your way out of; supersampling gives hanabi
Puffin's edge softness and leaves the letters in different places, crisper.

### What shipped anyway

The 2x path is in both shoot scripts behind `HANABI_SHOOT_2X=1`, off by
default, because it is worse overall and because one measurement should not be
able to change every number in the workstream by accident. It is worth keeping
for two reasons: the row-mark result is a real 1.25-point win that gap #92
explicitly ruled out ("Drawing the shape bigger and letting the downscale
soften it — **there is no downscale**"), and there is now one. And
`HANABI_UI_SCALE` is a working browser-style zoom for the app, which it was
not before finding 1.

- **Class** — `TEDIOUS` (three days of the workstream's central assumption,
  priced) + `WORKAROUND` (#97, #99)
- **Gaps filed?** — **#97** (the 5px text margin does not scale), **#98** (no
  supersampled capture: `ui_scale` is a layout zoom and they are not the same
  thing), **#99** (`on_draw_fg` hands you a scaled rect and no scale).


---

## The transcript pane, round two (feat/vis-pane)

**`main` 4.50% → 2.87% structural on `ref/02_thread.png` / r9, both sides shot
back to back and every A/B pair bit-identical (`getbbox()` `None`). Headroom
over the floor: +3.09 → +1.45. Nothing outside `main` moved.**

### 1. Where the +3.09 actually was

The first thing worth doing was refusing to guess. `main` is 787,566 pixels and
its whole diff was 36,974 of them, so a band-by-band sweep at 10px resolution
prices every part of the pane against its own floor in one pass and says which
paragraph to read. It took two minutes and it is the reason nothing in this
theme was a hunch.

| band | what is there | struct px | share |
|---|---|---|---|
| y190..250 | **the fenced code block** | **12,766** | **34.5%** |
| y251..280 | the prose line under the fence, and the bubble's bottom | 5,843 | 15.8% |
| y91..131 | the user turn: avatar and bubble | 3,779 | 10.2% |
| y161..181 | the prose line above the fence | 1,943 | 5.3% |
| y281..311 | the run-outcome divider | 1,688 | 4.6% |
| y791..949 | the composer — **not this theme's** | 10,955 | 29.6% |
| everything else in the pane | | 0 | 0% |

Two things fell out of that table before any code was read. **The fence was a
third of the region** — more than the composer, more than everything else in
the transcript put together. And **every band that is not a turn or the
composer is EXACTLY zero**: the pane's own furniture, its gutters, its top
inset and the whole 480px between the divider and the composer are pixel-exact
already. There is nothing left in this pane that is not a message.

### 2. The fence's dark surface is per-line, and only the LAST line hugs its text

This is the finding, and it is a correction to the entry above this one.

`## The code fence` measured the reference's two chips as x384..1018 and
x374..435, concluded "per-LINE, not a panel", and built per-line chips that hug
their own text. Half right. The short line hugs; **the long one is 635px of
surface behind 419px of text, and runs to the bubble's own inner edge.** The
earlier pass measured `"exit 65"`'s chip, generalised from it, and never
checked the number it was generalising to — 635 was even quoted in that entry's
own text, as the reference's mono line WIDTH, in the paragraph that concluded
the mono face is 2.3x too narrow. It is not a text width. It is a chip.

The rule is TextKit's and the fixture shows it. The fence's content is
`"error: …\nexit 65"`: the first line is terminated by a newline and the last
is not, and a background attribute drawn over a line fragment is stretched to
the container's trailing edge by the newline glyph. **Every line but the last
is full width.**

Two notes on provenance, because they pull opposite ways here:

- **Puffin's source says PANEL, and I did not follow it.** `CodeBlockView` is
  `VStack { … }.frame(maxWidth: .infinity).background(codeBackground)
  .clipShape(RoundedRectangle(cornerRadius: 8))` — one full-width rounded
  surface behind the whole block, and `SyntaxHighlighter.highlight` sets no
  background attribute at all. The frozen frame contradicts it flatly: at
  y230, x374..435 is (19,19,27) and x436..1018 is the bubble's (33,33,54). The
  checkout is v0.5.2 and the reference is v0.5.5. **Priced, because "follow the
  source" would have been the defensible choice and it is the expensive one:** a
  full-width panel over both lines is 27,132 px of dark where the reference has
  14,637, so 12,495 px wrong — very slightly worse than the hugging chips this
  replaced. Two readings of the same authority, a 12,000-pixel gap between
  them, and only the PNG can tell you which.
- **The same disagreement runs through the vertical.** `CodeBlockView` spends
  8px above its text and 8+4 below inside a `VStack(spacing: 6)`; run that
  arithmetic and the prose under the fence lands at y263. The reference puts it
  at y254, above the block's own stated bottom padding. hanabi's constants are
  measured off the frame — 15px of margin above, 1 below — and say so in their
  comment, because a reader who checks them against the Swift will find they
  disagree and deserves to know that was deliberate.

**What it was worth.** Full-width chips for every line but the last, plus a
21px code line pitch against the prose's 16 (the reference's chips are 21 tall
and stack with no gap; sharing one constant with prose had the fence five
pixels a line tight), plus the margins above: the fence band went **12,766 →
3,017 px** and `main` went **4.50% → 3.09%**.

### 3. A 656px hairline the metric could not see, in the strip a previous pass had already removed

The entry above says an unlabelled fence's empty header strip was "skipped
entirely when there is no language". Its three CHILDREN were. The bar itself
was still emitted at `pixels(kCodeBarBareH)` = zero height — **and a
zero-height div still paints its border.** It carried
`with_border_bottom(code_bg(), 1)`, so hanabi drew a 1px rule of the fence's
own dark colour clean across the assistant bubble at y190, where the reference
draws nothing.

**It cost 0.00 structural points, and it is the clearest thing in the
before/after crop.** A single row of (19,19,27) on (33,33,54) is 14/255; the
0.8px structural blur spreads it and the 12/255 tolerance eats what is left, so
the comparison never sees it. That is REFERENCE.md's "a wrong thing can score as
right" from a new angle — not a wrong colour inside tolerance, but a *right*
colour on a shape one pixel too thin for the metric's own smoothing to survive.
**The only reason I found it is that I cropped both frames and looked at them
before I ran anything**, which is the instruction in the brief and the one that
paid.

It also cannot be regression-tested. Filed as **gap #104**: `assert_ui` reads
x/y/w/h/hidden/text and a border is none of them, there is no "this element is
absent" assertion, and a 0-height bar and a missing bar have identical
geometry — so the one class of change this project makes constantly, deleting
chrome the reference does not draw, is the one class it cannot hold. This strip
has now been removed twice.

### 4. The run-outcome rule: a colour and a position, and neither pays alone

The rule sat at y296 in `theme::border()` (62,62,72); the reference has it at
y299 in (48,48,62). That band was 1,688 structural pixels — after the fence,
the largest single item left, and unlike everything else around it almost none
of it was floor (0.07–0.45 against 17.45).

Puffin's colour is not a token hanabi had: `runSeparator` fills its rules with
`Color(mutedText).opacity(0.25)`, which over the window ground resolves to
about (53,53,65). The reference's peak reads (48,48,62) because the 2x
downsample spreads a 1px line over three rows — summed over the ground those
three rows carry 30 units of ink and one crisp row at (53,53,65) carries 29,
which is the same line drawn two ways. `border()` carries 39.

Moved and recoloured **together**: **1,688 → 100 px, 0.19 points, AT FLOOR.**
This is the trap this region has sprung twice, taken deliberately from the
start: the position alone leaves a line 14/255 out of tolerance in the right
place, the colour alone puts an in-tolerance line three rows from where it
belongs, and each half on its own is worth approximately nothing. I did not
measure the halves separately, because the two previous entries already paid
for that lesson in both directions and a third confirmation is not worth a
build.

The date divider took the same colour, unmeasured and on purpose: no reference
frame contains one, and two greys of rule in one pane is a defect a reader sees
and this metric cannot.

### 5. Two experiments a previous theme had closed, re-run because the ground moved — both stayed closed, and one of them for a new reason

`## Transcript fixture` entry 2 is the standing rule here: re-run what a
previous theme declined when the reference under it changes. Both of these
changed.

**The mono size.** The brief says three sweeps have ended at the shipping value
and not to sweep again, and it is right — but the last sweep was **confounded**
and it is worth saying how. It scored 12/16/18/19/20 while the chip HUGGED its
text, so every step moved the ink and the surface behind it together, and the
surface is twenty-one rows deep against the ink's nine. With the chip now sized
by the block, a size change moves only the ink: a genuinely different
experiment, and the first one that isolates the thing everyone has been trying
to measure. One value, not a sweep — `BODY` (13), the only step the old sweep
skipped and the only one between 12's 270px of ink and 16's 730 against the
reference's 419. **2.87% → 2.90%.** Worse, and now worse for a reason nobody can
attribute to the chip. Reverted. Ink at 13 is 335px against 12's 280 and the
reference's 420, so it closes a third of the width and loses more than that in
placement — the same trade the 2x capture found, one face down.

**The body ink's brightness.** hanabi's transcript text is `text_primary`
(224,224,230) and Puffin's is `lightText` (237,237,242) — the same token in
Puffin serves the user bubble, the assistant bubble and the avatar's glyph, and
hanabi is 13/255 under it on all three, right on the metric's 12/255 tolerance
edge. Measured off the frames: the reference's prose peaks at (249..253) where
hanabi's plateaus at exactly 224, and the reference's user-bubble text peaks at
(246,246,252) against hanabi's (220,219,227). It looks like an easy point.
**It is worth 0.00.** `main` reads 2.87% before and 2.87% after, raw 2.67% both,
and only the main pane's pixels move at all (`getbbox()` is confined to the
pane, so `text_primary` is not what the sidebar draws its rows with — worth
knowing before anyone reaches for it). Reverted, because a global palette token
should not be moved for a parity match in one pane that pays nothing.

The reason it pays nothing is the third finding in a row pointing the same way:
brighter ink only scores where both frames HAVE ink, and hanabi's glyphs are
not where Puffin's are. This is the dilation result, the gamma result and the
2x result again, arrived at from the colour axis this time. **The last of every
text region is placement. It has now been confirmed four ways.**

### 6. A space I was sure had been eaten, and had not

In the first side-by-side crop hanabi's fence plainly read `matched'fbmacos…`
against the reference's `matched 'fbmacos…`, and the mechanism was sitting
right there in the library: `draw_runs_in_rect` advances its pen by
`measure_text`, and `measure_text` returns fontstash's ink BOX rather than the
advance it also returns, so a run ending in a space should lose it.

It does not. Measured properly — the ink start of every word on the line, which
in a monospace face is a ruler — hanabi's cells run 5.00, 5.33, 4.875, 5.00,
4.875 px and the styled boundary is the 4.875 one: **1px tight over eight
character cells, not one space.** The reference's own boundary is 7.75 against
its 7.33 average, i.e. slightly wide. The whole apparent defect was a 5px space
next to a 7.7px one at a face 1.5x smaller, at 2x magnification.

I nearly filed it. The reason I did not is `## Transcript fixture` entry 7's
rule — a gap that misattributes is worse than none — and the check that caught
it was arithmetic on the glyph cells rather than a second look at the crop.
**A pixel crop is the right tool for finding a candidate and the wrong tool for
confirming one.**

The underlying library defect is real and is filed as **gap #103**, on the
evidence that actually holds it up: `theme::text_px("exit 65")` returns 30px
where the advance is 35, so the last chip — the one that still hugs — is drawn
5px short, and a quarter of its 20px shortfall against the reference is this
rather than the face. The two `measure_text` overloads in the same backend file
disagree about what the width of a string is; one takes `bounds[2]-bounds[0]`
and one takes the advance.

### 7. What is left, and why I am leaving it

`main` at 2.87%, floor 0.83–1.41, and the composer is 30% of what remains and
is not this theme's.

| what | struct px | is it closeable |
|---|---|---|
| the composer band | 10,955 | someone else's region |
| prose under the fence | 4,178 | ~2,500 of it is floor; the rest is placement |
| the user turn | 3,779 | the typeface (declared in REFERENCE.md), plus 2px of bubble height |
| the fence's ink | 3,017 | the mono face; §5 |
| prose above the fence | 1,943 | ~1,000 floor, rest placement |
| the divider | 100 | AT FLOOR |

The user turn is the only line in that table I want to expand, because
REFERENCE.md's number for it is now wrong. It says the user bubble is **20px**
narrower than the reference's. On the ported fixture, measured today, it is
**45**: x818..1097 against x863..1097, for the same 42-character sentence, with
the avatar carried along beside it. The direction and the cause are unchanged
and the conclusion stands — a bubble is shrink-to-fit, so its width is its
text's width, and padding it out to Puffin's number would take away the one
property the shape exists to have. But it is a quarter of the transcript's
remaining diff rather than a handful of pixels down one edge, and anyone
weighing the typeface question again should weigh it against 45.

### 8. Things checked and found already right, so nobody checks them twice

Every one of these was a candidate in the brief and none of them is a gap:

- **The space between turns.** The assistant bubble's top is y154 against the
  reference's y153, and the first prose line inks at y164 in both.
- **The assistant bubble's own padding, and its corner radius.** inkdiff pairs
  the two bubbles at x374 w646 vs w645, h103 vs h103 — one pixel of width over
  a 646px box, and the rounding is indistinguishable.
- **Where the pane's content starts and ends vertically.** Bands y71..91 and
  y311..791 are 0.00 in both frames. The 12px scroll inset from
  `## Transcript fixture` is still right.
- **Anything hanabi draws that Puffin does not, or vice versa.** Nothing, once
  the hairline in §3 was gone. Every non-zero band in the pane contains a
  message.
- **The avatar's disc.** 20px, same fill, 6px left of the bubble and 6px below
  its top — settled two themes ago and still exact. Its GLYPH is dimmer than
  the reference's (peak 201 against 247), which is §5's `text_primary` again
  and is worth the same 0.00.

### 9. The harness behaved

Ten captures across five builds, every A/B pair shot back to back, every pair
`ImageChops.difference(...).getbbox()` `None`. The fixture-clock trap that
`## Transcript fixture` entry 10 predicted would fire on someone did not fire
here either — same 320/310-minute stamps, same hour of the day. It is still one
wall-clock hour from doing so and `HANABI_MOCK_NOW` is still the cheap fix.

---

## The status bar's 26px (feat/vis-statusmove)

### 1. The duplicate had already gone wrong, and that is what decided the design question

- **What I wanted** — to know whether hanabi's bottom strip should move to the
  sidebar footer (where Puffin keeps its only bottom chrome), be overlaid so it
  stops reserving height, or be kept and declared.
- **What I found before writing any code** — hanabi was drawing the same fact
  twice with two different numbers, in every capture this workstream has taken.
  The strip said **"3 blocked on you"**. The sidebar's Blocked row, 800px away
  in the same frame, badged **6**. The strip counted `s.tag ==
  ThreadTag::Blocked`; the badge counts `ecs::model::in_blocked_view`, which is
  `Blocked || Failed` — Puffin's own `case .blocked` rule, and the one the
  reference's own badge of six confirms. `main.cpp` had inherited the strip's
  copy for the macOS menu bar, so the menu bar said 3 as well.
- **Why that settles it.** "Do not delete the information" was the constraint,
  and the information is not in the strip: it is on the badge, correctly, and
  in `menubar.mm`'s `status_for_blocked` in words, system-wide. What the strip
  held was a second rendering of it, and a wrong one. Deleting a duplicate that
  contradicts the original is not deleting a feature.
- **The general lesson, and it is cheap.** Before deciding where a piece of
  chrome should live, grep for every other place the app states the same fact.
  Two of the three answers here were already in the codebase, one of them in
  `REFERENCE.md`'s own "Where Puffin puts 'N blocked on you'" section, written
  by an earlier branch and never acted on. The third was a bug.
- **Class** — `PROCESS`.

### 2. A rectangle over chrome does not cover the displacement that chrome causes

- `compare.py` declared "bottom status bar" `(283, 922, 1180, 949)` at 0.233
  structural points, on the correct reasoning that Puffin has no such surface.
  What no rectangle covered was the **26px the strip reserved**, which pushed
  the composer, its meta row, its pills and its rule out of register: **0.630
  points**, 2.7x the declared entry, and undeclarable — the same band carries
  ordinary closeable differences and a rectangle over the lot would hide them.
- **The strip's cheapness was the trap.** Its fill was `theme::sidebar_bg()` —
  (23,23,35), the identical colour Puffin's empty window paints there — so the
  entry priced at 0.233 and read as settled. Everything expensive about the
  strip was somewhere the rectangle did not reach.
- Before declaring chrome, ask what the chrome MOVES. That question has no
  entry in the exclusion table and it was worth three times the entry.
- **Class** — `PROCESS`.

### 3. Points and rates, in the direction that looks like a regression

Shot back to back on one binary (two captures of each build, byte-identical to
each other, so the mock's wall-clock divider did not move):

| | before | after |
|---|---|---|
| whole frame, structural | 5.00% | **4.38%** |
| shared surfaces | 4.82% | **4.21%** |
| `main` | 3.10% | **2.20%** |
| `footer` | 5.26% **AT FLOOR** | **8.19%** (+2.32 over floor) |
| composer band alone (y≥845) | 0.825 frame pts | **0.342** |

The footer's rate nearly doubled and it is not a regression: in frame points —
the currency that adds — the footer went 0.064 → 0.089 and `main` went 2.186 →
1.550. The count costs **0.025**, the register buys **0.636**, a 25:1 trade.
The rate moved because that region is 1.1% of the frame, so 270px of new ink is
three points of it. `feat/vis-divergences` wrote down that points and rates are
different currencies; this is the first time the difference has pointed the
wrong way, and anyone reading the region table alone would have reverted this.

### 4. Two separate metric changes on one capture, reported apart

`compare.py`'s "bottom status bar" entry is DELETED, not edited: the divergence
closed, and a rectangle left over the band would now hide live composer surface
— exactly what the `<-- STALE? excludes nothing` guard exists to catch, and it
would not have fired, because the composer under it does differ.

On the *same* baseline capture, metric change alone: whole frame 5.00% → 5.00%
(unchanged — the headline does not use exclusions), shared surfaces 4.70% →
**4.82%**, `main` 2.87% → **3.10%**. Then the app change, on the new script:
4.82% → **4.21%**. Quoting 4.70% against 4.21% would have credited the app with
0.11 points that were the script.

The full four-way, because it also shows why the entry had to GO rather than
stay:

| | old script | new script |
|---|---|---|
| baseline binary | shared **4.70%**, main 2.87% | shared 4.82%, main 3.10% |
| final binary | shared 4.31%, main **2.27%** | shared **4.21%**, main **2.20%** |

The old script on the NEW binary reads `main` 2.27% against the new script's
2.20% — a rectangle sitting over 27 rows of live, differing composer surface,
flattering the score by 0.07 points and hiding whatever is under it. It would
not have tripped the `<-- STALE? excludes nothing` guard either, because what
is under it now *does* differ. That guard catches a rectangle over agreement;
nothing catches a rectangle over disagreement, which is the more dangerous one.

### 5. The scripted runner ignores `window_height` in `# settings:`, silently

Every bottom-anchored assertion I wrote against the reference's own 949px
window measured against 760 instead. `run_ui_tests.sh` writes the `# settings:`
blob to the isolated HOME, but the uitest binary sizes its offscreen surface
from `HANABI_WIN_W`/`HANABI_WIN_H`, so the height in the blob reaches nothing.
The failure is a wrong number, not an error — `composer_bar y=851 but got 662`
— and it looks exactly like a layout bug you just introduced.

`ui_scale_is_a_zoom_not_a_bigger_canvas.e2e` is the only script in the
directory that passes both, in an `# env:` line, which is why it is the only
one whose bottom-anchored numbers were ever right. The new test does the same
and says why in a comment above the settings line.

### 6. Six tests broke on one 26px move, all on the same literal coordinate

`click 646 674` appeared in six scripts, copied forward, four pixels inside the
composer field's top edge. Moving the composer down 26px put that point on the
transcript; the typing went nowhere and every script failed on a symptom
(`Text not found: 'enter should reply here'`) that says nothing about the
cause. They are `click_ui composer_reply_input` now — the widget's centre, by
name — which is both a correct translation of the intent and immune to the next
person's layout change. The runner already had `click_ui` and five other
scripts already used it.

### 7. A placeholder's colour is not a property of the widget, and the escape gap #90 forbids works anyway

hanabi's composer hint inks at (94,94,106) against the reference's (141,141,165)
— the largest colour gap left in the band. `text_input` hardcodes
`field_label.explicit_text_color = ctx.theme.font_muted` and has no
`with_placeholder_color`, so the hint wears whatever the pane last left in one
global field: `text_faint`. Gap #105.

Gap #90 says a per-widget colour is a frame-wide edit because `ctx.theme` is
read at RENDER time — and a save/restore around the build call works here
anyway, because this particular line **copies a concrete colour into the entity
during the imm build** rather than resolving a `Theme::Usage::*` at render
time. The window is exactly one call wide. Worth knowing which half of #90 you
are up against before concluding you cannot scope a colour.

**And the same swap that made the footer WORSE makes this one better.**
`text_faint` → `text_secondary` cost the sidebar footer 0.5 points
(`feat/vis-tabs3`) because Puffin's 9pt SF Symbols never reach their own colour
while hanabi's sprite blits do, so matching the token overshoots. Here both
apps' 13px body text reaches full coverage — the reference's own peak is
(141,141,165) against a (140,140,166) token — so matching it is right. The rule
is not "never match Puffin's token". It is "measure what LANDS, not what is
declared", and the answer differs by type size within one app.

It is worth 68 of the hint row's 962 differing pixels, and the reason it is
small is that the two strings differ: brighter ink lands on the reference's
glyphs for the shared `Message ` and on bare background after it. Kept for the
colour, not the 68px.

### 8. What is left in the composer band, measured, with the real divergences named

3,829 differing pixels, down from 9,268. Profiled by column against the
reference:

| what | px | share | closeable? |
|---|---|---|---|
| the pill row, x 892..1098 | 1,861 | 48% | **NO** — Puffin's three disclosure pills (Tools / Thinking / Deliveries, `AgentcloudChatView.swift:197`) against hanabi's one fold-mode pill. A real feature difference. |
| the placeholder, x 366..517 | 894 | 23% | **mostly NO** — "Message Agentcloud… (↵)" against "Message hanabi…". The app's own name, and the ↵ hint cannot be drawn at all: Roboto has no U+21B5 and a missing codepoint paints nothing (gap #48), and a placeholder cannot carry a drawn glyph because the widget owns the string. |
| the model + meter, x 360..512 | 774 | 20% | **NO** — "Opus 5 (high)" and a 48x5 track at 0% against "Server default (High) ~61 tokens". The mock reports no denominator, so the meter cannot draw; `context_bar_needs_a_denominator.e2e` exists to stop anyone inventing one. |
| the send disc, x 1082..1101 | 247 | 6% | **partly** — see below. |

The input box's own four edges, the hairline at y=851 and the box's interior
now contribute **nothing**: the register is exact. That is the whole of what
this branch was for, and it is visible as an absence in the row profile.

**The send disc, measured and left alone deliberately.** Both discs span
y=899..916. hanabi's is x=1083..1101 and the reference's is x=1082..1099, so
hanabi's is 1px wider and 2px right — `kSendDia = 19` with a comment reading
"Puffin's is a 19px CIRCLE", and the reference draws 18. hanabi's fill is
`disabled_bg()` (44,44,50) with a light arrow; the reference's is (82,82,100)
with a DARK arrow, i.e. Puffin does not dim a send button it will not honour
and hanabi does. That last one is a product statement, not a defect, and
changing it means either a new token or dropping the disabled semantics — so it
is written down here rather than swapped for 0.02 points. hanabi's disc is also
a visible OCTAGON where the reference's is a circle, at a corner radius of
exactly half the side; that is the renderer's segment count and belongs
upstream.

### 9. Cropping the band was how I found the dot touching the digits

The first build put the activity light 1px from the "2" of "20 sessions",
because a right-aligned label sits 5px inside its own box (gap #84) and I had
measured the gap from the box rather than from the ink. The metric could not
see it — 0.003 points — and it is the first thing the eye lands on. `--regions`
would never have said. Both traps in the brief are the same trap: look at the
band.

### 10. Deleting a surface deletes everything gated on it, including the thing nobody looks at

The strip appended `  ·  backend: mock` under `HANABI_DEBUG`, and nothing else
in the UI prints `app.backend_label` — grepped, after the strip was already
gone. An env-gated dev affordance is exactly the kind of information that
survives a "do not delete the information" review by not being noticed, because
it is invisible in every capture and every user's app. It is in the footer
beside the version now, same gate, and the default capture is **byte-identical**
to the one taken before it was added, which is the only proof worth having that
a conditional costs nothing.

---

## The sidebar, round three (feat/vis-sb3)

Sidebar **10.64% -> 10.18%** structural on `ref/02_thread.png`, headroom over
the floor **+1.71 -> +1.24**. Search is at floor. The session list's whole
glyph column is now BELOW its own floor and can be closed.

### Read this first: the floor is computable for any rectangle, not just the seven named regions

`compare.py --floor` prints a floor per REGION, and that is what let two
regions be declared finished. The same arithmetic works on any rectangle you
name, and that is the half nobody had: a paste test alone says "the glyph
column is worth 0.73 points" and cannot tell you that 0.73 is roughly what a
perfect copy of it scores anyway.

`scripts/ceiling.py` (new) prints both, per rectangle:

| rectangle | main | ceiling | floor | verdict |
|---|---|---|---|---|
| views/icons | 13.56% | 0.00% | 4.57–8.55 | +5.01 |
| views/labels | 3.46% | 0.00% | 1.77–3.22 | +0.24 |
| views/badges | 9.19% | 0.00% | 1.72–3.64 | +5.55 |
| search | 4.26% | 0.38% | 1.54–3.17 | +1.09 |
| list/marks | 7.63% | 0.02% | 3.22–5.38 | +2.25 |
| list/titles | 17.21% | 0.04% | 10.54–14.64 | +2.57 |
| list/counts | 3.37% | 0.05% | 1.60–2.63 | +0.75 |

**That table is the anatomy of the +1.71** and it reorders the work
completely. The list's eighteen row titles are 12.8 of the region's 13.6 points
and everyone knows it — but they are only **+2.57 over their own floor**, and
that residual is the same CoreText-against-fontstash deficit four previous
rounds measured. Meanwhile a 34x26 pixel filter icon was carrying **+1.09**,
83% of an entire region's headroom, and a column of eighteen 9px marks was
carrying +2.25. Neither had ever been priced against its own floor.

Same table after this branch:

| rectangle | main | sb3 | |
|---|---|---|---|
| list/marks | 7.63% | **1.95%** | AT FLOOR — done |
| search | 4.26% | **3.20%** | floor is 3.17 |
| views/badges | 9.19% | 6.54% | +2.90 left |
| views/icons | 13.56% | 12.44% | +3.89 left, and blocked — gap #108 |
| list/titles | 17.21% | 17.21% | untouched, unreachable |

### The filter affordance was the wrong drawing, and the source said so in as many words

hanabi drew Lucide's `sliders-horizontal` — three rules with a knob riding each
one, which is a settings control. Puffin draws
`Image(systemName: "line.3.horizontal.decrease")`, right there in
`SidebarColumn.searchRow`. Three rules, decreasing, no knobs.

Measured off the frozen PNG at half coverage: **12 / 10 / 7 wide, ~1.3px thick,
one centre x, 3.25px pitch.** Lucide's own `list-filter` is NOT a substitute —
its bars run 18/10/4, so its bottom rule is half the length it should be. Drawn
from primitives instead.

**SEARCH 4.26% -> 3.46% on that one icon** — 88% of its measured ceiling, which
is a hit rate this workstream has not seen. The reason is worth stating,
because it is the counterexample to the `feat/vis-list2` finding that a
correct fix wins a fraction of its ceiling: **that finding is about TEXT.** A
ceiling assumes the reference's own rasterization comes with the fix, and for
a string it never does — but a rule is a rectangle, and hanabi can put a
rectangle exactly where Puffin put one. Shapes pay their ceiling; strings do
not.

The previous round had a hand-drawn version here and replaced it with the
sprite, correctly, because the hand-drawn one laid down 39px of ink against 95
and in a colour 90 levels too dark. The lesson is not "don't hand-draw"; it is
that neither version had been measured against the reference's actual glyph.

### Antialiasing you paint yourself — and it corrects gap #92's own escape list

Gap #92 says primitives cannot be antialiased, lists the escapes, and rules out
"a second, dimmer pass" because the fill path cannot alpha-blend a shape over a
shape. That is true and it is the wrong reason to stop.

**A partly covered pixel is not a translucent pixel.** Over a background whose
colour is known, coverage `c` composites to the OPAQUE colour
`bg + c*(fg - bg)`, which the caller can compute. No blending is involved at
any point. And coverage of an axis-aligned rectangle is separable, so a
fractional rect is at most nine solid rectangles — three column bands by three
row bands. That is `hanabi::glyph::rect_aa`.

What it bought, on the bang — the mark on six of the eighteen visible rows, and
the one carrying the most ink error in the column:

| | x14 | x15 | x16 | total |
|---|---|---|---|---|
| reference | 0.44 | 0.97 | 0.50 | 1.91 |
| hanabi, hard 2.3px stroke | 1.00 | 1.00 | 1.00 | 3.00 |
| hanabi, `rect_aa` | 0.47 | 1.00 | 0.47 | 1.94 |

Every row of the stroke and every row of the tittle now agrees with the
reference's to within 0.05 coverage. **list 13.79% -> 13.57% on that one
mark.**

Three limits, filed as gap #106, and the second is the one that will bite
someone:

1. **Axis-aligned only.** The arc, the cross and the chevron in the same
   vocabulary cannot have it, so the sidebar now draws two soft-edged marks
   beside three hard ones.
2. **It bakes the background in.** `draw_mark` had to grow a `bg` parameter and
   the session row had to hoist its hover-fill decision above the glyph slot to
   pass it. A caller that forgets gets a halo **under the pointer and nowhere
   else** — a state no reference captures and no screenshot test shoots.
3. **Nothing non-flat.** A gradient, an image or a translucent surface behind
   the glyph breaks it silently.

### Every mark was drawn to the reference's OUTER extent, which is 30–85% too much ink

The whole vocabulary, measured against the frozen PNG:

| mark | rows | ref ink | hanabi ink | over |
|---|---|---|---|---|
| bang | 6 | 14.8 | 30.0 | **x2.03** |
| chevron | 1 | 10.8 | 20.0 | x1.85 |
| cross | 2 | 17.6 | 30.0 | x1.70 |
| arc | 4 | 24.5 | 38.0 | x1.55 |
| dot | 4 | 33.5 | 44.0 | x1.31 |

Not a placement error — `feat/vis-list2` already swept placement and got 0.01
points for it. **A sizing error with one cause: the constants were derived from
where the reference's ink ENDS, and the reference's ink ends in a soft
fringe.** Without antialiasing (gap #92) a mark drawn to that outer extent
fills the fringe solid. The right target when you cannot antialias is the
reference's **half-coverage silhouette**, which is what a vector renderer's
shape actually is; the fringe outside it is coverage you are structurally
unable to produce, so reaching for it only adds ink.

Re-derived that way: dot r 3.9 -> 3.4, cross half-extent 3.9 -> 3.0 and stroke
2.0 -> 1.6, chevron half-height 4.2 -> 3.6 and stroke 2.0 -> 1.6, arc ring
3.0..4.8 -> 3.3..4.6. Every one helps, and they compose: **list 13.93% ->
13.79%.**

This is the dilation experiment from `## The floor` arriving from the other
side. That one added ink and the score got monotonically worse; this one
removes ink hanabi should never have had and the score gets monotonically
better. The deficit under a TEXT region is not coverage weight — but the
surplus over a SHAPE region is.

### The running arc's gap was in the wrong quadrant, and the source could not tell me

hanabi drew the spinner arc with its gap in the LOWER LEFT, under a comment
saying so. The reference puts it at the **TOP**. At a glance hanabi's mark
reads as a hook or a question mark where the reference's reads as a bowl — on
four of the eighteen visible rows, and they are the four rows that are running.

Measured on all four, which are identical to the pixel: centre (15.0, mid−0.5),
ink over **290 degrees**, gap **70 degrees wide centred on 275.5** — five
degrees clockwise of straight up. In afterhours' convention (0 = three
o'clock, increasing clockwise) that is −49 to 240 against the shipped −170 to
85.

**Which source I used, and why.** The PNG, alone.
`~/kt-ng2w-puffin`'s `SessionRowView.statusDot` is a 7pt filled `Circle()` with
no arc anywhere in it — the five-shape vocabulary arrived after v0.5.2, exactly
as REFERENCE.md warns. Read the checkout here and you would conclude hanabi
should draw a dot. (The brief's path for that file is also stale: it is
`Sources/Views/HomeSessionList.swift:1037`, not `SessionRowView.swift`, which
does not exist.)

Worth 0.13 points across list and search. Ship it anyway — it is a spinner and
it now looks like one.

### A blit reaches its colour and 9pt text never does, so one token cannot serve both

Puffin hands ONE colour to the whole smart-view row — `SmartViewSidebar` line
297, `Chrome.mutedText` for the icon and the label together — so hanabi using
one constant for both looks like the faithful reading. It is not.

`kViewLabelFg` (150,150,175) was measured off the LABEL. It is ten levels above
Puffin's actual token (140,140,166) *because* that is what it takes for
antialiased 9pt text to read like the reference's. Hand those ten levels to a
sprite blit, which reaches its colour in full, and every view icon peaks 15–22
levels above the reference's. **Two ink constants in one row is the faithful
implementation of one colour**, and this is the sidebar footer's colour
finding (REFERENCE.md) generalized: there, moving footer text to the nearest
token made it worse; here, moving the icons to the true token makes them right.
Same fact, opposite direction, because one is text and one is a blit.

**views 4.53% -> 4.41%.**

And a warning attached to it: the score keeps improving as the icon ink
darkens **past** the measured value — 4.42% at the true token, 4.39 at 135,
4.34 at 130, 4.31 at 125, monotonically. That is the metric paying for less
ink, the same way it pays for smaller text, and the only defensible place to
stop is the constant Puffin's own source names. Everything below it is fitting.

### The badge was a pixel low, and nothing about the row could fix it

The smart-view count badge measured ring y140..156 against the reference's
y139..155 — one pixel down — while the label, the icon and the row's whole
content were exactly on the reference's. So every lever that moves the row was
a regression somewhere else, and the row's padding was already swept to 6/4
against the reference's own label rows in an earlier round.

The lever that works is a **margin on the badge alone**: under
`AlignItems::Center` the row splits what is left, so 2px of bottom margin under
a 16px badge in a 22px content box is a 1px rise. Measured back: ring
y139..155, digit y143..150, both exactly the reference's rows.

**views/badges 8.88% -> 6.54%** — the largest single move in the region, from
one pixel.

Its digit was also two sizes small: `theme::type::SM` gave 7px of ink by 4
against the reference's 8 by 6, twelve lit pixels against thirty-two. At 14.0px
it is 8 tall and reaches its own colour. Its ink was re-measured by the
(pixel − fill) ratio across every digit pixel of the 6 and 3 badges —
0.641/0.803 and 0.643/0.804, **agreeing to two thousandths on two independent
badges**, which is what says a read is the colour and not the coverage — and
came back ten levels of red brighter than the constant.

What is left there is the digit at 5 columns against the reference's 6 and 20
lit pixels against 32, which is Roboto against SF at 8px. The rows match
exactly; the strokes cannot.

### Three things I could not reach, priced so nobody re-derives them

**The selected view's fill — gap #107.** It is 1.3px too tall and its corner
radius is 3.5 where the reference's is **8**. (The earlier round read the
radius as ~5 from "the fill's first row spans x3..276"; that row is the
SECOND. The corner keeps curving for six more rows, and r=8 fits the whole
profile to a pixel.) It cannot be fixed because the fill IS the row's
background box, so its height, position and radius are the same numbers as the
row's padding and pitch. Everything tried, views 4.41% before each:

| change | views |
|---|---|
| radius 5 → 8 | 4.44% |
| radius 5 → 11 | 4.48% |
| margin top 1→2, padding top 6→5 (drop the fill, hold the content) | 5.36% |
| inset 3→4, top 1→2, pad 6→5 | 4.81% |
| inset 4.3, top 1.7, pad 5.3 (the measured 27.7px height) | 5.13% |

The radius alone gets worse because the fill is *also* a pixel high and the
corner is where the two errors meet — trap #1, in its purest form. And the
other half cannot be applied: the fractional margins that would give the fill
its real height are also the six rows' pitch, so the rows drift.
`with_on_draw_bg` is the real answer and it means re-implementing the selected
and hover fills by hand for six rows plus the folded rail, for 0.37 points.
Measured, unspent, filed.

**The view icons' stroke — gap #108.** They carry 1.24–1.57x the reference's
ink inside bounding boxes that already agree to a pixel, so it is stroke:
Lucide draws at `stroke-width="2"` on a 24 grid, SF Symbols at this weight is
nearer 1.5. Not the size — swept, 14px 4.48%, 15px 4.45%, **16px 4.41%**, 17px
4.54%, so the shipping size is already optimal. Not the colour — that half is
fixed above. The only fix is regenerating the atlas, which is one sheet shared
with the tab strip and the footer, and **both of those are AT FLOOR**.

**The sub-agent count column.** Its "1" measures 1.3px left of the reference's
and one row taller. Swept size x position — 13.5/9.0, 12.5/9.0, 12.5/7.7,
13.0/7.7, 12.5/6.7, 13.5/7.7, 13.5/7.0 — and **nothing beats the shipping
value**; every move that puts the stem on the reference's column costs 0.02.
0.55 of ceiling, 0.75 over floor, and it is one digit of text. Left alone.
`kBadgeRightPad` was decoupled from `kCountRightPad` while establishing that:
they are two columns in two different lists and they measure out differently,
and one constant could not be right for both.

### No new test, and that is a finding

Every fact this branch changes is a pixel fact — a gap's angular position, a
stroke's coverage, an ink's twelve levels — and the scripted harness cannot see
pixels. `assert_ui` reads x/y/w/h/hidden/text and nothing else, which is
gap #86 already filed from the other direction. The 80 scripted tests all still
pass, including `selected_view_fill` and `smart_view_badge`, because nothing
this branch touches is expressible in what they can assert.

So the guard is the parity harness, run by hand, and here are the numbers a
regression would show: search 3.20 (floor 3.17), list/marks 1.95 (floor
3.22–5.38), views/badges 6.54, sidebar 10.18. `scripts/ceiling.py` prints all
four in one command.

### For the next person

- **Price a rectangle against its own floor before you touch it.**
  `scripts/ceiling.py`. Four rounds of work on this sidebar priced ceilings
  only, and a ceiling cannot tell a 12-point element that is 2 points from
  perfect apart from a 1-point element that is 1 point from perfect.
- **Shapes pay their ceiling; strings pay a tenth of it.** Sort your candidates
  by which they are before you sort them by size.
- **Where hanabi is over-inked, the metric and the truth agree.** Every
  ink-reduction in this branch helped, and they composed. That is the opposite
  of the text case and it is why the mark column reached its floor.
- **The list's glyph column is finished.** 1.95% against a floor of 3.22–5.38.
  It has now been worked four times; it does not need a fifth.
- Left in the sidebar: the badges (+2.90, mostly an 8px digit), the icons
  (+3.89, blocked on the atlas), the fill's corners (0.37, blocked on #111) and
  the eighteen row titles (+2.57, blocked on the typeface). **Nothing else in
  this region is above two pixels.**

---

## The row titles (feat/vis-titles)

**The list is AT FLOOR. 13.56% → 11.82% structural against a floor of
8.41–11.83, and the change is one pixel of horizontal position on eighteen
strings.** It is not the rasterizer. Six rounds said it was, and the evidence
was strong — the strings match, the start x matches, the size matches, the ink
is 77–86% of the reference's, 2x scored worse, three font sweeps ended at the
shipping value. Every one of those is true. The one that was measured wrong is
*the start x matches*: it never did, by exactly one pixel, on every row.

| | main | feat/vis-titles | floor |
|---|---|---|---|
| **list** | 13.56% | **11.82%** | 8.41–11.83 **AT FLOOR** |
| list/titles | 17.21% | **14.94%** | 10.54–14.64 |
| list/counts | 3.35% | 3.11% | 1.60–2.63 |
| list/marks | 1.95% | 1.95% | AT FLOOR (untouched) |
| search | 3.20% | **2.99%** | 1.54–3.17 **AT FLOOR** |
| sidebar | 10.28% | **9.15%** | 6.25–8.93 (+1.35 → +0.21) |
| whole frame, structural | 4.28% | **4.01%** | |
| shared surfaces | 4.10% | **3.83%** | |

Search and the footer move without being touched: `compare.py` blurs by 0.8px
before scoring, so the first and last row titles bleed across the list's
boundaries. Points, not credit.

### Do the per-element pricing FIRST, then take the element apart

`scripts/ceiling.py` is the right first move and `feat/vis-sb3` is right that
it reorders the work. But it prices a RECTANGLE, and "the eighteen row titles"
is not one element — it is eighteen. Priced as one rectangle they read +2.57
over floor, which is the shape of a rasterizer residual: a small uniform
surplus with nothing to grab. Priced one row at a time, the same 2.57 is not
uniform at all — five rows sit AT their own floor and three carry +6 to +9.5.
A number that is flat across a region is a metric fact; a number that is
concentrated is a bug. You cannot tell which you have until you split it.

The split took twenty lines of Python: find each row's ink band, and for each
one print ref-vs-hanabi ink, the ink bbox, and that band's own now/ceiling/floor.
The first column of the output settled it before any of the rest was read:

```
 #   refink  hbink  ratio   rx0  hx0    rx1  hx1
 0      563    434   0.77    29   28    131  128
 1      731    613   0.84    28   27    153  151
 2      371    329   0.89    29   28     92   88
 3     1055    903   0.86    28   27    207  208
 ...  eighteen more rows, and hx0 == rx0 - 1 on every single one
```

**Nineteen rows, nineteen first-ink columns, every one exactly one pixel left.**
Not a distribution around zero — a constant. Rasterization phase does not do
that; a wrong constant does. (Note the reference's own 28/29 alternation is
reproduced faithfully one pixel over, which is what says the two engines agree
about side bearings and disagree only about the origin.)

### Prove a rigid error is rigid before you go looking for it in the code

Shifting hanabi's own pixels is a five-minute test and it tells you the size of
the prize before you spend an afternoon finding the constant. Crop the title
column out of the capture, translate it, paste it back, re-score — the same
trick as the paste test, one axis over. The `dx=0, dy=0` cell is the control:
it is the cost of one bicubic round trip, and here it was zero.

```
        dy=    -1.0    -0.5     0.0     0.5     1.0
  dx=-1.0    20.72   19.51   18.86   18.71   19.45
  dx= 0.0    20.05   18.42   17.21   17.06   18.33
  dx=+1.0    19.25   17.06   14.94   14.89   17.08     <- floor is 14.64
  dx=+2.0    19.79   17.83   16.37   16.07   17.78
```

The minimum is at exactly +1.0, it is 2.27 points deep, and the vertical axis
is flat — which independently re-confirms `feat/vis-list2`'s pitch measurement
and says the row's baseline is right. **This is the test the whole workstream
was missing**: if the residual is per-glyph placement, no rigid shift can
help, and that is a falsifiable claim nobody had falsified. Run it on any text
rectangle before concluding rasterizer.

Run the same sweep on the glyph column and it says the opposite — `dx=0` is
1.95% and `dx=+1` is 10.13%. The marks are right where they should be. So the
error is not the row's left inset; it is inside the row, between the glyph slot
and the title.

### The constant was right, the comment was right, and the padding was never applied — and gap #85 had already said so

```cpp
// Puffin puts the glyph's centre at x=15.5 and the title's first ink at x=28,
// so the slot is 13 wide from kSbInset and the title carries a 6px left pad.
static constexpr float kRowLeftInset = kSbInset;   // 9
static constexpr float kGlyphW       = 13.0f;
static constexpr float kRowTitlePad  = 6.0f;
```

9 + 13 + 6 = 28, and 28 is the right answer. The title draws at **27**.
`.with_padding(Padding{.left = pixels(kRowTitlePad)})` on a label does not move
that label's text — afterhours positions a label from the element's own drawn
rect plus its private 5px margin, and consults `Padding` only for the element's
CHILDREN. Built at 6, 7 and 20 the captures are byte-identical apart from the
ellipsis budget. So the real arithmetic has always been
`kRowLeftInset + kGlyphW + 5 = 27`, the six were never in it, and the comment
that said they were is why five rounds trusted the number.

**And this was filed. Gap #85, from the smart-view row, in this same file:**
*"its comment did the arithmetic out loud — kSbInset 9 + icon 16 + a 12px pad
on the label = 37 … `pixels(12)` and `pixels(40)` produce a byte-identical
frame."* Same defect, same file, 2,200 lines apart, same shape of comment. #85
closes with *"silence is what made this cost a day"*; filed, it cost six
rounds. **Reading the gap log for the mechanism you are about to blame is
cheaper than measuring, and nobody in this workstream did it** — including,
until an hour ago, me.

The one thing #85 gets wrong is the thing that made the second instance
expensive. Its escape list opens with *"`with_margin` is the same story one
level out: it spaces the element from its siblings, not the text from the
element."* Literally true, practically backwards — margin moves the ELEMENT,
and the element's rect is what the text is drawn from, so the text goes with
it. That is the fix here: a left `Margin` of 1 and a width reduced by 1, no
extra entity — and the dead `.with_padding` is REMOVED rather than left in as
decoration, because the day #85 is fixed upstream it would have moved every
title in the list six pixels right. `render_snippet`, forty lines above the
defect, already documents the margin (*"The indent is a margin, which moves the
element and its text together"*), so one function in this codebase knew and the
gap doc said not to. #85 shipped an empty `sb_spacer_x` div per row instead.
Correction filed as gap #109, with the audit.

The audit is the part to carry forward: **twenty labels in hanabi set
horizontal padding**, nine of them on elements whose own label is the thing
meant to move. None is in a region above its floor today, so none is touched —
but `tab_label`, `md_table_cell`, `msg_time` and `dc_tag` are all drawing at
their element's edge right now.

### What is left, measured per glyph, and it IS the rasterizer

With the origin fixed, the residual is now worth the name six rounds gave it,
and this is the direct measurement rather than the inference.

**The ink deficit is uniform.** Coverage-weighted ink, hanabi over reference,
per row: 0.895 0.886 0.904 0.896 0.870 0.886 0.875 0.892 0.858 0.890 0.889
0.887 0.893 0.885 0.882 0.888 0.895 0.877 0.875. Nineteen rows, mean **0.885**,
full range 0.858–0.904. No row is a different bug from its neighbours. (Any
figure showing a low outlier is a clipped scan window — row 0's band starts at
y309, above the list rectangle's own y313, and reads 0.77 if you crop to the
region.)

**Puffin's title is the REGULAR face, not semibold.**
`HomeSessionList.swift:1212` is `.font(PuffinTheme.Font.message)`, and
`PuffinTheme.Font.message = face(Size.message)` with `face`'s weight parameter
defaulting to `.regular`; `messageEmphasis` exists and is used in three places,
none of them a session row. So `## The typeface question, settled` should read
*"Puffin renders the Regular face through CoreText with macOS stem darkening"*
— the 11.5% ink deficit is Regular against Regular, and there is no weight to
switch to. It also kills the last version of the "does Puffin bold an unread
row" question: it does not bold anything in this list.

**The advances drift, in both directions, up to ±4px.** Slide hanabi's
per-column coverage profile against the reference's inside a 24px window and
take the best-correlating sub-pixel offset; step the window along the string
and the offset is a curve. Every row starts registered — dx between −0.5 and
+0.6 at the first window, which is the fix landing — and then goes its own way:

| row | title | dx at start → end |
|---|---|---|
| 17 | `row 133 banyan diff gate` | −0.2 → **+4.0** |
| 3 | `coordinating 3 shard workers` | −0.2 → +1.9 |
| 7 | `two shards died` | −0.5 → +1.5 |
| 8 | `needs a decision before it can go on` | −0.3 → −0.3 |
| 16 | `Navi PRs: oak + juno` | −0.3 → −0.1 |
| 15 | `import failed twice` | −0.4 → −2.0 |
| 14 | `style guide written` | −0.3 → −2.2 |
| 18 | `parent — nothing to report` | −0.2 → −2.5 |
| 5 | `SKU backfill — my name for it` | −0.8 → **−3.7** |

Mean end-drift over all eighteen is −0.4px, so there is no second global
constant hiding in it. And the per-row residual tracks |drift| almost
perfectly: the five rows still at their own floor are the five whose drift ends
under 0.5px (rows 1, 8, 10, 16, 4), and the three carrying the most are rows 5,
17 and 18. **Two text engines advancing differently along a string is the
finished answer, and this is what it looks like measured instead of inferred.**

**The single largest named contributor is the em-dash, and it is priced.** Four
titles carry one. Measured as an isolated horizontal bar in both frames:

| row | ref | hanabi | |
|---|---|---|---|
| 5 | x106–118, **13px** | x105–114, **10px** | −3 |
| 11 | x96–108, 13px | x97–106, 10px | −3 |
| 12 | x163–175, 13px | x164–173, 10px | −3 |
| 18 | x72–84, 13px | x73–82, 10px | −3 |

Three pixels, identically, on all four — Roboto's em-dash at 16.5px against the
reference face's. It is visible in the drift curves as a step: row 18 goes +0.3
→ −1.9 across exactly the columns the dash occupies. Synthetically widening all
four dashes to 13px and sliding each tail 3px right — the paste-quality upper
bound, better than any real fix could do — is worth **0.22 points** (14.94 →
14.72). Not spent. There is no per-glyph advance override to spend it with, and
hand-drawing four dashes to buy two tenths of a point is fitting.

### Declare it, and here is the guard

The list joins the tab bar and the marks column. Its remaining 11.82% against a
floor whose top is 11.83% is per-glyph advance drift with an ink deficit of
11.5%, both measured directly above, and neither is reachable from hanabi.
**There is no seventh round in this region.**

The count column is the one thing left inside the list rectangle: 3.11% against
a floor of 1.60–2.63, and a synthetic sweep says its best offset is +0.5px
(2.82%). It is right-aligned through the `kAhTextInset` trick, so a half pixel
is not expressible, and `feat/sidebar-counts` already swept every real
constant. Three tenths of a point, unreachable, left alone — and now for the
second time, which is the note that matters.

**The guard is a scripted test, and it is the first pixel finding in this
workstream that `assert_ui` CAN see** — because the fix is an element's x, not
a colour or a coverage. `tests/ui/row_title_starts_where_puffin_starts.e2e`
asserts `row_title x=23`, and a build with the margin removed reports 22 and
fails. Everything else here stays where the harness cannot reach it: the drift
curves, the ink ratios and the dash widths are all gap #86.

---


---

## The lesson, encoded (no branch — done on main)

Gap #85 — padding on a label-only element is silently ignored — has now cost
this project three times, and the third was the expensive one:

1. a smart-view row's label sat 6px left of the reference for the whole parity
   effort, under a comment doing the arithmetic out loud;
2. the composer's pill labels drew at their element's edge (#91);
3. **every session-row title was one pixel left, which was 100% of the session
   list's remaining headroom** — after SIX rounds concluded the difference was
   the text rasterizer (#109).

Writing it in the gaps file a fourth time would not stop the fourth. So
`scripts/check_label_padding.py` finds them, and `scripts/run_tests.sh` runs it.

**There are thirty today, and they are not thirty bugs.** Wherever one is
visible, somebody has already tuned the geometry around it, so "fixing" the
padding moves something that currently looks right. What they are is thirty
places where the next edit will silently do nothing. The set is frozen in
`scripts/label_padding_baseline.txt`; the checker fails on a **new** one, and
prints the list to anyone who touches an old one.

Two exemptions, because a checker that cries wolf gets waived wholesale:
zeroed padding (the documented workaround for gap #76, where an element with no
padding silently gets a fraction of the *screen*), and centred labels, where
horizontal padding is irrelevant by construction.

Verified by injecting a hit: `NEW src/ecs/toast_system.h:116 toast_probe_new
(up to 9px)`, exit 1. And `compare.py --selftest` now runs in the same step —
it has three deliberate-breakage cases and nothing was running them.

- **Class** — `ENCODED`. The first thing in this workstream that stops a defect
  rather than describing it.

---

## The sidebar footer (feat/vis-footer)

Footer **8.06% -> 7.99%** structural on both references, headroom over the
floor **+2.21 -> +2.14** on `01_home` and **+2.19 -> +2.12** on `02_thread`.
In frame points, 0.1757 -> 0.1741 on 01 and 0.0698 -> 0.0692 on 02.

That is a small number and it is the honest one. **The useful output of this
round is the anatomy**: the region that was "worst in the app by headroom" is,
correctly bounded and correctly attributed, two thirds somebody else's and a
declared feature, and this entry is mostly the arithmetic that shows it.

### The anatomy of the +2.19, priced against the floor

778 structural diff pixels over the region's 9,650 shared ones. Where they are:

| piece | px | share | over its own floor | what it is |
|---|---:|---:|---:|---|
| the session list's last row, and the rule under it (y911..921) | 303 | **39%** | +132 | **not the footer's.** See below |
| the session count + activity light | 285 | **37%** | +150 | hanabi's; Puffin draws nothing in that 160px |
| `plus` against `info.circle` | 76 | 10% | +14 | declared product divergence |
| `search` against `ant` | 77 | 10% | +16 | declared product divergence |
| `gear` against `gearshape` | 37 | 5% | **AT FLOOR** | the only matched pair |
| the version label | 0 | 0% | — | declared: v0.5.5 is not a number hanabi can become |

The region's floor is 5.85%, so its 8.06% is **213 pixels** of headroom. Read
the table against that and there is no defect in it: 132 of the 213 are the
list's text, roughly 150 are a feature Puffin does not have, and the two
mismatched icons are 30 between them. The one pair that IS comparable was the
only thing in the band worth touching, and it is now at its floor.

(The per-rectangle floors deliberately do not sum to the region's. Each is
computed on its own boundary, and a boundary picks up its neighbour's edge
under an 0.8px blur: `foot/count`'s floor of 3.70% is 135 pixels, which is
exactly one row of 135 columns — the footer's own hairline at y921, half a
pixel above the rectangle's top edge. A floor is a property of the rectangle
you drew, not only of what is inside it.)

### 39% of this region is the session list, and the region boundary is why

`compare.py` cuts `footer` at `H * 0.96` = y911. The footer's own rule is at
**y921**. So ten rows of the list's last visible row are scored as footer, and
they are 303 of its 778 pixels — text, in the region with the app's smallest
denominator, which is the combination that makes a number look alarming.

This is not new and it is not a metric bug: `feat/vis-tabs3` already noticed
"203 more sit in rows y917..921 ... it belongs to whoever owns the list". What
is new is that it is now 39% rather than 32%, because everything else in the
band got smaller, and that **nobody has ever subtracted it before quoting the
footer's headroom.** The footer band alone — y922..948, the 28px Puffin
reserves — is 468 pixels and 0.1057 frame points.

The region boundary was left alone deliberately. Moving it to y921 would move
the `list` region's boundary in the same edit, under an agent who is live in
that code, and every list figure in this workstream with it. `ceiling.py` now
carries `foot/listbleed` as a named rectangle in its default partition
instead, so the split is one command away and nobody has to rediscover it.

### `ceiling.py` was pricing declared surface, and it ranked an unspendable rectangle top

The tool this round was told to reach for first said the version label was
**+5.00 reachable** — the largest single number in the footer, and the first
place its own ranking sends you.

Every pixel of that rectangle is masked out of the score. `compare.py`
declares `(8, 926, 60, 944)` as "sidebar footer version string", because
v0.5.5 is not a number hanabi can become, and `ceiling.py` did not know: it
called `diff_mask` and `floor_by_region` directly and never looked at
`KNOWN_DIVERGENCES`. A tool whose entire purpose is to rank work by
*reachable* size was ranking an unreachable rectangle first.

Fixed, and the fix is in all three columns — the score, the ceiling, and the
**floor**, which is the one that is easy to forget. A floor measured over
surface the score does not look at is a floor for nobody, and it is subtracted
from a masked score to produce "headroom", so the two halves of that
subtraction were being measured over different surfaces. `--no-exclusions`
reproduces the old output exactly, and a rectangle that is entirely declared
now prints `ALL DECLARED -- unspendable` rather than three plausible numbers.

The same inconsistency exists in `compare.py`'s own `--floor` column and is
NOT fixed here: its per-region score is masked and its per-region floor is
not. It matters most where the declared share is largest — `main` is **85.3%
declared**, so its printed floor of 0.69–1.05 is overwhelmingly the
rasterization cost of a transcript viewport nobody is being asked to move.
Left for whoever owns that region to decide, because changing it moves every
region's headroom in one edit and this branch is the footer's.

### The activity light was six wide and five tall

The one real defect in the band, and neither the metric (0.0018 frame points)
nor a 1x screenshot reports it.

hanabi's network light is a 6x6 box with `roundness 1.0` — a disc. It rendered
rows 933..937: **4/6/6/6/4**. Six wide, five tall, and lopsided. The cause is
one half pixel: the band's content runs y922..948, so centring a 6px dot is
`922 + (27 - 6) / 2 = 932.5`, and afterhours never rounds a position. Grid
snapping is off in hanabi and only ever snapped SIZES anyway, so the fraction
reaches a rasterizer with MSAA hardcoded off (#92) and one row falls off an
edge that lands exactly on a pixel boundary. At an integer y the same box
draws 4/6/6/6/6/4 and is round. Gap **#110**.

Two things about it generalise:

- **It is one axis at a time, so the shape changes proportion rather than
  moving.** A half-pixel translation is invisible; a circle that is a sixth
  short on one axis is a different drawing. Which axis depends on which of the
  caller's two expressions happened to come out whole.
- **The fractional part here was data-dependent.** The light's x is
  `text_px("20 sessions")` subtracted from a right edge, so the count of
  sessions in the catalog decides whether it loses a column instead. That is a
  bug that reproduces on one machine's fixture and not another's, and it is
  why the fix snaps both axes and the test sweeps forty x values.

**It costs 8 diff pixels** (285 -> 293 over the count's rectangle) and it is
right anyway. The reference has no light there at all, so every pixel hanabi
draws is a difference by construction and the metric will always prefer the
smaller, wronger shape — this is trap #1 with no other half to find, because
there is nothing to correct it *towards*.

### The gear is the only button that can be measured, and it was a size too small

`SidebarColumn.sidebarFooter`'s three buttons are `info.circle`, `ant` and
`gearshape`; hanabi's are `plus`, `search` and `gear`. Only the third pair
means the same thing, and REFERENCE.md already records why the other two must
not be closed. What that leaves is one comparable icon, and it was carrying
**37 of the buttons' 190 pixels against its neighbours' 76 and 77** — the
matched one costing half what the mismatched ones do, which is the ratio
`feat/vis-tabs3` found and the sanity check on the whole idea.

Measured off `ref/01_home.png`, the three reference glyphs are **11x11, 9x12
and 11x12** and read as one size, because SF Symbols normalises optically.
Lucide normalises by its 24-unit box, so hanabi's one nominal 13px gave `plus`
and `search` at 12x12 and `gear` at **10x12**: the gear carries more internal
padding than the other two and came out visibly smaller in the same cluster.
Swept:

| nominal | plus | search | **gear** |
|---|---:|---:|---:|
| 11 | 80 | 79 | 61 |
| 12 | 85 | 83 | 61 |
| **13 (shipped)** | 76 | 77 | 37 |
| **14** | 75 | 82 | **22** |
| 15 | 77 | 101 | 37 |
| 16 | 87 | 118 | 56 |

**gear 37 -> 22 at 14px**, where its box becomes 12x12 — level with its two
neighbours and against the reference's 11x12. `plus` and `search` stay at 13,
deliberately: sizing one icon set's glyph to a *different* icon set's box is
measuring the fixture, which is the same claim `feat/vis-tabs3` declined to
make about the tab strip's `+`. Per-icon `px` in the button table, with the
optical-sizing reason written above it.

### A blit and its label need two inks HERE too, and the earlier sweep could not see it

`feat/vis-tabs3` swept this band's colour axis and got a NEGATIVE result:
`text_faint` (100,100,112) beats `text_secondary` (142,142,154) 4.44% to 4.58%,
and the best colour available is worth 0.11 points. That result is correct and
it is **one constant for the whole band**, which is the thing that hides the
answer.

Split per element and re-swept analytically (recovering per-pixel coverage from
one render, which is exact for a blend):

| ink | plus | search | gear | all three |
|---|---:|---:|---:|---:|
| **text_faint (100,100,112)** | **76** | **77** | **37** | **190** |
| (120,120,139) | 78 | 76 | 49 | 203 |
| Chrome.mutedText (140,140,166) | 79 | 84 | 68 | 231 |
| (160,160,190) | 89 | 94 | 81 | 264 |

Monotonic, and — the part worth having — **it is monotonic on the GEAR too**,
the one icon whose shape is right. So this is not the two mismatched icons
hiding a real colour gap: a sprite blit genuinely overshoots here, at every
ink above the shipped one, on a pair that matches. `text_faint` stays, and now
it is measured on a matched pair rather than on an average over three.

The text is the other way round, and by the same rule. Measured on the
reference by (pixel − background), the only coverage-independent read: its
version label's mean ink is **54.9** above the (23,23,35) it sits on, and
hanabi's at `text_faint` was **37.2** — 68%, and visibly the dimmer of the two
side by side. 1.476x of text_faint's own (77,77,77) delta is (114,114,125);
`text_secondary` lands (119,119,119), inside six levels on every channel, and
it follows the light theme where an eleventh hardcoded token would not.
hanabi's version label now measures **54.3** against the reference's 54.9.

So the footer joins the smart-view row: **two inks where Puffin's source uses
one**, for the same reason and in the same direction — the blit takes the
lower constant, the text takes the higher one. REFERENCE.md's rule ("move a
BLIT to the source's token, and leave TEXT wherever it measures") needed one
amendment for this band: the blit's right token here is *below* the source's,
not at it, because these glyphs are 10pt where the smart-view row's are 13.

### The version label was five pixels right of the reference's, in a rectangle nothing can score

Puffin's footer is `.padding(.horizontal, 10)`, so its version ink begins at
x=10 and the reference confirms it: "v0.5.5" runs x10..36. hanabi's box was AT
10 and its ink at **15**, because afterhours insets a label's text by a
hardcoded 5px on every alignment with no way off (#84) — the gap this file has
cited four times, arriving once more in the one place where no harness could
report it.

`compare.py` declares that rectangle. The move cost and bought exactly **0.000
structural points**, measured both ways. The only reading available is
`ceiling.py --no-exclusions`, which is now a supported way to ask:

| build | foot/version, unmasked |
|---|---|
| main | 12.78% |
| + the 5px | 11.30% |
| + the ink | **10.86%** |

**A declared rectangle is not a rectangle nobody should work in.** It is a
rectangle whose *content* cannot converge — v0.5.5 against v0.1.0 — and
everything else about it still can: where it starts, how bright it is, what
size it sets. Both of this branch's visible improvements are inside one, and a
reader who trusted the region score would have concluded there was nothing
there. That is the mirror of the status bar's lesson: a rectangle over chrome
does not cover the displacement the chrome causes, and a rectangle over a
string does not cover where the string is drawn.

### The count is the price of information Puffin does not show, and here is the price

285 pixels, **0.0644 frame points on `01_home` and 0.0256 on `02_thread`** —
the two numbers differ by 2.5x because 01 declares its transcript viewport and
02 does not, so the same pixels are a different fraction of a different
denominator. Quote the reference with the number.

`feat/vis-statusmove` priced this at 0.025 when it moved the count here and
bought 0.636 of composer register for it. That was 02's number and it still
holds exactly. Nothing since has made it cheaper, and nothing in this round
tried to: every lever that reduces it is a lever that makes hanabi's own
status quieter or smaller than its own typography, which is fitting a metric
against an element the metric has nothing to compare with. **The reference has
no ink in that 160px, so the count's score is a pure function of how much ink
hanabi puts there and the optimum is zero.**

Declared in REFERENCE.md rather than in `compare.py`, and the distinction is
the point: the bar for a `compare.py` entry is "no change to hanabi's design
can close it", and deleting the count would close it. It is kept because it is
worth keeping, not because it is unspendable, and a score that stops charging
for it would stop being able to tell anyone what it costs.

### Two tests, one of which could not be a test

**`tests/ui/sidebar_footer_starts_where_puffins_does.e2e`** pins the version
label's box at x=5 (the ink at 10, plus afterhours' 5) and the three buttons on
the reference's own 24px pitch. Verified to fail without the fix — reverting
`label_box_x` to the identity gives

```
[E2E ERROR] assert_ui (line 42): assert_ui 'sb_version': x=5 but got 10
```

It takes its window size from `# env:`, not `# settings:`, for the reason the
statusmove entry records: the runner writes the settings blob and the uitest
binary sizes its surface from `HANABI_WIN_W/H`, so a bottom-anchored assertion
declared only in the blob measures against 760 and fails looking exactly like a
layout bug you just wrote.

**`tests/unit/test_footer_geometry.cpp`** holds the property `assert_ui`
cannot: that the light's origin is on a whole pixel. The harness ROUNDS what it
reports, so it reads 933 for an unsnapped 932.5 and 932 for an unsnapped 932.4
— it can pin where the light is and can never pin that it is round. Verified
to fail: reverting the two `std::floor`s and `label_box_x` gives 57 failures,
`FAIL: fs::dot_y(922.0f, 27.0f) == 932.0f`, 19 x `FAIL: whole(y)`, 36 x
`FAIL: whole(fs::dot_x(text_left))`, and both `label_box_x` checks. That file
exists only because of gap #86, and it is the second one in this repo to
(`test_tab_colors.cpp` was the first).

**One existing assertion had to move**: `composer_reaches_the_window_floor.e2e`
read `assert_ui sb_activity_dot y=933` and now reads 932. It caught the change
before I updated it, which is the only evidence worth having that it was a real
assertion and not a decoration.

### For the next person

- **Subtract the list before you quote this region.** 39% of `footer` is the
  session list's last row, because the region cut is `H*0.96` and the footer's
  rule is ten rows lower. `ceiling.py` prints the split.
- **The footer band itself is finished, short of a product decision.** 468
  pixels: 293 a feature Puffin lacks, 153 two icons that are deliberately not
  Puffin's, 22 the one matched glyph at its floor. There is no third thing.
- **Work inside a declared rectangle when the reference tells you to.** The
  content is what cannot converge; the position, the size and the ink still
  can, and `ceiling.py --no-exclusions` will price them.
- **A shape on a half pixel is a different shape, not a moved one** (#110).
  Snap both axes of anything under about 10px, and remember that an axis
  derived from `text_px` has a fractional part that changes with the data.

---


---

## The floor had the bug it was invented to expose (no branch — done on main)

`--floor`'s two columns sat side by side meaning different things. The score
masks the declared divergences; the floor did not. So a region's headroom —
score minus floor, the number everybody has been steering by all day — was a
masked figure minus an unmasked one.

The footer agent found it and left it, correctly, because fixing it moves every
region at once. Fixed now, and it moves two verdicts:

| | before | after |
|---|---|---|
| **tabbar (01)** | AT FLOOR | **+0.72** |
| **tabbar (02)** | AT FLOOR | **+0.30** |
| main (01), 85.3% declared | +2.26 | +1.27 |
| views | +0.34 | +0.91 |
| footer | +1.78 | +1.98 |

**The tab bar was declared finished on a floor measured mostly over the window
frame** — the one thing in that region hanabi cannot draw and the score does
not charge for. Its real floor is 1.10–1.66 against a score of 1.96. Two
rounds concluded it was at floor.

This is the same shape as gap #109 one level up: a number that reads as
authoritative, arrived at by measuring something adjacent to the thing it
names. The rule that falls out — **a floor, a ceiling and a score have to be
taken over the same surface, or their difference is not a quantity** — now has
`ceiling.py` honouring declarations in all three columns and `compare.py`
doing the same.

---

## The tab strip, round four (feat/vis-tabs4)

Two rounds declared this region AT FLOOR. The floor was wrong — it was computed
over the macOS window frame, which the score already declares — and the
corrected numbers are +0.72 on `01_home` and +0.30 on `02_thread`. This round
spends what is spendable, and the useful half of it is the four things that
turn out not to be.

**The result, both references, structural, tab strip region:**

| | before | after | floor |
|---|---|---|---|
| `ref/01_home.png` (two pinned tabs) | 2.81% (+0.72) | **2.81% (+0.72)** | 1.41–2.08 |
| `ref/02_thread.png` (one unpinned tab) | 1.96% (+0.30) | **1.91% (+0.25)** | 1.10–1.66 |

01 does not move because the thing that was wrong is invisible in it, and that
is the finding of the round.

### What the +0.72 and the +0.30 are made of

Partitioned by element, in diff pixels against each element's own floor, over
the shared surface. This is `ceiling.py` with a rectangle per element rather
than per region, which is the only way the answer comes out:

**`ref/01_home.png`, 1755 diff px, floor 1302, headroom 453**

| element | diff | floor | headroom |
|---|---:|---:|---:|
| tab 1's title | 367 | 172 | **195** |
| tab 2's title | 1281 | 956 | **325** |
| tab 1's pin | 27 | 30 | −3 |
| tab 2's pin | 20 | 15 | +5 |
| the `+` | 55 | 112 | −57 |
| both tabs' frames, corners, hairline, the empty strip | ~5 | ~5 | 0 |

**`ref/02_thread.png`, 1228 diff px, floor 1038, headroom 190**

| element | diff | floor | headroom |
|---|---:|---:|---:|
| the title | 1123 | 887 | **236** |
| the close `×` | 47 | 22 | **25** |
| the `+` | 55 | 112 | −57 |
| frame, corners, hairline, empty strip | ~7 | ~7 | 0 |

So: **the strip is its titles, plus one missing mark.** Every drawn shape in it
is at or under its own floor, including the `+`, which the previous round left
open as blocked on the palette. The pins are within five pixels of theirs.

### The reference draws a close × that hanabi drew nowhere — and the rule against it was read off the one frame that cannot show it

The one real fix, and it is a whole missing element rather than a nudge.

- **What the source says.** `TabChip.body` ends its `HStack` with
  `if !tab.isKeptOpen { closeButton }`. There is no hover condition anywhere in
  `Sources/Views/TabStrip.swift`: an unpinned chip carries its × at rest, and a
  pinned one never carries it at all, because `closeRequest` refuses a pinned
  tab and springs its pin instead.
- **What hanabi did.** `bool showClose = hovered;` — the × on hover, on every
  tab, pinned or not. Above it, a comment: *"A row of tabs each carrying a
  permanent × reads as a toolbar of buttons; the reference shows a clean title
  on every tab, active one included, and the close affordance only under the
  cursor."*
- **Why that comment was wrong, and why it was reasonable.** It was read off
  `ref/01_home.png`, whose two tabs are **both pinned** — the single state in
  which Puffin also draws no ×. The frame it cites is the frame that cannot
  distinguish the two rules. `ref/02_thread.png` is the unpinned state and it
  draws one: 8px of ink at x483..490, y46..53, against nothing at all in
  hanabi's capture of the same frame.
- **What it cost.** Two rounds of "the strip is at floor" while a whole
  affordance was missing from every unpinned tab in the app, invisible to the
  reference everyone was working from.

This is the lesson the brief predicted and it generalises past the ×:
**01 and 02 differ on pinning on purpose, so any rule about a tab that varies
with pinning has exactly one frame that can test it, and it is never both.**
Before concluding a tab-strip rule from a capture, ask which of the two states
the capture is in and whether the rule is a function of it.

**The mark, built and measured.** Puffin's geometry is two constants —
`.padding(.horizontal, 10)` and `closeButton`'s `.frame(width: 14, height: 14)`
— restated by `TabTooltip.horizontalPadding` and `.markWidth`, which is the
source agreeing with itself. `chip right 504 − 10 − 14 = 480`, centre 487,
against the reference's measured 486.5. hanabi's dormant hover × was a 16px box
5px in, centring at 491.

| | rect (476,40)-(498,58), floor 2.02–5.56% | diff px |
|---|---|---:|
| no × at all | 11.87% | 47 |
| × at 11px, the size used elsewhere in the app | 7.58% | 30 |
| × at 8px with a +1px y bias | **3.79% — AT FLOOR** | **15** |

The two builds between the second and third rows are gap #114: `draw_px` sizes
the sprite's BOX and what has to match is its INK, and nothing relates them.
At 11 the mark came out x482..491 y43..53 — three wide, three tall and a pixel
and a half high. At 8 it is the reference's extent row for row.

**A pinned tab keeps hanabi's hover ×, deliberately**, and that is now a
declared divergence in REFERENCE.md. Puffin can afford to draw none because its
close paths all funnel through a refusal and its context menu carries a Close
Tab item; hanabi's menu has no close item, so matching Puffin exactly would
leave Cmd+W and middle-click as the only ways out of a pinned tab. It costs
nothing at the metric: neither reference captures a hover.

### Four things that are NOT worth what the last round thought, each with the number

The round-three entry above leaves four leads open. All four are now measured
and all four are worth approximately nothing. They are written out at length
because each one reads like the obvious next move.

**1. The active tab's title is pure white and it should be lightText — worth
0.2 points, and the metric is split on the sign.**

Recovered by the (pixel − background) ratio over the whole title, which is
coverage-independent: the reference gives 1.00:0.936:0.825 over the (46,58,88)
fill, and `Chrome.text` = lightText (235,235,245) predicts 1.00:0.937:0.831.
hanabi's own recovers to 1.00:0.943:0.799, which is (255,255,255) exactly. So
the source and the pixels agree that Puffin's active title is **not white**,
and hanabi's is.

Swept analytically — coverage is recoverable from one render with a known
colour, so any other ink can be re-synthesised without rebuilding:

| ink | 01 tab 2 | 02 |
|---|---:|---:|
| white (255,255,255) — shipped | 1281 | **1123** |
| lightText (235,235,245) — Puffin's | **1273** | 1129 |
| text_primary (224,224,230) | 1283 | 1132 |
| best of seven candidates | 1272 | 1123 |

Eight pixels better on 01, six worse on 02, out of ~1280 and ~1123. **Left at
white**, by REFERENCE.md's own rule — move a BLIT to the source's token and
leave TEXT wherever it measures — with the recovered constant written above the
code so nobody re-derives it.

**2. "hanabi's greys are neutral where Puffin's are violet" is worth ZERO in
this strip, and the round-three measurement that says otherwise was measuring
brightness.**

Round three called this *"the single largest thing standing between the small
drawn marks and their floor"* and priced the `+` at 75 diff px against 56
"recoloured to the reference's ink". Re-swept per mark, analytically, over the
whole plausible range:

| ink | the `+` | pin (inactive) | pin (active) |
|---|---:|---:|---:|
| `text_secondary` (142,142,154) — shipped, blue:red **1.00** | 55 | 27 | 20 |
| `Chrome.mutedText` (140,140,166) — Puffin's, blue:red **1.19** | **55** | **27** | **20** |
| (148,148,172) — round three's figure | 49 | — | — |
| `text_faint` (100,100,112) | 90 | — | — |

**Puffin's own token scores identically to hanabi's on all three marks.** The
six pixels round three found came from (148,148,172) being eight units
*brighter*, not eighteen units *bluer* — the sweep separates the two axes and
only brightness moves. Nineteen units of blue at 12/255 tolerance sounds
decisive and is not, because a mark this small is mostly partial coverage and
partial coverage scales the deficit down with it.

That does not make the palette observation false — the reference's muted ink
really is 1.10 blue:red and hanabi's really is 1.00 — it makes it **not worth a
palette-wide change on this evidence.** The tab strip was the case put forward
for one; priced locally it is zero, and the `+` is 57 pixels UNDER its floor
already.

**3. The chip's title inset is 12 where Puffin's is 10, and moving it to
Puffin's makes both references worse.**

Built and shot, unpinned 12 → 10 and pinned 26 → 28:

| | before | after |
|---|---|---|
| tabbar on 01 | 2.81% (+0.72) | 2.82% (+0.74) |
| tabbar on 02 | 1.91% (+0.25) | 1.97% (+0.31) |
| 02's title first ink | 297 (ref 294) | 295 (ref 294) |

**Reverted.** Two reasons, and the second is the one worth keeping:

- On 02, where both apps draw the *same string*, the rigid-shift sweep
  REFERENCE.md prescribes is flat and its minimum is at dx=0: 1138 / 1159 /
  1133 / **1123** / 1123 / 1137 / 1129 for dx −3..+3. That is the signature of
  a rasterizer residual, not a placement bug, and the 2px move is inside its
  noise in the wrong direction.
- **On 01 the sweep cannot be run at all, and reading first-ink instead is a
  trap I fell into.** Off row y=48 the reference's pinned title starts at 313
  and hanabi's at 311, which says "move hanabi 2px right". Off the whole ink
  band it starts at **310**, because 313 is the stem of `T` in "TODO" and 310
  is the left end of its crossbar, while hanabi's 311 is the ascender of `k` in
  "kicker-tick". Two different strings, two different first glyphs, and
  first-ink is a property of the glyph. The 2px move that reading recommended
  is exactly the 2px that made 01 worse.

**First-ink is not a measurable quantity when the strings differ.** The list
round could use it on nineteen rows because both apps drew the same nineteen
titles.

**4. Shipping a bold face for the active tab's title is worth 8 pixels — and
this is the place where Puffin genuinely bolds, so the bold-face round's
negative result did not have to transfer, and does.**

`Font.row(_:selected:)` is `face(size, selected ? .semibold : .regular)`, so
Puffin's active tab title is semibold. It shows: over 02's shared string
hanabi's title inks **256 units against the reference's 482 — 53%** — where the
inactive title on 01 inks slightly MORE than the reference's. That asymmetry is
the weight, isolated, and it is a much bigger deficit than the 11.5% the list
round measured with both sides in Regular.

Emulated by widening every stem, which is what a heavier face does:

| stem | ink | diff px |
|---|---:|---:|
| +0.0 (shipped) | 256 | 1123 |
| +0.3 | 276 | 1119 |
| +0.7 | 317 | 1145 |
| +1.4 (ink 419, nearest the reference's 482) | 419 | **1115** |
| +2.0 | 520 | 1149 |

A 34-pixel spread over a range that more than doubles the ink. Matching the
reference's ink almost exactly buys **8 diff pixels of 1123**. The reason is
the one the bold-face round gave — the diff counts non-overlap on both sides,
so ink added to a string whose advances are drifting creates as much
disagreement as it removes — and gap #77's remaining half is now priced in the
one region where the source actually asks for a bold.

### So what IS 02's remaining 236 pixels of title?

Not colour (0.2 points), not weight (8 px), not position (flat sweep). It is
the typeface's advances, and on 02 they can be read directly because both apps
draw `row 133 banyan diff gate`. Word runs, thresholded at 15% coverage:

| word | reference | hanabi | offset |
|---|---|---|---:|
| `row` | 294–315 | 297–314 | **+3** |
| `133` | 319–338 | 319–335 | 0 |
| `banyan` | 342–383 | 339–373 | −3 |
| `diff gate` | 387–435 | 377–417 | **−10** |

hanabi's string is 120px where the reference's is 141 — **85%** — and the
deficit accumulates monotonically from +3 at the head to −10 at the tail. No
single shift touches it; the head and the tail want opposite ones. This is
REFERENCE.md's already-recorded "45px narrower, and that is the typeface", one
region over and measured per word.

**01's 520 pixels of title cannot be measured at all**, because its two strings
differ, and that is a different statement from "cannot be closed". See below.

### Porting `t9` and `t2`'s titles is the SAME decision as porting r9's transcript, and here is why it matters more than the 2.6 points

Round three declined to rename hanabi's mock sessions to Puffin's fixture
strings, on the grounds that it would be measuring the fixture. Since then
`feat/vis-fixture` ported `mock-outcome-2` into r9 — and it ported the **title**
with it: `src/api/mock_client.h:1025` is
`pf("r9", "row 133 banyan diff gate", ...)`, which is the reference's own tab
label, verbatim. That is why `ref/02_thread.png`'s tab title is the only title
in this strip anybody can say anything about.

So the precedent exists, it was taken deliberately, and the argument for
extending it is not the 2.6 points. It is this: **sharing the string is what
makes the title measurable.** With it, 02 answers "is the position right"
(yes, flat sweep), "is the weight the gap" (yes, 53% ink), "is a bold worth
shipping" (no, 8 px) and "what is left" (15% of advance, accumulating). Without
it, 01 answers none of them — its rigid-shift sweep is ±40px of noise that
prefers −3 on one tab and +4 on the other, and its first-ink reading actively
misleads, as it did above.

The honest statement about 01, therefore, is **not** that its +0.72 is
unclosed. It is that its two titles carry 520 pixels of headroom between
them — more than the region's whole net 453, because the `+` and the pins sit
under their own floors and give some back — and that those 520 are two strings
saying different words, on which no measurement means anything until they say
the same words. I would port them; it is Gabe's call, and it is
the same call that was already made once.

### Cited, not re-filed

- **#104** (a scripted test cannot assert an element is ABSENT) is why the
  pinned half of the close-mark rule has no test. `tests/ui/
  tab_close_shows_on_an_unpinned_tab.e2e` asserts the × is there on an unpinned
  tab, unhovered, at 480x14x14; nothing can assert it is *not* there on a
  pinned one, because `assert_ui` retries a missing element rather than failing
  and there is no `assert_ui_absent`.
- **#86 / #61** (assert_ui reads x/y/w/h/hidden/text and never a pixel) is why
  the 8px glyph size, the +1px bias and every ink in this entry are guarded by
  `tests/unit/test_tab_colors.cpp` — arithmetic against constants measured off
  the reference — rather than by the scripted suite.
- **#92** (no antialiasing on primitives) is why the corner radius stays at 4.
  Puffin's chip is `UnevenRoundedRectangle(topLeadingRadius: 6,
  topTrailingRadius: 6)` and the reference's arc confirms it — its left edge
  reaches full coverage only at y=39, seven rows below the chip's top, where
  hanabi's is there by y=37. Priced over the corners' own rectangles it is
  **2 pixels on 02 and 4 on 01**, every one of the four at or within a pixel of
  its floor. The 0.8px structural blur forgives a two-pixel arc; correcting it
  would mean hand-drawing the corner, and #106's escape does not reach it —
  the coverage of a rounded corner is not separable the way an axis-aligned
  rectangle's is.

### New gaps

- **#113** — every scripted failure but one names the element it was about; the
  timeout, which is the one that means "it is not there", does not. Four
  identical `Command 'assert_ui' timed out after 30 frames` lines for four
  assertions about one element, with the name in scope one line above.
- **#114** — a sprite's rendered INK extent is not derivable from its atlas rect
  and its `draw_px`, so every icon is sized by build-measure-repeat. Two builds
  for one 8px mark.
