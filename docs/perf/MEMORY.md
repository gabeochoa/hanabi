# Memory: what hanabi holds, and whether it ever lets go

**Why this file exists.** The app was reported as getting "slower and slower
every second until it freezes". One probe — `HANABI_SOAK`, watching the malloc
zones' live block count per frame — found the cause in one run: Metal returns
autoreleased objects and a render loop is not a Cocoa run loop, so every frame
leaked six of them. Fixed, RSS growth went from +2816 KB per 1000 frames to
+11.

That probe measures the SLOPE of a steady state. Everything in this file is
what it cannot see: costs that are per *thread opened* rather than per frame,
costs that live on the GPU where a malloc counter has no visibility at all, and
costs that are written to disk and survive the restart that resets everything
else.

Written the way `docs/visual-parity/FRICTION_LOG.md` is: what was wanted, what
happened, what it cost, numbers inline. A number is worth more than an
adjective.

---

## The instrument: `HANABI_MEMLADDER=1`

`scripts/measure_launch.sh` prints ONE peak-RSS number for a process that
opened a window, loaded a catalog, opened a tab and laid out a transcript. That
number is the process. It is not the catalog and it is not the tab, and it
moves when anything ahead of it changes, so an optimisation cannot be judged
against it.

Ported from Puffin's `Tests/MemoryAttributionTests.swift`, whose opening
comment is the same complaint arrived at independently in a Swift app: *"This
walks the ladder in a process of its own and prints the delta at each rung,
which is the only form of the number an optimisation can be judged against."*

```bash
H=$(mktemp -d); mkdir -p "$H/Library/Application Support/hanabi"
echo '{"window_width":1180,"window_height":949,"open_tabs":[],"active_tab":"","theme":"dark"}' \
  > "$H/Library/Application Support/hanabi/settings.json"
env HOME="$H" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
    HANABI_CONFIG=/tmp/none HANABI_MEMLADDER=1 \
    HANABI_STRESS_SESSIONS=500 HANABI_MEM_SESSIONS=8 HANABI_MEM_CHURN=500 \
    output/hanabi.exe --screenshot /tmp/o.png 2>&1 | grep '^\[ladder\]'
```

| knob | what it does |
| --- | --- |
| `HANABI_MEMLADDER=1` | run the ladder instead of a capture, and exit |
| `HANABI_MEM_SESSIONS=<n>` | threads open at once on the tab rung (8) |
| `HANABI_MEM_CHURN=<n>` | threads opened and closed ONE at a time (= sessions) |
| `HANABI_MEM_SETTLE=<n>` | frames pumped before each reading (30) |
| `HANABI_MEM_SAMPLES=<n>` | readings per rung, reduced by median (9) |
| `HANABI_MEM_IMAGE_DIR=<dir>` | attach and remove every .png in the dir, one at a time |
| `HANABI_MEM_HOLD=<s>` | sit at the last rung so `malloc_history` can attach |

### The ladder, rung by rung

Catalog 520 sessions, 8 tabs at the peak, 500 threads opened and closed one at
a time. `dRSS` is against the rung above.

| RSS | dRSS | blocks | dblocks | rung |
| ---: | ---: | ---: | ---: | --- |
| 9,200 KB | — | 1,095 | — | process floor, before graphics or app state |
| 19,264 KB | +10,064 | 5,967 | +4,872 | window + systems built, no frame yet |
| 32,256 KB | +12,992 | 7,035 | +1,068 | the app running, empty: no catalog, no tab |
| 44,352 KB | +12,096 | 17,742 | +10,707 | catalog loaded, sidebar drawing it |
| 44,688 KB | +336 | 18,560 | +818 | the FIRST thread open |
| 46,960 KB | +2,272 | 27,593 | +9,033 | 8 threads open at once |
| 47,024 KB | +64 | 24,964 | −2,629 | scrolled both panes end to end |
| 47,024 KB | +0 | 24,955 | −9 | every tab closed, back to the empty app |
| 47,840 KB | +816 | 26,750 | +1,795 | after 500 opened and closed one at a time |
| 47,840 KB | +0 | 26,753 | +3 | **the same state again — the noise floor** |

