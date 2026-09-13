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

## The gate

`zig build resize-drive-gate` (also in `test`): `--selftest` first -- the
planted `HANABI_RESIZE_SYNC_DRAW=0` arm must FAIL on sizes skipped -- then the
two scenes. Asserted by count: live resize started and ended once; ≥ 20 steps
applied; frames ≥ 90% of applied sizes; sizes skipped = 0; the last frame
equals the settled window; a frame after the live resize ended; the window
within 4 px of the pointer (AppKit's slack under posted events, see #594);
natural-width audit mismatches = 0 and, on the big scene, ≥ 100 rule hits.
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
