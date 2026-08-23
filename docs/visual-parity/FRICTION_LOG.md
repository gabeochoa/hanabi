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
