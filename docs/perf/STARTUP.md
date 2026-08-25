# Startup, persistence and the data layer

**Why this file exists.** Everything in hanabi that has ever been profiled runs
every frame. This is the other half: what happens **once**, at launch, or on a
keystroke. Nobody had measured it, so nobody knew that the launch gate was
passing by one millisecond for a reason that had nothing to do with the app's
own startup code.

Written the way `docs/visual-parity/FRICTION_LOG.md` is: what I wanted, what
happened, what it cost, numbers inline. A number is worth more than an
adjective, and **a negative result with a number is worth more than a plausible
fix without one** — several of the entries below are negative results.

## How anything here was measured

**Wall clock was not usable.** This box is shared with three other agents
running builds; load average sat between 18 and 29 for the whole session. An
A/B run earlier in the night came out with the *faster* binary reading 50%
slower. So:

- Micro-benchmarks use **`CLOCK_THREAD_CPUTIME_ID`**, which counts only cycles
  this thread was actually given. A neighbouring build steals throughput but
  not the measurement. `tools/bench_startup_data.cpp` and
  `tools/bench_data_layer.cpp`.
- Where a number could only be wall clock (cold launch), both arms were run
  **interleaved, alternating binaries**, 12 rounds each, and reported as
  min/median/max rather than a single reading.
- **Operation counts** are quoted wherever one exists, because they are
  load-invariant entirely: "the settle loop runs exactly 3 frames" is a fact no
  scheduler can argue with, and it was identical across every run.

Two numbers in this document were **wrong on the first attempt and caught
before they were believed**; both are written up below, because the way they
were wrong is more instructive than the corrected values.

---

## 1. Where the launch gate's 249 ms goes

**What I wanted.** `scripts/measure_launch.sh` gates FirstFrame < 250 ms and was
reading ~249 ms. That is not a gate passing, it is a gate about to start
flaking. I wanted the breakdown.

**What happened.** There was no breakdown to be had. The app logs `Startup`
(~20–26 ms) and the harness reads `FirstFrame` (~200–250 ms), and **nothing
covered the ~175 ms in between** — `Startup` stops at "systems ready", and
`FirstFrame` is logged inside the capture loop, so every settle frame and every
sleep between the two fell in a hole. Adding marks (`HANABI_STARTUP_PROF=1`,
headless) gave this:

| phase | run 1 | run 2 | run 3 | whose |
|---|---|---|---|---|
| `graphics::init` | 19 ms | 24 ms | 19 ms | OS / Metal |
| `preload` | +1 | +1 | +0 | ours |
| `setup_app_state` | +0 | +0 | +1 | ours |
| `build_systems` | +1 | +0 | +0 | ours |
| **settle-wait loop** | **+192** | **+90** | **+178** | **ours** |
| capture frame 0 | +3 | +1 | +1 | mixed |

**All of hanabi's own initialisation is ~1 ms.** Fonts, the icon atlas, the
settings load and the client construction together do not reach two
milliseconds. Every suspect on the list going in was wrong.

The entire cost was the headless settle-wait loop, which pumps frames until the
session list resolves. It runs **exactly 3 frames** on the mock backend — the
same count on every single run — and slept 8 ms after each one. Two bugs:

1. It slept **after** rendering and re-checked at the top of the loop, so the
   iteration that actually resolved the fetch still paid a full 8 ms sleep
   before anyone looked.
2. The 8 ms floor was sized for a network backend. The mock resolves
   `list_sessions()` in **0.118 ms**. The comment above the loop claimed "the
   mock resolves on the first poll, so it exits this loop immediately (adds ~0
   wait)". It does not, and never did.

**The fix.** Re-check readiness immediately after the render, and back the sleep
off 1, 2, 4, 8, 8… instead of sitting at 8. Four iterations reach the old
cadence, so a real network fetch of a few hundred ms polls essentially as before
(~40 renders over 300 ms against ~37).

**What it cost, measured.** Interleaved A/B, 12 rounds each arm, alternating
binaries so both saw the same machine load:

| | min | p50 | max |
|---|---|---|---|
| before (8 ms fixed) | 146 | **220** | 273 |
| after (1,2,4,8 backoff) | 63 | **80** | 96 |

**The distributions do not overlap** — the old binary's best run is worse than
the new binary's worst. `scripts/measure_launch.sh` went **199 → 66 ms**, moving
FirstFrame from 1 ms under its 250 ms budget to 184 ms under it.

