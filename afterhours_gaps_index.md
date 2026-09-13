# afterhours gaps — index

`afterhours_gaps.md` is ~14,000 lines and 237 parsed entries, written by dozens of
agents over several days. As a record it is good. As a work queue it is
unusable: you cannot see what matters, what is one change, what is the same
finding filed four times, and what has already been fixed under it.

Built against `afterhours_gaps.md` at this branch's head. The file grows while
you read it — it gained thirteen entries from three other branches during the
two hours this took — so the counts below are a snapshot and the *shape* is the
durable part.

This file is the front matter. It changes no entry's number — **source code and
eight commit messages cite these numbers, so nothing is renumbered here** — it
only sorts, weighs, groups and corrects them.

Each detailed entry that records a shipped workaround, proof patch, negative
result, or Hanabi-owned performance finding has a `**Hanabi reference.**`
subsection. References use stable repository paths plus symbols, test names, or
grep-able strings instead of line numbers; `None — no app-side workaround is
implemented` is explicit when the prose might otherwise imply code exists.
`scripts/check_gap_references.py` enforces coverage and rejects referenced files
that do not exist, and `make source-checks` runs it.

**Read section 1 and stop.** The rest is reference.

| | |
|---|---|
| Numbered headings parsed by the reference checker | **261** |
| Distinct numeric gap numbers | **256** (several numbers are used twice, #31 three times — §5) |
| Plus the `AN-8`…`AN-12` animation sub-series | **5** |
| **Rows in the triage table (§6)** | **265** rows, **265** unique identifiers — includes index-only ids with no detailed entry |
| Standalone live asks | **143** (152 less the ten fixed at 1ac6db2, plus #593) |
| Live but subsumed into a family canonical | **56** (§3) |
| Already fixed upstream | **37** (24 closed at pin 9ff9079 and REMOVED; 13 more at 1ac6db2 — #137 #103 #575 #573 #72 #275 #277 #340 #435 #436 #210 #255 #85 — fixed and kept IN PLACE, see the second closure table) |
| Deliberate NEGATIVE results — do not promote | **24** (§4) |
| hanabi/platform-owned, not afterhours' | **26** |
| **Rows explicitly marked wrong** | **5** (§2) |

Everything in §2 was checked by reading `vendor/afterhours` at the pinned
submodule **9ff9079**; the 1ac6db2 closures and re-tests above were read at that pin. Nothing here was verified by running the library; each
correction says so.

### Closed and removed at pin 9ff9079

Twenty-four entries whose defects are fixed in the pinned library are DELETED
rather than annotated: a closed gap left in a work queue is read as work. Each
was verified against `vendor/afterhours` at 9ff9079 -- the fixing commit is an
ancestor of the pin, or the fixed behaviour was read in the pinned source --
not taken from a postscript.

| gap | fixed by | what closed it |
|---|---|---|
| #28b | verified in pinned source | a 2nd child of a custom-background div renders; `on_draw_fg` on a bg div fires (69 live uses in hanabi) |
| #29a | `5b15bab` | hover resolves through the subtree, so a hoverable child no longer steals the parent row's fill |
| #29b | `1b568f9` | `text_input` placeholder text and colour |
| #31c | `209f80e` | the sokol macOS backend filters control codes (DEL, U+007F) out of the CHAR queue |
| #32b | verified in pinned source | the caret is placed by `position_text_ex` measurement rather than inside the last glyph |
| #17 | `817d00e`, `1b568f9` | `text_input` honours an explicit font size and a caller's background; placeholder text and colour |
| #22 | `a1b9a4b` | styled `TextSpan` runs word-wrap |
| #24 | `a1b9a4b` | hard newlines honoured in labels |
| #26 | `a1b9a4b` | `HasScrollView` draws a real scrollbar |
| #42 | `2b207d4` | label measurement exposed |
| #43 | `5161dbf` | component lookup off `dynamic_cast` |
| #47 | `2d6f23d` | quoted `expect_no_text` can fail (control-tested here) |
| #55 | `ca1736f` | `right_click_text` / `right_click_ui` |
| #113 | `2caf525` | retry-timeout names its command |
| #115 | `2393fe3`, `c682382` | widget retirement preserves survivor order |
| #161 | `2caf525` | full tree dump, no 200-character cap |
| #180 | `5996464` | `hash_call_site` replaces the app's `mk()` |
| #192 | `2caf525` | `dump_ui` registered |
| #200 | `1ad3360` | render-pipeline leak per resize |
| #211 | `bdea3b9` | atlas exhaustion reported |
| #220 | `2ccc38e` | `viewport_size` optional on an unmeasured frame |
| #231 | `f607faa` | `wait_frames` counts ticks |
| #351 | `bdea3b9` | `fonsSetErrorCallback` registered |
| #352 | `bdea3b9` | `AFTERHOURS_FONT_ATLAS_SIZE` |

Ten more at pin **1ac6db2** (2026-09-12, range 9ff9079..1ac6db2, 68 commits), each
read in the pinned source and, where hanabi could see it, measured. These are
closed IN PLACE (the heading reads `FIXED at 1ac6db2`, the filed text stays under
`As filed`) rather than deleted: 55 lines across `src/`, `docs/` and these two
files cite the numbers, and the reference checker refuses a citation of a removed
id.

| gap | fixed by | what closed it |
|---|---|---|
| #137 | `82145f9` | sokol `measure_text` returns the advance, so the shared cache and the app's own measure agree; measured here as a 1px shift of every centred label and one more glyph per row in the narrow approval card (six scripts re-pinned, 162 baselines re-captured) |
| #103 | `82145f9` | the same: ink box → advance |
| #575 | `30c6ad6` + `82145f9` | `measure_text_internal` is gone; one measure, one answer |
| #573 | `1586a17` | `get_active_font`/`get_font` fall back instead of `.at()`-throwing on a missing face |
| #72 | `7736594` | `has_interacted` keeps `visual_focus_id` at ROOT until a deliberate focus move or click; `tab_walks_the_focus_ring.e2e` reads `ring off` with nothing named at rest |
| #275 | `0c67090` | `assert_within_parents` — the content-box containment walk beside the viewport one; adopted in `settings_segments_fit_their_row.e2e` and `a_compaction_divider_opens_its_summary.e2e` |
| #277 | `cc26cbc` | the inset is `ui::kTextInset` with `text_inset_for(rect)`; hanabi's three copies are bound to it |
| #340 | `b9844c2` | the draw-pass wrap is memoised (LRU keyed on runs and width, both draw paths) |
| #435 | `b9844c2` | same memo, plain wrapped labels |
| #436 | `b9844c2` | same memo, styled runs |

Also fixed and rewritten in place: **#210** (`865c4e6`,
sampler check + `AFTERHOURS_SG_SAMPLER_POOL_SIZE`), **#255** (`7208d0c`/`6daa71b`,
`has_editing_action` + the once-per-run report), **#85** (`cc26cbc`, the warn-once).
Narrowed, not closed: **#83** (ring off at rest, but the rule is "after any
interaction"), **#136/#87/#69** (`with_fit_content` exists; the bubble memo is not
migrated), **#420/#326/#455** (`height_of(index)` exists; no retained index), **#112**
(`with_tooltip` exists; no accessible name), **#223** (fixed timestep, deadline still
`dt`). Re-tested and still open: **#591**, **#592**, **#265**, **#266** (both proof
patches still red-before/green-after at 1ac6db2), **#350/#353** (`3f264ca` is the
raylib atlas packer; sokol's fontstash path is unchanged).

