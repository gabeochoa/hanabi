# Text: measuring it, wrapping it, cutting it

**Why this file exists.** `docs/perf/TRANSCRIPT.md` fixed the transcript's
measure pass and left a sentence in entry 3 that turned out to be the whole of
this branch:

> `TextMeasureCache` was the surprise in the other direction: 100%, 463
> entries. […] **Do not read that 100% as safe.** And at 100% it was still
> 3,456 lookups per frame — 3,456 FNV hashes over strings that had to be built
> in order to be hashed.

A cache at a 100% hit rate is not the same thing as work not being done. The
transcript's own memo had been fixed, so nothing *re-measured* a message — but
the render pass still asked "how many lines is this paragraph?" about every
visible paragraph on every frame, and the only way to ask cost a wrap.

Written the way `docs/visual-parity/FRICTION_LOG.md` is: what was wanted, what
happened, what it cost, numbers inline. Library halves are `#135`, `#136`,
`#137` and `#116`, plus `#190`–`#192` filed here.

**Every headline is a COUNT.** This box is shared with several other agents
and its load average has been observed at 29; `docs/perf/GATES.md` records an
A/B on it that came out backwards. The counts below repeat to the call between
runs — verified by running each measurement twice and diffing the output — and
that is what makes them assertable in a gate.

---

## The headline

`scripts/perf_text.sh`, 900 frames per scenario, 1180x949, mock backend.
`before` is `main` at `1abdaa3` (this branch is rebased onto it, so both
columns are the same base plus or minus this branch alone); `after` is the
head of `perf/text`.

| | idle | | read (scrolling) | | rows (2000 sessions) | |
|---|---:|---:|---:|---:|---:|---:|
| | before | after | before | after | before | after |
| text measurements / frame | 492.6 | **5.3** | 1315.1 | **10.6** | 171.1 | **2.5** |
| line breaks worked out / frame | 61.8 | **0.14** | 169.8 | **0.14** | 20.4 | **0.01** |
| allocations / frame | 9,320 | **5,744** | 19,032 | **9,235** | 4,182 | **3,006** |
| frame CPU (ms) | 3.19 | 2.55 | 4.05 | 3.49 | 1.61 | 1.53 |

"Text measurements" is every `TextMeasureCache` lookup (hits included) plus
every uncached `theme::text_px` call: the two ways this app asks a font how
wide something is. **99% of them are gone.** "Line breaks worked out" is wraps
plus line-count computations — the same question the old `wrap_text` counter
asked, now that the work goes through two functions instead of one. Frame CPU
is in the table for scale and is not a claim: the spread between three
consecutive identical runs on this box is larger than most of those deltas,
while the counts repeat to the call.

Per-operation numbers, which are the honest form for the paths that are
memoized:

| | before | after |
|---|---:|---:|
| measure calls per ellipsized title (cold) | 8.10 | **5.25** |
| allocations per bubble hug (cold) | — | **−133.6** |
| uncached fontstash calls / frame | 18.3 | **0.1** |

---

## 1. The cache with a 100% hit rate that was the problem

- **What I wanted** — to find what was left after the transcript work.
- **What happened** — 61.8 wraps per frame on a transcript standing still, and
  the memo in front of the measure pass reading 99.9%. Both true. The wraps
  were the RENDER pass: `render_rich_body` calls `count_lines(ip.visible,
  textW)` for every visible paragraph, every frame, to size a box whose height
  cannot change unless the text or the width does. The measure pass had a memo
  from the transcript branch. The render pass never did, and nothing
  distinguished them, because "wrap calls per frame" was one number.
- **Cost** — 0.24 ms/frame and 3.2 KB of text wrapped per frame at 120
  messages; 0.57 ms and 8.3 KB while scrolling.
- **Fix** — a bounded LRU keyed by (text, width, font size), 512 entries,
  `src/util/text_cache.h`. 61.6 hits and 0.1 misses per idle frame.
