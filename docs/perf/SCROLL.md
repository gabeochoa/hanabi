# The scroll, and the list nobody was scrolling

**Why this file exists.** The bug report is one sentence: *"what I did was just
open the program and scroll the sidebar up and down until it broke."* Two
things had already been found and fixed under that sentence — a Metal
autorelease leak (`docs/perf/MEMORY.md`) and a Home pane that built a card per
attention-worthy session (`docs/perf/SIDEBAR.md`) — and neither of them was
the scroll. Nobody had profiled the scroll path itself under sustained input.

This is that. Written the way `docs/visual-parity/FRICTION_LOG.md` is: what was
wanted, what happened, what it cost, numbers inline. A number is worth more
than an adjective.

The headline is one line. **An expanded sidebar list cost 17.2 ms of CPU a
frame at a 2000-session catalog, flat, from the first frame to the last, and it
is one click away on any catalog that size.** It is 1.53 ms now.

---

## The headline

`HANABI_STRESS=scrollall`, 2000-session catalog, 3000 frames, CPU time
(`CLOCK_THREAD_CPUTIME_ID`, not wall clock — see section 1).

| | before | after | |
|---|---:|---:|---|
| frame | 17.217 ms | **1.533 ms** | 11.2x |
| entities | 6645 | 496 | 13.4x |
| allocations / frame | 46,508 | 3,703 | 12.6x |
| text measures / frame | 215.8 | 11.8 | 18.3x |
| RSS | 66,912 KB | 53,104 KB | −13.8 MB |
| `chat_row` entities | 2020 | 29 | |

1.533 ms is *below* the 1.55 ms the same catalog costs with the list capped, so
the expanded list now costs what the capped one does. That is the property; the
ratio is just what it happened to be worth today.

Entity count against catalog size, list expanded, three runs at each size:

| sessions | 20 | 2000 | 20000 |
|---|---:|---:|---:|
| entities | 364 364 364 | 472 472 472 | 483 483 483 |

Zero spread, to the entity, and flat across a 1000x catalog.

---

## 1. The scroll arm was a second idle arm, and had been all along

- **What I wanted** — to reproduce. `HANABI_STRESS=scroll` plus `HANABI_SOAK=1`
  at 2000 sessions and a long run, per the brief, and read the per-bucket
  curve for anything with a positive slope.

- **What happened** — nothing had a slope, and nothing had anything else
  either. Six thousand frames: RSS +80 KB per 1000, heap +23 KB, entities +0,
  frame time flat. A clean PASS on the arm named after the bug.

  So I ran the arm against the control, with counters instead of a clock.
  2000 frames, 2000 sessions, `HANABI_PROF=1`:

  | | allocations | uncached text measures |
  |---|---:|---:|
  | idle | 7,422,071 | 27,683 |
  | scroll | 7,422,153 | 27,683 |

  Eighty-two allocations apart in 7.4 million, and the text-measure counter
  equal to the call. The arm was not measuring scrolling.

  It *was* scrolling — instrumented, the offset moves 0 → 12 → 24 and clamps at
  424 — and that is the finding. The sidebar caps its flat list at
  `fillCap = viewportRows * 2`, so the list is about 26 rows whatever the
  catalog is, and wheeling it slides a clip rectangle over rows that were going
  to be built anyway. **Scrolling a capped list is free, and it is free because
  the list is short, not because the scroll is cheap.**

- **The part worth keeping** — this is `SIDEBAR.md` section 4's lesson arriving
  from the other side. There it was *a harness only measures the things
  somebody thought to drive*. Here somebody did think to drive it, wrote the
  arm, named it after the report, and the arm drove a list the report was not
  about. **An arm that runs is not an arm that measures.** The counters are how
  you tell, and they cost one env var.

## 2. The list a person scrolls is the one they asked to see all of

- **What I wanted** — the list from the report. Scrolling down a 2000-session
  sidebar ends at a row that says **"Show 1962 more…"**, because the cap put it
  there. That is where the gesture in the report lands.

