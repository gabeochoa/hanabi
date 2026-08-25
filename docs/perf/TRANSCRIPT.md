# The transcript under a long thread

**Why this file exists.** The reported symptom was an app that got "slower and
slower every second until it freezes". That was a per-frame Metal leak, and it
is fixed. What the leak was hiding is that the transcript's cost was
proportional to the number of messages in the thread — not to the number on
screen — so a long conversation was permanently expensive rather than
progressively so, and nothing in the project could see it.

Written the way `docs/visual-parity/FRICTION_LOG.md` is: what was wanted, what
happened, what it cost, numbers inline. Gaps that turned out to be the
library's are `#135`–`#138` in `afterhours_gaps.md`.

**All timings here are CPU time** (`CLOCK_THREAD_CPUTIME_ID`), not wall clock.
See entry 0 for why that is not a detail.

---

## 0. Before any of it: the instrument was coarser than the bug

- **What I wanted** — to confirm the fixture comment's claim that this shape
  "made the per-frame rebuild ~15-20ms", then improve it.
- **What happened** — `HANABI_SOAK` reports one number per bucket, whole-frame
  wall-clock ms. Six consecutive buckets of *identical* work on the
  160-message fixture:

  ```
  6.748  7.546  5.160  6.662  6.561  4.990   ms/frame
  ```

  A 2.5 ms spread on a 6 ms frame. Every per-frame cost in the transcript is
  smaller than that spread, so removing one entirely would land inside the
  noise and read as a fast bucket.

  Worse, the machine was not idle: load average 29, a virus scanner at 199%
  CPU, two Xcode builds, Puffin, and Spotlight indexing. An A/B of the pre-fix
  and post-fix binaries over 12 buckets each came out with the **post-fix one
  50% slower** on the 480-message fixture (median 8.0 ms vs 10.6, best bucket
  6.8 vs 4.5) while every phase counter said its work had fallen tenfold. Wall
  clock there is not a noisy measurement of frame cost; it is a measurement of
  a different thing, and repetitions do not converge on the value you wanted.
- **Cost** — a day's worth of conclusions would have been backwards. Built
  `src/util/prof.h` first: named phase timers on `CLOCK_THREAD_CPUTIME_ID`
  (cycles this thread was actually given, so being descheduled costs nothing),
  pure counters, and a global `operator new` counter. `HANABI_BIG_TURNS=<n>`
  sizes the fixture so everything can be plotted against length rather than
  asserted at one point.
- **The rule that came out of it** — **prefer a count to a time.** Counts are
  identical to three decimals across runs on any machine at any load; the slope
  gate gates counts for exactly this reason.

---

## 1. Frame time against transcript length

`HANABI_STRESS=idle`, 1180x949, mock backend, 900 measured frames after a
120-frame settle. `before` is `ea71cbe` (instrumentation only); `after` is the
head of `perf/transcript`.

### Whole frame (CPU ms)

| messages | before | after | change |
|---:|---:|---:|---:|
| 12 | 2.82 | 2.67 | −5% |
| 48 | 4.51 | 3.45 | −24% |
| 120 | 6.00 | 3.34 | **−44%** |
| 240 | 5.67 | 3.47 | −39% |
| 480 | 8.90 | 3.71 | **−58%** |

### The transcript's own phases (CPU ms/frame)

| messages | pass1 measure | | pass2 build | | minimap | |
|---:|---:|---:|---:|---:|---:|---:|
| | before | after | before | after | before | after |
| 12 | 0.157 | **0.011** | 0.58 | 0.47 | — | 0.017 |
| 48 | 0.762 | **0.040** | 0.79 | 0.58 | — | 0.059 |
| 120 | 2.009 | **0.083** | 0.96 | 0.53 | — | 0.126 |
| 240 | 2.846 | **0.147** | 0.75 | 0.48 | — | 0.221 |
| 480 | 5.786 | **0.274** | 0.99 | 0.44 | — | 0.413 |

**Slope of the measure pass: 12.0 µs per message before, 0.56 µs after — 21×
flatter.** The `before` minimap column is empty because before this branch the
minimap was inside pass 2's timer; see entry 5.

### Text wrapping, per frame

| messages | wrap calls before | after | KB wrapped before | after |
|---:|---:|---:|---:|---:|
| 12 | 89.7 | 61.3 | 5.5 | 3.1 |
| 120 | 242.9 | 61.8 | 18.7 | 3.1 |
| 240 | 413.0 | 62.3 | 33.5 | 3.2 |
| 480 | 753.4 | **63.3** | 63.0 | **3.3** |

Before: linear. After: **flat** — the transcript wraps text for what is on
screen, which is what it should always have done.

### Allocations, per frame

| messages | before | after |
|---:|---:|---:|
| 12 | 12,221 | 9,046 |
| 120 | 30,015 | 9,565 |
| 480 | 90,023 | 11,954 |

Before is ~167 allocations **per message per frame**: a 480-message thread
allocated ninety thousand times per frame — 5.4 million times a second — to
render four visible messages.

