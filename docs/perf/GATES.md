# Performance gates

**Why this file exists.** The app shipped a leak that grew it ~9 MB a minute
until it froze, and every gate in the project was green while it did. Not
because the gates were wrong — because all three of them measure a *young idle
app*, and a leak is a young idle app looking fine:

- `make test` renders **45 frames** and asserts on the 45th.
- `scripts/measure_launch.sh` gates the **first frame** and the peak RSS of a
  process **under a second old**.
- `make perf` times **thread switches in-process**, with no frame loop at all.

Nothing measured the *slope*. This file describes the gates that do, what set
each threshold, and — the part that matters when one goes red at 2am — how to
reproduce a failure and how to break each one on purpose to check it still
bites.

Everything below was measured on 2026-08-25 against `main` at `3bb921d`, on
`gabeochoa-mac-GRQ7Y259H4`, a machine shared with three other agents running
builds. Load averages during the samples ranged from 10 to 34. That is not an
apology for the numbers; it is the reason several of them are ratios.

---

## The gates, and what each costs

| gate | command | in `make test`? | wall clock |
| --- | --- | --- | --- |
| soak | `make soak-gate` | yes | ~3 s |
| allocation | `make alloc-gate` | yes | ~20 s |
| catalog scaling | `make scaling-gate` | yes | ~9 s |
| scroll | `make scroll-gate` | yes | ~6 s |
| source checks | `make source-checks` | yes | <1 s |
| screenshot subset | `make validate-screenshots-fast` | yes | ~11 s |
| screenshot baselines | `make validate-screenshots` | **no** — `make gate` | ~50 s |
| long soak | `make soak` | **no** — before a release | ~53 s |
| soak report | `make soak-report` | **no** | ~60 s |
| widget retirement | `make retire-gate` | yes | ~3 s |
| event transcript | `make events-gate` | yes | ~8 s |
| transcript slope | `scripts/perf_transcript_slope.sh` | yes | ~10 s |
| text measurement | `scripts/perf_text_gate.sh` | yes | ~14 s |
| find level | `scripts/find_gate.sh` | yes | ~12 s |
| launch / RSS | `scripts/measure_launch.sh` | yes | ~4 s |

This table was two tables run together for a while: the note below interrupted
the rows and the rest were appended after it, so `retire-gate`, `soak-report`
and the three script gates each appeared once, twice or not at all depending
where the reader stopped. One table now.

> **Every wall clock in this file used to be a lie in any pipeline.** Two
> gates bounded their run with `( sleep "$RUN_TIMEOUT"; kill … ) &`, and
> `kill "$WATCH_PID"` kills the subshell rather than the `sleep` it is blocked
> in — so the sleep was reparented, kept the script's stdout open, and any
> reader of that stdout (`make`, `tee`, a CI capture) blocked for the whole
> timeout. `make soak-gate` cost **121 seconds instead of 4**, to the second,
> on every run. Invisible on a terminal, which is why it survived.
> `scripts/watchdog.sh` fixes it; `scripts/check_watchdogs.py` keeps it fixed;
> `make source-checks` runs that. The numbers above are piped numbers.

The additions cost about **twenty seconds** on a `make test` that runs
| widget retirement | `make retire-gate` | yes | ~3 s |

| digest screens | `make digest-gate` | yes | ~9 s |
| autorelease source check | `make source-checks` | yes | <1 s |
| long soak | `make soak` | **no** — before a release | ~33 s |


The five additions cost about **twenty-three seconds** on a `make test` that runs
between four and six minutes depending on what else this box is doing
(observed: 228 s, 283 s, 336 s for the same tree). That was the budget: a suite
that takes fifteen minutes is a suite people stop running, and a gate nobody
runs is worth exactly as much as a gate that cannot fail. Anything longer went
behind `make soak`.

`make digest-gate` was added later, by `perf/digest`, and the reason it exists
is worth reading before adding a sixth: **the three gates above it were all
green over a Blocked screen building 506 cards a frame, for as long as that
screen had existed.** Not because any of them was wrong — because
`scaling-gate` opens one screen and never navigates, `scroll-gate` drives a
different pane, and `soak-gate` measures a slope where this was a plateau.
Every gate is blind to whatever it does not open, and the blindness is
invisible from a green board. `docs/perf/DIGEST.md`.

`make events-gate` was added later still, by `perf/post-merge`, and its reason
is the sharpest version of the one above: **six feature branches landed a whole
new event model on the transcript and every count in this file read IDENTICAL
to the unit across the merge** — 810.0 / 1162.0 / 2740.0 allocations a frame
before and after, 327/453 entities before and after, 13/506 digest cards before
and after. Not because the event model is free. Because **no fixture in this
repo could produce one of its row kinds**: the synthetic catalog leaves
`Message::kind` at its `Text` default and the long-transcript fixture emits
User / Assistant / Tool / Tool. The gates were reading a transcript with none
of the new rows in it and correctly reporting no change.

Its FIRST arm is therefore not a cost at all — it asks the app, through
`HANABI_PROF=1` gauges, how many event / delivery / spawn / thinking items the
transcript actually built, and fails at zero before printing anything else. A
gate whose fixture produces none of the thing it measures cannot fail, and all
four such gates section 0 found were found by luck. `docs/perf/EVENTS.md`.

They sit alongside `scripts/perf_transcript_slope.sh`, the transcript agent's
gate on per-message allocation slope, which `make test` also runs. That one
gates the cost of a *longer thread*; these gate the cost of a *longer run* and
of a *bigger catalog*. Three different axes, three different gates, and none of
them would have caught the other two's bug.

---

---

## 0. The audit — every gate, the defect it claims to catch, and what it did

**Why this section is first.** Two gates in this suite were found this month
to have stopped asserting anything, and both were found by luck.
`perf_transcript_slope.sh` went permanently green when a fix rerouted its work
to a new function. `scaling_gate.sh` asserted nothing at all for a while after
sidebar virtualization landed. A gate that cannot fail is worse than no gate:
it occupies the slot, it costs the wall clock, and it is read as evidence.

So every gate and every perf script in `scripts/` was taken in turn, the
defect it names in its own failure message was constructed, and the gate was
run against it. Everything below is an observed reading, not an argument.
The injections are one-line patches; each row names the patch precisely enough
to reproduce it.

Measured 2026-08-25 on `gabeochoa-mac-GRQ7Y259H4`, branch `perf/flake`.

**It is a program, not an afternoon.** `scripts/gate_audit.py` holds every
defect below as a one-line patch plus the gate it should turn red; `make
gate-audit` runs the lot (~25 minutes, one rebuild per defect), and
`make gate-audit DEFECT=scroll.blocks` runs one. Regenerate this table from it
whenever a threshold moves or a gate is added. An anchor that stops matching is
itself the signal: it means the code a gate is watching has moved and nobody
told the gate.

### The table

