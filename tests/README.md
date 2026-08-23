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
make test        # unit + e2e + scripted UI + perf micro-bench + launch/RSS gate
make unit-e2e    # unit + e2e only (no perf)
make e2e        # e2e only
make uitest      # scripted UI tests only (tests/ui/*.e2e)
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

## Current measured numbers

Headless, 1100×760, mock backend, Apple Silicon, `HANABI_FRAME_TIMING=240
HANABI_FRAME_SPLIT=1`, median of 240 frames:

| screen | widgets | frame | update (rebuild) | render (layout+draw) |
|---|---|---|---|---|
| Home digest, idle | 315 | 0.95 ms | 0.38 ms | 0.57 ms |
| a short transcript | 343 | 1.14 ms | 0.29 ms | 0.85 ms |
| 120-message transcript | 460 | 1.64 ms | 0.38 ms | 1.25 ms |

- Startup ~27 ms · FirstFrame ~223 ms (budget < 250 ms; FirstFrame is nearly
  all graphics-stack init, not ours — see gap #8)
- Peak RSS ~47 MB (headless)  (budget < 250 MB)
- Thread-switch ~0.013 ms uncached / ~0.0005 ms cached

**Those numbers are at `-O2`, which the app only started building at recently.**
Before that there was no `-O` flag in the main build at all, and the same three
screens measured 5.45 / 6.25 / 9.08 ms — the "idle frame floor" that had been
attributed to per-frame tree rebuilding was 5-6x compiler, not architecture.
`make OPT=0` reproduces the old build if you need to compare.

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

## End-to-end screenshot harness (`scripts/screens.sh`)

`bash scripts/screens.sh` captures a PNG of **every screen / notable UI state**
the headless `--screenshot` path can reach, into `/tmp/hanabi_screens/`
(override with `HANABI_SCREENS_OUT`). One command, review-them-all-at-once.
It exits non-zero if any capture is missing or not exactly 1100×760.

Per state it writes the matching `settings.json`, runs the app headless with a
per-shot timeout, `pkill`s any stray `hanabi.exe`, and verifies dimensions.

**Isolation / safety.** Each capture runs with `HOME` pointed at a throwaway
temp dir, so our `settings.json` is written and read *there* — the user's real
`~/Library/Application Support/hanabi/settings.json` is never touched by a
render. As belt-and-suspenders, the script also backs the real file up front
and restores it byte-for-byte on exit (trap), asserting an md5 match. Backend
is forced to the zero-config mock (`HANABI_BACKEND=mock`) and runtime backend
config is isolated (`HANABI_CONFIG=/tmp/none_$$`).

**States captured (29):** the Home digest and every smart view (Blocked /
Review / Starred / Archived), light and dark; the transcript on two different
threads; the chat welcome screen; the folded sidebar rail; the settings sheet,
new-task sheet and device-code login overlay; ten open tabs; the long-transcript
fixture; every tool row expanded; split view; the hover states (row star, tab,
message actions); the focused composer; and the transient states nobody sees for
long enough to review by hand — skeleton, thread-loading, load-older, thinking,
streaming.

Add a state to the list in the script rather than running the app by hand. The
knobs that make them reachable are `HANABI_VIEW`, `HANABI_TEST_OVERLAY`,
`HANABI_AUTH_DEMO`, `HANABI_EXPAND`, `HANABI_BIG_TRANSCRIPT`, `HANABI_SPLIT`,
the `*_DEMO` forcings, and the persisted `sidebar_collapsed` setting.

### Test-only hover hook (`src/test_hooks.h`)

The headless capture has no mouse, so true hover states (row star-on-hover,
tab hover) can't arise from hit-testing. `src/test_hooks.h` adds a tiny hook,
`hanabi::test_hooks::force_hover(name)`, gated **entirely** behind the
`HANABI_TEST_HOVER` env var: it returns true only when that var is set and
equals `name`, and is a **hard no-op returning false when unset** — so every
normal windowed run, every ordinary screenshot, and every test that doesn't opt
in renders byte-identically to before the hook existed (the env var is read
once and cached). It only ever turns a hover branch *on*; it never suppresses a
real hover and never mutates app/UI state. It is wired into exactly two hover
branches:

- sidebar chat row → `HANABI_TEST_HOVER=row:<sessionId>` reveals that row's
  hollow-star affordance (target an *unstarred* row, e.g. `row:t2`).
- content tab → `HANABI_TEST_HOVER=tab:<sessionId>` lights that tab's hover bg
  (target a *non-active* tab, e.g. `tab:t6`).

No `vendor/` file is modified; the hover hot-state machinery lives in vendored
afterhours (which we don't patch), so the hook is applied in *our* render code
at the point where we already branch on hover.

## Scripted UI tests (`make uitest`, `tests/ui/*.e2e`)

The checks above are headless *logic* tests: they call the shipped decision
functions directly, with no window and no widget tree. That leaves the part
users actually touch — does hovering this reveal that, does clicking here open
the right thread — asserted by nobody.

`make uitest` builds a SECOND binary of the whole app, `output/hanabi_uitest.exe`,
with afterhours' e2e input hooks compiled in (`-DAFTER_HOURS_ENABLE_E2E_TESTING`,
its own object dir, so the shipping build carries none of it). That binary takes
`--e2e <script>`: it renders the real UI headlessly, injects synthetic mouse and
keyboard into the real widget tree, and asserts against the text that actually
rendered.

**Click by NAME, not by coordinate.** `click_ui <debug_name>` finds the element
and clicks its centre, so a layout change retargets the click instead of quietly
landing on empty space — which is a passing test that exercises nothing. Raw
`click x y` is for hit-testing behaviour itself.

**A leading `# env: KEY=VAL` line** adds environment for one script. Needed for
any state a click cannot reach — an overlay whose only binding is a Cmd chord,
which the injector cannot produce (`afterhours_gaps.md` #49).

A script is a plain list of commands — `click_ui <name>`, `click x y`, `mouse_move x y`,
`scroll_wheel dx dy`, `type "…"`, `key ENTER`, `wait_frames n`,
`expect_text "…"`, `expect_no_text "…"`, `screenshot path`. A leading
`# settings: {…}` line chooses the settings.json the app starts from (open
tabs, theme, window size); the default is Home, no tabs, dark.

`scripts/run_ui_tests.sh` runs each script in its own process with a timeout, so
a hang is attributed to the script that caused it. Same isolation contract as
the screenshot harness: throwaway `HOME`, mock backend forced, runtime config
pointed at a path that does not exist.

**Two things the registry cannot see.** Text painted through `on_draw_fg`
(the search field's placeholder, every drawn glyph) never reaches it, so
assert on a real label nearby instead. And text scrolled out of the viewport is
not registered, which is usually what you want.

**Write assertions that can fail.** A row title that is also in the sidebar is
visible before and after the click that was supposed to open it — assert on
something only the new state shows. Two sharp edges in the harness are worked
around in `run_e2e` (see `src/main.cpp`) and written up in `afterhours_gaps.md`:
the runner never folds command failures into its own verdict in single-script
mode, and it declares itself finished in the same tick it dispatches the last
command, so a trailing assertion is never observed.