---

## 2. Is the transcript virtualized? Yes for the build, no for the measure

- **What I wanted** — to know whether the culling (`cull`, `winTop`, `winBot`)
  skips the *layout* of off-screen messages or only their *draw*.
- **What happened** — neither, exactly, and the honest answer needed the
  timers fixed first (entry 5). The pane is two passes:

  - **Pass 1** walks *every* message and computes its height, because it
    cannot place message 61 without knowing the height of messages 1–60.
  - **Pass 2** walks the item list, accumulates off-screen items into a single
    spacer div, and builds widgets only for the visible ones.

  Pass 2 is genuinely virtualized, and measurably so: **0.47 ms/frame at 12
  messages and 0.44 at 480**, and 5,456 allocations per frame at 120 messages
  against 5,534 at 480. Flat. There is also intra-message culling inside
  `render_rich_body`, so a 260-line message off the top of the viewport builds
  a spacer, not 260 text entities.

  So the answer to "does it skip layout or only draw" is: **it skips the widget
  build entirely, which is the expensive half.** The failure was one level up.
  Pass 1 *has* to visit every message — but visiting was supposed to be a hash
  lookup, and instead it was a full re-measure, because the memo in front of it
  had a 34% hit rate.
- **Cost** — 2.0 ms/frame at 120 messages, 5.8 at 480, for work whose answer
  had not changed since the thread was opened.