The captured PNG is **byte-identical** across both arms (md5
`0cda24ab722ee0af5c4d0292c6edc2a8`, 1100×760), so nothing about what the capture
sees changed.

**Why the win is 140 ms and not the 21 ms of sleep that was removed.** On a box
at load 28, `sleep(8ms)` does not cost 8 ms. It costs 8 ms *plus* the wait to be
rescheduled behind every other runnable thread. Removing three sleeps removes
three reschedules. On an idle machine this fix would be worth about 21 ms; it is
worth 140 ms **exactly when the machine is busy, which is when the gate flakes.**

### The windowed number is a different number, and it is not ours

`HANABI_STARTUP_PROF=1 HANABI_QUIT_AFTER_FIRST_FRAME=1` on a real windowed cold
launch:

| | run 1 (cold) | run 2 | run 3 |
|---|---|---|---|
| `Gfx init` (window + GPU/Metal — vendored/OS) | 229 ms | 171 ms | 187 ms |
| `App init` (preload + state + systems — **ours**) | 38 ms | **1 ms** | **1 ms** |
| WindowedFirstFrame | 309 ms | 203 ms | 218 ms |

**Quoting discipline:** the gate's 249 ms is the **headless** FirstFrame; the
203–309 ms above is the **windowed** one. They measure different things (no
Cocoa window, no on-screen swap, different Metal pipeline warm-up) and must
never be compared to each other. The fix in this branch moves the **headless**
number, which is the one the gate reads.

The windowed launch is 84–89% `Gfx init`, which is OS/Metal window creation
inside the vendored backend. Hanabi's own contribution to a warm windowed launch
is **1 ms**. There is no bite to take out of it. The one genuinely cold run
(first launch after a build) shows 38 ms of App init — dyld, the page cache and
Metal's shader cache warming — and that is also not ours.

---

## 2. Persistence — mostly a negative result

**What I wanted.** `Settings::is_starred` is a linear scan, `set_starred` writes
the whole JSON file on every toggle, and `apply_local_overlays` calls three of
these per session on every list fetch. That is O(sessions × starred) and a full
serialise per star. It looks indefensible.

**What happened.** At any realistic size it is free, and the obvious fix would
have been a fix to nothing.

| catalog | starred | `apply_local_overlays` | `write_save_file()` | one star toggle |
|---|---|---|---|---|
| 2020 | 50 | 0.20 ms | 0.22 ms | ~0.19 ms |
| 2020 | 500 | 1.75 ms | 0.24 ms | ~0.24 ms |
| 2020 | 2000 | 3.78 ms | 0.52 ms | ~0.50 ms |

**The debounce is not worth writing.** A star toggle costs **0.19 ms** at a
realistic starred count. Starring ten threads in a row costs 1.9 ms in total —
less than one frame at 120 Hz. Set against that: a deferred write is a write
that can be lost, `set_starred`'s comment promises "a star survives relaunch",
and a crash window buys a data-loss bug in exchange for two milliseconds
nobody can perceive. **Measured as noise; not changed, deliberately.**

`write_save_file()` is also **flat in the catalog size** — 0.21 ms at 20
sessions and 0.21 ms at 2000 — because it serialises the settings, not the
sessions. It only grows with how much the user has starred/muted/read, and even
at 2000 starred it is half a millisecond.

The quadratic in `apply_local_overlays` is real but only bites at a starred set
in the high hundreds, which is a user who has starred a quarter of their
catalog. At 50 starred it is 0.20 ms per fetch.

**A caveat this table cannot see.** The first version of this benchmark reported
`write_save_file()` at 0.187 ms **without ever opening a file**. `files::init()`
had not run, `get_settings_path()` returned empty, the `ofstream` silently
failed, and the number that came back was the JSON build with the I/O missing.
That is exactly the shape of a fake green: it is fast, it is plausible, and it
is measuring nothing. The bench now calls `files::init()` before it measures
anything.

---

## 3. The data layer

### 3a. The mock rebuilt the whole catalog to answer "how many sessions?"

**What I wanted.** To know how often `seed()` runs, since it builds the entire
fixture as a `std::vector<Session>` by value.

**What happened.** Four call sites — `list_sessions()`, `get_session()`,
`get_settings()` and `find_mutable()` — and three of them threw almost all of it
away. `get_settings()` built every Session, every Message and every string in
the catalog **to read `.size()` off it**. `get_session()` built the whole thing
to find one row, which is what a sidebar click does.

