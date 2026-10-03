# Idle frame activity

## Decision

Hanabi retains the last completed Metal frame and returns before `SystemManager::run` when no source of frame activity is live. The display callback still arrives, so input and background completions wake on the next callback. Full UI work runs at three cadences:

| state | full-frame cadence | examples |
| --- | ---: | --- |
| active | 60 fps | streaming, thinking pulse, scroll easing, sidebar animation, drag |
| periodic | 10 fps | caret, toast/auth/theme timers, pending futures, settings debounce, outbox retry |
| idle | 2 fps | safety pulse only |

Immediate wake sources ignore those cadence limits: pointer input, key input, resize, exposure, native menu/notification/deep-link/drop work, ready futures, SSE activity, app requests, split changes, cache-generation changes, shortcut-revision changes, and search release transitions. Native menu commands and shortcut-recorder deliveries expose non-consuming pending probes, so frame admission never steals the queue item from `CommandSystem` or `ShortcutsSystem`. The native Edit bridge replays an ordinary key event and therefore uses the same immediate key-input wake. Search stays active while its lazy corpus is deepening, and pending disk reads remain periodic until their ready edge wakes immediately.

`vendor/afterhours` is unchanged. The viable seam is one level above the framework: returning from Hanabi's `app_frame` before `begin_drawing` and `SystemManager::run` retains the already-presented Metal layer and avoids `ClearUIComponentChildren`, full immediate-mode rebuild, autolayout, and draw.

## Current-main baseline

Measured first on `71c761f`, 1,200 idle Home frames at 1180×949 with `CLOCK_THREAD_CPUTIME_ID` and `HANABI_PROF=1`:

| metric | current main |
| --- | ---: |
| thread CPU | 1.4855 ms/frame |
| wall work | 2.075–2.210 ms/frame |
| steady allocations | 811/frame |
| allocation bytes | 95,646 B/frame |
| live entities | 347 |

At the observed 60 Hz window cadence, that is about 89 ms of main-thread CPU and 48,660 allocations each second while the pixels do not change.

## Prototype comparison

The deterministic comparison runs the real system tree over 1,200 simulated 120 Hz display callbacks, ten logical seconds, with the same process and fixture.

| strategy | full frames / 10 s | CPU ms/s | allocations/s | worst input wait |
| --- | ---: | ---: | ---: | ---: |
| legacy full redraw | 1,200 | 136.432 | 71,044.3 | one callback |
| fixed idle 10 fps | 93 | 11.401 | 6,060.5 | 108 ms |
| event-driven retained frame | 20 | **2.761** | **1,774.1** | one callback |

The retained-frame strategy cuts deterministic idle CPU 98.0%, allocations 97.5%, and full-frame execution 98.3%. A real five-second windowed run, including AppKit/display-callback overhead, moved from 105.576 to 20.896 CPU ms/s and from 35,635.9 to 3,736.5 allocations/s: 80.2% and 89.5% reductions.

The fixed-10-fps prototype was rejected. It saves work, but a key arriving just after an idle tick waits more than 100 ms. The event-driven policy preserves one-callback input latency and uses the lower 2 fps cadence only when no reason to draw exists.

## Correctness coverage

`test_frame_activity` pins these transitions:

1. Pointer and key input render on the next display callback.
2. Resize, exposure, native notification, ready future, SSE, app request, and split change wake immediately.
3. Streaming, thinking, scroll easing, animation, and dragging hold 60 fps.
4. Caret and timer work hold 10 fps.
5. Fully idle work falls to two full frames per second.
6. The fixed-10-fps prototype demonstrates its 100+ ms input delay.
7. Lazy disk reload completion, cache-epoch changes, and search close/release transitions cannot be starved by idle retention.
8. Native command queues, shortcut-recorder deliveries, Edit-bridge key replay, and shortcut-revision refreshes all wake without consuming their payload during admission.

`make idle-gate` runs the real UI tree and gates the absolute per-second level. With retention disabled it was verified red at 1,200 full frames, 128.412 CPU ms/s, and 71,044.1 allocations/s. With retention enabled it passed at 20 full frames, 1.880 CPU ms/s, and 1,774.1 allocations/s.

## Commands