**What the shape says.** Two thirds of the app's memory is spent before it has
shown you anything: 10 MB to build a Metal device and a system manager, and
another 13 MB to render one empty frame. The 520-row catalog is 12 MB, 23 KB a
row. Against all of that, the eight open tabs are 2.3 MB and the FIRST thread
open is 336 KB — the transcript, the thing the app is for, is the cheapest
thing on the ladder.

**What the last two rungs say.** Closing eight tabs gave back 3 KB. Then 500
threads opened and closed one at a time retained 324 KB that the app is in the
wrong state to be holding, and the noise floor under that measurement is three
blocks. That residue is the subject of most of this file.

### The instrument had to be fixed before it could be believed

The first version took ONE reading per rung. The same configuration measured
1634, 6272 and 2976 retained blocks on three consecutive runs. That is not a
measurement, it is a coin, and a conclusion had already been written down from
it ("about 10 blocks per thread") that was pure noise.

The app has async loader workers and rebuilds its entire widget tree every
frame, so the in-use count at any instant carries whatever transient allocation
was outstanding when the sample landed. Each rung is now the median of nine
readings a frame apart, and the ladder ends with a rung that does nothing at
all, whose delta is printed as the noise floor.

| | run 1 | run 2 | run 3 |
| --- | ---: | ---: | ---: |
| retained after 200 open/close, single sample | 1,634 | 6,272 | 2,976 |
| retained after 200 open/close, median of 9 | 4,495 | 4,600 | 4,242 |
| noise floor | +0 | +2 | +1 |

±100% became ±4%. **One caveat, stated because it bit:** the noise floor covers
variance WITHIN a process. Block counts still vary by ±1500 between separate
processes, because allocator layout does. When comparing two builds, compare
the KB column and run each more than once.

---

## Entries

### 1. Five maps keyed by session id that nothing ever erased

- **What I wanted** — to know how much a thread costs after you close it.
- **What happened** — `main_pane_system.h` had five function-local statics
  (`s_unread`, `s_followMap`, `s_prevOffsetMap`, `s_lastScrollYMap`,
  `replyDrafts`) plus `AppComponent::composerHistory`, all keyed by session id,
  all written on the first frame a thread is drawn, none ever erased. Each has
  a comment explaining, correctly, why the state must be per-session. None says
  when an entry goes away, and the answer was never. Nothing outside the
  function could even count them.
- **Cost** — measured before/after on the ladder, held after every tab is
  closed:

  | | 200 threads | 1000 threads |
  | --- | ---: | ---: |
  | before | 493 KB | 663 KB |
  | after | 425 KB | **423 KB** |

  ~0.2 KB per thread, growing forever, against flat.
- **Fix** — one LRU, 64 threads deep, in `src/ecs/pane_state.h`, with a
  `size()` the ladder prints.
- **What the bound costs the user** — a dropped thread's unread divider
  recomputes from the persisted stamp (the line comes back, it re-marks), its
  follow-the-bottom latch re-arms, and its scroll velocity restarts at zero for
  one frame. Eviction *skips* any entry holding an unsent draft — it walks back
  from the oldest until it finds an empty one — so the bound is never paid by a
  thread you are typing in.

### 2. The one per-thread record the app writes without being asked

- **What I wanted** — to check whether any of this survives a restart.
- **What happened** — `last_read` does. Star, mute, archive and tool-fold all
  grow one entry per thread and all are correctly unbounded: each is a
  deliberate act on one thread, so the map is exactly as large as the user made
  it. Reaching the bottom of a thread is not an act, it is what reading IS —
  and it wrote an entry nothing removed, into a file that is fully
  re-serialised and rewritten on every single advance.