| call | before | after |
|---|---|---|
| `get_session(one id)` | 6.626 ms | **0.007 ms** (947×) |
| `get_settings()` | 6.562 ms | **0.000 ms** |
| `list_sessions()` | 6.954 ms | **0.327 ms** (21×) |

Built once into a function-local static and handed out by const reference.
`fill_sub_agent_counts` folded in at build time — it derives from the row's own
`sub_agents`, so it was recomputing a constant on every call as well.

**The memory cost, measured rather than assumed: zero.** Peak RSS is 69 MB at
2000 sessions either way (50 vs 49 MB at 20). The catalog is retained now, but
the transient peak of the old by-value build was already the same size.

The cache is keyed on `HANABI_STRESS_SESSIONS` so a test that changes the
catalog size mid-process gets a rebuild rather than silently keeping the first
size it ever asked for.

### 3b. `trim_to_cap` scans the whole cache directory after every save

**Yes, it does.** `trim_to_cap` calls `total_bytes()` **first**, before it knows
whether anything needs evicting, and `total_bytes()` walks the cache directory
with a `file_size()` stat per entry. The default cap is 1 GiB, so on essentially
every save the answer is "under cap, nothing to do" and the entire walk was
pure cost.

| cache files | `total_bytes()` | `trim_to_cap(1 GiB)`, under cap |
|---|---|---|
| 200 | 0.630 ms | 0.674 ms |
| 2000 | 6.408 ms | 5.904 ms |

Linear at ~3.2 µs/file. Every transcript save — which is every thread opened on
a real backend — pays this.

**This number was also wrong the first time.** The bench wrote its fixture files
to `$HOME/Library/Application Support/hanabi/cache`; `disk_cache::cache_dir()`
actually resolves to `$HOME/.config/hanabi/cache`. It measured an empty
directory and reported **0.003 ms for a 2000-file scan** — 2000 `stat()` calls
in three microseconds, which is impossible. The bench now sets
`HANABI_CACHE_DIR` and **asserts `cache_dir()` agrees before measuring
anything**, and refuses to print a number if it does not.

### 3c. Every websocket frame is parsed, dumped, and parsed again

**What I wanted.** Per-delta allocation in a stream that arrives at token rate.

**What happened.** Found it in one line. The receive loop has already parsed
each frame into a `json` object, and then calls:

```cpp
const agentcloud::LiveFrame lf = agentcloud::classify_live_frame(msg.dump());
```

`classify_live_frame` takes a `std::string` and immediately does
`json::parse(msg_json, ...)`. So every frame on the wire is **parse → dump →
parse**: a full serialisation of the object back to text, and a full re-parse of
that text, to reach fields the caller was already holding.

| | per burst of 5000 frames | per frame |
|---|---|---|
| `dump()` + re-parse | 11.6 ms | 2.3 µs |
| read the already-parsed object | 0.096 ms | 0.02 µs |

**89–121× the cost of the direct read**, and it is entirely avoidable — the
fix is an overload taking `const json&`, with the existing string overload kept
as a thin wrapper so `tests/unit/test_agentcloud.cpp` (19 call sites) does not
change at all.

Honest scale: at a human token rate this is ~0.1 ms/sec, which nobody would
feel. It matters in a burst — a fast replay or an attach that dumps a backlog —
and it costs nothing to remove.

### 3d. The search index — a negative result

**When is it built, and is it rebuilt per keystroke?** Built **once per
opening** of the palette, not per keystroke; `indexed_` guards it and typing
re-queries the in-memory index. The comment above it is accurate. Querying it
costs **0.004 ms** at 2020 documents.

Two things worth noting anyway, neither a bug today:

- `index_.query()` runs on **every frame** the panel is open, not just on a
  keystroke — but at 0.004 ms that is 0.5% of a 120 Hz frame budget, and it is
  not worth caching.
- `build_index()` calls `disk_cache::load_transcript()` for every session that
  is not in the in-memory LRU. At 2000 sessions that is up to 2000 file reads
  and JSON parses on the single frame the palette opens. The benchmark above
  measures the title-and-preview path only (**0.433 ms at 2020 docs**); the
  disk-backed path is bounded by how many transcripts the user has actually
  opened, which on a real backend grows without limit. **Not measured against a
  full disk cache, and not fixed — flagged here as the next thing to look at.**
  `search::Index` also stores a fully lower-cased copy of every body alongside
  the original, so a full index costs roughly 2× the corpus in memory.

---

## Summary — what moved, and what did not

**Measured wins**

