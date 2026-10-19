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

Same script, same scales, same host, at `6135be9e` (instant threads in),
the Mac lane's block verbatim; load 7.4–8.0 for the window, as before.

```
## perf_scale_probe — 2026-10-18T18:40Z

host: boulder arm64 27.0.1 · build: 6135be9e1e16 · launches: 5 · soak frames: 900

| scale | load | cold start ms (min / med / max) | switch ms (p50 / p95 / max, n) | scroll fps (p50 / p95) | RSS KB (max) |
|---:|---:|---|---|---|---:|
| 1 | 7.97 | 64.0 / 69.0 / 131.0 | 0.92 / 1.64 / 1.69 (n=34) | 1669.45 / 1604.19 | 40048 |
| 10 | 7.74 | 57.0 / 65.0 / 77.0 | 0.95 / 1.65 / 1.67 (n=34) | 1094.09 / 1032.60 | 46432 |
| 100 | 7.44 | 179.0 / 198.0 / 267.0 | 1.07 / 1.86 / 1.91 (n=34) | 911.58 / 851.63 | 589120 |
```

Every column is within noise of BEFORE, which is the expected result and
not a disappointing one: the four probes measure the client's own cost
against a mock that answers an attach in microseconds, and the sweep's
opens are first opens (one memory-cache hit per run, the first thread,
0 frames in both blocks). Instant threads changes what a RE-open after a
relaunch asks the network for; the block below is built to see that.

## Memory at 100×: the mock's share, measured

The 589 MB at 100× is the mock, not the client. A std-only probe on Linux
(gcc 11, glibc) builds the mock's seed at each scale and reads
`/proc/self/status` around it:

| scale | sessions | messages | text bytes | RSS growth for the seed |
|---:|---:|---:|---:|---:|
| 1 | 20 | 61 | 7 KB | 0.7 MB |
| 10 | 200 | 5,551 | 0.6 MB | 6.9 MB |
| 100 | 2,000 | 603,961 | 67 MB | **649 MB** |

`sizeof(api::Message)` is 960 bytes — fifteen strings and two inline
fact structs (`ArtifactRef`, `ElementFacts`) — so 604k of them is 580 MB
before a byte of text; the clones at 100× carry their base thread a
hundred times over and the mock keeps every one resident. The client
holds the catalog (2,000 × 520-byte `SessionSummary`, about 1 MB), five
cached transcripts, and the render caches; its share at 100× is the RSS
minus the seed, which on Boulder is within the probe's noise of zero.

