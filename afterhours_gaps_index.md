# afterhours gaps — index

`afterhours_gaps.md` is ~12,000 lines and 212 numbered entries, written by dozens of
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

**Read section 1 and stop.** The rest is reference.

| | |
|---|---|
| Numbered headings in the file | **212** |
| Distinct gap numbers | **202** (nine numbers are used twice, one three times — §5) |
| Plus the `AN-8`…`AN-12` animation sub-series | **5** |
| **Rows in the triage table (§6)** | **218** — one per indexed heading, nothing dropped |
| Standalone live asks | **123** |
| Live but subsumed into a family canonical | **44** (§3) |
| Already fixed upstream | **9** |
| Deliberate NEGATIVE results — do not promote | **11** (§4) |
| hanabi/platform-owned, not afterhours' | **17** |
| **Entries WRONG or overtaken by events** | **9 found here, 4 already known** (§2) |

Everything in §2 was checked by reading `vendor/afterhours` at the pinned
submodule **428047e**. Nothing here was verified by running the library; each
correction says so.

---

## 1. The top ten

If ten things get fixed in afterhours, these ten, in this order.

The ordering rule is *pain per line of upstream change*, with two thumbs on the
scale: a defect that **fails silently and corrupts something downstream** beats
a defect that is merely painful, and a gap that **every app vendoring afterhours
must work around** beats one only hanabi hits.

---

### 1. #351 + #350 + #352 + #353 (origin: #211) — the glyph atlas fills silently, and the symptom is that `measure_text` starts returning wrong numbers

**#351 is about six lines and it is the single highest-value change in this
file.** `fonsSetErrorCallback` already exists, is exactly the hook a consumer
needs, and nothing in the backend registers it — so when the 2048² atlas fills,
text measurement quietly begins lying and every box sized from it is wrong. A
layout engine handed a wrong width does not crash; it draws a frame that is
subtly and unfixably wrong, with no message anywhere.

This is now the best-evidenced finding in the file, because the condition has
been **reached and watched** rather than reasoned about
(`hanabi.exe --atlas-stress`, driving printable ASCII at climbing sizes):

```
  pt          width   detector
  64         2644.0   ok
  120        4836.0   FULL   <- the atlas can take no new rect
  124         622.0   fault  <- the app's own measurement is now WRONG
  136         231.0   fault
```

