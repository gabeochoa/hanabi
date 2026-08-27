# The event model, and the gates that could not see it

**Why this file exists.** Six feature branches landed on top of this session's
performance work in a few hours — a new event model with per-class transcript
rendering, multiline text editing, focus rings, layout containment across 55
elements, minimap drag-scrubbing, search. Every one of them touches the
per-frame path. Every gate in `make test` was green before and green after.

That is the finding, and it is not the reassuring one it looks like.

Measured 2026-08-26 on `gabeochoa-mac-GRQ7Y259H4`, a machine shared with
several other agents; load averages during the samples ran from 9 to 16. Every
headline here is a COUNT, for the reason `docs/perf/DIGEST.md` gives: the box
has been observed at load 134 and no timing taken on it that day was worth
anything. The timings that appear are CPU time (`CLOCK_THREAD_CPUTIME_ID`) and
are labelled as scale rather than claim.

`main` is `88fde14`. The pre-feature base is `a1920b4` — the commit after the
last perf merge (`perf/flake`) and before the first feature merge
(`feat/minimap-drag`). Both were built from clean worktrees and run
back to back.

---

## 1. The headline: the user's bug is still fixed

His words were *"it just gets slower and slower every second until it
freezes"*, and his reproduction was *"open the program and scroll the sidebar
up and down until it broke"*. That path, driven for **12,000 frames — 200
seconds of continuous scrolling at 60fps** — at a 2000-session catalog with
the list expanded:

```
  metric       slope /1000f     per minute @60fps      budget rising  verdict
  RSS                 +0.0 KB          +0.0 KB           2048   0.47  ok
  heap bytes          +0.0 KB          +0.0 KB           2048   0.46  ok
  GPU bytes           +0.0 KB          +0.0 KB             64   0.00  ok
  entities            +0.0             +0.0               100   0.00  ok
  cpu time            +0.0 ms          +0.0 ms              0   0.56  ok
  heap blocks         -0.4             -1.4             20000   0.26  ok
  allocs/frame      1136.7  operator new calls, steady state
```

Twenty-three buckets, 253 pairwise slopes, flat on every column. `rising` at
0.26–0.56 is a coin, which is what noise looks like; a leak reads 1.00.

And the LEVEL beside the trend, because a flat-but-slow result and a
rising-but-fast result are different findings: **1.22 ms of frame CPU, 453
entities, 1,136.7 allocations a frame, from the first measured bucket to the
last.** Not slow. `make soak`'s thirteen arms agree — every gated arm flat,
`open` and `mixed` reported as designed.

---

## 2. Then and now, every headline number in `docs/perf/`

"Then" is the figure the named doc records as its result; "now" is `main` at
`88fde14`, re-measured with the doc's own method. **Several "then" columns are
older than `main`** — each doc was written at its own branch point and four
more perf branches landed after most of them — so a number that improved
usually improved because of a later branch, not because of the features. Where
the fixture itself changed, the row says so rather than pretending the two
numbers are comparable.

### `docs/perf/SCROLL.md` — `scrollall`, 2000 sessions, 3000 frames

| | then (`perf/scroll`) | now (`88fde14`) | |
| --- | ---: | ---: | --- |
| frame CPU | 1.533 ms | **1.22 ms** | better |
| entities | 496 | **453** | better |
| allocations / frame | 3,703 | **1,136.7** | 3.3x better (`perf/alloc`, `perf/digest`) |
| text measures / frame | 11.8 | **2.45** | better |
| RSS | 53,104 KB | 72,144 KB | **not comparable** — see below |
| entities @ 20 / 2000 sessions | 364 / 472 | **327 / 453** | better |

