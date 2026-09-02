# The screens whose job is to show everything, and how they stopped costing everything

**Why this file exists.** `docs/perf/RETIRE.md` ends with a section called
"What this does NOT fix", and the first item in it is this:

> `render_home` caps every section at 20 cards (`kMaxSection`, the
> `perf/sidebar-scaling` fix). `render_digest` — Blocked, Review, Starred,
> Archived — does not: it builds one card per matching session,
> unconditionally. At 2000 sessions a single frame of one of those screens
> builds **506 cards, 2,024 entities**, which is 8x what Home builds beside
> it. […] The fix is a cap (like Home's) or virtualization (like the
> sidebar's), and it is a product decision rather than a performance one —
> Blocked is *the* place you go to see everything blocked on you, and a cap
> there hides rows on the one screen whose job is not to.

Everything in that paragraph is true except the sentence that made it stop.
It is not a cap OR virtualization; it is virtualization, and the branch that
did the sidebar had already written it. This is that, applied to cards.

Measured 2026-08-25 on `gabeochoa-mac-GRQ7Y259H4`, shared with several other
agents. Every headline here is a COUNT — entities, widgets, allocations —
because for part of the day the box's load average was 134 and no timing taken
on it was worth anything. The timings that survive are the ones taken
interleaved, base binary and branch binary alternating, after that was fixed.

---

## The headline

A static Blocked screen at a 2000-session catalog, 1200 frames, the base
binary at `ef29c1a` and this branch alternating, three runs each:

| | before | after | |
| --- | ---: | ---: | --- |
| **allocations / frame** | **20,826.6** | **1,826.0** | **11.4x** |
| entities | 2480 | 509 | 4.9x |
| frame (CPU) | 5.95 ms | 1.16 ms | 5.1x |
| RSS | 57,264 KB | 52,944 KB | −4.3 MB |
| live heap blocks | 73,706 | 56,136 | −17,570 |

Zero spread on every count: 2480 and 509, exactly, on all three runs of each.

Per screen, `HANABI_VIEW=<view>`, min-of-120-frames, interleaved:

| screen | widgets before | after | frame before | after |
| --- | ---: | ---: | ---: | ---: |
| Blocked | 2472 | **501** | 5.53–5.65 ms | **1.03–1.04 ms** |
| Review | 1796 | **501** | 3.93–3.98 ms | **1.01–1.03 ms** |
| Starred | 1349¹ | **508** | | |
| Archived | 1349¹ | **508** | | |
| Home | 444 | 444 | 1.25 ms | 1.25 ms |

¹ at `HANABI_STRESS_PINNED=10 HANABI_STRESS_ARCHIVED=10`, which did not exist
before this branch. See section 3 — those two screens were **empty at every
catalog size**, and that is its own finding.

And the property, rather than the ratio — widgets against catalog size, one
screen, three sizes:

| sessions | 20 | 2000 | 20,000 |
| --- | ---: | ---: | ---: |
| Blocked, entities | 394 | 509 | 539 |
| Home, entities | 346 | 452 | 481 |

Blocked now scales like Home, which is to say it does not scale.

---

## 1. Two heights, and why that is the whole difference from the sidebar

`docs/perf/SCROLL.md` section 3 makes the sidebar's window sound like
arithmetic, and for the sidebar it is:

> the arithmetic is easy: rows are a fixed `kRowHeight`, so
> `first = offset / pitch`, `span = viewport / pitch + overscan`

A digest card is **34 px tall or 52**, depending on whether its second line is
short enough to ride inline on the title row. So there is no division. The
window is a prefix sum, and to compute it something has to know a card's
height *without building the card* — which is the thing the card used to
decide about itself, on the way past, in a five-line expression with no name.

That expression is now `ecs::digest::card_pitch`, in a header of its own, and
**both callers go through it**: the window's arithmetic is made of it, and the
built card adds it to the running content-y. They cannot drift because they
are the same call. (`afterhours_gaps.md` **#224**: nothing in the library can
tell you how tall a child would be, so the app restates the box model —
margin-top plus body plus margin-bottom, and the claim that adjacent margins
do not collapse, which is true of afterhours and the opposite of CSS.)

The estimate-and-correct shape the brief offered as the alternative was not
needed and would have been worse. `test_window_over_mixed_heights` carries the
reason as an assertion: scrolled to a card whose real top is at y=5100, a
uniform-pitch guess puts the first built card at index 121 — past the card
actually under the top edge — so the top of the viewport paints empty. The
exact sum costs 0.023 ms a frame at 506 cards, which is the entire price of
knowing.

## 2. The pitch pass allocates nothing, and that was a design decision, not luck

506 sub-lines get composed per frame to decide 506 heights. Done the obvious
way — `std::string card_meta(const SessionSummary&)`, as it was — that is 506
heap round trips a frame to answer a question about a length, and the window
would have replaced 2,024 entities with 506 mallocs.

So `sub_line` returns a `std::string_view`. Every branch that does not compose
views straight into the session — the preview, its tail after the separator, a
string literal — and the two that do compose write into a caller-owned scratch
the pass reuses. Measured:

```
[prof] digest.pitch          0.0228 ms/frame
[prof] digest.pitch.allocs        0.0 /frame
```

Zero. Not "small". The unit test asserts it directly, by pinning the scratch's
buffer address across a whole second pass over the rows.

The render path still allocates, about 7.7 times per built card, and that is
`afterhours_gaps.md` **#221**: `with_label` takes a `const std::string&`, so
every label is a heap allocation per widget per frame even when the text
already exists. The split is the interesting part — **the measurement path is
allocation-free and the render path is not**, which is only tolerable because
the window made the render path small.

## 3. Two of the five screens held nothing, at any catalog size

This is the finding I did not go looking for.

`HANABI_VIEW=starred HANABI_STRESS_SESSIONS=2000` renders an **empty state**.
So does Archived. The synthetic catalog cycles state and tag on the row index,
which fills Blocked and Review; there is no cycle that can fill the other two,
because starring and archiving are things a *user* does rather than states a
backend reports.

So for as long as the scaling gate has existed, two of the five screens have
been passing it the way a screen nobody renders passes it. And the "63 cards"
I reported for Starred and Archived in this branch's first commit were not
theirs at all: they were **Home's leftovers** — entities from the frames before
`HANABI_VIEW` is applied, which nothing retires (gap #115). I measured a
screen and read another screen's residue.

`HANABI_STRESS_PINNED` / `HANABI_STRESS_ARCHIVED` fill them now, as a
percentage of the synthetic rows so they track whatever size was asked for,
deterministic on the index, and **off by default** — every existing script that
sets `HANABI_STRESS_SESSIONS` is pinned to the row counts it produces, and
archiving a tenth of them would move all of those numbers.

The gate's third arm exists because of this: it **fails when a digest view
matches fewer than 100 sessions**. An empty screen is not a bounded screen, and
it passes every other arm for the wrong reason.

## 4. The frame with no viewport was the whole fix, twice over

The window reads `HasScrollView::viewport_size.y`. On frame one that is zero —
the scroll view exists but `MeasureScrollViews` has not run — and the obvious
fallback, the one the sidebar uses and the one I wrote first, is "build the
lot; by frame two there is a number to read".

Measured, the same binary, that fallback the only difference:

```
  fallback = "build everything"    widgets 2473   frame 2.91 ms
  fallback = the pane's own listH  widgets  501   frame 1.05 ms
```

**The frame time was already right.** 2.91 ms against the 5.58 it started at,
a 1.9x improvement, on a build whose widget count had not moved by a single
entity — 2473 against the 2472 it started at.

Nothing retires a widget (#115), so ONE unmeasured frame mints four entities
per matched session and the app carries all 2276 of them for the rest of the
process. Frame two builds thirty and the census still reads 2276. A branch
whose entire subject is entity counts came within half an hour of shipping
with the entity count unchanged, and what caught it was reading the census
instead of the clock.

Filed as **#220**, with the one-line upstream fix that would have made it a
compile error: make `viewport_size` an `optional`, so "not measured yet" is
not silently the same value as "measured as zero".

## 5. The cursor walks the list; the window builds the viewport; those are different passes

The keyboard cursor is the one thing in this pane that must NOT agree with the
window. A person arrowing down has asked to reach the end of the list, and a
cursor over the built cards stops dead at row sixteen with sixty-five below it.

So `render_digest` keeps two passes — one over every matched row, which builds
`listRows_` and sums `listY_`, and one over the window, which builds cards —
and `digest_card` takes a `trackCursor` flag so it does not count the built
ones a second time on the way past.

**Nothing was watching that.** `tests/ui/list_navigation.e2e` walks Home's
list, which is capped rather than windowed, so it has no rows a cursor could
fail to reach. `digest_is_windowed.e2e` drives the mouse wheel, which moves
the offset directly and never asks the cursor anything. The half of the fix
most likely to be quietly deleted — collapsing two passes into one is the
obvious simplification, and the screen looks completely correct until the
seventeenth Down — had no assertion on it at all.

This is the session's second lesson landing on the first: *a perf fix that
reroutes work silently retires every gate watching the old path* — and the
path this branch rerouted was not the one the numbers are about.
`digest_cursor_walks_the_whole_list.e2e` watches it now. Rehearsed against the
collapse:

```
  window follows the cursor   digest cards 19 of 81 @ 10
  cursor walks only the built digest cards 16 of 81 @ 0   (forever)
```

The card count stays perfectly right in the broken case, which is what this
class of defect looks like from every other assertion in the suite.

## 6. Home was not building too much; it was deciding too much

Home has been capped since `perf/sidebar-scaling` and its entity count is flat
— 346 / 452 / 481 across a 1000x catalog. What the cap does not bound is the
work Home does to decide WHICH eighty cards, which is four full walks of the
catalog plus a full sort of it, every frame:

| phase | 2000 sessions | 20,000 | allocations/frame @2000 |
| --- | ---: | ---: | ---: |
| `home.recent` | 0.0124 ms | 0.0933 | 15.7 |
| `home.partition` | 0.0105 | 0.0780 | 30.0 |
| `home.sort` | 0.0094 | 0.1980 | 0 |

The four vectors are reused now (**45.7 → 0.09 allocations a frame**), and the
sort is a `partial_sort` over `kMaxSection`. Interleaved in one binary,
alternating on an env var so the arms cannot be a block apart, five runs each:

```
  2000     sort 0.0095 0.0094 0.0093 0.0094 0.0093
           part 0.0053 0.0054 0.0052 0.0053 0.0054    1.77x
  20,000   sort 0.1884 0.2057 0.2002 0.1955 0.2065
           part 0.0415 0.0409 0.0427 0.0429 0.0422    4.72x
```

At 2000 that is four microseconds and not worth a commit for the time. It is
here for the SHAPE, which the 20,000 column makes visible: 0.198 ms is 6% of
that frame spent choosing twenty rows out of twenty thousand.

**The one thing that is not a pure win.** `std::sort` and `std::partial_sort`
are both unstable and break ties differently, so two sessions stamped at the
same second can swap places between builds. Nothing in the fixtures ties, so
nothing moves; a real backend could. The honest fix is a total order
(tie-break on id) and it belongs with a test that has two threads at one
timestamp. Recorded rather than assumed — and recorded because I spent an hour
bisecting a UI-suite failure onto this commit before finding it was section
7's problem instead.

## 7. What is left, priced

| phase | ms/frame @2000 | note |
| --- | ---: | --- |
| `sidebar.collect` | 0.1245 | SCROLL.md §7's finding, still the biggest named phase |
| `digest.build` | 0.0855 | bounded by the window; 611 allocations, gap #221 |
| `digest.pitch` | 0.0228 | catalog-linear, allocation-free |
| `digest.collect` | 0.0047 | catalog-linear, 0.02 allocations |
| FRAME (cpu) | 1.365 | |

`digest.collect` and `digest.pitch` are both O(catalog) and both stay, for the
reason `SIDEBAR.md` §5 gives about `sidebar.collect`: removing the walk means
an index invalidated by every writer of the sessions vector, and a stale index
is a Blocked screen listing a thread that is not blocked. 0.0275 ms of the two
together is 2% of the frame.

**One thing I could not account for, and it is not on any screen.** At 20,000
sessions the app allocates 7,644 times a frame against 5,065 at 2,000 — and
the main pane accounts for +225 of that delta and the sidebar for +149. The
other ~2,200 are somewhere else in the frame entirely, with the entity count
flat (452 → 481). It is a tenth of the way outside the gate's range and it is
not this theme's, so it is a number left here rather than a fix: whoever
chases it should start with a per-system `AllocScope`, which is now cheap to
add.

---

## Footguns hit while doing this

### A failure that is not yours costs a worktree to prove, and I got it wrong first

`tests/ui/select_word_and_line.e2e` failed on this branch. The box's load
average was 123 — another agent had leaked several dozen runaway processes —
and the runner budgets every assertion's retries in a field called
`wait_seconds`, so I wrote it up as the suite failing correct scripts under
load, and filed a gap saying so.

Then I built the merge-base in a second worktree and ran the whole suite on
both, on a quiet box (load 6.6):

```
  base   (main @ ef29c1a)   86 passed, 1 failed — select_word_and_line
  branch                    88 passed, 1 failed — select_word_and_line
```

Identical. It is simply broken on main, the way `tracker_links.e2e` already
is, and the load had nothing to do with it. Gap **#223** is rewritten around
what is actually there: `wait_seconds` is decremented by whatever `dt` the
host passes, hanabi passes a fixed 1/60 so the budgets are frames wearing a
seconds-shaped name, and a host that passed real elapsed time would get the
suite I wrongly described. A latent trap, not an active one.

The lesson is the cheap half: **before believing a UI-suite failure is yours,
build the merge-base in a second worktree and run it there.** It costs a
submodule checkout and one compile, and it is the difference between a
sentence that is true and a sentence that is plausible. Two of the three
things I concluded from a loaded box in this branch turned out to be wrong;
this one and the interleaving in section 6 are both here because of it.

(The genuinely load-sensitive part is `run_ui_tests.sh`'s own `TIMEOUT=60`
seconds of wall clock, which kills a script outright with rc 124. That is what
took out an unrelated script during the spike, and it says rc=124 rather than
`Text not found`, so it does not lie about why.)

### `scripts/run_ui_tests.sh <one-script>` runs the whole suite

The argument is accepted and ignored — every run is all 89 scripts, about two
and a half minutes. Nothing warns. Four separate times I read "88 passed, 1
failed" and had to look twice to see whether the one I was iterating on was in
it. Grep for your script's name, not for `passed`.

### `git stash` is shared across every worktree

Already in `SCROLL.md`, still true, and now also true that there are ninety
worktrees on this machine. Not used here. To compare against another revision,
`git worktree add /tmp/<name> --detach <rev>` and build there — which is also
the only honest way to get a "before" number, since interleaving two binaries
is the only comparison this box supports (`scripts/perf_ab.sh`).

### An arm that runs is not an arm that measures — the same trap, one level up

I ran the new `digest` stress arm against the base binary to get a "before"
for the scrolled case. The base binary does not KNOW that arm: an unrecognised
`HANABI_STRESS` value falls to `Scenario::None`, silently, so the base sat on
Home and reported 452 entities against the branch's 522 — the branch looking
*worse*, on the branch's own headline metric.

`SCROLL.md` §1 records this trap for a scenario that drove the wrong widget.
This is the same trap for a scenario that does not exist yet, and it is the
reason the level comparison in this file uses `HANABI_VIEW=blocked
HANABI_STRESS=idle`, which both binaries understand identically.

### `kFixtureEnv` is a list you must remember to join

The mock caches its generated catalog and keys the cache on a hardcoded list
of environment variables. Anything `build_seed()` reads must be added to
`kFixtureEnv` or the cache serves a catalog built for different settings, with
no error. The file says so in a comment; it is worth saying again here,
because the failure is a screen that is simply wrong and nothing points at the
cache.

---

## How to measure this

```bash
# The four screens, both arms. ~9 s, in `make test`.
make digest-gate

# One screen, cards built against sessions matched.
HANABI_BACKEND=mock HANABI_VIEW=blocked HANABI_STRESS_SESSIONS=2000 \
  HANABI_FRAME_TIMING=120 output/hanabi.exe --screenshot /tmp/o.png |
  grep -E 'FrameTiming|DigestCards'

# The same thing on screen, for a scripted test to assert.
HANABI_CARD_AUDIT=1 ...      # "digest cards 16 of 81 @ 0"

# Starred and Archived have nothing in them without these.
HANABI_STRESS_PINNED=10 HANABI_STRESS_ARCHIVED=10 ...

# The scrolled arm: Blocked, swept end to end at 96 px a frame.
HANABI_BACKEND=mock HANABI_STRESS=digest HANABI_STRESS_SESSIONS=2000 \
  HANABI_SOAK=1800 HANABI_SOAK_EVERY=600 HANABI_SOAK_CENSUS=1 HANABI_PROF=1 \
  output/hanabi.exe --screenshot /tmp/o.png | grep -E '\[soak\]|\[prof\]'

# A "before": build the merge-base in its own worktree and INTERLEAVE.
# Never two blocks. And use HANABI_VIEW + HANABI_STRESS=idle, because the
# older binary does not know the newer arms and will not say so.
git worktree add /tmp/base --detach <rev> && cd /tmp/base \
  && git submodule update --init vendor/afterhours && make -j8
```

Read the allocation count first, the entity count second, and the frame time
last. The first two are exact and the third is a shared laptop.

---

## 5. Home, the screen this file left capped

**What was still true after everything above.** The four digest screens became
windowed and Home did not. Home kept the `perf/sidebar-scaling` answer — a CAP,
`kMaxSection = 20`, applied to each of its four sections — and a cap is a
different thing from a window: it bounds the column at eighty cards and then
builds all eighty, every frame, whatever the viewport holds. `render_digest`'s
own preamble says the quiet part: *"eight times what Home builds beside it, and
Home is capped."* Sixty of those eighty cards were off screen.

Measured 2026-09-02 on `boulder-KF74T3NW36`, against `main` at `14312fe`,
1180×949, mock backend, 600-frame headless runs. `cache.cardtitle_hit` is one
call per built card, so it is the card count directly:

| | before | after | |
| --- | ---: | ---: | --- |
| digest cards built / frame (2000 sessions, Home) | 75.7 | **16.8** | 4.5× |
| `DigestCards: built` of `matched` (2000 sessions) | 63 of 63 | **11 of 63** | |
| Home frame (CPU) | 1.2445 ms | **0.9576 ms** | −23% |

And the allocation gate's five arms — the three that open Home moved, the two
that do not are unchanged to the allocation:

| arm | 14312fe | this branch | | ceiling |
| --- | ---: | ---: | ---: | ---: |
| home20 | 740.0 | **556.0** | −24.9% | 670 |
| home2000 | 1034.0 | **608.0** | −41.2% | 730 |
| tabs20 | 640.0 | 640.0 | — | 770 |
| thread480 | 2599.0 | 2599.0 | — | 3120 |
| draft6 | 955.0 | **766.0** | −19.8% | 920 |

Every figure reproduced to the unit across three repetitions of the whole
gate. There is no spread to report because there is none: this is a count of
`operator new` calls, and `docs/perf/ALLOCATIONS.md` explains why that is the
instrument on a shared box.

### It is the same window, not a second one

Home is a column of four sections with a header between them, so the one thing
it needs that `render_digest` did not is a section's own origin. That is
`digest::section_window`, which is `card_window` with the viewport shifted into
the section's coordinates and nothing else — one line, so the arithmetic that
decides what Blocked builds is the arithmetic that decides what Home builds.

`tests/unit/test_digest_layout.cpp` pins the equivalence rather than trusting
it: `test_section_windows_agree_with_one_whole_column` drives a four-section
column at sixty scroll offsets, with and without a pending ease, and asserts
that windowing section by section builds **exactly** the card indices one
window over the concatenated column would, and that the spacers plus the built
cards still sum to the column's real height. Drop the `sectionY` shift and it
fails 40 assertions.

The rest follows `render_digest` exactly: the cursor walk visits every row
built or not (so arrowing off the window scrolls to a card the next frame
builds), `digest_card` is told `trackCursor=false` so it cannot count a row
twice, and card ids are keyed on the window SLOT rather than the row index —
gap #115, nothing retires a widget, so index keys would mint four entities per
card ever scrolled past.

The cap stays. It bounds `matched`, which is why Home's arm in
`scripts/digest_gate.sh` has a matched floor of 60 where the digest views have
100. Removing the cap is still the product decision it always was; this change
is only about not building what is not on screen.

### The gate

Home is the fifth arm of `make digest-gate`, reading the same
`DigestCards: built=… matched=…` line the other four do. Rehearsed by real
revert, not by a lowered threshold — `scripts/gate_audit.py digest.home_window`
un-windows `render_home` and the arm reads:

```
  home             63       63       431       428    0.99x
  FAIL: 'home' built 63 cards of 63, over 40.
```

431 widgets against 217, on the same catalog. `scripts/gate_audit.py
alloc.home_window` turns the allocation gate red on the same defect —
`home20 740.0 / 670 = 110%`, `home2000 1034.0 / 730 = 142%`,
`draft6 955.0 / 920 = 104%` — which is the previous behaviour reproducing its
own baseline to the allocation.

### How to measure this one

```bash
make digest-gate                     # five arms now, Home is the fifth
make alloc-gate                      # the three Home arms moved

HANABI_BACKEND=mock HANABI_VIEW=home HANABI_STRESS_SESSIONS=2000 \
  HANABI_STRESS_PINNED=10 HANABI_STRESS_ARCHIVED=10 HANABI_FRAME_TIMING=60 \
  output/hanabi.exe --screenshot /tmp/o.png | grep -E 'FrameTiming|DigestCards'

# cards built per frame, and the Home frame's CPU
HANABI_BACKEND=mock HANABI_PROF=1 HANABI_SOAK=600 HANABI_SOAK_EVERY=600 \
  HANABI_STRESS=idle HANABI_STRESS_SESSIONS=2000 \
  output/hanabi.exe --screenshot /tmp/o.png |
  grep -E 'cardtitle_hit|FRAME \(cpu\)|ALLOCATIONS'
```