622.0 where the truth is ~5,000 is the dangerous number, not the 0.0 further
down: a zero is catchable, and a plausible-but-wrong width is indistinguishable
from a correct measurement of a shorter string. Note also that a dropped glyph
is not *drawn* either (#353), so the string is invisible as well as
unmeasurable, and both failures are reported the same way — not at all.

Four asks, cheapest first, and the first one alone converts silent corruption
into a message with a name:

* **#351** — call `fonsSetErrorCallback` in the backend's init and `log_error`
  `FONS_ATLAS_FULL` once.
* **#350** — return whether the call that just measured dropped a glyph.
  `fonsTextBounds` already knows: it gets NULL back from `fons__getGlyph` per
  refused codepoint and discards that on the way out. One `bool`.
* **#352** — `graphics::Config::font_atlas_width/height`, defaulted to 2048.
  Same struct and same request as #210's pool sizes; do them together.
* **#353** — draw the substitute glyph (codepoint 0) instead of skipping the
  quad, so an invisible failure becomes the oldest visible one in typography.

hanabi has since built `src/util/atlas_guard.h` — a probe that asks for a glyph
the atlas has never held at a size it has never held, which is exact rather than
heuristic and fires one step before the app's own text is affected — and gated
it. That is a fifty-line workaround for a six-line library change, in every app
that vendors afterhours, and it can only ever detect the condition, never
prevent it.

### 2. #115 — nothing retires a widget, so every system walks the union of every screen the app has ever shown

**The largest measured cost in the file, and the mechanism under six other
entries.** `imm::mk()` keeps a permanent map and hands back the same entity
forever; nothing marks an entity as not-built-this-frame and nothing sweeps one.
Hanabi's Home pane was drawn **twice** and then charged 3.15 ms — **69% of every
frame** — to 800 frames that never showed it. This is the shape of "it gets
slower and slower until it freezes".

The fix is a frame stamp in `mk()` (which already has the entity in hand) and an
end-of-frame sweep. Hanabi has worked around it in fifty lines of app code, and
that is the argument for fixing it upstream, not against: **every app that
vendors afterhours has to write those fifty lines**, has to discover that
`existing_ui_elements` is a public global and that `mk` can be shadowed, and
still cannot retire the nine entities the library created for itself (#162).

### 3. #210 — the GPU pools are fixed, the sampler pool runs out FIRST at 64, and `load_texture` does not check it

**~3 lines for the correctness half.** `sg_setup` is called with a
default-constructed `sg_desc`, so every consumer gets sokol's defaults
(`_SG_DEFAULT_SAMPLER_POOL_SIZE = 64`, verified) with no way to raise them.
`load_texture` makes an image, a view **and a sampler** per texture; it checks
the first two and not the third. So between the 61st texture and the 124th it
returns a struct with a real width, a real height, valid ids and
`sampler_id = 0` — which every "did this load?" test a consumer can write reads
as success. **Sixty textures of silent wrongness.** Check the sampler, and put
the pool sizes on `graphics::Config`.

### 4. #137 + #136 + #340 + #116 + #135 — text: the cache answers a different question than the app can ask, nothing hugs its own text, and the draw path re-wraps every frame

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
  use it in both, document which.
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
  (#42 is the same finding for plain labels, without the current numbers.)
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
  pinned 428047e it takes that arm from 1007 to 824 allocations a frame, and a
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

### 5. #275 — nothing in the stack asks whether a widget is inside its PARENT

**A few lines, in a walk that already runs.** Three things look like they answer
"is this drawing outside its box" and none of them does: the layout warning is
main-axis-only and goes to a log nobody reads, and `assert_no_overflow` — the one
assertion — measures each element against the **viewport**, not against its
parent's content box. Confirmed in the source. On a report of *"many buttons are
going outside the bounds"* it named **1 of 55**. `assert_no_overflow` already
walks every laid-out element with its parent one hop away; adding the
parent-box comparison beside the viewport one turns the only containment
assertion in the toolkit into one that answers the question it is named for.

### 6. #326 + #170 + #224 — virtualization only handles uniform row heights, which is not what a list is

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

### 7. #85 + #277 + #75 + #100 + #84 + #91 + #109 — a label is drawn at a hardcoded 5px that no caller can set, read, or override

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

### 8. #255 — an editing feature is opted into by ENUMERATOR NAME, and opting out is silent

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

### 9. #83 + #265 + #266 + #267 + #72 + #46 — the focus ring

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

### 10. #192 + #161 + #113 — `dump_ui` is fully written, is not registered, and reports itself as a typo

**Two lines.** `HandleDumpUICommand` is ~100 lines of working code that walks the
tree and emits XML with every element's name, rect and text. Twenty-five sibling
handlers are registered in `register_ui_commands`; this one is defined and never
mentioned again (verified). So the command that would have answered
"where did this land", "what was on screen when the assertion failed" and
"which widget's width changed" is present in every build and reachable from
none, and the runner tells you it is a typo.

It earns a top-ten slot on ratio alone. Beside it, two more of the same
character: a scripted failure truncates its evidence to 200 characters, so four
different failures in one session printed *identical bytes* (#161); and the one
failure that means "it is not there" — the timeout — is the one failure that is
never told which element it was about (#113). Three fixes, all under ten lines,
that between them shorten the investigation behind half the entries in this file.

---

**Just below the line**, in case there is an eleventh through fifteenth:

* **#145** — `begin_frame`/`end_frame` are two free calls, not a frame scope, so
  the six Metal objects the library autoreleases per frame have nowhere to be
  drained. Every consumer must discover this and write its own pool. Two calls.
* **#200** — a headless resize creates five Metal render pipelines that
  destroying the render texture never releases: **4.8 MB per 1000 frames**,
  larger than the leak that started this project.
* **#76** — an unpadded element is not unpadded: it silently gets
  `Spacing::sm` = `screen_pct(0.02)`, a fraction of the *window* (verified at
  `component_init.h:105`), which changes on resize.
* **#180 / #181 / #183 / #221** — the per-frame allocation quartet. `mk()`
  builds a `stringstream` and a ~200-character string to hash **per widget per
  frame** (verified); a `ComponentConfig` is copied three times on the way in;
  the focusable set is a `std::set` rebuilt every frame. Roughly 4.6 heap
  allocations per widget per frame, all of it removable without an API change.
* **#163** — a scroll view's offset is clamped against a content size measured
  from children that are not there, so leaving a screen resets it to the top.
  Verified: `MeasureScrollViews` sums `cmp.children` and calls `clamp_scroll()`
  unconditionally, and `ClearUIComponentChildren` emptied that list this frame.
  One `if`.

---

## 2. Corrections — entries that are WRONG, or right when written and overtaken since

An entry claiming the library cannot do something it can is worse than no entry.
Four were already known. **Nine more are recorded here**, each verified against
`vendor/afterhours` at 428047e and each postscripted in place in
`afterhours_gaps.md` rather than deleted.

### Already known before this pass

| # | What was wrong |
|---|---|
| **#115** | Claimed the app was structurally stuck. `existing_ui_elements` is a **public inline global**, and `mk` can be shadowed through the single using-declaration — one word, and the workaround is fifty lines. The gap stands; the "impossible" framing did not. Retracted in place. |
| **#117** | The **commit** blamed a vendored constant. The gate the gesture goes through is hanabi's own — `src/ui/text_select.h:56`, `kMultiClickMs = 400`; `MULTI_CLICK_TIME` lives in the text-input widgets and is not on that path. Same failure as #115, one file over. The entry has since been rewritten; the commit stands as written. |
| **#27** | The "~8.6 ms idle-frame floor" was mostly hanabi's own missing `-O2`, not the per-frame rebuild. Retracted in place. The design observation stands; the number was wrong. |
| **#49** | Corrected by **#256**: `CMD+` in a script parses as Ctrl and `SUPER+` is dropped, so a Cmd chord IS scriptable if the app binds the Ctrl twin. #49's "the whole shortcut surface is unreachable" is too strong. |

### Found in this pass

| # | Verdict | What the source actually says |
|---|---|---|
| **#79** | **The ask has landed** | "A label cannot be told to fit a width" — `TextOverflow::Ellipsis` exists on `ComponentConfig` (`component_config.h:428`) and the renderer binary-searches the longest fitting prefix at `rendering.h:737-782`, which is this entry's own proposed fix, in its own proposed place. hanabi already uses it twice. Two caveats keep a much smaller ask alive: the ellipsis measures with the ink box (#103/#137) and hardcodes `rect.width - 10.f` (#75/#277). |
| **#24** | **Resolved upstream** | "Wrapped text ignores hard line breaks." It does not. `rendering.h:629` computes `has_hard_break` and the branch below reads "Only soft-wrap when asked; otherwise break on `'\n'` alone"; `ui::wrap_text`'s doc comment says "Honors hard `'\n'`". |
| **#26** | **Resolved upstream** | "`HasScrollView` has no built-in scrollbar." It has all of it: `show_scrollbar` (defaults **true**), `scrollbar_thickness`, `scrollbar_min_thumb`, `scrollbar_geometry()` which auto-hides when content fits, and `HandleScrollbarDrag` mapping a thumb drag back into `scroll_offset`. What is left is *overlay* behaviour — **#94**, written against these fields. |
| **#23** | **Overtaken** | "No list virtualization for scroll views." `imm::virtual_list` exists (`imm_components.h:159`) with spacers and a 4-row overscan. What it cannot do is non-uniform heights — the live entry is **#326**. |
| **gap #22 follow-up** (line ~1099) | **Wrong** | "Styled spans are COLOR-only (no per-run weight)." `TextSpan` is `{text, color, weight}` (`ui_core_components.h:457`), and its own comment documents the fallback when the app registered no bold face. Per-run **weight** is first-class; per-run **font/slant** is not. hanabi's bold renders as colour because hanabi bundles no bold face (**#77**), which is a resource decision, not a library one. |
| **#326** | **Wrong symbol** | The function is `imm::virtual_list`, not `imm::vlist` — nothing named `vlist` exists outside three debug-name literals. The entry as written is not greppable, and it is the entry a reader lands on when hunting the primitive. Everything else in it checks out. |
| **#265** | **Partly overtaken** | "Nothing in the theme turns the contrast edges off." `focus_ring_thickness = 0` now returns early before any outline is emitted (`rendering.h:220-223`), and the comment above it names this exact finding as the reason. That total off switch is what makes hanabi's `focus_visible.h` possible. The entry's real ask — keep the coloured ring, drop the two edges — is still impossible. |
| **#117** | **Stale evidence, third time** | The entry quotes `double_click 415 225`; the tree at `2fd9e84` reads `415 252`. Master moved it in `7f15b253444b` while the entry was written on a branch that never picked the fix up. Separately, `click_text` — half of this entry's proposed fix — is already a runner command and three hanabi scripts use it. The remaining ask narrows to `double_click_text` / `triple_click_text`. |
| **#220** | **Narrower than stated** | "A virtualizing consumer must build the WHOLE list once." Not inside `virtual_list`, which renders a fixed 61-row initial window when `viewport_size` is still zero (`imm_components.h:189-191`). True only of a consumer windowing by hand, which is what hanabi does *because of* #326. |

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

**Thirteen families cover 120 of the 217 entries.** Fix the canonical one and the
rest either close or shrink to a footnote — 41 of them are subsumed outright
(the `dup→` rows in §6) and the remainder get smaller. Where the members were
filed by different agents from different features, that is noted: it is the
strongest evidence that the underlying mechanism, not the symptom, is the thing
to fix.

| Family | Canonical | Also filed as | The one mechanism |
|---|---|---|---|
| **Widget lifetime** | **#115** | #171, #162, #163, #220, #146, #160, AN-9 | Nothing retires an entity, so identity is the slot, the library's own entities are invisible, a scroll view clamps against children that are not there, and an exit animation has nothing to animate. #160 is the *cost* of the fix; #146 is how you would gate it. |
| **Text measurement and wrap** | **#136** | #135, #116, #137, #191, #103, #82, #190, #69, #87, #79, #340, #42 | No content sizing and no prefix/count query, so every consumer re-derives metrics the layout already has — against a cache that answers a different question (#137), keyed by a font name that does not change when the face does (#190), measuring the ink box rather than the advance (#103), with no weight parameter (#82). Twelve entries; **filed independently by at least six agents**, the latest (#340) three weeks after the first. |
| **The 5px label inset** | **#85** | #75, #277, #84, #91, #100, #109 | One literal `Vector2Type{5.f, 5.f}` in `rendering.h`, unexposed and unqueryable, that also swallows the element's own padding in silence. #91 is the fuller statement, #85 carries the byte-identical-frames proof, #109 is the second time it cost a region. |
| **Focus ring** | **#83** | #46, #72, #265, #266, #267, #263 | One `focus_ring_for`, and no `:focus-visible`, no per-widget offset, no independent contrast edges, no check that focus can move. #263 (`text_area` draws no ring at all) is the same code path from the other end. |
| **Virtualization** | **#326** | #23, #170, #31a, #224, #220, #147 | `virtual_list` divides by one row height. Everything else here is a consumer working around that: windowing by hand against state the library writes after the build. |
| **Alpha and antialiasing** | **#92** | #13, #15, #106, #96 | `sample_count` is pinned at 1 and the sokol_gl default pipeline has blending off, so nothing small or translucent can be drawn correctly. #96 is the **negative** result that limits the family (see §4). |
| **Text input vs text area** | **#67** | #17, #29b, #33b, #34b, #35b, #57, #65, #105, #261, #262, #263, #260, #258 | Multi-line is a different widget, not a mode, so every property `text_input` grew has to be grown again on `text_area`: placeholder, background, focus ring, selection-collapsing word motion, and the harness assertion that can see it. Thirteen entries; most of them are four lines each. |
| **Scripted-test addressing** | **#51** | #55, #61, #73, #59, #104, #117, #232, #285, #86, #147, #337 | A script can address a named element or a raw coordinate, and nothing in between — no text run, no colour, no absence, no scope, no gesture-by-name. #337 is #147 with a second pane: a debug name stops naming ONE widget the moment the app renders the same code twice. |
| **Per-frame allocation** | **#180** | #181, #183, #221, #325, #138, #44 | Strings and node allocations minted per widget per frame in code that already has the data: a hashed rendering of a source location, three config copies, a `std::set` rebuilt every frame, `const std::string&` where a view would do. |
| **OS integration** | **#33a** | #1, #5, #16, #28a, #31b, #32a, #34a, #35a, #36, #60, #465–#474 | afterhours is a game framework; hanabi is the first native desktop app on it, so appearance, menu bar, notifications, hotkeys, deep links, bundling, resource paths, font enumeration and drag-and-drop are all app-side `.mm`. **#32a is the one that breaks a shipped app** (`get_resource_path` resolves from CWD, and a launched `.app` has CWD `/`). #465–#474 are the verified bundle/LaunchServices/UserNotifications/CoreSpotlight follow-up, including its measured costs and platform-gated proof. |
| **GPU accounting** | **#210** | #126, #125, #212, #145, #200 | Fixed pools nobody can size or query, no byte accounting, deferred frees, no frame scope. Every one of them fails quietly. |
| **Glyph atlas** | **#351** | #211, #350, #352, #353 | One fixed 2048² atlas, one unregistered fontstash callback, and a `measure_text` that returns a plausible wrong number when it fills. #211 is the origin entry and carries the measurements; #351 is the fix. #352 is the same `graphics::Config` request as #210's pool sizes. |
| **e2e runner determinism** | **#223** | #231, #39, #40, #113, #161, #192, #259, #380, #381 | The runner's budgets are seconds fed by the host's `dt`, its verdict is not observed on the last command, its evidence is truncated, its best diagnostic is unregistered, a handler cannot own its own timeout message (#380 — and #113 is that same overwrite from the other side), and the directory mode runs a whole suite in one process with no reset between scripts (#381). #223 and #231 are **the same finding filed twice**, by two agents, four hours apart. |

**Exact duplicates**, as opposed to families — the same finding written twice:

* **#223 ≡ #231** — the `wait_frames`-is-seconds finding. Keep **#223** (it has
  the reproducibility framing); #231 has the tighter one-line fix.
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
**#114** (`gen_icons.py`), plus the bundle/LaunchServices/UserNotifications/
CoreSpotlight findings **#465–#474**. They sit in the same numbered series as
library gaps and read as asks; they are not.

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
| `tests/e2e/test_perf.cpp` — "Reported for afterhours (gap #43)" | #43 — not ambiguous; the second `#43` heading is its measurement section |

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
fixed upstream" are different facts: `wrong` for #23, #49, #79, #117 and #220;
`fixed` for #22, #24 and #26; and #326 and #265 stay `live` / `dup` because the
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
| AN-9 | No exit / leaving animation | — | MED | L | dup→#115 |
| AN-10 | No one-shot state-change trigger | — | LOW | S | live |
| AN-11 | No shimmer / gradient-mask primitive | — | LOW | M | live |
| AN-12 | No drag gesture + spring-to-slot | — | LOW | L | dup→#287 |
| 13 | `draw_texture_pro` has no alpha blending | — | HIGH | S | live |
| 14 | `load_texture` sampler has no mipmaps | — | MED | S | live |
| 15 | Low-alpha `with_custom_background` renders opaque | — | HIGH | S | live |
| 16 | No OS appearance query | — | MED | S | live |
| 17 | `text_input` ignores font size + custom background | — | HIGH | S | live |
| 18 | No flex-grow: cannot pin a trailing element right | — | HIGH | M | live |
| 19 | Icon atlas has no waiting/attention glyph | — | — | — | app |
| 20 | Icon atlas has no automated/scheduled glyph | — | — | — | app |
| 21 | `--screenshot` waits on list, not transcript | — | — | — | app |
| 22 | Styled label spans do not word-wrap | — | HIGH | M | fixed |
| 23 | No off-screen culling / list virtualization | — | HIGH | M | wrong |
| 24 | Wrapped text ignores hard `\n` | — | HIGH | S | fixed |
| 25 | Degenerate triangle on mixed round/sharp corners | — | MED | XS | live |
| 26 | `HasScrollView` has no built-in scrollbar | — | MED | M | fixed |
| 27a | Immediate mode rebuilds the tree every frame | — | MED | XL | live |
| 27b | `spawn_status` overflows `spawn_card` | — | — | — | app |
| 28a | No OS window-focus / frontmost query | — | MED | M | live |
| 28b | 2nd child of a custom-bg div did not render | — | — | — | fixed |
| 29a | Single `hot_id` steals the parent's hover fill | — | — | — | fixed |
| 29b | `text_input` has no placeholder | — | — | — | fixed |
| 30a | No scroll-anchor / preserve-position-on-prepend | — | HIGH | M | live |
| 30b | Scroll is a raw wheel-delta add, no smoothing | — | MED | S | live |
| 31a | Virtualization window built from a STALE offset | — | MED | S | dup→#326 |
| 31b | No macOS `.app` bundle packaging | — | LOW | M | dup→#33a |
| 31c | sokol pushes U+007F into the CHAR queue | — | — | — | fixed |
| 32a | `get_resource_path` resolves from CWD, not the exe | — | HIGH | XS | live |
| 32b | Caret draws inside the last glyph | — | — | — | fixed |
| 33a | No menu bar, notifications, hotkey, Spotlight | — | MED | L | live |
| 33b | No Shift+Enter newline in `text_input` | — | MED | M | dup→#67 |
| 34a | No URL-scheme / deep-link handling | — | LOW | M | dup→#33a |
| 34b | `text_input` does not wrap or clip long text | — | MED | M | dup→#67 |
| 35a | No "list installed system fonts" primitive | — | LOW | S | live |
| 35b | No Escape-to-clear on `text_input` | — | LOW | XS | dup→#57 |
| 36 | No app cache dir distinct from config dir | — | LOW | XS | live |
| 37 | No text selection on read-only text | — | HIGH | L | live |
| 38 | A container cannot report hover unless clickable | — | MED | XS | live |
| 39 | The e2e runner never fails a single-script run | — | HIGH | S | live |
| 40 | The last command's result is never observed | — | HIGH | XS | live |
| 41 | No worked example of an e2e host loop | — | MED | S | live |
| 42 | The draw path re-measures every string every frame | — | HIGH | M | live |
| 43 | Component lookup goes through `dynamic_cast` | — | HIGH | M | live |
| 44 | The imm builder copies its config a lot | — | MED | S | dup→#181 |
| 45 | Widget callbacks outlive their frame; no imm `on_submit` | — | MED | S | live |
| 46 | The focus ring fans out at the corners | — | HIGH | S | dup→#83 |
| 47 | `expect_no_text` can never fail | — | — | — | fixed |
| 48 | A missing codepoint draws nothing, with no query | — | HIGH | S | live |
| 49 | A script cannot press Cmd | — | — | — | wrong |
| 50 | Graphics-layer key reads bypass the injector | — | MED | S | live |
| 51 | No way to ask where a piece of text landed | — | HIGH | M | live |
| 52 | Selection across elements needs a document order | — | — | — | neg |
| 53 | A wrong layout is corrected silently, warned forever | — | MED | S | live |
| 54 | `check_single_action_impl` ignores the injected reader | — | MED | XS | live |
| 55 | A script can right-click a coordinate, never an element | — | MED | S | live |
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
| 69 | A wrapped label cannot size itself to its text | — | HIGH | M | dup→#136 |
| 70 | An entity created this frame is not findable by id | — | MED | S | live |
| 71 | Grid snapping quantizes child POSITIONS | FOOTGUN | HIGH | XS | live |
| 72 | A focus ring is painted at rest | — | HIGH | S | dup→#83 |
| 73 | `assert_ui_text` matches ANY element with that label | — | HIGH | S | live |
| 74 | The resolved layout tree cannot be walked | — | HIGH | M | live |
| 75 | Text is inset by a hardcoded 5px no caller can turn off | WORKAROUND | HIGH | S | dup→#85 |
| 76 | An unpadded element silently gets a fraction of the SCREEN | FOOTGUN | HIGH | XS | live |
| 77 | No bold face bundled (the silent half is FIXED) | WORKAROUND | MED | S | live |
| 78 | `draw_circle_v` truncates its centre to whole pixels | — | MED | XS | live |
| 79 | A label cannot be told to fit a width | — | — | — | wrong |
| 80 | Every box rasterizes 1px bigger and 1px up-left | WORKAROUND | HIGH | S | live |
| 81 | Per-corner rounding bits are named for the OPPOSITE corner | FOOTGUN | HIGH | XS | live |
| 82 | Text cannot be measured at a weight | FOOTGUN | HIGH | XS | dup→#136 |
| 83 | The focus ring paints at rest; no `:focus-visible` | WORKAROUND | HIGH | S | **live — top 10** |
| 84 | Right-aligned text can never sit flush to its box | — | MED | XS | dup→#85 |
| 85 | Padding on a label-only element is silently ignored | — | HIGH | S | **live — top 10** |
| 86 | A capture emits pixels and no geometry | TEDIOUS | HIGH | S | live |
| 87 | `Dim::Text` measures unwrapped; `max_width` clamps nothing | WORKAROUND | HIGH | S | dup→#136 |
| 88 | A row cannot baseline-align its children | FOOTGUN | MED | M | live |
| 89 | Right-aligning needs no spacer | TEDIOUS | — | — | neg |
| 90 | `ctx.theme` is one global read at RENDER time | FOOTGUN | HIGH | M | live |
| 91 | A label is not a layout participant | — | HIGH | S | dup→#85 |
| 92 | Primitives are not antialiased (MSAA hardcoded off) | WORKAROUND | HIGH | S | live |
| 93 | An absolute child can only be placed from the LEADING edge | WORKAROUND | MED | S | live |
| 94 | The scrollbar is a bare on/off bool; no overlay mode | WORKAROUND | MED | S | live |
| 95 | `clipboard.h` declares none of the symbols it calls | WORKAROUND | MED | XS | live |
| 96 | A translucent shape blends correctly in `on_draw_fg` | NOT A GAP | — | — | neg |
| 97 | An absolute child cannot be `percent()`-sized | WORKAROUND | MED | XS | live |
| 100 | The private 5px margin is in DEVICE pixels | WORKAROUND | MED | XS | dup→#85 |
| 101 | No supersampled capture; `ui_scale` is a layout zoom | IMPOSSIBLE | MED | M | live |
| 102 | `on_draw_fg` gets a SCALED rect and no scale | WORKAROUND | HIGH | XS | live |
| 103 | `measure_text` returns the ink BOX, not the advance | WORKAROUND | HIGH | XS | dup→#136 |
| 104 | A script cannot assert an element is ABSENT | TEDIOUS | HIGH | S | live |
| 105 | A field's placeholder colour is a frame-wide global | TEDIOUS | MED | XS | live |
| 106 | No AA, and the one escape needs a flat, known background | WORKAROUND | HIGH | S | dup→#92 |
| 107 | A selected row's fill IS the row's own background box | MISSING | MED | S | live |
| 108 | Icon stroke weight is baked into the atlas | MISSING | — | — | app |
| 109 | #85 again, live 2,200 lines down, cost a whole region | FOOTGUN | HIGH | S | dup→#85 |
| 110 | Nothing rounds a widget's ORIGIN | SURPRISING | HIGH | S | live |
| 111 | A hover highlight IS the hit rectangle | MISSING | MED | XS | live |
| 112 | No tooltip and no accessible name | MISSING | HIGH | M | live |
| 113 | The timeout is the one failure not told its element | FOOTGUN | HIGH | XS | **live — top 10** |
| 114 | A sprite's rendered INK extent is not derivable | TEDIOUS | — | — | app |
| 115 | Nothing retires a widget | WORKAROUND | CRIT | M | **live — top 10** |
| 116 | No way to ask how much of a string fits in a width | WORKAROUND | HIGH | S | **live — top 10** |
| 117 | A script pins coordinates and goes stale silently | TEDIOUS | MED | S | wrong |
| 125 | `load_texture` has no max dimension | WORKAROUND | MED | XS | live |
| 126 | Nothing says how many GPU bytes are held | IMPOSSIBLE | MED | XS | live |
| 135 | `wrap_text` is O(words) measures and O(words) strings | PERFORMANCE | HIGH | S | **live — top 10** |
| 136 | Nothing sizes a box to its own text | PERFORMANCE | CRIT | M | **live — top 10** |
| 137 | The cached measure and the app's measure disagree | FOOTGUN | CRIT | XS | **live — top 10** |
| 138 | ~4.6 heap allocations per widget per frame | PERFORMANCE | HIGH | M | dup→#180 |
| 145 | No frame SCOPE, so Metal autoreleases have no drain | FOOTGUN | HIGH | XS | live |
| 146 | Nothing reports the size of the tree just built | WORKAROUND | MED | XS | live |
| 147 | A scroll view is addressable only by DEBUG NAME | — | MED | S | live |
| 155 | The first draws cost 5-8x and there is no pre-warm | PERFORMANCE | MED | S | live |
| 160 | A component is two cache misses to write four bytes | TEDIOUS | MED | S | dup→#115 |
| 161 | A failed assertion truncates its evidence to 200 chars | TEDIOUS | HIGH | XS | **live — top 10** |
| 162 | An app cannot see the widgets the LIBRARY built | TEDIOUS | MED | XS | dup→#115 |
| 163 | A scroll view clamps against children that are not there | WORKAROUND | HIGH | XS | live |
| 170 | `Overflow::Scroll` clips; there is no way to build less | MISSING | HIGH | M | **live — top 10** |
| 171 | Identity is the SLOT, so state re-points at another row | — | HIGH | M | dup→#115 |
| 172 | Input injection needs the e2e plugin compiled in | MISSING | MED | S | live |
| 180 | `mk()` builds a stringstream + 200-char string per widget | PERFORMANCE | HIGH | XS | live |
| 181 | A `ComponentConfig` is copied three times on the way in | PERFORMANCE | HIGH | S | live |
| 183 | The focusable set is a `std::set` rebuilt every frame | PERFORMANCE | MED | XS | live |
| 190 | `TextMeasureCache` is keyed by a font's NAME | FOOTGUN | HIGH | XS | dup→#136 |
| 191 | `wrap_text` gives the LINES or nothing | PERFORMANCE | HIGH | S | dup→#136 |
| 192 | `dump_ui` is written, unregistered, reported as a typo | TEDIOUS | HIGH | XS | **live — top 10** |
| 200 | A headless resize leaks five Metal pipelines a frame | BLOCKING | HIGH | S | live |
| 210 | Fixed GPU pools; the sampler pool exhausts at 64, silently | — | CRIT | XS | **live — top 10** |
| 211 | Fixed glyph atlas; overflow corrupts `measure_text` | — | CRIT | XS | **live — top 10** |
| 350 | Nothing can be asked of the atlas, not even "was that measure complete" | MISSING | CRIT | XS | **live — top 10** |
| 351 | `fonsSetErrorCallback` exists and is never registered | MISSING | CRIT | XS | **live — top 10** |
| 352 | The 2048² atlas is a build-time constant of the library | MISSING | HIGH | XS | **live — top 10** |
| 353 | A dropped glyph is not drawn either, and neither failure is reported | FOOTGUN | HIGH | XS | **live — top 10** |
| 212 | Destroying a GPU object does not free it until next frame | SURPRISING | MED | XS | live |
| 220 | A scroll view's viewport is zero on frame one | SHARP EDGE | MED | XS | wrong |
| 221 | `with_label` takes `const std::string&` | TEDIOUS | MED | XS | dup→#180 |
| 222 | An absolute child is still counted in its parent's flow | SHARP EDGE | MED | XS | live |
| 223 | The retry budget is seconds fed by the host's `dt` | SHARP EDGE | MED | XS | live |
| 224 | Nothing says how tall a child WOULD be | — | HIGH | M | **live — top 10** |
| 230 | `mouse.pos` is NaN until the first mouse event | FOOTGUN | MED | XS | live |
| 231 | `wait_frames N` is stored as SECONDS | FOOTGUN | MED | XS | dup→#223 |
| 232 | A coordinate test cannot state its own precondition | TEDIOUS | MED | S | live |
| 240 | Coloured runs are first-class | NOT A GAP | — | — | neg |
| 241 | `imm::mk` hashes the SOURCE LOCATION | NOT A GAP | — | — | neg |
| 255 | A feature is opted into by ENUMERATOR NAME, silently | FOOTGUN | CRIT | S | **live — top 10** |
| 256 | Correction to #49: `CMD+` means Ctrl, `SUPER+` is dropped | FOOTGUN | MED | XS | live |
| 257 | No action for delete-to-line-start | MISSING | MED | S | live |
| 258 | `expect_input_text` cannot see a multiline field | WORKAROUND | HIGH | XS | live |
| 259 | The script parser is line-based; no `\n` escape | TEDIOUS | MED | XS | live |
| 260 | `text_area`'s word motion does not collapse a selection | SHARP EDGE | MED | XS | dup→#67 |
| 261 | `text_area` has no placeholder | MISSING | MED | XS | dup→#67 |
| 262 | `text_area` hardcodes its field background | MISSING | MED | XS | dup→#67 |
| 263 | `text_area` draws no focus ring | MISSING | MED | XS | dup→#67 |
| 264 | `default_keymap()` is not macOS-correct | FOOTGUN | HIGH | S | live |
| 265 | The ring is three outlines, not one | — | HIGH | XS | dup→#83 |
| 266 | The ring's offset is one number for the whole app | — | MED | S | dup→#83 |
| 267 | The ring is drawn with no reference to whether focus moves | — | MED | XS | dup→#83 |
| 275 | Nothing asks whether a widget is inside its PARENT | — | HIGH | S | **live — top 10** |
| 276 | `Dim::Percent` ignores the child's own margin | FOOTGUN | HIGH | XS | live |
| 277 | The 5px label inset is hard-coded and unqueryable | FOOTGUN | HIGH | S | **live — top 10** |
| 285 | Every element-addressed input command is a CLICK | TEDIOUS | MED | S | live |
| 286 | A widget cannot know its own position on the frame built | — | MED | M | live |
| 287 | There IS a drag primitive, unreachable from the config | — | HIGH | XS | live |
| 325 | `with_debug_name` takes a `std::string` | PERF | MED | XS | dup→#180 |
| 326 | `virtual_list` handles UNIFORM row heights only | MISSING | HIGH | S | **live — top 10** |
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
| 340 | Styled text re-wraps and re-allocates on the RENDER path, per frame | MISSING | HIGH | M | **live — top 10** |
| 341 | What a second pane costs (hanabi's own accounting) | PERF | — | — | app |
| 380 | A custom command cannot own its timeout message | TEDIOUS | MED | XS | dup→#223 |
| 381 | The directory mode runs a suite in one process with no reset | MISSING | HIGH | S | dup→#223 |
| 405 | Trackpad and wheel arrive as the same float; one `scroll_speed` cannot serve both conventions | MISSING | HIGH | S | **live** |
| 406 | `HandleScrollInput` skips the ancestor-scroll correction its sibling `HandleScrollbarDrag` applies | TEDIOUS | LOW | XS | neg (latent) |
| 407 | An injected wheel event is delivered on TWO frames, so a script cannot spell one notch | MISSING | MED | XS | **live** |
| 408 | `assert_ui` cannot see a scroll offset, though `dump_ui_node` prints one | TEDIOUS | MED | XS | **live** |
| 409 | An OS preference read inside the per-frame widget build, 333 ns a panel a frame | PERF | LOW | S | app (fixed) |
| 410 | The only handle on a widget from outside is a linear walk of every entity | MISSING | LOW | S | **live** |
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