The RSS row is the one honest complication in this table. `perf/stress` landed
after `SCROLL.md` was written and made the synthetic catalog 2.1x richer per
thread on purpose ("the synthetic stress catalog rendered the cheapest path
this app has"). 2000 sessions is 2000 bigger threads than it was. The A/B that
*is* comparable — the same fixture, base against main — is in section 3, and
it reads +512 KB.

### `docs/perf/RETIRE.md` — `views`, 2000 sessions

| | then | now | |
| --- | ---: | ---: | --- |
| entities, cheapest screen in the cycle | 167 | **181** | ~ |
| entities, dearest screen | 2,467 | **813** | 3.0x better (`perf/digest`) |
| stale widgets | 0 | **0** | = |
| live / built | 1.06x | **1.04x** | = |
| epoch advances per frame | yes | **yes** (1324 / 1200) | = |

`RETIRE.md`'s own "What this does NOT fix" named the uncapped digest views as
the reason its dearest screen barely moved. That got fixed; this is the number.

### `docs/perf/DIGEST.md` — Blocked, 2000 sessions, 1200 frames

| | then | now | |
| --- | ---: | ---: | --- |
| allocations / frame | 1,826.0 | **661.0** | 2.8x better |
| entities | 509 | **246** | 2.1x better |
| frame CPU | 1.16 ms | **0.92 ms** | better |
| cards BUILT against 506 matched | ≤ 40 | **13** | = |
| widget ratio, 200 → 2000 sessions | ~1.0x | **0.99x** | = |

### `docs/perf/TEXT.md` — `scripts/perf_text.sh`, 900 frames per scenario

| | then | now | |
| --- | ---: | ---: | --- |
| text measurements / frame, idle | 5.3 | **5.29** | = |
| …read (scrolling) | 10.6 | **10.66** | = |
| …rows (2000 sessions) | 2.5 | **2.45** | = |
| allocations / frame, idle | 5,744 | **3,119.4** | better |
| …read | 9,235 | **4,668.1** | better |
| …rows | 3,006 | **1,848.4** | better |
| line-count memo hit rate @ 480 msgs | 99%+ | **96.32%** | see §6 |

### `docs/perf/ALLOCATIONS.md` — `make alloc-gate`

| arm | then | now | ceiling |
| --- | ---: | ---: | ---: |
| `home20` | 827.0 | **810.0** | 1,000 |
| `home2000` | 1,197.0 | **1,162.0** | 1,450 |
| `thread480` | 2,740.0 | **2,740.0** | 3,300 |

`thread480` is identical to the allocation. That is not luck and it is the
subject of section 4.

### `docs/perf/MEMORY.md` and `docs/perf/GATES.md`

| | then | now | |
| --- | ---: | ---: | --- |
| soak RSS slope, idle, per 1000 frames | +11 KB | **+0.0 KB** | = |
| GPU bytes slope | +0.0 KB | **+0.0 KB** | = |
| launch: FirstFrame | gate < 250 ms | **55 ms** | = |
| launch: peak RSS | gate < 250 MB | **48 MB** | = |
| `make soak`, 13 arms | all flat | **all flat** | = |
| transcript slope, allocations / message | 0.16 | **0.161** | = |
| transcript render-cache hit @ 480 msgs | 99%+ | **99.6%** | = |

**Nothing regressed.** Not one number in those six tables moved the wrong way
on its own account, and the one that looks like it did (RSS) is a fixture
change with its own A/B below.

---

## 3. The A/B that matters, and what it actually proves

Base `a1920b4` against main `88fde14`, same box, back to back, same fixtures:

| gate | base | main |
| --- | --- | --- |
| alloc: home20 / home2000 / thread480 | 810.0 / 1162.0 / 2740.0 | **810.0 / 1162.0 / 2740.0** |
| digest: built / matched / w@200 / w@2000 | 13 / 506 / 210 / 207 | **13 / 506 / 210 / 207** |
| retire: live / built / stale | 221 / 212 / 0 | **221 / 212 / 0** |
| scroll: entities @ 20 / 2000 | 327 / 453 | **327 / 453** |
| scaling: widgets @ 20 / 2000 | 320 / 426 | **320 / 426** |
| scrollall @ 2000: entities, allocs/f | 453, 1136.7 | **453, 1136.7** |

Every count identical to the unit, across a merge that added an event model, a
focus-ring system, multiline editing bindings, containment changes to 55
elements and a drag gesture.

A result that clean is not a pass. It is a question. **Six branches touched the
per-frame path and moved nothing any gate can see — what is it that the gates
cannot see?**

---

## 4. The answer: no fixture in this repo can produce an event row

`feat/event-model` added `api::EventKind` with ten values and taught the
transcript to build a distinct `Item` and a distinct renderer for six of them.
Here is where those rows come from:

| source of a transcript | emits an `EventKind` other than `Text`? |
| --- | --- |
| `stress_turn` — the synthetic catalog, `HANABI_STRESS_SESSIONS` | **no.** Every `Message` it builds leaves `kind` at its `Text` default |
| the `rbig` fixture — `HANABI_BIG_TRANSCRIPT`, the long-thread perf fixture | **no.** User / Assistant / Tool / Tool, and no thinking row either |
| the twenty hand-written mock threads | **one of them.** `r8` carries five event rows |

And here is what measures each of those:

- `alloc-gate`'s `thread480` arm, `perf_transcript_slope.sh` and
  `perf_text_gate.sh` all drive **`rbig`**.
- `scaling-gate`, `scroll-gate`, `digest-gate`, `retire-gate` and all thirteen
  `make soak` arms drive **`stress_turn`**.
- Nothing at all drives **`r8`**.

So the per-message gate, the text gate, the allocation gate and the whole soak
were reading a transcript with **zero** of the new row kinds in it. They
reported no change because there was no change to report. They were right, and
they were blind, and from a green board those look the same.

Verified rather than argued. `r8` is the only thread in the repo that renders
one, and opening it on each binary is the entire measurable footprint of the
event model as `main` shipped it:

| `HANABI_OPEN=r8`, 800 frames | base | main | delta |
| --- | ---: | ---: | ---: |
| entities | 167 | 180 | **+13** |
| allocations / frame | 766.0 | 899.0 | **+133** |
| frame CPU | 0.753 ms | 0.841 ms | +0.088 ms |

Five event rows. **+2.6 widgets and +26.6 allocations per frame per visible
event row** — and five rows in one mock thread is the entire exposure any gate
in `make test` had to a feature that reclassifies every message in every
transcript.

**This is `docs/perf/STRESS.md`'s own finding, arriving a second time.** It was
found there for tool rows, thinking rows and code fences, and fixed there — in
`stress_turn`. The fixture it was not fixed in is the one the per-message gates
use, and the next feature to add a row kind would have walked into it again.

### The fix: `HANABI_BIG_EVENTS=1`

`MockClient::build_seed`'s `rbig` block now gives every turn the mix a real
session has, behind a knob. **Off by default, and the default is the point:**
`thread480`'s ceiling and `perf_transcript_slope.sh`'s limits were set against
the four-row turn, and changing the shape under them would move every one of
those numbers in the same commit that claims to measure a regression.

The mix is observed, not invented. One real agentcloud session read **32 Text ·
13 Thinking · 68 ToolCall · 6 SubAgent · 2 Delivery** over 121 rows. Taking the
two Text rows a turn as the unit, that is sixteen turns: 0.8 thinking, 4.25
tool, 0.4 sub-agent and 0.125 delivery per turn. The fixture emits a thinking
row and four tool rows every turn, a sub-agent every third and a delivery every
eighth — within a few percent of each, and a whole number per turn.

Node, skill and status rows are the exception, at one of each per sixteen
turns. The observed session had none, so no ratio argues for them; they are
there because all three go through `render_event_row`, and a path no fixture
reaches is a path no gate can see. Which is the sentence this whole section is
about.

---

## 5. What it costs, now that something can measure it

`rbig`, `HANABI_OPEN=rbig`, 400 frames, 1180x949, three runs each, zero spread
on every count. `msgs` is 4 per turn without events and 7.65 with.

| | turns | msgs | items | widgets | allocs/f | frame CPU | minimap | pass1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| no events | 15 | 60 | 45 | 296 | 2,657.0 | 1.86 ms | 0.027 | 0.042 |
| | 480 | 1,920 | 1,440 | 1,707 | 2,662.0 | 6.06 ms | 0.847 | 1.263 |
| with events | 15 | 115 | 68 | 323 | 2,323.0 | 1.83 ms | 0.040 | 0.049 |
| | 480 | 3,672 | 2,263 | 2,500 | 2,363.0 | 8.14 ms | 1.330 | 1.520 |

Per MESSAGE, which is the question the brief asked:

| | no events | with events | |
| --- | ---: | ---: | --- |
| widgets per message | 0.759 | **0.612** | −19% |
| frame CPU per 1000 messages | 2.256 ms | **1.774 ms** | −21% |
| allocations per frame, 32x the thread | 2,657 → 2,662 | **2,323 → 2,363** | flat, and lower |

**The per-message widget and allocation cost did not grow. Both fell.** An
event row is one styled `div`; an assistant bubble is a wrapper, a body, N
lines and a hover action row. Drawing the events costs less per row than the
prose they sit between — and it is more rows only because those rows were
previously **dropped**, which is the bug `feat/event-model` was written to fix.

Gap **#138**'s shape — a mark per item, linear in allocations — **is not
present**: allocations per frame are flat to 1.7% across a 32x thread, because
the transcript's render cache holds. The per-message allocation slope is
0.16/turn against a limit of 2.0.

It is present as a WIDGET and CPU linearity, and that is the next section.

---

## 6. What is still slow, with the number attached

### The minimap rail was 2,263 widgets and a solid stripe — FIXED, with a gate

Two things that are each correct alone. `minimap::slot_h` is exact and
unclamped, because the slots must sum to the rail or every mark below a drift
points somewhere it does not mean. `minimap::draw_mark` clamps the DRAWN dot up
to `kMinDotH` = 2px, because a one-line item should still be visible.

Together, on a long thread: 2,263 items on an ~800px rail is an average slot of
**0.35px**, and every one of those 2,263 dots is clamped up to 2px. They
overlap six deep. **The rail paints a solid stripe — it stops being a map,
silently, and the longer the thread the less it says**, which is precisely
backwards. It is a correctness failure and a perf failure in one line of
arithmetic.

The fix (`hanabi::minimap::group_marks`) is the bound the geometry already
implies: a rail of `railH` pixels can hold `railH / kMinDotH` marks and no
more, so past that density adjacent items share a mark. A group's slot is the
SUM of its items' heights, so the slots still tile the rail exactly and nothing
below a group drifts; a click goes to the group's first item; the group wears
the rarest kind in it, so a turn collapsed with the forty tool rows after it
still reads as a turn.

**It only engages when the rail is oversubscribed** — `n * kMinDotH > railH`,
an aggregate test, not a per-item one. Below that every item keeps its own
mark at whatever slot it earns, including the sub-2px slots a short row gets in
a sparse thread. That is deliberate: per-item thresholding would merge those
too, which is a behaviour change at a density that works, and
`tests/ui/minimap_navigator.e2e` (160 messages, 120 items, 240px of dots on a
~590px rail) is the reader who would have noticed.

| `rbig`, with events | before | after | |
| --- | ---: | ---: | --- |
| minimap marks @ 1,123 items | 1,123 | **241** | 4.7x |
| widgets @ 3,672 messages | 2,500 | **579** | 4.3x |
| frame CPU @ 3,672 messages | 8.14 ms | **3.52 ms** | 2.3x |
| `transcript.minimap` @ 3,672 | 1.330 ms | **0.198 ms** | 6.7x |
| widgets @ 7,344 messages | — | **579** | bounded |
| widgets @ 14,688 messages | — | **637** | bounded |

Gated by `make events-gate`, unit-tested in
`tests/unit/test_minimap_marks.cpp`. Both were made to fail against the
ungrouped rail before being believed — the gate reads `marks 1121 / ceiling
400 FAIL`, `widgets 1378 / 700 FAIL`, `per turn 4.69 / 2.0 FAIL`; the unit test
reads `2001 failed`.

### `transcript.pass1_measure` is linear in the message count — NOT fixed, 6.12 ms

With the rail bounded, this is the largest per-message cost left and it is now
most of the frame:

| messages | frame CPU | pass1 | pass1 as a share |
| ---: | ---: | ---: | ---: |
| 3,672 | 3.52 ms | 1.50 ms | 43% |
| 7,344 | 4.68 ms | 3.00 ms | 64% |
| 14,688 | 7.18 ms | 6.12 ms | **85%** |

Pass 1 walks **every message in the thread, every frame**, to build the item
list and its measured heights. The heights are memoized — `cache.msgrender_hit`
reads 293.7 a frame against 0.6 misses — so what is left is the walk itself and
the memo lookups: at 918 messages, 289 `measure.bubble_h` calls and 146
`cache.hug_hit` a frame, each a hash of a key that had to be built to be
hashed. That is `docs/perf/TEXT.md` section 1's finding ("a cache at a 100% hit
rate is not the same thing as work not being done") one level up the stack.

**The fix is an incremental item list**, rebuilt only when the message vector
changes rather than every frame — a version counter on the vector plus an
append-only path, since a transcript almost always grows at the end. It is not
cheap: pass 1 also depends on the pane width, the fold state, the unread
boundary and the date-divider setting, so the invalidation has more inputs than
the message count. Sized at a day rather than an hour, which is why it is
written down here with the number rather than attempted.

**14,688 messages is not a real thread.** At 3,672 — which is — pass 1 is 1.50
ms and the whole frame is 3.52 ms, i.e. 284 fps. This is a slope with a long
runway, not a fire.

### The line-count memo is exactly at its cap on a 480-message thread

```
  line-count memo bound               512 entries peak     cap 512   ok
  line-count memo @ 60               38.4 hit / 0.2 miss = 99.48%
  line-count memo @ 480              41.9 hit / 1.6 miss = 96.32%
```

`perf_text_gate.sh` reads `ok` because the bound holds — the gate's subject is
"does the memo stay bounded", and it does. But **peak == cap** means it is
evicting, and the hit rate falling from 99.48% to 96.32% between the two thread
lengths is that eviction showing. A longer thread evicts more. Nothing here is
broken and nothing is over budget; it is the point at which the next
measurement should be taken, and `kLineCountEntries` is the constant to move
when it is.

### Still uncounted, unchanged from `docs/perf/GATES.md`

Gap **#155** (pipeline compilation in the first frames), **#210** (sokol's
fixed pools: images 128, samplers 64 — a silent correctness failure, not a
memory one), **#211** (the 2048² glyph atlas whose overflow corrupts
measurement). None of them moved and none of them is reachable from anything
this branch added; they remain as `MEMORY.md` entry 5 and `GATES.md` describe
them.

