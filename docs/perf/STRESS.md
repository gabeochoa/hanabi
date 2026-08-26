# Stress: making the freeze happen on demand

**Why this file exists.** `docs/perf/GATES.md` describes the gates that watch
for a slope; this one describes the thing that *drives the app while they
watch*. It is the half of the harness a person actually types at: thirteen
scenarios, a way to run one until it breaks, and a report you can `diff`.

Everything measured on `gabeochoa-mac-GRQ7Y259H4`, 2026-08-25, against
`main` at `e391f61` plus this branch. That box is shared with three other
agents; load averages during the samples ranged from 5 to 27, and several of
the numbers are ratios for exactly that reason.

Puffin is the reference implementation
(`fbobjc/Apps/Internal/Puffin/Sources/App/StressDriver.swift`,
`Tests/StressFixtures.swift`, `scripts/perf-gate.sh`) and most of what is
right here was taken from it rather than invented. Where hanabi differs, the
difference is stated.

---

## The one-line versions

```bash
make soak                # every scenario, four at a time            ~53 s
make soak-report         # the same, as a diff against a baseline    ~60 s
make stress SCENARIO=churn FRAMES=5000 SESSIONS=500
make stress-break UNTIL=cpu:3.0 SESSIONS=2000
make soak-gate           # the short gate, also inside `make test`   ~3 s
```

---

## The scenarios

`HANABI_STRESS=<name>`, read once at startup, a hard no-op when unset. Each
one drives the app's own request flags — the same ones a click sets — between
`sm.run()` calls, so what it writes is read by the next frame exactly as a
real input would be.

| name | what it drives | why it is here |
| --- | --- | --- |
| `idle` | nothing | the control. Growth here is growth for no reason at all |
| `scroll` | the sidebar's wheel, 60 frames down and 60 up | the clip and layout path — but see `docs/perf/SCROLL.md` on what it is *not* |
| `scrollall` | the whole catalog, after expanding the list | the reported symptom, which is the other list (perf/scroll) |
| `read` | the transcript's wheel | the pane with the genuinely large content |
| `threads` | one thread open every 30 frames | the heaviest single thing the app does |
| `tabs` | round-robin between previews | **does not accumulate tabs** — see below |
| `search` | type a query, hold it, clear it | the hold is the point; see below |
| `open` | every thread as a KEPT tab, never closed | "open every thread until it breaks" |
| `resize` | the window narrower and wider, one step a frame | layout only by default; see gap #200 |
| `churn` | open a thread, leave it, close it, open the next | the motion that found five unbounded per-session maps |
| `mixed` | all of the above interleaved | the only arm that resembles use |
| `views` | Home / Blocked / Review / Starred / Archived / a thread, on a cycle | the only one that CHANGES SCREEN, which is why #115 lived a month |
| `digest` | Blocked, swept end to end at 96 px a frame | the screen whose job is to show everything; the biggest card list |
| `bigidle` | `idle` against a 2000-session catalog | a per-row leak is 100x more visible. **Not a `HANABI_STRESS` value** — it is a `soak.sh` ARM (`scenario="idle"; sessions=2000`); `HANABI_STRESS=bigidle` parses to `Scenario::None` |

Knobs: `HANABI_STRESS_FRAMES`, `HANABI_STRESS_SETTLE` (120),
`HANABI_STRESS_TABS` (8), `HANABI_STRESS_EVERY`, `HANABI_STRESS_SESSIONS`,
`HANABI_STRESS_RESIZE_BACKEND`.

Thirteen arms, four at a time, **53 seconds** for the lot.

### Every scenario terminates on a COUNT

Puffin's rule, and it is right: *"a count is the same amount of work on a fast
machine and a slow one, so two runs can be compared; a wall-clock window
measures how much a machine got through, which is the question already being
asked."* Nothing here is bounded by a duration.

### `tabs` has never opened more than one tab

`app.requestOpenTab` is the flag a sidebar row click raises, and
`TabFlowSystem` consumes it with `keep=false` — a PREVIEW, which by design
reuses the one preview slot rather than making a new tab. So the arm that
`GATES.md` described as catching *"anything the tab strip or a per-tab cache
holds on to"* was re-pointing a single tab, and had been since it was written.

It was invisible because nothing reported what the scenario did. Now
everything does:

```
tabs     threads_opened=70   kept_tabs=0     tabs_now=1
threads  threads_opened=40   kept_tabs=0     tabs_now=1
open     threads_opened=100  kept_tabs=100   tabs_now=100
```

