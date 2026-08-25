# Retiring widgets: what a screen you left costs, and how to stop paying it

**Why this file exists.** `docs/perf/SIDEBAR.md` found that the Home pane was
building 696 cards at a 2000-session catalog and capped it, and then found
something stranger while checking why the cap helped: Home had been drawn
**twice** in the first thousand frames and was still costing 3.15 ms a frame at
frame 800. Not the cost of drawing it — the cost of *having drawn* it. That was
filed as `afterhours_gaps.md` #115 and worked around with a cap, which lowers
the high-water mark and does not touch the mechanism.

This is the mechanism, fixed.

Everything below was measured on 2026-08-25 on `gabeochoa-mac-GRQ7Y259H4`, a
machine shared with several other agents; load averages during the samples ran
from 4 to 12. Every number that could be a ratio or a count is one.

---

## The headline

`HANABI_STRESS=views` — Home, Blocked, Review, Starred, Archived, a thread,
repeat — at `HANABI_STRESS_SESSIONS=2000`. Same binary in both columns,
`HANABI_RETIRE=0` against the default. Measured on `main` at `1abdaa3`, after
`perf/scroll` (the sidebar's floor moved a long way in that branch; every
number here is against the new one).

**What the app HOLDS, sampled once per screen through two full navigation
cycles** (`HANABI_SOAK_EVERY=60`, one bucket per dwell — the entity count is
exact and identical run to run, so this needs no repetition):

| what the frame is drawing | sweep off | sweep on |
| --- | --- | --- |
| the cheapest screen in the cycle (a thread) | 2589 | **167** |
| the dearest (one of the uncapped digest views) | 2809 | **2467** |
| every sample in between | 2589 – 2809, flat | 254 – 2191, tracking |