The probe now reports this itself: the app logs `[mock] seed scale= …
rss_delta_kb=` (resident growth measured around the seed build, through
the soak's RSS reader) and the script adds `mock seed KB` and `client KB
= RSS max − mock seed` columns, so the next run separates the two.

What a 960-byte message means for the client is a separate, real item:
a 300-row thread is 288 KB of struct before text, and the five-thread
cache at 100× holds 1.4 MB of it — and every copy of a `Session` (the
memory cache copies out, reconcile copies in, the disk writer snapshots)
moves all of it. Scoped for its own commit: `ArtifactRef` is 232 bytes
and `ElementFacts` 280, both inline and both empty on an ordinary
message; behind a small owning indirection (null when empty, deep copy,
`->` access with an empty static on null) `Message` drops to about 450
bytes. The change touches 69 reads in 7 files (17 in
`main_pane_system.h`, 16 in `disk_cache.cpp`, 12 in `mock_client.h`, 11
in `element_rows.h`, 9 in `transcript_reconcile.h`) and 10 writes, and
`ElementFacts` has its own string member named `element`, so the rewrite
is by hand per site, not a substitution — it needs the Mac compile loop
and goes after the gate clears.

### AFTER-3 — the 464-byte message

Done, with the compiler finding the sites: `Message` keeps `artifact()`
/ `element()` (const reads; an empty static when the message has none)
and `artifact_mut()` / `element_mut()` (copy-on-write into one shared
`Extras` block holding both structs), so a message without an artifact
or an element carries one null pointer where it carried 512 bytes.
`sizeof(api::Message)` 960 → **464**. The same Linux probe, scale 100,
604k messages:

| | RSS growth for the seed | probe wall (seed + 2,000 opens) |
|---|---:|---:|
| before (960 B) | 649,288 KB | 0.46 s |
| after (464 B) | **355,620 KB** | **0.28 s** |

Mac (Boulder, the gate's own clone of e4a2d8a1 against ac0b03ee, two
interleaved rounds each at load 10–14 with another build alongside,
2026-10-19 09:13–09:22Z; `perf_scale_probe`, scales 10 and 100):

| scale | arm | RSS max KB | mock seed KB | client KB | mock seed ms | frame cpu p99 / worst ms | >16 ms |
|---:|---|---:|---:|---:|---:|---:|---:|
| 10 | before r1 / r2 | 46,928 / 46,560 | 7,136 / 7,968 | 39,792 / 38,592 | 1.2 / 1.1 | 1.44 / 1.66 · 1.35 / 1.57 | 0 |
| 10 | after r1 / r2 | 44,368 / 44,544 | 5,296 / 5,504 | 39,072 / 39,040 | 0.9 / 0.9 | 1.23 / 1.34 · 1.39 / 1.43 | 0 |
| 100 | before r1 / r2 | 589,040 / 589,088 | 557,280 / 556,992 | 31,760 / 32,096 | 84.6 / 84.1 | 1.49 / 1.59 · 1.47 / 1.51 | 0 |
| 100 | after r1 / r2 | **356,816 / 356,864** | **324,688 / 324,640** | 32,128 / 32,224 | **51.2 / 53.0** | 1.46 / 1.59 · 1.59 / 2.52 | 0 |

At 100× the resident maximum falls 39 % and the seed 42 %, reproduced
to the kilobyte across rounds; the client's own share is flat (the win
is the catalog's messages, as the arithmetic says). Cold start, switch
latency, scroll fps and the frame tail sit inside the round-to-round
noise at that load — no frame over 16 ms in any of the eight rows.

−294 MB is 496 bytes × 604k, within 2 MB of the arithmetic; the wall is
the seed's allocation and copying, and the client's own copies of a
`Session` (memory cache out, reconcile in, disk snapshot) move the same
fraction less. 92 sites rewrote; the mock's `HANABI_TOOLS_DEMO` threads
and the disk cache's artifact / element round trip are the writers. The
remaining 464 bytes are thirteen strings (416) and the scalars; eight of
the strings are tool-row fields (`tool_result`, `tool_status`,
`tool_node`, `tool_input`, `run_outcome`, `run_note`, `image_path`,
`local_id`), which is the next cut if memory comes back to the top of
the queue — half the rows in a typical thread are tool rows, so it would
pay less per message than this one did.

## Cold start at 100×: also the mock

The same Linux probe times the launch-path pieces at 100× (2,000 rows):
building the mock's seed **304 ms**; a second `list_sessions` (the
catalog copy) 1.6 ms; `keep_one_row_per_id` 1.0 ms; writing the catalog
to the disk cache 17.6 ms; reading it back 13.8 ms. At 10× the seed is
3.2 ms and the rest under 2 ms each. The headless `FirstFrame` pumps
frames until the session list resolves — it IS cold-to-interactive for
the list — and at 100× most of what it waits for is the mock constructing
604k messages. The app now logs `FirstFrame mock seed: N ms` beside
`FirstFrame`, and the probe prints `mock seed ms` and `client cold ms =
cold start − seed` so the client's own launch cost is a column.

Of the client's pieces the only one that grows past a millisecond is the
catalog's disk write (18 ms for 2,000 summaries as JSON, off the frame
thread) and its read on the next launch (14 ms, on the launch path when
the cache is warm). Both scale with rows, not messages; neither is near
the 250 ms gate at 100×.

## The queue, in the order the owner feels it

1. **First open**: cold start to the first INTERACTIVE frame, and the
   first open of a thread. The headless `FirstFrame` is the former for the
   list (above); instant threads is the latter, with AFTER-2 below as its
   measurement. Open: a windowed cold-to-interactive number (the headless
   one omits Cocoa and Metal's window), which `STARTUP.md` already notes
   differs by ~100 ms.
2. **Scroll smoothness**: 897 fps p50 / 738 slow tail through a 300-row
   thread at 100× (BEFORE), 912 / 852 (AFTER). Nothing near a frame
   budget; the fall from 1× is the ledger's per-row cost. Jank is the
   tail, not the median, so the soak now keeps every frame's CPU time and
   prints `[soak] tail frames= p50= p95= p99= worst= over16= over8=` at
   exit; the probe shows p99 / worst / frames-over-16ms for the scroll
   run. Open: the first Mac numbers in that column, then whichever row
   kind the worst frames fall on (a table, a diff card, a long code
   block) via the existing per-row prof zones. The attribution now
   exists: every transcript row renders inside a `render.row.<kind>`
   zone (bubble, tool, tool_pile, stretch, spawn, thinking, event,
   delivery, compaction, artifact, element, outcome, system) and a table
   inside `render.block.table`; under `HANABI_PROF=1` the soak snapshots
   the zone table before each frame and, for a frame over
   `HANABI_SOAK_SLOW_MS` (8) after the settle, prints
   `[soak] slow frame N <ms>: zone=ms/calls …` with that frame's eight
   biggest zones. The probe runs the scroll soak with it on at 1 ms and
   appends the six slowest frames per scale under the table, so the
   answer to "which row kind" is in the report rather than in a guess.
   First Mac run (Boulder, load 9.6, 2026-10-19 12:27Z, main 1919a88a +
   the zones): after the settle no scroll frame at 10× or 100× exceeded
   4 ms of CPU — p99 1.28 / 1.26 ms, worst 1.32 / 1.42 ms — so the
   scroll tail has no slow kind to name at these scales; the block now
   lists the slowest six regardless, so the next run says which zones
   an ordinary 1.3 ms frame is made of.
3. **Menus, tooltips, popovers**: input to first painted frame ≤ 16 ms.
   `LATENCY.md` and `tests/ui/interaction_latency_budgets.e2e` already
   measure event-to-GPU-ink for popup open / close, focus gain / loss,
   click feedback, typing and submit, with FRAME budgets (3–4 frames,
   measured 1–2) — the deterministic unit on a shared box. Open: the row
   menu, the command palette and a tooltip are not in that fixture; add
   them, and tighten the budget for open / close to 1 frame where the
   measurement already shows 1.
4. **Memory**: the mock's share is now a column; the client's real item
   is the 960-byte `Message` (above).

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

**First attempt (a1530d70, Boulder, load 12): hollow.** Both arms came
back `fresh landed=0 … full=0 since=0` — no network answer ever landed in
the sweep. The reason is the sweep's cadence, not the cache: the `threads`
scenario opens a new thread every 30 frames, which is half a second at
60 Hz but ~50 ms of wall in the headless loop (a frame is about a
millisecond), so every priced 250 ms attach was superseded by the next
open before it landed, and a superseded fetch records nothing. The two
numbers that did land are worth keeping: first paint from the DISK copy
was 9.56 ms p50 in the old arm and 6.39 ms in the new (7–15 headless
frames, against ~1.6 ms / 1 frame for a first open answered by the mock's
in-memory network) — the disk read is a JSON parse on a worker plus a
per-frame poll, and it is slower than a mock round trip, which only
matters against a mock.

The block now opens one thread every 600 frames for 6,000 frames
(`HANABI_STRESS_EVERY` / `HANABI_PROBE_RELAUNCH_DWELL`, `_FRAMES`): ten
opens per arm, each with ~0.6–1 s of wall to land a 250 ms + 1 ms/row
attach. Expected shape at 10×: first paint equal in both arms (disk copy),
fresh p50 lower in the after arm by about the rows' share (30 rows ×
1 ms), rows fetched ≈ 30/open before and ≈ 1/open after, full / since
10 / 0 before and 0 / 10 after.

**Populated (1bb2f5c3, Boulder, load 10.5), the Mac lane's block verbatim:**

```
### relaunch at 10x — attach costs 250 ms + 1000 us/row; one open every 600 frames, 6000 frames

| arm | opens | first paint p50 ms | fresh p50 / p95 ms | rows fetched | full / since |
|---|---:|---:|---|---:|---|
| before (full fetch) | 11 | 17.06 | 349.50 / 413.43 | 200 | 8 / 0 |
| after (since-delta) | 11 | 7.02 | 343.12 / 425.67 | 9 | 1 / 7 |
```

Per-open lines: before `fresh r5x1 333 ms rows 30 full`, `fresh r5x2 376 ms
rows 30 full`, `fresh r6x1 289 ms rows 20 full`; after `fresh r5x1 338 ms
rows 1 since`, `fresh r5x2 407 ms rows 1 since`, and one `fresh t9 265 ms
rows 2 full` — a thread the disk did not hold, which takes the full path
by design.

Reading. This is the number that proves instant threads: the same eleven
reopens after a relaunch asked the network for **200 rows before and 9
after**, eight full attaches against one, seven of them deltas. The
fresh time barely moves (350 → 343 ms p50) because the priced attach is
250 ms either way and the rows' share (30 × 1 ms) sits inside the
load-10 noise; on a real server where a transcript's cost is its rows,
the delta is the saving. First paint — from the disk copy in both arms —
went 17 → 7 ms p50; the old arm's full fetch parsed and merged 30 rows on
a worker while the disk read was landing, the new arm's delta did not.
The sweep's 900-frame columns at 10× are unchanged within noise (switch
p95 1.96 ms, 986 fps, RSS 46.7 MB of which the mock's seed is 8.2 MB and
the client 38.5 MB; frame CPU p99 1.29 ms, worst 1.45 ms, none over
16 ms).
