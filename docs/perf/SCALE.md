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

_Numbers land here from the Mac lane's run of `scripts/perf_scale_probe.sh`
at the commit that introduced it; host and build are in the block the script
emits. Linux cannot build the binary (no zig, no Metal), so the source lane
records the method and the Mac lane records the numbers._

## AFTER — instant threads

_Same script, same scales, same host, after Part 3._