- **What happened** — clicking it sets the render limit to the whole list.
  `HANABI_STRESS=scrollall` clicks it — through `ecs::more_key`, the same
  sentinel the expander row writes — and then sweeps at 96 px (three rows) a
  frame over a 1200-frame triangle, so one half-period walks 1800 rows.

  | | scroll | scrollall | |
  |---|---:|---:|---|
  | frame (CPU) | 1.55 ms | **17.22 ms** | 11.1x |
  | entities | 461 | 6645 | 14.4x |
  | allocations / frame | 3,711 | 46,508 | 12.5x |
  | text measures / frame | 13.8 | 215.8 | 15.6x |

  The census says it in one glance:

  ```
  [soak] entity census (top 20 by debug name):     # 2000 sessions, expanded
  [soak]     2020  row_title
  [soak]     2020  row_glyph
  [soak]     2020  chat_row
  [soak]      207  row_subagent_count
  ```

  17.2 ms of CPU is 58 frames a second with nothing left over, before the
  window server gets a turn, on a machine doing nothing else. On the shared box
  this was measured on it is 30–60 ms of wall clock, which is what "it freezes"
  looks like from the outside.

- **The part worth keeping** — **the sidebar was never virtualized; it was
  CAPPED, which looks the same until the user declines the cap.** A cap answers
  a product question — how much of this list has the user asked to see — and it
  had been quietly doing a cost job it cannot do, because the user is allowed
  to ask for all of it. The two numbers are different and the gap between them
  is unbounded.

  `SIDEBAR.md` section 1 says "the sidebar was innocent" and it was right about
  the frame it measured. The sidebar is innocent for as long as nobody clicks
  the row the sidebar put at the bottom of itself.

## 3. The fix, and the two things that made it more than arithmetic

- **What I wanted** — build the rows on screen, not the rows asked for.

