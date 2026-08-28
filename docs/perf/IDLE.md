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
