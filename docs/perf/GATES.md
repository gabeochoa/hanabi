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
| soak | `make soak-gate` | yes | ~4 s |
| catalog scaling | `make scaling-gate` | yes | ~9 s |
| autorelease source check | `make source-checks` | yes | <1 s |
| long soak | `make soak` | **no** — before a release | ~65 s |

`make test` was about four minutes before this and is about four minutes and
fifteen seconds after it. That was the budget: a suite that takes fifteen
minutes is a suite people stop running, and a gate nobody runs is worth exactly
as much as a gate that cannot fail.

---

## 1. The soak gate — does the app grow while it sits still?

`scripts/soak_gate.sh`, driving `HANABI_SOAK` in `src/util/soak.h`.

It runs the real render loop headlessly for **1000 frames** against the
deterministic mock catalog, sampling every 250, and compares the bucket ending
at frame 500 with the one ending at frame 1000 — a 500-frame window, 8.3
seconds of app time, after 120 unmeasured settle frames and two further buckets
discarded. The first buckets carry lazy-init that is not a leak; comparing
against them would make every run look like an improvement.

Four things are gated, all expressed **per 1000 frames** so the number does not
depend on how long the run was:

| metric | budget / 1000 frames | why this one |
| --- | --- | --- |
| RSS | +512 KB | the reported symptom itself: a process that grows without bound gets slower and then freezes |
| live malloc bytes | +512 KB | moves the instant something is not freed, where RSS lags by whole pages |
| entities | +25 | an ECS that is not being torn down |
| frame time | +3.0 ms | deliberately loose; see below |

**What set 512 KB.** Nine consecutive clean runs read
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

### The honest part: this gate asserts what is true today

Frame time **is** linear in the catalog on `main` right now. A gate demanding
otherwise would be red on `main`, and a gate that is red on `main` is a gate
somebody deletes on a Tuesday. So the two ceilings sit just above today's
measurements. What they catch is a regression that makes the scaling *worse* —
a second per-row pass, a row that stops being culled, a lookup that walks the
catalog — which is the failure this project has actually had.

**`perf/sidebar-scaling` should lower both numbers in the same commit that
makes them true.** After the sidebar is virtualized the widget ratio should
collapse to about 1.0 and the frame ratio to under 2.0; the constants to change
are the first two lines of `scripts/scaling_gate.sh`:

```bash
WIDGET_RATIO_CEILING="${HANABI_SCALE_WIDGET_CEILING:-10.00}"   # -> 1.50
FRAME_RATIO_CEILING="${HANABI_SCALE_FRAME_CEILING:-12.00}"     # -> 2.50
```

The gate is worth very little at 10.00x. Holding the fix in place is the entire
reason it exists, and it does not do that until that line changes.

### Reproducing a failure

The rehearsal used the most likely real regression: a row that stops being
culled. In `src/ecs/sidebar_system.h`, replace

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

## 4. `make soak` — the long form, for before a release

`scripts/soak.sh`. Five arms, 4000 frames each, about 65 seconds total.

| arm | what it drives | why it is here |
| --- | --- | --- |
| `idle` | nothing | the control. Growth here is growth for no reason at all, the strongest possible finding |
| `scroll` | sidebar wheel, 60 frames down and 60 up | the reported symptom was "scroll the sidebar until it breaks" |
| `threads` | opens a thread every 30 frames | the heaviest thing the app does: fetch, transcript rebuild, tab |
| `tabs` | 8 tabs, then round-robin | anything the tab strip or a per-tab cache holds on to |
| `bigidle` | idle, against a 2000-session catalog | a per-row leak is 100x more visible; a catalog-sized cache shows as a higher plateau rather than a slope |

Memory budgets are **tighter** than the short gate's — 256 KB per 1000 frames
against clean arms reading `+5.3, +10.7, +32.0, +53.3, +64.0` KB of RSS — because
over 4000 frames the settling and the page quantisation that dominate a
1000-frame window have amortised away.

```
=== soak summary (per 1000 frames) ===
  idle      PASS  RSS +32.0 KB     heap +11.5 KB        5s
  scroll    PASS  RSS +5.3 KB      heap +4.1 KB         5s
  threads   PASS  RSS +64.0 KB     heap +19.9 KB        7s
  tabs      PASS  RSS +53.3 KB     heap +32.6 KB        6s
  bigidle   PASS  RSS +10.7 KB     heap +0.2 KB        41s
```

Overrides: `make soak FRAMES=20000`, `make soak ARMS="scroll tabs"`.

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

### A leak that is not on the heap

Both memory metrics come from the malloc zones and from `mach_task_basic_info`.
A leak of GPU memory, of file descriptors, of Mach ports, or of anything in a
`vm_allocate`d region that is never touched again will move neither much.
`sg_*` resources (textures, buffers, pipelines) are the realistic case: sokol
allocates them on the GPU and hanabi creates them from the font atlas and the
icon atlas. Nothing in this project counts them. A gate would need sokol's own
`sg_query_stats`, which afterhours does not surface.

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
path is not reachable from here.

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