- **Cost** — 200 threads read took `settings.json` from 88 bytes to 5,724. With
  the file pre-seeded to look like a year of use, 1500 frames of
  `HANABI_STRESS=threads` over a 520-row catalog:

  | seeded entries | settings.json | RSS | frame time |
  | ---: | ---: | ---: | ---: |
  | none | 113 B | 47.5 MB | 4.17 ms/f |
  | 2,000 | 72 KB | 48.3 MB | 4.24 ms/f |
  | 10,000 | 360 KB | 52.3 MB | 4.64 ms/f |

  +4.8 MB resident and +0.47 ms *every frame*, from a file. The frame cost is
  the rewrite: about 50 threads were read over that run, so each advance writes
  the whole 400 KB back synchronously — roughly 15 ms on the UI thread, twice a
  dropped frame, every time you finish reading a thread.
- **Fix** — capped at 2000 entries, dropping the oldest STAMPS (the stamp is
  the moment the thread was last read, so "oldest" means what it should).
  Pruned on load too, so a file written by an older build shrinks on first run.
- **What the bound costs the user** — the transcript computes an unread divider
  only when the stamp is > 0, so a thread whose stamp was dropped opens with no
  "new since you were last here" line. It does not open marked unread.

### 3. A cache "bounded by clearing" was bounded by being useless

- **What I wanted** — to check `transcript_render_cache.h`'s claim that it is
  bounded by clearing when the open thread changes.
- **What happened** — it is, and that is a correct bound for one pane. Split
  view renders TWO transcripts in one frame, by swapping `app.openSession` and
  `app.splitSession` around a second `render_transcript` call. So the one cache
  is handed thread A, then thread B, then A again next frame, and clears itself
  every time. In split view the memoization was not degraded, it was OFF, in
  exactly the mode that has twice as much to measure.
- **Cost** — two 60-turn threads side by side at 1600x1000, 200 frames, median:
  **10.90 / 10.91 ms before, 8.77 / 8.51 ms after — 21% of the frame.** Single
  pane unchanged (5.57 vs 5.67 ms), which is the control.
- **Fix** — a small LRU over per-thread maps, three deep: two panes plus one
  spare, so a tab switch while split does not evict the pane you did not touch.
- **A memory audit that found frame time.** This is the entry that justifies
  auditing caches that are already "bounded": the bound was real, and it was
  achieved by throwing away the thing the cache exists for.

### 4. 114 MB of textures for images the composer no longer holds

- **What I wanted** — to find out what `static std::unordered_map<std::string,
  Cached> cache;` in `src/ui/inline_image.h` actually holds. Its comment said
  "a small bounded path->texture cache".
- **What happened** — nothing bounded it. Every distinct path the app ever drew
  stayed decoded on the GPU for the life of the process. The composer's
  attachment LIST is capped at five (`kMaxAttachments`); removing a chip
  removed the chip and kept the texture.
- **Cost** — added a ladder rung (`HANABI_MEM_IMAGE_DIR`) that attaches every
  PNG in a directory one at a time, clearing the list between, which is what a
  person pasting screenshots all day does. Composer empty at the end:

  | images | RSS at the rung | over the empty-app baseline | |
  | ---: | ---: | ---: | --- |
  | (baseline) | 42,864 KB | | |
  | 60 | 159,680 KB | +114 MB | unbounded |
  | 180 | 269,984 KB | +227 MB | linear in the count |
  | 60 | 97,232 KB | +54 MB | bounded |
  | 180 | 99,136 KB | +56 MB | **flat** |

  1.9 MB per 640x480 image. A Retina screen grab is 3024x1964 = 23.7 MB of
  RGBA, so five pasted and removed is 119 MB that never comes back.
- **The instrument that found the last leak is blind to this one.** Live malloc
  moved +427 KB across that whole rung. A GPU texture is not a malloc block:
  every byte was inside `sg_make_image`. Only RSS sees it, which is why the
  ladder prints three columns and why `afterhours_gaps.md` #126 exists.