| change | before | after |
|---|---|---|
| headless FirstFrame (p50, interleaved A/B) | 220 ms | **80 ms** |
| `scripts/measure_launch.sh` FirstFrame | 199 ms | **59–66 ms** |
| `trim_to_cap` under cap @2000 cache files | 4.979 ms | **0.001 ms** |
| `apply_local_overlays` @2000 starred | 3.755 ms | **0.048 ms** |
| stream frame classification | 2.3 µs | **0.02 µs** |
| `get_session()` @2020 rows | 6.626 ms | **0.007 ms** |
| `get_settings()` @2020 rows | 6.562 ms | **0.000 ms** |
| `list_sessions()` @2020 rows | 6.954 ms | **0.327 ms** |

**Measured as noise, and deliberately left alone**

- **Two of the four startup font faces load the same file.** `DEFAULT_FONT` and
  `SYMBOL_FONT` are both `Roboto-Regular.ttf`, and afterhours' `load_font` does
  not dedupe by path — it calls `load_font_from_file` every time — so the face
  really is read and rasterised twice. It costs about **60 µs**: all four loads
  plus the UI plugin init together are 275–299 µs. The fix would be to hand the
  second name the already-loaded `Font` handle rather than the path, which
  means two names sharing one atlas, and that is a lifetime question worth more
  than 60 µs of risk. Left alone knowingly.

- Star toggle / `write_save_file()` — 0.19 ms. A debounce would trade a
  data-loss window for two milliseconds nobody can perceive.
- `apply_local_overlays` at a realistic starred count — 0.20 ms per fetch.
- `index.query()` per frame — 0.004 ms.
- Hanabi's own launch init (fonts, atlas, settings, client) — **~1 ms total.**
  Every suspect on the original list was wrong.

**Found, measured, not fixed here**

- `build_index()`'s unbounded disk reads on palette open (3d above).
- **`trim_to_cap` can return with the cache still over its cap.** Every
  transcript is trimmed to `keep_tail` messages, and once they all are there is
  nothing left to reclaim, so the loop exits over cap — 2,802,000 B against a
  2,690,500 B cap in the bench. **Pre-existing**, not caused by anything here:
  the unmodified `disk_cache.cpp` produces that number to the byte, which is
  how I know. Worth its own fix.

## The two fixes made after the first draft of this document

### `trim_to_cap` no longer scans the directory to say "nothing to do"

`trim_to_cap` now keeps a **deliberately conservative** running estimate of the
cache size: it adds every byte written and never subtracts one. Overwriting a
large transcript with a small one, or removing a file outside a trim, both make
the real total *smaller* than the estimate — so "estimate ≤ cap" **proves**
"real ≤ cap", and a trim that is genuinely due can never be skipped. Being
wrong the other way just costs a scan, which is the old behaviour.

| | before | after |
|---|---|---|
| `trim_to_cap(1 GiB)`, under cap, 2000 files | 4.979 ms | **0.001 ms** |

Invalidated on `wipe_all()` and `set_namespace()` (a different namespace is a
different directory), refreshed after any eviction, and force-rescanned every
64 calls so drift self-corrects. **Eviction is unchanged, verified against the
base file rather than assumed:** both arms free 7,960,000 B and leave
2,802,000 B, identical to the byte.

### The stream stopped parsing every frame twice

`classify_live_frame_parsed(const json&)` is now what the socket calls.
Deliberately **not** an overload: `nlohmann::json` converts implicitly from a
string literal, so adding `classify_live_frame(const json&)` beside the string
one made `classify_live_frame("")` ambiguous and broke four existing call sites
immediately. Two names, no trap.

It is published rather than `.cpp`-private specifically so the path production
takes is a path a test can drive — otherwise this change would have moved the
socket onto an untested function while the entire string-driven suite stayed
green.

### The persistence quadratic, removed on principle

`is_starred`/`is_muted` now answer from an `unordered_set` beside the ordered
vector:

| starred | before | after |
|---|---|---|
| 50 | 0.197 ms | 0.044 ms |
| 500 | 1.645 ms | 0.053 ms |
| 2000 | 3.755 ms | **0.048 ms** |

Flat in the starred count instead of multiplying by it. **This is still a
quadratic removed because it was free to remove, not a fix for something anyone
was feeling** — at 50 starred the old code cost 0.197 ms per fetch.

Two structures holding one fact is a desync bug waiting to happen, and in a
settings store the failure is silent and eats user data. `test_settings.cpp`
gained `test_star_and_mute_index_agrees_with_the_stored_list`, which walks both
structures in both directions after starring, unstarring, re-starring **and**
a reload.