```sh
make idle-gate
HANABI_IDLE_DISABLE=1 make idle-gate
HANABI_IDLE_DIAG_SECS=10 HANABI_PROF=1 ./output/hanabi.exe
HANABI_IDLE_FIXED_10FPS=1 HANABI_IDLE_DIAG_SECS=10 HANABI_PROF=1 ./output/hanabi.exe
```

## Audit across idle states (2026-10-03)

`scripts/cpu_audit.sh` runs the same deterministic harness (`HANABI_IDLE_TIMING`, 1,200 simulated 120 Hz callbacks = 10 logical seconds, the real system tree and frame-admission policy) in the states a reader actually leaves Hanabi in, and prints frames, thread CPU per logical second, and allocations per logical second, with `IdleWhy:` naming the activity bit that admitted each frame. It mirrors the reference's 0.8.9 CPU work (idle redraws, hidden-pane work, polling, scroll). Measured on Aspen, mock catalog, before and after on the same binary build except for the fix:

| arm | before: frames / cpu ms/s / allocs/s | after | what kept it awake |
|---|---|---|---|
| home | 20 / 1.02 / 1,732 | 20 / 1.11 / 1,732 | idle pulse only (unchanged) |
| tabs5 (five restored tabs) | **1,200 / 41.8–50.4 / 73,205** | 20 / 0.92 / 1,810 | `state_request` every callback |
| split (two panes) | **1,200 / 52.5–60.1 / 103,206** | 20 / 1.09 / 2,310 | `state_request` every callback |
| settings (search focused) | **1,200 / 39.8–47.0 / 95,526** | 93 / 3.45 / 7,958 | `state_request` + caret |
| thinking (reply in flight) | 586 / 22.6 / 34,593 | 294 / 10.2 / 17,657 | `thinking` at the display rate |
| mm3 (needs-you rows, faces moving) | faces frozen (see 2); "draw while moving" = 600 / 22.9 / 33,603 | 252 / 9.6 / 14,462 | `animation` |
| list1hz2000 (list lands every second, 2,000 threads) | 20 / 1.29 / 9,164 | -- | not a finding: main-thread cost flat |
| sidebar scroll (scroll gate, 2,000 threads, list expanded) | 0.62 ms/frame min-of-half, 288 entities | -- | not a finding: bounded by the viewport |

The three fixes:

1. **A stale open-at-bottom request held the loop at the display rate.** `Pane::scrollBottomPending` is consumed by the pane's own draw when the thread it names is on screen. The restore sets it on the last tab opened; with another tab showing, or a surface (Settings has no transcript), or a split's second pane, it was never consumed -- and `pane_has_request` counted it, so every callback was an immediate wake. Now `scroll_bottom_due` counts it only while that thread is the pane's open session; the request itself is kept for when the tab comes on screen. 38-48x less idle CPU in those states.
2. **MM3 faces now wake the loop when their frame changes, not every callback.** The moving faces (an angry loop on rows waiting on you, the calm blink on a run) set a "moved" flag the collector cleared on EVERY display callback, rendered or not -- so in a real window the faces drew at the 2 fps idle pulse (frozen-looking); the scripted fixture passed only because scripted runs force full cadence. Now each drawn face reports when its frame next changes (`mm3::next_change_in`), the loop wakes at that deadline (`Mm3Motion::change_due`), and the face clock keeps real time across a long frame gap (a 4.5 s rest is one gap). Against the naive fix ("keep drawing while anything moves"): 600 -> 252 frames, 22.9 -> 9.6 ms/s.
3. **A thinking turn pulses at 30 fps.** Its one slow dot (1.4 s period) and seconds counter do not need the display rate, and the state lasts minutes. `FrameCadence::Pulse` (33 ms); text arriving is still `streaming`, at the display rate. 586 -> 294 frames, 22.6 -> 10.2 ms/s.

Both follow-ups from that pass were then fixed (same day):

