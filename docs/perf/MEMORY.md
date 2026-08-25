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

**The GPU half is no longer blind** — entry 5 adds `src/util/gpu_mem.h`, which
reads `-[MTLDevice currentAllocatedSize]` through the device sokol already
created, and the soak gate reports and gates it. Entry 5 is also where the
worst finding in this file is, and it is not a leak: the image cache was
allowed four times more textures than the GPU has sampler slots for, and past
that ceiling afterhours returns textures that report their real dimensions and
cannot be drawn.

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
- **Not fixed then, fixed in entry 5b** — hanabi held full-resolution pixels to
  draw a 64px chip, because afterhours' `load_texture` takes a path and nothing
  else. `afterhours_gaps.md` #125. It is 4x rather than the 28x claimed here:
  one texture serves both the chip and the 420pt inline view, so the reduction
  is bounded by the LARGER box, not the chip.
- **Also not fixed then:** the 32 MB budget above was counted in `w * h * 4` and
  was really costing 47 MB, and the 512-entry cap it sat behind was eight times
  what the GPU can represent. Both in entry 5.

### 5. The GPU side, which every instrument in this file was blind to

This is entry 4's "not fixed" line, done — and three other things found on the
way, two of them worse than the one that was being looked for.

**The instrument first, because there was not one.** A GPU texture is not a
malloc block, so `soak.h` cannot see it and `mem_ladder.h` sees only RSS, which
is one page-granular number for the whole process that attributes nothing.
`src/gpu_mem.mm` asks the driver instead: sokol already hands out the
`MTLDevice` it created (`sg_mtl_device`), so `-[MTLDevice currentAllocatedSize]`
is every GPU byte the process holds — the font atlas, the glyph textures, the
offscreen render target and sokol's own buffers included, none of which
afterhours reports and none of which hanabi creates.

It reports two numbers of deliberately different kinds. `device_bytes()` is
ground truth and cannot say whose bytes they are; `hanabi::gpu::ledger` is
hanabi's own count, taken where hanabi asks, and knows the caller but not the
driver. A flat ledger under a climbing device is a leak hanabi does not own.

**What the number said immediately.** 45,616 KB of GPU memory at the window
rung, before the app has drawn anything, and 46,224 KB for the empty app. Two
thirds of this app's malloc-side memory is spent before it shows you anything
(top of this file); so is 46 MB of GPU memory that nothing here could see.

**Every number in this entry is against `main` at `cc9fae1`** (`perf/scroll`,
`perf/text` and `perf/retire` merged), measured after rebasing onto it, with
the "before" arm taken at this branch's first commit -- which adds the counter
and changes nothing else, so both arms are the same cache read by the same
instrument. The empty-app GPU baseline is 46,224 KB in both.

**The GPU figures did not move across either rebase**, on three successive
bases, to the kilobyte -- 82,064 / 79,440 / 49,296 KB for the three image sets
every time. That is the expected answer and also a check on the instrument: a
sidebar row and a text measurement are not textures, so a branch that removed
thousands of one and memoised the other should move RSS and leave the device
counter alone, and that is exactly what happened. The RSS figures did move, and
are quoted against the newest floor.

#### 5a. The estimate every budget was built on was 32% low

`w * h * 4`. afterhours' `load_texture` builds and uploads a full box-filtered
**mip chain** at image creation — sokol has no runtime mipmap generation, so
the levels have to be supplied up front. A 640x480 RGBA8 image is 1,228,800
bytes of base level and **1,638,352 bytes resident**.

So the 32 MB image cache was costing 47 MB. Measured on the ladder's image
rung, 60 640x480 PNGs attached and removed one at a time, against the empty-app
baseline of 46,224 KB:

| | before | after |
| --- | ---: | ---: |
| images held | 27 | 20 |
| the cache's own estimate | 32,400 KB | 31,999 KB |
| device, over baseline | 48,384 KB | **35,840 KB** |
| estimate error | +49% | +12% |
| ladder rung peak RSS | 98,400 KB | 84,384 KB |

The budget did not change; it became true. A cache that says 32 MB and costs 47
is worse than one with no bound, because the number is what anyone sizing the
app will believe. The residual 12% is Metal's own alignment and padding, which
is the honest floor for an estimate computed from outside the driver.

#### 5b. Decode to the size it is drawn at — 4x, and the pixels are identical

`afterhours_gaps.md` #125, closed on hanabi's side. A Retina screen grab is
3024x1964: 31.6 MB resident, to draw an image the transcript caps at 420 points
tall in a 644-point column.

**Why it is safe is a stronger claim than "close enough".** afterhours already
box-filters every uploaded image into a mip chain, and the GPU already samples
a reduced level when it draws minified. The pixels on screen were never coming
from the base level. `src/ui/decode_to_fit.h` does the same halving with the
same 2x2 box filter *before* the upload and stops keeping the levels above the
one that gets sampled. Verified rather than argued: `hanabi::downscale::halve`
was run against the vendored `build_mip_chain` over 3024x1964, 640x480,
1023x777 and the degenerate 2000x3 — **39 levels, zero mismatches**. The only
level that no longer exists is one this app never samples, at any `ui_scale`
afterhours permits.