The left column is flat because it is the union of everything: whatever is on
screen, the app is carrying all five screens. The right column tracks the
screen. On a thread that is **15.5x fewer entities**; on one of the four
uncapped digest views it is barely better, because a digest view genuinely
builds 2276 of those widgets and that is a different bug (see "What this does
NOT fix").

**What that costs per frame**, phase-locked buckets of 360 frames — one whole
navigation cycle, so both columns average the same six screens — over 2880
frames, CPU time (`CLOCK_THREAD_CPUTIME_ID`, not wall):

| | sweep off | sweep on | |
| --- | --- | --- | --- |
| ms/frame (CPU) | 4.63 | **3.06** | −1.57 ms, −34% |
| built this frame | 2182 | 2182 | identical work |
| live widgets, same phase | 2820 | 2467 | |
| stale widgets, same phase | 629 | **276** | |
| live heap, on a thread frame | 48.2 MB | **43.4 MB** | −4.8 MB |
| live heap blocks, same | 75,022 | **52,234** | −22,788 |

Both columns are **flat** across the run: 2817, 2817, 2817, 2820 against 2467
every time. This was never a leak. It is a plateau — the app accumulates the
union of every screen you have visited, stops, and charges you for it forever.
That is worth saying twice, because it is exactly why no existing gate could
see it (section 6).

The 276 widgets still stale with the sweep on are the ones inside the 90-frame
grace: the screen you were looking at a second ago, kept deliberately.

## 1. The seam: hanabi owns `mk`

#115 concluded the app could not fix this, and gave a reason that was wrong:

> the ids are inside `mk()`'s private map

`imm::existing_ui_elements` is an `inline std::map<UI_UUID, EntityID>` at
namespace scope. Any app that includes the header can read it, walk it and
erase from it — and since it maps call-site hash to entity, **it is exactly the
list of widgets the library currently owns**. That is the whole fix's
foundation, and it sat in `entity_management.h` the entire time.

The second half is that `mk` can be shadowed. hanabi imports it through one
using-declaration in `src/ecs/ui_imports.h`, and ADL cannot reach
`afterhours::ui::imm::mk` on its own (its enclosing namespace is not an
associated namespace of `Entity`), so changing that line puts hanabi's `mk` in
front of the library's at all 335 call sites at once:

```cpp
using hanabi::widget_epoch::mk;   // was: using afterhours::ui::imm::mk;
```

It forwards to `imm::mk` — same call-site hash, same entity, same reuse — and
records the frame that built it.

**The one thing that must not be got wrong** is the `std::source_location`.
`imm::mk` hashes the call site to decide which entity to hand back, so a
wrapper that lets the default argument bind to its own body gives every widget
in the app one entity. The library detects it and aborts:

```
[ERROR] Entity ID conflict detected! ... Location: src/ui/widget_epoch.h:101:9
libc++abi: terminating due to uncaught exception of type std::bad_optional_access
```

which is a good failure mode, and `tests/unit/test_widget_retire.cpp` provokes
it deliberately so nobody has to discover it from a crash.

## 2. The stamp is a side table, and that is measured, not taste

The obvious shape is a component:

```cpp
entity.addComponentIfMissing<BuiltAt>().epoch = frame;   // 0.108 ms/frame
```

The one in the tree is a vector:

```cpp
g_stamps[entity.id] = frame;                             // 0.030 ms/frame
```

`scripts/perf_ab.sh`, idle, 2000 sessions, interleaved, median of 5 runs of 800
frames, each against the same binary without the stamp:

| stamp | before | after | cost |
| --- | --- | --- | --- |
| component | 1.310 | 1.418 | +0.108 ms/frame |
| dense vector | 1.309 | 1.339 | +0.030 ms/frame |

**3.6x, for the same four bytes.** An `Entity` holds
`std::array<std::unique_ptr<BaseComponent>, 128>` *inline* — a kilobyte — so a
late-registered component sits ~500 bytes past the entity header in a different
cache line, behind a pointer to a separate allocation: two misses per widget
per frame. The vector is 4 bytes per EntityID, contiguous, and stays in L1.
Filed as gap #160, along with the `int entity_type` sitting unused in the
entity header that would have made the write free and that an app must not
touch.

The price of the side table is that it is not tied to the entity's lifetime the
way a component is. Section 3 is how that is paid for.

## 3. The sweep: two operations, and the map is the ownership list

Once every 15 frames, walk `existing_ui_elements`; for any entry whose entity
has not been built for 90 frames:

1. **erase the hash**, and
2. **mark the entity for cleanup** (afterhours' own post-update bridge destroys
   it at the end of the update phase, before the frame renders).

Doing only (2) is the dangerous half-fix, and it is the one that looks right.
afterhours recycles EntityIDs, so a hash left pointing at a destroyed entity is
not a slow leak — it is the next `mk()` at that call site being handed a live
widget belonging to something else. The unit test that pins this
(`the_map_never_points_at_a_dead_entity`) is the reason to have a unit test
here at all.

**The loop is over the map, not the entity collection**, and that is what makes
the side table safe: the map contains exactly the ids `mk` owns *right now*, so
an entity with a stale stamp that is not in it — one the library made for
itself, one a test spawned, one whose id was recycled — cannot be touched. It
is also O(live widgets) instead of O(all entities), on the frames that sweep.

**Where it runs** is `ecs::WidgetRetireSystem`, registered immediately after
afterhours' pre-layout bridge and before every UI-creating system. A screen
coming *back* on the very frame its widgets are retired rebuilds cleanly,
because retired ids do not reach the free list until this frame's cleanup.

**What is deliberately absent**: nothing resets UIContext's focus / hot /
active ids. All three are re-derived every frame from widgets built *this*
frame — `focus_id` is dropped by `EndUIContextManager` on any frame its widget
does not call `try_to_grab`, `hot_id` is a hit test over live entities,
`active_id` is released on mouse-up — so a widget unbuilt for 90 frames gave
all three up on the first of those frames. hanabi's text-selection owner is the
one cross-frame widget id that is *not* self-healing, and it is dropped when
its widget stops being one of ours.

Three knobs, all read once: `HANABI_RETIRE=0` (off, so it can be A/B'd and
bisected past), `HANABI_RETIRE_GRACE` (90 frames), `HANABI_RETIRE_EVERY` (15).

## 4. What it costs when there is nothing to retire

Idle at 2000 sessions — one screen, no navigation, nothing stale but the
launch skeletons — min-of-bucket over 600 frames, five runs each way,
interleaved:

```
  sweep off   1.442  1.443  1.448  1.451  1.451     median 1.448
  sweep on    1.402  1.403  1.418  1.451  1.452     median 1.418
```

Indistinguishable, and if anything the sweep is ahead, because it takes the
launch skeletons away. The stamp's 0.030 ms is inside this and does not surface
above the noise. (Measured before the `perf/scroll` merge; the absolute floor
has come down since, the comparison has not changed.)

## 5. Measuring an app that is moving

The first attempt at putting `views` in the long soak **failed** on a run whose
memory was flat to the kilobyte:

```
  RSS         +10.7 KB     budget 256   ok
  heap bytes  +62.9 KB     budget 256   ok
  entities    +31.0        budget  25   FAIL  1.2x over budget
```

Nothing grew. The entity count of a navigating app is a function of *which
screen the sample landed on*, and the soak's shared 500-frame bucket is not a
whole number of the arm's 360-frame navigation cycle, so consecutive buckets
compared Home against a thread and the difference was reported as growth. The
median-of-three-buckets window does not help: the three buckets are three
different screens.

Sampling every 360 frames — one whole cycle, every sample on the same screen —
gives `+6.9` on the identical run. `scripts/soak.sh` now sets the bucket per
arm for this reason.

**A measurement of a moving app has to be sampled in whole periods of the
motion, or it measures the sampling.** Every "before/after" number in this file
is phase-locked; the ones in the first draft of it were not, and they were
nonsense in both directions — the same binary read 167 built widgets on one
sample and 2191 on the next.

## 6. Why the two existing gates are blind to this

Both are good gates. Neither can see #115, and the reasons are worth keeping
because they say what a third gate had to do differently.

- **`soak_gate.sh` measures a slope.** This is a plateau. With the sweep off,
  the entity count over three navigation cycles reads 1020, 1247, 1270: rising
  to a high-water mark and stopping. A slope gate reads that as an app that
  settled — correctly, and uselessly.
- **`scaling_gate.sh` measures one screen at two catalog sizes** and never
  navigates. With the sweep off it reads 1.31x widgets; with it on, 1.32x. It
  is blind by construction, not by accident.

- **`scroll_gate.sh` expands one list and sweeps it**, which is a different
  question about the same screen. It does not leave it either.

So `scripts/retire_gate.sh` (`make retire-gate`, ~3 s, in `make test`) navigates
and then counts. Two counts, no milliseconds: **stale widgets** (budget 0, with
the grace turned down to 2 frames for the run) and **live / built** (ceiling
1.50x, measured 1.06x against 7.87x defective).

**The check that keeps that gate honest is the epoch.** The likeliest shape of
this regression is not a broken sweep, it is someone deleting one line from
`build_systems()`. Rehearsed:

```
  live widgets           1260
  built / frame          1251
  stale widgets             0          0
  live / built          1.01x      1.50x
```

A green board on a completely broken fix — with no system the epoch never
advances, every stamp reads as current, "stale" is 0 for the best possible
reason and the worst possible cause, and `built` silently accumulates every
`mk()` call of the whole run so the ratio collapses to 1. The census now prints
the epoch and the gate fails when it is below the frame count. The two scripted
tests have the same blind spot; this is what covers it.

---

## What this does NOT fix

### The screen you are ON is still uncapped, on four of the five digest views

`render_home` caps every section at 20 cards (`kMaxSection`, the
`perf/sidebar-scaling` fix). `render_digest` — Blocked, Review, Starred,
Archived — does not: it builds one card per matching session, unconditionally.
At 2000 sessions a single frame of one of those screens builds **569 cards,
2191 entities**, which is 12x what Home builds beside it.

Retirement changes what that costs: it is now the cost of the screen you are
LOOKING AT rather than a permanent tax. That is the right shape, and it is
still a screen that costs 12x its neighbour. The fix is a cap (like Home's) or
virtualization (like the sidebar's), and it is a product decision rather than a
performance one — Blocked is *the* place you go to see everything blocked on
you, and a cap there hides rows on the one screen whose job is not to.

### The library's own widgets

Nine entities per run never came through hanabi's `mk` — the UI root, the
scrollbars, the drag spacer — so nothing hanabi writes can retire them. Nine is
fine; "unbounded by contract" is not, and it is filed as #162. The soak census
reports the number next to everything else so the blind spot is visible rather
than assumed.

### Coming back to a screen still loses your place

And, usefully, **not because of this work**. See #163: `MeasureScrollViews`
runs on every scroll view every frame, computes the content size by summing
children that were cleared at the top of the frame and never refilled while the
screen was away, and clamps the offset to zero. One frame off-screen and the
position is gone.

The carve-out for it — never retire an entity carrying `HasScrollView` — was
written, measured and deleted: Home scrolled to y=-354, a trip to a thread and
back, and the header is at y=123 both with the carve-out and with
`HANABI_RETIRE=0`. Keeping the entity keeps the field, and the field is
overwritten the next frame.

### State on any other retired widget

An entity destroyed after 90 frames of not being built takes its afterhours
state with it: animation phase, text-input contents, drag state. Nothing in
hanabi is currently exposed to that — the composer and the sidebar's search
field are built every frame, and the palette and rename modal clear themselves
when they close — but a future widget that holds state and is built only
sometimes is the shape of the next bug here. If one appears, the carve-out
`holds_state_worth_keeping()` was the idea, and #163 is the reason to check the
state actually survives before believing it.

---

## Reproducing any of this

```bash
# the headline, both ways (phase-locked: EVERY must divide the 360-frame cycle)
for r in 0 1; do
  HANABI_RETIRE=$r HANABI_BACKEND=mock HANABI_STRESS=views \
  HANABI_STRESS_SESSIONS=2000 HANABI_SOAK=2880 HANABI_SOAK_EVERY=360 \
  HANABI_SOAK_CENSUS=1 ./output/hanabi.exe --screenshot /tmp/x.png |
  grep -E '\[soak\] (frame|widgets)'
done

# the gate, and the gate failing
make retire-gate
HANABI_RETIRE=0 bash scripts/retire_gate.sh

# what a frame holds against what it built, on any scenario
HANABI_SOAK_CENSUS=1 ... ./output/hanabi.exe --screenshot /tmp/x.png |
  grep '\[soak\] widgets:'

# the navigation arm on its own, at soak length
make soak ARMS=views
```