- **Fix** — a 32 MB LRU over decoded pixels, `unload_texture` on eviction, and
  one refinement that matters more than the budget: an entry touched within the
  last sixteen accesses is never evicted. Without that, a working set larger
  than the budget evicts what it is about to draw and re-decodes a PNG every
  frame, which is far worse than the memory it saves.
- **What the bound costs the user** — an image not drawn in a while is
  re-decoded next time it appears: one slow frame on the scroll back to it.
- **Not fixed** — hanabi holds full-resolution pixels to draw a 64px chip,
  because afterhours' `load_texture` takes a path and nothing else. That is 28x
  more saving and it is upstream's: `afterhours_gaps.md` #125.

### 5. What is correctly bounded, and left alone

Checked, and no change made. A bounded cache is not a bug.

| | bound | verdict |
| --- | --- | --- |
| `transcript_cache.h` | 5 threads x 20 messages, LRU with move-to-front on every access | correct, and the comment is accurate |
| `api::disk_cache` | `trim_to_cap`, 1 GiB default, LRU by mtime, trims long transcripts before deleting them, never evicts `sessions.json` | correct, and more careful than it needed to be |
| `AppComponent::liveSubs` | one per OPEN TAB, reaped off the UI thread when the tab closes (`loader_system.h:988`) | correct |
| `AppComponent::expandedPiles` | grows only on a click, cleared on thread change | user-intent, correctly unbounded |
| `Settings` `starred`/`muted`/`archived`/`tool_fold`/`row_order` | one entry per deliberate act on one thread | user-intent, correctly unbounded |
| `AppComponent::composerAttachments` | 5, the cap the orchestrator's own message route enforces | correct — the *texture cache behind it* was the bug (entry 4) |

### 6. What measured as noise

Blunt, because the point of a noise floor is to be allowed to say this.

- **Entity count after churn.** It moves — 1,332 after closing every tab,
  1,465 after 500 more open/close cycles. That is 0.27 entities per thread and
  it does not reproduce cleanly run to run. With
  `AFTER_HOURS_UI_SINGLE_COLLECTION` the immediate-mode widgets ARE entities
  and are recycled rather than destroyed, so the count is a high-water mark of
  widgets, not a live-object count. Nothing was done about it and nothing
  should be until there is a measurement that separates the two.
- **The residual per-thread slope after the four fixes.** 288 KB at 100
  threads, 364 KB at 500, 537 KB at 2000: about 0.13 KB per thread. Named by
  `MallocStackLogging=1` + `malloc_history -allBySize`, live allocations at
  1000 threads:

  | calls | bytes | stack |
  | ---: | ---: | --- |
  | 491 | 39,280 | `Settings::set_last_read` → `std::map` node |
  | 714 | 34,272 | `afterhours::ui::wrap_text` → TextSpan vectors |
  | 616 | 29,568 | the same, at another width |
  | 511 | 31,040 | `MockClient::list_sessions`, the 520-row catalog |

  Only the first scales with threads opened rather than with widgets on screen,
  and it is capped at 2000 entries (entry 2). The remainder tracks the widget
  high-water mark and the catalog, both of which are one-time.
- **The 55 MB residue above the texture budget** (entry 4). It is Metal's own
  allocator high-water mark, and it is flat: 60 images and 180 images cost the
  same. Not worth chasing from this side of the API.

---

## How to check a change against this

```bash
# the whole ladder, twice, and compare the KB column
/tmp/ladder.sh 500 8 500          # see the invocation at the top

# just the retention question
... HANABI_MEM_CHURN=1000 ... | grep 'STILL HELD'

# name what is retained
HANABI_MEM_HOLD=180 MallocStackLogging=1 ... &
malloc_history <pid> -allBySize | grep -E '^[0-9]{3,} calls'
```

A count that matches the number of threads churned is a per-thread allocation;
the mean block size says what kind of thing it is. That is how the Metal leak
went from "somewhere" to six exact calls in one run, and it is still the
fastest tool here.
