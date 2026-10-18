# Scale: the four probes at 1× / 10× / 100×

What the app costs when the catalog is ten and a hundred times the mock's
default, measured the same way before and after the instant-threads work.
Companion to `STARTUP.md` (what a launch costs once), `SCROLL.md` and
`MEMORY.md` (what a frame costs, and whether it trends), and `LATENCY.md`
(event to ink for fourteen interactions). This file is about one axis those
do not vary: **how much data there is**.

## The knob

`HANABI_MOCK_SCALE=<n>` (`src/api/mock_scale.h`, `docs/mock-knobs.md`). Every
seeded session gains `n-1` clones named `<id>x<copy>`, and each clone carries
the base transcript `n` times over, so sessions, messages per thread, tool
rows and attachments all scale by `n` together. Clone messages are numbered
`1..N` in order with `last_seq = N` and timestamps 30 s apart up to the mock
clock — a thread shaped like the wire's, so the since/window paging, the read
cursor and a delta fetch all see what they would see from the orchestrator.
The base rows are untouched; every fixture that names one still holds. The
pass is a pure function of the base seed and the indices, so the same knob
value yields the same catalog on every run.

At the default seed (20 sessions, ~90 messages): 10× is 200 sessions and the
clones hold 10 copies of their base thread; `t2x1` is 30 messages, the
longest base thread's clones run to a few hundred. 100× is 2,000 sessions and
clones of up to several thousand messages.

## The probes

`scripts/perf_scale_probe.sh` runs all four for each scale and prints one
markdown row per scale (`HANABI_PROBE_OUT=<file>` appends it here). Each is
the app's own headless path (`zig build`; `output/hanabi.exe --screenshot`)
on the mock backend with the mock clock pinned.

| probe | how | reads |
|---|---|---|
| cold start | `N` launches (default 5), the app's `FirstFrame: N ms` line | min / median / max ms |
| switch-to-thread | `HANABI_STRESS=threads` (an open every 30 frames) under `HANABI_SWITCH_PROBE=1`: `src/util/switch_probe.h` stamps the open request as the tab system consumes it and the first frame in which the transcript's ledger is filled and its rows drawn for that id | p50 / p95 / max ms, opens landed; `(cache)` marks an open whose first drawn frame was served from the disk or memory transcript cache |
| scroll fps | `HANABI_STRESS=read` through the longest thread (`t2x1` at scale > 1, `t2` at 1), wall ms/frame from the soak samples every 100 frames | fps p50, and the 5th percentile (the slow tail) |
| resident memory | the soak samples' RSS during the scroll run | max KB |

Wall clock moves with host load; the script prints the 1-minute load beside
each row and the reader keeps it there. `FirstFrame` on the headless path is
the launch gate's number, not the windowed cold launch (`STARTUP.md` has the
difference); it is the comparable number across builds. Frames are the
load-invariant companion to the switch milliseconds (`p50 frames` in the
`[switch] summary` line of the run log).

## BEFORE — instant threads not yet built

Run by the Mac lane at `1cdec3b5` on Boulder (arm64, macOS 27.0.1), the
probe's own block, verbatim; the box's 1-minute load was gated below 8 for
the window and climbed back past 20 minutes after.

```
## perf_scale_probe — 2026-10-18T17:56Z

host: boulder arm64 27.0.1 · build: 1cdec3b595c9 · launches: 5 · soak frames: 900

| scale | load | cold start ms (min / med / max) | switch ms (p50 / p95 / max, n) | scroll fps (p50 / p95) | RSS KB (max) |
|---:|---:|---|---|---|---:|
| 1 | 7.64 | 61.0 / 80.0 / 234.0 | 0.95 / 1.83 / 2.21 (n=34) | 1689.19 / 1609.29 | 40304 |
| 10 | 7.64 | 58.0 / 68.0 / 76.0 | 0.88 / 1.54 / 1.66 (n=34) | 1097.69 / 1041.13 | 46496 |
| 100 | 7.90 | 169.0 / 202.0 / 225.0 | 1.04 / 1.84 / 2.13 (n=34) | 896.86 / 738.00 | 588752 |
```

What the numbers say, read honestly:

- **Switch-to-thread is already under the 50 ms target at every scale**:
  p95 1.5–1.8 ms, one frame. The `threads` scenario opens each thread once
  in a sweep, so 33 of the 34 opens per run are FIRST opens against the
  mock — a mock answers in microseconds, so this measures the client's own
  cost (request → ledger filled → rows drawn), not a network. The one
  `(cache)` open per log is the sweep's first thread, served by the memory
  cache in 0 frames (0.73 ms) against 1 frame for a first open.
  Instant-threads cannot move this number on the mock; what it removes is
  the full newest-N fetch a relaunch paid per thread against a real
  orchestrator, which the probe can only show as `(cache)` on a re-open
  and as the mock's attach journal ("since N" instead of "full").
- **Cold start is flat from 1× to 10× (61–80 ms) and 2.5–3× at 100×**
  (169–225 ms): two thousand sessions through the catalog's seed, sort and
  first layout. The 234 ms at 1× is one outlier launch of five.
- **Scroll through the longest thread**: 1689 → 1098 → 897 fps p50 (t2 at
  3 rows; t2x1 at 30 rows at 10×, 300 rows at 100×), the slow tail
  1609 → 1041 → 738. Nothing near a frame budget; the fall is the ledger's
  per-row cost with ten times the rows per thread.
- **Resident memory**: 40 MB → 46 MB → 589 MB. 1× to 10× costs 6 MB; 100×
  costs 540 MB — 2,000 sessions whose clones carry a hundred copies of their
  base thread each, all held by the mock in memory, plus the catalog. The
  10× figure is the one that resembles a real account; the 100× one is the
  mock's own footprint as much as the app's and is kept for the trend.

## AFTER — instant threads

_Same script, same scales, same host, after Part 3._

## AFTER-2 — the relaunch, with a network that costs something

The BEFORE block shows why the four probes cannot see instant threads on
the mock: the mock answers an attach in microseconds, so a full newest-N
fetch and a one-row delta cost the same nothing. The relaunch block gives
the attach a price — `HANABI_MOCK_OPEN_DELAY_MS` (250, a round trip) plus
`HANABI_MOCK_OPEN_ROW_US` (1000, per message returned) — and measures the
thing instant threads changes: **the first open of a thread after a
relaunch**, when the disk holds it and the memory cache does not.

`HANABI_PROBE_RELAUNCH=1 scripts/perf_scale_probe.sh` runs, at 10×, a warm
launch that opens the threads once (filling a private `HANABI_CACHE_DIR`)
and then a measured launch that opens them again, twice over:
`HANABI_INSTANT_THREADS=0` — the open path as it was, a full newest-N fetch
started beside the disk read — and `=1` — wait for the disk read, fetch
only rows after the cached seq. Both arms paint their first frame from the
disk copy; the columns that differ are the probe's `fresh` lines: when the
network's answer landed, how many rows it carried, and whether the mock
saw a full read or a since-delta.

_The block lands here from the Mac lane's run at the commit that added
it._