`open` promotes each tab the way a person's second click does: raising the
same id again finds the tab in `tabOrder` and `keep_tab`s it, which is
literally what `tab_bar_system.h` does on *"a click on the tab you are already
reading"*. Shipped path, no product change.

`tabs` is kept, because a preview being re-pointed sixty times is a real
gesture — clicking down a list is exactly that — it is just not the gesture
its name suggests.

### A scenario that drove nothing shouts

Ported from Puffin's `StressDriver.reportLines`, whose comment is the whole
argument: a run that measured nothing *"produced them SILENTLY: the same
defect as kt-4ond, where a probe with no call site answered 'clean' to a
question it had not measured. A run that measured nothing has to say so louder
than a run that measured something."*

An arm that drove nothing is the flattest run anybody ever took. Every run
prints its counts, pass or fail, and `scripts/soak.sh` treats
`DROVE-NOTHING` as its own outcome alongside PASS / FAIL / INCOMPLETE.

The counts come from the strip's own `tabOrder`, never from the driver's
belief about what it did. That is not pedantry: the first version of `churn`
reported `churn_cycles=100` for a run that closed zero tabs, because it looked
the strip up with `EntityHelper::get_singleton_cmp`, which returns null for it
— silently.

### Why `search` holds the query

A filter that re-derives itself every frame costs the same whether or not the
query changed, and the frames where nothing is being typed are most of the
frames a person spends looking at their own search results. A scenario that
only ever typed would measure the keystroke and miss the pause.

### A slope cannot see a defect that was always there

The single most useful thing to know about everything in this file. `perf/scroll`
found a bug that a trend-only verdict passed cleanly: 17.040 ms then 17.326 ms
across the run — flat, and terrible. A slope is a derivative, and a derivative
of a constant is zero however awful the constant is.

So every arm is read three ways, and they fail on different things:

| reading | what it is | catches | blind to |
| --- | --- | --- | --- |
| `verdict()` | Theil-Sen SLOPE over every bucket | a leak — cumulative, monotone | anything bad from frame one |
| `trend_verdict()` | RATIO of the two halves' minima | a cost that arrived and stayed | a slow steady leak |
| the diffable report | LEVEL, exact, no threshold | anything that changes the tree's shape | anything that costs time without costing widgets |

The third is the one that needs saying, because it does not look like a gate.
`entities_end 268` and `widget.digest_card 20` are absolute counts with no
budget anywhere near them, and a defect that is bad on frame one changes them
on frame one. `make scroll-gate`'s entity arm and `make scaling-gate`'s widget
arm are the same idea with a ratio instead of a baseline.

**Pair every trend arm with a level arm.** That is the rule the scroll work
arrived at independently, and it is why this branch's report holds exact
counts alongside banded measurements rather than banding everything.

---

## Run until it breaks

```bash
HANABI_STRESS_UNTIL="rss:600000,blocks:400000,cpu:3.0"
HANABI_STRESS_UNTIL_SUSTAIN=30      # frames a condition must hold (default 30)
```

`rss` / `heap` / `blocks` / `entities` / `tabs` take an absolute ceiling;
`cpu` takes a **ratio** against the settled baseline.

**The asymmetry is the repo rule.** Never gate on absolute milliseconds on a
box whose load average has hit 29. A byte is a byte whoever else is running,
so a memory ceiling means the same thing loaded or quiet. A millisecond does
not, and a ratio against a baseline measured on the same machine in the same
minute divides the machine out.

The baseline is the **median** frame CPU over the settle pass. Median, because
the settle carries the launch burst and one 40 ms frame in a mean puts the
baseline somewhere no later frame can reach — which disarms the condition
without saying so. From the settle and not from the measured run, because a
baseline taken while the scenario drives rises with whatever the scenario
costs and never trips.

**A condition has to hold for 30 frames.** The first version tripped on any
single frame and `cpu:3.0` fired at frame 1 of 30000 — one 4.6 ms frame
against a 1.4 ms baseline, which is a tab opening, not a freeze. Half a second
of the app being that slow is what the bug report describes.

```
$ make stress-break UNTIL=cpu:3.0 SESSIONS=2000

  [soak] frame 2000  2.885 cpu  RSS 56544 KB  entities 1367
  [soak] frame 3000  3.154 cpu  RSS 57184 KB  entities 1659
  [soak] frame 4000  3.352 cpu  RSS 57712 KB  entities 1915
  [soak] frame 4884  3.733 cpu  RSS 58416 KB  entities 2140
  [break] BROKE at frame 4884 of 30000, on `cpu`, after holding for 30
  [break] consecutive frames.
  [break]   a frame cost 5.882 ms of thread CPU, 4.23x the settled baseline
  [break]   of 1.391 ms (ceiling 3.00x)
  [break] State: 245 tabs, 2140 entities, 58416 KB RSS.
```