| gate · arm | defect injected | observed |
| --- | --- | --- |
| **measure_launch** · FirstFrame | 400 ms of sleep once, on the first frame | `FirstFrame: 470 ms` — `FAIL: FirstFrame 470 ms >= 250 ms` |
| **measure_launch** · peak RSS | retain 400 × 1 MB strings at startup | `Peak RSS: 457 MB` — `FAIL: peak RSS 457 MB >= 250 MB` |
| **soak_gate** · RSS | retain one 512-byte string per frame | `RSS +661.3 KB /1000f  budget 512  FAIL 1.3x over` |
| **soak_gate** · heap bytes | same | `heap bytes +657.5 KB  budget 256  FAIL 2.6x over` |
| **soak_gate** · heap blocks | `new char[24]` × 4 per frame, retained | `heap blocks +3985.6  budget 100  FAIL` — and at the budget of 1000 it was set at, a 512-byte-per-frame leak read exactly `+1000.0  ok` |
| **soak_gate** · entities | sidebar folder base id keyed on `epoch / 25`, so the set grows without bound, `HANABI_RETIRE=0` | `entities +2680.0  budget 25  FAIL 107.2x over budget` |
| **soak_gate** · cpu time | a per-frame walk over a vector that grows by one each frame | `cpu time +3.5 ms  budget 1.0  FAIL 3.5x over` (2.4 → 7.7 ms/frame across the run) |
| **scaling_gate** · widgets | the sidebar's cap AND its row window both removed | `widgets 373 → 6618 = 17.74x  budget 1.50x  FAIL` |
| **scaling_gate** · frame time | same | `min ms/f 1.44 → 16.55 = 11.49x  budget 2.50x  FAIL` |
| **scroll_gate** · level (entities) | `row_window()` returns the whole list | `entities, list expanded 381 → 6626 = 17.39x  budget 1.60x  FAIL` |
| **scroll_gate** · trend, frame cpu | a per-frame walk over an index of every row visited, forty times a frame | `frame cpu, min-of-half 1.221x  budget 1.150x  FAIL` |
| **scroll_gate** · trend, blocks | row ids keyed on the row INDEX, not the window slot (#115) | `live blocks /1000f +276.9  budget 40  FAIL` (its level arm goes too: `2.94x`) |
| **retire_gate** · epoch | `begin_epoch()` removed from `WidgetRetireSystem::once` | `epoch 1 after 1200 frames  FAIL` |
| **retire_gate** · stale | `HANABI_RETIRE=0` | `stale widgets 774  budget 0  FAIL` |
| **retire_gate** · live/built | `HANABI_RETIRE=0` | `live / built 2.15x  ceiling 1.50x  FAIL` |
| **perf_text_gate** · measures/frame | the line-count memo's `find` forced to miss | `measures/frame 125.37 → 137.22  limit 20.0  FAIL` |
| **perf_text_gate** · line-count memo rate | same | `0 hit / 76.5 miss = 0.00%  limit 95.0  FAIL` |
| **perf_text_gate** · advance memo rate | `theme::text_px`'s memo `find` forced to miss | `0 hit / 21.7 miss = 0.00%  limit 95.0  FAIL` |
| **perf_text_gate** · memo bound | `kLineCountEntries` 512 → 4096 | `line-count memo bound 1208 entries peak  cap 512  FAIL` |
| **perf_transcript_slope** · allocations slope | the transcript render cache's `get` forced to miss | `allocations/frame 8636.7 → 24112.8  slope 36.8/msg  limit 12  FAIL` |
| **perf_transcript_slope** · render-cache rate | same | `0 hit / 342.8 miss = 0.0%  limit 95.0%  FAIL` (both sizes) |
| **perf_transcript_slope** · wrap calls, level | the line-count memo's `find` forced to miss | **added by this branch** — `wrap calls/frame 81.4  limit 5.0  FAIL` |
| **find_gate** · repeated-frame level | the whole-result memo hit forced off | `rows/frame 797.6 / 6099.8  limit 30  FAIL` at 480 / 3,672 messages |
| **soak_gate** · entity level | sidebar folder base id keyed on the epoch mod 400, so the set SATURATES, `HANABI_RETIRE=0` | **added by this branch** — `entity level 27001  ceiling 4000  FAIL`, with every slope row above it reading `ok` |
| **run_ui_tests** · a script's assertion | the tracker host check removed from `link_hotspot` | `[E2E ERROR] expect_no_text (line 18): 'Opened' IS visible but should not be` |
| **validate-screenshots-fast** · composer fill | the composer field given a `(57,57,68)` background (gap #262 in hanabi; vendor is read-only) | `6 of 8 FAIL, 1.1551%-2.4837% differs` — every screen with a composer in frame; `15_settings_dark` and `18_auth_dark` stay green |
| **validate-screenshots-fast** · focus ring | `theme.focus_ring_thickness` forced to 0 in `FocusVisibleSystem` (gap #263) | `28_composer_focus_dark FAIL 0.5289% differs`, and only it |
| **check_autorelease** | one `AutoreleaseFrame` use deleted from `src/main.cpp` | *(see below)* |
| **check_label_padding** | a label given a padding the baseline does not allow | *(see below)* |
| **check_watchdogs** | an unredirected backgrounded `sleep` added to a script | *(see below)* |
| **compare.py --selftest** | the exclusion arithmetic altered | *(see below)* |

One row is worth reading twice. The **entities** injection above was first
built with no bound at all — a fresh set of widgets every frame — and the run
grew to 108,875 entities and **151 ms a frame** by frame 1500 and was killed by
the gate's own 120-second watchdog. The gate then reported
`FAIL: the run ended before it reached a verdict (rc=137), twice` — which is
correct, and is NOT the entities arm firing. A defect large enough to time the
run out is invisible to every arm the run never reached. That is why the row
above uses `epoch / 25`: a defect has to be small enough for the gate to
survive measuring it, and "the gate went red" is only evidence when you check
WHICH row went red.

### The screenshot net, which was dark when this audit was written

The two `validate-screenshots-fast` rows are newer than the rest of the table
and they are here for the reason section 0 exists at all. When they were added,
`make validate-screenshots` was failing **30 of 30** and had been for 285
commits: the baselines were stale, and — separately — 17 of the harness's 35
states changed with the local hour, so the set stopped reproducing an hour or
two after it was cut. Meanwhile 32 unit tests, 105 scripted UI tests and every
gate above passed on a build with a visibly wrong composer.

Both halves of that composer regression are now defects in this harness. The
fix for *why nobody noticed* is not in this file: the eight-screen subset runs
inside `make test` now, because the full compare was a separate target and a
gate nobody runs is worth exactly as much as a gate that cannot fail.
`docs/screenshots/baselines/README.md`.

### The four gates that did NOT go red, and what was done about each

Every one of these is the same shape: **an arm that measures a SLOPE, against
a defect that has no slope.** It is the finding `docs/perf/SCROLL.md` section 4
made about the scroll bug, arriving three more times.

**1. `perf_transcript_slope` · wrap calls — could not fail, now can.**
The line-count memo was disabled completely. Wrap calls per frame went from
**0.2 to 76.5** at 60 messages and **1.6 to 81.4** at 480 — 380 times the work
the memo exists to remove — and the gate passed every arm:

```
  wrap calls/frame        76.5 -> 81.4   slope 0.012 calls/message  limit 0.05   ok
  transcript slope gate: PASS
```

Of course it passed. Wrap work with no memo is proportional to what is ON
SCREEN, and the same amount is on screen at 60 messages and at 480. The arm
gates the per-message slope, so the one regression that removes the memo
entirely is invisible to it — and that regression is the one the script's own
header is about ("1.58 calls per message before the render-cache fix").
`perf_text_gate.sh` does catch it (125 measures a frame against a limit of
20), so the defect was never unguarded; the arm named after it simply was not
the guard. **Fixed**: the arm now has a LEVEL beside its slope — wrap calls
per frame at the longer thread, ceiling 5.0 against a clean read of 1.6.

**2. `soak_gate` · entities — could not fail on a bounded explosion, now can.**
A widget minted per frame with retirement off, cycling over 400 ids so the set
SATURATES: 27,001 entities and **36 ms a frame**, from the first bucket to the
last. The gate:

```
  [soak]   entities            +0.0             +0.0                25   0.00  ok
  [soak] PASS: flat over the run.
```

Perfectly flat, and perfectly ruined. The soak gate measures slope by design
and this is its blind spot stated exactly: **a defect that costs from frame
one costs the same on frame two thousand.** Worse, nothing else in the suite
caught it either — `scaling_gate` compares two catalog sizes and this defect
is catalog-independent, so its ratio stays flat. Which means, before this
branch, **an idle app rendering at 27 fps passed every gate in `make test`.**
`measure_launch.sh` bounds the FIRST frame and nothing bounds the rest.
**Fixed**: soak_gate now has a level arm on the absolute entity count at its
own fixed catalog — a count, not a millisecond, so it is exact and the shared
box cannot move it. Clean reads 250; the ceiling is 4000; the defect reads
27,001.

**3. `scaling_gate` — bites, but not on either mechanism alone.**
The sidebar's row count is bounded twice over: `visible_limit` caps the list at
two viewports, and `row_window` builds only the slice on screen. Remove
EITHER and the gate stays green:

| what was removed | widgets @20 → @2000 | ratio | verdict |
| --- | --- | --- | --- |
| nothing | 320 → 426 | 1.33x | ok |
| `row_window` (virtualization) | 329 → 435 | 1.32x | **ok** |
| `visible_limit` (the cap) | 319 → 425 | 1.33x | **ok** |
| both | 373 → 6618 | 17.74x | FAIL |

Each mechanism alone holds the count flat, so the gate cannot tell you either
one is still there. That is not a broken gate — it is a gate whose subject is
"is the sidebar's row count bounded", and the answer is still correctly yes.
But it does not gate virtualization, and after virtualization landed it was
read as though it did. **The scroll gate's level arm is the one that gates
virtualization** (`row_window()` returning the whole list: 17.39x against
1.60x), and that division is now written down here and in both scripts. No
code change: adding a third gate for a property two others already cover would
be the redundancy this audit exists to find.

**4. `soak_gate` · heap blocks — the budget was 25x looser than it needed.**
Not a "cannot fail", a "barely can". The first RSS-leak injection above leaked
exactly 1000 blocks per 1000 frames, and the arm printed:

```
  [soak]   heap blocks      +1000.0          +3600.0              1000   1.00  ok
```

Over budget by nothing, on a `>` comparison, in a run whose RSS arm was
already red. The budget was set when the metric was
`malloc_zone_statistics().blocks_in_use`, which drifts by a thousand blocks on
a run that allocates nothing (`src/util/heap_walk.h`). The metric is exact
now, so the budget comes down — see the block-budget note in section 1.

### The source checks

| check | defect injected | observed |
| --- | --- | --- |
| `check_autorelease.py` | one `const hanabi::AutoreleaseFrame framePool;` deleted from `src/main.cpp` | `check_autorelease: FAIL` / `src/main.cpp:1047: graphics::begin_frame() with no autorelease pool in scope` |
| `check_label_padding.py` | `.with_padding(Padding{.left = pixels(6.f)})` added to a `with_label` element with no child div | `NEW  src/ecs/sidebar_system.h:294  sb_snippet_audit  (up to 6px)` / `1 NEW label(s) set horizontal padding that does nothing` |
| `check_watchdogs.py` | `( sleep 30; echo late ) &` appended to `scripts/soak_gate.sh` | `check_watchdogs: FAIL` / `scripts/soak_gate.sh:233: a backgrounded job that sleeps, with the caller's stdout` |
| `compare.py --selftest` | the exclusion dropped from the DENOMINATOR (`total = w * h`) | `FAIL  shared pct: got 1.0, want 1.0416666666666665` / `selftest: FAIL` |

One note on the last one, because it is the kind of thing this section exists
to stop: changing `TOL` from 12 to 200 does **not** fail the selftest, and that
is correct rather than a hole. The selftest declares its subject — "prove the
exclusion arithmetic" — and builds black-against-white frames on purpose so the
threshold is not part of what it is measuring. A check that fails for reasons
outside its stated subject is a check that gets waived. `TOL` is unguarded, and
saying so here is the honest version of "it did not go red".

### The instruments this audit added

Three env-gated diagnostics, all hard no-ops when unset, all of them the thing
that turned a guess into a reading:

| knob | prints | found |
| --- | --- | --- |
| `HANABI_SOAK_SIZES=1` | live blocks grouped by size class, from the same heap walk | that the block column was moving while the heap was not — the scroll gate's one-in-five red |
| `HANABI_LINK_AUDIT=1` | every rect the link hit test tested a point against, hits and misses | that `tracker_links_need_a_host` was clicking 348px from any link; that `mouse.pos` is NaN two thirds of the time (gap #230) |
| `HANABI_SELECT_AUDIT=1` | the element a press resolved to: rect, byte offset, click run, text | that one press on a line seam was two hits and a single click selected a word |
| `HANABI_DBG_SETTLE=1` | now also `frames=`, `content_ready_at=`, `settled_with_content=` | that the scripted settle is NOT the variable it looked like — 46 frames and content at frame 5, identical across sixteen runs at load 28 |

The pattern is worth naming. Each of these prints what the code DECIDED, next
to the input it decided from, at the moment it decided. None of them is a
metric. Every one of them ended an investigation that had been running on
plausible theories — the scroll step was blamed on "something filling late",
the UI flake on the settle, the selection on a stale coordinate — and in each
case the theory was wrong and one line of output said so.

---

## 0b. Reproducing under load, and the footgun in doing it

Several gates here are only interesting under contention, and this box already
carries other agents' builds. **Do not generate load with a job-control idiom
that relies on `jobs -p`.** In a non-interactive shell it does not return what
you expect, the kill silently matches nothing, and every spinner is reparented
to init and keeps burning a core forever. This session leaked **52** of them
across four measurement batches before anyone noticed; the box's load average
reached 134, and every reading taken in that window was against a machine
carrying an unknown number of runaway processes — including readings whose
whole point was the load level.

Capture the PIDs explicitly and trap:

```sh
LOADPIDS=""
for i in $(seq 1 16); do ( while :; do :; done ) & LOADPIDS="$LOADPIDS $!"; done
trap 'kill -9 $LOADPIDS 2>/dev/null' EXIT INT TERM
...the measurement...
kill -9 $LOADPIDS 2>/dev/null
```

The `trap` is the part that matters: it fires when the measurement dies, times
out, or is interrupted, which is exactly when the manual kill does not run.
Before finishing, confirm you left nothing:

```sh
ps -Ao pid,ppid,pcpu,args | awk '$2==1 && $3>10'
```

Anything of yours reparented to init (`ppid == 1`) burning CPU is a leak. On a
clean box that command prints only system daemons.

Two more rules from the same hour:

- **Re-take anything measured during a window you were not sure about.** A
  number taken under a load you mismeasured is not a number. The heap-walk
  diagnosis in `src/util/heap_walk.h` survived because it predates the first
  spinner; the audit runs above were re-taken.
- **An injection harness must rebuild after it restores.** The driver used for
  this audit patched a source, built, ran the gate and restored the source —
  and left the DEFECTIVE binary in `output/`. The next clean `soak_gate.sh` run
  read 6,883 entities and 8.7 ms a frame off it. `scripts/fresh.sh` exists for
  exactly this and its header tells the same story from an afternoon that cost
  two debugging sessions. It warns; it does not stop you.

### What is NOT a gate, and does not pretend to be

These are in `scripts/` and assert nothing. That is correct for all of them —
they are instruments, not verdicts — and the list is here so that "it did not
go red" is never mistaken for a finding:

`perf_ab.sh` (interleaved A/B of two binaries), `perf_curve.sh` (frame cost
against catalog size), `perf_text.sh` (text-measure counters), `soak_spread.sh`
(run-to-run spread of the soak's own numbers), `ceiling.py`, `probe.py`,
`downsample.py`, `inkdiff.py`, `gen_icons.py`, `compare_screenshots.py`,
`screens.sh`, `shoot_hanabi.sh`, `shoot_hanabi_02.sh`, `shoot_puffin.sh`,
`review_shots.sh`, `winlist.swift`, and the two libraries `fresh.sh` and
`watchdog.sh`.

`perf_curve.sh` exits 1 on a missing binary or missing output — a plumbing
error, not a verdict, and the same is true of every "could not measure" exit in
the gates above. Those paths are deliberate and are covered in each script:
a gate that reports PASS on no data is the worst failure mode in this file,
and every gate here distinguishes "measured and flat" from "measured nothing".

---

## 1. The soak gate — does the app grow while it sits still?

`scripts/soak_gate.sh`, driving `HANABI_SOAK` in `src/util/soak.h`.

It runs the real render loop headlessly for **2000 frames** against the
deterministic mock catalog, sampling every 250, and fits a **slope** across
every bucket past frame 500 — six points, fifteen pairwise slopes — after 120
unmeasured settle frames. Everything inside the warm-up is excluded: it carries
lazy-init that is not a leak.

**The verdict is a slope, not a subtraction.** It used to be one bucket minus
another, which carries the full noise of both its endpoints: a single bucket
that faulted in a page reports its whole spike as growth at the end of a run,
or as an improvement at the start. `src/util/trend.h` is Theil-Sen — the median
of all pairwise slopes — with a ~29% breakdown point, so a page-fault bucket
moves it by nothing. At two points it *is* the old two-point delta, and the
probe says `degraded` rather than pretending otherwise. Unit-tested in
`tests/unit/test_trend.cpp`, which is the first test this estimator has ever
had.

It also reports **`rising`**: the fraction of bucket pairs that went up. A leak
adds on every bucket and reads 1.00; noise is a coin and reads ~0.50. That
column is what tells a reader whether to believe the slope above it.

Six things are gated, all **per 1000 frames**:

| metric | budget / 1000 frames | why this one |
| --- | --- | --- |
| RSS | +512 KB | the reported symptom itself; also the noisiest, so the loosest |
| live malloc bytes | +256 KB | moves the instant something is not freed, where RSS lags by whole pages |
| **live malloc blocks** | **+1000** | one leaked block a frame. The sharpest of the malloc columns |
| **GPU bytes** | **+64 KB** | the malloc counters are blind to a texture; the tightest here, and it can be |
| entities | +25 | an ECS that is not being torn down |
| frame CPU | +1.0 ms | `CLOCK_THREAD_CPUTIME_ID`, not wall — see below |

**Live blocks is gated, and that is new.** It was report-only on the grounds
that it "sawtooths by thousands either way", which was true of a two-point
delta over 1000 frames and is not true of a median of fifteen slopes over 2000:
34 clean runs across three load levels read a worst sample of **+16.0**, against
**+9996** from the pool-less binary. It catches what the byte budgets cannot —
verified against a build retaining two ~40-byte strings a frame:

```
  RSS          +96.0 KB/1000f   ok
  heap bytes  +117.5 KB         ok
  heap blocks +1728.0           FAIL 1.7x
```

A leak of one 32-byte map node a frame is 32 KB of heap and zero KB of RSS per
1000 frames — inside both byte budgets, over this one. That is
`docs/perf/MEMORY.md` entry 1's five per-session maps, and nothing here could
see them before.

**Frame time is gated on the thread CPU clock.** Wall clock measures how much
of the machine the app was *given*; a regression is a change in how much work
it *does*. Ten clean runs at load average 27:

| metric | min | median | max | spread |
| --- | ---: | ---: | ---: | ---: |
| cpu ms / 1000f | -0.1 | +0.0 | +0.0 | **0.1** |
| wall ms / 1000f | -0.8 | -0.1 | +0.7 | **1.5** |

Fifteen times tighter, with no defect in either column. Wall is kept as
report-only: a run where wall climbs while CPU is flat is the app *waiting* on
something, which is a real and different finding.

**A run with too little data says so.** Fewer than two buckets past the
warm-up prints `INCONCLUSIVE` and returns 2, where the old code printed a
sentence and returned 0. Below six buckets it warns: measured on the `bigidle`
arm, at four fit points one clean run in six read +789 blocks per 1000 frames
against +45 for the other five; at ten points the worst of six was +13. The
robustness of a median is a function of how many points it has.

**Smaller buckets were tried and are worse**, which is the useful negative:
1000 frames in 100-frame buckets gives five fit points and a clean RSS spread
of 206.7 KB per 1000 frames, against 0.0 at 250-frame buckets. RSS noise is a
whole page arriving at once, so a shorter window multiplies it. On that metric
the way to more points is more frames.

**What set 64 KB for GPU bytes,** which is the tightest number in this gate by
two orders of magnitude. Three clean runs read `+0.0, +0.0, +0.0` KB — exactly
zero, three times, and not by luck. Nothing in a steady-state frame allocates
GPU memory: textures are made when an image, an icon atlas or a font atlas is
first needed and then held, the render target is made once, and the settle pass
has already paid all of it. Confirmed across four scenarios rather than one —
`threads`, `tabs`, `read` and `search`, 1500 frames each over a 2000-session
catalog, GPU 43,600 KB in every bucket of every arm, to the kilobyte. So unlike
every other column here this one has no noise for the budget to absorb, and
64 KB is not slack: it is one 128x128 RGBA texture, the smallest thing whose
appearance every frame would be a real defect.

The number comes from `-[MTLDevice currentAllocatedSize]`, read through the
device sokol already created (`src/gpu_mem.mm`), so it counts the font atlas,
the glyph textures, the offscreen render target and sokol's own buffers as well
as anything hanabi loaded. The gate is **skipped loudly** when no Metal device
answers — "not measured — NO DEVICE" rather than a column of +0.0 — because a
zero that means "no accounting" and a zero that means "nothing leaked" are the
same glyph, and reading the first as a pass is how a gate stops gating.

**What set the old 512 KB.**  Eight consecutive clean runs read
`+0, +0, +32, +32, +64, +96, +96, +192` KB of RSS growth per 1000 frames, and
`-19, -19, -1, +13, +13, +45, +61, +77` KB of heap. The same binary with the
autorelease pool removed read `+2784, +2816, +2848, +2912, +3040` KB of RSS and
`+2739 … +2915` KB of heap. 512 sits 2.7x above the worst clean sample and 5.4x
below the smallest defective one, with 15x of clear air between the two clouds.

The defect was reintroduced two ways, deliberately: once by compiling the pool
class out wholesale, and once by deleting a **single**
`const hanabi::AutoreleaseFrame framePool;` line from the soak loop — which is
the shape the regression will actually take, because that is what a refactor
does to a four-line RAII object with no callers. Both read the same.

**Why RSS is gated alongside heap and not instead of it.** RSS is the noisier
of the two by a long way. That `+192 KB` sample came with a heap delta of
*minus* 0.8 KB: pages faulted in by something other than the app's own
allocations. Heap bytes is the sharper instrument and RSS is the one that
matches the complaint, so both gate, and a failure gets one retry — a real leak
fails every attempt (three consecutive defective runs read 2848, 2912 and 3040)
so the retry cannot launder one.

**Why a shorter run was rejected.** 600 frames with 150-frame buckets puts the
window at frames 300-600, where the app has not finished settling: five clean
runs there read up to **+267 KB** per 1000, half the budget, with no defect at
all. 1000 frames costs about a second and a half more and moves the worst clean
sample from +267 to +96.

**Why frame time is loose here.** 3.0 ms per 1000 frames against clean samples
spanning -0.9 to +1.5. Frame time is the one metric in this gate that a busy
machine moves, and this box has three other agents on it. A gate that flakes
gets disabled and a disabled gate is worse than none. The long soak runs a
tighter frame-time budget because it has the resolution to mean it.

### Reproducing a failure

```bash
# watch it go red, then put it back
sed -i '' 's/#if defined(__APPLE__)$/#if 0/' src/util/autorelease.h
make -j8 && make soak-gate          # expect: FAIL, ~5.4x over budget
git checkout src/util/autorelease.h && make -j8
```

Deleting one `const hanabi::AutoreleaseFrame framePool;` line from the soak
loop in `src/main.cpp` does the same thing and is the more honest rehearsal.

The failure looks like this — the numbers are from the run that deleted one
line:

```
  [soak]   metric       per 1000 frames  per minute @60fps      budget  verdict
  [soak]   RSS              +2816.0 KB      +10137.6 KB            512  FAIL  5.5x over budget
  [soak]   heap bytes       +2739.2 KB       +9861.3 KB            512  FAIL  5.4x over budget
  [soak]   entities            +0.0             +0.0                25  ok
  [soak]   frame time          +0.3 ms          +1.0 ms              3  ok
  [soak]   heap blocks     +10004.0         +36014.4                 -  report-only
  [soak] ---------------- SOAK GATE: FAIL ----------------
  [soak] The app grew while it sat still. Nothing about this run
  [soak] asked it to: the catalog is fixed, the window never resizes,
  [soak] and the same frame is drawn over and over.
  [soak]
  [soak] Shape of it: 10.0 live blocks are added every frame and never
  [soak] freed, averaging 282 bytes each.
```

That last pair of lines is the whole point of the rewrite. "10.0 blocks a
frame, 282 bytes each" is a description of the bug: six autoreleased Metal
objects and a few small companions, per frame, forever. A 32-byte block would
be a map node; a megabyte one would be a buffer. The number tells you where to
look before you have run a single profiler.

### GPU: reproducing a failure, and the ceiling that makes it hard

One line in the soak loop, the shape a per-frame cache miss takes:

```cpp
{ TextureType leak = afterhours::load_texture("/some/file.png"); (void)leak; }
```

```
  [soak]   GPU bytes  +705800.0 KB   +2540880.0 KB   64   FAIL  11028.1x over budget
```

**But read this before trusting that gate.** A runaway texture leak does *not*
grow without bound. It grows to 264,048 KB and stops dead, flat to the kilobyte
for the remaining 800 frames, because sokol's image pool is a **fixed 128
entries** and every allocation past it fails. So a slope gate catches a texture
leak only during the ~124 frames it takes to exhaust the pool — and this gate's
own 120-frame settle pass is almost exactly long enough to hide the whole of
it. The rehearsal above had to run with `HANABI_STRESS_SETTLE=0` and 20-frame
buckets to see anything at all.

Past that ceiling the failure changes shape entirely: images stop loading and
the app draws nothing where they were. Worse, the **sampler** pool is 64 and
runs out first, and afterhours does not check it — between the 61st texture and
the 124th, `load_texture` returns a texture with valid dimensions, a valid
image and no sampler, which every "did it load?" test in this app reads as
success. `afterhours_gaps.md` #210; `docs/perf/MEMORY.md` entry 5c has the
measurement and the fix.

### Naming the leaking allocation

When the shape is not enough, the tool that found the original in twenty
minutes:

```bash
MallocStackLogging=1 HANABI_BACKEND=mock HANABI_SOAK=10000 \
    ./output/hanabi.exe --screenshot /tmp/x.png &
sleep 20 && leaks --groupByType $!    # or: heap $! -addresses all
```

The original read, over 10,248 frames:

```
10248 calls  9182208 bytes  -[AGXG16XFamilyCommandQueue commandBufferWithUnretainedReferences]
10248 calls  6558720 bytes  -[AGXG16XFamilyCommandBuffer initWithQueue:retainedReferences:]
10248 calls  3279360 bytes  +[MTLRenderPassDescriptor renderPassDescriptor]
```

Exactly one of each per frame is the signature of a missing pool. A count that
is *not* a clean multiple of the frame count is something else.

---

## 2. The catalog-scaling gate — does the frame cost more when the list is longer?

`scripts/scaling_gate.sh`.

Two headless runs of the same binary, identical but for
`HANABI_STRESS_SESSIONS` (20, the hand-written fixture, and 2000). It reports
and gates two **ratios**.

**Why a ratio and not a millisecond.** Measured on this box, the same binary's
median frame time at 2000 sessions read **8.27 ms on a quiet minute and 16.07
ms on a busy one**. Any absolute threshold between those two numbers is a coin
flip. A ratio between two sizes measured back-to-back on the same machine
divides the machine out: both halves are slowed by the same contention. The
brief asked for this and the box proved it within the hour.

The measurements the ceilings come from:

| sessions | widgets | min ms/frame (quiet) | min ms/frame (load ~20) |
| --- | --- | --- | --- |
| 20 | 348 | 1.41 | 1.40 |
| 100 | 454 | 1.65 | 1.65 |
| 500 | 986 | 2.83 | 4.22 |
| 2000 | 2985 | 7.86 | 11.32 |

**Widget ratio is the primary gate**, at `2985 / 348 = 8.58x`, and it is
gated at **10.00x**. It is the better of the two for a reason that has nothing
to do with taste: it is *deterministic*. Five consecutive runs gave 348 and
2985 widgets, exactly, every time — no spread at all, on a box under load 20.
It is also the property itself rather than a symptom of it: a widget per row
whether or not the row is on screen, torn down and re-laid-out every frame.

**Frame-time ratio is the backstop**, at `7.86 / 1.41 = 5.57x` quiet and
`11.32 / 1.40 = 8.09x` under load, gated loosely at **12.00x**. It uses the
*minimum* frame time over 120 frames — the cleanest frame, the one that got a
whole timeslice — best-of-two runs, because the minimum is the sample least
polluted by whatever else the box is doing.

### The ceilings, and the day they came down

This section used to read "the honest part: this gate asserts what is true
today", because frame time was linear in the catalog and a gate that is red on
`main` is a gate somebody deletes on a Tuesday. It also said what should happen
next: *"`perf/sidebar-scaling` should lower both numbers in the same commit
that makes them true ... the gate is worth very little at 10.00x."* That branch
landed and the constants did not move; `perf/scroll` moved them.

| | widgets | frame time |
| --- | ---: | ---: |
| ceiling, as written | 10.00x | 12.00x |
| measured now | 1.31x | 1.19x – 1.30x |
| ceiling now | **1.50x** | **2.50x** |

1.31x on every one of five runs — 338 and 444 widgets, exactly — and the frame
ratio measured against unmodified `main` back to back, which read 1.29x and
1.30x in the same minutes on the same box.

What makes it hold is not the cap. Before `perf/scroll` the widget count was
bounded because `fillCap` happened to be small; now it is bounded because the
sidebar builds a window (`docs/perf/SCROLL.md`). The difference shows in what
this gate no longer catches: **removing the sidebar's row cap alone moves
neither number**, which is correct rather than a hole — an uncapped list that
is windowed costs a window. The cost side of the sidebar has moved to
`make scroll-gate`.

### Reproducing a failure

Rehearsed against the new ceilings by breaking it on purpose, both arms firing
each time:

| defect | widgets | frame |
| --- | ---: | ---: |
| Home's per-section cap removed (`kMaxSection`) | 355 → 8934, 25.17x | 15.61x |
| the sidebar's row cap AND its window removed | 391 → 6636, 16.97x | 14.94x |

The original rehearsal below still describes the shape, but note that it no
longer fires on its own: with a window in place, uncapping the list changes
what the user is SHOWN and not what the frame builds.

In `src/ecs/sidebar_system.h`, replace

```cpp
const int limit = (expandedMore || total <= cap) ? total : cap;
```

with `const int limit = total;`, rebuild, and run `make scaling-gate`:

```
  sessions           20       2000      ratio
  widgets           392       9169     23.39x
  min ms/f         1.72      69.09     40.17x
  a 100x bigger catalog; budget 10.00x widgets, 12.00x frame time

  FAIL: widget ratio 23.39x exceeds 10.00x.
        A 100x catalog is building 23.39x the widgets, so the frame's
        work is growing with the catalog faster than it did when this
        ceiling was set (100x sessions -> 8.58x widgets, 2026-08-25).
        Look for a per-row widget added to the sidebar or the tab strip,
        or a row that stopped being culled.

  FAIL: frame-time ratio 40.17x exceeds 12.00x, with widgets at 23.39x.
        Time is growing faster than the widget count, so this is not
        just more rows: something is doing per-row work that is more
        than constant per row — a scan, a sort, or a lookup that walks
        the catalog.
```

Both arms fire, and 69 ms a frame at 2000 sessions is what "it freezes" looks
like from the outside.

---

## 3. The autorelease source check — the four lines that look like nothing

`scripts/check_autorelease.py`, run by `make source-checks`.

**Widened 2026-08-25 to cover texture loads, not just frames.** `sg_make_image`
builds an `MTLTextureDescriptor` and friends, so `load_texture` hands back
autoreleased objects the same way `begin_frame` does — measured at 113 bytes a
load (2000 load+unload pairs: 646 KB of live heap unpooled against 420 KB
pooled). Small per call and unbounded per process, which is the same shape as
the original bug four orders of magnitude down. The check found two sites the
first time it ran, and the rule it enforces is the point of them: both were
reached only from inside a frame loop and neither was leaking, until a pre-warm
started calling one of them before any frame existed. A function that touches
Metal owns its own pool, because it cannot know whose scope it will be called
in.

`hanabi::AutoreleaseFrame` has no callers, returns nothing, and reads as dead
code. It is the entire fix for the bug that started all of this. Someone
tidying `main.cpp` will delete it and the app will look completely fine for the
first thirty seconds.

The soak gate catches that in four seconds of runtime and is the real guard.
This is the cheap one that runs on the source, names the exact line, and — the
part the soak gate genuinely cannot do — sees the loops the soak gate never
executes. It found two of those on `main`, both fixed in this branch: the
`HANABI_FRAME_TIMING` diagnostic loop and the scripted-UI (`--e2e`) loop were
each rendering without a pool. The scripted suite runs 85 scripts through that
second one.

It checks two things:

1. Every `graphics::begin_frame()` under `src/` is inside a scope — its own or
   an enclosing one — that declares an `AutoreleaseFrame`. It tracks brace
   depth with strings and comments stripped, and refuses to report a verdict on
   a file whose braces do not balance rather than confidently reporting
   nonsense.
2. `src/util/autorelease.h` still *pushes and pops a real pool*, so stubbing
   the class out is caught as loudly as deleting its uses.

### Reproducing both failures

```bash
# (a) delete one use
#     src/main.cpp, remove a `const hanabi::AutoreleaseFrame framePool;`
python3 scripts/check_autorelease.py
```
```
check_autorelease: FAIL
  src/main.cpp:1185: graphics::begin_frame() with no autorelease pool in scope
      graphics::begin_frame();

  A frame loop that calls into Metal without a pool leaks the
  render pass's autoreleased objects — about 2.5 KB a frame,
  ~9 MB a minute at 60fps, never returned. Add, as the first
  line inside the loop body:

      const hanabi::AutoreleaseFrame framePool;
```
```bash
# (b) stub the type out
sed -i '' 's/#if defined(__APPLE__)$/#if 0/' src/util/autorelease.h
python3 scripts/check_autorelease.py
```
```
check_autorelease: FAIL
  src/util/autorelease.h: the Apple branch is no longer guarded by a bare
  `#if defined(__APPLE__)`. If a second condition was added, a build can now
  silently compile the no-op pool on a Mac
```

---

## 3b. The scroll gate — the list the report was about

`scripts/scroll_gate.sh`, and `docs/perf/SCROLL.md` is the whole story. Two
arms, ~6 s, in `make test`:

| arm | what it asks | budget | measured |
| --- | --- | ---: | ---: |
| level | entity count at 20 sessions vs 2000, list EXPANDED | 1.60x | 1.30x, zero spread |
| trend | min-of-half frame CPU, second half over first | 1.15x | 0.986 – 1.027 over 8 runs |
| trend | live malloc blocks per 1000 frames | +150 | −149 to 0 over the same 8 |

**Why the level arm exists, and why it runs first.** The defect this gate was
written for has no slope. Against the build with row virtualization reverted
the trend arm reads 17.040 ms then 17.326 ms — ratio 1.017, blocks +1.9 — and
passes cleanly. It is eleven times too expensive on the first frame and eleven
times too expensive on the six-thousandth. Every gate this project added after
the Metal leak measures a slope, because the leak was a slope; **"does it get
worse" and "is it bad" are different questions.**

All three arms have been made to fail on purpose:

| arm | defect | read |
| --- | --- | ---: |
| level | `row_window()` returns the whole list | 16.61x vs 1.60 |
| blocks | row ids keyed on the row index, not the window slot (#115's shape) | +180/1k vs 150 |
| frame cpu | a per-frame walk over an index of rows visited | 1.223x vs 1.15 |

The frame-CPU arm fires at about +0.47 ms of drift across the halves of a
1600-frame run.

**Minimum, not mean.** Contention and downclocking only ever *add* time to a
bucket — there is no mechanism by which a busy machine makes a frame cheaper —
so the cheapest bucket of a half is the least-polluted estimate of what the app
cost over that half, and a ratio of two of them removes most of what is left.
`scripts/perf_ab.sh` makes the same argument.

**And the settle pass had to be fixed before any of this could be measured.**
The soak's settle loop rendered without calling the stress driver, so it
settled the launch burst and nothing else: every scenario's warm-up landed
inside the measured window. On the scroll arm that is a title memo filling with
1800 entries while the first buckets are sampled. Eight clean runs spanned
−148 to +847 blocks per 1000 frames with an undriven settle and −149 to 0 with
a driven one, which is the difference between having a budget and not.

---

## 3c. The widget-retirement gate — does the app hold screens it stopped drawing?

`scripts/retire_gate.sh`, added with the fix for `afterhours_gaps.md` #115.
Full write-up in `docs/perf/RETIRE.md`.

afterhours never retires a widget that stops being built, so before the fix the
app walked the union of every screen it had ever shown: at a 2000-session
catalog, 2844 entities and 4.60 ms/frame for screens nobody was looking at,
against 200 and 3.16 after.

**Neither gate above can see that**, and the two reasons are the reason this
one exists:

- **The soak gate measures a SLOPE. This is a PLATEAU.** With the sweep off,
  the entity count over three navigation cycles reads 1020, 1247, 1270 —
  rising to a high-water mark and stopping, which a slope gate correctly and
  uselessly reads as an app that settled.
- **The scaling gate measures one screen at two catalog sizes and never
  navigates.** 1.31x widgets with the sweep off, 1.32x with it on.
- **The scroll gate expands one list and sweeps it.** It never leaves the
  screen either, so the widgets of the screen it is not on are not its
  question.

So this one navigates — one run of the `views` arm, 500 sessions, 1200 frames
— and then reports two COUNTS off the soak census. No milliseconds: an entity
count is exact and identical run to run, which an ms figure on this box is not.

| metric | budget | measured | with the sweep off |
| --- | --- | --- | --- |
| stale widgets | 0 | 0 | 1083 |
| live / built | 1.50x | 1.06x | 7.87x |
| epoch | >= frames | 1324 | — |

"Stale" is widgets `imm::mk()` still owns that nothing has built for longer
than the grace. The run turns the grace down to 2 frames and sweeps every frame
(the shipping defaults are 90 and 15), so anything stale is something the sweep
FAILED to take rather than something it has not got to yet.

### The epoch row is not a perf number

It is the guard against this gate's own blind spot, and it is there because the
rehearsal found the hole. The likeliest regression is not a broken sweep, it is
someone deleting one line from `build_systems()`. With the system unregistered:

```
  live widgets           1260
  built / frame          1251
  stale widgets             0          0
  live / built          1.01x      1.50x
```

Green, on a completely broken fix. With no system the epoch never advances,
every widget's stamp reads as current, "stale" is 0 for the best possible
reason and the worst possible cause, and `built` accumulates every `mk()` call
of the whole run so the ratio collapses to 1. The census prints the epoch and
the gate fails when it is below the frame count:

```
  FAIL: the widget epoch is 1 after 1080 frames.
        The epoch advances once per frame in ecs::WidgetRetireSystem.
```

The two scripted tests (`tests/ui/widgets_of_a_screen_you_left_are_retired.e2e`
and its `HANABI_RETIRE=0` twin) have the same blind spot; this covers it.

### Reproducing a failure

```bash
HANABI_RETIRE=0 bash scripts/retire_gate.sh    # both arms fire
```

and the honest version of the same thing — comment out the
`ecs::WidgetRetireSystem` registration in `src/main.cpp`, rebuild, and watch the
epoch row catch what the other two rows cannot.

---

## 4. `make soak` — the long form, for before a release

**Thirteen arms, four at a time, 53 seconds.** `scripts/soak.sh`. Parallel
because every gated metric is now load-insensitive — counts of things, plus a
frame budget on the thread CPU clock, so a sibling arm descheduling an arm
costs it nothing. `make soak JOBS=1` runs it serially (84 s) and produces the
same verdicts and a byte-identical report; that is how the claim was checked.

`open` and `mixed` accumulate tabs on purpose, so they run `REPORTED` rather
than gated: growth there is a cost per tab, and gating them flat would assert
that opening a hundred tabs is free. An arm that DROVE NOTHING is its own
outcome alongside PASS / FAIL / INCOMPLETE.

`make soak-report` reduces the same run to a diffable text artifact and diffs
it against `docs/perf/soak-baseline.txt`. See `docs/perf/STRESS.md`.

### The old five-arm form

`scripts/soak.sh`. Seven arms, 4000 frames each, about 65 seconds total.

| arm | what it drives | why it is here |
| --- | --- | --- |
| `idle` | nothing | the control. Growth here is growth for no reason at all, the strongest possible finding |
| `scroll` | sidebar wheel, 60 frames down and 60 up | the reported symptom was "scroll the sidebar until it breaks" |
| `threads` | opens a thread every 30 frames | the heaviest thing the app does: fetch, transcript rebuild, tab |
| `tabs` | 8 tabs, then round-robin | anything the tab strip or a per-tab cache holds on to |
| `scrollall` | the sidebar's list EXPANDED, then swept, at 2000 sessions | the arm above scrolls the list the cap allows; this one scrolls the list the user asked for, which is the one in the report |
| `views` | Home / Blocked / Review / Starred / Archived / a thread, on a cycle | the only arm that CHANGES SCREEN. Every other one sits on a single screen for its whole run, which is why #115 lived here for a month |
| `bigidle` | idle, against a 2000-session catalog | a per-row leak is 100x more visible; a catalog-sized cache shows as a higher plateau rather than a slope |

Memory budgets are **tighter** than the short gate's — 256 KB per 1000 frames
against clean arms reading `+5.3, +10.7, +32.0, +53.3, +64.0` KB of RSS — because
over 4000 frames the settling and the page quantisation that dominate a
1000-frame window have amortised away.

```
=== soak summary (per 1000 frames) ===
  idle      PASS  RSS +0.0 KB      heap +1.7 KB         5s
  scroll    PASS  RSS +26.7 KB     heap +0.5 KB         5s
  scrollall PASS  RSS +0.0 KB      heap +0.1 KB         8s
  threads   PASS  RSS +0.0 KB      heap -1.4 KB         5s
  tabs      PASS  RSS +0.0 KB      heap +0.2 KB         4s
  bigidle   PASS  RSS -5.3 KB      heap +0.1 KB         6s
```

Six arms in 33 s, against five in 65 s before `perf/scroll`. The arms got
faster because the app did — `bigidle` alone went from 41 s to 6 s.

Overrides: `make soak FRAMES=20000`, `make soak ARMS="scroll tabs"`.

**`views` is sampled every 360 frames, not 500 like the others**, and the
reason generalises. Its entity count is a function of which screen the sample
landed on, and 500 is not a whole number of its 360-frame navigation cycle — so
consecutive buckets compared Home against a thread and the arm FAILED at +31
entities per 1000 frames on a run whose memory was flat to the kilobyte. Median
of three buckets does not help; the three buckets are three different screens.
Phase-locked, the identical run reads +6.9. A measurement of a moving app has
to be sampled in whole periods of the motion, or it measures the sampling.

### A gate that reports nothing is not a gate that failed

All three runtime gates distinguish three outcomes, not two: passed, failed,
and **did not finish**. This matters here specifically. `scripts/review_shots.sh`
kills `output/hanabi.exe` in *every* worktree it can find, not just its own,
and there are eighty worktrees on this machine — so a soak arm can be shot in
the head mid-run by an agent doing something unrelated in another branch. That
happened once while this branch was being written: the `bigidle` arm died at
frame 3500 of 4000 and the first draft of the summary reported it as

```
  bigidle   FAIL  RSS ?            heap ?              43s
```

which reads exactly like a leak and is not one. It now reports

```
  bigidle   INCOMPLETE RSS not measured   heap not measured     43s
  AN ARM DID NOT FINISH — that is a killed or crashed process, not a
  measurement. Re-run it on its own before believing anything here.
```

The same distinction is in `soak_gate.sh` (a run that produced buckets but
never reached a verdict, twice) and in `scaling_gate.sh` (no `FrameTiming`
line at all). Re-running the arm on its own is the first move, every time.

---

## 5. The allocation gate — how many times does one frame call malloc?

`scripts/alloc_gate.sh`, reading the `allocs/frame` line `src/util/soak.h`
prints under `HANABI_PROF=1`.

**Why it is not one of the gates above.** Every other runtime gate in this file
measures a SLOPE. A frame that allocates four thousand times and frees all four
thousand is flat on all of them — flat RSS, flat live blocks, flat entity
count — and it is the reported symptom just as surely as a leak is. When this
gate was written the app allocated 3,535 times per frame standing still on the
Home view with a 2000-session catalog, and the soak gate read −0.6 KB of heap
growth per thousand frames. Both numbers were correct.

**Why an absolute ceiling, here, on this box.** Because the number is
DETERMINISTIC. Not low-variance: identical, to within one allocation, on every
run and in every bucket of a run, while the same binary's wall-clock frame time
moved between 1.6 ms and 6.5 ms with the machine's load during the same
measurements. Everything in this file that is expressed as a ratio is a
workaround for a measurement that moves; this one does not move.

| arm | main @ ddb391c | after | ceiling |
| --- | ---: | ---: | ---: |
| `home20` — the 20-session fixture, Home | 2,550.0 | 827.0 | 1,000 |
| `home2000` — Home over 2000 sessions | 3,535.0 | 1,197.0 | 1,450 |
| `thread480` — a 480-message thread over 2000 | 6,687.0 | 2,740.0 | 3,300 |

~20% of headroom on each. Not for noise — there is none. For a feature that
legitimately adds a widget or a label, which should cost a few allocations and
not five hundred.

Text measurement is deliberately NOT gated here even though it is a large share
of the total: `scripts/perf_text_gate.sh` gates it directly, and two gates on
one property is one gate nobody maintains.

### Reproducing a failure

Two rehearsals, both real reverts rather than a lowered ceiling.

```bash
# (a) the whole branch: build at the commit that adds only the instrument
#     home20 2550 (255%), home2000 3535 (244%), thread480 6681 (202%) — all FAIL

# (b) ONE LINE, which is the shape the regression will really take.
#     src/ui/widget_epoch.h, point the app's mk wrapper back at the library's:
sed -i '' 's|hanabi::ui::mk(parent, otherID, location);|afterhours::ui::imm::mk(parent, otherID, location);|' \
    src/ui/widget_epoch.h
make -j8 && make alloc-gate
```

(b) compiles, renders identically, changes no pixel, and reads:

```
  arm             allocs/f    ceiling  of ceil   verdict
  home20            2466.0       1000     247%   FAIL
  home2000          3361.0       1450     232%   FAIL
  thread480         5803.0       3300     176%   FAIL
```

A one-line change inside a wrapper whose stated job is something else entirely,
costing two thousand mallocs a frame forever. That is what this gate is for.

The failure text names the four causes this project has actually had and the
command that lists the call sites; `docs/perf/ALLOCATIONS.md` is the full map.

---

## Where the stress harness is, and what it adds

`docs/perf/STRESS.md` is the other half of this file: the thirteen scenarios,
`HANABI_STRESS_UNTIL` (run until a failure condition and report where it
broke — the app manages **245 open threads** before a frame costs 3x what it
did at rest), the diffable report, and the footguns found building it.

Three of its findings belong here because they change what the gates above
mean:

- **`tabs` has never opened more than one tab.** `requestOpenTab` opens a
  PREVIEW, which reuses one slot, so the arm this file described as catching
  "anything the tab strip or a per-tab cache holds on to" was re-pointing a
  single tab. `open` is the arm that grows the strip; every scenario now
  reports what it drove, and a scenario that drove nothing fails.

- **The synthetic stress catalog rendered the cheapest path this app has.**
  `HANABI_STRESS_SESSIONS` generated one user line and one paragraph per turn
  — no tool rows, no thinking rows, no code fences, no failed runs. So every
  big-catalog number ever taken scaled up the part of a transcript that costs
  nothing. Fixed, at +14% a frame for 2.1x the rows.

- **A slope cannot see a defect that was always there.** Every arm is read
  three ways now — a slope (a leak), a ratio between the halves' minima (a
  cost that arrived and stayed), and the report's exact counts (a level, with
  no threshold at all). Pair every trend arm with a level arm.

---

## What could NOT be gated, and why

This is the most useful section in the file, because it is a map of where the
next one gets in.

### Frame-time drift over a long run is unmeasurable on this machine

The `bigidle` arm pegs a core for 40 seconds, and on a shared laptop that is
long enough to thermally throttle. On a clean tree it read **+1.4 ms per 1000
frames over 3000 frames and +0.4 ms over 7000** — a real algorithmic slope does
not shrink as the window grows — and the per-bucket times wandered
non-monotonically (8.6, 8.3, 8.5, 9.2, 9.8, 8.9, 10.7, 10.5) while the heap
stayed flat to the byte.

So the long soak's frame-time budget is 2.0 ms per 1000 frames, which is loose
enough to be nearly decorative. **A frame-time regression smaller than about 4
ms over a 3000-frame run will not be caught by anything in this repo**, and on
this box it cannot be: the measurement noise is larger than the signal. Fixing
that needs a quiet machine, not a better gate. If one ever exists, set
`HANABI_SOAK_MAX_MS_PER1K=0.5` there and it will mean something.

### ~~A leak that is not on the heap~~ — GPU memory is now counted; the rest is not

**Superseded for textures.** This section used to say nothing in the project
counted `sg_*` resources and that a gate would need `sg_query_stats`, which
afterhours does not surface. It does not need it: sokol hands out the
`MTLDevice` it created, so `-[MTLDevice currentAllocatedSize]` answers directly
and counts everything, including the resources created inside afterhours that
it would never report. That is the GPU column above, and
`docs/perf/MEMORY.md` entry 5 is what it found.

Two caveats replace it, both real. The gate sees a texture leak for about two
seconds before the pool ceiling flattens it (above). And the device counter is
a total: it says how many bytes, never whose — `hanabi::gpu::ledger` exists to
answer the second question for hanabi's own textures, and a flat ledger under a
climbing device is a leak hanabi does not own.

**Still uncounted:** file descriptors, Mach ports, and anything in a
`vm_allocate`d region that is never touched again. Also **object COUNT** as
distinct from bytes — sokol's pools are fixed (images 128, samplers 64, views
256) and exhausting one is not a memory failure at all, it is a silent
correctness failure. Nothing gates that; the image cache is capped below the
pool instead, with a `static_assert`, which is a promise rather than a
measurement.

### A window resize, because the headless resize path leaks worse than the app

`afterhours_gaps.md` **#200**. The headless backend honours a resize by
recreating the offscreen render target, and `load_render_texture` →
`sgl_make_context` creates five Metal render pipelines that
`sgl_destroy_context` does not release: **4.8 MB per 1000 frames, 18.7 MB a
minute** — twice the autorelease leak that started this project, and named with
`malloc_history` (40510 live `_sg_init_pipeline` allocations over 8103
resizes). It is the headless branch only; a real window resize goes through
Cocoa. `HANABI_STRESS=resize` therefore resizes the LAYOUT only and measures
flat; `HANABI_STRESS_RESIZE_BACKEND=1` reproduces #200 in one run. The
render-target half of a resize stays unmeasured.

### Anything only the windowed app does — with one instrument now, and no gate

**`HANABI_GPU_WATCH=<n>`** prints the device's GPU byte total, the window size,
the image-cache occupancy and the pool-failure count every n frames of the
WINDOWED loop. It is the only memory instrument on that path. It gates nothing
and is not in `make test`; it exists because the paragraph below was true and
one question needed answering with a real window.

`HANABI_GPU_WATCH_RESIZE=1` also drags the window through
`metal_set_window_size` every n frames — the same NSWindow frame change a
person's drag makes. That is how `afterhours_gaps.md` #200's scope was settled:
73 real windowed resizes, 33 distinct sizes each visited twice about forty
resizes apart, and the GPU total is **identical to the kilobyte on 32 of the 33**
(the one exception reads 14 MB LOWER the second time, because its first sample
was taken before the app had settled). A windowed resize leaks no GPU memory.
osascript could not be used for this: it cannot resize another app's window
without assistive access, and that is not a permission to grant on somebody's
daily machine to settle a measurement.

```bash
HANABI_BACKEND=mock HANABI_GPU_WATCH=20 HANABI_GPU_WATCH_RESIZE=1 output/hanabi.exe
```

### Anything only the windowed app does

Every gate here runs the **headless** path. The windowed app has a Cocoa run
loop, an NSWindow, a real swapchain and a display link, and
`scripts/measure_launch.sh` already documents that its launch number is not
comparable (`HANABI_MEASURE_WINDOWED=1` is report-only for exactly this
reason). A leak in the menu-bar extra, the global hotkey, the URL handler, or
the window-restore path is invisible to all of this. That is not a small
category: three of those four are `native_*_install()` calls that only ever run
on a windowed frame.

### The interaction that actually broke it

The original report was "scroll the sidebar up and down until it breaks". The
`scroll` arm drives `HasScrollView::scroll_target` directly rather than
injecting a wheel event, because the injector lives behind
`AFTER_HOURS_ENABLE_E2E_TESTING` and is therefore absent from the binary a
person actually runs. It exercises the same clamp, ease, layout and clip — but
not the wheel handler itself, and not the OS event path. A leak in the event
path is not reachable from here. Filed as **afterhours_gaps.md #172**.

**And it was scrolling the wrong list.** The `scroll` arm wheels the sidebar as
the app first shows it, which the sidebar caps at two viewports — so at a
2000-session catalog `idle` and `scroll` allocated 7,422,071 and 7,422,153
times over the same 2000 frames. Eighty-two apart in 7.4 million: the arm named
after the bug report was a second idle arm. The list a person scrolls is the
one they clicked "Show N more…" on, and that list cost 17.2 ms of CPU a frame.
`scrollall` drives it, `make scroll-gate` gates it, and `docs/perf/SCROLL.md`
is the write-up.

The same is true of every input: **no gate here presses a key, opens a menu, or
resizes a window**, and the scripted-UI suite that can do those things runs 45
to a few hundred frames and asserts on text, not on memory. The gap between
"the driver can reach it" and "the driver can reach it *and* something is
watching memory while it does" is where the next one lives.

### Per-frame allocation attributed to a call site

The soak gate can say "10 blocks a frame, 282 bytes each". It cannot say
*where*. That took `MallocStackLogging` and `leaks`, which need a live process,
twenty seconds of run time and a human reading symbol names — not something
that can sit in `make test`. The recipe is above; the gate deliberately points
at it rather than pretending to replace it.

**Since resolved, and not by that recipe.** `HANABI_PROF_SITES=1` plus
`scripts/alloc_sites.sh` attributes every allocation to a call site from inside
the process, in one run, with no debugger. It also established that
`malloc_history -allBySize` cannot see per-frame churn at all — it walks LIVE
blocks, and the frame frees everything it allocates, so on this app it reports
Metal's startup and nothing else. See `docs/perf/ALLOCATIONS.md`.

### Whether a cache is *correctly* sized

`bigidle` shows a 2000-session catalog settling at 59.9 MB RSS against the
fixture's 38.4 MB and staying there. That plateau is a cache doing its job or a
cache that is 20 MB too big, and nothing distinguishes those two from the
outside. The gate can only see the slope, and the slope is zero in both cases.

### The widget count is a proxy, and it is a count of survivors

`scaling_gate.sh` counts entities holding a `UIComponent` in
`UICollectionHolder`'s collection after the frame. That is what the tree *is*,
not what was *built* — a widget created and discarded within the frame costs
real time and is invisible here. See `afterhours_gaps.md` #146.

---

## The find level gate — does an unchanged query revisit the transcript?

`scripts/find_gate.sh`, in `make test`, opens Cmd+F on 480- and 3,672-message
fixtures and compares each with the same transcript closed. It gates exact work
rather than time: rows visited by the collector, whole-result hit rate, cached
entry count, and the open/closed allocation ratio.

The long arm reads 20.6 visited rows per measured frame because one cold scan of
3,672 messages is amortized across the run; every unchanged frame after that
visits zero. Its ceiling is 30.0. The memo hit rate is 99.33% against a 95%
floor, the cache holds 1,836 paintable entries against its 16,384 cap, and the
allocation ratio is 1.528× against a 2.0× ceiling.

The failure was rehearsed by disabling the whole-result hit branch while leaving
the rest of the implementation intact. The 480- and 3,672-message arms read
797.6 and 6,099.8 rows/frame and both failed. That proves this is a level gate
on repeated unchanged collection, not another slope gate that the original
flat cost can pass.

`docs/perf/FIND.md` records the full 480 / 3,672 / 14,688-message before/after
matrix and the invalidation contract.
