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