Twenty 3024x1964 PNGs through the ladder, four surviving in the protection
window:

| | before | after |
| --- | ---: | ---: |
| per image, resident | 30,931 KB | **7,731 KB** |
| the cache's own estimate | 92,799 KB | 30,925 KB |
| device, over baseline | 131,264 KB | 33,216 KB |
| ladder rung peak RSS | 272,000 KB | 138,176 KB |

Peak RSS halves rather than quarters because the decode buffer still exists for
the moment before the upload; it is freed *before* the upload rather than
after, so the full-resolution copy and the new chain are never both live.

Both frozen references are byte-identical across the change (md5
`b2946f20559c46232a7bafd5455bf96e`, `4a3b0fa2059a24e79b4355aad2570210`;
`compare.py` 6.26% raw / 7.35% structural on 01 and 3.80% / 4.00% on 02, before
and after). Expected rather than reassuring — neither reference contains an
inline image, so that scores the chrome. The claim about the image is the
bit-exact mip comparison above.

#### 5c. The bound that bytes cannot express: 512 textures against a pool of 64

The worst thing found here, and it was found while rehearsing a gate.

sokol allocates every GPU object from **fixed pools** — images 128, samplers
64, views 256 — set at `sg_setup`, which afterhours calls with a
default-constructed `sg_desc` and no hook. `load_texture` makes one of each per
texture, so **samplers are the binding constraint at 64**. Measured in a process
that had done exactly what hanabi's launch does: the sampler pool ran out after
**61** loads and the image pool after 124.

And `load_texture_from_pixels` checks the image and the view and **not the
sampler**. Past the 61st texture it returns a `TextureType` with the file's real
dimensions, a valid image, a valid view and `sampler_id == 0` — which
`inline_image::available`, the composer chip and `bubble_height`'s image term
all read as loaded.

The image cache's entry cap was **512**. The byte budget could not have caught
that and it is worth being clear why: bounding bytes does not bound objects,
and the way to hold 512 textures is for them to be small. A 96x96 avatar is
49 KB, so 32 MB is six hundred of them and the byte budget never fires at all.
Eighty of them through the real app:

| | before | after |
| --- | ---: | ---: |
| entries held | 80 | 32 |
| unsamplable, and believed loaded | **20** | 0 |
| device, over baseline | 7,680 KB | 3,072 KB |
| ladder rung peak RSS | 50,896 KB | 45,904 KB |

Capped at 32 — 64 samplers less 16 reserved for the app's own atlases, halved
again for headroom — with a `static_assert` tying the cap to the pool constant,
and `decode_to_fit` rejecting an unsamplable texture at the one seam every
texture in hanabi comes through. `afterhours_gaps.md` #210.

#### 5d. A texture leak does not grow. It plateaus, and then images stop.

The other consequence of a fixed pool, and it is a warning about this whole
class of gate. A texture leaked every frame in the soak loop grew GPU memory to
264,048 KB and then sat **flat to the kilobyte for 800 frames**, because the
image pool was full and every further allocation failed. A slope-based leak
detector sees a texture leak for about two seconds and then goes green — and
the soak's own 120-frame settle pass is almost exactly long enough to hide the
whole of it.

The gate is still worth having (it reads +0.0 KB on clean runs and 11,028x over
budget on that leak, with the settle disabled so the window opens in time), but
the honest statement is that it catches the first two seconds of a texture leak
and nothing after.

#### 5e. What is bounded, checked with the new instrument

| | bound | verdict |
| --- | --- | --- |
| glyph atlas | one **fixed** 2048x2048 R8 image, 4 MB, at init | bounded, and it never grows: 94 glyphs at fifty sizes moved the device counter 0 KB |
| icon atlas | one texture behind a state guard | correct |
| offscreen render target | `set_window_size` unloads before reloading; the windowed path never resizes it | correct |
| `inline_image` cache | 32 MB of true GPU bytes **and** 32 entries | correct as of 5a/5c; it was neither before |
| every soak arm | `threads`, `tabs`, `read`, `search`, 1500 frames over a 2000-session catalog | GPU 43,600 KB in every bucket of every arm, to the kilobyte |

**The glyph atlas's failure mode is not memory, it is layout, and it is
silent.** Past the point where it fills, `measure_text` returns a wrong width
and then zero: the 94-character ASCII string measures 5933 px at 144 pt, 230 px
at 192 pt and **0.0 at 288 pt**, with no error and no log. Every wrap, hug and
ellipsize in this app is computed from `measure_text`, so a string that measures
zero lays out as absent. hanabi is nowhere near it — four faces x fourteen sizes
x the ASCII set, and again at 6x, all fit — but nothing would say so if it were.
`afterhours_gaps.md` #211.