## On proving a test fails without its fix

All three new tests were neutered and watched go red. Two attempts that did
**not** discriminate are worth recording, because "the suite went red" is not
the same as "my test went red":

1. **Removing the retract check from the classifier** failed the *existing*
   test at line 453, not the new one — the string path delegates to the parsed
   path, so breaking one breaks both.
2. **Removing the `is_object()` guard** failed *nothing*. `str_or`/`obj_at` are
   defensive, so the guard is redundant for a null or an array, and the test's
   null/array cases could not see its absence.

The neuter that worked was the one that reproduced the failure the test
actually guards: the string path keeping its own correct retract handling while
the parsed path — the socket's — loses it. Only the new test failed.

The same applies in `test_settings.cpp`: under the neuter (dropping
`starred_set_.erase`), **zero pre-existing tests failed.** Every existing
star/mute test calls `load_save_file()` before asserting, and a reload rebuilds
the index from the file, repairing the desync before anything looks at it. The
new test asserts *before* the reload, which is the only place the bug is
visible.

---

## A flake I nearly reported as a regression

A suite run came back with `thinking_disclosure` and `tool_fold_persists`
failing, having been 85/85 green an hour earlier. Both are driven entirely by
one of the environment variables the mock fixture reads, and I had just put a
**cache** in front of that fixture. It was an excellent suspect. I wrote the
fix — key the cache on all nine env vars rather than one — saw both tests pass,
and was one keystroke from reporting it as the cause.

It was not the cause.

- `scripts/run_ui_tests.sh` runs each script as its **own process** with its own
  env, so the cache cannot leak across scripts there at all.
- Rebuilt the **committed, single-key** version and re-ran both tests: `rc=0`,
  both.
- Ran each five more times on that same binary: **10/10 pass**, at load average
  11. The suite that failed them ran at load ~28.
- Both failure logs show `[TIMEOUT] expect_text` with only the sidebar
  painted — the async fetch had not landed inside the harness's window.

They are load-induced flakes. The cache-key change stays, because
`build_seed()` really does read nine environment variables and keying on one
is wrong, and `main.cpp` really does support `--e2e <directory>` which runs
many scripts in one process — but it is **defence, not a fix**, and the commit
says so.

**The thing worth someone's attention.** The e2e settle in `run_e2e` gates on a
**frame count** (`for i in 0..300`), while the headless capture path gates on
**wall clock** and carries a comment explaining exactly why:

> IMPORTANT: gate on WALL-CLOCK time, not a frame count. Headless frames don't
> sleep to target_fps — the loop runs as fast as the CPU allows, so a fixed
> frame budget elapses in a few milliseconds, far short of the real network
> fetch.

Two settle loops, one lesson learned, applied to one of them. Under load the
main thread can burn 300 headless frames in milliseconds while the worker doing
`list_sessions()` is starved, and the script proceeds against an unpopulated
pane. **I did not change it**, for the reason this whole document is written
the way it is: I could not reproduce the failure, so I could not measure a fix,
and a fix nobody can measure is the thing this branch exists to argue against.
The harness's own `settle finished with no transcript loaded` warning did not
fire in either log, so the frame-count settle is a *suspect with a motive*, not
a diagnosis.

---

## Suite status on this branch

`make test`: **19/19 unit**, **84/85 scripted**, launch gate **PASS**,
transcript slope gate **PASS**, both source checks pass.

The one scripted failure is `select_word_and_line`, and it is **not from this
branch**. Proven rather than asserted: `git checkout 73f7613 -- src/ tests/`
(the exact commit this branch was cut from, none of my changes present),
rebuild, same solo invocation — **it fails there too**. It also fails on
current `main`, which has since advanced past my branch point. Its symptom is
the same one the flakes showed: `[TIMEOUT] expect_text` with only the sidebar
painted, i.e. the restored tab's transcript never arrived.

Two other scripted tests, `thinking_disclosure` and `tool_fold_persists`,
failed one run at load ~28 and pass 10/10 at load 11 — see the section above
for why I am confident those are flakes and not the fixture cache.

**Final launch numbers on this branch**, ten consecutive headless runs at load
average 23 (the gate reads best-of-six, so it sees the low end):

    60 63 71 79 88 101 104 107 127 199   ms

against a **249 ms** reading at the start of the night and a 250 ms budget. The
spread is the machine, not the app — which is the whole reason the A/B in
section 1 was run interleaved rather than as two batches.