**The app opens 245 threads before a frame costs three times what it did at
rest, and it degrades monotonically the whole way.** That is the first number
in this project that answers the reported symptom in its own terms.

A run that did not break reports `SURVIVED`, and says in as many words that
this is not a pass: a ceiling nothing came near is a condition that was never
tested. An unrecognised key prints `UNKNOWN CONDITION` rather than arming
nothing quietly.

---

## The diffable report

```bash
make soak-report      # runs every arm, diffs against docs/perf/soak-baseline.txt
make soak-baseline    # regenerate it; commit the result with a reason
```

A regression should be a text diff. That only works if a clean run writes the
same bytes every time, so the report carries two kinds of line and treats them
completely differently.

**The deterministic ones, exact.** Entity count, widget count broken down by
the debug name that built it, tabs open, and what the scenario drove. These
are properties of the tree and the script, not of the machine —
`scaling_gate.sh` measured "348 and 2985 widgets, exactly, every time" over
five runs under load 20. A regression that adds a widget per row is one
changed line, **caught with no threshold at all**, which is the only kind of
gate that cannot drift.

**The measured ones, banded** to `ok` / `OVER` / `OVER_2x` / `OVER_5x` /
`OVER_10x`. Coarse on purpose: "+42.3 KB" would make every run differ from
every other and the diff would be noise.

A 60-row catalog added, as a diff:

```
< entities_end 268          < widget.digest_card 20
> entities_end 406          > widget.digest_card 49
```

The pool-less binary, as a diff:

```
< band.rss ok               < verdict PASS
> band.rss OVER_10x         > verdict FAIL
```

### The pinned clock this needed

Two runs a minute apart used to differ:

```
< open widget.date_divider 2      < churn widget.date_divider 2
> open widget.date_divider 1      > churn widget.date_divider 1
```

Every mock timestamp is `now - N` so the ages the rows show stay constant —
right for a screenshot baseline, wrong for a diff, because a message twelve
hours old lands on a different calendar DAY depending on what time of night
the run happened. `HANABI_MOCK_NOW=<epoch>` pins `api::mock_now()`. Unset by
default, so captures and baselines are untouched; the soak scripts set it,
because wanting two runs to be comparable is a different thing from wanting
them to look right. Two full 11-arm runs 65 seconds apart are byte-identical.

---

## The mock, against Puffin's

Puffin runs its own harness with `-mockBackend YES` and its PERFORMANCE.md
opens on what that cost it: every number it had came from a mock with "20
fixture rows" that "hides every problem below". hanabi had the same shape of
problem one level in.

**Closed.** `HANABI_STRESS_SESSIONS=<n>` generated one user line and one
paragraph of prose per turn. The hand-written twenty carry tool rows with real
output, thinking rows, code fences, failed runs and sub-agent deliveries; the
synthetic two thousand carried none of it, so every big-catalog measurement
ever taken scaled up the part of a transcript that costs nothing. A turn is
now the ask, a folded thinking row, a `Role::Tool` row with
`tool_result`/`tool_status`/`tool_duration_ms`/`tool_node`, a reply with a
fenced code block every third turn and a `run_outcome` that draws a rule, and
a sub-agent delivery every fourth turn — with a failure every seventh, which
is Puffin's ratio. Deterministic from `(session, turn)` alone.

Cost, A/B/A/B interleaved, warm rounds: **2.373 / 2.318 ms a frame before,
2.693 / 2.664 after** — +14% for 2.1x the rows. The percentage is the smaller
half; the larger half is that the tool-row renderer, the fold machinery, the
code-fence path and the failed-run rule were not being walked at all.

**Still open, and why.**

- **No latency.** hanabi's mock returns a transcript synchronously and
  instantly. A real fetch is 50–500 ms, so nothing in a stress run exercises
  the loading state, two fetches overlapping, or a tab opened while the
  previous one is still in flight — which is where a real user finds a stall.
  Not closed here because a `sleep` in `get_session` would land on whichever
  thread called it, and establishing that the call is always off the UI thread
  is a bigger read than this branch had room for. It is the single highest
  value thing left in the mock.
- **No streaming.** Puffin's fixtures generate `blockDelta` frames and its
  `FreezeRepro --live` arm streams a reply in after the window is up, with the
  comment that every wild capture of its freeze was taken *with a run live*.
  hanabi's stress catalog is entirely at rest. A leak in the streaming path is
  unreachable from any arm here.
