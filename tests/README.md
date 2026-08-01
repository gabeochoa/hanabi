# hanabi tests

Headless, deterministic tests + performance-regression gates for the native
macOS client. Everything runs with **no window** and **no network** (the
zero-config `MockClient` is the default backend), so it's CI-friendly and fast.

## Run everything (one command)

```sh
scripts/run_tests.sh        # build + unit + e2e + perf + launch gate; PASS/FAIL summary
```

or via make:

```sh
make test        # unit + e2e + perf micro-bench + launch/RSS gate
make unit-e2e    # unit + e2e only (no perf)
make e2e        # e2e only
make perf        # perf micro-bench + launch/RSS gate
scripts/measure_launch.sh   # standalone launch/RSS gate (used by Phase P/X)
```

All app runs are backgrounded with a hard timeout and a guaranteed
`pkill -9 -f hanabi.exe` cleanup — no stray processes, never hangs a shell.

## What each check asserts

### 1. Unit — `tests/unit/test_api.cpp` (pre-existing)
Backend-agnostic API layer: config defaults, factory fallback to mock, mock
list sorted, mock get_session, high-signal model populated, http defaults calm.

### 2. Headless e2e — `tests/e2e/test_e2e.cpp`
Exercises the **real shipped logic** against the mock + the real afterhours ECS
core. The sidebar/main-pane/tab systems delegate their pure decisions to
`src/ecs/thread_model.h` (`ecs::model`) and `src/ecs/tab_model.h`
(`ecs::model`); the tests call those exact functions (no copies).

- **Session list** loads from the mock, sorted newest-first, expected sample
  threads (`t1`/`t4`/`t13`) present.
- **State model + glyphs**: `is_attention()` true only for `Attention`;
  `glyph_for()` precedence Blocked→triangle, Review→diamond, Done→dot, bare
  Attention→triangle, and running/parked/archived→none. Verified on the actual
  mock seed too.
- **Smart-view filtering**: Blocked = `tag==Blocked` (count 2), Review =
  `state==Ready` / agent-verified (count 2), Starred = `starred` (count 3).
- **Tab model**: opening a thread adds a tab; opening an already-open thread
  FOCUSES it (no duplicate, exactly one active); closing the active tab falls
  back to the neighbor; closing the last tab drops to Home + clears transcript.
- **Backend-agnostic defaults**: no env → `make_client()` returns mock; http
  with no base URL still falls back to mock (no crash); a default
  `SessionSummary` is calm (Unknown/None/not-starred/no folder/no glyph).
- **Transcript cache** — **PENDING (skipped)**: the LRU cache (≤20 msgs × ≤5
  threads, instant re-open, LRU eviction) is not implemented yet. The test
  asserts the *current* re-fetch behavior and prints a SKIP referencing
  **Phase X** (`docs/phased-plan.md`). It does NOT fake a pass. Flip it to a
  real cache-hit assertion when `TranscriptCache` lands.

### 3. Perf micro-benchmark — `tests/e2e/test_perf.cpp` (built `-O2`)
In-process **thread-switch latency**: times switching among 5 recently-opened
threads through today's path (model tab focus + `MockClient::get_session`, which
is what `LoaderSystem` runs on every open — there is no cache yet).
- Prints `per-switch (avg)` ms every run (trend-visible).
- Asserts a generous regression guard: **< 5.0 ms/switch** (`kSwitchCeilingMs`).
- The strict **sub-millisecond CACHED-switch** assertion is **PENDING Phase X**.

### 4. Launch + RSS gate — `scripts/measure_launch.sh`
Runs the headless one-shot render path (same state-build + first-frame path the
windowed app uses) under `/usr/bin/time -l` and parses:
- **Startup: N ms** — process start → systems ready (the app's own log).
- **FirstFrame: N ms** — process start → first frame rendered (test-only hook in
  `run_headless_screenshot`, gated on `i == 0`).
- **Peak RSS** — macOS `/usr/bin/time -l` "maximum resident set size" (bytes).

Gate (both are the project's HARD budgets, easy to tighten — two constants at
the top of the script):

| Metric      | Gate metric | Ceiling | Source doc          |
|-------------|-------------|---------|---------------------|
| Launch      | FirstFrame  | 250 ms  | phased-plan Phase P |
| Peak RSS    | RSS         | 250 MB  | phased-plan Phase X |

## Current measured numbers (cli:aspen)

- Startup ~19–28 ms · FirstFrame ~22–31 ms  (budget < 250 ms) — huge headroom
- Peak RSS ~47 MB (headless)  (budget < 250 MB)
- Thread-switch ~0.004 ms/switch (current uncached mock path)

See `todo.md` → **Perf baseline** for the dated snapshot.

## Notes on what can / can't be asserted headlessly

- **Pure logic** (state/glyph/smart-view classification) and **tab flow**
  (open/focus/close over the entity system) are fully testable headlessly —
  they were extracted into graphics-free headers so the tested code IS shipped.
- **Rendering** (actual pixels, layout geometry, hit-testing) needs the Metal
  backend + a window, so it is NOT asserted here; it's covered by the
  `--screenshot` smoke path (valid 1100×760 PNG, exit 0) and per-phase visual
  validation in `docs/phased-plan.md`.
- No `vendor/` (afterhours) file is modified by any test; missing capabilities
  are logged in `afterhours_gaps.md`.