---

## 7. `make test` was RED on `main` before this branch touched it

`88fde14`, built clean in its own worktree, no changes:

```
  103 passed, 2 failed
  failed: a_click_on_a_line_seam_is_one_click select_word_and_line
```

Both are coordinate-driven text-selection scripts and neither is a selection
bug. The six feature branches moved every body line of the `t2` thread down
**26px** — the transcript's three lines went from element tops 218 / 234 / 250
to 228 / 244 / 260 — so `double_click 415 226` now lands above the line whose
character count the assertion names, and `click 415 234` is no longer a seam.

Re-measured mechanically with `HANABI_SELECT_AUDIT=1`, the knob
`docs/perf/GATES.md` records as the instrument for exactly this, rather than
nudged until green:

```
[sel] press=(415.0,252.0) run=1 off=17 rect=(329.0,244.0 656.0x16.0) len=61
      text="  acct 8842 - ledger $128.60, computed $116.20 (delta $12.40)"
[sel] press=(415.0,260.0) run=1 off=17 rect=(329.0,260.0 656.0x16.0) len=58
      text="  acct 1097 - ledger $54.10, computed $51.00 (delta $3.10)"
```

252 is the centre of the 61-character line; 260 is the seam it shares with the
58-character one below. Both scripts updated to those coordinates with the
reading quoted in them, so the next move is a diff rather than an
investigation. `select_word_and_line.e2e`'s own comment already keeps the
history of every previous move — this is the fifth.