- **No failure injection.** The mock never returns an error from
  `list_sessions` or `get_session`, so the error path and the retry are
  unmeasured.

---

## What is still not gated, and why

Read `docs/perf/GATES.md`'s own section of this name first; these are the ones
this branch added or changed.

### The resize arm can only measure half a resize

`afterhours_gaps.md` **#200**: the headless backend honours a resize by
destroying and recreating the offscreen render target, and
`load_render_texture` → `sgl_make_context` creates five Metal render pipelines
that `sgl_destroy_context` does not release. **4.8 MB per 1000 frames, 18.7 MB
a minute** — twice the autorelease leak that started this project. Named with
`MallocStackLogging` + `malloc_history`: 40510 live `_sg_init_pipeline`
allocations over 8103 resizes, exactly five each.

It is the HEADLESS branch only; a real window resize goes through Cocoa and
never touches the offscreen target. So it is a harness leak rather than a user
one — and it still costs, because it makes the one arm that could have gated a
user-facing resize leak unable to gate anything.

`HANABI_STRESS=resize` therefore resizes the **layout** only, which is the
half hanabi owns:

| | RSS per 1000 frames | |
| --- | ---: | --- |
| layout only (default) | +0.0 KB | PASS |
| `HANABI_STRESS_RESIZE_BACKEND=1` | +5209.6 KB | FAIL 10.2x |

The cost of the workaround: the arm draws into a render target of the wrong
size, so it can never be extended into a screenshot test, and the
render-target half of a resize stays unmeasured.

### `open` and `mixed` cannot be gated on flatness

They accumulate tabs on purpose, so growth in them is a **cost per tab** and
gating them flat would assert that opening a hundred tabs is free. They run
`REPORTED`. What makes them worth running is the diffable structure (widgets
per tab is exact) and the break conditions.

An honest gate for them would be a cost-per-tab budget — RSS delta divided by
tabs opened — and it is not here because setting one needs the number to be
stable across catalog sizes and that was not measured.

### Nothing here presses a key or opens a menu

Still true, and still the gap `GATES.md` named. `mixed` writes
`app.searchQuery` directly, which is what the text field writes, but no arm
goes through the OS event path, the menu bar, the global hotkey or the URL
handler — three of which only exist on a windowed frame.

---

## Footguns found while building this

Each of these cost real time and each will cost it again.

**A backgrounded `sleep` holds the caller's stdout, and a pipe waits for it.**
`( sleep "$RUN_TIMEOUT"; kill -9 "$APP_PID" ) &` reads as obviously correct.
`kill $WATCH_PID` kills the subshell, not the sleep it is blocked in, so the
sleep is reparented and runs to completion — with the script's stdout still
open. `make soak-gate` cost **121 seconds instead of 4** whenever its output
went through a pipe, which is every `make`, every `tee`, and every CI capture.
Invisible on a terminal. `scripts/watchdog.sh` and
`scripts/check_watchdogs.py`.

**A stale binary and a real regression look identical.** `output/hanabi.exe`
left behind by a build of a deliberately broken tree made `make soak` report
all eleven arms leaking +2816 KB per 1000 frames, in agreement to three
significant figures. Puffin's PERFORMANCE.md has the same rule from the same
mistake. `scripts/fresh.sh` warns; `HANABI_REQUIRE_FRESH=1` makes it fatal.
(The agreement is the tell — idle, scroll and resize sharing a leak to 1% is
implausible — but reading that takes knowing it.)

**Search-and-replacing a function's body into a call to itself.** Introducing
`api::mock_now()` and then replacing every `static_cast<int64_t>(std::time(nullptr))`
with `mock_now()` rewrote the new function's own fallback. Infinite recursion,
`EXC_BAD_ACCESS`, and it only crashed when `HANABI_MOCK_NOW` was UNSET —
which is every ordinary run and none of the soak scripts, so it passed every
gate that had been run on it. `lldb -b -o run -o 'bt 12'` named it in one
line.

**`find_singleton<T>()` and `EntityHelper::get_singleton_cmp<T>()` are
different lookups.** The tab strip is a component on an ordinary entity, and
the registered-singleton lookup returns null for it — silently. The first
`churn` reported 100 cycles for a run that closed zero tabs.

**Smaller buckets do not make RSS quieter.** 1000 frames in 100-frame buckets
gives more fit points and a clean RSS spread of 206.7 KB per 1000 frames,
against 0.0 at 250-frame buckets. RSS noise is a whole page arriving at once,
so a shorter window multiplies it. On that metric the way to more points is
more frames.