- **Class** — `WORKAROUND` (the library half is #135)

## 2. Counting lines without building them, and the price of restating a rule

- **What I wanted** — the number of lines, not the lines.
- **What happened** — there is no such call. `ui::wrap_text` returns
  `std::vector<std::string>` and `count_lines` took `.size()`. Filed as #135
  and, from one step out, #191: the hug needs each line's EXTENT, so a
  counting overload alone would not have covered it either.
- **Fix** — `src/util/wrap_count.h`, which restates afterhours' break rule over
  byte offsets. The reduction that makes it possible: a candidate line is
  always a contiguous range of its source line, so one scratch buffer serves
  every probe and nothing is allocated per word. Two forms — a count, and the
  line SPANS, which is what the bubble hug measures.
- **The search** — the fast form probes the LAST word first and bisects when
  that overflows: O(log W) measures per output line instead of O(W), and ONE
  measure for a source line that fits, however many words it has.
- **What it cost to be safe** — `tests/unit/test_wrap_count.cpp` is
  differential against `ui::detail::wrap_text_to_width` itself, not against a
  hand-copied reference: 9,200 (string, width) pairs per metric for the count,
  and 3,082 wraps compared LINE FOR LINE for the spans. The two
  implementations share no code, so nothing but that test can notice upstream
  changing a break rule. It caught two whitespace details while the code was
  being written (`span 0 is "a b" but the line is "a b "`).
- **And the assumption, checked at runtime** — bisection needs prefix width to
  be monotonic in prefix length, which kerning does not guarantee.
  `HANABI_VERIFY_WRAP=1` runs both searches on every call and counts
  agreement: **75,234 calls with the app's real font, zero disagreements.**
  The test constructs a metric that dips mid-line and shows the two searches
  parting company at 84 of 300 widths, so the caveat is demonstrated rather
  than asserted.
- **Class** — `WORKAROUND` (#135 / #191)

## 3. The ellipsis search started in the middle of the string

- **What I wanted** — fewer measurements per ellipsized row title (#116).
- **What happened** — the search was already a bisection rather than a
  backward scan, and it started at `len / 2` — when the full string's width
  had just been measured, so the cut point is near `len * budget / full`.
  Seeding there and galloping outward: **8.10 → 5.25 measure calls per cut**
  over 1,478 cuts.
- **It found a wrong cut** — the bisection floored its midpoint to a code point
  boundary and treated "the midpoint floored back onto `lo`" as "there is no
  boundary between `lo` and `hi`". Those are different statements, and the
  difference is a title cut one glyph short in front of any multi-byte
  character. It had never fired because a middle-started search never got that
  close; narrowing the range reached it on the first run
  (`utf8/proportional at maxW=67.00: got "reconciling …" want "reconciling —…"`).
- **Read the app-level number honestly** — the sidebar's memo serves 99.9% of
  rows in every steady-state scenario, so the probes that remain are cold
  ones: a live search, a fling into unseen rows, a fold. The unit count is
  where this is measurable, and that is why the test asserts on it.
- **Class** — `WORKAROUND` (#116)

## 4. hanabi's own measuring still goes around the library's cache — but not around a cache

- **What I wanted** — #137's problem, one branch later: `theme::text_px`
  returns the pen ADVANCE and `TextMeasureCache` caches the INK BOX, a
  consistent 2 px apart, so the shared cache cannot be adopted without moving
  every hugged bubble.
- **What happened** — the app got its own memo over its own semantics: bounded
  LRU, 1024 entries, same values by construction. **18.3 → 0.1 uncached
  fontstash calls per idle frame** (18.3 against `main` at 1abdaa3), and
  15,858 → 226 in the cold-ellipsis
  scenario.
- **The number that explains why nobody had done it** — the app's entire
  app-measured vocabulary is ~120 distinct (string, size) pairs. 17 calls a
  frame is not a hot spot. It is 17 calls a frame forever.
- **Class** — `FOOTGUN` (#137)

## 5. Every one of these caches was serving the wrong font

- **What I wanted** — to know what invalidates the memos this branch was
  making load-bearing.
- **What happened** — nothing did. Settings → Hyperlegible calls
  `fontMgr.load_font(DEFAULT_FONT, path)`, which replaces the glyphs behind
  the name. The name does not change, the handle does not change, no size
  changes — so no key in any measurement cache moves. Four hanabi caches and
  afterhours' own carried on serving measurements of a face that was no longer
  on screen.
- **Fix** — a generation counter (`src/util/text_epoch.h`), read inside the
  shared cache TYPE rather than at the call sites, plus a `clear()` on the
  library's cache. Cost: one unsigned compare on a lookup that was already a
  hash and a compare.
- **What could not be shown** — the two faces are 2.97 structural points
  apart, so the swap is very visible, but a live swap and a cold start in
  Hyperlegible produce frames **0.02% apart**, and every one of those pixels
  turned out to be the relative-time label ticking between the two captures.
  So the stale entries produce no symptom anyone can point at on this catalog.
  This is a fix for a cache that is WRONG, found by asking a question, not by
  chasing a report.
- **Class** — `FOOTGUN` (#190)

## 6. The inline-markdown parser walked one character at a time

- **What I wanted** — after `73a0790`, the largest remaining cost in the
  transcript frame that this repository is allowed to change.
- **What happened** — a `sample` profile of the 240-turn fixture says the frame
  is not ours to fix: 45% of the main thread is `afterhours::measure_text` —
  `fonsTextBounds` → `stbtt_GetGlyphKernAdvance`, re-measuring every drawn
  string at draw time — and another ~14% is `__dynamic_cast` and the
  `strcmp` under `std::type_info::operator==`, from
  `run_systems_on_ui_entities` testing every UI entity against every UI
  system. Both are inside `vendor/afterhours`, which is off-limits here. What
  IS ours is `transcript.pass2_build`, 0.2532 ms of a 2.67 ms scrolling frame,
  and the largest single thing inside it was `md_to_spans`.
- **Why it cost what it did** — the parser's fallback for an ordinary
  character was `push(std::string(1, line[i]), base)`. Every byte of every
  visible line built a one-character `std::string`, appended it to
  `visible`, and appended it again to the current run's text. A 4 KB
  paragraph paid 4,096 calls and two strings' worth of geometric regrowth to
  produce one span. Measured on the 240-turn fixture: 44 calls and 1,732 bytes
  per idle frame, 116 calls and 4,543 bytes per frame while scrolling.
- **Fix** — `src/ui/md_spans.h`. Same grammar, same output byte for byte, but
  the scan jumps to the next delimiter with `find_first_of` and pushes the run
  between them in one append, and `visible` is reserved once. The parser is
  now a pure function of (line, palette) with no dependency on the theme, the
  ECS or a font, which is what lets `tests/unit/test_md_spans.cpp` exist at
  all.
- **Measured**, 240-turn fixture at 1180x949, 900 frames, medians of three
  runs, `73a0790` against this commit:

  | | idle | | read (scrolling) | |
  |---|---:|---:|---:|---:|
  | | before | after | before | after |
  | `transcript.pass2_build` (ms) | 0.0938 | **0.0696** | 0.2532 | **0.1836** |
  | frame CPU (ms) | 1.9946 | **1.9652** | 2.6703 | **2.5889** |
  | allocations / frame | 3,060.4 | **3,027.2** | 4,530.2 | **4,443.4** |

  The build pass is 26–27% cheaper. The frame numbers are the ones to be
  careful with, so they were taken twice with different instruments:
  `HANABI_PROF=1` over 900 frames says −1.5% idle and −3.0% scrolling with no
  overlap between the three runs on either side, and `scripts/perf_ab.sh`
  (interleaved, no profiler, median of 15) says **1.01x** idle and **1.03x**
  scrolling. Two instruments, the same direction, the same size.
- **The instrument was the first thing that had to go** — an early version put
  a `prof::Scope` inside `md_to_spans`. At 116 calls a frame, two
  `CLOCK_THREAD_CPUTIME_ID` reads per call cost ~0.1 ms/frame, and the
  profiled frame came out 4% SLOWER after a change that made it faster. That
  is `prof.h`'s own rule arriving the hard way: a timer on a call this small
  measures the timer. It is a `tick` now, and `transcript.pass2_build` is the
  timer that reports it.
- **Class** — `FOOTGUN` (ours, not the library's)

## 7. What was tried on the way and thrown away

- **Sending uniform-coloured lines down the plain text path.** In the app's
  palette `base` and `strong` are both `text_primary`, so a line with only
  `**bold**` in it merges to ONE span whose colour is exactly the
  `with_custom_text_color` already on the widget — which means dropping
  `with_styled_label` for those lines is colour-identical by construction, and
  it takes them off afterhours' `draw_runs_in_rect` / `wrap_runs_to_width`
  path, which is 18% of the frame. It made things WORSE and the counter said
  so immediately: allocations went from 3,027 to 3,707 per idle frame and from
  4,443 to 4,644 while scrolling, with no frame-time change. The plain path
  allocates more per draw than the span path does, so the whole premise was
  backwards. Reverted; the three lines are not in this commit.
- **Memoizing `md_to_spans` on the line text.** The obvious next move after
  the memos in entries 1–4, and it does not pay here. `colour_links` MUTATES
  the parse before the widget sees it, so a cached entry has to be copied on
  every hit — and the copy is the same two string allocations the parse now
  costs. A memo would buy the scan, which after this commit is the cheap half.
- **`strip_inline_markers`, which has the identical per-character shape.** Left
  alone deliberately: it is the USER-bubble path, and no arm here calls it
  often enough to measure. Named so the next person does not have to find it
  twice.

---

## Where the transcript frame actually goes

Taken with `sample` on the 240-turn fixture so the next person does not have
to; percentages are of main-thread samples, and everything named here except
the last two rows is inside the vendored submodule.

| | idle | read |
|---|---:|---:|
| `afterhours::measure_text` (all callers) | 45.3% | 37.5% |
| — of which `stbtt_GetGlyphKernAdvance`, self | 28.4% | 22.4% |
| `__dynamic_cast` under `run_systems_on_ui_entities` | 10.2% | 17.7% |
| `MainPaneSystem::for_each_with` (the build pass) | 6.0% | 9.7% |
| `SidebarSystem::for_each_with` | 3.5% | 2.8% |

Text measurement at DRAW time is the frame. It is #135's other half: the
memos in this document removed hanabi's own measuring, and the library still
re-measures every string it draws, every frame, with kerning. Nothing in
`src/` can reach it.

---

## The gates, and one that had gone blind

`scripts/perf_text_gate.sh`, in `make test`, ~5 s. It gates measurements per
frame and their slope in thread length, the hit rate of each memo, and — in a
third short arm at **1,200 messages** — the peak size of each cache against the
cap it was sized with.

**The bound arm runs at 1,200 messages for a reason.** At 480 the line-count
memo holds 488 of its 512, so deleting the eviction outright leaves the gate
green; I checked, by deleting it. A bound gate that runs where the cap is not
reached is a bound gate that cannot fail. At 1,200 the same sabotage reads
`1208 entries peak, cap 512, FAIL`.

**`scripts/perf_transcript_slope.sh` had gone blind, by my hand.** It gates
`text.wrap_text` calls per frame, and the transcript stopped calling
`ui::wrap_text` in the second commit of this branch: line counting goes
through `text.count_lines` and the hug through `wrapped_line_spans`. The row
it watched read 0.03 calls a frame and could not move. It sums both rows now.

That is the general hazard, and it is the most transferable thing here: **a
perf fix that routes work through a new function silently retires every gate
watching the old one.** Nothing warns you. The gate keeps passing.

`tests/unit/test_md_spans.cpp` guards entry 6, and it is two gates in one
binary. The first is differential, for the same reason `test_wrap_count.cpp`
is: the parser decides what text gets WRAPPED, so a byte it drops or keeps
changes a line count and therefore a box height, and "these twelve cases look
right" is not the property. The reference is the per-character implementation
this commit replaced, kept verbatim in the test, and the two are compared over
3,840 generated lines — matched and unmatched delimiters, adjacent ones,
multi-byte text, and the `a_b_c` shapes real prose is full of.

The second is a COUNT, and it is the one that fails if anyone writes the old
loop again: over 1–4 KB plain lines the run-based parser allocates 3.00 times
per line and the per-character one 15.00, so the ceiling is 6 and the floor
under the reference is 12. Rehearsed by pointing the test's own subject at the
reference — `15.00 allocations/line, FAIL` — rather than by lowering the
ceiling until it went red.

---

## What measured as noise, or as smaller than it looked

Blunt, in the house style.

- **The ellipsis memo's hit rate under every steady-state scenario.** 99.9%,
  including a 2000-row sidebar being scrolled, because the visible row set is
  capped at 38 and scrolling does not change which titles are drawn as much as
  I expected. The `sample` profile that put `fit_to_width` at 34% of the main
  thread predates the memo; there is nothing left to find there at rest.
- **A resize drag through the ellipsis memo.** It does not exist: hanabi's
  sidebar is a fixed width, so a window resize mints no new keys (124 resize
  steps, 38 entries, unchanged). The animated FOLD is the real width-varying
  input, and even that saturates — six folds produce 220 entries and
  twenty-four folds produce the same 220, because the tween is deterministic
  and frame-locked and walks the same intermediate widths every time. The
  4096-entry cap the memo used to clear at was never going to be reached.
- **Allocations, after the line-count memo.** Unchanged at ~5,940/frame idle.
  The memo removes measurement, not allocation; what is left is afterhours'
  per-widget rebuild cost (#138), which is flat in thread length.
- **`first_n_lines`.** Still materialises lines, and still never fires on any
  fixture here: `kFoldLines` is 40 and the fixture's assistant messages are
  ~15 lines. `docs/perf/TRANSCRIPT.md` named this suspicion a branch ago and
  it is still unmeasured for the same reason — no fixture triggers it. It is
  the obvious next thing for anyone who builds one.
- **Frame CPU, for any single change on this branch.** Three consecutive
  identical runs of the idle scenario read 2.71 / 2.60 / 2.63 ms while their
  counters agreed to the call (55,554 `count_lines` calls, allocations within
  100 of each other). Every per-commit claim here is a count for that reason.

---

## How to reproduce any of this

```bash
# The three scenarios, counts only.
scripts/perf_text.sh

# Entry 6: the same fixture through two instruments. The first is the phase
# table (counts and CPU per phase); the second is the interleaved A/B, which
# runs no profiler at all.
HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_TURNS=240 HANABI_PROF=1 \
  HANABI_SOAK=900 HANABI_STRESS=read output/hanabi.exe --screenshot /tmp/s.png
SIZES=0 TAB=rbig TURNS=240 scripts/perf_ab.sh <old.exe> <new.exe> read 15 800

# Where the frame goes, when the phase table says it is not in our code.
sample <pid of a soak run> 12 -file /tmp/hanabi.sample

# The gate (also in `make test`).
scripts/perf_text_gate.sh

# Does the bisecting wrapper agree with the greedy one, with the REAL font?
HANABI_VERIFY_WRAP=1 HANABI_PROF=1 ... output/hanabi.exe   # text.wrap_verified / text.wrap_disagree

# The scripted path is profilable now too, which is how the fold numbers
# above were taken:
HANABI_PROF=1 HANABI_UI_TESTS=<dir> bash scripts/run_ui_tests.sh
```