4. **Live runs on tabs behind another.** Measured with a scripted live stream in the mock (`HANABI_MOCK_LIVE=<events/s>`, or `manual` for fixtures; `HANABI_IDLE_REALTIME=1` so the debounce and the stream run on the wall clock; `HANABI_AUDIT_WARM_TABS=1` so every restored tab is cached and subscribed). Arm `live5`: five tabs, each with a run emitting 2 events/s, one on screen, 10 real seconds.

   | | frames | cpu ms/s | allocs/s | fetches (full + resume) | rows pulled | transcript writes |
   |---|---:|---:|---:|---:|---:|---:|
   | before | 1,064-1,076 | 249 | 90,000 | 37 + 9 | 464 | 57 |
   | after | 51 | 17-18 | 5,300 | 1 + 9 | 32 | 11 |

   Three things compounded: every hidden tab refetched a full 40-row window every second its run was live (`since` was only taken from a pane, never from the cached copy); each reply was persisted (a JSON write plus a cache-cap walk, on the main thread); and a dirty flag waiting out the 1 s debounce was an immediate wake on every display callback (120 Hz). Now (`AppComponent::LiveSub`): a hidden tab's events only mark it stale -- no fetch, no frame; it catches up every 15 s, resuming from the cached copy's newest row and folding the reply into that copy (writing the reply alone would replace a thread with its tail); showing it fetches at once. The shown tab still follows its run, resuming once a second. A sub wakes a frame only when its refetch is DUE, and only a shown thread's events stamp the frame-waking event clock. Over 20 s each hidden tab catches up once (at 15 s). Fixture: `a_hidden_live_tab_waits_and_catches_up_when_shown`.
5. **The caret blinks on the wall clock and wakes the loop only when it toggles** (`ui/caret_clock.h`, `ecs::drive_caret_clocks`). afterhours' text area advanced its blink by a fixed 0.016 s per drawn frame, so at the 10 fps a focused field held, the composer's 0.53 s half-cycle took 33 frames -- a 3.3 s phase -- while costing ten full frames a second. Before each drawn frame every focused field's timer is now set to where a wall-clock blink is (minus the step the widget is about to add), a keystroke's reset re-anchors it, and the soonest toggle is the frame loop's caret deadline. Arm `caret` (Settings search focused, real time): 93 frames / 27.1 ms/s / 7,959 allocs/s -> 27 / 10.0-10.7 / 2,739. `vendor/afterhours` is unchanged (the widget's public state is written). Unit: `test_caret_clock`.

6. **Transcript writes leave the frame thread** (`api::disk_cache::save_transcript_async`, one background writer). `LoaderSystem::save_and_trim` used to build the JSON, write the file and run the cap check in the frame that landed a fetch. Now it hands a snapshot to the writer: coalesced per thread (snapshots queued before a write starts collapse into the newest, written at most 150 ms after the first), ordered per thread (one writer), read-through (`load_transcript` answers from a queued or in-flight snapshot before the file), clear-safe (`wipe_all_report` drops the queue and waits out the write in flight; `set_namespace` flushes first), the cap check once per drained queue, and flushed on quit (`app_cleanup`, bounded at 3 s; an atexit hook for other exits). Measured with the disk cache switched on under the mock (`HANABI_MOCK_DISK_CACHE=1`), frame-thread wall time inside the persist call (`PersistCost`), each arm run twice:

   | arm | before: calls / total ms / worst ms | after | writer thread (after) |
   |---|---|---|---|
   | persist1 (the 160-message fixture on screen, live run, 10 s) | 12 / 79-85 / 8.7-13.3 | 12 / 0.6-1.9 / 0.2-1.5 | 11 writes, 73-78 ms, worst 9.1-9.2 |
   | persist5 (five live tabs, 2,000 cached transcripts, 20 s) | 25 / 74-98 / 8.1-16.0 | 24-25 / 0.6-0.7 / 0.15-0.35 | 24-25 writes, 77-83 ms, worst 4.5-7.7 |

   The worst single persist in a frame went from most of a 16.7 ms frame to under 1.5 ms; persist1's frame-thread CPU 30.7 -> 22.5-23.4 ms/s (persist5's total is dominated by the harness seeding 2,000 files at start). The write volume is unchanged at a 1 s refetch cadence (coalescing only folds bursts). Unit: `test_transcript_writer` (coalescing, read-through, per-thread order, a wipe drops queued writes, bounded flush).

Still open: the Pulse cadence (thinking) and the MM3 deadline wake are the remaining steady costs while a run is visible; sessions.json, created-at times and drafts still write on the frame thread (small files, rare).