- **Class** — `WORKAROUND` (fixed in hanabi; the library halves are #135, #136)

---

## 3. The cache hit rates I found

`transcript_render_cache.h` memoizes (display body, line count, measured
height) per message. Measured hit rates, idle, static transcript:

| cache | before | after |
|---|---:|---:|
| `TranscriptRenderCache` @ 60 msgs | **34.6%** | 99.8% |
| `TranscriptRenderCache` @ 120 msgs | **34.0%** | 99.9% |
| `TranscriptRenderCache` @ 480 msgs | **33.4%** | 99.6% |
| hug memo (new) | — | 36.2 hits / 0.03 misses per frame |
| afterhours `TextMeasureCache` | 100.0% | 99.9% |

**34% on a transcript that never changed.** The number that explains it is not
the miss count but the miss *breakdown*, which nothing reported until I split
it: of 72.5 misses per frame at 120 messages, **99.8% were width-stale and
0.2% were genuinely cold**.

The mechanism, in full, because it is the whole finding:

A user bubble hugs its text. afterhours cannot size a box to its content
(#79 / #87 / #103), so hanabi derives the width by measuring — and that is
inherently two passes at two widths. `user_box()` asks for the message at the
bubble's **maximum** text width (630 px on a 1180 px window) so it can wrap the
body and find its longest line; `bubble_height()` then asks for the same
message again at the **hugged** width that fell out of it (458 px). The cache
held one entry per key. So:

```
frame N:    ask 630 -> miss -> compute -> store 630
            ask 458 -> miss -> compute -> store 458   (630 evicted)
frame N+1:  ask 630 -> miss -> compute -> store 630   (458 evicted)
            ask 458 -> miss -> compute -> store 458
            ... forever, every user message
```

A cache that misses because it is cold is working. A cache that misses because
it evicted itself is **worse than no cache**, because it pays the lookup and
the recompute both, and it looks like a cache in the code review.

`TextMeasureCache` was the surprise in the other direction: 100%, 463 entries.
I expected it to thrash, because `wrap_text` measures a growing *prefix* per
word and each prefix is a distinct key — a real thread of varied prose would
blow the 4096-entry LRU. It does not here only because the fixture's text is
templated and repeats. **Do not read that 100% as safe.** And at 100% it was
still 3,456 lookups per frame — 3,456 FNV hashes over strings that had to be
built in order to be hashed.

---

## 4. Text measurement: hanabi's own measuring goes *around* the library's cache

- **What I wanted** — the app measures text constantly. afterhours ships a
  `TextMeasureCache` (LRU, 4096 default) wired up as a singleton. Send hanabi's
  measuring through it.
- **What happened** — the cached function and the callable function answer
  different questions. `theme::text_px` → `measure_text_internal` →
  `fonsTextBounds(...)`'s **return value**, the pen advance. The cached path
  `ui::measure_text_line` → `measure_text` → `(bounds[2] - bounds[0])`, the
  **ink bounding box**. Probed in-app, same string, same font, same size, same
  frame:

  ```
  [probe] uncached=440.0000 cached=442.0000  delta=+2.0000  "Follow-up question #0: can you dig into th"
  [probe] uncached=435.0000 cached=437.0000  delta=+2.0000  "Follow-up question #1: can you dig into th"
  ```

  A consistent 2 px. Switching to the cache widens every user bubble by 2 px,
  which against a frozen pixel reference is a regression, not an optimisation.
- **Cost** — the one shared cache the library provides for exactly this is
  unusable by the app that needs it most. `theme::text_px` stays uncached, so
  the fix had to be *calling it less*: 106.8 uncached fontstash calls per frame
  → 70.6, and the remainder is outside the transcript entirely (sidebar, tabs,
  composer) and flat in thread length.
- **Class** — `FOOTGUN`
- **Gap filed?** — #137.

Note that hanabi's wrapping *does* go through the cache — `wrapped_lines` →
`ui::wrap_text` → `measure_text_line` → `TextMeasureCache` — which is why that
cache reads 100% rather than 0%. It is only the app's own one-shot measures
that go around it.

---

## 5. The timer that made a virtualized pass look linear

- **What I wanted** — to know whether pass 2 was really per-visible-message.
- **What happened** — it read 0.53 ms/frame at 120 messages and 1.02 at 480,
  which is the signature of *not* being virtualized, and would have sent me
  rewriting the wrong pass. The RAII scope object was declared just before the
  item loop, so it did not end with the loop — it ended with the enclosing
  **function**, swallowing the read-mark, the scroll pin, the jump-to-bottom
  button and `minimap_rail()`.
- **Cost** — one wrong conclusion, caught only because the allocation counter
  disagreed with the timer. Scoping it to the loop and giving the minimap its
  own timer separated a flat cost from a linear one that had been hiding inside
  it.
- **Class** — `FOOTGUN` (mine, not the library's)
- **The rule** — an RAII timer needs an explicit block. A scope that ends
  "somewhere below" is a scope that measures something else.

---

## 6. What is left, and what measured as noise

**Left, and linear:** the minimap, at ~4.6 heap allocations and ~0.86 µs per
item per frame (0.13 ms and 572 allocations at 120 messages; 0.41 ms and 2,218
at 480). It draws one mark per message by design — that is what a minimap is —
so there is nothing to virtualize. Every way of making it cheaper either
deletes the feature, moves pixels (`draw_mark` clamps each dot to `kMinDotH`
and centres it, so merging two slots does not draw what two slots drew), or
re-implements per-widget hover by hand. Documented as #138 and left alone; the
slope gate's allocation limit is set at 12 per message specifically to leave
room for it, and says so.

**Left, and flat:** pass 2 at ~0.45 ms/frame and ~5,500 allocations, of which
most is afterhours' per-widget rebuild cost (#138) for the visible turns. Real,
but it does not scale with the thread, so it is a constant to attack another
day.

**Measured as noise — be blunt about these:**

- **The fixture comment's "~15-20ms".** Not reproducible. The pre-fix binary on
  the 160-message shape reads ~6 ms whole-frame wall clock and ~5.4 ms CPU. The
  Metal leak fix is the likely reason, and the comment was not updated. The
  cost was real and worth fixing; the number in the file was stale.
- **Whole-frame wall clock, on this machine, entirely.** See entry 0. Every
  headline in this document is CPU time because the wall clock produced a
  confident backwards answer.
- **`TextMeasureCache` thrash.** I predicted the 4096-entry LRU would blow on a
  120-message thread, because the greedy wrapper makes a distinct cache key per
  word. It reads 100% with 463 entries. The prediction was wrong *for this
  fixture* — the text is templated and repeats — and I would not bet on it for
  real prose, but I did not measure real prose, so it goes here rather than in
  the findings.
- **`tool_out_lines` / the tool pile heights.** Suspected as per-frame string
  work over every tool message; measured at 0.008 ms/frame and 34 calls/frame,
  under a hundredth of the measure pass. Left alone.
- **`code_block_h`, the markdown scan, the syntax highlighter.** All inside
  `rich_body_h`, which is 0.21 ms/frame over 3.4 calls/frame — i.e. it runs for
  the *visible* messages only, which is correct. The scan was on the list of
  suspects and is not a problem.
- **Folding (`first_n_lines`, `is_folded`).** A live suspect on paper — it
  re-wraps a folded body every frame, uncached. It never fires on this fixture:
  `kFoldLines` is 40 and the fixture's assistant messages are ~15 lines. It
  remains a real per-frame cost for a thread of long pastes, and it is
  unmeasured because I had no fixture that triggered it. Named here so the next
  person does not have to rediscover the suspicion.

---

## How to reproduce any of this

```bash
# One point on the curve, with the full phase + counter breakdown.
H=$(mktemp -d); mkdir -p "$H/Library/Application Support/hanabi"
echo '{"window_width":1180,"window_height":949,"open_tabs":["rbig"],"active_tab":"rbig","theme":"dark"}' \
  > "$H/Library/Application Support/hanabi/settings.json"
env HOME="$H" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
    HANABI_CONFIG=/tmp/none HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_TURNS=30 \
    HANABI_PROF=1 HANABI_SOAK=900 HANABI_STRESS=idle \
    output/hanabi.exe --screenshot /tmp/o.png 2>&1 | grep '^\[prof\]'

# The gate: same work at 60 and 480 messages, gating the DIFFERENCE.
scripts/perf_transcript_slope.sh            # runs inside `make test`

# HANABI_BIG_TURNS=<n> is n turns = 4n messages (default 40 = 160).
```