That the two of them went red at the same instant, on a script whose comment
predicted it, is the argument for the audit knob and against coordinate
assertions in general (`afterhours_gaps.md`: there is no way to ask where a
piece of text landed).

---

## 8. What was added, and how each was made to fail

| | what it guards | made to fail by | it read |
| --- | --- | --- | --- |
| `make events-gate` | the fixture DREW event rows at all | the `HANABI_BIG_EVENTS` lambda forced false | `items built  event 0  deliv 0  spawn 0  think 0` → FAIL, run stops |
| `make events-gate` | the rail's mark count is bounded | `group_marks`' guard forced true | `minimap marks 1121  ceiling 400  FAIL` |
| `make events-gate` | widgets, LEVEL and per-turn | same | `widgets 1378 / 700 FAIL`, `per turn 4.69 / 2.0 FAIL` |
| `make events-gate` | allocations, LEVEL and per-turn | (correctly stayed green — see below) | `2365.0 / 2900 ok` |
| `test_minimap_marks` | the grouping arithmetic | `group_marks`' guard forced true | `2001 failed` |
| `check_fixture_env.py` | a fixture knob missing from `kFixtureEnv` | `HANABI_BIG_EVENTS` dropped from the list | `build_seed() reads HANABI_BIG_EVENTS and kFixtureEnv does not list it` |

