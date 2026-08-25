# The sidebar, under a catalog the size of a real one

**Why this file exists.** A leak was reported ("it just gets slower and slower
every second until it freezes"), found, and fixed — Metal returns autoreleased
objects and a render loop is not a Cocoa run loop, so every frame leaked six of
them. RSS growth went from +2816 KB per 1000 frames to +11. That was the
*slower every second* half. This file is the other half: the app was also
slow at rest, in a way that got worse the more sessions you had, and nothing in
the project could see it because the mock catalog had twenty rows in it.

Twenty rows is the whole story of why none of this was found earlier. At twenty
rows an O(n) walk is free, an O(n log n) sort is free, and a cache that never
evicts looks like a design decision. `HANABI_STRESS_SESSIONS=<n>` was added to
make the catalog a realistic size; everything below is what that exposed.

`docs/visual-parity/FRICTION_LOG.md` is the model for this file: what was
wanted, what happened, what it cost, with the numbers inline. A number is worth
more than an adjective.

---

## The headline

`scripts/perf_ab.sh`, interleaved run for run, min-of-bucket over 800 frames,
median of 5. All four stress scenarios, ms/frame.

**idle** — no input, nothing animating, nothing streaming

| sessions | 0 | 100 | 500 | 1000 | 2000 |
|---|---|---|---|---|---|
| before | 1.110 | 1.981 | 2.483 | 3.210 | 4.562 |
| after | 0.992 | 1.324 | 1.311 | 1.317 | **1.358** |
| | 1.12x | 1.50x | 1.89x | 2.44x | **3.36x** |

**scroll** · **threads** · **tabs**

| sessions | 0 | 100 | 500 | 1000 | 2000 |
|---|---|---|---|---|---|
| scroll before | 1.055 | 1.968 | 2.481 | 3.888 | 4.671 |
| scroll after | 0.990 | 1.304 | 1.299 | 1.418 | **1.340** |
| | 1.07x | 1.51x | 1.91x | 2.74x | **3.49x** |
| threads before | 1.283 | 2.902 | 3.386 | 4.249 | 5.548 |
| threads after | 1.122 | 2.207 | 2.067 | 2.222 | **2.241** |
| | 1.14x | 1.31x | 1.64x | 1.91x | **2.48x** |
| tabs before | 1.077 | 3.079 | 3.806 | 4.886 | 6.371 |
| tabs after | 0.999 | 2.433 | 2.420 | 2.506 | **2.309** |
| | 1.08x | 1.27x | 1.57x | 1.95x | **2.76x** |

Entity count, idle (exact, and the same under every scenario):

| sessions | 0 | 100 | 500 | 1000 | 2000 |
|---|---|---|---|---|---|
| before | 300 | 532 | 1064 | 1733 | 3063 |
| after | 300 | 528 | 532 | 533 | 531 |

**Up to 3.5x at 2000 sessions, and every "after" row is flat.** Not flatter —
flat: idle is 1.324 ms at 100 sessions and 1.358 at 2000, and 1.704 at
*20,000*. Every "before" row is a straight line in the catalog size. The entity
count stops tracking the catalog entirely.

Nothing regressed at the small end. The first pass at the threads scenario read
0.92x at zero extra sessions, which is the one direction that would have
mattered; at 11 reps instead of 5 it is 1.14x, and the 0.92 was the box.

A fifth scenario — `search` — was added during this work because none of the
four above types anything, and it is where the worst number on the branch was:
**213 ms/frame at 20,000 sessions, now 4.5.** Section 4.

The brief for this work was "make the sidebar cost what it draws". The sidebar
turned out to be the wrong suspect, which is the most useful thing in this
document, so it goes first.

---

## 1. The sidebar was already virtualized. The Home pane was not.

- **What I wanted** — to find the place that walks 2000 sessions to draw 20
  rows. The row rendering is capped (`fillCap`), so something upstream of the
  cap had to be materializing the catalog: the entity count said roughly one
  entity per session, and the sidebar is the thing with the list in it.

- **What happened** — the sidebar was innocent. Its rows have been capped at 38
  the whole time. What the entity count was measuring was
  `MainPaneSystem::render_home`, one pane over, where **Recent was capped at 20
  cards and the three attention sections above it were not**. The reasoning
  that left them uncapped is right there in the code — "on a calm backend the
  attention buckets are all empty" — and it is true of a calm backend and says
  nothing about a busy one. At 2000 synthetic sessions the state cycle puts a
  third of the catalog into Attention or Running, so Home builds 696 cards at
  four entities each.

  Finding this took adding an entity **census** to the soak probe
  (`HANABI_SOAK_CENSUS=1`), which groups the live entity set by the debug name
  its `ComponentConfig` was built with. The bare total — 300 at twenty rows,
  3063 at 2020 — is a fact with no address, and it had the sidebar carrying the
  blame for a month:

  ```
  [soak] entity census (top 20 by debug name):     # 2000 sessions, before
  [soak]      696  dc_sub
  [soak]      696  dc_name
  [soak]      696  dc_top
  [soak]      696  digest_card
  [soak]       38  chat_row        <- the sidebar, capped, as designed
  ```

- **Cost** — one shared cap constant, four call sites. 2.06x at 2000 sessions
  on its own (4.594 → 2.227 ms), and the entity count went flat.
  Provably a no-op on the twenty-session fixture the test suite renders:
  twenty sessions cannot put twenty-one rows in one section, and the census
  reports 20 cards before and after.

- **The part worth keeping** — I spent the first hour of this reading
  `sidebar_system.h` because the brief pointed there and the sidebar is the
  thing with the list in it. The entity census took twenty minutes to write and
  answered it in one run. **A total is not a measurement; a breakdown is.**

## 2. And it was a screen that had been drawn twice

- **What I wanted** — to understand why capping a pane that is not on screen
  moved the steady-state frame time at all. With a chat tab open, Home is not
  the view.

- **What happened** — it is not on screen and it is not being rebuilt.
  Instrumented, `render_home` runs **twice** in the first thousand frames and
  never again. The 2784 entities those two frames built are still in the
  collection at frame 800, and every UI system iterates the entire collection
  every frame.

  So the 3.15 ms this change removed was not the cost of *drawing* Home. It was
  the cost of *having drawn* Home, charged every frame for the life of the
  process. afterhours' `mk()` retains entities by UUID and nothing ever retires
  one — filed as **afterhours_gaps.md #115**, with the measurement.

- **Cost** — ~1.1 µs per entity per frame, just to exist
  (3.15 ms / 2784 entities). Which means the app-side rule is: **the high-water
  mark of what you have ever built is your permanent frame cost.** Every list
  needs a cap whether or not it scrolls.

## 3. Ellipsizing a title was quadratic in the length of the title

- **What I wanted** — a row title cut to its column with an ellipsis. Once per
  visible row per frame; 38 rows.

- **What happened** — `fit_to_width` walked the cut point backwards one code
  point at a time, taking a `substr` and measuring it against the font at each
  step. Measuring a prefix is itself linear in the prefix (fontstash walks
  every glyph; `stbtt_GetGlyphKernAdvance` binary-searches the kern table per
  pair), so one title cost O(len²) glyph work plus a malloc and a free per
  probe. `sample` over 8 s at 2000 sessions: **2025 of ~5900 main-thread
  samples, 34%** — on 38 rows, long after those rows were capped.

- **Cost** — three changes, biggest first: memoize on the argument tuple (the
  function is pure and a title does not change between frames); binary search
  the cut point, O(log n) measurements instead of O(n), which is the cold path;
  one reusable scratch buffer so the probes stop allocating. 1.55x at 2000
  (2.227 → 1.438 ms), and it is what made the curve *flat* rather than merely
  lower.

  The algorithm moved to `src/util/ellipsize.h` to be testable at all — the
  metric is now a callable, so a headless test can hand it a ruler.
  `tests/unit/test_ellipsize.cpp` checks the new answer against the old linear
  scan at every width from zero to past the end of the string.

- **What the test changed about the fix** — I wrote the header claiming the two
  agree. The test's synthetic backwards-kern metric said otherwise, and it is
  right: prefix width is not monotonic under kerning, a dip can hide an
  arbitrarily long fitting prefix behind a non-fitting one, and no bounded
  correction recovers the true maximum. The guarantee in the header is now the
  true one, in two halves. A third neuter earned its keep by *not* failing — a
  walk-back loop I had written to guarantee the result fits deleted cleanly
  with every test green, because `lo` is only ever assigned a boundary that
  measured as fitting. Dead safety net, removed.

  This is **afterhours_gaps.md #116**: there is no way to ask the library how
  much of a string fits in a width. fontstash already walks the glyphs
  accumulating advances; "how much fits in W" is that walk with an early exit —
  one pass, exact under kerning, cheaper than what it replaces. Around 300
  lines of hanabi exist because it is not there.

## 4. The freeze was in the search box, and no scenario had ever typed

- **What I wanted** — the last of the four suspects: something that re-derives
  itself on a keystroke or a frame. Four stress scenarios existed (`idle`,
  `scroll`, `threads`, `tabs`) and not one of them typed anything, so the
  sidebar filter — the one path that touches every session in the catalog *and*
  reads a file for each one — had never been run under load at all.

- **What happened** — a `search` scenario, added, and its first run:

  | sessions | 0 | 100 | 500 | 2000 | 20000 |
  |---|---|---|---|---|---|
  | ms/frame | 0.940 | 2.162 | 5.450 | **18.061** | **212.958** |
  | entities | 411 | 1134 | 3134 | 10615 | — |

  Eighteen milliseconds a frame at 2000 sessions with a word in the search box,
  against 1.36 idle on the same catalog. At 20,000 it is **213 ms — 4.7 frames
  per second.** That is not slow, that is the *"until it freezes"* in the
  original report, and it is one keystroke away on any screen.

  Two independent causes, both the same shape as everything else here:

  1. **The result list was uncapped.** In the code: "A live search shows ALL
     matches uncapped — the filter has already narrowed the list, and hiding
     matches behind 'show more' would defeat it." True of a query that narrows
     to a handful; false of the **first keystroke**, which narrows nothing.
     Typing `r` matched most of the catalog and drew all of it. The one moment
     the list is guaranteed to be at its longest is the moment the cap was
     switched off.
  2. **`content_matches` opens a file per session, per frame.** It reads the
     transcript whole, lowercases every byte and scans it — for every session
     whose title did not match, on every frame the query is live. Two thousand
     file opens a frame, and it stays two thousand when nothing is cached,
     because a failed open is still a syscall.

- **Cost** — the cap is the same cap and the same "Show N more" the unfiltered
  list already has; the header still carries the true match count, so the search
  still *answers* with all of them. The memo is keyed on `(id, query)` and
  dropped when the corpus changes — a transcript written, wiped or trimmed
  bumps a generation. And typing gets cheaper as you type: if the new query
  contains the old as a substring, nothing that missed the old can match the
  new, so narrowing a search re-reads the hits, not the catalog.

  | sessions | 0 | 100 | 500 | 2000 | 20000 |
  |---|---|---|---|---|---|
  | before | 1.010 | 2.359 | 5.812 | 19.412 | 212.958 |
  | after | 0.949 | 1.647 | 1.873 | **2.120** | **4.537** |
  | | 1.06x | 1.43x | 3.10x | **9.16x** | **46.94x** |

- **The part worth keeping** — this was the largest single number on the branch
  and it was found by *adding a scenario*, not by reading code. Four scenarios
  had been written and the one input a person gives a list most often was not
  among them. **A harness only measures the things somebody thought to drive.**

- **And the test almost proved nothing.** The memo's test is about the
  invalidation, not the speed — a stale `false` is a thread that has silently
  dropped out of your search results. The first draft passed under *both*
  neuters, because it wandered off to another query between reading an answer
  and invalidating it, and a query change drops the memo, so it had already
  thrown away the entry it meant to catch going stale. The ordering of those
  lines is now the test and the comment says so.

## 5. Everything else the brief predicted about the sidebar was real, and measured as noise

The brief named four suspects in `sidebar_system.h`, and all four were exactly
as described. They are also, at any catalog size a person will have, worth
nothing. This section is here because "we looked and it was not there" is a
result.

| suspect | what it was | what it was worth |
|---|---|---|
| smart-view counts | two full passes over `app.sessions` per frame for four integers | noise |
| `blocked_count` | the first of those two passes | noise |
| the members collect | a `vector<const SessionSummary*>` of all 2000, malloc'd and freed per frame | noise |
| the sort | `std::sort` over all 2000 per frame to render 38 | noise |

Fixed anyway — one pass instead of two (and two of the four integers were dead:
Pinned and Archived carry no badge and both rows pass `-1`), `partial_sort` to
the render limit instead of a full sort, and a reused buffer. Interleaved A/B,
median of 7:

| sessions | before | after | ratio |
|---|---|---|---|
| 2000 | 3.131 | 3.216 | 0.97x |
| 20000 | 1.977 | 1.765 | 1.12x |
| 100000 | 2.892 | 2.467 | 1.17x |

**At 2000 sessions this is noise.** All the linear work left in the sidebar
together measures at about **26 ns per session per frame** — 0.05 ms of a
1.44 ms frame. It is in the branch because it is structurally right and because
it is 12–17% once the catalog is an order of magnitude bigger, not because it
moved the number anyone reported.

The one linear pass that remains is deliberate. Selecting the newest 38 of n
requires looking at n; the alternative is a maintained sorted index, which has
to be invalidated by every writer of the sessions vector — the star toggle, the
archive toggle, the mute toggle, the loader's refetch, a toast's undo. A stale
count is a smart view that says 6 and lists 5. 0.05 ms is not worth that.

## 6. The profiler and the stopwatch disagreed, and the stopwatch won

- **What I wanted** — to finish the job `sample` pointed at. After the memo
  landed it *still* put `fit_to_width` at 15% of the main thread, which reads
  like a cache that is missing.

- **What happened** — the cache was not missing. Instrumented over 3000 idle
  frames at 2000 sessions: **100,000 hits against 38 misses**, one miss per
  visible row for the life of the process. What the profile was pointing at was
  the hit path, which was building an owning key (a copy of the title) to
  search with and returning the fitted string by value — three allocations per
  row per frame counting `strip_parked_prefix`, for bytes that already existed.

  Fixed properly: a transparent hash/equal pair so a `string_view` searches the
  map, the hit returned by const reference, and `display_title_view` for the
  prefix strip. 76 fewer mallocs a frame. Measured:

  | sessions | before | after | ratio |
  |---|---|---|---|
  | 0 | 1.015 | 1.024 | 0.99x |
  | 2000 | 1.379 | 1.385 | 1.00x |
  | 20000 | 1.769 | 1.704 | 1.04x |

- **Cost** — an hour, and a commit that says in its own message that it is
  hygiene. `sample` said 15%; the stopwatch says nothing. Where they disagree
  the stopwatch wins. Sampling attribution on inlined, templated C++ is a
  pointer, not a measurement — it is excellent at *what to look at next* and
  should never be quoted as *what it costs*.

## 7. A shared /tmp is not a place to keep a harness

- **What I wanted** — before/after numbers for the scroll scenario.

- **What happened** — 0.81x. A 24% regression at 2000 sessions, clean and
  monotonic across the curve, exactly where my changes were. I profiled it,
  instrumented the title cache looking for thrash, and re-ran the A/B at higher
  rep counts before noticing the entity count in my own output: **3063**, the
  number this branch had removed two commits earlier.

  This box is shared. Another agent had replaced `/tmp/soak.sh` with a
  same-named script hard-coded to a *different checkout's* binary, ignoring the
  `BIN` variable mine took. Every measurement for about twenty minutes was of a
  build I had not made. Re-measured against the right binary, that scenario is
  1.32 ms/frame at 531 entities and there is no regression in it at all.

- **Cost** — forty minutes, and it would have been the whole afternoon if the
  entity count had not been printed next to the timing. The harness now lives
  in `scripts/perf_curve.sh` and `scripts/perf_ab.sh`, with a private HOME per
  *run* (a run that inherits a persisted folded shelf renders something else),
  interleaved A/B (a load spike during a block of A runs does not look like
  noise, it looks like a clean win), a header line naming the binary, and a
  hard exit on empty output.

- **The part worth keeping** — the entity count is the honest column. Frame
  time on a shared box is a soft number that can be wrong for six reasons.
  Entity count is exact, identical run to run, and it caught a wrong binary
  that three timing runs and a profile did not.

- **And the second-best story about not believing a number.** The suite has one
  red test, `select_word_and_line.e2e`, on this branch and on unmodified
  master. My first diagnosis was that the harness spaces a double-click in
  frames while the widget measures the gap in wall clock, which is plausible,
  fits a load-dependent failure, is the mirror of FRICTION_LOG #2 — and is
  wrong. It is a stale coordinate: the script clicks y=225 and hanabi's own hit
  test says the transcript's body lines are at 228/244/260, so the press never
  lands. I had already written the wrong version into `afterhours_gaps.md` and
  had to retract it. **afterhours_gaps.md #117** is the corrected entry, and
  the correction is the more useful half: a mechanism that explains the symptom
  is not the same as the mechanism, and the cheap way to tell them apart was
  one `printf` in the hit test, which I should have run before writing anything
  down.

---

## How to measure this

```bash
# The curve for one binary.
SIZES="0 100 500 1000 2000" scripts/perf_curve.sh idle 5 800

# Two binaries, interleaved.
scripts/perf_ab.sh /path/to/old/hanabi.exe output/hanabi.exe idle 5 800

# What is being materialized, by widget.
HANABI_SOAK_CENSUS=1  # with any HANABI_SOAK run

# How many rows the sidebar DREW, out of how many matched. The gap between
# them is virtualization, and it is the only form of it a script can read.
HANABI_ROW_AUDIT=1
```

Scenarios are `idle`, `scroll`, `threads`, `tabs`, `search`
(`src/util/stress.h`). If you are adding one, section 4 is the argument for
bothering.
Read the entity count first and the frame time second.

Both scripts report the MINIMUM bucket per run and the MEDIAN across runs:
contention only ever makes a bucket slower, so the minimum is the
least-contended estimate of our own cost. That is biased low in absolute terms
— these are comparison instruments. Compare the columns, never a number here
against a number from somewhere else.

## What is left, and where it is

Not the sidebar. After this branch the idle profile at 2000 sessions is
dominated by afterhours' own passes, and the two biggest items are already
filed:

- **gap #42** — the draw path re-measures every string from scratch every
  frame. `position_text_ex` calls `measure_text` directly, bypassing
  `TextMeasureCache`; `rendering.h` even carries a `// TODO add some caching
  here?` at the call site. `measure_text` + `position_text_ex` were 1157 of
  3908 main-thread samples (~30%) in the scroll profile, on text identical to
  the frame before.
- **gap #115** — nothing retires a widget (above).

One thing seen in the profile that is not yet filed anywhere and is worth a
look before it is: `imm::mk()` builds a `std::stringstream`, formats a
`source_location`'s file, line, column and function into it, and hashes the
resulting string — **per widget, per frame**. 330 of 3908 samples (8.4%) in the
scroll profile. The identity it is computing is a compile-time constant plus
two integers. Not filed because it wants a measurement of its own rather than a
number read off a profile of something else — see section 6 for why that
distinction is not pedantry.
