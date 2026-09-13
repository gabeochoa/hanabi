# A live window resize

The measurements behind `scripts/resize_drive_gate.sh`, the natural-width rule
in `src/ecs/transcript_render_cache.h`, and the same-step draw in
`src/sokol_impl.mm`. Capture rig: the M1 Pro build machine, macOS, load
average 8 (shared), `nice -n 10`, mock backend, isolated HOME, window
1100x760 at 2x. Every number below is pasted from a run's `SUMMARY`/`CADENCE`
line or the `[prof]` table; nothing is computed by hand.

## The stimulus

`HANABI_RESIZE_DRIVE=grow:300x150:150,grow:-500x-250:150,grow:200x100:80`
(`src/resize_drive.mm`). A user-interactive-QoS thread posts real NSEvents at
the window's bottom-right resize zone -- mouse-down, a mouse-dragged every
8 ms along three legs with two edge reversals (net zero), the final position
repeated so the last step lands, mouse-up -- into NSApp's queue. The sampler
(`HANABI_RESIZE_DRIVE_SAMPLE=1`) places the main thread inside
`-[NSWindow(NSWindowResizing) _resizeWithEvent:]` ← `-[NSThemeFrame
mouseDown:]`, frames drawn from `-[MTKView drawRect:]` in
`NSEventTrackingRunLoopMode`; `NSWindowWillStartLiveResize` fires once,
`windowDidResize` per applied step, `NSWindowDidEndLiveResize` once. That is
AppKit's own live resize. What it is not: the window server's delivery of a
real pointer (afterhours_gaps.md #594 has the three consequences).

Why not the obvious drivers: an 8 ms `NSTimer` in common modes fired every
29 ms (p50) with the app idle and 37 ms inside the tracking loop; a default-QoS
thread's `sleep_until` was coalesced by 33–50 ms under `nice`;
`CGEventPostToPid(getpid())` is dropped without the Accessibility grant
(`live_starts=0`). `sample(1)` cannot attach to the process here, hence the
in-process sampler.

## What a drag costs, before

9342161 bytes, driver at user-interactive QoS, three runs per scene.

| scene | app frame CPU p50 / p95 / max | frame wall p95 | inter-frame gap p50 / p95 / max | steps applied | sizes skipped |
|---|---|---|---|---|---|
| t2 (typical) | 1.4–1.7 / 4.3 / 6 ms | 7–9 ms | 13–17 / 35–45 / 61–73 ms | 166–183 | 7 / 19 / 10 |
| 3,672 messages | 4.8 / 19.7 / 20.6 ms | 20 ms | 20 / 67 / 87 ms | 87–105 | 4–10 |

Main thread, all samples: 73–76% in `mach_msg2_trap` under the tracking loop's
`nextEventMatchingMask` (idle, waiting for the next event), 8–12% inside the
app's frame, the rest CA transaction commits (`_CASCreateFencePort`,
`commit_transaction`), drawable registration and IOSurface churn. Inside the
app's frame, 20–26% of samples are `sglue_swapchain` → `[CAMetalLayer
nextDrawable]` blocking on a drawable of the new size.

The display link kept ticking: backend callbacks every 8.0–8.4 ms p50 through
the whole drag; the frame policy drew one frame per applied size and skipped
the callbacks between (callbacks 350–530 against 90–180 rendered frames).

Pre-upgrade control (2953d07 + afterhours 9ff9079, same driver grafted,
interleaved pre/cur/pre/cur): t2 gap p50 21–23 / p95 66 ms, CPU p50 3.8–4.0;
big CPU p50 6.0–6.1 / p95 16.0–16.2, gap p50 17–20 / p95 59–61. The current
build is at or under the control on every column; the upgrade did not
regress resizing.

### Where the big scene's frame goes (HANABI_PROF=1, 116 frames)

| phase | calls/frame | ms/frame | share |
|---|---|---|---|
| FRAME (cpu) | 1 | 8.52 | 100% |
| transcript.pass1_measure | 1 | 5.75 | 68% |
| measure.bubble_h | 728 | 5.23 | |
| text.count_lines | 1064 | 1.57 | |
| text.rich_body_h | 364 | 1.27 | |
| cache.msgrender_miss / miss_widthstale | 696 / 681 | | |

Every new width made the item index rebuild (`global_facts_equal` fails on
`pane_width`) and every render-cache lookup a width-stale miss: the whole
transcript re-measured, per frame, for as long as the drag lasts.

## Change 1: the natural-width rule

`transcript_render_cache.h`: an entry measured at a width where its widest
hard line fit is exact at every width whose wrap width still holds that line
-- the greedy counter's first probe per hard line is "does the whole line
fit", and when it does no other probe runs, so the count and the height are
the hard-line count at any such width. The widest hard line is measured once
per message and source with the fonts the counter would use (headings at their
heading font, paragraphs as `md_visible` at the body font; code blocks and
tables are width-independent), and the hug width of a user bubble reuses the
same fact.

Exactness: `tests/unit/test_pane_memory.cpp` at the boundary (fits at exactly
the natural width, misses one pixel under, a wrapped entry never travels, the
hug capped at the new max); `HANABI_NATURAL_AUDIT=1` re-measures every hit the
rule served the long way and counts disagreements -- the gate runs with it on.
Across the drive on t1/t2/t6/big (1400 → 900 → 1100 wide): 42,443 + 48,893
audited hits, 0 mismatches.

| scene | pass1 ms/frame | frame CPU p95 | render-cache misses/frame |
|---|---|---|---|
| big, before | 5.75 | 19.7 ms | 696 |
| big, after (two runs) | 4.22 / 4.83 | 12.3 / 12.7 ms | 370 / 411 |
| t2 | 0.03 → 0.03 | unchanged | |

What is left: the messages that WRAP still re-measure at every width -- half
the big fixture's bubbles -- and that is the cost of exact layout at the new
width, not a cache miss.

## Change 2: draw the step inside the step

`sokol_impl.mm`: on `NSWindowDidResizeNotification`, when the window is
`inLiveResize` and no frame is in progress, `-[MTKView draw]` -- the frame for
the new size is drawn inside the resize step through the same callback and
`gfx::begin_frame` (the target swap stays at the frame boundary; #374). Off
with `HANABI_RESIZE_SYNC_DRAW=0`.

Three runs per arm, same bytes, t2:

| arm | sizes skipped | gap p95 | gap max | steps p50 | callbacks |
|---|---|---|---|---|---|
| off | 7 / 19 / 10 | 37–45 ms | 61–73 ms | 13–17 ms | 349–357 |
| on | 0 / 0 / 0 | 34–35 ms | 46–60 ms | 13–17 ms | 501–522 |

Every applied size gets painted; the tail shortens a little; the step cadence
does not move, so the hypothesis that in-step drawing would let AppKit apply
steps faster is not supported. One extra callback per step (~1.5–2 ms CPU).

## Change 3: the fit interval

`src/util/wrap_count.h`, `transcript_render_cache.h`, `measured()`. What
change 1 left was every message that WRAPS: on the big fixture that is every
assistant turn (a 135-character closing paragraph in a column that fills the
pane), ~380 render-cache misses per applied size.

The bisecting counter is a deterministic function of the text and of the
outcomes of its comparisons `measure(probe) <= max_width`, and nothing else
in it reads the width. So its answer at one width holds at every width where
every probe that fit still fits and every probe that overflowed still
overflows: the half-open interval `[max fitting advance, min overflowing
advance)` over the probes that call made. It is the same computation, so it
needs no monotonicity of the metric -- only determinism. Change 1's rule is
the case where nothing overflowed. The counter records the interval on
request; `count_lines` keeps it beside each memoized count; `measured()`
intersects it over every count one message asks for (the fold count and
each paragraph and heading in `rich_body_h`; tables, code blocks and blank
lines are flat in the width) and the render cache serves the entry at any
wrap width inside it, through the `wrap_w_of` the caller already passes.

Exactness: `tests/unit/test_wrap_count.cpp` sweeps a corpus (the fixture's
turn, a many-paragraph markdown answer, a short/long/short reply, the wrap
corpus) at 1 px over 1..400 under four metrics, two of them non-monotonic,
and requires the count and the spans identical at every width inside a
recorded interval (`HANABI_WRAP_STRESS=1` steps half a pixel). It does not
require a different answer just outside: a moved break can leave the count
unchanged. `tests/unit/test_pane_memory.cpp` holds the cache to both bounds.
Interval hits count under the same audit as change 1: `HANABI_NATURAL_AUDIT=1`
re-measures each one the long way.

Rig as above (1100x760, mock, private HOME per run, the three-leg pattern),
both arms `nice -n10`, interleaved A B B A A B, baseline b6d82ef built in its
own worktree, the box quiet by arrangement. Pasted from `[prof]` and
`SUMMARY`, three runs per arm.

| big (918 turns), audit off | baseline b6d82ef | with the interval |
|---|---|---|
| render-cache misses / frame | 383.1 / 379.2 / 364.4 | 59.4 / 46.6 / 59.9 |
| interval hits / frame | 0 | 337.6 / 320.1 / 346.7 |
| transcript.pass1_measure ms / frame | 4.54 / 4.58 / 4.39 | 1.77 / 1.60 / 1.85 |
| FRAME (cpu) ms / frame | 7.13 / 7.22 / 6.95 | 4.54 / 4.52 / 4.40 |
| frame CPU p50 / p95 | 5.14 / 13.19, 5.00 / 13.18, 4.91 / 13.03 | 3.64 / 11.22, 3.79 / 9.20, 3.87 / 11.26 |
| gap p50 / p95 / max | 16.4–17.5 / 59–64 / 83–108 | 16.8–19.0 / 66–68 / 83–91 |
| steps applied · callbacks | 117 · 518, 111 · 514, 102 · 517 | 116 · 538, 110 · 563, 111 · 558 |
| sizes skipped | 0 / 0 / 0 | 0 / 0 / 0 |

t2 (typical), audit off: misses 0.4–0.5 → 0.1 per frame, interval hits
0.4–0.7, FRAME (cpu) 2.63–2.88 → 2.28–2.86 ms, CPU p95 3.79–5.03 → 3.80–3.96.
Within run-to-run noise: a typical thread wraps too little for this to
matter. t1 and t6 (audit on): misses 0.4–0.5 → 0.1 and 0.3–0.4 → 0.1.

Exactness on the drive, audit ON (separate runs; with the audit on the
candidate's frame time reads as the baseline's, because the audit pays the
miss on every hit): big `natural_audited` 90,275 / 97,638 / 81,081 with
`natural_mismatch` 0 / 0 / 0; t1 197 / 170, t2 362 / 348 / 367, t6 84 / 76
audited, 0 mismatches.

What the big fixture overstates: every assistant turn there is the same
text and wraps in ONE paragraph, so its interval is wide and ~85% of the
turns are served per step. A real answer with many wrapped paragraphs has
that many breaks pinning the interval, and the saving shrinks toward the
baseline; no fixture in the mock has that shape, so that arm is unmeasured.
Not measured: allocations per frame (the drive does not report them). The
cadence row is UNRESOLVED: gap p50/p95 read a little higher on the candidate
and the max a little lower, over the same posted drive (389 drag events every
run) and a comparable count of applied steps, with ~40 more display-link
callbacks per run; three runs per arm on a shared box do not separate that
from noise, and no cadence win is claimed.

## The gate

`zig build resize-drive-gate` (also in `test`): `--selftest` first -- the
planted `HANABI_RESIZE_SYNC_DRAW=0` arm must FAIL on sizes skipped -- then the
two scenes. Asserted by count: live resize started and ended once; ≥ 20 steps
applied; frames ≥ 90% of applied sizes; sizes skipped = 0; the last frame
equals the settled window; a frame after the live resize ended; the window
within 4 px of the pointer (AppKit's slack under posted events, see #594);
off-width audit mismatches = 0 and, on the big scene, ≥ 100 audited hits;
and, on the big scene, `cache.interval_hit` ≥ 100 with `cache.msgrender_miss`
under it (the baseline's logs read 0 hits against 47,736–53,244 misses and
fail this; the candidate's read 36,814–47,850 against 6,426–8,262 and pass).
Timing percentiles are printed, not gated: the box is shared.

## What this does not settle

- The applied-step cadence and the 60–90 ms tail. Under posted events the
  driver contends with the main thread for the event-queue lock (drag events
  arrive in bursts, p95 ~50 ms apart), so the cadence measured here is a mix
  of AppKit's and the driver's. What was established: the display link never
  stalls, the app never skips an applied size (now), and the app's own frame
  is 1.5–5 ms at typical content. Whether a real pointer sees 8 ms steps or
  16 ms steps needs the window server path, which needs the Accessibility
  grant this machine does not give.
- `nextDrawable` waits at a new size (up to 30 ms wall on the big scene) are
  CAMetalLayer allocating drawables for the new drawable size on every step;
  not touched here.