**Say which rows did NOT go red.** The allocation arms stayed green under the
ungrouped rail and are right to: a mark costs a widget and CPU, not malloc
traffic, and a gate that went red on every arm would be telling you less rather
than more. In the unit test,
`test_a_group_keeps_the_most_worth_seeing_kind` also passed under the
injection — with one slot per item, slot 0 IS item 0 — and it guards the
priority rule, not the bound. Both are recorded in the files themselves.

`check_fixture_env.py` deserves its own line. `mock_client.h` says in plain
words that anything in `build_seed` reading the environment must be added to
`kFixtureEnv`, because the catalog is built once and cached on those names —
and the failure when you forget is silent, ORDER-DEPENDENT (the scripted runner
loads a whole directory into one process, so whichever script runs first
freezes the fixture for the rest) and points at the script rather than the
list. It had already cost this project two timed-out scripts once. Adding
`HANABI_BIG_EVENTS` needed that line and very nearly did not get it, which is
the whole argument for the check.

---

## 9. The lesson, which is the same one three files already have

`docs/perf/SIDEBAR.md` §4: *a harness only measures the things somebody thought
to drive.* `docs/perf/SCROLL.md` §1: *an arm that runs is not an arm that
measures* — the arm named after the bug report was scrolling the wrong list.
`docs/perf/GATES.md` §0: *a gate that cannot fail is worse than no gate.*