Three left a residue, and that is what survives: `1ad3360` exposed the mid-pass
render-target teardown (**#374**), `bdea3b9` reports that the atlas filled but
still cannot say WHICH measurement was short (**#350**) or draw a substitute
glyph (**#353**), and `aad267f` clips a focused field's top edge (**#375**).
#223, #181, #48 and #210 are rewritten to the scope that is still live rather
than kept whole.

---

## 1. The top ten

If ten things get fixed in afterhours, these ten, in this order.

The ordering rule is *pain per line of upstream change*, with two thumbs on the
scale: a defect that **fails silently and corrupts something downstream** beats
a defect that is merely painful, and a gap that **every app vendoring afterhours
must work around** beats one only hanabi hits.

---

### 1. #350 + #353 — the atlas reports that it filled, but not WHICH measurement it broke

**Upstream now warns.** `bdea3b9` registers `fonsSetErrorCallback` and warns
once when the atlas fills, and `AFTERHOURS_FONT_ATLAS_SIZE` sets the ceiling.
The three entries that described those absences are closed and removed (see
the closure table above).

**What is left is the dangerous half.** A warning says the atlas filled; it does
not say that the width a layout just used was short. That is #350 — return
whether the call that just measured dropped a glyph; `fonsTextBounds` already
knows, it gets NULL back from `fons__getGlyph` per refused codepoint and
discards it. And #353: a dropped glyph is not DRAWN either, so the string is
invisible as well as unmeasurable. Draw codepoint 0 instead of skipping the
quad and an invisible failure becomes the oldest visible one in typography.

The evidence is unchanged and still the best in this file, because the
condition was reached and watched rather than reasoned about
(`hanabi.exe --atlas-stress`, printable ASCII at climbing sizes):

```
  pt          width   detector
  64         2644.0   ok
  120        4836.0   FULL   <- the atlas can take no new rect
  124         622.0   fault  <- the app's own measurement is now WRONG
  136         231.0   fault
```

622.0 where the truth is ~5,000 is the dangerous number, not the 0.0 further
down: a zero is catchable, a plausible-but-wrong width is not.

hanabi carries `src/util/atlas_guard.h` for exactly this — a probe that asks
for a glyph the atlas has never held at a size it has never held, which is
exact rather than heuristic — and gates it with `scripts/atlas_gate.sh`. That
stand-in stays until #350 and #353 land.

### 2. #210 — CLOSED at 1ac6db2 (`865c4e6`): the sampler is checked and its pool is sized

**Upstream sized three of the four.** `11e7338` added
`AFTERHOURS_SG_PIPELINE_POOL_SIZE` (128), `AFTERHOURS_SG_IMAGE_POOL_SIZE` (256)
and `AFTERHOURS_SG_BUFFER_POOL_SIZE` (256), applied at both the windowed and
headless setup (`backend.h:227-234`). The "default `sg_desc`, no way to raise
them" complaint is closed for pipelines, images and buffers.

**The sampler pool was not sized, and sampler creation is still unchecked.**
It keeps sokol's `_SG_DEFAULT_SAMPLER_POOL_SIZE = 64` while its three
neighbours became tunable, so it is now the pool that runs out FIRST.
`load_texture` makes an image, a view **and a sampler** per texture; it checks
the first two and not the third. Past the 64th sampler it returns a struct with
a real width, a real height, valid ids and `sampler_id = 0` — which every "did
this load?" test a consumer can write reads as success.

Two small changes: add `AFTERHOURS_SG_SAMPLER_POOL_SIZE` beside the other
three, and check `sg_query_sampler_state` before returning the texture.

**Proof patch:** `vendor_patches/210-reject-unsamplable-textures.patch`
implements and verifies the correctness half, and `make verify-vendor-patches`
proves it is still absent from the pin — which is what keeps this entry honest
rather than asserted.

### 3. #136 + #116 + #135 — text: `with_fit_content` landed (partial), the cheap overloads did not; #137 and #340 are CLOSED at 1ac6db2

Text metrics have been called the number-one papercut in this file since day
one, and three weeks later they still are — the newest entry in the family
(#340) was filed today. Five entries, one subsystem, and the fix splits into a
correctness decision, a sizing feature, a cache and three overloads:

* **The decision (#137, ~1 line, and it is a correctness bug).** There are two
  measure functions and they disagree. `measure_text_internal` returns the pen
  **advance**; `measure_text` — the one the shared `TextMeasureCache` and the
  layout pass go through — returns the **ink bounding box**. Measured on the same
  string, same font, same frame: a consistent 2 px apart. The consequence is that
  the one cache the library ships for exactly this purpose is *unusable* by the
  app that needs it most, because adopting it moves every bubble 2 px. Pick one,
  use it in both, document which. **Proof-patch decision:** rejected in
  `vendor_patches/README.md`; the edit is one line but is not pixel-safe without
  an upstream choice between advance and ink semantics.
* **The feature (#136).** `ComponentSize{fit_content(max), ...}`. Nothing sizes a
  box to its own text, so the universal chat-bubble layout costs a wrap plus a
  measure per line in app code, and forces every memo of it to hold two widths.
  AutoLayout already has the text, the font and the width, and is already
  wrapping. One sizing mode deletes both memos and a whole class of bug.
* **The render-path re-wrap (#340), newly measured and the biggest single
  allocation site in the app.** `draw_runs_in_rect` calls
  `detail::wrap_runs_to_width` on the DRAW pass, per frame, for text that has
  not changed at a width that has not changed — building a fresh
  `vector<vector<TextSpan>>` and a fresh `std::string` per span each time. On a
  480-message transcript that one library call site is **~10% of every
  allocation the app makes**, and it doubles with a second pane. The app cannot
  reach it: it is a free function on the draw side with no state parameter, and
  hanabi's own memo is on the measure path. Cache the wrap on `HasLabel` keyed
  by (rect width, font size, spacing) — the same shape as the `TextMeasureCache`
  that already exists, invalidated by the same edits that rewrite `spans`.
* **The same defect in `text_area`, and this one has a PROVEN patch (#305).**
  `state.layout_cache.rebuild(...)` is called unconditionally every frame
  (`text_area.h:228`) and its probe reaches past `TextMeasureCache` to the raw
  backend `measure_text`, building a `std::string` per probe. The guard is
  already written and never called: `HasTextAreaState::needs_layout_rebuild()`
  has no call site anywhere in the tree. Measured in hanabi's composer standing
  still: **+196 `operator new` per frame for a 130-character draft that does
  not even wrap**, forever, at 60Hz.
  `vendor_patches/305-text-area-wraps-every-frame.patch` holds the inputs
  `rebuild` reads and switches the probe to `measure_text_line`; applied to the
  pinned 428047e -- a HISTORICAL measurement at that revision -- it takes that
  arm from 1007 to 824 allocations a frame, and a
  six-line draft from 1030 to 847 — below what the same string cost in a
  single-line `text_input`. Unlike #340 the app has NO reach at all here:
  `with_word_wrap(false)` does not take the cheap path, because
  `text_layout.h:51` turns a zero wrap width into `1e9f` rather than into the
  `max_width <= 0` early-out `wrap_text_to_width` itself provides.
* **The cheap overloads (#116, #135, #191).** "How much of this string fits in W"
  and "how many lines is this" — the two questions a list UI and a transcript ask
  constantly — can only be answered today by materialising every wrapped line
  and throwing it away. Hanabi measured **3,456 cache lookups per frame** to
  produce one integer per message. Three overloads beside the existing ones,
  sharing the same break loop so they cannot disagree with it.

### 4. #275 — CLOSED at 1ac6db2 (`0c67090`): `assert_within_parents`

**A few lines, in a walk that already runs.** Three things look like they answer
"is this drawing outside its box" and none of them does: the layout warning is
main-axis-only and goes to a log nobody reads, and `assert_no_overflow` — the one
assertion — measures each element against the **viewport**, not against its
parent's content box. Confirmed in the source. On a report of *"many buttons are
going outside the bounds"* it named **1 of 55**. `assert_no_overflow` already
walks every laid-out element with its parent one hop away; adding the
parent-box comparison beside the viewport one turns the only containment
assertion in the toolkit into one that answers the question it is named for.

### 5. #326 + #170 + #224 — virtualization only handles uniform row heights, which is not what a list is

`imm::virtual_list` exists and is good, and it divides by **one** `row_height`.
Every list in a real app has measured, differing heights: bubbles, tool piles,
cards that wrap to two lines. So hanabi hand-rolled the same window **three
times** — sidebar, digest, transcript — and each copy re-derives the same
prefix-sum-over-measured-heights. Three copies of one algorithm is where the
fourth gets it wrong; a 2,000-row sidebar built 2,000 rows to show nineteen,
at 17.2 ms a frame.

A second entry point taking a `std::function<float(size_t)>` height accessor and
binary-searching the prefix sum covers it, with the uniform case unchanged as the
fast path. #224 is the same ask one level up: `measure_config(config,
available_w)` — how tall would this child be, without minting an entity — is
what turns every hand-rolled window in this file into a call.

### 6. #75 + #100 + #84 + #91 + #109 — the label inset is NAMED now (`ui::kTextInset`, `cc26cbc`; #277 closed, #85's warn-once landed) but still cannot be set per single-line label

**Seven entries. One literal.** `rendering.h` positions every label at
`Vector2Type{5.f, 5.f}` (verified at the call site), and padding on a label-only
element is *silently ignored* — proven twice over, by building at `pixels(12)`
and `pixels(40)` and getting byte-identical frames. The cost is written down in
the file: **#109 cost a whole region, #85 cost a day**, and the app now carries
two different constants for the one number because a text child and a drawn
child of the same parent land on different columns. It is in device pixels too,
so every label slides leading-ward as `ui_scale` rises (#100).

Two changes, either of which helps: name the constant and expose
`text_origin_for(entity)` so an app can put a drawn thing on the text's column;
and make padding on a label-only element a **warn-once** instead of silence.
Honouring the padding is the wrong fix — it would silently move nine live labels
in this app alone. Silence is what made this cost days.

**Proof-patch decision:** rejected in `vendor_patches/README.md`. The same 5px
contract is coupled to multiple 5px/10px calculations across plain, wrapped,
styled, immediate, batched, ellipsis, and text-input paths; a partial patch
would create divergent pixels.

### 7. #255 — CLOSED at 1ac6db2 (`7208d0c`): `has_editing_action` + the once-per-run report; hanabi static_asserts its four

`if constexpr (magic_enum::enum_contains<InputAction>("TextWordLeft"))` — verified,
eleven of them in `text_input/component.h` alone. Word motion, word delete, undo,
redo, cut, copy, paste and select-extend all exist in the library and all
compile out to nothing if the consumer's enum happens not to carry a name
nobody wrote down. **No error, no warning, nothing to grep.** This cost hanabi
word editing *for its whole life* — "alt-backspace never landed" was never a
bug, it was a name that was never typed.

This is the discoverability failure that costs a consumer features permanently
and invisibly, and any of three cheap fixes closes it: a documented list of the
names, a startup warning naming each action that resolved to nothing, or a
`static_assert`-able trait so the consumer opts out on purpose.

**Proof patch:** `vendor_patches/255-word-editing-capability.patch` (+7) exposes
that trait; `tests/vendor_probes/word_editing_capability_probe.cpp` is a compile
failure before and classifies complete/incomplete enums after.

### 8. #83 + #265 + #266 + #267 + #46 — the focus ring (#72 CLOSED at 1ac6db2: no ring at rest; the `:focus-visible` rule is still hanabi's)

Six entries, one widget. A ring is painted **at rest**, on whatever happened to
be focusable first, so the app opens with a blue box around a row nobody
touched — in every screenshot the harness has ever taken. There is no
`:focus-visible`, and `FocusSource` cannot be used to build one because it is
reset to `Grab` at the top of every frame, so it answers "who claimed focus this
frame", not "how did this come to be focused". Hanabi now ships a hand-rolled
`src/ui/focus_visible.h` to fake it.

Under that: the ring is **three** outlines, and the two you did not ask for take
their colour from the ring's own luminance rather than from the backdrop, so a
requested 1px hairline measured as a 3px white-blue-white band; its offset is one
global number for the whole app, so a UI with both full-bleed rows and inset
chips cannot have a correct ring on either; and it is drawn from focus state with
no reference to whether focus can move at all, so an input map missing one
binding paints a ring that is a lie.

Fix in order of cheapness: a sticky `focus_visible` bool on the context (~10
lines, and the distinction already exists on `FocusSource`); gate the two
contrast edges independently; let a widget carry its own ring offset the way
`HasRoundedCorners` already overrides its radius.

**Proof patch:** `vendor_patches/265-focus-ring-contrast-toggle.patch`
(+17/-12) implements the independent edge gate while preserving the three-line
default in both renderers; `tests/vendor_probes/focus_ring_contrast_probe.cpp`
proves three outlines by default and one with contrast disabled.

### 9. #374 — a resize tears down the render target mid-pass and aborts the process

`window_manager::set_window_size` destroys and recreates the headless render
target inline, and every caller reaches it from inside an open pass, so a
pass's attachments are freed with its own draw commands still recorded against
them. Sokol aborts: `VALIDATE_APIP_ATTACHMENTS_ALIVE`. Exactly the seven
`tests/ui` scripts containing a `resize` abort at this pin; 32 scripts at the
same window sizes are unaffected.

It was survivable by accident until `1ad3360` — every render texture owned an
sgl context, and unload destroyed the recorded commands with it. That commit
correctly stopped leaking five render pipelines per resize, and uncovered this.

**Two lines upstream:** end the pass before recreating the target. Until then
hanabi defers the swap to a frame boundary (`src/util/gfx_resize.h`), gated by
`scripts/stress_resize_gate.sh` and `scripts/check_resize_deferral.py`.

### 10. #375 — a focused field's border loses its top edge

`text_input` sets `Border::all(ctx.theme.accent, 2px)` on focus, and the top
row does not render: measured 8 accent pixels on `18d_palette_empty_dark` y=160
where 506 belong, with the bottom row and both verticals intact.

It is a CLIP, not an overpaint — four candidate painters were removed and the
screen re-rendered for each (hanabi's own border, the theme ring, the field's
fill, a foreground stroke) and the edge stayed missing every time. Nothing an
app draws at the field's own top edge renders at all.

Hanabi's workaround is a wrapper that owns its own rect
(`src/ui/edged_field.h`), gated by `scripts/focus_edge_gate.py`. Upstream: emit
the focus border outside the field's clip, or inset children by the border
width.

## 2. Corrections — entries that are WRONG, or right when written and overtaken since

An entry claiming the library cannot do something it can is worse than no entry.
Four were already known. **Nine more are recorded here**, each verified against
`vendor/afterhours` at 428047e (HISTORICAL; this index's current-source
assertions are at 9ff9079) and each postscripted in place in
`afterhours_gaps.md` rather than deleted.

### Already known before this pass

| # | What was wrong |
|---|---|
| **upstream 2393fe3** | Claimed the app was structurally stuck. `existing_ui_elements` is a **public inline global**, and `mk` can be shadowed through the single using-declaration — one word, and the workaround is fifty lines. The gap stands; the "impossible" framing did not. Retracted in place. |
| **#117** | The **commit** blamed a vendored constant. The gate the gesture goes through is hanabi's own — `src/ui/text_select.h:56`, `kMultiClickMs = 400`; `MULTI_CLICK_TIME` lives in the text-input widgets and is not on that path. Same failure as upstream 2393fe3, one file over. The entry has since been rewritten; the commit stands as written. |
| **#27** | The "~8.6 ms idle-frame floor" was mostly hanabi's own missing `-O2`, not the per-frame rebuild. Retracted in place. The design observation stands; the number was wrong. |
| **#49** | Corrected by **#256**: `CMD+` in a script parses as Ctrl and `SUPER+` is dropped, so a Cmd chord IS scriptable if the app binds the Ctrl twin. #49's "the whole shortcut surface is unreachable" is too strong. |

### Found in this pass

| # | Verdict | What the source actually says |
|---|---|---|
| **#79** | **The ask has landed** | "A label cannot be told to fit a width" — `TextOverflow::Ellipsis` exists on `ComponentConfig` (`component_config.h:428`) and the renderer binary-searches the longest fitting prefix at `rendering.h:737-782`, which is this entry's own proposed fix, in its own proposed place. hanabi already uses it twice. Two caveats keep a much smaller ask alive: the ellipsis measures with the ink box (#103/#137) and hardcodes `rect.width - 10.f` (#75/#277). |
| **upstream a1b9a4b** | **Resolved upstream** | "Wrapped text ignores hard line breaks." It does not. `rendering.h:629` computes `has_hard_break` and the branch below reads "Only soft-wrap when asked; otherwise break on `'\n'` alone"; `ui::wrap_text`'s doc comment says "Honors hard `'\n'`". |
| **upstream a1b9a4b** | **Resolved upstream** | "`HasScrollView` has no built-in scrollbar." It has all of it: `show_scrollbar` (defaults **true**), `scrollbar_thickness`, `scrollbar_min_thumb`, `scrollbar_geometry()` which auto-hides when content fits, and `HandleScrollbarDrag` mapping a thumb drag back into `scroll_offset`. What is left is *overlay* behaviour — **#94**, written against these fields. |
| **#23** | **Overtaken** | "No list virtualization for scroll views." `imm::virtual_list` exists (`imm_components.h:159`) with spacers and a 4-row overscan. What it cannot do is non-uniform heights — the live entry is **#326**. |
| **gap upstream a1b9a4b follow-up** (line ~1099) | **Wrong** | "Styled spans are COLOR-only (no per-run weight)." `TextSpan` is `{text, color, weight}` (`ui_core_components.h:457`), and its own comment documents the fallback when the app registered no bold face. Per-run **weight** is first-class; per-run **font/slant** is not. hanabi's bold renders as colour because hanabi bundles no bold face (**#77**), which is a resource decision, not a library one. |
| **#326** | **Wrong symbol** | The function is `imm::virtual_list`, not `imm::vlist` — nothing named `vlist` exists outside three debug-name literals. The entry as written is not greppable, and it is the entry a reader lands on when hunting the primitive. Everything else in it checks out. |
| **#265** | **Partly overtaken** | "Nothing in the theme turns the contrast edges off." `focus_ring_thickness = 0` now returns early before any outline is emitted (`rendering.h:220-223`), and the comment above it names this exact finding as the reason. That total off switch is what makes hanabi's `focus_visible.h` possible. The entry's real ask — keep the coloured ring, drop the two edges — is still impossible. |
| **#117** | **Stale evidence, third time** | The entry quotes `double_click 415 225`; the tree at `2fd9e84` reads `415 252`. Master moved it in `7f15b253444b` while the entry was written on a branch that never picked the fix up. Separately, `click_text` — half of this entry's proposed fix — is already a runner command and three hanabi scripts use it. The remaining ask narrows to `double_click_text` / `triple_click_text`. |
| **upstream 2ccc38e** | **Narrower than stated** | "A virtualizing consumer must build the WHOLE list once." Not inside `virtual_list`, which renders a fixed 61-row initial window when `viewport_size` is still zero (`imm_components.h:189-191`). True only of a consumer windowing by hand, which is what hanabi does *because of* #326. |

### One entry I expected to be wrong and is not

**#38** ("a container cannot report hover unless it is clickable") looks
superseded by `ctx.mouse_in_subtree()`, and the entry itself already credits
that primitive and marks **#29 resolved** on the strength of it. But #38's
residual ask is different and still live: the child's own hover *fill* still
paints, and `with_skip_hover_override()` — a one-line setter for a field that
already exists — is not there. Left alone. Recorded because the near miss is
the same shape as the two entries that went wrong.

---

## 3. Duplicates and families

**Fourteen families cover 135 of the 248 indexed headings.** Fix the canonical one and the
rest either close or shrink to a footnote — 51 of them are subsumed outright
(the `dup→` rows in §6) and the remainder get smaller. Where the members were
filed by different agents from different features, that is noted: it is the
strongest evidence that the underlying mechanism, not the symptom, is the thing
to fix.

| Family | Canonical | Also filed as | The one mechanism |
|---|---|---|---|
| **Widget lifetime** | **#171** | #162, #163, #146, #160, AN-9 | Retirement itself landed upstream (`2393fe3`, `c682382`) and hanabi uses the library sweep. What remains: the library's own entities are invisible to a consumer, a scroll view clamps against children that are not there, and an exit animation has nothing to animate. #160 is the *cost* of the sweep; #146 is how you would gate it. |
| **Text measurement and wrap** | **#136** | #135, #116, #137, #191, #103, #82, #190, #69, #87, #79, #340, #435, #436, #437, #450, #570, #574, #575, #576, #579 | No content sizing and no reusable draw-layout artifact, plus no point-size contract or font generation. Apps otherwise re-derive metrics and can measure the backend-global face after drawing another. |
| **The 5px label inset** | **#85** | #75, #277, #84, #91, #100, #109 | One literal `Vector2Type{5.f, 5.f}` in `rendering.h`, unexposed and unqueryable, that also swallows the element's own padding in silence. #91 is the fuller statement, #85 carries the byte-identical-frames proof, #109 is the second time it cost a region. |
| **Focus ring** | **#83** | #46, #72, #265, #266, #267, #263, (#592 sideways: where a click PUTS focus) | One `focus_ring_for`, and no `:focus-visible`, no per-widget offset, no independent contrast edges, no check that focus can move. #263 (`text_area` draws no ring at all) is the same code path from the other end. |
| **Virtualization** | **#326 / #420** | #23, #170, #31a, #224, #147, #455 | `virtual_list` divides by one row height and has no retained prefix-height index or range invalidation. Everything else here is a consumer working around that: windowing by hand against state the library writes after the build. #455 carries the current busy-event CPU and allocation measurements; #420 carries the retained index workaround and range-invalidation ask. |
| **Alpha and antialiasing** | **#92** | #13, #15, #106, #96, #481 | `sample_count` is pinned at 1 and the sokol_gl default pipeline has blending off, so nothing small or translucent can be drawn correctly. #96 is the **negative** result that limits the family (see §4); #481 is the status-pill instance and current measured workaround. |
| **Text input vs text area** | **#67** | #29b, #33b, #34b, #35b, #57, #65, #105, #261, #262, #263, #260, #258 | Multi-line is a different widget, not a mode, so every property `text_input` grew has to be grown again on `text_area`: placeholder, background, focus ring, selection-collapsing word motion, and the harness assertion that can see it. Thirteen entries; most of them are four lines each. |
| **Scripted-test addressing** | **#51** | #61, #73, #59, #104, #117, #232, #285, #86, #147, #308, #337, #437, #456, #457, #483 | A script can address a named element or a raw coordinate, and nothing in between — no text run, no colour, no absence, no scope, no gesture-by-name. #337 is #147 with a second pane: a debug name stops naming ONE widget the moment the app renders the same code twice. #483 measures the screenshot tax of the missing colour properties. |
| **Accessibility semantics** | **#112** | #458 | Icon-only controls have debug names and pixels but no platform role, accessible name, description, or value. #458 is the message/tool proof and downstream visible-label cost. |
| **Per-frame allocation** | **#181** | #183, #221, #325, #138, #44, #438 | Strings and node allocations minted per widget per frame in code that already has the data: a hashed rendering of a source location, three config copies, a `std::set` rebuilt every frame, `const std::string&` where a view would do. #438 records the visible rich-text remainder after find collection stopped scaling with the thread. |
| **OS integration** | **#33a** | #1, #5, #16, #28a, #31b, #32a, #34a, #35a, #36, #60, #465–#474, #571, #572 | afterhours is a game framework; hanabi is a native desktop app, so appearance, menus, notifications, hotkeys, deep links, bundling, resource paths, installed-font discovery and collection-face selection are app-side `.mm`. |
| **GPU accounting** | **#210** | #126, #125, #212, #145, #374 | Fixed pools nobody can size or query, no byte accounting, deferred frees, no frame scope. Every one of them fails quietly. |
| **Glyph atlas** | **#350** | #353 | Upstream `bdea3b9` reports that the atlas filled, and `AFTERHOURS_FONT_ATLAS_SIZE` sets the ceiling. What survives: no per-measurement completeness (#350), so `measure_text` still returns a plausible wrong number, and no substitute glyph (#353), so the character is simply absent from the screen. |
| **e2e runner determinism** | **#223** | #39, #40, #259, #380, #381, #457 | The SCRIPT deadline is still seconds fed by the host's `dt` (#223), the verdict is not observed on the last command, custom commands lose quoted arguments (#457), a handler cannot own its own timeout message (#380), and the directory mode runs a whole suite in one process with no reset between scripts (#381). The truncated-evidence and unregistered-`dump_ui` members landed upstream in `2caf525`; `wait_frames`-in-seconds landed in `f607faa`. |
| **Pointer buttons and scroll axes** | **#405** | #30b, #406, #407, #408, #445, #446 | The facade has a two-axis wheel but a horizontal-only view accepts X only; wheel lifetime, scroll assertions, nested hit-testing, and middle-button state each diverge between real input and the scripted model. #445 is the button form, #446 the axis-policy form. |
| **Drag interaction** | **#287** | AN-12, #447, #448, #449 | A raw drag primitive exists, but there is no candidate threshold or nested-control exclusion, scrolled/clipped hit tests use raw rects, and the overlay can copy only a flat label/color. Hanabi carries the complete composite-tab gesture outside the primitive. |

**Exact duplicates**, as opposed to families — the same finding written twice:

* **#223** — the e2e timeout family. The `wait_frames`-in-seconds half was
  filed twice, by two agents four hours apart, and BOTH are closed: upstream
  `f607faa` made `wait_frames` count ticks, and the second filing was removed
  with the other resolved entries. #223 survives rewritten to the only part
  still live — the SCRIPT deadline, `set_timeout(seconds)` fed by the host's
  `dt`.
* **#85 ≡ #91** — padding ignored on a label-only element. Keep **#85** (it
  carries the proof); #91 is the better-written statement of it and says so.
* **#72 ≡ #83** — focus ring at rest. Keep **#83**; #72 was filed one theme
  earlier and #83 supersedes it with the `FocusSource` analysis.
* **#92 ≡ #106** — no antialiasing. #106 is #92 plus the failed escape hatch.
* **#44 ≡ #181** — `ComponentConfig` copies. #181 has the measurement.
* **#1 ≡ #16** — OS appearance query, filed twice on the same day.
* **#33a ⊇ #5** — the menu-bar extra is one bullet of the platform-shim ask.

---

## 4. Negative results — keep them, and do NOT promote them

These are entries that say **"this is NOT a gap"**. They are among the most
valuable things in the file, because each one records a wrong conclusion that
was about to cost real work, and each one stays wrong the same way for the next
reader. They must never be quietly folded into the ask list.

| # | What it establishes |
|---|---|
| **#240** | **A capability that exists but is undiscoverable.** `with_styled_label(std::vector<TextSpan>)` makes a two-colour row ONE widget with normal layout, measurement and overflow. The obvious reading of the API — `with_label` takes one string, `with_custom_text_color` takes one colour — points the other way, and acting on it means a Row of two children, one of which needs a hand-computed width because nothing hugs (#136). The gap here is documentation, not capability. |
| **#241** | **A collision the library makes unrepresentable.** `imm::mk` hashes the SOURCE LOCATION, so two row kinds built at two call sites cannot collide however the app numbers them — and hanabi's hand-allocated id bases in the transcript protect against nothing. The entry is careful to say what it does *not* retire: **#171 stands untouched**, because identity keyed on the SLOT is a different problem from identity keyed on the call site. |
| **#89** | Right-aligning a child needs no spacer sibling. `JustifyContent::FlexEnd` does it, with no phantom child. Written down because the reference client uses a real `Spacer` view and copying that shape would have added an entity per row. |
| **#96** | A translucent **shape** blends correctly inside `on_draw_fg`; only the **texture** path needs its own pipeline. This one is load-bearing: it bounds family #92 above, and the evidence in front of you points the other way, so acting on the wrong reading costs every call site a manual pipeline dance. |
| **#338** | **Two subtrees built from the same call sites get DISJOINT widget identities**, and the text measure cache is width-independent — so a split pane needed neither an id-namespacing scheme nor a per-pane cache. The natural fear about splitting a view is the one thing the library already handles. |
| **#307** | **A stale `LineIndex` in a text area is unobservable.** `HasTextAreaState` maintains a source-line index at six sites and rebuilds it only when told, so an outside write to `storage` leaves it describing the previous string — an obvious latent bug with a one-line fix. Nothing reads it: `text_area` navigates by VISUAL rows off `layout_cache` (`text_area.h:597-600`) and the `line_index` consumers in `utils.h` have no call site in `text_area.h` at all. The one-line fix was written, and a scripted test for it passed WITHOUT the fix, twice. |
| **#339** | **`imm::divider` and `hsplit` already exist**, and the hand-rolled version had exactly the bug the library's own doc comment warns about. The cost of not looking was a defect the library had already written down. |
| **#475** | Arbitrary source-rectangle sprites already support an app-owned Lucide archive icon; a library icon catalogue would be the wrong layer. |
| **#477** | The generic click system correctly focuses the clicked widget; choosing an application pane is host policy. |
| **#478** | Smart-view row consistency is one application renderer and one mode choice, not a missing primitive. |
| **#480** | A custom foreground glyph and adjacent label can already carry independent colors. |
| **#482** | `JustifyContent::FlexEnd` already anchors an empty-state column without absolute positioning. |
| **#459** | **Conditional immediate-mode construction already makes a hidden hover subtree free.** Returning before `imm::div` creates the overlay yields zero hidden action entities; no library feature is missing. |
| **#4** | The status-glyph primitives are real and reachable — `draw_triangle`, etc. — so a shape-per-status glyph needed no gap at all. |
| **#8** | Windowed launch cost is dominated by OS/graphics init, not by anything hanabi or afterhours does. **Log-only, deliberately.** Do not turn this into a performance ask. |
| **#7** | RAM knobs: a *watch* item, recorded so that IF a ceiling is hit the exact knob is already written down. Not a request. |

Two more that read as gaps and are not, and belong on the same shelf:

* **#52** — selection across elements needs a document order, and the author
  chose **not** to build it and argues an app should not. That is a design
  position, not a backlog item.
* **#38**'s credit paragraph — `mouse_was_in_subtree()` is called out by name as
  the right primitive, correctly built. The residual ask is one line
  (`with_skip_hover_override()`), and the entry should not be read as
  criticism of the fix.

And the entries that are **hanabi/platform-owned**, not afterhours': **#19**, **#20** (icon
atlas resources), **#21** (the app's own screenshot harness), **#27b**
(`spawn_status` overflows `spawn_card` — the app's width math), **#108** and
**#114** (`gen_icons.py`), the bundle/LaunchServices/UserNotifications/
CoreSpotlight findings **#465–#474**, **#479** (the four-state sidebar vocabulary),
and **#484** (proof that changing that callback moved no entity or allocation
counts). They sit in the same numbered series as library gaps and read as asks;
they are not.

---

## 5. Colliding numbers — do not renumber, but know which is which

**Nine numbers are used twice in the file and one is used three times.** This
predates the current work and is not fixable by renumbering, because source code
and commit messages cite these numbers. The map below is the fix: it says which
entry each ambiguous citation means.

Line numbers are deliberately **not** given: several agents append to this file
concurrently and every number here went stale twice while the index was being
written. The entry's title is the durable handle. To list all nineteen colliding
headings with their current lines:

```sh
grep -nE '^#{2,4} #(2[789]|3[0-5])\b' afterhours_gaps.md
```

| # | Entry A (the first one in the file) | Entry B | Entry C |
|---|---|---|---|
| **27** | immediate-mode rebuild / idle-frame floor | `spawn_status` overflows `spawn_card` (app-side) | |
| **28** | no OS window-focus / frontmost query | **2nd child of a custom-bg div** (RESOLVED 2026-08-03) | |
| **29** | single `hot_id` steals the parent's hover (FIXED) | **`text_input` has no placeholder** (RESOLVED) | |
| **30** | no scroll-anchor on prepend | raw wheel-delta, no smoothing | |
| **31** | virtualization window from a stale offset | no macOS `.app` bundle packaging | **sokol pushes U+007F into the CHAR queue** (FIXED) |
| **32** | `get_resource_path` resolves from CWD | caret draws inside the last glyph (FIXED) | |
| **33** | no menu bar / notifications / hotkey / Spotlight | no Shift+Enter newline | |
| **34** | no URL-scheme handling | `text_input` does not wrap or clip | |
| **35** | no system-font enumeration | no Escape-to-clear | |

**Which one does a live citation mean?** Every citation in the working tree
resolves, and they resolve to the *second* entry in three of the four cases —
which is the opposite of what a reader assumes:

Find them all with `grep -rn 'gaps\? #[0-9]' src/ tests/ scripts/`.

| Citation | Means |
|---|---|
| `src/ecs/main_pane_system.h` ×4 — "gap #28 now fixed" | **#28 = entry B** (nested custom-bg child + `on_draw_fg`) |
| `tests/ui/sidebar_collapses_to_a_rail.e2e` — "no native placeholder, gap #29" | **#29 = entry B** (`text_input` placeholder) |
| `tests/unit/test_textinput.cpp` — "FIXED UPSTREAM (afterhours gap #31)" | **#31 = entry C** (control codes in the CHAR queue) |
| (removed at pin 9ff9079 with the upstream 5161dbf probe it described) | — | — |

No source file cites #27, #30, #32, #33, #34 or #35 bare, so those collisions
are dormant. Commit messages citing #27 (`61c1700c6551`, `e391f61aa35d`, and 16
earlier) mean **entry A**, the perf entry, from context.

**Separately, eight commits cite gap numbers that renumbering has since broken**
— a branch picked a provisional number and the merge renumbered the entry. The
full table is in `docs/COMMIT_AUDIT.md` L1; the mapping is
#98→#101, #99→#102, #99→#97, #120→#105, #130/#131/#132→#110/#111/#112,
#140→#109. Note the **#99 collision**: two agents assigned #99 to two entirely
different gaps, so an older checkout resolves the same citation two ways. Those
commits are immutable; the mapping above is the only fix.

---

## 6. Full triage table

Every entry, by number. **CLASS** is the file's own where it carries one.

**IMPACT** — how much pain this causes a consumer:
`CRIT` silently produces a wrong result · `HIGH` blocks or badly distorts real
work · `MED` costs a workaround that stays · `LOW` an annoyance · `—` not an ask.

**SIZE** — how big the upstream change looks:
`XS` under ~10 lines · `S` one function or one signature · `M` a new component,
field or pass · `L` a subsystem · `XL` a design change.

**STATUS** — `live` · `fixed` (landed upstream) · `wrong` (see §2) ·
`app` (hanabi's, not afterhours') · `neg` (deliberate negative result) ·
`dup→#N` (subsumed).

Two things the STATUS column does *not* mean. A **`dup→#N` row is still a live
ask** — it is subsumed, not closed, and if the canonical is fixed in a way that
misses it, it comes back. And the nine corrections in §2 land here under three
different labels, because "wrong as written" and "right when written, since
fixed upstream" are different facts: `wrong` for #23, #49, #79, #117 and upstream 2ccc38e;
`fixed` for upstream a1b9a4b, upstream a1b9a4b and upstream a1b9a4b; and #326 and #265 stay `live` / `dup` because the
correction narrows them rather than closing them.

| # | One line | CLASS | IMPACT | SIZE | STATUS |
|---|---|---|---|---|---|
| 1 | No OS appearance (light/dark) query | — | MED | S | dup→#16 |
| 2 | No property tween / animation helper | — | LOW | M | live |
| 3 | Absolute `button()` click vs manual hit-test | — | LOW | S | live |
| 4 | Status-glyph shapes are reachable | — | — | — | neg |
| 5 | macOS menu-bar extra (NSStatusItem) | — | MED | L | dup→#33a |
| 6 | Headless capture cannot supersample | — | MED | M | dup→#101 |
| 7 | RAM knobs — watch item only | — | — | — | neg |
| 8 | Launch cost is OS/graphics-init dominated | — | — | — | neg |
| AN-8 | No per-item stagger / delay | — | LOW | S | live |
| AN-9 | No exit / leaving animation | MISSING | MED | L | live — needs a retired widget to survive a frame, which `2393fe3`'s sweep does not provide |
| AN-10 | No one-shot state-change trigger | — | LOW | S | live |
| AN-11 | No shimmer / gradient-mask primitive | — | LOW | M | live |
| AN-12 | No drag gesture + spring-to-slot | — | LOW | L | dup→#287 |
| 13 | `draw_texture_pro` has no alpha blending | — | HIGH | S | live |
| 14 | `load_texture` sampler has no mipmaps | — | MED | S | live |
| 15 | Low-alpha `with_custom_background` renders opaque | — | HIGH | S | live |
| 16 | No OS appearance query | — | MED | S | live |
| 18 | No flex-grow: cannot pin a trailing element right | — | HIGH | M | live |
| 19 | Icon atlas has no waiting/attention glyph | — | — | — | app |
| 20 | Icon atlas has no automated/scheduled glyph | — | — | — | app |
| 21 | `--screenshot` waits on list, not transcript | — | — | — | app |
| 23 | No off-screen culling / list virtualization | — | HIGH | M | wrong |
| 25 | Degenerate triangle on mixed round/sharp corners | — | MED | XS | live |
| 27a | Immediate mode rebuilds the tree every admitted frame | — | MED | XL | app fixed→#540; upstream live |
| 27b | `spawn_status` overflows `spawn_card` | — | — | — | app |
| 28a | No OS window-focus / frontmost query | — | MED | M | live |
| 30a | No scroll-anchor / preserve-position-on-prepend | — | HIGH | M | live |
| 30b | Scroll is a raw wheel-delta add, no smoothing | — | MED | S | live |
| 31a | Virtualization window built from a STALE offset | — | MED | S | dup→#326 |
| 31b | No macOS `.app` bundle packaging | — | LOW | M | dup→#33a |
| 32a | `get_resource_path` resolves from CWD, not the exe | — | HIGH | XS | live |
| 33a | No menu bar, notifications, hotkey, Spotlight | — | MED | L | live |
| 33b | No Shift+Enter newline in `text_input` | — | MED | M | dup→#67 |
| 34a | No URL-scheme / deep-link handling | — | LOW | M | dup→#33a |
| 34b | `text_input` does not wrap or clip long text | — | MED | M | dup→#67 |
| 35a | No "list installed system fonts" primitive | WORKAROUND | LOW | S | dup→#571 |
| 35b | No Escape-to-clear on `text_input` | — | LOW | XS | dup→#57 |
| 36 | No app cache dir distinct from config dir | — | LOW | XS | live |
| 37 | No text selection on read-only text | — | HIGH | L | live |
| 38 | A container cannot report hover unless clickable | — | MED | XS | live |
| 39 | The e2e runner never fails a single-script run | — | HIGH | S | live |
| 40 | The last command's result is never observed | — | HIGH | XS | live |
| 41 | No worked example of an e2e host loop | — | MED | S | live |
| 44 | The imm builder copies its config a lot | — | MED | S | dup→#181 |
| 45 | Widget callbacks outlive their frame; no imm `on_submit` | — | MED | S | live |
| 46 | The focus ring fans out at the corners | — | MED | S | dup→#83; `ffd62d8` squares the ring on a square element |
| 48 | A missing codepoint draws nothing, with no query | — | HIGH | S | live |
| 49 | A script cannot press Cmd | — | — | — | wrong |
| 50 | Graphics-layer key reads bypass the injector | — | MED | S | live |
| 51 | No way to ask where a piece of text landed | — | HIGH | M | live |
| 52 | Selection across elements needs a document order | — | — | — | neg |
| 53 | A wrong layout is corrected silently, warned forever | — | MED | S | live |
| 54 | `check_single_action_impl` ignores the injected reader | — | MED | XS | live |
| 56 | A new `text_input` cannot be focused programmatically | — | MED | S | live |
| 57 | `text_input` blurs itself on Escape | — | MED | S | live |
| 58 | No colour input of any kind | — | LOW | L | live |
| 59 | `assert_ui` cannot assert a value containing a space | — | MED | XS | live |
| 60 | sokol's drag-and-drop cannot be turned on | — | MED | XS | live |
| 61 | A script can assert a rect and a string, never a colour | — | HIGH | S | live |
| 62 | Styled spans lose a monospace block's columns | — | MED | S | live |
| 63 | A container cannot draw over its own children | — | MED | S | live |
| 64 | No window-level chrome / render layers | — | MED | M | live |
| 65 | `text_input` padding derives from field HEIGHT | — | MED | S | live |
| 66 | A placeholder is a string, so an undrawable hint is blank | — | LOW | S | dup→#48 |
| 67 | Multi-line is a different widget, not a mode | — | HIGH | M | live |
| 68 | Nothing reports the height an element came out at | — | HIGH | M | live |
| 69 | A wrapped label cannot size itself to its text | — | MED | M | dup→#136 (partial) |
| 70 | An entity created this frame is not findable by id | — | MED | S | live |
| 71 | Grid snapping quantizes child POSITIONS | FOOTGUN | HIGH | XS | live |
| 72 | A focus ring is painted at rest | — | — | — | fixed at 1ac6db2 (`7736594`); entry kept in place |
| 73 | `assert_ui_text` matches ANY element with that label | — | HIGH | S | live |
| 74 | The resolved layout tree cannot be walked | — | HIGH | M | live |
| 75 | Text is inset by a hardcoded 5px margin that no caller can turn off | WORKAROUND | HIGH | S | partial→#590 |
| 76 | An unpadded element silently gets a fraction of the SCREEN | FOOTGUN | HIGH | XS | live |
| 77 | No bold face bundled; installed faces now unblock Hanabi | WORKAROUND | MED | S | app fixed |
| 78 | `draw_circle_v` truncates its centre to whole pixels | — | MED | XS | live |
| 79 | A label cannot be told to fit a width | — | — | — | wrong |
| 80 | Every box rasterizes 1px bigger and 1px up-left | WORKAROUND | HIGH | S | live |
| 81 | Per-corner rounding bits are named for the OPPOSITE corner | FOOTGUN | HIGH | XS | live |
| 82 | Renderer measurement is weight-aware; global app measure is not | FOOTGUN | HIGH | XS | wrong→#574 |
| 83 | No `:focus-visible`; the library's rule since 1ac6db2 is "after any interaction" | WORKAROUND | MED | S | **partial — top 10**; #72 half closed |
| 84 | Right-aligned text can never sit flush to its box | — | MED | XS | partial→#590 |
| 85 | Padding on a label-only element is silently ignored | — | — | — | fixed at 1ac6db2 (`cc26cbc` warns once); the inset asks live in #75/#91 |
| 86 | A capture emits pixels and no geometry | TEDIOUS | HIGH | S | live |
| 87 | `Dim::Text` measures unwrapped; `max_width` clamps nothing | WORKAROUND | — | — | fixed at 1ac6db2 (`c1c1eac`) with #136's `with_fit_content` |
| 88 | A row cannot baseline-align its children | FOOTGUN | MED | M | live |
| 89 | Right-aligning needs no spacer | TEDIOUS | — | — | neg |
| 90 | `ctx.theme` is one global read at RENDER time | FOOTGUN | HIGH | M | live |
| 91 | A label is not a layout participant | — | HIGH | S | live (canonical now; #85 closed) |
| 92 | Primitives are not antialiased (MSAA hardcoded off) | WORKAROUND | HIGH | S | live |
| 93 | An absolute child can only be placed from the LEADING edge | WORKAROUND | MED | S | live |
| 94 | The scrollbar is a bare on/off bool; no overlay mode | WORKAROUND | MED | S | live |
| 95 | `clipboard.h` declares none of the symbols it calls | WORKAROUND | MED | XS | live |
| 96 | A translucent shape blends correctly in `on_draw_fg` | NOT A GAP | — | — | neg |
| 97 | An absolute child cannot be `percent()`-sized | WORKAROUND | MED | XS | live |
| 100 | The private 5px margin is in DEVICE pixels | WORKAROUND | MED | XS | dup→#91 |
| 101 | No supersampled capture; `ui_scale` is a layout zoom | IMPOSSIBLE | MED | M | live |
| 102 | `on_draw_fg` gets a SCALED rect and no scale | WORKAROUND | HIGH | XS | live |
| 103 | `measure_text` returns the ink BOX, not the advance | — | — | — | fixed at 1ac6db2 (`82145f9`); entry kept in place |
| 104 | A script cannot assert an element is ABSENT | TEDIOUS | HIGH | S | live |
| 105 | A field's placeholder colour is a frame-wide global | TEDIOUS | MED | XS | live |
| 106 | No AA, and the one escape needs a flat, known background | WORKAROUND | HIGH | S | dup→#92 |
| 107 | A selected row's fill IS the row's own background box | MISSING | MED | S | live |
| 108 | Icon stroke weight is baked into the atlas | MISSING | — | — | app |
| 109 | #85 again, live 2,200 lines down, cost a whole region | FOOTGUN | — | — | fixed with #85 at 1ac6db2 (the warn-once) |
| 110 | Nothing rounds a widget's ORIGIN | SURPRISING | HIGH | S | live |
| 111 | A hover highlight IS the hit rectangle | MISSING | MED | XS | live |
| 112 | No accessible name (tooltip landed at 1ac6db2) | MISSING | MED | M | partial |
| 114 | A sprite's rendered INK extent is not derivable | TEDIOUS | — | — | app |
| 116 | No way to ask how much of a string fits in a width | WORKAROUND | HIGH | S | **live — top 10** |
| 117 | A script pins coordinates and goes stale silently | TEDIOUS | MED | S | wrong |
| 125 | `load_texture` has no max dimension | WORKAROUND | MED | XS | live |
| 126 | Nothing says how many GPU bytes are held | IMPOSSIBLE | MED | XS | live |
| 135 | `wrap_text` is O(words) measures and O(words) strings | PERFORMANCE | HIGH | S | **live — top 10** |
| 136 | Nothing sizes a box to its own text | PERFORMANCE | MED | M | **partial** — `with_fit_content` at 1ac6db2; bubble memo not migrated |
| 137 | The cached measure and the app's measure disagree | — | — | — | fixed at 1ac6db2 (`82145f9` measures text by advance); entry kept in place |
| 138 | ~4.6 heap allocations per widget per frame | PERFORMANCE | HIGH | M | dup→#181 |
| 145 | No frame SCOPE, so Metal autoreleases have no drain | FOOTGUN | HIGH | XS | live |
| 146 | Nothing reports the size of the tree just built | WORKAROUND | MED | XS | live |
| 147 | A scroll view is addressable only by DEBUG NAME | — | MED | S | live |
| 155 | The first draws cost 5-8x and there is no pre-warm | PERFORMANCE | MED | S | live |
| 160 | A component is two cache misses to write four bytes | TEDIOUS | MED | S | live — the *cost* of the sweep that landed in `2393fe3` |
| 162 | An app cannot see the widgets the LIBRARY built | TEDIOUS | MED | XS | live — dup→#171 |
| 163 | A scroll view clamps against children that are not there | WORKAROUND | HIGH | XS | live |
| 170 | `Overflow::Scroll` clips; there is no way to build less | MISSING | HIGH | M | **live — top 10** |
| 171 | Identity is the SLOT, so state re-points at another row | MISSING | HIGH | M | **live — family canonical** |
| 172 | Input injection needs the e2e plugin compiled in | MISSING | MED | S | live |
| 181 | A `ComponentConfig` is copied three times on the way in | PERFORMANCE | HIGH | S | live |
| 183 | The focusable set is a `std::set` rebuilt every frame | PERFORMANCE | MED | XS | live |
| 190 | `TextMeasureCache` is keyed by a font's NAME | FOOTGUN | HIGH | XS | dup→#579 |
| 191 | `wrap_text` gives the LINES or nothing | PERFORMANCE | HIGH | S | dup→#136 |
| 210 | Fixed GPU pools; the sampler pool exhausts at 64, silently | — | — | — | fixed at 1ac6db2 (`865c4e6`); entry kept, source cites it |
| 350 | Nothing can be asked of the atlas, not even "was that measure complete" | MISSING | CRIT | XS | **live — top 10** |
| 353 | A dropped glyph is not drawn either, and neither failure is reported | FOOTGUN | HIGH | XS | **live — top 10** |
| 365 | Find-in-conversation normalized every loaded message every frame | PERFORMANCE | HIGH | M | app (fixed) |
| 212 | Destroying a GPU object does not free it until next frame | SURPRISING | MED | XS | live |
| 221 | `with_label` takes `const std::string&` | TEDIOUS | MED | XS | dup→#181 |
| 222 | An absolute child is still counted in its parent's flow | SHARP EDGE | MED | XS | live |
| 223 | The script deadline is seconds fed by the host's `dt` (fixed timestep landed at 1ac6db2) | SHARP EDGE | MED | XS | live · #591 is the wall-clock half |
| 224 | Nothing says how tall a child WOULD be | — | HIGH | M | **live — top 10** |
| 230 | `mouse.pos` is NaN until the first mouse event | FOOTGUN | MED | XS | live |
| 232 | A coordinate test cannot state its own precondition | TEDIOUS | MED | S | live |
| 240 | Coloured runs are first-class | NOT A GAP | — | — | neg |
| 241 | `imm::mk` hashes the SOURCE LOCATION | NOT A GAP | — | — | neg |
| 255 | A feature is opted into by ENUMERATOR NAME, silently | FOOTGUN | — | — | fixed at 1ac6db2 (`7208d0c`); hanabi static_asserts its names |
| 256 | Correction to #49: `CMD+` means Ctrl, `SUPER+` is dropped | FOOTGUN | MED | XS | live |
| 257 | No action for delete-to-line-start | MISSING | MED | S | live |
| 258 | `expect_input_text` cannot see a multiline field | WORKAROUND | HIGH | XS | live |
| 259 | The script parser is line-based; no `\n` escape | TEDIOUS | MED | XS | live |
| 260 | `text_area`'s word motion does not collapse a selection | SHARP EDGE | MED | XS | dup→#67 |
| 261 | `text_area` has no placeholder | MISSING | MED | XS | dup→#67 |
| 262 | `text_area` hardcodes its field background | MISSING | MED | XS | dup→#67 |
| 263 | `text_area` draws no focus ring | MISSING | MED | XS | dup→#67 |
| 264 | `default_keymap()` is not macOS-correct | FOOTGUN | HIGH | S | live |
| 265 | The ring is three outlines, not one | — | HIGH | XS | **proof patch — contrast toggle** |
| 266 | The ring's offset is one number for the whole app | — | MED | S | dup→#83 |
| 267 | The ring is drawn with no reference to whether focus moves | — | MED | XS | dup→#83 |
| 275 | Nothing asks whether a widget is inside its PARENT | — | — | — | fixed at 1ac6db2 (`0c67090`); entry kept in place |
| 276 | `Dim::Percent` ignores the child's own margin | FOOTGUN | HIGH | XS | live |
| 277 | The 5px label inset is hard-coded and unqueryable | — | — | — | fixed at 1ac6db2 (`cc26cbc`); entry kept in place |
| 285 | Every element-addressed input command is a CLICK | TEDIOUS | MED | S | live |
| 286 | A widget cannot know its own position on the frame built | — | MED | M | live |
| 287 | There IS a drag primitive, unreachable from the config | — | HIGH | XS | live |
| 325 | `with_debug_name` takes a `std::string` | PERF | MED | XS | dup→#181 |
| 326 | `virtual_list` handles UNIFORM row heights only | MISSING | — | — | fixed at 1ac6db2 (`4a439b4`); see #420 for what is left |
| 327 | No draw-only element; a decorative mark costs an Entity | MISSING | HIGH | M | live |
| 305 | `text_area` re-wraps EVERY FRAME and bypasses `TextMeasureCache` | PERF | HIGH | XS | **live — patch proven** |
| 306 | `with_auto_grow` knows the row count and will not return it | MISSING | MED | XS | live |
| 307 | `HasTextAreaState::line_index` moves no caret; a stale one is invisible | NOT A GAP | — | — | neg |
| 308 | `assert_ui` can assert geometry and text, nothing about colour | MISSING | HIGH | S | live |
| 335 | Two view trees in one window is not a notion the library has | MISSING | HIGH | L | live |
| 336 | Tab order cannot be scoped, so Tab walks out of a split pane | MISSING | HIGH | S | live |
| 337 | With two panes a debug name stops naming ONE widget | FOOTGUN | HIGH | S | dup→#51 |
| 338 | Two subtrees from the same call sites get disjoint identities | NOT A GAP | — | — | neg |
| 339 | `imm::divider` and `hsplit` already exist | NOT A GAP | — | — | neg |
| 340 | Styled text re-wraps and re-allocates on the RENDER path, per frame | — | — | — | fixed at 1ac6db2 (`b9844c2`); entry kept in place |
| 341 | What a second pane costs (hanabi's own accounting) | PERF | — | — | app |
| 374 | `set_window_size` tears down the render target mid-pass, aborting the process | BLOCKING | HIGH | XS | **live — top 10** |
| 375 | A focused `text_input`'s border loses its top edge to the field's own clip | VISUAL | MED | S | **live — top 10** |
| 380 | A custom command cannot own its timeout message | TEDIOUS | MED | XS | dup→#223 |
| 381 | The directory mode runs a suite in one process with no reset | MISSING | HIGH | S | dup→#223 |
| 405 | Trackpad and wheel arrive as the same float; one `scroll_speed` cannot serve both conventions | MISSING | HIGH | S | **live** |
| 406 | `HandleScrollInput` skips the ancestor-scroll correction its sibling `HandleScrollbarDrag` applies | TEDIOUS | LOW | XS | neg (latent) |
| 407 | An injected wheel event is delivered on TWO frames, so a script cannot spell one notch | MISSING | MED | XS | **live** |
| 408 | `assert_ui` cannot see a scroll offset, though `dump_ui_node` prints one | TEDIOUS | MED | XS | **live** |
| 409 | An OS preference read inside the per-frame widget build, 333 ns a panel a frame | PERF | LOW | S | app (fixed) |
| 410 | The only handle on a widget from outside is a linear walk of every entity | MISSING | LOW | S | **live** |
| 420 | `virtual_list` has no RETAINED variable-height index (`height_of` landed at 1ac6db2) | MISSING / PERF | MED | M | partial · extends #326 |
| 455 | Variable-height transcript virtualization still scans every item | PERFORMANCE | MED | M | dup→#420/#224; measured |
| 456 | E2E has no clipboard assertion despite exposing clipboard reads | MISSING | MED | S | **live** |
| 457 | Custom E2E commands lose quoted arguments | FOOTGUN | HIGH | S | **live** |
| 458 | Icon controls have no semantic accessible name or role | MISSING | HIGH | M | dup→#112 |
| 459 | Conditional construction gives zero hidden hover entities | NOT A GAP | — | — | neg |
| 435 | Plain wrapped labels rebuild their line vectors on every draw | — | — | — | fixed at 1ac6db2 (`b9844c2`); entry kept in place |
| 436 | Styled labels independently rebuild nested wrapped runs on every draw | — | — | — | fixed at 1ac6db2 (`b9844c2`); entry kept in place |
| 437 | The renderer exposes no byte-to-rectangle layout map for find bands | MISSING | HIGH | M | dup→#51 |
| 438 | Visible rich text still reparses markdown and copies configs every frame | PERFORMANCE | — | — | app |
| 465 | Bundle identity comes from Info.plist, not the executable path | FIXED | HIGH | S | app (fixed) |
| 466 | TLS bundle retained absolute Homebrew dylib paths; self-containment costs 5,628 KB | FIXED | HIGH | S | app (fixed) |
| 467 | The linker's ad-hoc signature did not seal the assembled app/resources | FIXED | HIGH | XS | app (fixed) |
| 468 | A declared URL scheme is inert until explicit LaunchServices registration | FIXED | HIGH | S | app (fixed) |
| 469 | Notification authorization is user-controlled; Aspen reports denied | PLATFORM-GATED | — | — | platform |
| 470 | Async first-run authorization can race and drop the first event | FIXED | MED | S | app (fixed) |
| 471 | Foreground presentation and request sound are separate switches | FIXED | MED | XS | app (fixed) |
| 472 | Default CoreSpotlight cannot persist batch state or enumerate prior ids | FIXED | HIGH | M | app (fixed) |
| 473 | CoreSpotlight accepted the item; three mdquery predicates still returned 0 | PLATFORM-GATED | LOW | — | platform |
| 474 | A bundled headless executable still has the real bundle id | FIXED | CRIT | XS | app (fixed) |
| 475 | App-owned atlas already supports the real archive icon | NOT A GAP | — | — | neg |
| 476 | No atomic scroll-to-end operation for the smoothing state | MISSING | MED | XS | live |
| 477 | Pane focus after a child click is application policy | NOT A GAP | — | — | neg |
| 478 | Smart-view row consistency is one app renderer | NOT A GAP | — | — | neg |
| 479 | Four-state sidebar glyph vocabulary is Hanabi policy | PERF PROOF | — | — | app |
| 480 | Foreground glyph and label can have independent colors | NOT A GAP | — | — | neg |
| 481 | Status-pill alpha needs app pre-compositing | WORKAROUND | HIGH | S | dup→#15 |
| 482 | FlexEnd already anchors an empty-state column | NOT A GAP | — | — | neg |
| 483 | Color assertions still require screenshot processes | MISSING | HIGH | S | dup→#308 |
| 484 | One draw-callback branch adds zero entities/allocations | PERF PROOF | — | — | app |
| 525 | Closed popovers still resolve and retain a UI entity | PERFORMANCE | MED | XS | live |
| 526 | Synthetic right-click misses direct button polling | MISSING | MED | XS | live |
| 527 | Secondary controls lack native accessibility semantics | MISSING | HIGH | M | dup→#112/#458 |
| 540 | Host can retain the last Metal frame above the framework | NOT A GAP | — | — | app fix / #27 correction |
| 541 | Metal ignores `RunConfig::target_fps` | MISSING / PERF | HIGH | S | live |
| 542 | No event-triggered request-frame primitive | MISSING / PERF | CRIT | M | live |
| 543 | Clear, emit, layout, input, and draw are indivisible | MISSING | HIGH | L | live |
| 544 | No public non-consuming input-activity snapshot | MISSING | HIGH | S | live |
| 545 | Window exposure/backing changes are hidden from the host | MISSING | HIGH | S | live |
| 546 | Futures and SSE have no frame-wake contract | MISSING / PERF | CRIT | M | live |
| 547 | Timers cannot publish their next visual deadline | MISSING / PERF | HIGH | M | live |
| 548 | `dt` is callback time, not admitted-frame time | SHARP EDGE | HIGH | S | live |
| 549 | No headless cadence harness for the production host loop | TESTING / MISSING | HIGH | M | live |
| 560 | Native application menus are outside afterhours' host contract | NOT A GAP | — | — | host |
| 561 | AppKit consumes menu key equivalents before afterhours sees them | PLATFORM | HIGH | S | app workaround |
| 562 | E2E parses SUPER but never holds it | MISSING | HIGH | XS | live |
| 563 | `CMD+` means Ctrl rather than Command in scripts | FOOTGUN | HIGH | XS | dup→#256 |
| 564 | Synthetic input is intentionally absent from shipping builds | NOT A GAP | — | — | security boundary |
| 565 | Text editing has no imperative native-responder command surface | MISSING | HIGH | M | live |
| 566 | Native Edit capabilities depend on magic enum names | FOOTGUN | HIGH | S | dup→#255 |
| 567 | Headless UI assertions cannot observe AppKit menus | PLATFORM | MED | — | dup→#308 |
| 568 | Modifier release state has no Super slot | MISSING | HIGH | XS | live |
| 569 | Non-layered input mapping has no remapping method | TEDIOUS | MED | XS | app workaround |
| 570 | Fontstash size is not native point size | WORKAROUND | HIGH | S | app fixed |
| 571 | No installed-font catalog | MISSING | MED | S | app workaround |
| 572 | Path-only font loading cannot select collection faces | FOOTGUN | HIGH | S | app workaround |
| 573 | Font load failure is stored as an invalid handle | — | — | — | fixed at 1ac6db2 (`1586a17`); entry kept in place |
| 574 | `measure_text_internal` ignores `FontManager::active_font` | FOOTGUN | CRIT | S | app fixed |
| 575 | Advance and ink bounds are different APIs | — | — | — | fixed at 1ac6db2 (`30c6ad6` + `82145f9`); entry kept in place |
| 576 | Weighted renderer measurement already works | NOT A GAP | — | — | neg; corrects #82 |
| 577 | Loaded font IDs cannot be unloaded and cap at sixteen | FOOTGUN | HIGH | S | app workaround |
| 578 | Headless 2x zoom is not Retina rasterization | IMPOSSIBLE | HIGH | M | dup→#101 |
| 579 | Font replacement has no cache generation | FOOTGUN | CRIT | XS | dup→#190 |
| 580 | No cancellable background-job primitive | MISSING | HIGH | M | live |
| 581 | No deactivate hook for conditional systems | MISSING | HIGH | S | live |
| 582 | Transcript payload ownership belongs above the ECS | NOT A GAP | — | — | neg |
| 583 | Entity pool exposes its retained high-water count | NOT A GAP | — | — | neg |
| 584 | Text-measure cache is bounded and observable | DUPLICATE | — | — | dup→#340 |
| 585 | No retained-byte attribution by system/component | MISSING | MED | M | live |
| 586 | No memory-pressure/cache-purge event | MISSING | MED | M | live |
| 587 | No frame-safe mailbox for background completions | MISSING | HIGH | M | live |
| 588 | Skeleton and stale metadata are app UI state | NOT A GAP | — | — | neg |
| 589 | No per-system CPU accounting seam | MISSING | MED | S | live |
| 590 | Button variants drop per-widget text inset | FOOTGUN | HIGH | XS | app workaround |
| 591 | The e2e runner has no wall-clock wait; a worker holding real seconds cannot be awaited | MISSING | MED | XS | live (re-tested 1ac6db2) · extends #223; app workaround: latch + `release_compaction` |
| 592 | Every click on a `HasClickListener` moves keyboard focus to it; no activate-without-focus | FOOTGUN | HIGH | XS | live (re-tested 1ac6db2) · app workaround (refocus) |
| 593 | `System<>`'s six overrides lack `override`; a consumer compiling the library as user code gets 18 warnings per TU | SHARP EDGE | LOW | XS | live · app workaround (every include via `-isystem`) |
| 550–559 | Session-lifecycle audit: no new framework gaps; existing #112/#458 and #326/#420 apply | NOT A GAP | — | — | unassigned |
---

## How to keep this useful

1. **Never renumber.** Source code and commit messages cite these numbers, and
   `docs/COMMIT_AUDIT.md` L1 records eight commits already broken by a renumber
   at merge. New entries take the next free number in the range you were given.
2. **New entries go in `afterhours_gaps.md`, and add a row here.** One line in
   §6 and, if it belongs to one, a mention in its family row in §3.
3. **Read the vendored source before you file.** Two entries went wrong because
   nobody did, and nine more here were right when written and are not any more.
   A one-line `grep` in `vendor/afterhours/src/` is the whole of the diligence
   this needs.
4. **A negative result is a result.** If you go looking for a gap and find a
   capability, file that with the same care (§4). It is the cheapest thing in
   this file to write and the most expensive thing to rediscover.