#### 5e-bis. Widget retirement does not orphan a texture, and could not

`perf/retire` sweeps widgets nothing has built for 90 frames, which changes how
long an image widget lives, and the obvious worry is a texture whose owning
widget was swept and whose bytes stay resident. It cannot happen here, for a
structural reason worth stating: **a widget never owns a texture in this app.**
`inline_image`'s cache is a path-keyed store that owns every texture it makes;
a widget's `on_draw_fg` closure holds a `std::string` path and asks the cache
for the handle each frame. Retiring the widget removes a caller, not an owner.

Measured anyway, with the device counter, over the three arms that change
screen and therefore retire the most: `views` (entities swinging 169 to 283 as
screens come and go), `churn` and `mixed`, 2000 frames each. GPU **43,600 KB in
every bucket of every arm**, to the kilobyte, slope +0.0.

The failure mode that DOES exist is the opposite one, and it is deliberate: an
image whose widget was swept keeps its texture until the LRU evicts it, which
is the protection window doing its job — dropping it would mean re-decoding a
PNG on the next frame that draws it. It is bounded by the byte budget and the
entry cap, both of which are 5a and 5c.

#### 5f. The autorelease leak's smaller cousin, and one thing to know before probing

`load_texture` autoreleases too: `sg_make_image` builds an
`MTLTextureDescriptor` and friends. 2000 load+unload pairs leak 646 KB of live
heap bare against 420 KB pooled — **113 bytes a load** that only a pool
reclaims. `scripts/check_autorelease.py` now requires a pool in scope for a
texture call as well as for `begin_frame`, and found two sites the first time it
ran: the icon atlas's load and the image cache's eviction. Neither was leaking,
because both are reached from inside a pooled frame loop — until the pre-warm
started calling one of them before any frame exists.

**#200 is a heap leak, not a GPU one, and the two instruments disagreeing is
the point.** The resize arm leaks five Metal render pipelines per resize --
+4,928 KB of RSS and +65,966 live blocks per 1000 frames, 2.4x and 3.3x over
their budgets. The GPU column on that same run reads **-8,192 KB**: it goes
DOWN, because the arm sweeps the render target smaller, and the leaked objects
are Objective-C descriptors on the heap that `currentAllocatedSize` does not
count at all. A GPU gate would never have caught it and a heap gate would never
have caught entry 5's textures. Neither instrument subsumes the other, which is
why both columns are in the table.

And, learned by crashing into it: **sokol does not free a destroyed Metal object
until the next frame boundary.** Create and destroy in a loop with no
`begin_frame` between and it exhausts an internal id pool and trips an assert in
`_sg_mtl_alloc_pool_slot`. A texture is not freed when you destroy it; it is
freed when you next draw. That also means a cache eviction is over budget by one
entry until the frame ends. `afterhours_gaps.md` #212.

### 6. What is correctly bounded, and left alone

Checked, and no change made. A bounded cache is not a bug.

| | bound | verdict |
| --- | --- | --- |
| `transcript_cache.h` | 5 threads x 20 messages, LRU with move-to-front on every access | correct, and the comment is accurate |
| `api::disk_cache` | `trim_to_cap`, 1 GiB default, LRU by mtime, trims long transcripts before deleting them, never evicts `sessions.json` | correct, and more careful than it needed to be |
| `AppComponent::liveSubs` | one per OPEN TAB, reaped off the UI thread when the tab closes (`loader_system.h:988`) | correct |
| `AppComponent::expandedPiles` | grows only on a click, cleared on thread change | user-intent, correctly unbounded |
| `Settings` `starred`/`muted`/`archived`/`tool_fold`/`row_order` | one entry per deliberate act on one thread | user-intent, correctly unbounded |
| `AppComponent::composerAttachments` | 5, the cap the orchestrator's own message route enforces | correct — the *texture cache behind it* was the bug (entry 4) |

### 7. What measured as noise

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
- **The soak probe's own RSS column.** Giving `soak.h`'s verdict the same
  median treatment tightened the live-block column from a 18% spread across
  three identical runs (5194 / 4962 / 4395) to 3% (3768 / 3888 / 3840). It did
  nothing for RSS (704 / 656 / 416 before, 400 / 336 / 800 after), because RSS
  is page-granular and lags the allocation by a long way — the ladder's fix for
  that is `malloc_zone_pressure_relief`, which the soak cannot call mid-run
  without perturbing the steady state it exists to measure. Read the soak's
  block column, not its RSS column.
- **Windowing the soak's FRAME TIME column.** Tried, reverted, and the comment
  in `soak.h` says why: the memory columns are cumulative so a median window is
  strictly better, and frame time is not. Moving its early anchor out of the
  launch burst made two runs in three report "frame time is trending UP" —
  correctly, because the `threads` scenario opens a tab every 30 frames and
  more tabs really are slower, and uselessly, because a leak detector that
  fails when the machine is busy is a leak detector nobody reads.
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