This is the fourth. The gates were not wrong, the fixtures were not wrong when
they were written, and nobody skipped a step. A feature added six row kinds and
the fixtures kept producing four, and the only visible symptom was a set of
numbers that did not move — which reads exactly like success.

The generalisable move is the one `make events-gate`'s first arm makes and the
soak already made for scenarios ("a scenario that DROVE NOTHING is its own
outcome alongside PASS / FAIL / INCOMPLETE"): **before a gate reports a cost,
it should have to say what it drew.** Four gauges and an `-eq 0` check. Every
gate in this repo that measures a render path could carry one, and the four
sleeping gates `GATES.md` §0 found by luck would have announced themselves.

---

## Message actions and real tool metadata — 2026-08-27

The message-action/tool-detail branch was measured with the same `scripts/events_gate.sh` long busy-event fixture before and after the UI changes:

| 15 / 240 turns | before | after | delta |
| --- | ---: | ---: | ---: |
| allocations/frame | 2333 / 2369 | 2345 / 2381 | +12 / +12 |
| widgets | 325 / 501 | 330 / 506 | +5 / +5 |
| allocation slope/turn | 0.16 | 0.16 | 0 |

The fixed +12 allocations and +5 widgets are the visible tool name/status/footer detail. Hidden message actions add no entity: `MainPaneSystem::message_actions` returns before its first `div` unless that pane's message is hovered, focused, or showing recent feedback. The 240-turn arm remains below the 2900 allocation and 700-widget ceilings.

The remaining cost is unchanged: variable-height transcript pass 1 still walks the complete item list. The 14,688-message measurement above is 6.12 ms in `transcript.pass1_measure`, 85% of the frame. That vendor-bound mechanism and the current Hanabi workaround are filed as afterhours gap #455.