- **What happened** — the arithmetic is easy: rows are a fixed `kRowHeight`, so
  `first = offset / pitch`, `span = viewport / pitch + overscan`, and two
  spacer `div`s of the exact height of the rows that were skipped keep
  `content_size`, the scrollbar thumb, the clamp and the y of any given row at
  the numbers they would have been.

  Two things were not easy, and both are the library's shape rather than the
  problem's — filed as **afterhours_gaps.md #170** and **#171**.

  1. **The offset is a frame stale, by construction.** The build runs before
     `RunAutoLayout`, before `MeasureScrollViews`, before `ease_scroll`. A
     window sized exactly to what it reads shows a strip of empty sidebar for
     one frame every time the view moves — a flicker that is never seen in
     development and gets reported as "it flickers when I scroll fast".

     The compensation is exact rather than guessed: the distance the easing is
     about to travel is `|scroll_target − scroll_offset|`, so the overscan is
     that, plus three rows of slack. Which is why the constant is *three* and
     not the twenty a guess would have picked. It is bounded at 32 rows so a
     fling cannot turn the window back into the list.

  2. **The row id is the memory policy.** `mk()` retains an entity per distinct
     id forever and nothing retires one (**#115**), so keying rows on the row
     INDEX — the natural spelling, the one where the widget "is" the row —
     mints an entity for every row ever scrolled past. Measured on a
     1600-frame sweep of a 2000-row list: **+180 live blocks per 1000 frames,
     still climbing at the end of the run**, against −18 for the same list
     keyed on the window slot. The virtualization would have been perfect and
     the leak would have been exactly the one it was written to remove.

     Slot keys are flat, and they cost something: everything the library keys
     on entity id now belongs to a *position* rather than to a row — hover,
     `active`, the click listener's `down`, the debug name a driver looks the
     widget up by. Mostly that is right, because the cursor is over a place.
     The drag path keeps the absolute index, because a reorder is about the
     row's place in the list.

- **Cost** — the numbers at the top of this file. One 90-line function, two
  spacers, and a scripted test.

## 4. The bug had no slope, so a trend gate would have shrugged at it forever

- **What I wanted** — the gate the brief asked for: a scroll soak whose verdict
  fails if frame time or the live-block count trends upward across buckets.

- **What happened** — I built it, pointed it at the build with the fix reverted,
  and it passed. **17.040 ms then 17.326 ms; ratio 1.017; blocks +1.9.**

  Of course it did. The defect is eleven times too expensive on frame one and
  eleven times too expensive on frame six thousand. "Gets slower every second"
  was the *other* bug in this report — the autorelease leak, fixed weeks ago —
  and the half that was left is not a slope at all, it is a cliff you walk off
  by clicking a row.

  So `scripts/scroll_gate.sh` has two arms:

  | arm | what it asks | budget |
  |---|---|---|
  | level | expanded-list entity count at 20 sessions against 2000 | 1.60x |
  | trend | min-of-half frame CPU, last half over first | 1.15x |
  | trend | live malloc blocks, per 1000 frames | +40 |

  The block budget was +150 and the arm was red one run in five. Section 8
  is what that turned out to be.

  The level arm runs first because it is the one that catches the bug that
  happened. It is also exact: entity counts have zero spread, run to run, which
  no timing on this box does.

- **The part worth keeping** — **"does it get worse?" and "is it bad?" are
  different questions and a slope only answers the first.** Every gate this
  project added after the leak measures a slope, because the leak was a slope.
  The next one will not be.

  All three arms were made to fail on purpose, because a gate nobody has broken
  is a gate nobody knows the sensitivity of:

  | arm | defect | read |
  |---|---|---|
  | level | `row_window()` returns the whole list | 16.61x vs 1.60 |
  | blocks | ids keyed on row index, not window slot (#115's shape) | +277/1k vs 40 |
  | frame cpu | a per-frame walk over an index of rows visited | 1.223x vs 1.15 |

  The frame-CPU arm fires at about **+0.47 ms of drift across the halves of a
  1600-frame run**. Anything a person would describe as getting slower is far
  larger than that.

## 4b. The one-in-five red was the measurement, and it always had been

- **What I wanted** — the cause of the step this file's owner was asked to
  explain: "a real, occasional, one-bucket step of a few hundred live blocks —
  something filling late, not something leaking, because the run after it is
  flat again." Three claims about the app, none of them checked. A retry had
  been bolted over it, which is the part that matters: a retry that hides a
  genuine 400-block step hides the next real leak too.

- **What happened** — it reproduced in three runs out of ten, so the first
  thing to do was look at the buckets rather than the verdict. The failing
  runs did not have a step in the SECOND half. They had a first bucket about
  900 blocks LOW:

  ```
  pass:  102604  102756  102757  102756
  fail:  101705  102525  102098  102730
  ```

  At one sample every 50 frames the shape is not a step at all. It is a ramp
  down of almost exactly one block per frame for a thousand frames, and then a
  single jump back up of ~950. Per frame, the jump is **+1018 blocks for
  +16 KB** — a thousand allocations of sixteen bytes each, appearing in one
  frame, in a run whose entity count, RSS and heap bytes do not move.

  Nothing allocates like that. So the next question was whether anything
  allocated at all, and the way to ask it is to walk the heap instead of asking
  the allocator. `HANABI_SOAK_SIZES=1` prints the live blocks grouped by size,
  from the zone's own enumerator:

  | frame | zone tally | heap walk | difference |
  |---:|---:|---:|---:|
  | 20 | 102497 | 100699 | 1798 |
  | 500 | 101828 | 100512 | 1316 |
  | 580 | 102769 | 100510 | 2259 |
  | 1540 | 101960 | 100685 | 1275 |

  **The heap is flat to nineteen blocks. The moving quantity is the difference
  between the two columns.** `malloc_zone_statistics().blocks_in_use` is the
  allocator's own running tally, not a count of live blocks, and on this OS it
  drifts. Setting `MallocNanoZone=0` removes the drift entirely — two runs,
  spreads of 18 and 20 — which places it in the nano allocator's per-magazine
  object counters, approximate by construction and re-synced in lumps.

  It is not specific to scrolling either. The same drift is in `idle` and in
  `scroll`, in about half of all runs:

  ```
  idle, run 1   100580 100483 100384 ... 99766 100696 100593   (spread 930)
  idle, run 2   100724 100727 100728 ... 100710 100712 100713  (spread  19)
  ```

  Whether the lump lands inside a bucket half decided the verdict. That is the
  whole of the one-in-five.

- **The fix** — `src/util/heap_walk.h` counts live blocks by walking the zones
  (`introspect->enumerator` with a null reader, which means "these addresses
  are mine"). Exact, repeatable, and still sensitive: the row-id defect the
  block arm exists to catch reads +8,900 blocks on the walk. The tally is kept
  as a report-only column on the bucket line, so the divergence stays visible
  rather than becoming a mystery again. `tests/unit/test_heap_walk.cpp` pins it
  with the drift itself — churn 200,000 x 48 bytes, free every one, and the
  walk reads +0 against a tally that reads +341, the same number every round.

  Twelve consecutive clean runs of the arm now span **6.9** blocks per 1000
  frames. They spanned **845**. The budget came down from 150 to 40 and the
  retry came off.

- **Two other things the fix exposed**, both about giving each half more than
  two buckets to reduce over:

  1. The settle was 700 frames and the driven triangle's period is 1200, so
     the first measured bucket was still watching the title memo fill — worth
     +184 blocks, which two buckets a half cannot median away. Every clean run
     read +110 to +117 against a budget of 150, and nobody had asked why a
     "flat" arm sat at 75% of its budget. Settle is 1300 now and it reads
     under +9.
  2. The frame-CPU arm has its own flake, and this file's claim that "a busy
     box cannot produce it" is wrong. Contention only ever ADDS time, yes — so
     the minimum of a half is clean **only if at least one bucket in that half
     was clean.** With two buckets a half, a busy patch covering both of the
     last half's buckets is a FAIL: one run in ten read 1.722x on a tree whose
     block column was flat. Buckets are 200 frames now, four to a half.

- **The part worth keeping** — **a retry is a claim about a distribution, and
  nobody had looked at the distribution.** The note over that retry made three
  statements about the app ("real", "filling late", "not leaking") and the
  quantity it described was never produced by the app at all. The tell was
  there in the printed line the whole time: a thousand blocks arriving in one
  frame for sixteen kilobytes, next to an RSS that did not move.

  Retries have their place — `soak_gate.sh` keeps one, over a whole-run failure
  that a killed process produces. This one was over a NUMBER, and a number that
  needs a retry needs an explanation first.

## 5. Wall clock was measuring the neighbours

- **What I wanted** — to read the soak's frame-time column and believe it.

- **What happened** — one 6000-frame scroll soak, the same binary drawing the
  same frame throughout, memory flat to the kilobyte:

  ```
  1.788  1.556  1.476  1.748  7.640  7.335  5.592  3.998  2.246  2.087
  1.851  1.566
  ```

  A 5.2x hump in the middle of a run that did nothing to deserve it. Three
  other agents build on this box; the load average across today's runs ran 8 to
  34. The soak's frame-time verdict was built on that column, which is why its
  budget had to be 3.0 ms per 1000 frames — loose enough that the only
  regression it can catch is one nobody needs a gate to notice.

  `CLOCK_THREAD_CPUTIME_ID` does not count time this thread was not running.
  48 buckets over 6 identical runs:

  | | min | max | spread |
  |---|---:|---:|---:|
  | wall clock | 1.482 | 4.830 | 3.26x |
  | CPU time | 1.480 | 2.679 | 1.81x |

- **And the residue is not contention.** 1.81x is still a lot for identical
  work. `CLOCK_THREAD_CPUTIME_ID` counts wall time *while scheduled*, so a
  thread parked on an efficiency core burns more of it doing the same thing. No
  clock available in-process removes that. It is why the scroll gate reduces by
  MINIMUM across a half-run — contention and downclocking only ever *add* time,
  so the cheapest bucket of a half is the least-polluted estimate of what the
  app cost over it — and why it compares two of them as a ratio rather than
  quoting either.

  Wall clock is still printed, as a report-only row. It is the number a person
  feels, and a wall row well above the CPU row is the box being busy rather
  than the app being slow, which is the first thing worth knowing when a
  verdict looks wrong.

## 6. The settle pass settled the launch and nothing else

- **What I wanted** — a block-count budget for the gate. The first eight clean
  runs spanned **−148 to +847 blocks per 1000 frames**, which is not a budget,
  it is a shrug.

- **What happened** — the soak's settle loop renders without DRIVING. It never
  called the stress driver at all. So it settles the launch burst, correctly,
  and every scenario's own warm-up lands inside the measured window: the first
  tab `threads` opens, the first query `search` types, and for `scrollall` the
  entire first sweep of the list — during which a title memo fills with 1800
  entries while the first buckets are being sampled. Live blocks trending up,
  and it is a cache arriving.

  One monotonic counter now runs across both loops, so the scenario's phase is
  continuous over the boundary rather than restarting at it. The same eight
  runs:

  | | worst | best |
  |---|---:|---:|
  | blocks / 1000 frames, undriven settle | +847 | −149 |
  | blocks / 1000 frames, driven settle | **0** | −149 |

  The budget went from unsettable to 150.

- **The part worth keeping** — "settle before measuring" is in `stress.h`'s
  header comment, quoted from Puffin, and the loop underneath it did half of
  it. A settle that does not drive is a settle for the process, not for the
  scenario, and every scenario that does something on its first frames was
  measuring itself starting up.

## 7. What is left, priced

Not the scroll. After this branch the frame at 2000 sessions with the list
expanded is 1.43 ms, and the two things the sidebar itself still does per frame
are:

| phase | ms/frame | share |
|---|---:|---:|
| `sidebar.collect` | 0.1255 | 8.8% |
| `sidebar.sort` | 0.0315 | 2.2% |

`collect` is 62 ns per session and it is not the predicates — they are three
cheap comparisons — it is walking 2000 `SessionSummary` structs, id and title
and preview and folder, through a cache that cannot hold them.

**Both are catalog-linear and neither is scroll-linear**, which is the
distinction this branch is about: they cost the same whether the list is moving
or still. Neither is touched, for the reasons `SIDEBAR.md` section 5 already
gives — removing `collect` means an index invalidated by every writer of the
sessions vector, and a stale index is a sidebar that lists a thread that is not
there; removing the sort saves 0.015 ms against an ordering argument that has
to hold through `apply_row_order` moving up to 64 pinned rows forward. They are
priced here so the next person starts from a number rather than from "measures
as noise", which was recorded when the frame around them was 3.1 ms.

The rest is afterhours' own passes, and the two biggest are already filed:
**#42** (every string re-measured from scratch every frame) and **#115**
(nothing retires a widget).

---

## Footguns hit while doing this

Both cost real time and neither is about the app.

### `git stash` is shared across every worktree of a repo

There are eighty worktrees on this machine and several agents working in them
at once. `git stash push` and `git stash pop` operate on **one stack, per
repository, not per worktree.** I stashed my changes to build a pristine
binary, popped, and got somebody else's work — three modified files and two
new headers I had never seen — while my own sat one entry down. Their stash
entry was dropped from the list by my pop.

Recovering it is possible only because pop prints the dropped commit:

```
Dropped refs/stash@{0} (1dfd71ba72269799dd89e160db41086810513aba)
git stash store -m "restored" 1dfd71ba72269799dd89e160db41086810513aba
```

**Do not use `git stash` in this repo.** To compare against another revision,
add a worktree for it — `git worktree add ~/w/hanabi-scroll-base --detach main`
— and build there. It costs a submodule checkout and a first compile, and it
cannot take anyone else's work with it. `scripts/perf_ab.sh` already wants two
binaries anyway.

### A new worktree has no `vendor/afterhours`

It is a submodule, so `git worktree add` gives you an empty directory and the
build fails with `No rule to make target
'vendor/afterhours/src/plugins/files.cpp'`, which reads like a missing file
rather than a missing checkout. `git submodule update --init vendor/afterhours`
in the new worktree, once.

### The scaling gate's frame column will tell you your branch is 1.6x slower

It read 0.99 ms on `main` and 1.56 ms on this branch, back to back, at the same
catalog size, and the gap was clean and reproducible across three runs of each.
It is the box. Interleaved through `scripts/perf_ab.sh` the same two binaries
read 1.00x, 1.05x and 1.06x — this branch marginally *faster*.

This is `SIDEBAR.md` section 7 happening again to the next person, so: **never
compare a number from one block of runs against a number from another block.**
`perf_ab.sh` alternates for exactly this reason and it takes the same wall
clock to run.

---

## How to measure this

```bash
# What the block column is actually made of, by size class. This is the
# diagnostic that ended section 4b -- it was the column that did NOT move.
HANABI_SOAK_SIZES=1 ... | grep '^\[size\]'

# The reproduction. The scenario clicks "Show N more…" itself.
HANABI_BACKEND=mock HANABI_STRESS=scrollall HANABI_STRESS_SESSIONS=2000 \
  HANABI_SOAK=3000 HANABI_SOAK_EVERY=500 HANABI_SOAK_CENSUS=1 HANABI_PROF=1 \
  output/hanabi.exe --screenshot /tmp/o.png | grep -E '\[soak\]|\[prof\]'

# The gate, both arms.
make scroll-gate

# The trend verdict on any scenario, not just the gate's.
HANABI_SOAK_TREND=1 ...

# How many rows the sidebar BUILT, out of how many matched, and from where.
HANABI_ROW_AUDIT=1     # "sidebar rows 29 of 320 @ 291"

# Two binaries, interleaved. Never two blocks.
scripts/perf_ab.sh /path/to/base/hanabi.exe output/hanabi.exe scrollall 7 600
```

Read the entity count first, the allocation count second, and the frame time
last. The first two are exact and the third is a shared laptop.

### Generating load to reproduce, without leaving the load behind

`docs/perf/GATES.md` section 0b has the whole story and the trap idiom. The
short version, because this file is where somebody will be told to "reproduce
under load": **`jobs -p` does not give you your background PIDs in a
non-interactive shell**, so the kill matches nothing and every spinner is
reparented to init and burns a core until someone notices. Fifty-two were
leaked in one session this way, and the box reached a load average of 134 —
which then contaminated the very measurements the load was for. Capture `$!`
per job, `trap` the kill on EXIT INT TERM, and check
`ps -Ao pid,ppid,pcpu,args | awk '$2==1 && $3>10'` before you finish.
