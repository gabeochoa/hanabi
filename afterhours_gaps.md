# afterhours gaps (from building hanabi)

> **Start with [`afterhours_gaps_index.md`](afterhours_gaps_index.md).** This
> file is the record — 189 entries, ~10,000 lines, appended by many hands. The
> index is the work queue: a ranked top ten with the argument for each, every
> entry weighed for impact and upstream size, the thirteen families that are one
> mechanism seen from thirteen angles, the nine entries the vendored source shows
> are wrong or already fixed, the deliberate NOT-A-GAP results that must not be
> promoted, and the map of the nine gap numbers that are used twice.
> **Numbers here are never reused or renumbered** — source code and eight commit
> messages cite them.

Running log of capabilities the vendored `afterhours` immediate-mode UI does
not (readily) provide, discovered while building hanabi's app code. These are
NOT implemented in `vendor/afterhours/` (that submodule is owned by external
maintainers and off-limits). Each entry is a TODO: what was missing, why it was
wanted, the app-code workaround used, and the minimal upstream change that
would help.

Nothing here BLOCKED Phase 2 — every item has a working app-code workaround.

---

## INDEX (what a mature UI toolkit would give us for free)

Grouped for review. "AN-#" = the Animation sub-series (post-MVP delight); its
numbers are independent of the main series (both happen to reuse 8–12).

**Text & layout**
- #18 no flex-grow (can't pin a trailing element right)
- #22 styled/colored label spans don't word-wrap (blocks inline `code` pills)
- #23 no off-screen culling / list virtualization for scroll views
- #24 wrapped text ignores hard `\n` line breaks
- #30 no scroll-anchor / preserve-position-on-prepend (load-older snapped to top)
- #31 virtualization window built from a STALE offset (no velocity/next-offset ⇒ fast-fling blanks)

**Scroll / hit-testing / input**
- #3 absolutely-positioned button click vs manual hit-test
- #26 `HasScrollView` has no built-in (draggable) scrollbar
- #29 single global `hot_id`: a hoverable child steals the parent row's hover fill

**Rendering / compositing**
- #13 `draw_texture_pro` has no alpha blending
- #14 `load_texture` sampler has no mipmaps (minified icons alias)
- #15 low-alpha `with_custom_background` renders OPAQUE
- #25 `draw_rectangle_rounded` degenerate triangle on mixed round/sharp corners

**Per-frame cost**
- #27 immediate-mode clears + rebuilds the whole tree every admitted frame; Hanabi now retains the last frame while idle (#540), but the upstream dirty-layer gap remains
- #200 every headless resize leaks five Metal render pipelines (4.8 MB per 1000 frames), so a resize scenario cannot gate anything
- #340 every styled label re-wraps and re-allocates on the DRAW pass, uncached: 367 vector allocations/frame at one pane, the app's biggest single site
- #341 (ours, unfixed) a second pane costs 1.42x frame CPU and 1.79x allocations; what the remaining 1.79x is and what fixing it would take

**More than one of something (split view)**
- #335 two independent view trees in one window is not a notion the library has — focus, the collection and the identity map are all process-wide singletons
- #336 tab order cannot be scoped to a subtree; `FocusClusterRoot` only steers the ring, never navigation
- #337 #147's consequence: with two panes a debug name stops naming one widget, and the driver silently gets either
- #338 NOT A GAP: `mk` hashes `parent.id`, so two subtrees from the same call sites are disjoint; `TextMeasureCache` is width-independent
- #339 NOT A GAP: `imm::divider` and `hsplit` already exist — the hand-rolled divider had the exact bug the library's doc comment warns about

**Widgets**
- #17 imm `text_input` ignores `with_font_size` / `with_custom_background`

**OS integration**
- #1 / #16 no OS appearance (light/dark) query
- #5 macOS menu-bar extra (NSStatusItem) — app-side .mm
- #28 no OS window-focus / frontmost query ⇒ can't focus-gate a global hotkey (Cmd+Shift+N stole Chrome's incognito)

**Animation sub-series (AN, post-MVP)**
- AN-8 per-item stagger/delay · AN-9 exit animation · AN-10 one-shot state-change trigger · AN-11 shimmer/gradient-mask · AN-12 drag gesture + spring-to-slot

**Testing / headless**
- #6 headless capture can't supersample (hi-DPI) · #21 `--screenshot` waits on list not transcript load

**Icon-atlas resource gaps (ours, not framework)**
- #19 waiting/attention glyph · #20 automated/scheduled glyph

**Watch-only**
- #7 RAM knobs · #8 windowed launch cost is OS/graphics-init dominated (log-only)

**Added 2026-08-22** (full entries at the end of the file)
- #37 no text selection on read-only text — you cannot copy an answer *(blocks)*
- #38 a container can't report hover unless it carries a click listener
- #39 the e2e runner never fails a single-script run *(blocks, silently)*
- #40 the e2e runner never observes the last command's result *(blocks)*
- #41 the e2e harness has no worked example of a host loop
- #42 the draw path re-measures every string every frame; `TextMeasureCache` is bypassed *(~21% of an idle frame)*
- #43 component lookup goes through `dynamic_cast`, so type identity costs a `strcmp` *(~16%)*
- #44 the imm builder copies `ComponentConfig` by value on every widget *(~7%)*

**Added 2026-08-25** (full entries at the end of the file)
- #160 a component is two cache misses to write four bytes; no per-entity user word *(3.6x the cost of a side table)*
- #161 a failed scripted assertion truncates the evidence to 200 chars, and `dump_ui` is not registered
- #162 an app can retire the widgets it built and cannot see the ones the library built
- #163 a scroll view off-screen is measured against zero children, so leaving a screen resets it to the top *(pre-dates the retirement work)*

**Added 2026-08-26** (full entries at the end of the file)
- #275 nothing asks whether a widget is inside its PARENT: the one warning is main-axis only and goes to a discarded log, and `assert_no_overflow` measures the VIEWPORT *(it named 1 of 55)*
- #276 `Dim::Percent` is the one sizing mode that ignores the child's own margin, so `percent(1) + margin` overflows by exactly the margin — and moves the WRAP, not just the child
- #277 the 5px a label is drawn at is hard-coded, unexposed and unqueryable, so a text child and a drawn child of one parent are on different columns *(the app carries 5 AND 6 for it)*

---

**Added 2026-08-25** (windowing the digest screens; full entries at the end)
- #220 a scroll view's viewport is zero on frame one, and under #115 one uncapped frame is a permanent plateau
- #221 `with_label` takes `const std::string&`, so every label is a heap allocation per widget per frame
- #222 an absolutely-positioned child still counts in its parent's flex flow (120 log lines a frame)
- #223 the e2e runner's retry budget is named in seconds and fed by the host's `dt`, so reproducibility is the host's undocumented decision
- #224 nothing can measure a child without building it, so a variable-height window re-implements the box model

---

**Added 2026-08-26** (text editing in the composer; full entries at the end)
- #255 an editing feature is opted into by ENUMERATOR NAME, and opting out is silent — no error, no warning, nothing to grep *(cost hanabi word editing for its whole life)*
- #256 correction to #49: `CMD+` in a script means Ctrl, and `SUPER+` is parsed and dropped — so a Cmd chord IS scriptable if the app binds the Ctrl twin
- #257 no InputAction for delete-to-line-start, and an outside system cannot erase text: `init_state` re-seeds the field from the string it is bound to
- #258 `expect_input_text` is the only field assertion never taught about `HasTextAreaState`, and the only one that reads a field's TEXT *(a multiline field cannot be asserted at all)*
- #259 the script parser is line-based with no newline escape, so a multi-line expectation cannot be written down
- #260 `text_area`'s word motion does not collapse a selection to its near edge; `text_input`'s does, and `text_area`'s own char motion does
- #261 `text_area` has no placeholder — the word does not occur in the file
- #262 `text_area` hardcodes its field background and ignores `with_transparent_bg`
- #263 `text_area` draws no focus ring; `text_input` sets a 2px accent border when focused
- #264 `default_keymap()` is not macOS-correct: every editing chord is bound to Cmd and Ctrl alike, Option is bound to nothing, so Cmd+Left is "previous word" and Cmd+Backspace is "delete word"
- #366 `wrap_text_to_width` returns the lines and not their offsets in the source, and a break CONSUMES the whitespace at it — so a query straddling a wrap is in no rendered line and a consumer must reconstruct the mapping *(the smaller, cheaper half of #51)*

**Search subsystem: what it costs, and what is left** *(docs/SEARCH.md)*
- #365 find-in-conversation re-normalized every loaded message every frame — FIXED by a bounded per-pane memo; 223×–264× lower collection cost and a level gate that fails when unchanged frames revisit rows
- #367 opening Cmd+Shift+F parsed the whole disk cache on the UI thread — **370 ms at 2000 threads**. FIXED: 0.2 ms to open, 8 transcripts a frame after
- #368 the sidebar's deep filter reads a file per thread on the first frame of every NEW query — **165 ms at 2000 threads**, memoized to 0.05 ms after. NOT FIXED
- #369 a sidebar search plus "Show N more" un-virtualizes the list, and the two tests that would catch it never overlap. NOT FIXED; blocked on #224/#326
- #370 NOT A GAP: `ui::measure_text_line` / `TextMeasureCache` exist and hanabi's highlight code bypasses them — ours to fix, not theirs
- #371 four small leftovers: two snippet cutters with one bug, the mock reading another backend's cache, no Unicode folding, `advance(..., Step::None)` returning 0
- #372 the sidebar truncates search results and the catch-all group is headerless, so nothing says how many matched. NOT FIXED
- #373 ideas considered and rejected, with reasons
- #435 plain wrapped labels rebuild their line vectors on every draw; measured update to #42/#340
- #436 styled labels independently rebuild nested wrapped runs every draw; measured update to #340
- #437 the renderer exposes no byte-to-rectangle layout map, so find highlight wraps again; measured refinement of #51
- #438 visible rich text still reparses markdown and copies configs every frame; app-owned remainder after #365

**Added 2026-08-26** (the glyph atlas; full entries at the end of the file)
- #350 nothing can be asked of the font atlas — not its occupancy, not whether a measurement dropped a glyph — so a PARTIAL drop (622 px where the truth is ~5000) is undetectable from outside *(the residue #211's detector cannot cover)*
- #351 `fonsSetErrorCallback` is exactly the hook needed, is never called, and the `FONScontext` is a backend-private static — so `FONS_ATLAS_FULL` is raised, checked against a null pointer and discarded, twice per dropped glyph
- #352 the atlas is a hardcoded 2048×2048 with no `graphics::Config` field, so the ceiling belongs to the library *(same request as #210's pool sizes)*
- #353 a dropped glyph is not DRAWN either, so an unmeasurable string is also invisible — the failure looks like missing data, not like a font problem
**Added 2026-08-26** (shipping the multiline composer; full entries at the end)
- #305 `text_area` re-wraps its whole text EVERY FRAME — `needs_layout_rebuild` exists and nothing calls it — and measures through the raw backend instead of `TextMeasureCache` *(+196 allocs/frame for a 130-char draft standing still; patch proven, `vendor_patches/305-…`)*
- #306 `with_auto_grow` knows the row count and `ElementResult` will not carry it, so an app drawing the BOX around a growing field must read `state.layout_cache` to size it
- #307 NOT A GAP: `HasTextAreaState::line_index` moves no caret — `text_area` navigates by VISUAL rows off `layout_cache` — so an outside write that leaves the index stale is unobservable *(a one-line "fix" whose test passes without it)*
- #308 `assert_ui` understands x/y/w/h/hidden/text and nothing about colour, so 106 scripted UI tests passed straight through both #262 and #263

**Resolved / corrected**
- #115 (a widget that stops being built is never retired) is **fixed upstream**
  by `2393fe3` + `c682382`; Hanabi now consumes the library's frame stamp,
  ordered cleanup, and retirement sweep directly. Its allocation-free `mk`
  key remains only for the separate cost in #180.
- #29 (a hoverable child steals the parent row's hover fill) is **fixed** —
  `ctx.mouse_was_in_subtree(id)` is exactly the primitive, verified against a
  real pointer; hanabi's hand-rolled workaround is deleted.
- #27's "~8.6ms idle-frame floor" was **mostly our own missing `-O2`**, not the
  per-frame rebuild. See the perf section at the end. The design observation
  stands; the number was wrong and we're sorry for the bad signal.

The promote-these-upstream synthesis (grouped by theme, with proposed API
shapes) is in sections A–H near the end.

---

## 1. No OS appearance ("follow system" light/dark) query
- **Gap:** afterhours exposes no way to read the host OS's current appearance
  (macOS `NSApp.effectiveAppearance` / `AppleInterfaceStyle`) or to subscribe
  to appearance-change notifications.
- **Why wanted:** the design intent (docs/decisions.md #6) is to follow macOS
  system appearance for light/dark. hanabi has a full swappable token set
  (`src/ui/theme.h`) and switches at runtime, but currently only via a
  persisted explicit setting (dark default), not by mirroring the OS.
- **App-code workaround:** ship an explicit dark/light setting (persisted in
  Settings); "follow system" is left as a TODO. A future app-side ObjC++ shim
  in `src/sokol_impl.mm` could query `AppleInterfaceStyle` directly without any
  afterhours change — so this may not need upstream at all.
- **Minimal upstream help (optional):** a tiny platform accessor, e.g.
  `afterhours::graphics::system_appearance() -> {Light,Dark,Unknown}` plus an
  "appearance changed" event, so apps don't each re-implement the ObjC++/Win32
  probe. Low priority; app-side shim is viable.

## 2. No built-in property tween / animation helper
- **Gap:** there is no animation/tween utility (e.g. an eased value that
  advances by `dt` toward a target). Sizes are recomputed per frame from data,
  which is flexible but means every animated property is hand-rolled.
- **Why wanted:** the collapsible sidebar animates its width with a smoothstep
  ease (mirroring the mock's `.18s ease` and floatinghotel's approach).
- **App-code workaround:** hand-rolled in `LayoutSystem` — store
  `animFrom/animTarget/animT` on the layout component, advance `animT += dt/dur`
  each frame, and apply `smoothstep(animT)`. Works fine (see
  `src/ecs/layout_system.h`). This is the established afterhours pattern, so
  arguably not a real gap — just boilerplate.
- **Minimal upstream help (optional):** a header-only `Tween`/`AnimatedValue`
  helper (`step(dt)`, easing fns) to remove per-app boilerplate. Very low
  priority.

## 3. Absolutely-positioned `button()` click result vs. manual hit-testing
- **Gap:** for a horizontal tab strip laid out with absolute positions +
  `with_translate(x,y)` (each tab and its close-× overlapping in one row), it
  was simpler/more reliable to hit-test with `afterhours::ui::is_mouse_inside(
  ctx.mouse.pos, RectangleType{...})` + `ctx.mouse.just_pressed` than to rely
  solely on each `button()`'s `ElementResult`, because overlapping absolutely-
  positioned buttons (tab body vs. its close button) made the returned click
  ambiguous.
- **Why wanted:** VS Code-style tabs with a close × sitting on top of each tab.
- **App-code workaround:** manual `is_mouse_inside` hit-tests for tab-body and
  close-× regions, consuming `ctx.mouse.just_pressed` on close so the click
  doesn't also fall through to the tab body (see `src/ecs/tab_bar_system.h`).
  This is exactly what the reference app (floatinghotel) does too, so it's a
  known/accepted pattern rather than a hard blocker.
- **Minimal upstream help (optional):** clearer documented z-order/hit-test
  semantics for overlapping absolutely-positioned interactive widgets, or a
  helper that returns the top-most clicked element among overlapping siblings.
  Low priority.

## 4. Status-glyph shapes — NOT a gap (real primitives reachable)
- **Context:** Phase 2.1 replaced the sidebar rows' text tag chip + attention
  dot with a dedicated SHAPE-per-status glyph (red up-triangle = blocked,
  green diamond = review, blue dot = done). This needed non-rectangular
  drawing inside a render system.
- **Resolution:** afterhours DOES expose the primitives — the sokol backend
  (`backends/sokol/drawing_helpers.h`) provides `afterhours::draw_triangle`,
  `afterhours::draw_poly` (used with 4 sides for the diamond), and
  `afterhours::draw_circle_v`. And the immediate-mode UI exposes a per-widget
  custom-draw hook: `ComponentConfig::with_on_draw_fg(std::function<void(
  RectangleType)>)`, invoked by the renderer with the widget's final on-screen
  rect (`plugins/ui/rendering.h`). So the glyphs are drawn with REAL distinct
  shapes (no rectangle approximation) via a small transparent glyph-slot widget
  whose `on_draw_fg` paints the shape. See `SidebarSystem::draw_glyph` in
  `src/ecs/sidebar_system.h`. Logged here only as a discovery note — this was
  NOT a gap; the library had everything needed.

## 5. macOS menu-bar extra (NSStatusItem)
hanabi wants a menu-bar/status-bar item (up by the clock) that shows a
blocked-on-you count and opens a new chat on click. afterhours only creates a
normal Sokol app window (sapp_run) — there is no NSStatusItem / menu-bar-extra
/ LSUIElement path in the library. Workaround plan: implement it in our OWN
Objective-C++ (.mm) code (NSStatusItem + NSMenu) alongside the existing
src/sokol_impl.mm, NOT in vendor. Upstream ask (optional): a hook to run app
code without owning the main window, or a documented way to coexist with an
app-owned NSStatusItem. Deferred to Phase 4.

### #6 — Headless offscreen capture cannot supersample (hi-DPI screenshots)
The WINDOWED Metal path already sets `desc.high_dpi = true`, so the live app is
crisp on Retina — no gap there. But the HEADLESS capture path
(`graphics::init` with `DisplayMode::Headless` → `metal_init`) creates a fixed
`cfg.width x cfg.height` offscreen texture at 1x and renders into it directly.
`graphics::Config.hidpi` is read ONLY by the raylib backend
(`vendor/afterhours/src/backends/raylib/windowed.h`); the Sokol/Metal backend
ignores it and never sets `graphics::render_scale()`. Rendering into a 2x-sized
texture is NOT a workaround — the adaptive UI just lays out at the larger
logical size (thin sidebar in a big canvas), it does not supersample.
IMPACT: `--screenshot` PNGs used for docs + pixel-perfect phase validation are
1x and look soft; they under-represent the (crisp) real window.
WORKAROUND: none clean in app code without touching vendor. For validation we
compare layout/proportion/color at 1x; treat softness as a capture artifact,
not an app defect.
UPSTREAM ASK (minimal): have the Metal headless path honor `Config.hidpi` —
allocate the offscreen texture at `width*scale x height*scale`, set
`render_scale(scale)`, and keep the ortho projection in logical pixels (same
contract the windowed high_dpi path already uses), so the capture is a true
@2x supersample. Deferred; do NOT patch vendor.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The core upstream gap remains (no true headless supersample), but the old absolute claim that rendering at 2x is not a workaround is stale: current scripts pair 2x dimensions with HANABI_UI_SCALE=2.0 and downsample. It is intentionally opt-in because text metrics get worse.

**Hanabi reference.** `src/main.cpp` (`What DOES work, and the old note here said it did not`) — headless capture now documents the 2x-size plus HANABI_UI_SCALE path and why it is zoom/downsample rather than true supersampling. `scripts/shoot_hanabi.sh` (`2x CAPTURE (HANABI_SHOOT_2X=1)`) — the review capture script implements the opt-in 2x path and downsample step. Measurement/gate: `docs/visual-parity/FRICTION_LOG.md` (`Capturing at 2x (feat/vis-hidpi)`) — the visual-parity measurement record for the 2x capture experiment.



### #7 — (watch) memory knobs for RAM budget < 250 MB
hanabi targets peak RSS < 250 MB. Not a gap yet (baseline ~70 MB windowed), but
IF we later need to cap/evict GPU-side memory (font atlas is fixed 2048x2048 in
setup_sokol_gl_and_fonts; per-texture image cache), afterhours may not expose an
API to size/evict those. If we hit the ceiling: do NOT patch vendor — record the
exact knob needed here (e.g. configurable font-atlas dimensions, a texture-cache
eviction hook) and work around in app code (e.g. our own image-cache eviction for
textures we own). Placeholder until measured.

**Hanabi reference.** Hanabi-owned performance finding: `src/util/texture_budget.h` (`kDefaultMaxEntries <= hanabi::gpu::kMaxLiveTextures`) — hanabi now has an app-owned texture-cache policy bounded by bytes and sampler-pool capacity. `src/gpu_mem.mm::hanabi_metal_allocated_bytes` — GPU memory is measured through the Metal device rather than inferred from malloc. Tests: `tests/unit/test_texture_budget.cpp::test_the_budget_bounds_what_the_device_holds` — unit coverage for the texture byte budget and entry cap. Measurement/gate: `docs/perf/MEMORY.md` (`The GPU half is no longer blind`) — current memory audit and GPU/RSS attribution.



### #8 — Windowed launch cost is dominated by the vendored/OS graphics init (NOT ours; log-only)
Rigorous windowed-launch profiling (2026-08-02, cli:aspen; `HANABI_STARTUP_PROF=1`
+ `HANABI_QUIT_AFTER_FIRST_FRAME=1` — both diagnostic-only envs added in
src/main.cpp) decomposed the REAL windowed cold launch (`main()` →
`graphics::run` → sokol `sokol_init_cb` [`sg_setup` + `setup_sokol_gl_and_fonts`]
→ our `app_init` → first `app_frame`):

    phase                                cold        warm
    Gfx init (process→app_init entry)    150-205ms   122-149ms   ← vendored/OS
    App init (preload+state+systems)     1-6ms       0-1ms       ← OURS
    WindowedFirstFrame (total)           171-266ms   146-170ms

The overwhelming majority of the launch number is **Gfx init**: everything from
process start until our `app_init` runs — i.e. `sapp_run()` creating the
Cocoa/Metal window + GPU context, `sg_setup()`, and `setup_sokol_gl_and_fonts()`
(the fixed 2048×2048 fontstash atlas alloc). All of that is inside vendored
afterhours / the OS Metal driver. Clearing the Metal function/pipeline cache
(`~/…/C/com.apple.metal`) adds ~40-70ms to Gfx init (first-draw shader/pipeline
compile) — also a Metal-driver cost, not ours. This span is NOT ours to cut and
we do NOT patch vendor; the split log makes it honest (App-init is ~1-3ms, so
"our" launch cost is negligible).

Note: the earlier reported 1.1-1.4s cold launches were NOT reproducible in
steady state on this machine — they were either (a) a genuinely-cold box (cold
dyld shared cache after reboot + cold Metal driver + first `amfid` signature
validation of a never-run adhoc binary) which is a one-time OS cost that
disappears on the 2nd run, OR (b) the auth-blocking bug below (which WAS ours and
is now fixed). `DYLD_PRINT_STATISTICS` is suppressed by the hardened runtime on
the adhoc-signed binary, so dyld pre-main can't be attributed from inside.

UPSTREAM ASK (optional, low priority): a hook/callback afterhours could fire
right at the top of `sokol_init_cb` (before `sg_setup`) so an app can time the
window-create vs GPU-setup split itself. Until then, Gfx init is a single
opaque vendored span. Log-only; do NOT patch vendor.



**POSTSCRIPT 2026-08-26 (source-reference audit).** Conclusion still holds, but the numeric ranges in the entry are stale relative to docs/perf/STARTUP.md's current 229/171/187 ms gfx-init runs and 38/1/1 ms app-init runs.

**Hanabi reference.** Negative result: `src/main.cpp` (`Gfx init: {} ms (window + GPU/Metal context`) — startup logging still separates vendored/OS graphics init from app init. `scripts/measure_launch.sh::WindowedFirstFrame` — the measurement script extracts the windowed first-frame split. Measurement/gate: `docs/perf/STARTUP.md` (`The windowed number is a different number, and it is not ours`) — current launch measurements and conclusion that warm app init is about 1 ms.

---

## Animation gaps (V2 / post-MVP — surfaced by docs/animation-assessment.md)

The `Anim` declarative builder (`plugins/ui/animation_config.h`) and the
key-based manager (`plugins/animation.h`) cover the core delight set (hover,
press-spring-back, appear fade, loop pulse, idle float, count tick). The items
below are the residual gaps for the "delightful V2" direction. All are
NON-BLOCKING and have an app-code workaround (the manual per-frame lerp/spring
pattern already proven in `src/ecs/layout_system.h`). Do NOT patch vendor.

### AN-8 — No per-item stagger / delay on declarative animations
- **Gap:** `AnimationDef` (`animation_config.h:47-60`) has no `delay` /
  `stagger_index`. Every `OnAppear` starts the frame the widget first renders,
  so a list of rows fades in all at once — no cascade.
- **Why wanted:** staggered fade+slide as threads load into the sidebar (a
  signature "delightful" moment).
- **App-code workaround:** use the key-based manager per row —
  `animation::one_shot(RowFade, i, ...)` with a leading `.hold(i * 0.03f)`
  segment before the fade, or gate each row's first-emit by index. See the
  stagger-helper plan in the assessment §4.
- **Minimal upstream help (optional):** a `delay` (and/or `stagger_index * step`)
  field on `AnimationDef`, applied before the track goes active.

### AN-9 — No exit / "leaving" animation (OnExit) in immediate mode
- **Gap:** triggers are `OnAppear/OnClick/OnHover/OnFocus/Loop`
  (`animation_config.h:14-20`) — there is no `OnExit`. In immediate mode, when a
  row/tab's data is gone the widget simply isn't emitted next frame; nothing
  keeps a departing widget alive to animate out.
- **Why wanted:** rows/tabs fading or sliding out when closed; a true screen
  cross-fade (outgoing pane fades while incoming fades in).
- **App-code workaround:** app-owned "keep-alive" — hold a departing item in a
  fading set for N ms and drive its `with_opacity`/`with_translate` from a manual
  tween before dropping it; for screens, one `transitionT` float (same shape as
  the sidebar smoothstep) crossfading both panes.
- **Minimal upstream help (optional):** an `OnExit`/leaving lifecycle, or a
  "keep this widget alive M ms after its last emit and run its exit anim" hook.
  This is the single biggest structural gap; hard in pure immediate-mode.

### AN-10 — No one-shot "value/state changed" trigger on a widget
- **Gap:** the declarative triggers are interaction/appearance edges; there's no
  "this widget's underlying value changed" trigger to fire a one-shot flash.
- **Why wanted:** a row flashing/pulsing once when its status flips to
  needs-you/blocked.
- **App-code workaround:** detect the transition in the owning system
  (sidebar/row) and fire a `one_shot` fade via the key-based manager, or drive a
  short manual tween. The manager's `on_change`/`on_step`
  (`animation.h:219-230`) covers the *counter* case cleanly.
- **Minimal upstream help (optional):** an `OnValueChanged`-style trigger, or a
  documented pattern for app-driven one-shot widget anims.

### AN-11 — No shimmer-sweep / gradient-mask primitive
- **Gap:** a pulsing-opacity skeleton is trivial (`loop().opacity(...)`), but a
  moving *shimmer sweep* (highlight band translating across a placeholder) needs
  a gradient mask / animated `background-position`-style effect, which the
  drawing layer doesn't expose. `loop().translate_x(...)` on a highlight widget
  approximates it coarsely.
- **Why wanted:** polished skeleton/shimmer loading placeholders.
- **App-code workaround:** approximate with a translating highlight rect via
  `with_on_draw_fg`, or accept a pulsing skeleton (Supported) for MVP-of-V2.
- **Minimal upstream help (optional):** a linear-gradient fill / mask primitive
  in the sokol drawing helpers.

### AN-12 — No drag gesture + spring-to-slot path
- **Gap:** interaction triggers are boolean edges (`OnClick/OnHover/OnFocus`);
  there's no pointer-delta drag model, and no "animate toward a moving/dropped
  target" affordance in either system.
- **Why wanted:** spring-based tab reorder (drag a tab, others spring aside, it
  settles into its slot).
- **App-code workaround:** track drag delta app-side on the existing manual
  tab hit-test (see gap #3) and, on drop, run a manual spring — reuse
  `afterhours::ui::anim::spring(...)` (`animation_config.h:261-279`) on an
  app-owned `AnimTrack`, or the key-based `.to(target, dur, easing)`.
- **Minimal upstream help (optional):** a drag-gesture helper returning a live
  delta, and/or a "spring an owned value toward a target" convenience.

### #13 — draw_texture_pro has no alpha blending (sgl default pipeline)
- **Gap:** `afterhours::draw_texture_pro` (sokol backend,
  drawing_helpers.h ~1222) emits a textured quad but does NOT enable alpha
  blending, and `begin_drawing` calls `sgl_defaults()` every frame which loads
  the sokol_gl DEFAULT pipeline (blending disabled by default in sokol_gfx).
  Result: a white-on-transparent PNG atlas blits its transparent pixels
  (rgb=0, a=0) as OPAQUE BLACK squares — per-texel alpha is ignored. (Filled
  shapes/text look fine because shapes are opaque and fontstash uses its own
  blended pipeline.)
- **Why wanted:** blitting monochrome icon sprites from a tinted atlas (Phase H
  chrome icons) — the whole point is per-texel alpha for the icon shape.
- **App-code workaround (used):** create ONE blend-enabled `sgl_pipeline`
  (`src_alpha` / `one_minus_src_alpha`) lazily in app code and wrap each blit in
  `sgl_push_pipeline(); sgl_load_pipeline(pip); draw_texture_pro(...);
  sgl_pop_pipeline();`. All `sgl_*`/`sg_*` symbols are already reachable from
  app TUs (drawing_helpers.h calls them), so no vendor edit is needed. See
  src/ui/icons.h (AtlasTexture::blend_pipeline + draw_fg).
- **Minimal upstream help (optional):** either enable blending in the sokol_gl
  default pipeline used for 2D UI, or add a `draw_texture_pro` variant that
  pushes a blend pipeline around the quad, so callers don't each roll their own.

**Hanabi reference.** `src/ui/icons.h` (`A pipeline with standard src-alpha over blending`) — AtlasTexture constructs a blend-enabled sgl pipeline in app code. `src/ui/icons.h::sgl_load_pipeline(AtlasTexture::get().blend_pipeline())` — icon blits push the blend pipeline around draw_texture_pro.



### #14 — load_texture sampler has no mipmaps (aliases when minified)
- **Gap:** `afterhours::load_texture` (sokol backend) creates its image with a
  sampler using `mipmap_filter = SG_FILTER_NEAREST` and a single mip level (no
  mipmap chain is generated). So when a texture is drawn much SMALLER than its
  source resolution, the GPU does bilinear minification off the full-res level
  with no mip pyramid — thin high-contrast features (line-icon strokes) alias /
  shimmer badly. There is no app-side way to request mipmap generation,
  trilinear, or anisotropic filtering through `load_texture`.
- **Why wanted:** a monochrome icon atlas authored at a comfortable size then
  drawn small (Phase H chrome icons: a 64px cell drawn at ~16px = 4x
  minification) aliases visibly.
- **App-code workaround (used):** author the atlas NEAR the real draw size so
  the minification ratio stays <= ~2x, which plain bilinear handles cleanly.
  We regenerated icons.png at 32px cells (was 64px). Icons draw at ~14-16px
  logical (up to ~32px on a 2x-retina window), so 32px cells keep the sampled
  ratio in bilinear's clean range. See scripts/gen_icons.py (CELL=32) +
  src/ui/icons_atlas.h.
- **Minimal upstream help (optional):** generate a mipmap chain in
  `load_texture` (and set `mipmap_filter = LINEAR` for trilinear), or add a
  `load_texture` variant/param to opt into mipmaps + an anisotropy level, so
  minified textures don't alias.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The mipmap limitation/workaround remains, but the entry's atlas inventory is stale: the atlas now has 23 entries, including clock/automated/hand/check_circle/pin/panel_left/sliders.

**Hanabi reference.** `scripts/gen_icons.py` (`CELL = 32`) — the generator authors atlas cells near draw size to avoid large minification. `src/ui/icons_atlas.h` (`inline constexpr int kCell = 32`) — the generated atlas currently uses 32 px cells.



### #15 — low-alpha `with_custom_background` renders OPAQUE (UI rect fill, not just textures)
- **Gap:** same root cause as #13 (sokol_gl default pipeline has blending
  disabled), but it bites the ordinary UI path, not just texture blits. A
  translucent color passed to `ComponentConfig::with_custom_background(Color{r,g,b,a})`
  with a < 255 is filled by `draw_rectangle` (sokol drawing_helpers.h) as a
  FULLY OPAQUE quad — the alpha byte reaches `sgl_c4b` but the non-blended
  pipeline discards it. So a "soft tint" surface (e.g. a status pill authored
  as `{220,60,60,31}` ≈ 12% red) renders as a SATURATED SOLID block, and any
  same-hue label text on it drops to zero contrast (invisible).
- **How it showed up:** hanabi digest tag chips (BLOCKED/DONE) rendered as solid
  red/blue rectangles with no visible label — the intended subtle translucent
  pill was impossible via alpha alone. Pixel-sampled: token said a=31, screen
  showed a=255.
- **Why wanted:** translucent tint surfaces are a core UI idiom (chips, hover
  overlays, selection washes, badges) — you want to author "12% of accent over
  whatever's behind" and have it composite, not force-pick an opaque color per
  surface/theme.
- **App-code workaround (used):** pre-composite the tint OVER the known surface
  color into an opaque color before handing it to `with_custom_background`.
  Added `theme::over(fg, bg)` (src-over in app code) and blend the tag bg over
  the card surface (`theme::over(tag_bg, panel_bg_2())`). Works, but the caller
  must KNOW the exact backdrop color — brittle when surfaces stack or the
  backdrop is itself dynamic. See src/ui/theme.h (over) + src/ecs/main_pane_system.h (tag_bg).
- **Minimal upstream help (optional):** enable src-over blending on the UI fill
  pipeline (same fix that would resolve #13 for the general case), so a
  low-alpha `with_custom_background` composites over whatever it's drawn on —
  no per-call-site backdrop knowledge or manual pre-blend needed.

**Hanabi reference.** `src/ui/theme.h` (`inline Color over`) — theme::over pre-composites translucent tokens into opaque colors. `src/ecs/main_pane_system.h` (`return theme::over(theme::tag_blocked_bg(), surface)`) — tag-chip backgrounds are pre-composited over their known surface.



### #16 — No OS appearance (light/dark) query
- **Gap:** afterhours exposes no way to read the host OS's current appearance
  (macOS `AppleInterfaceStyle` / `NSApp.effectiveAppearance`, or the equivalent
  on other platforms). There is nothing like `graphics::os_appearance()` or a
  changed-notification hook.
- **How it showed up:** Phase K settings panel offers a THEME choice of
  Light / Dark / System. Light and Dark work fully (Settings::set_theme +
  theme::set_mode). "System" cannot be honored — there is no reachable signal
  for what the OS is set to.
- **Why wanted:** "match system" is a standard, expected theme option in a
  native desktop app; users toggle OS dark mode and expect the app to follow.
- **App-code workaround (used):** "System" is a labelled *choice* that is
  remembered in `AppComponent::themeChoice == "system"` but currently falls
  back to the Dark palette for rendering (with a note in the panel footnote).
  See src/ecs/settings_system.h (apply_theme).
- **Minimal upstream help (optional):** a `graphics::os_appearance()` returning
  {Light, Dark, Unknown} plus an appearance-changed callback so "System" can be
  resolved live without app-side platform code.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's workaround statement is obsolete for current hanabi: System is now backed by a macOS appearance probe.

**Hanabi reference.** Current code: `src/ecs/settings_system.h` (`"System" now tracks the real macOS appearance`) — System theme now resolves from the OS instead of falling back to Dark. `src/sokol_impl.mm` (`extern "C" bool macos_is_dark_mode`) — macOS AppleInterfaceStyle is exposed through the app's native seam.



### #17 — imm `text_input` ignores `with_font_size` and `with_custom_background`
- **Gap:** the immediate-mode `text_input` widget
  (vendor/afterhours/src/plugins/ui/text_input/component.h) DERIVES its font
  size from the field's computed HEIGHT (`derived_fs = field_h * 0.5f`) and
  forces its inner field background to `Theme::Usage::Secondary` from
  `ctx.theme`. So `ComponentConfig::with_font_size(...)` and
  `with_custom_background(...)` passed to `text_input` are silently ignored:
  a tall field yields an oversized font that overflows, and the field surface
  can't be tinted with the app's own theme tokens.
- **How it showed up:** Phase K composer input. A 72px-tall field rendered the
  draft at ~36px (overflowing the box); setting `.with_font_size(FontSize::Small)`
  had no effect; the field stayed a dark `Theme::Usage::Secondary` surface even
  in the app's Light theme (so it reads dark-on-a-light-sheet).
- **Why wanted:** an app with its own token-based theme needs the text field to
  (a) size its font independently of field height and (b) use the app's
  surface color, so the input matches the rest of the themed UI in both modes.
- **App-code workaround (used):** constrain the field to a single-line height
  (~34px) so the height-derived font (~17px) is readable and doesn't overflow;
  accept that the field's inner surface is themed by `ctx.theme` rather than the
  app tokens (dark in both modes). Add a caption above the field for labelling
  since placeholder/label styling is likewise not honored here. See
  src/ecs/composer_system.h (render_input).
- **Minimal upstream help (optional):** honor an explicit `with_font_size` on
  `text_input` when provided (fall back to the height-derived size only when
  unset), and let `with_custom_background` override the forced
  `Theme::Usage::Secondary` field fill.
- **Follow-on (2026-08-01, sidebar search): no placeholder / empty-state hint.**
  `text_input` has no `with_placeholder(...)` — an empty field renders as a
  bare box, so the sidebar search read as an unlabeled box + magnifier
  (hostile-review defect 13). Compounding it, the forced opaque
  `Theme::Usage::Secondary` fill (above) means a placeholder painted BEHIND the
  input is COVERED by the field's own fill — you can't just draw hint text under
  it. **App-code workaround (used):** paint a faint "Search conversations" hint
  as an ABSOLUTELY-positioned overlay child (out of flex flow, so it never
  shifts the input) at a higher render layer than the input, drawn via
  `on_draw_fg` + `afterhours::draw_text` and rendered only while the query is
  empty. The overlay's screen origin is derived from the sidebar panel geometry
  (panel xy → header height → search-wrap/field paddings + magnifier slot). See
  `src/ecs/sidebar_system.h` (render_search, `sb_search_placeholder`).
  **Minimal upstream help (optional):** add `with_placeholder(std::string)` to
  `text_input` that renders the faint hint inside the field when the bound
  string is empty (drawn on top of the field fill, cleared on first keystroke) —
  the standard text-field affordance.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The current source still has a text_input theme workaround for sidebar search, but the composer-specific claims are stale: composer is text_area, and text_input placeholder support exists.

**Hanabi reference.** `src/ecs/sidebar_system.h` (`The immediate-mode text_input forces its field background`) — sidebar search still scopes ctx.theme to work around text_input styling. `src/ecs/sidebar_system.h` (`.with_placeholder("Search")`) — sidebar search now uses a native placeholder on text_input. Tests: `tests/ui/composer_box_grows_with_the_draft.e2e` (`assert_ui composer_input_wrap h=67`) — the multiline composer grows with draft content.



### #18 — no flex-grow: can't pin a trailing element to the right edge
- **Gap:** afterhours' flex layout has no `flex-grow` / "fill remaining space" on a
  child. A row like `[icon(18px)] [label] [count(24px)]` can't make the label expand to
  push the count against the right edge. `percent(1.0f)` on the label sizes it to the
  FULL parent width (not "remaining after siblings"), and a fixed `percent(0.72f)`
  can't hit the exact right edge because the row mixes pixel + percent children and the
  usable width varies with sidebar/scrollbar state. Mixed-unit rows therefore leave the
  trailing count floating mid-right instead of right-aligned.
- **Why wanted:** the mock right-aligns every count (`.lbl{flex:1}` in CSS) so the
  smart-view counts and folder counts share ONE right edge. Without flex-grow the two
  count columns land at slightly different x (~17px apart) — each internally consistent,
  but not a shared edge.
- **App-code workaround (now: shared edge via pixel-computed labels):** since the
  overflow fix already computes row widths in pixels, size the label/name column
  EXPLICITLY (`labelW = rowContentW − leadSlot − countColW`) instead of `percent`, so
  the trailing count box is pushed flush to the row's right edge. Applying the SAME
  reserved count-column width (`kCountColW`) + right inset (`kCountRightPad`) across
  smart-view rows, folder heads, AND the time-group heads lands every count box at the
  same right edge (`panelW − kCountRightPad`). Residual is now only glyph-shape
  antialiasing (~2-3px between different digits), not a layout offset — the previous
  ~17px cross-section gap is gone. A true `flex-grow` would remove the pixel bookkeeping
  entirely, but the shared edge is achievable today.
- **Minimal upstream help (optional):** add `with_flex_grow(int)` (or a
  `SizeExpr::remaining()` / `fill()` size mode) so one child can absorb leftover main-axis
  space — the standard flexbox primitive. Would let trailing counts/badges right-align
  cleanly and remove the mixed-unit percent guessing.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The workaround remains, but the entry's named constants are stale: current source uses dynamic badge width and kBadgeRightPad rather than the older kCountColW/kCountRightPad shape.

**Hanabi reference.** `src/ecs/sidebar_system.h` (`Label column: explicit pixel width`) — smart-view labels are pixel-sized to push badges to the right edge. `src/ecs/sidebar_system.h` (`float svLabelW = kSvContent`) — the current implementation computes remaining width rather than using flex-grow.



### #19 — icon atlas has no "waiting / attention" glyph (hanabi resource gap)
- **Gap:** the generated Lucide atlas (`src/ui/icons_atlas.h`, built by
  `scripts/gen_icons.py`) carries 13 sprites — brand / gear / plus / search /
  sidebar_close / sidebar_open / chevron_down / home / **blocked** / review /
  star / folder_grid / fold_all. The **blocked** sprite is a Lucide no-entry /
  prohibition circle-slash that reads as "forbidden / banned", which is wrong
  for the Blocked smart view (its meaning is "waiting on you / needs
  attention", not "you are not allowed"). No atlased sprite reads as
  waiting/attention (no clock, hourglass, inbox, bell, or hand).
- **Why wanted:** the Blocked smart-view nav icon should signal "attention",
  matching the per-row red-triangle attention glyph.
- **App-code workaround (used):** the Blocked smart view now draws its icon as
  a WARNING TRIANGLE (outlined up-triangle + bang) via a small owned draw
  helper (`SidebarSystem::draw_attention_icon`), reusing the per-row attention
  triangle's shape so the view and its rows share one vocabulary. This lives
  entirely in `src/ecs/sidebar_system.h` — no atlas regeneration.
- **Minimal fix (owned elsewhere — scripts/gen_icons.py):** add a Lucide
  "clock" (best fit) or "bell" / "hourglass" / "inbox" glyph to the atlas and
  switch the Blocked smart view to it, so the nav uses a real atlased icon
  rather than an app-drawn shape. NOT done here (atlas + gen script are owned
  by another agent); reported for that owner to pick up.

**POSTSCRIPT 2026-08-26 (source-reference audit).** No longer true: the atlas has a hand glyph and the Blocked smart view uses it. The old draw_attention_icon helper remains in the file but is not selected.

**Hanabi reference.** Current code: `scripts/gen_icons.py` (`("hand", "hand")`) — the icon generator now includes the waiting/attention hand glyph. `src/ui/icons_atlas.h` (`{"hand",`) — the generated atlas contains the hand sprite.



### #20 — icon atlas has no "automated / scheduled" (clock/gear) glyph (hanabi resource gap)
- **Gap:** the generated Lucide atlas (`src/ui/icons_atlas.h`) still has no
  glyph that reads as "automated / scheduled / cron" — no clock, gear-cog,
  timer, or repeat/refresh sprite. The sidebar needs to de-emphasize
  scheduled/cron sessions ("Schedule: …" / "*-tick" titles) so they don't read
  as peers of real human conversations (defect #5).
- **Why wanted:** a small atlased "clock"/"repeat" sprite in each cron row's
  status slot would carry the "this is a machine job" meaning with a
  recognizable icon.
- **App-code workaround (used):** the sidebar detects an automated row by title
  shape (`SidebarSystem::is_automated`: starts with "Schedule:" or ends with
  "-tick") and draws a small FAINT HOLLOW SQUARE (`SbGlyph::Automated`) in the
  row's existing status-glyph slot, plus dims the title to the faint token.
  This is geometrically distinct from the calm round dot / Done dot / running
  ring, and lives entirely in `src/ecs/sidebar_system.h` — no atlas change.
- **Minimal fix (owned elsewhere — scripts/gen_icons.py):** add a Lucide
  "clock" or "repeat"/"refresh-cw" glyph to the atlas so the sidebar (and any
  future scheduled-job UI) can blit a real icon instead of an app-drawn box.
  NOT done here (atlas + gen script are owned by another agent). This is the
  same atlas the #19 note wants a "clock" for — a single "clock" sprite would
  satisfy both #19 (attention) and #20 (scheduled) if reused thoughtfully.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The no-glyph claim is false in current source, and the old hollow-square row treatment was removed as a design guess.

**Hanabi reference.** Current code: `scripts/gen_icons.py` (`("automated", "repeat")`) — the icon generator now includes an automated/scheduled glyph. `src/ui/icons_atlas.h` (`{"automated",`) — the generated atlas contains the automated sprite.



### #21 — headless --screenshot wait gates on session-LIST load, not TRANSCRIPT load (hanabi harness gap)
- **Gap:** `run_headless_screenshot` (src/main.cpp) pumps frames until the
  session LIST leaves Loading, then renders a fixed 45-frame budget (~tens of
  ms) and captures. The transcript message-fetch for the restored/auto-opened
  tab is an ASYNC network request kicked during those frames; against the REAL
  http backend it takes ~hundreds of ms — far longer than 45 fast frames — so
  the capture fires while `transcriptState == Loading` and the pane shows
  "Loading… / Open a thread", never the real messages. (The mock resolves the
  transcript synchronously via the in-memory cache, so mock transcripts capture
  fine — this ONLY bites the real backend.)
- **Impact:** you CANNOT headlessly screenshot a REAL transcript today. Proven:
  a real run loads 104 real sessions (listState=Loaded, sidebar fully
  populated) but the transcript pane stays Loading. Windowed capture is also
  blocked on this box (macOS TCC denies `screencapture`).
- **Fix (owned elsewhere — src/main.cpp, NOT this agent's file):** extend the
  pre-capture wait loop to ALSO wait for the active tab's transcript to leave
  Loading — e.g. after the list resolves and the first tab auto-opens, keep
  pumping frames (under the same wall-clock deadline, bump kMaxWait to ~8-10s)
  until `app.transcriptState != Loading` (or the deadline). One-liner in the
  existing wait loop's break condition. This is the single change that unblocks
  a real-transcript screenshot.
- **App-code workaround (this agent):** none possible from main_pane_system.h —
  the wait budget lives in main.cpp. Transcript RENDER correctness is proven on
  the mock (session r1 carries a real User+Assistant+Tool+Tool+Assistant mix),
  and the render path is backend-agnostic (the http adapter flattens real
  blocks[] into the same api::Message list the mock uses), so the real
  transcript will render identically once the harness waits for it.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The described headless wait bug is fixed in current run_headless_screenshot.

**Hanabi reference.** Current code: `src/main.cpp` (`tab's TRANSCRIPT to finish loading too`) — headless screenshot capture now waits for the opened transcript as well as the session list. Tests: `tests/README.md` (`The runner settles on CONTENT, not on a frame count`) — the scripted UI runner documents the same content-settle requirement.



### #22 — styled label spans (`with_styled_label`) render on ONE line, don't word-wrap
- **Gap:** `component_config.h::with_styled_label(std::vector<TextSpan>)` +
  `HasLabel::spans` support multi-COLOR runs (e.g. a green "+4" then a red "-2"),
  but the render path (`rendering.h`, the `draw_text_in_rect(hasLabel.label...)`
  call) draws the concatenated PLAIN `label` with word-wrap, and the per-span
  COLORING is documented as "on one line". So there is no way to render
  inline-styled runs (e.g. an inline `code` pill in a different color) that ALSO
  word-wraps across a multi-line paragraph.
- **Why wanted (hanabi):** the transcript wants ChatGPT/Claude-style inline
  markdown — `` `code` `` rendered as a distinctly-colored/backgrounded run
  inside a wrapping assistant paragraph, plus `**bold**`. Literal backticks in
  every message are the fastest "this isn't a real product" tell (messages
  critique #3). A wrapping body is mandatory (assistant answers are multi-line),
  so the single-line span path can't be used for it.
- **App-code workaround (used):** strip the markdown DELIMITERS from the body at
  render time (`` `code` `` -> `code`, `**bold**` -> `bold`) so the noise is gone
  and the text reads cleanly, even though we can't yet style the run. This is a
  transform on the display string only (api::Message untouched). A short inline
  code token could later be split into its own non-wrapping styled widget if it
  fits, but a general wrapping-with-inline-styling body needs vendor support.
- **UPDATE (2026-08-02):** the FENCED code-block case (multi-line ```` ``` ````
  blocks) is now handled app-side WITHOUT this gap — a fenced run doesn't need
  inline-in-paragraph styling, it's its own block, so main_pane_system renders
  each inner line as a full-width sunken monospace row (no wrap needed). Only the
  INLINE-in-a-wrapping-paragraph `code`/`**bold**` case still waits on this gap.
- **Minimal fix (owned elsewhere — vendor/afterhours):** teach the wrapping text
  renderer (`detail::wrap_text_to_width` + the draw loop) to carry per-run color
  through the wrap, so a `spans` label wraps AND colors. Then hanabi can render
  real inline-code pills. NOT done here (vendored; logged for that owner).
- **PROVEN FIX (2026-08-02):** rewrote the buffered span draw path to word-wrap
  runs across label_rect (each word keeps its span color; wraps on the same
  boundaries as the plain wrapper so caller height matches). Applies + builds +
  tests 8/8. Captured as vendor_patches/22-styled-spans-word-wrap.patch (+ tag
  hanabi-fix-gap22) with the turnkey hanabi wiring in vendor_patches/README.md.
  Pointer not bumped (needs maintainer push); hanabi keeps stripped inline code
  until then.



**POSTSCRIPT 2026-08-26 (source-reference audit).** The old stripped-inline-code workaround is no longer the current assistant path; current code emits styled spans. There is also a separate stale source comment saying TextSpan has no weight, contradicted by the later gap postscript.

**Hanabi reference.** Current code: `vendor_patches/22-styled-spans-word-wrap.patch` (`Subject: [PATCH] feat(ui): styled label spans now word-wrap across lines`) — the captured proof patch for wrapping TextSpan labels. `src/ecs/main_pane_system.h` (`Inline markdown -> colored spans`) — current transcript path treats the gap as unblocked. Tests: `tests/ui/find_sees_through_markdown.e2e` (`stores. The transcript shows "6 failures"`) — scripted coverage around rendered markdown text vs stored markdown markers.

---

### #23 — no off-screen child culling / list virtualization for scroll views
- **Gap:** a `ScrollPanel` lays out and renders EVERY child every frame, even
  children scrolled far outside the viewport. There is no "only build/lay-out
  the visible slice" primitive and no way to tell the layout engine a child is
  off-screen and can be skipped. On a long immediate-mode list the per-frame
  cost is O(all children) through the 7-pass recursive layout (autolayout.h,
  with `solve_violations` iterating up to 10× per container).
- **Why wanted (hanabi):** the chat transcript is immediate-mode — it rebuilt
  every message (name + wrapped body + tool rows + folds) every frame. On a
  120-message thread that was **~146ms/frame (~7fps)** — the direct cause of the
  "choppy scroll / low framerate" the user reported. Only ~a viewport-worth of
  messages is ever visible.
- **App-code workaround (used, big win):** the transcript now pre-computes each
  item's height (memoized per message id+width — see #A / transcript_render_cache.h),
  reads last frame's `HasScrollView::scroll_offset.y` + `viewport_size.y` (they
  persist across frames since imm entities are keyed by `mk(parent,id)`), and
  only emits entities for items intersecting `[scroll − ½vp, scroll + vp + ½vp]`.
  Items outside are collapsed into ONE spacer `div` of their summed height, so
  total content height (and thus the scrollbar + scroll math) is preserved
  exactly while their text/measure/child-layout is never built. Result:
  **146ms → ~15.7ms/frame** on the same 120-message thread (a long transcript now
  costs the same as a short one; the residual ~15ms is the app-wide layout floor
  — sidebar + full-tree passes — not the transcript).
- **Minimal fix (owned elsewhere — vendor/afterhours):** a first-class
  virtualization hook on `HasScrollView` — e.g. a callback `(visible_range) →
  build children`, or a `with_virtualized(item_count, item_height_fn,
  build_item_fn)` helper that internally does the spacer trick + only calls the
  builder for visible indices. Would remove the need for every app to hand-roll
  height accounting + spacer divs. Related: a cheaper "skip layout for a subtree
  flagged off-screen" flag would help even non-list cases.

**POSTSCRIPT 2026-08-26 (gap index) — OVERTAKEN.** The primitive this entry
asks for exists in the pinned submodule (428047e):
`afterhours::ui::imm::virtual_list(ctx, mk(parent), count, row_height,
render_row)` (`src/plugins/ui/imm_components.h:159`) builds a leading spacer,
the window, and a trailing spacer, sizing the window from
`HasScrollView::scroll_offset`/`viewport_size` with a 4-row overscan. So "no
list virtualization primitive" is no longer true. What it does NOT do is the
half hanabi actually needs — it divides by ONE `row_height`, so a list of
mixed-height rows cannot use it. The live entry is **#326**; #31 (the stale
offset) is also partly answered by the overscan. Verified by reading the
vendored source, not by running it.

**POSTSCRIPT 2026-08-26 (source-reference audit).** Hanabi still has a mixed-height app-side virtualizer, so the workaround is live. The broad 'no list virtualization primitive' wording is stale because later notes/current docs acknowledge a uniform-row virtual_list exists upstream.

**Hanabi reference.** `src/ecs/main_pane_system.h` (`Pass 2: emit spacers + only the visible items`) — the transcript virtualizer emits spacers for off-window content and only builds visible items. `src/ecs/main_pane_system.h` (`render_bubble(ctx, col`) — visible transcript items are the only ones passed to the body renderer. Measurement/gate: `docs/perf/TRANSCRIPT.md` (`Pass 2 is genuinely virtualized`) — measured current transcript virtualization behavior and remaining costs.



### #24 — wrapped text ignores hard line breaks (`\n` is treated as a word char)
- **Gap:** `detail::wrap_text_to_width` (rendering.h) does greedy word-wrap by
  splitting on SPACES only; a `\n` in the label is treated as an ordinary
  character inside a "word". So a multi-line body (numbered/bulleted list,
  paragraphs) collapses into ONE run-on wrapped paragraph — AND any height the
  caller computed assuming `\n` = line break is wrong, leaving a large empty gap
  (the box is sized for N logical lines but the renderer draws far fewer).
- **Why wanted (hanabi):** assistant answers are lists + paragraphs delimited by
  `\n` / `\n\n`. Rendered as one box they became an unreadable run-on blob AND
  produced a ~100px void between the author name and the visible text (a top UI
  critique). This is distinct from #22 (that's about inline per-run COLOR; this
  is about honoring hard breaks at all).
- **App-code workaround (used):** the assistant body is split on `\n` and each
  segment rendered as its OWN wrapped text box (blank line → a half-pitch gap
  for paragraph spacing); the height model sums the same per-segment wrapped
  heights so it matches the render exactly. Bounded by #23's virtualization so
  the extra per-line boxes only exist for visible turns. Side benefit: real list
  breaks + hanging structure with no vendor change.
  UPDATE (2026-08-02): the expanded tool-output panel (render_tool_block) now
  uses the SAME per-line split — its captured tool_result honors \n breaks +
  indentation instead of collapsing to a run-on blob.
- **Minimal fix (owned elsewhere — vendor/afterhours):** teach
  `wrap_text_to_width` to force a line break on `\n` (and the draw loop + the
  measure API #A to advance a line), so a single wrapping label honors hard
  breaks. Pairs naturally with #22 (per-run color) and #A (a real measure/wrap
  API that reports line count).

**POSTSCRIPT 2026-08-26 (gap index) — RESOLVED UPSTREAM.** The pinned submodule
(428047e) breaks on `\n`. `rendering.h:629` computes
`const bool has_hard_break = text.find('\n') != std::string::npos;` and the
branch below it is commented "Only soft-wrap when asked; otherwise break on
'\n' alone" — so a hard newline always breaks, with or without
`TextOverflow::Wrap`. `ui::wrap_text`'s own doc comment
(`plugins/ui/text_measure.h`) now reads "Honors hard '\n'". The app-side
per-segment split described above is no longer needed for correctness, though
it is still what gives the paragraph spacing. Verified by reading the vendored
source, not by running it.



**POSTSCRIPT 2026-08-26 (source-reference audit).** Current source does not rely on one label swallowing hard breaks; rich body rendering splits on newlines and uses explicit paragraph spacing. The entry's upstream-gap statement is obsolete.

**Hanabi reference.** Current code: `src/ecs/main_pane_system.h` (`size_t nl = shown.find('\n', start);`) — render_rich_body walks hard-newline-delimited segments. `src/ecs/main_pane_system.h` (`blank line = half pitch`) — blank-line paragraph spacing remains an app-side behavior.

---

# WISHLIST — what would make afterhours a joy to build on

Distinct from the numbered gaps above (which are concrete missing primitives / bugs
with app-side workarounds). This section is the aspirational maintainer-facing
feedback Gabe asked for: patterns I had to HAND-ROLL in hanabi that feel like they
belong IN the library, and capabilities I kept wishing existed. Each is grounded in
real hanabi code — if a future app hits the same wall, that's the signal to promote it.

## A. Text metrics are the #1 papercut — promote a real measure/wrap API
- **What I hit:** afterhours wraps + draws text internally (`draw_text_in_rect`,
  `wrap_text_to_width`), but app code has NO access to "how tall will this string be
  at width W and font F?" So I hand-rolled `estimate_height()` with a fudged
  `kGlyphW=6.2px/glyph, kLinePitch=15px` model — which was WRONG (8px/18px first),
  producing boxes ~2x too tall so wrapped text rendered bottom-aligned in empty boxes
  (a ~140px gap above every message; found only by instrumenting with debug bgs).
- **What I wish existed:** `ui::measure_text(text, font, size, max_width) -> {w, h, lines}`
  using the SAME fontstash metrics the renderer uses. Immediate-mode UIs live or die
  on this — every scroll list, chat bubble, card, and tooltip needs to size a text box
  to its content. Right now every app re-derives a bad approximation.
- **Bonus:** a `children()`-that-includes-wrapped-text sizing mode, so a Column can
  size to its wrapped text without the app precomputing pixel heights at all.

## B. Per-frame work has no memo/dirty layer — promote caching hooks
- **What I hit:** immediate mode rebuilds EVERY widget EVERY frame. For a 100+ message
  transcript that's thousands of entities + thousands of text measurements per frame →
  ~15-20ms/frame → ~50-65fps → visibly choppy scroll. I'm now hand-rolling per-message
  height/text-transform caches keyed by message id.
- **What I wish existed:** (1) a retained/memoized sub-tree ("this subtree's inputs
  didn't change, reuse last frame's layout"), or (2) built-in text-measure caching
  keyed by (string,font,size,width), or (3) list VIRTUALIZATION — a scroll container
  that only builds/lays-out children intersecting the viewport. Any one of these turns
  a long-list immediate-mode UI from 60fps to 120fps. Virtualization is the big one;
  every chat/log/feed app needs it and every one will re-invent it.

## C. Alpha compositing is a recurring trap — promote real blended fills
- **What I hit:** `with_custom_background` with a low-alpha color renders OPAQUE (the
  sgl default pipeline has blending off — gaps #13/#15). I built `theme::over(fg,bg)`
  to pre-composite every tint (chips, hover washes, tool-row tints, skeleton bars) by
  hand. EVERY translucent surface in the app goes through this workaround.
- **What I wish existed:** a rect/texture fill path with real alpha blending (a
  blend-enabled pipeline as the default, or a `with_blended_background`). Translucency
  is table stakes for modern UI (hover states, scrims, tinted chips, glass) — having to
  manually pre-composite over the KNOWN backdrop is fragile (breaks the moment the
  backdrop isn't known, e.g. over an image or a gradient).

## D. Styled text that wraps — promote it (blocks real markdown)
- **What I hit:** `with_styled_label` (multi-color TextSpans) exists but renders
  single-line only; it doesn't word-wrap (gap #22). So I can't render an inline `code`
  pill inside a wrapping paragraph — I strip the markdown delimiters instead. This is
  THE thing keeping hanabi's chat from looking like a real chat app (a UI critic ranked
  "no inline code styling / no real lists" in the top 5 defects).
- **What I wish existed:** wrapping + per-run styling together (color, weight, bg pill,
  maybe a font swap to mono for code). Ideally a tiny inline-markup renderer
  (`code`, **bold**, *italic*, lists) as a first-class label mode — every text-heavy
  app wants it and it's painful to fake.

## E. Animation/transition primitives — promote a tween + disclosure kit
- **What I hit:** no property tween (#2), no per-item stagger (#8), no exit animation
  (#9), no one-shot state-change trigger (#10), no shimmer/gradient-mask (#11), no
  spring-to-slot drag (#12). I hand-rolled a smoothstep sidebar-width ease and a static
  (un-animated) skeleton because there's no shimmer. Collapsible rows (tool piles,
  sub-agents, folds) pop instantly with no ease.
- **What I wish existed:** a small animation core — `animate(value, target, duration,
  easing)` that survives immediate-mode re-creation (keyed by widget id), plus canned
  "disclosure" (height 0↔auto ease) and "shimmer" helpers. Disclosure + shimmer alone
  would cover 90% of what a polished app needs and every app re-invents them.

## F. OS integration seams — promote a platform shim
- **What I hit:** no OS appearance query (#1/#16 — I read AppleInterfaceStyle via my own
  .mm), no natural-scroll-direction query (had to read com.apple.swipescrolldirection
  in Obj-C++), menu-bar/NSStatusItem is all app-side .mm (#5), no way to open a URL /
  activate the app / know DPI without hand-written extern "C" AppKit glue.
- **What I wish existed:** a thin `afterhours::platform` layer: `appearance()`,
  `natural_scroll()`, `open_url()`, `activate_app()`, `content_scale()/dpi`, and a
  menu-bar/tray abstraction. These are the same 6 functions every desktop app writes;
  they belong in the backend, not copy-pasted per app.

## G. Headless/testing affordances — promote them (I built a pile)
- **What I hit:** to screenshot + test hanabi I hand-built: a `--screenshot` headless
  render path, a wait-for-async-load loop (gap #21), input injection for tests
  (mouse/wheel/keys), and a pile of `HANABI_*_DEMO` env hooks to force UI states
  (skeleton, streaming, auth, a specific view) for headless capture.
- **What I wish existed:** first-class headless render-to-PNG, a deterministic
  "render N frames then capture" test harness, and a documented input-injection API
  for UI tests. afterhours HAS `testing::input_injector` but it's under-documented and I
  found it by grepping. A blessed "UI snapshot test" recipe would save every app this.
- **Bonus (gap #6):** headless capture can't supersample (hi-DPI) — a 2x offscreen
  target just lays out larger, doesn't sharpen. A real hi-DPI headless mode would make
  screenshot-based review crisp.

## H. Small quality-of-life wins
- **flex-grow / space-between that actually pins a trailing element** (#18) — I reserve
  a fixed count-column width to right-align counts because there's no grow. A real
  flex-grow (or `margin-left:auto`) would kill a whole class of geometry hacks.
- **A monospace font tier** in the theme's FontSizing (only proportional tiers exist) —
  code/commands/logs all want mono and I have to pass the font by name each time.
- **Icon/atlas helpers**: I built the Lucide spritesheet + `draw_at`/`draw_fg` blit +
  a gen script. A tiny "sprite atlas" helper (load a packed PNG + name→rect map + a
  tinted blit) would be reusable by any app; mine is 90% generic.
- **Scroll-to / scrollIntoView**: I poke `HasScrollView::scroll_offset = 1e9` +
  clamp_scroll() to stick-to-bottom while streaming. A `scroll_to(entity)` /
  `scroll_to_bottom()` API would be cleaner and less magic-number-y.
- **A "hot"/hover state I can READ in app code** for arbitrary widgets, to drive
  hover-only affordances (stars, message actions) without threading HasClickListener
  everywhere.

---

### #25 — sokol `draw_rectangle_rounded` emits a DEGENERATE triangle for mixed round/sharp corners (visible glitch)
- **Gap/bug:** `vendor/afterhours/src/backends/sokol/drawing_helpers.h`
  `draw_rectangle_rounded(...)`'s `emit_corner_arc` lambda has a broken
  sharp-corner branch:
  ```cpp
  if (radius <= 0.0f) {           // sharp corner
    sgl_begin_triangles();
    sgl_v2f(cx, cy);
    sgl_v2f(cornerX, cornerY);    // only TWO vertices — a triangle needs THREE
    sgl_end();                    // degenerate/garbage primitive
    return;
  }
  ```
  Any **mixed** corner config (e.g. `RoundedCorners().all_sharp().top_round()`
  — rounded top, sharp bottom) drives the two bottom corners through this path,
  emitting malformed triangles. On Metal this renders as a **diagonal "triangle"
  cut** across the shape (looks like the fill is sliced).
- **Where it bit hanabi:** the chat TAB buttons used `.all_sharp().top_round()`
  (rounded tops, square bottoms — the classic tab shape). Inactive tabs showed a
  diagonal triangle notch on a bottom corner. (Reported by Gabe with a screenshot,
  2026-08-02.)
- **Why the all-rounded / all-sharp cases are fine:** `all_round()` → every
  radius > 0, the buggy branch never runs. `all_sharp()` (or roundness 0) →
  the function early-returns to a plain `draw_rectangle`. Only the MIXED case
  hits the degenerate triangle.
- **App-side workaround (used):** stop using mixed corners on the affected
  widgets — round ALL FOUR corners (`all_round()`) with a small roundness so no
  radius is 0. The bottom corners sit against the content bridge, so a slight
  round there is imperceptible; the tab still reads as rounded-topped.
- **PROVEN FIX (2026-08-02):** applied `return;` in the sharp-corner branch,
  rebuilt, and confirmed hanabi tabs render clean rounded-top/square-bottom
  (top_round()) with NO diagonal glitch. Captured as
  vendor_patches/25-rounded-corner-degenerate-triangle.patch (+ submodule tag
  hanabi-fix-gap25). Not yet on the afterhours remote / pointer not bumped
  (needs the maintainer's push); hanabi keeps the all_round() workaround until
  then.
- **Minimal upstream fix (vendor, off-limits here):** in the sharp-corner branch
  of `emit_corner_arc`, don't emit a 2-vertex triangle — either skip the arc
  entirely (the straight edges already cover a square corner via the center
  triangle-fan) or emit a proper degenerate-free corner (3 coincident/þidentical
  verts, or just `return;` without any `sgl_*`). A one-liner: replace the whole
  `radius <= 0` block with `return;`.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The app is no longer keeping the all_round workaround described by the entry; current tab colors use a mixed-corner config, so the behavior has been corrected/superseded.

**Hanabi reference.** Current code: `vendor_patches/25-rounded-corner-degenerate-triangle.patch` (`Fix: return early`) — the captured proof patch deletes the degenerate sharp-corner primitive. `vendor_patches/README.md` (`app-side all_round() workaround was removed`) — patch README says the workaround was removed after the fix.



### #26 — `HasScrollView` has no built-in scrollbar / scroll-indicator render
- **Gap:** afterhours' `HasScrollView` (`vendor/afterhours/src/plugins/ui/components.h`)
  fully TRACKS scroll state — `scroll_offset` (Vector2, `.y` grows downward),
  `content_size` (total scrollable content, `.y`), and `viewport_size` (the
  visible window, `.y`) — and clamps/updates them every frame. But it NEVER
  renders a visible bar. There is no `draw_rectangle`/primitive anywhere in the
  scroll-view render path for a track or thumb, and no config flag to opt into
  one. So a scroll panel gives the user **no indication of scroll position, of
  how much content there is, or even that more content exists below the fold** —
  the content just silently clips at the viewport edge.
- **Why wanted:** hanabi's transcript, sidebar folder/thread list, home, and
  digest are all `preset::ScrollPanel()`s. With no bar, a long thread looks like
  it simply ends at the last visible line; the user can't tell there's more to
  scroll to, and has no handle on where they are in the content. Standard
  desktop affordance (macOS overlay scrollbar) is table-stakes for a scrolling
  region.
- **App-code workaround (used — `src/ui/scrollbar.h`, TEMPORARY):** paint a thin,
  muted, macOS-overlay-style indicator ourselves. `attach_scroll_indicator(ent)`
  hangs a `HasOnDraw.fg` custom-draw on the scroll entity; the fg fires with the
  panel's on-screen viewport rect (the scroll entity's own `rect()` is the
  fixed viewport, not offset), and reads the LIVE `HasScrollView` metrics off the
  entity by id at draw time (freshest, post-layout — same access pattern the
  transcript virtualization already uses). From those three numbers it computes a
  track (= viewport height) and a rounded thumb:
  `thumbH = max(minThumb, trackH * viewport/content)`,
  `thumbY = trackY + (offset/(content-viewport)) * (trackH-thumbH)`,
  pinned to the inside of the viewport's right edge. Callers reserve a few px of
  right padding so text never runs under the bar. It **auto-hides** when
  `content_size.y <= viewport_size.y` (nothing to scroll). Translucency goes
  through `theme::over()` because the fill pipeline can't alpha-blend (gaps
  #13/#15) — a raw low-alpha color would render as a harsh opaque block. The
  thumb is now **DRAGGABLE app-side** (2026-08-02): `scroll_indicator`'s
  `on_draw_fg` hit-tests the thumb rect against `input::get_mouse_position()` /
  `input::is_mouse_button_down(0)` (edge-detected per scroll-id in a function
  static `unordered_map<EntityID, DragState>`), and on drag maps the cursor's
  track-space y back into `scroll_offset.y` via
  `frac = clamp((mouseY - grabDY - trackY) / (trackH - thumbH), 0, 1)`,
  `scroll_offset.y = frac * (content - viewport)`, then `clamp_scroll()`. It
  writes the offset during the render pass (takes effect next frame, same as the
  jump-to-bottom pattern) and only ever mutates while THIS bar is actively
  dragging (`s_activeDrag` global — one bar at a time), so it never fights the
  wheel. Clicking the track above/below the thumb pages one viewport toward the
  click. The thumb brightens (alpha 150 → 220) while hovered/dragging. It still
  accurately reflects position + content ratio and updates every frame; still
  **auto-hides** when content fits.
- **Minimal upstream fix (vendor, off-limits here):** give `HasScrollView` an
  optional built-in scrollbar render — a config flag (e.g. `with_scrollbar()`)
  that, when the content overflows, draws a track + thumb from the metrics it
  already computes, in the scroll view's own render pass. Ideally the thumb is
  **draggable** (hit-test the thumb rect, map drag delta back into
  `scroll_offset`) and auto-hides/fades like a native overlay scrollbar. Because
  the component already owns all three metrics, this is a pure render + one
  input-handler addition — no new state.

**POSTSCRIPT 2026-08-26 (gap index) — RESOLVED UPSTREAM.** All of it landed.
`HasScrollView` (`plugins/ui/components.h:588`) carries `show_scrollbar`
(default **true**), `scrollbar_thickness` and `scrollbar_min_thumb`;
`scrollbar_geometry()` (`components.h:630`) returns the track and thumb rects
and returns nothing when `!show_scrollbar || !needed`, i.e. it auto-hides when
the content fits; and `HandleScrollbarDrag` (`plugins/ui/systems.h:1903-1952`)
maps a drag on the thumb back into `scroll_offset`. The draggable bar this
entry asks for is the shipped behaviour. What is left is the *macOS overlay*
behaviour — nothing at rest, a bar while scrolling — which is **#94**, written
against these fields. Verified by reading the vendored source, not by running
it.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's requested draggable built-in scrollbar is now shipped upstream; remaining work is overlay auto-hide styling, not absence of a scrollbar.

**Hanabi reference.** Current code: `src/ecs/sidebar_system.h` (`show_scrollbar =`) — current code drives the built-in HasScrollView scrollbar visibility. `src/ecs/sidebar_system.h` (`(scrollbar now drawn by afterhours)`) — the old temporary app-side scroll indicator is gone.



### #27 — immediate-mode UI clears + rebuilds the whole tree every frame; no retained-layout / dirty-skip primitive (the T7 idle-frame floor)
- **Gap:** the UI is fully immediate-mode. Every frame `BeginUIContextManager`
  runs `ClearVisibity` + `ClearUIComponentChildren` over all `UIComponent`s,
  then our systems (sidebar/main-pane/tab-bar) re-emit the ENTIRE component tree
  via `mk()`/`div()`/`button()`, then `AutoLayout::autolayout` re-solves the
  whole tree, then the render pass draws it. There is no way to tell afterhours
  "nothing changed this frame — keep last frame's laid-out tree and just
  redraw" (or better, re-present the last framebuffer). `mk()` retains ENTITIES
  by UUID (good — no per-frame alloc churn), but their children/visibility are
  cleared and layout is re-solved unconditionally, so an app-side "skip our
  update tick when idle" leaves the tree empty and renders nothing. The
  clear+rebuild is baked into the vendored UI systems, off-limits to edit.
- **Measured (2026-08-02, live on aspen, `HANABI_FRAME_SPLIT=1`, 120 frames):**
  - idle Home:       **8.71ms/frame** = update(tick) **3.35ms** + render(layout+draw) **5.36ms**
  - big transcript:  **11.59ms/frame** = update **3.54ms** + render(layout+draw) **8.04ms**
  The update half (our systems re-emitting) is a flat ~3.4ms of pure idle waste;
  the render half is the bigger cost and scales with visible content (already
  cut by the virtualization gap #23 + the transcript_render_cache memoization).
- **Why it matters:** this ~8.6ms idle floor is the app's steady-state per-frame
  cost even when the user is doing nothing — it's the headline "T7" perf item.
  Everything app-side that's cheap to fix (string/measure memoization, off-screen
  culling) is already done; the remaining win requires the framework.
- **CONFIRMED app-side ceiling (2026-08-02):** the sokol frame driver is
  vsync-locked — `sapp_desc.swap_interval` is never set (defaults to 1) in the
  vendored `metal_run` (backend.h ~719), so the app renders at the display
  refresh (~111-120Hz) and there is NO app-side FPS cap (the Metal
  `set_target_fps` is a documented no-op). And because
  `BeginUIContextManager` CLEARS the tree before our systems run, an app-side
  'skip our update tick when idle' renders an EMPTY tree — not viable. So both
  halves (the ~3.8ms update re-emit and the ~6.2ms vsync'd autolayout+draw) are
  gated behind vendored code. This is genuinely NOT fixable in app code without
  editing vendor; leaving it logged rather than hacking a half-measure.
- **Minimal upstream fix (vendor, off-limits here):** a retained/dirty-frame
  mode. Two shapes, either works: (a) a global `ui_dirty` flag the app sets on
  any state change (input, stream tick, load, resize); when clear, the UI
  systems skip the clear+re-emit+autolayout and the backend re-presents the last
  frame — turning idle frames into a swap only. (b) per-subtree layout caching:
  autolayout memoizes a subtree's solved rects keyed by its config hash + size,
  and only re-solves subtrees whose inputs changed. (a) is the bigger win for an
  idle desktop app (idle → ~0ms of our work); (b) helps the mixed case. Neither
  is expressible in app code because the clear+rebuild lives in the vendored
  `BeginUIContextManager`/autolayout pass.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The architectural limitation may remain, but the entry's headline 8.7/11.6 ms floor is stale; current docs attribute most of the old number to building without -O.

**Hanabi reference.** Hanabi-owned performance finding: `src/main.cpp::HANABI_FRAME_SPLIT` — frame split instrumentation still exists for measuring update vs render costs. Measurement/gate: `tests/README.md` (`Home digest, idle`) — current headless frame measurements are ~0.95/1.14/1.64 ms, not the old 8-12 ms floor.



### #28 — No OS window-focus / frontmost-app query or focus-gated global hotkey support
afterhours has no notion of OS-level application activation state (is this app
frontmost?) and no focus-gated global-hotkey primitive. The framework's input
layer only sees key events routed to the window while it's key; it cannot
express "register a system-wide chord ONLY while my app is frontmost, and
release it when I lose focus." This matters because a Carbon `RegisterEventHotKey`
registration is process-wide and CONSUMES the chord even when the app is in the
background — so a global hotkey silently steals that key combination from every
other app (bug: Cmd+Shift+N stole Chrome's "New Incognito Window").

Worked around entirely in app code (`src/native_extras.mm`): hand-rolled an
`NSApplication` active-state observer (`HotkeyFocusObserver`) on
`NSApplicationDidBecomeActiveNotification` / `NSApplicationWillResignActiveNotification`
that REGISTERS the Carbon hotkey when hanabi becomes frontmost and UNREGISTERS
it when hanabi resigns active. When unregistered, the chord flows through to the
frontmost app normally. If afterhours grew a cross-platform "app-focus changed"
signal (or a focus-gated hotkey binding), this NSApp-notification plumbing could
move behind the framework seam instead of living in the .mm.

**Hanabi reference.** `src/native_extras.mm` (`FOCUS GATING`) — native app focus is observed to gate global hotkey registration. `src/native_extras.mm::HotkeyFocusObserver` — NSApplication active/resign notifications register and unregister the Carbon hotkey.



### #29 — Single hot-entity: a hoverable child steals the parent row's hover fill
afterhours tracks hover as ONE global `hot_id` (context.h): `HandleClicks`
(systems.h) sets hot on the deepest element under the mouse, so a child button
that overlaps its parent's rect takes `hot_id` away from the parent while the
cursor is over the child. The renderer (rendering.h) paints a widget's hover
wash only while it is `is_hot`, and there is (a) no way to ask "is the mouse
anywhere in this subtree?" cheaply at emit time (the tree hit-test helper is
internal + scroll-offset-aware, not exposed), and (b) no hit-test-exclusion
flag (`with_skip_tabbing` only affects focus/tab order, not hot). So a row with
a trailing hover-affordance (the sidebar thread row's star toggle) FLICKERS its
whole-row hover fill on/off as the pointer crosses from the row body onto the
star — the row loses `is_hot` for exactly the frames the star owns it.

Worked around entirely in app code (`src/ecs/sidebar_system.h`): the row no
longer relies on the framework's per-frame `is_hot` wash. It BAKES the hover
wash into the row's BASE `HasColor` whenever the pointer is in the row body OR
on its star child, and sets `hover_bg` to the same value, so the row paints the
identical fill regardless of which of {row, star} currently owns `hot_id` — no
flash. Detecting star-hover needs the child's id before the child is emitted, so
the row caches the star's (stable across frames) entity id per session in a
static map and ORs `is_hot(star)||was_hot(star)` into the row hover signal. The
star also gets `skip_hover_override=true` so its own (transparent) fill never
tints. If afterhours grew either a `mouse_in_subtree(id)` query or a
"child-hover propagates to parent hot" option (or a hit-test-ignore flag), this
per-row id cache + base-color baking could collapse to a single call.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The old per-row star id cache described in the entry is gone; current code uses mouse_in_subtree/mouse_was_in_subtree.

**Hanabi reference.** Current code: `src/ecs/sidebar_system.h` (`Subtree, not the row entity alone`) — row hover now reads subtree hover rather than caching the star child id. `src/ecs/sidebar_system.h::ctx.mouse_was_in_subtree(row.ent().id)` — current row-hover test composes with hoverable children.



### #30 — No scroll-anchor / preserve-position-on-prepend for scroll views
- **Gap:** `HasScrollView` tracks `scroll_offset` in absolute px from the top.
  When content is inserted ABOVE the current viewport (loading older messages
  at the top of a transcript), the total content grows but the offset is
  unchanged, so the SAME px-from-top now shows OLDER content — the view snaps
  upward to the newly-loaded oldest message. There's no "anchor to a child /
  hold the viewport on the currently-visible item across a content-size change"
  primitive (what web browsers call scroll anchoring).
- **Why wanted (hanabi):** "load older" prepends a page of older messages; the
  user expects their current position to stay put (new content appears above),
  not to be yanked to the top.
- **App-code workaround (used):** the loader records the message COUNT before a
  load-older fetch; on the frame the larger content lays out, the transcript
  render (main_pane_system.h) measures the total height of the newly-prepended
  items and bumps `scroll_offset.y` by exactly that delta, once, then clears the
  pending anchor — holding the viewport on the same message. Works because the
  app already measures every item's height for virtualization (#23).
- **Minimal upstream fix:** an opt-in "anchor" mode on `HasScrollView` — remember
  the top-most visible child before layout and re-derive `scroll_offset` after,
  so a content-size change above the fold keeps the visible item stable.

**Hanabi reference.** `src/ecs/components.h` (`anchorPending is the session id awaiting the offset bump`) — pane state carries the pending scroll-anchor adjustment. `src/ecs/main_pane_system.h` (`sv.scroll_offset.y += prependedH`) — render applies the measured prepended height to preserve viewport position.



### #31 — Virtualization window must be built from a STALE scroll offset (no next-offset / velocity hint)
- **Gap:** an app doing its own list virtualization (#23) reads
  `HasScrollView.scroll_offset` which is LAST frame's value (the current frame's
  layout hasn't run yet), and builds the visible window from it. On a fast
  fling the offset moves many px between frames, so a window built with a small
  margin around the stale offset doesn't cover where the content actually is
  this frame → the list goes BLANK until the scroll settles and the window
  catches up. There's no framework signal for "the offset the scroll view will
  settle at" or the current scroll velocity.
- **Why wanted (hanabi):** fast-scrolling a long transcript blanked out the
  messages (components stopped rendering, popped back on stop) — a jarring
  "not a real app" tell.
- **App-code workaround (used):** the transcript tracks its own per-frame scroll
  delta (velocity) per session and extends the virtualization margin
  generously in the travel direction (base ~1 viewport + up to ~4 viewports of
  velocity-scaled lookahead), so the built window covers where a fast fling is
  heading. Idle cost is unchanged (velocity 0 => no extension).
- **Minimal upstream fix:** expose the scroll view's velocity (or a predicted
  next offset), or — better — provide the virtualization itself (see #23) so
  apps don't re-derive a velocity-aware window by hand.

**Hanabi reference.** `src/ecs/pane_state.h` (`Virtualiser scroll velocity`) — per-pane state tracks last scroll position for virtualization overscan. `src/ecs/main_pane_system.h` (`velocity-aware extension in the direction of travel`) — transcript virtualization expands the window according to scroll velocity. Measurement/gate: `docs/perf/TRANSCRIPT.md` (`velocity-aware extension in the direction of travel`) — current perf doc explains the scrolling cost and blank-gap prevention tradeoff.



### #28 — a 2nd column child of a custom-background div doesn't render; on_draw_fg on a bg div doesn't fire (hanabi-observed)
- **Gap:** In the transcript, the user bubble is a `div` with `.with_custom_background(...)` + FlexDirection::Column. Adding a SECOND child after the first (a text label) — verified with a bright test label — does NOT render the 2nd child. Separately, attaching `.with_on_draw_fg(...)` to the bubble div itself (which has a custom background) never fires the callback (verified with a bright filled-rect probe — nothing drew). Tool-row checks DO draw via on_draw_fg, but they're transparent-bg children inside a Row, not children of a custom-bg Column.
- **Impact:** Blocked the "WhatsApp-style sync check as a nested badge under the bubble" approach. Worked around by appending a font-safe text suffix to the bubble's single body label ("· sent" etc.). A proper trailing badge/glyph row inside a filled bubble needs either (a) on_draw_fg to fire on bg divs, or (b) multi-child layout to render for custom-bg columns.
- **Repro:** add a 2nd `div(mk(bub.ent(), 9), ...label...)` after the user_text label; it doesn't appear. Add on_draw_fg to `bub`; it doesn't fire.
- **Also:** unicode U+2713 (✓) renders BLANK in the bundled font (JetBrainsMono/Roboto) at MICRO size — a check glyph must come from the icon atlas (draw_at "star"-style) or a drawn shape, not a text label.

**POSTSCRIPT 2026-08-26 (source-reference audit).** Resolved; source contains old dead/duplicated comments around the interim text suffix, but current code renders sync_check as a nested child.

**Hanabi reference.** Current code: `src/ecs/main_pane_system.h` (`gap #28 (nested child of a custom-bg bubble`) — current user bubble comments say the upstream bump fixed nested child/on_draw_fg rendering. `src/ecs/main_pane_system.h::draw_sync_check(st` — the real sync glyph is rendered as a nested on_draw_fg child.



### #29 — text_input has no placeholder support
- **Gap:** `afterhours::ui::imm::text_input` renders an empty field with no placeholder/ghost-text affordance when the bound string is empty. The hanabi composer + sidebar search both show a blank box with no "Reply…" / "Search…" hint. A `with_placeholder("…")` (rendered as faint text, cleared on first keystroke, not part of the value) would fix it cleanly.
- **Impact:** minor UX — the composer gives no cue what to type. Worked around by adjacent labels elsewhere; a real placeholder needs input support.
- **Related:** gap #17 (text_input forces its own Secondary bg + derives font size from height, ignoring per-widget colors) — same widget, same "input is hard to style" family.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The text_input claim is obsolete. The composer still draws its own placeholder only because it moved to text_area, which lacks that feature.

**Hanabi reference.** Current code: `src/ecs/sidebar_system.h` (`.with_placeholder("Search")`) — current sidebar search uses text_input placeholder support. `src/ecs/main_pane_system.h` (`text_input renders a placeholder itself`) — composer comments confirm text_input placeholder exists, while text_area still needs overlay text.



### #30 — scroll is a raw wheel-delta add (no smoothing/momentum) → feels janky vs native macOS
- **Gap:** `HandleScrollInput` does `scroll_offset += direction * wheel * speed` — the rendered offset jumps by the raw wheel delta each event. There's no eased/target-based scrolling, so on macOS (where native scroll has momentum + sub-pixel smoothing) hanabi's scroll feels stepped/janky even at 100+fps. This is the "scroll perf" complaint — it's smoothness, not framerate (frame cost on a 120-msg transcript is only ~5.8ms).
- **Fix (PROVEN, vendor_patches/30-smooth-eased-scrolling.patch):** add `scroll_target` (wheel writes here) + `scroll_smoothing` factor; `scroll_offset` eases toward `scroll_target` once per frame (before the mouse-inside early return so an in-flight glide keeps animating). `scroll_smoothing >= 1` = instant (byte-identical legacy default); 0.28 = smooth glide that settles exactly (0.5px snap). clamp_scroll clamps both. Ease math unit-verified (first step 28%, settles ~21 frames ≈ 0.2-0.35s).
- **hanabi-side (committed, SFINAE-guarded):** `apply_scroll_prefs` sets `scroll_smoothing=0.28` (env `HANABI_SCROLL_SMOOTH` overrides); programmatic offset writes (jump-to-bottom, scrollbar drag/page) sync `scroll_target`. All guarded via `hanabi::has_smooth_scroll<>` detection so hanabi compiles against BOTH pinned edfe234 (no-op) and the patched afterhours (active) — verified both directions build 0 + test 8/8. Activates automatically when Gabe lands the patch + bumps the pointer.

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): "unit-verified" is the wrong
word and there is no test.** No test file in the repo names `scroll_smoothing`
-- `git grep -l scroll_smoothing` reaches this file, `src/util/scroll_prefs.h`
and the patch, and nothing under `tests/`. Nor could there be one that runs the
ease: the eased code is in `vendor_patches/30-smooth-eased-scrolling.patch`,
which `vendor_patches/README.md` still lists as ready-to-apply against the
pinned base, so it is not compiled into anything `make test` builds. "First step
28%, settles ~21 frames" is arithmetic on `0.28`, done on paper and correct as
arithmetic; it is not a measurement of a running binary. `has_smooth_scroll`
has since left `src/` entirely, so even the SFINAE half is gone.



**POSTSCRIPT 2026-08-26 (source-reference audit).** Raw wheel-delta scrolling is no longer current: HasScrollView has scroll_target/scroll_smoothing in the pinned API and hanabi writes it directly. The entry's no-test/SFINAE statements are stale.

**Hanabi reference.** Current code: `src/util/scroll_prefs.h` (`set_scroll_smoothing(sv, smooth)`) — current app writes scroll_smoothing directly; SFINAE/no-op path is gone. `src/ecs/follow_latch.h` (`afterhours' wheel handler`) — current code assumes upstream scroll_target/eased scrolling behavior. Tests: `tests/ui/wheel_scrolls_the_transcript.e2e` (`The wheel moved a transcript exactly zero pixels`) — scripted regression covers wheel/eased-scroll/follow-latch interaction.

---
## Reusable app-scaffolding gaps (survey 2026-08-03) — what a NATIVE DESKTOP app needs that afterhours doesn't provide

afterhours is a game/UI framework; hanabi is the first *native macOS desktop app* on it, so it had to hand-roll all the OS-integration + packaging scaffolding below. These are candidates to upstream so the next desktop app doesn't re-implement them. Grouped by whether afterhours is MISSING it or HAS-but-unusable.

### #31 — MISSING: macOS `.app` bundle packaging (Info.plist, Resources, URL schemes)
- afterhours has zero bundling support. hanabi's `makefile` hand-writes the whole `.app`: `Contents/MacOS/<exe>`, `rsync` of `Contents/Resources/`, and a heredoc'd `Info.plist` (CFBundle* keys, LSMinimumSystemVersion, NSHighResolutionCapable, LSApplicationCategoryType, **CFBundleURLTypes** for the `hanabi://` scheme).
- Upstream shape: a reusable `bundle.mk` include or a `tools/mk_bundle.sh <exe> <name> <id> <plist-extras>` that any afterhours desktop app can call. Also a Linux `.desktop` + Windows resource equivalent for cross-platform.

**Hanabi reference.** `src/native_extras.mm` (`We now SHIP a .app bundle`) — allowed source confirms the app now expects a real Hanabi.app bundle for Spotlight/deep links. `src/native_extras.mm` (`Info.plist (CFBundleURLTypes -> hanabi)`) — native URL handling depends on the bundle declaring the scheme.



### #32 — HAS-BUT-UNUSABLE: `files::get_resource_path` resolves relative to CWD, not the executable/bundle
- `ProvidesResourcePaths` sets `resource_folder_path = fs::current_path() / root_folder` (files.cpp ~54). A launched `.app` has CWD `/`, so bundled resources under `Contents/Resources/` are never found — the app can only find resources when run from its build dir. This makes the resource API unusable for the exact case (a shipped bundle) it's most needed for.
- hanabi workaround (src/preload.cpp `get_exe_dir()` + `resolve_resource_root()`): platform-specific exe-path lookup (`_NSGetExecutablePath` / `/proc/self/exe` / `GetModuleFileNameA`) then probe `<exe>/resources`, then `<exe>/../Resources` (.app), then CWD fallback — and pass THAT to `files::init`.
- Upstream fix: `files::init` should resolve the resource root from the executable path (with a CWD/dev fallback), not raw CWD. The exe-dir helper is trivially generic and belongs in the files plugin.

**Hanabi reference.** `src/preload.cpp` (`std::filesystem::path get_exe_dir()`) — app-owned executable-directory resolver. `src/preload.cpp` (`std::string resolve_resource_root()`) — resource root probes executable resources, .app Resources, then CWD fallback.



### #33 — MISSING: native menu-bar extra (NSStatusItem), notifications, global hotkey, Spotlight
- afterhours has no menu-bar / tray, no native notifications, no global-hotkey registration, no Spotlight/indexing. hanabi implemented all of these in `src/native_extras.mm` (NSStatusItem status item + menu; UNUserNotification/NSUserNotification click-to-open; Carbon RegisterEventHotKey Cmd+Shift+N; CoreSpotlight donation). These are generic desktop-app needs — a thin `afterhours/desktop` (mac) shim (menubar item, post-notification, register-hotkey, on-activate callback) would be broadly reusable.

**Hanabi reference.** `src/menubar.mm` (`NSStatusItem* g_status_item`) — menu-bar status item implementation. `src/native_extras.mm::RegisterEventHotKey` — global hotkeys are implemented in native Obj-C++.



### #34 — MISSING: URL-scheme / deep-link handling (Apple-event kInternetEventClass/kAEGetURL)
- A non-App-Store bundle receives `myapp://...` opens via the classic Apple-event route (`kInternetEventClass`/`kAEGetURL` on the shared NSAppleEventManager). hanabi hand-registers a handler (`native_extras.mm handleGetURLEvent`) and drains it into the app via a pending-open queue. Generic: afterhours could expose `desktop::on_open_url(cb)` + auto-register the handler when the bundle declares CFBundleURLTypes.

**Hanabi reference.** `src/native_extras.mm::handleGetURLEvent` — Apple-event URL handler parses hanabi://thread links. `src/main.cpp::native_take_open_thread` — frame loop drains pending deep links into requestOpenTab.



### #35 — MISSING: runtime font swap is possible but there's no "list installed system fonts" / font-picker primitive
- FontManager.load_font(name, path) DOES allow swapping a named font at runtime (good — hanabi uses it for a font-choice pref). But there's no way to ENUMERATE available system fonts (macOS CTFontManagerCopyAvailableFontFamilyNames / fontconfig on Linux), so an app can't offer "pick any system font" without its own platform code. A `fonts::list_system_families()` would enable a real font picker. (hanabi ships a bundled-font CHOICE instead — Roboto default + Atkinson Hyperlegible, both OFL/Apache, no enumeration needed.)

**Hanabi reference.** `src/preload.cpp` (`fontMgr.load_font("hyperlegible"`) — bundled alternate font is loaded under a stable name. `src/ecs/settings_system.h::render_font_row` — settings exposes Standard/Hyperlegible rather than enumerating system fonts.



### #36 — MISSING: config/save path is fine, but no "app data/cache dir" distinct from config
- `files::get_config_path()` (per-app config dir) works and hanabi uses it. But there's no separate get_cache_path() (XDG cache / ~/Library/Caches) — hanabi puts its transcript disk-cache under the config dir. Minor; a cache-vs-config split is the platform-correct convention.

**Hanabi reference.** None — no app-side workaround is implemented.



## gap #30 update (2026-08-03): hanabi-side eased-scroll workaround shipped
The vendor smooth-scroll patch (#30, scroll_target/scroll_smoothing fields) is captured in
vendor_patches/ but NOT landed in the pinned submodule, so HandleScrollInput still writes the
wheel delta straight into scroll_offset (stepped/chunky). Rather than wait on the vendor merge,
hanabi now eases scrolling ENTIRELY in hanabi-owned state via ecs::ScrollEaseSystem
(src/ecs/scroll_ease_system.h), a render-phase system that runs before the UI render systems:
it records the wheel destination and glides scroll_offset toward it each frame. It is
has_smooth_scroll-guarded so it auto-DISABLES the day the vendor patch lands (vendor easing wins),
and HANABI_SCROLL_SMOOTH=1 forces legacy instant. Pins (follow-latch to end / jump-to-top) are
detected as snaps so stay-at-bottom is never slowed. No vendor edit; pure hanabi workaround.

## gap #22 follow-up (2026-08-03): styled spans are COLOR-only (no per-run weight)
The wrap-aware styled-label primitive (upstream fbb6aef/1e95cd1) that unblocked inline markdown
carries per-run COLOR only (TextSpan{text,color}) — there is no per-run font/weight/slant. So
hanabi renders **bold**/_italic_ as a brighter COLOR and `code` in an accent tint, not as heavier
or oblique glyphs. Fine for now (color conveys emphasis + it word-wraps), but true bold/italic
would need a per-run font field on TextSpan (e.g. {text,color,font_name}) + the styled draw path
picking the run's font. Logged as a wishlist item; NOT patched (vendored afterhours is read-only).

**POSTSCRIPT 2026-08-26 (gap index) — WRONG at the pinned submodule (428047e).**
`TextSpan` is not `{text, color}`. It is
`{std::string text; Color color; colors::FontWeight weight = Regular;}`
(`plugins/ui/ui_core_components.h:457-461`), and the struct's own comment says
"`weight` resolves against the FontManager as base + \"@bold\" etc., falling
back to the base face when that variant was never loaded — so a run asking for
Bold in an app that registered no bold font renders regular rather than
nothing." So per-run WEIGHT is first-class and has been for some time; what is
still absent is per-run FONT/slant. The reason hanabi's bold renders as a
brighter colour is not the library — it is that hanabi bundles no bold face
(**#77**), which is a resource decision, and the fallback above makes asking for
one harmless. An app that loads `DEFAULT_FONT@bold` gets real bold runs today.
Verified by reading the vendored source, not by running it.

## gap #28 RESOLVED (2026-08-03): nested custom-bg child + on_draw_fg now fires
After the afterhours bump (edfe234 -> f24508a), a 2nd child of the custom-bg user bubble WITH
on_draw_fg now renders (verified with a probe box, then the real glyph). This unblocked the true
WhatsApp-style sync indicator: the user bubble now shows a real ✓/✓✓ corner glyph via
draw_sync_check (LocalOnly single gray ✓ / Persisting gray dot / Synced accent ✓✓ / Failed amber
✓+dot) INSTEAD of the interim body-text suffix ("· sent"). draw_sync_check was previously dead
code; now wired. bubble_height adds +12px when sync!=None. Verified ✓✓ renders, test 9/9.

### #31 — sokol macOS backend pushes CONTROL-CODE chars (backspace U+007F) into the CHAR queue
- **Gap/bug:** `vendor/afterhours/vendor/sokol/sokol_app.h` `keyDown:` emits a
  `SAPP_EVENTTYPE_CHAR` for every key whose `NSEvent.characters` is non-empty —
  including BACKSPACE (characters = U+007F DEL) and other control keys. The
  afterhours sokol backend (`backends/sokol/backend.h` SAPP_EVENTTYPE_CHAR case)
  pushes ANY `char_code > 0` into the char queue, and the text_input widget
  drains it via `insert_char()`, whose only guard is `codepoint < 32` — so 0x7F
  (127 >= 32) is NOT rejected and gets TYPED into the field as a DEL glyph.
- **Symptom (Gabe, macOS, 2026-08-03):** "hitting backspace adds a space, hitting
  space does nothing" — the stray 0x7F is inserted (renders like a space/box) and
  the control byte corrupts the field so real editing misbehaves.
- **Proven:** `insert_char(state, 0x7F)` returns true and appends byte 0x7f
  (tests/unit/test_textinput.cpp; standalone repro confirmed).
- **App-side workaround (used):** `ecs::ComposerCharFilterSystem` (src/ecs/
  char_filter_system.h) runs before the UI-creating systems, drains the char
  queue via `metal_detail::pop_char()`, drops non-typable codepoints
  (`hanabi::is_typable_char` — rejects C0 controls + 0x7F, keeps tab + space +
  printable), and re-pushes the survivors in order. The widget then only ever
  pops real characters. `metal_detail::push_char/pop_char` are public, so no
  vendored edit is needed.
- **Minimal upstream fix (vendor, off-limits here):** either don't push control
  codes in the SAPP_EVENTTYPE_CHAR case (`if (ev->char_code >= 32 &&
  ev->char_code != 0x7F) push_char(...)`), or tighten `insert_char`'s guard to
  also reject 0x7F. A one-liner in the backend's CHAR case is cleanest.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's ComposerCharFilterSystem workaround is stale; no tracked src/ecs/char_filter_system.h remains in the allowed tree, and the test drives upstream insert_char unfiltered.

**Hanabi reference.** Current code: `tests/unit/test_textinput.cpp` (`FIXED UPSTREAM (afterhours gap #31)`) — current unit test states the app-side char filter was deleted and upstream now rejects controls. Tests: `tests/unit/test_textinput.cpp::test_backspace_char_not_inserted` — unit coverage that 0x7F does not enter text storage.



### #32 — text_input CURSOR (caret) draws INSIDE the last glyph, not after it
- **Symptom (Gabe):** "the typing playhead is inside the last typed letter" — the
  blinking caret overlaps the final character instead of sitting one gap to its right.
- **Where:** `vendor/afterhours/src/plugins/ui/text_input/component.h` cursor-overlay
  render (~line 288+): the caret x is derived from measured text-before-cursor width
  but is missing a small trailing advance / half-gap, so it lands on the glyph's right
  edge (visually inside it) rather than in the inter-character gap.
- **Fix direction (vendor):** offset the caret x by +1px (or +half the space advance)
  past the measured prefix width; clamp to the field's content width. App can't fix it
  (the caret is drawn entirely inside the vendored widget).

**POSTSCRIPT 2026-08-26 (source-reference audit).** No app workaround is visible in allowed source; the allowed docs say the current vendor pin already carries the correction.

**Hanabi reference.** Current code: `docs/COMMIT_AUDIT.md` (`caret should stop`) — current audit notes the vendor pin already included the caret correction.



### #33 — text_input has NO Shift+Enter newline (single-line only; Enter is the only submit)
- **Symptom (Gabe):** "shift+enter should make a new line." The single-line `text_input`
  treats plain Enter as submit (WidgetPress) and has no Shift+Enter → insert '\n' path.
  `text_area` (multiline) exists but the composer uses `text_input`.
- **Fix direction (vendor):** in `text_input`'s key handling, when WidgetPress fires AND
  Shift is held, insert '\n' instead of invoking on_submit (and let the field grow / wrap).
  Requires the field to render multi-line (see #34). Alternatively hanabi swaps the
  composer to `text_area` — but that widget needs the same wrap fix and a submit binding.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The single-line text_input claim is obsolete for current composer behavior; a nearby source comment still says Shift+Enter is not a newline and should be corrected.

**Hanabi reference.** Current code: `src/ecs/main_pane_system.h` (`auto inputRes = afterhours::ui::imm::text_area`) — composer now uses multiline text_area. `src/ecs/main_pane_system.h` (`.with_submit_on_enter(hanabi::enter_sends`) — plain Enter submit remains configurable while text_area handles newlines. Tests: `tests/ui/composer_shift_enter.e2e` (`Shift+Enter breaks the line instead of sending`) — scripted UI coverage for Shift+Enter newline and Enter submit.



### #34 — text_input does NOT wrap or clip long text — it overflows OUTSIDE the field box
- **Symptom (Gabe):** "the text is still not wrapping it just goes outside the box." Typing
  past the field width draws the text beyond the input's right edge (over the Send button /
  pane) instead of horizontally scrolling within the clip OR wrapping to a new line.
- **Where:** `text_input/component.h` — there IS a horizontal-scroll offset
  (state.scroll_offset_x) + an `Overflow::Hidden` inner container (~line 158-167), but in
  hanabi's composer setup the text still paints outside. Likely the clip container's width
  isn't propagating (the transparent text_input inside a fixed-width wrap), OR the draw
  path draws at the widget rect ignoring the clip. Single-line has no wrap at all.
- **Fix direction:** (a) ensure the text draw is scissor-clipped to the field content rect
  so overflow is hidden and h-scroll keeps the caret visible; and/or (b) add an opt-in
  multi-line WRAP mode (pairs with #33 shift+enter) so the composer grows vertically.
- **App-side interim:** none reliable yet (the draw is inside the vendored widget); needs
  the vendor fix. Tracked for the afterhours branch.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The old overflow description no longer matches the composer path; current source uses text_area with auto-grow/max-lines behavior.

**Hanabi reference.** Current code: `src/ecs/main_pane_system.h` (`.with_max_lines(kComposerMaxRows)`) — composer field now auto-grows as a multiline text_area bounded by max lines. `src/ecs/main_pane_system.h` (`composerRows_ = rows < 1`) — composer box height follows the text_area layout cache. Tests: `tests/ui/composer_box_grows_with_the_draft.e2e` (`assert_ui composer_input_wrap h=151`) — scripted UI coverage for multiline growth and max height.



### #35 — text_input: no Escape-to-clear; arrow-key caret movement depends on InputSystem
- **Escape:** Gabe wants Esc (2nd press) to clear the field. text_input has no
  clear-on-escape; MenuBack(ESC) is mapped app-side but not wired to the field. This is
  APP-SIDE fixable (composer listens for ESC and clears replyDraft + field state).
- **Arrow keys:** WidgetLeft/Right (arrows) move the caret via the action system — which
  only works now that hanabi registered afterhours::input::InputSystem (see the fix in
  build_systems). Before that, NO caret movement worked. HOME/END are mapped but LEFT/RIGHT
  must also be in hanabi's mapping (they are). Verify caret moves post-InputSystem-fix.

### #27-family (recurring) — spawn_status overflows spawn_card (NoWrap fixed-width child)
- **Symptom (log spam, every frame):** `Layout overflow: 'spawn_status' extends outside
  parent 'spawn_card'` + `NoWrap set but would overflow (child_size=140, offset=564,
  container=692)`. The spawn card's status label is a fixed 140px child placed at offset
  564 in a 692px card → 704 > 692 by 12px.
- **This is APP-SIDE (hanabi's spawn_card renderer), not vendor:** the fixed-width status
  column + lead columns exceed the card content width. Fix the width math (shrink the
  status column / compute it from remaining width like the tool-row meta cluster) so it
  fits, killing the per-frame overflow warn (which also triggers solve_violations = perf).



**POSTSCRIPT 2026-08-26 (source-reference audit).** The gap's 'no Escape-to-clear' half is stale because hanabi ships it app-side; the mapping dependency remains documented and implemented.

**Hanabi reference.** `src/ecs/main_pane_system.h::ESCAPE-TO-CLEAR` — composer-focused Esc clears the field through app-owned routing. `src/input_mapping.h::bind(InputAction::WidgetLeft` — input mapping binds arrow/caret actions explicitly. Tests: `tests/ui/escape_dismisses_one_thing.e2e` (`With nothing over it, Esc belongs to the composer again`) — scripted coverage for Esc ownership and clearing.

---

## TEXT-INPUT COMPONENT — FULL REQUIREMENTS SPEC (what a real text field must do)

Gabe asked for a complete spec of everything we need from the input UI component,
researched against the HTML `<input>`/`<textarea>` editing model, Dear ImGui's
`InputText` (backed by `stb_textedit`), egui's `TextEdit`, and native macOS text
behavior. Gaps #29/#31/#32/#33/#34/#35 above are individual instances; THIS is the
consolidated target. Grouped by capability, each marked:
  [OK] works in afterhours today · [PARTIAL] half-there · [GAP] missing.

References: WHATWG/HTML editing ("Move the caret" / "Change the selection"),
Dear ImGui `imgui_widgets.cpp` STB_TEXTEDIT_K_* bindings, egui `TextEdit`
(singleline/multiline, desired_rows, clip, cursor/selection, clipboard), macOS
NSText standard key bindings.

### 1. Text entry
- [OK]   Insert printable Unicode (UTF-8) at the caret.
- [OK]   Replace the active selection when typing.
- [GAP]  Reject/round-trip control codes: 0x7F (DEL) and other C0 controls must
         NOT be inserted (gap #31 — macOS backspace emits 0x7F as a CHAR). insert_char
         rejects < 32 but NOT 0x7F.
- [GAP]  IME / dead-key / composed input (accents, CJK) — no composition support.
- [OK]   max_length clamp.

### 2. Deletion
- [OK]   Backspace: delete char before caret (or the selection).
- [OK]   Delete: delete char after caret (or the selection).
- [PARTIAL] Word delete: Ctrl/Alt+Backspace = delete word left; Ctrl/Alt+Delete =
         delete word right. (text_area has word ops; verify single-line text_input.)
- [GAP]  Cmd+Backspace (macOS) = delete to start of line.

### 3. Caret movement
- [OK]   Left / Right by one char (WidgetLeft/WidgetRight) — but was DEAD until the
         app registered afterhours::input::InputSystem (see hanabi build_systems).
- [OK]   Home / End (TextHome / TextEnd) to line start/end.
- [PARTIAL] Word left/right (Ctrl/Alt+Arrow) — move_cursor_word_left/right exist;
         confirm they're MAPPED in the consuming app.
- [GAP]  Up / Down between lines (multiline only — needs the multiline path).
- [GAP]  Cmd+Left/Right (macOS) = line start/end; Cmd+Up/Down = doc start/end.
- [GAP]  Caret must render in the GAP AFTER the last glyph, not inside it (gap #32,
         FIXED on the hanabi/text-input-fixes branch: origin = pad_left).

### 4. Selection
- [OK]   Shift+Left/Right extends selection by char.
- [PARTIAL] Shift+Home/End, Shift+Ctrl/Alt+Arrow (word), Shift+Cmd+Arrow — partial.
- [GAP]  Select-all: Ctrl/Cmd+A. (TextSelectAll action exists; confirm Cmd on macOS.)
- [GAP]  Double-click = select word; triple-click = select line/all.
- [GAP]  Click-drag to select a range; Shift+Click to extend.
- [OK]   Typing / Backspace / Delete replaces the selection.
- [PARTIAL] Selection highlight render (aligned to pad_left now, gap #32 family).

### 5. Clipboard
- [PARTIAL] Cut / Copy / Paste (Ctrl/Cmd+X/C/V). Paste path exists (filters cp<32);
         verify Cmd on macOS + cut/copy of the selection. Paste must also strip
         control chars + (for single-line) collapse/‑strip newlines.
- [GAP]  Paste into multiline should keep newlines (multiline path).

### 6. Undo / redo
- [PARTIAL] push_undo_snapshot() exists (undo stack). Wire Ctrl/Cmd+Z (undo) and
         Ctrl+Shift+Z / Cmd+Shift+Z (redo) bindings + confirm coalescing of runs.

### 7. Submit / newline
- [OK]   Enter fires on_submit (single-line) — but ONLY once the app attaches a
         HasTextInputListener AND registers InputSystem (both were missing in hanabi).
- [GAP]  Shift+Enter = insert newline (gap #33) — needs the multiline render + a
         newline-insert that bypasses the cp<32 guard.
- [GAP]  Escape = clear / revert (gap #35). App-side workaround in hanabi; a
         first-class on_cancel/clear behavior would belong here.

### 8. Layout / rendering
- [PARTIAL] Single-line horizontal scroll (scroll_offset_x) keeping the caret
         visible — exists but text can still paint OUTSIDE the field box in some
         host setups (gap #34): the draw must be SCISSOR-CLIPPED to the content rect.
- [GAP]  Multi-line WRAP mode (wrap at width, grow vertically to a max, then scroll)
         — egui `multiline()` / `desired_rows` / HTML `<textarea>`. Needed for the
         chat composer (gap #33/#34). text_area exists but isn't wrap+submit-wired.
- [PARTIAL] Placeholder text when empty (gap #29 — no native placeholder; hosts
         overlay their own). Should be first-class (HTML `placeholder`).
- [OK]   Caret blink; focus ring.
- [GAP]  Config honored: with_font_size / with_custom_background were ignored
         (gap #17); font derived from height. A field should honor explicit styling.
- [GAP]  Disabled + read-only visual states (read-only: selectable, not editable).

### 9. Focus / interaction
- [OK]   Click to focus + position caret.
- [OK]   Tab / Shift+Tab focus traversal (skip_tabbing supported).
- [GAP]  Focus must not be stolen/reset each frame by the immediate-mode rebuild
         (verify caret/selection persist across frames while focused).
- [GAP]  Mouse: drag-select, double/triple-click (see §4).

### 10. Accessibility / misc
- [GAP]  Screen-reader / a11y role (out of scope for the sprite backend, note it).
- [PARTIAL] Scroll the field into view on focus (host concern).
- [GAP]  Input filtering / validation hook (numeric-only, max, pattern) — ImGui has
         InputTextFlags (CharsDecimal, CharsNoBlank, callbacks). A filter callback
         (accept/transform a codepoint) would cover gap #31 generically.

### PRIORITY for the chat composer (hanabi's actual need), in order:
  1. [GAP] control-char filter (#31) — DONE app-side; belongs in the widget/backend.
  2. [GAP] scissor-clip single-line so text can't escape the box (#34a).
  3. [GAP] multiline wrap + Shift+Enter newline (#33/#34b) — the big one.
  4. [FIXED] caret origin (#32) — landed on hanabi/text-input-fixes.
  5. [GAP] macOS Cmd-based bindings (word/line nav, select-all, clipboard, undo).
  6. [GAP] double/triple-click + drag selection.
  7. [GAP] first-class placeholder (#29) + honor style config (#17).

---

# Session 2026-08-22 — parity pass (transcript polish)

### #37 — No text selection on read-only text (you cannot copy an answer)

- **What I was trying to build.** The transcript is the product. A user reads an
  answer and wants a line of it: drag across it, Cmd+C, paste into a terminal.
  Every chat client on the desktop does this, and it is the single most common
  thing anyone does with a body of text they did not write.

- **What I tried.** Rendered the message body the ordinary way and dragged
  across it:

  ```cpp
  div(ctx, mk(turn, 2),
      ComponentConfig{}
          .with_label(body)
          .with_text_overflow(TextOverflow::Wrap)
          .with_font_size(theme::type::BODY)
          .with_debug_name("asst_line"));
  ```

  Then went looking for the opt-in — `with_selectable()`, a `HasTextSelection`,
  anything on `ComponentConfig` — and for a context-level accessor that would
  hand back whatever the user had highlighted.

- **What happened.** Nothing highlights and nothing is exposed. `ComponentConfig`
  has no selection knob (the only near-miss is the `readonly` comment on
  `component_config.h:152`, which is a text_input mode, not a label feature), and
  the press/drag path never associates a byte range with a rendered label.
  Dragging across a message does exactly what dragging across a rectangle does.

  The frustrating part is how close it already is. `ui/text_selection.h` is a
  complete, backend-free selection-geometry module — `byte_offset_at()`,
  `selection_rects()`, `substring()`, `line_start_offsets()`, `Selection` with
  anchor/cursor — deliberately written against a `measure` callable so it builds
  anywhere. `clipboard::set_text()` works on the sokol backend. But
  `grep -rl 'text_selection::' src/` returns exactly one file,
  `ui/text_input/text_layout.h`. All of the hard geometry is done and it is
  reachable only from inside the editable widget.

- **The workaround.** A per-message **Copy** button, revealed on hover
  (`main_pane_system.h`, `message_actions`). 108 added lines, and three costs
  worth naming:
  1. **A permanent 24px of vertical space per message.** The strip has to be in
     `bubble_height()` or the virtualization spacers drift the instant the mouse
     moves, so it is reserved on every turn whether or not it is painted. On a
     120-message transcript that is ~2900px of reserved whitespace.
  2. **Whole-message granularity.** A user who wants one account number out of a
     four-line reply copies all four lines and edits them somewhere else.
  3. **No selection, so no Cmd+C.** The keyboard route people actually use is
     still dead; they have to find and hit a button.



**POSTSCRIPT 2026-08-26 (source-reference audit).** The afterhours gap remains, but the entry's stated workaround is stale: the Copy button is no longer the only route; hanabi now has per-element selection and copy.

**Hanabi reference.** `src/ui/text_select.h` (`Selecting text in the transcript`) — hanabi implements app-owned read-only text selection. `src/ecs/main_pane_system.h::selectable_text` — rendered transcript text registers with the app selection helper. Tests: `tests/ui/select_text_in_a_message.e2e` (`afterhours has no selection on read-only text`) — scripted coverage that dragging transcript text keeps the transcript stable.

- **What the library could offer instead.** An opt-in on the config, defaulting
  off so nothing changes for anyone who doesn't ask:

  ```cpp
  // ComponentConfig
  .with_selectable_text()            // opt in; no cost when unset

  // Context, after the frame resolves
  [[nodiscard]] std::string selected_text() const;
  [[nodiscard]] bool has_selection() const;
  ```

  The implementation is mostly assembly of parts that exist. On press inside a
  selectable element, claim it (one selection at a time — a press anywhere else
  clears) and seed `Selection.anchor` from
  `text_selection::byte_offset_at(lines, local, line_h, measure)`; on drag,
  update `.cursor` the same way; in the render pass, paint
  `selection_rects(...)` behind the glyphs in a theme token
  (`selection_bg`/`selection_fg`) — the rects come back in the element's own
  coordinates already; on Cmd/Ctrl+C, `clipboard::set_text(substring(lines,
  sel.range()))`.

  Two things that would make it genuinely good rather than merely present:
  **double-click selects a word, triple-click a line** (the text-input spec in
  this file already wants both, §4 — one implementation could serve both
  widgets), and an **`I` beam cursor** over selectable text via the existing
  `HasCursor`, so it is discoverable.

  Selection spanning *several* elements (drag from one message into the next) is
  a much bigger design question — a document-order model across the tree — and I
  would explicitly leave it out of v1. Per-element selection alone covers the
  overwhelming majority of the need, and it is the difference between "I can get
  this text out" and "I cannot".

- **Severity: blocks the feature.** Not the app — hanabi ships with a Copy
  button — but it blocks the interaction, and it is the one gap on this list a
  user notices in the first thirty seconds. For any app whose main surface is
  text somebody wants to keep, this is the highest-value thing on the list.

### #38 — A container cannot report hover unless it is clickable

- **What I was trying to build.** The reveal half of #37's workaround: show the
  Copy button only while the pointer is over that message. The natural shape is
  "does the mouse sit anywhere in this turn's subtree", asked of the plain
  `Column` div that already wraps the turn.

- **What I tried.**

  ```cpp
  auto turn = div(ctx, mk(parent, 200 + index * 10),
      ComponentConfig{}
          .with_size(ComponentSize{percent(1.0f), children()})
          .with_flex_direction(FlexDirection::Column)
          .with_transparent_bg()
          .with_debug_name("asst_turn"));
  const bool hovered = ctx.mouse_was_in_subtree(turn.ent().id);   // always false
  ```

- **What happened.** Always false. `ResolveHitTarget::is_candidate`
  (`ui/systems.h`) opens with

  ```cpp
  if (e.is_missing<HasClickListener>() && e.is_missing<HasDragListener>())
    return false;
  ```

  so an element with no listener is never eligible to be `hot_id`, and every
  query derived from `hot_id` — `is_hot`, `was_hot`, `mouse_in_subtree`,
  `mouse_was_in_subtree` — reports false for it forever. The rule is a
  reasonable default for click resolution; it just also means *hover is a
  privilege of clickable things*, which containers and read-only surfaces are
  not.

- **The workaround.** Two lines per turn, and neither reads like intent:

  ```cpp
  // hoverable only because of this listener; it deliberately does nothing
  turn.ent().addComponentIfMissing<afterhours::ui::HasClickListener>(
      [](Entity&) {});
  turn.ent().get<afterhours::HasColor>().skip_hover_override = true;
  ```

  The empty lambda is load-bearing, which is exactly the kind of line that gets
  "cleaned up" by the next person through. And the second line is needed because
  the moment an element becomes hot, `rendering.h` (~1549) swaps its fill for
  `hover_bg()` unless `HasColor::skip_hover_override` is set — a field with no
  `ComponentConfig` setter, so it has to be poked directly on the component
  after construction, bypassing the config API the rest of the call site uses.
  hanabi now does this in two places (`sidebar_system.h:1866` for the star, and
  here).



**Hanabi reference.** `src/ecs/main_pane_system.h` (`Hoverable only because of this listener`) — turn containers still add a no-op click listener solely to become hover-test candidates. `src/ecs/main_pane_system.h::ctx.mouse_was_in_subtree(turn.id)` — message actions are revealed through subtree hover state. Tests: `tests/ui/message_copy_on_hover.e2e` (`expect_text "Copy"`) — scripted coverage that hovering a message reveals the Copy action.

- **What the library could offer instead.** Two small additions:

  ```cpp
  .with_hoverable()             // eligible for hot_id; no click semantics
  .with_skip_hover_override()   // opt out of the automatic hover fill
  ```

  `with_hoverable()` would add whatever marker `is_candidate` should really be
  testing — say `AcceptsPointer` — with `HasClickListener`/`HasDragListener`
  implying it. That keeps today's behaviour for every existing call site while
  letting a container say "I want to know about the pointer" without pretending
  to be a button. `with_skip_hover_override()` is a one-line config setter for a
  field that is already there.

- **Severity: makes it ugly.** Everything works; the code lies about why.

- **Credit where it is due:** `mouse_was_in_subtree()` is the right primitive and
  it is exactly what this needed. Gap #29 in this file (a hoverable child steals
  the parent row's hover fill) describes the problem it solves, and the sidebar's
  hand-rolled fix for #29 — caching the star's id and OR-ing `is_hot`/`was_hot`
  across both entities, `sidebar_system.h:1607-1665` — predates it and can now be
  deleted. **#29 is resolved.** One call, no id bookkeeping, and it composes to
  any depth. Thank you.

### #39 — The e2e runner never fails a single-script run

- **What I was trying to build.** Scripted UI tests: hover this, click that,
  assert the text that appeared. One script per process so a hang is attributed
  to the script that caused it, and the process exit code is the verdict.

- **What I tried.** The documented shape, from the Quick Start in
  `e2e_testing.h`:

  ```cpp
  t::E2ERunner runner;
  runner.load_script(path);
  while (!runner.is_finished()) { t::test_input::reset_frame();
                                  runner.tick(dt); render_one_frame(); }
  return runner.has_failed() ? 1 : 0;
  ```

- **What happened.** Every script passed, including one written to fail. This
  script exits 0:

  ```
  mouse_move 140 700          # the sidebar — nowhere near a message
  wait_frames 3
  expect_text "Copy"          # only ever rendered on a hovered message
  ```

  The assertion does fail. `E2ECommandCleanupSystem` times the command out after
  `MAX_FRAMES`, logs `[E2E ERROR]`, and increments
  `detail::command_error_count()`. But `has_failed()` returns `failed_`, and
  `failed_` is only ever set by the `validate` path; the bridge from the counter
  into the result is `finalize_current_script()`, which begins

  ```cpp
  if (script_results_.empty() || current_script_idx_ >= script_results_.size())
      return;
  ```

  and `script_results_` is only populated by `load_scripts_from_directory()`. So
  in single-script mode the counter is read by nobody and the runner is
  structurally incapable of reporting a failure. A green suite that cannot go
  red is worse than no suite.

- **The workaround.** Ignore `has_failed()` and read the handlers' counter
  directly (`src/main.cpp`, `run_e2e`):

  ```cpp
  const int errors = t::get_command_error_count();
  const bool failed = errors > 0 || runner.has_failed() || ranOut;
  ```

  Two lines, but they require having read the internals of three headers to know
  the public API is not load-bearing.



**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's workaround is gone because upstream fixed single-script failure reporting. tests/README still carries old workaround wording.

**Hanabi reference.** Current code: `src/main.cpp` (`Both were working around afterhours bugs that are now fixed`) — run_e2e no longer reads the internal command-error counter workaround. `src/main.cpp` (`const bool failed = runner.has_failed() || ranOut`) — current verdict trusts runner.has_failed plus timeout.

- **What the library could offer instead.** Fold the counter in for both modes —
  give `finalize_current_script()` a single-script branch, or have `has_failed()`
  itself consider `get_command_error_count()`:

  ```cpp
  bool has_failed() const {
    if (!script_results_.empty()) { /* … as today … */ }
    return failed_ || get_command_error_count() > 0;
  }
  ```

  A test in the library's own suite that runs a deliberately-failing script and
  asserts the runner reports failure would have caught this, and is worth having
  whichever way it is fixed.

- **Severity: blocks the feature** — silently, which is the bad kind. Anyone
  adopting the harness in single-script mode gets a suite that always passes and
  no signal that anything is wrong.

### #40 — The last command's result is never observed

- **What happened.** A script's final assertion is not waited for. `tick()` ends
  with

  ```cpp
  dispatch_command(cmd);
  index_++;
  if (index_ >= commands_.size()) { finalize_current_script(); finished_ = true; }
  ```

  so the last command is dispatched and the runner declares itself finished in
  the *same* tick, before any system has had a chance to handle it. The host's
  `while (!runner.is_finished())` loop exits immediately, the frame that would
  have evaluated the assertion never runs, and `finalize_current_script()` reads
  an error count the command had no opportunity to increment. There is a guard
  for exactly this a few lines above —

  ```cpp
  if (index_ >= commands_.size()) { if (has_pending_commands()) return; … }
  ```

  — but it is unreachable, because the bottom of the previous tick already set
  `finished_`. Ending a script with an assertion is the normal case, so this
  affects most scripts.

- **The workaround.** Pump 40 more frames after the runner says it is done —
  past `PendingE2ECommand::MAX_FRAMES` (30) — so a trailing command resolves or
  times out for real, and only then read the verdict. Costs ~0.7s per script and
  an explanation in the host loop.



**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's 40-extra-frame workaround is no longer current; upstream runner behavior is documented as fixed.

**Hanabi reference.** Current code: `src/main.cpp` (`runner used to declare itself finished in the same tick it dispatched the`) — run_e2e documents the upstream fix for the last-command drain bug. `src/main.cpp` (`No drain loop, and no reading the handlers' counter`) — current host loop removed the 40-frame drain workaround.

- **What the library could offer instead.** Don't set `finished_` while commands
  are outstanding; let the top-of-tick guard do its job:

  ```cpp
  dispatch_command(cmd);
  index_++;
  // …and nothing else. The next tick's
  //   if (index_ >= commands_.size()) { if (has_pending_commands()) return; … }
  // finalizes once the queue actually drains.
  ```

  That is a deletion, and it makes the existing guard meaningful.

- **Severity: blocks the feature** (paired with #39 — together they are why a
  script asserting something plainly false came back green).

### #41 — The e2e harness has no worked example of a host loop

- **What happened.** Wiring the driver into an app took reading five headers,
  because nothing in the repo does it. `grep -rl E2ERunner .` returns only the
  three headers that define it; no `examples/` program hosts one. The Quick
  Start in `e2e_testing.h` covers handler registration and stops there, which is
  the easy half.

  The things that had to be discovered by reading source:
  1. **`AFTER_HOURS_ENABLE_E2E_TESTING` is the switch.** Without it,
     `input_system.h` compiles the injection branches out and every synthetic
     click goes to the real platform. It is not named in `e2e_testing.h` at all.
  2. **`test_input::reset_frame()` is the host's job and nothing says so.** The
     library never calls it. Without it a synthetic press stays pressed forever
     and the second click of a script never registers — which looks like a bug
     in the app, not in the harness.
  3. **Registration order is load-bearing.** The handlers inject this frame's
     input, so they have to be registered ahead of the input and UI systems or
     every click lands a frame late and races the assertions.
  4. **Screen size comes from `window_manager::ProvidesCurrentResolution`,** so
     percentage coordinates silently fall back to 1280x720 if the app hasn't
     registered it.
  5. **The app must settle before the script starts.** Data loads
     asynchronously; a script that clicks a sidebar row on frame 1 clicks empty
     space.



**Hanabi reference.** Proof patch or spike, not shipped: `src/main.cpp` (`static int run_e2e`) — hanabi has a complete host loop using E2ERunner. `src/main.cpp` (`t::test_input::reset_frame();`) — host loop documents and performs per-frame input reset. Tests: `tests/ui/composer_enter_replies_in_thread.e2e` (`Enter in the composer REPLIES to the open thread`) — one scripted UI test that runs through this host loop.

- **What the library could offer instead.** One example program under
  `examples/` — a window, three widgets, a `.e2e` script, and the host loop —
  plus a HOST LOOP section in the Quick Start covering the five points above.
  The whole loop is about 25 lines; having it written down once would have saved
  most of an evening, and it is the difference between a feature people adopt
  and a feature people don't find.

  `hanabi`'s integration is in `src/main.cpp` (`run_e2e`) and
  `scripts/run_ui_tests.sh` if a worked example is useful to crib from — it is
  MIT, take any of it.

- **Severity: makes it ugly** — nothing is broken, but a genuinely good piece of
  the library is sitting unused for want of a page of documentation.

## Perf, re-measured 2026-08-22 — and a correction to #27

**First, a retraction.** This file (#27) and hanabi's own notes have been
carrying an "~8.6ms idle-frame floor" attributed to afterhours rebuilding the
widget tree and re-solving layout every frame. That was mostly wrong, and the
error was ours: hanabi's makefile had **no `-O` flag in the main build at all**.
Only the perf micro-benchmark asked for `-O2`; the app, and the `.app` bundle
built from the same objects, were `-O0`. Adding `-O2`:

| screen | widgets | -O0 | -O2 |
|---|---|---|---|
| Home digest, idle | 315 | 5.45 ms | **0.95 ms** |
| a short transcript | 343 | 6.25 ms | **1.14 ms** |
| 120-message transcript (virtualized) | 460 | 9.08 ms | **1.64 ms** |

Median of 240 frames, headless 1100x760, mock data, Apple Silicon. So the floor
was 5-6x compiler flag, not architecture. **#27 stands as a design observation —
there is still no dirty/skip layer, and every one of those frames is identical
work on a screen where nothing changed — but it is no longer costing us
anything we can feel, and nobody should re-prioritise it on our account.** Sorry
for the bad signal.

What follows is where the time goes *now*, at `-O2`, on an idle Home digest —
`sample` at 1ms for 10s, 5,859 non-worker samples, frame median 0.95ms. The
absolute numbers are small (a fifth of a millisecond each). They are here
because they are pure repeated work with clean fixes, and because they scale
with content: the same proportions on a bigger screen are a bigger bill.

### #42 — The draw path re-measures every string from scratch, every frame

- **What I was trying to build.** Nothing in particular — this is what an idle
  screen costs. 315 widgets, no animation, no input, the same pixels as the
  frame before.

- **What happened.** Roughly a fifth of the frame is spent measuring text that
  has not changed:

  | | self |
  |---|---|
  | `stbtt_GetGlyphKernAdvance` | 11.5% |
  | `fons__getGlyph` | 5.6% |
  | `stbtt__GetGlyphClass` | 1.4% |
  | `fons__getQuad` | 1.3% |
  | `fonsDrawText` | 1.0% |
  | | **~21%** |

  The stack says why:

  ```
  RenderImm::render_me
    -> draw_text_in_rect          (rendering.h)
       -> position_text_ex        347 samples
          -> measure_text(font, text, size, spacing)     <-- uncached
             -> fonsTextBounds
                -> fons__getQuad
                   -> fons__tt_getGlyphKernAdvance
                      -> stbtt_GetGlyphKernAdvance       <-- GPOS re-parse, per pair
  ```

  Two things are stacked here. `position_text_ex` measures the string to align
  it in its rect — necessary information, but the answer is identical every
  frame for an unchanged label. And underneath, `stbtt_GetGlyphKernAdvance`
  walks the font's GPOS coverage tables for **every adjacent glyph pair**, with
  no memo; `stbtt__GetCoverageIndex` and `ttUSHORT` between them were the two
  hottest functions in the whole profile before optimisation.

  The part that makes this an easy fix: **`ui::TextMeasureCache` already exists**
  — LRU, generation-pruned, hashed on (text, font, size, spacing) — and is
  already registered as a singleton in `utilities.h`. It appeared in a 10-second
  profile exactly once. `rendering.h` calls the raw `measure_text` in about a
  dozen places, including this one; only `measure_text_wrapped` takes the cache.
  The cache is built and then not used on the path that needs it most.

- **The workaround.** None available app-side — `position_text_ex` is inside the
  renderer. hanabi memoizes at a much coarser grain (`transcript_render_cache.h`
  caches measured message heights), which helps the app's own layout pass but
  cannot touch what the renderer does when it draws.



**Hanabi reference.** Hanabi-owned performance finding: `docs/perf/SIDEBAR.md` (`gap #42 — the draw path re-measures every string from scratch every frame`) — current perf docs still identify the afterhours draw-path measurement cost. `src/ui/theme.h` (`afterhours ships a TextMeasureCache wired up as a singleton, and it is unusable from here`) — hanabi uses its own measurement semantics where the library cache cannot be adopted directly. Measurement/gate: `docs/perf/TEXT.md` (`text measurements / frame | 492.6 | **5.3**`) — current text perf document records hanabi-side measurement reductions.

- **What the library could offer instead.** Two independent changes, either one
  worth having:

  1. **Route `rendering.h`'s measurements through the cache that exists.** The
     singleton is reachable the same way `systems.h:580` already reaches it:

     ```cpp
     if (auto *cache = EntityHelper::get_singleton_cmp<ui::TextMeasureCache>())
       size = cache->measure(text, font_name, font_size, spacing);
     else
       size = measure_text(font, text.c_str(), font_size, spacing);
     ```

     Best done once behind a `measure_cached(...)` helper in `rendering.h` and
     applied to the dozen call sites, so no new call site can forget.

  2. **Memoize the kern pair.** A `(glyph_a, glyph_b, size) -> advance` map in
     the fontstash shim would collapse the GPOS re-parse for everyone, including
     callers who legitimately miss the measure cache. A UI draws from a small
     alphabet; the table saturates in the first frame.

- **Severity: makes it slow** — ~0.2ms/frame here, and it is the largest single
  item left in an idle frame. Not urgent for hanabi at 0.95ms/frame, but it is
  paid on every frame of every app on the library, forever.

### #43 — Component lookup goes through `dynamic_cast`, so type identity costs a `strcmp`

- **What happened.** The second-largest cluster in the same profile is C++
  runtime type machinery, and the single hottest non-font function is `strcmp`:

  | | self |
  |---|---|
  | `_platform_strcmp` | 9.1% |
  | `System<UIContext, FontManager>::for_each_derived` (two sites) | 4.5% |
  | `std::type_info::operator==` | 1.7% |
  | `dyn_cast_slow` | 1.1% |
  | | **~16%** |

  The chain, from the unoptimised profile where it is fully legible:

  ```
  run_systems_on_ui_entities
    -> System<UIContext, FontManager>::for_each_derived
       -> HasAllComponents<...>::has_all
          -> Entity::has_child_of<UIContext>()            entity.h:84
             -> child_of<UIContext, BaseComponent>(BaseComponent*)
                -> __dynamic_cast
                   -> dyn_cast_try_downcast
                      -> __si_class_type_info::search_above_dst
                         -> std::type_info::operator==
                            -> strcmp                     <-- comparing type NAMES
  ```

  On Apple's libc++abi, `type_info::operator==` for types from the same image
  can fall through to comparing the mangled name strings. So each system, for
  each UI entity, for each required child component, does a `dynamic_cast` whose
  inner loop is string comparison. Two call sites in this profile alone (256 and
  259 samples) were ~9.4% of the main thread. It is O(entities x systems x
  required-components) per frame and it grows with the app.

- **The workaround.** None — this is inside `System::for_each_derived`.



**Hanabi reference.** Hanabi-owned performance finding: `tests/e2e/test_perf.cpp` (`=== afterhours #43: component lookup ===`) — microbenchmark compares has_child_of(dynamic_cast) to has(bitset). Measurement/gate: `tests/e2e/test_perf.cpp` (`has_child_of<T> (dynamic_cast)`) — prints ns/call and ratio for the measured component lookup cost.

- **What the library could offer instead.** The type identity is already known
  without asking the runtime. `Entity` carries a `std::bitset<128>` of component
  ids and `components::get_type_id<T>()` gives a stable index — the same
  mechanism `Entity::has<T>()` uses, which does not appear in the profile at
  all. `has_child_of<T>()` could ask the same question:

  ```cpp
  template <typename T> bool has_child_of() const {
    // instead of dynamic_cast'ing each component to T
    for (const auto &c : children_components)
      if (c && c->is_child_of(components::get_type_id<T>()))  // or a bitset test
        return true;
    return false;
  }
  ```

  If the child relationship genuinely needs a runtime downcast, the cheap fix is
  to **cache the answer per (entity, system)** — the set of components on a UI
  entity is stable for the entity's life, and the imm layer reuses entities
  across frames, so the cast result can be computed once and invalidated when a
  component is added or removed.

  A smaller, free win regardless: `__dynamic_cast` is much faster when the class
  hierarchy is simple, and much slower when it has to search. If `child_of`
  can be restructured so the common answer is "no" without entering libc++abi
  at all, most of this disappears.

  `docs/speed.md` already lists "remove virtual destructor / BaseComponent
  inheritance" and the SoA/archetype work as HIGH-impact core items — this is a
  measurement in support of that section, from a real app rather than a
  microbenchmark, and it suggests the *type-identity* half is worth pulling out
  as its own smaller change: the profile says `strcmp`, not cache misses.

#### #43, measured

A percentage of a flame graph is hard to act on, so here is the gap with a
number on it. `make perf` in hanabi now times the two lookups head to head
(`tests/e2e/test_perf.cpp`, "afterhours #43"), on an entity with three
components, averaging a present component and an absent one:

```
  has_child_of<T> (dynamic_cast):     61.6 ns/call
  has<T>          (bitset):            1.0 ns/call
  ratio:                                63x
```

**61.6 ns versus 1.0 ns for the same question.** Two things make it that bad:
`has_child_of` walks the whole `componentArray` — `max_num_components` slots,
not the components the entity actually has — and dynamic_casts each one; and
each cast's inner loop can bottom out in `strcmp` on mangled type names.

What that costs per frame depends on the shape of the app.
`run_systems_on_ui_entities` runs every system over every UI entity, and
`for_each_derived` asks `has_child_of` once per required component. hanabi's
idle Home digest is 315 UI entities; the render pass alone walks them with
several derived systems. At 61.6 ns a call, a few thousand calls a frame is a
few tenths of a millisecond — which matches the ~16% of a 0.95 ms frame the
profile attributed to this chain, from the other direction.

The scaling is the part that should worry a library author: it is
O(entities x systems x required-components) with a ~60x constant, and every one
of those three grows as an app gets more interesting. hanabi is a small app.

**The fix looks cheap.** `Entity::has<T>()` already answers the same question
via `components::get_type_id<T>()` and a bitset, in 1 ns, and never appears in
a profile. If the child relationship genuinely needs a runtime downcast, the
result is cacheable per (entity, component-type): the component set of a UI
entity is stable for its life, and the imm layer reuses entities across frames.

- **Severity: makes it slow.** ~0.15ms/frame here, 63x slower than the
  alternative that already exists in the same header. Cheap to fix relative to
  the rest of the speed.md list, and it scales with entity count, so it gets
  worse exactly as an app gets more interesting.

### #44 — The imm builder copies its config a lot

- **What happened.** Smaller, but visible in the same profile and worth a line:

  | | self |
  |---|---|
  | `ComponentConfig` copy ctor | 1.8% |
  | `imm::mk(Entity&, int, source_location)` | 1.8% |
  | `ComponentConfig` move ctor | 1.3% |
  | `ComponentConfig::operator=(&&)` | 1.1% |
  | `~ComponentConfig()` | 1.0% |
  | | **~7%** |

  `ComponentConfig` is passed by value through `div()` / `button()` into
  `init_component` and `add_missing_components`, and it carries `std::string`s
  (label, debug name) and `std::function`s (`on_draw_fg`), so each hop is a real
  allocation-touching copy. A `const &` through the internal chain, or moving it
  once at the entry point, should take most of this off — it is the one call
  every widget in the tree makes.

- **Severity: makes it slow** (mildly). Listed because it is a small, local
  change on the hottest possible path.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The old 7% profile claim has been superseded by allocation-count evidence and renumbered upstream asks (#181), but the underlying config-copy issue remains.

**Hanabi reference.** Hanabi-owned performance finding: `docs/perf/ALLOCATIONS.md` (`ComponentConfig is copied three times per widget`) — current allocation docs restate the same cost in the updated perf taxonomy. `src/ecs/line_draw_state.h` (`ComponentConfig three times on the way into a widget`) — hanabi moved large draw-callback state onto entities to avoid multiplying captures through those copies. Measurement/gate: `docs/perf/ALLOCATIONS.md` (`ComponentConfig label copies | ~470 | upstream`) — current allocation accounting reports the remaining upstream cost.



### #45 — Widget callbacks outlive the frame that wrote them (no imm `on_submit`)

- **What I was trying to build.** Enter sends the message in the chat composer.
  The same composer is a "reply to this thread" field when a thread is open and
  a "start a new conversation" field when one isn't, so the handler has to know
  which — the same decision the Send button makes.

- **What I tried.** The imm layer has `text_input(...)` but no submit hook, so
  the handler goes on the entity, the way the docs and the existing widgets do
  it:

  ```cpp
  inputEnt.addComponentIfMissing<text_input::HasTextInputListener>(
      nullptr,
      [appPtr = &app, kickoff, canSend, canStream, draftPtr = &replyDraft]
      (Entity& e) {
          …
          if (kickoff) appPtr->requestKickoffPrompt = text;
          else         appPtr->requestSendPrompt = text;
          draftPtr->clear();
      });
  ```

- **What happened.** Enter created a brand-new conversation every time, even
  with a thread open — while the Send button, three lines away and reading the
  same variables, replied correctly. Two controls that are supposed to be the
  same action, doing different things.

  `addComponentIfMissing` keeps the FIRST closure forever. In an immediate-mode
  UI the surrounding code runs every frame, so it is natural to write a lambda
  that captures this frame's values — but the lambda that survives is the one
  from whatever frame the entity happened to be created on. Here that was a
  frame during startup, before the session finished loading, when `kickoff` was
  legitimately `true`. It stayed `true` for the life of the process. The
  captured `&replyDraft` had the same problem: it pointed into a per-thread
  draft map at whichever thread was open at birth, so Enter cleared the wrong
  thread's draft after a tab switch.

  Nothing here is a bug in afterhours — `addComponentIfMissing` does exactly
  what it says. But it is a trap the imm style walks you into: everything else
  in the frame is rebuilt from current state, and this one thing silently isn't.
  It is also invisible in review; the code reads correctly.

- **The workaround.** The listener is now stateless — it parks the submitted
  text on the app, and the per-frame composer code routes it with a mode
  computed this frame. ~15 lines, one new field on the app component, and a
  paragraph of comment so nobody "simplifies" it back.



**Hanabi reference.** `src/ecs/components.h` (`What Enter in the composer submitted, before it has been routed`) — composer listener parks facts on AppComponent instead of capturing per-frame mode. `src/ecs/main_pane_system.h` (`Enter parked its text here`) — per-frame composer code routes submit text using current kickoff/reply state. Tests: `tests/ui/composer_enter_replies_in_thread.e2e` (`Enter in the composer REPLIES to the open thread`) — script guards the stale-capture failure mode.

- **What the library could offer instead.** Two options, and I would want both:

  1. **A config-level submit hook, refreshed each frame like every other
     config value:**

     ```cpp
     text_input(ctx, mk(parent, 1), draft,
         ComponentConfig{}
             .with_on_submit([&](std::string_view text) { … })   // re-set every frame
             .with_placeholder("Message…"));
     ```

     `ComponentConfig` already carries `std::function`s (`on_draw_fg`) and they
     are applied fresh on every build, so the mechanism exists — this would just
     extend it to submit. The frozen-capture class of bug then cannot happen,
     because the callback is replaced every frame along with everything else.

  2. **A result-style read, matching `button()`:** the imm layer's whole idiom
     is `if (button(...)) { … }` — the widget returns what happened and the
     caller reacts in the frame's own scope, where all the state is live. A
     `text_input(...)` that returned `{changed, submitted}` would let the
     composer write:

     ```cpp
     auto res = text_input(ctx, mk(parent, 1), draft, cfg);
     if (res.submitted) { /* full access to this frame's state */ }
     ```

     which is the shape that makes the bug unwriteable.

  Failing either, a line in `PLUGIN_API.md` next to `addComponentIfMissing`
  saying "the first closure wins; do not capture per-frame state" would have
  saved this.

- **Severity: blocks the feature** (it shipped broken and looked fine), and the
  class of bug is worse than the instance: any listener attached this way with
  a captured value is wrong in a way that only shows up in a specific startup
  order.

### #46 — The focus ring fans out at the corners (concentric rings, constant roundness)

- **What I was trying to build.** A focused chat composer that looks like a
  focused text field.

- **What happened.** Clicking into the composer drew bracket marks hanging off
  each corner — four short arcs that don't join the ring, at a visibly bigger
  radius than the field's own corner. It reads as a rendering glitch, and it is
  on the app's most-used control.

  `rendering.h` (~1481-1536) draws the ring as an outline plus `thickness`
  concentric rounded-line rects:

  ```cpp
  for (float t = 0; t < thickness; t += 1.0f) {
    RectangleType thickRect = {focus_rect.x - t, focus_rect.y - t,
                               focus_rect.width  + t * 2.0f,
                               focus_rect.height + t * 2.0f};
    draw_rectangle_rounded_lines(thickRect, focus_roundness, focus_segments,
                                 focus_col, focus_corner_settings);
  }
  ```

  Every ring is passed the same **roundness fraction**, and roundness is
  relative: `radius = min(w, h) * 0.5 * roundness`. Each ring outward is 2px
  taller, so each ring's corner radius is 0.25px larger than the one inside it.
  The straight edges stay parallel — a 1px step is invisible there — but at the
  corners the arcs sweep different radii from different centres, so they splay
  apart into a fan. With the defaults (`focus_ring_thickness = 3`,
  `focus_ring_offset = 4`, plus the contrast outline) that is five arcs at five
  radii, which is the bracket.

  It is worst exactly where you would want a focus ring most: a short wide
  control. On a 34px-tall field `min(w,h)` is the height, so the ring's radius
  is dominated by the dimension that is changing fastest in relative terms.

- **The workaround.** Collapse the ring to a single hairline flush with the
  element, in `ThemeDefaults` at startup:

  ```cpp
  theme.focus_ring_thickness = 1.0f;
  theme.focus_ring_offset    = 0.0f;
  ```

  One ring has nothing to diverge from. It costs the ring's visibility on
  low-contrast backgrounds — the dual-colour outline is a genuinely good idea
  and this throws it away — and it doesn't fully clear the artifact: two faint
  accent arcs remain at the left corners, drawn about 10px in (roughly the
  field's `padding.left`), which suggests a second focus-coloured rounded rect
  is being emitted somewhere for the input's content rect. That one I could not
  reach from app code.



**Hanabi reference.** `src/preload.cpp` (`theme.focus_ring_thickness = 1.0f;`) — sets the app default to a single focus-ring hairline. `src/preload.cpp` (`theme.focus_ring_offset = 0.0f;`) — keeps that hairline flush so concentric rounded outlines cannot fan out.

- **What the library could offer instead.** Grow the ring in **radius**, not
  just in rect — keep the arcs concentric:

  ```cpp
  // radius of the element's own corner
  const float base_r = std::min(focus_rect.width, focus_rect.height)
                       * 0.5f * focus_roundness;
  for (float t = 0; t < thickness; t += 1.0f) {
    RectangleType r = { focus_rect.x - t, focus_rect.y - t,
                        focus_rect.width + t*2.0f, focus_rect.height + t*2.0f };
    // absolute radius grows 1:1 with the offset, so every ring is parallel
    draw_rectangle_rounded_lines_px(r, base_r + t, focus_segments,
                                    focus_col, focus_corner_settings);
  }
  ```

  which needs a **pixel-radius entry point** next to the fraction-based one:

  ```cpp
  void draw_rectangle_rounded_lines_px(RectangleType, float radius_px,
                                       int segments, Color, CornerSettings);
  ```

  That primitive is worth having on its own. A relative roundness is a
  reasonable default for a standalone widget, but it makes any two rectangles of
  different sizes impossible to keep visually parallel — hanabi already carries
  a `theme::roundness_for_px(px, w, h)` helper that back-solves the fraction
  from a wanted pixel radius, purely to line a message bubble up with the tool
  card next to it. Both problems disappear if the primitive can just be told the
  radius. (Related: gap #25, the degenerate triangle on mixed corners, is in the
  same family of "the rounded-rect path is doing radius arithmetic the caller
  cannot see".)

- **Severity: makes it ugly** — cosmetic, but on the control the user looks at
  most, and it made a polished app look broken.

### #47 — `expect_no_text` can never fail (its argument keeps the quotes)

**RESOLVED upstream** — the vendored afterhours (submodule 428047e) carries the
one-line fix suggested at the bottom of this entry: `parse_script` now routes
`expect_no_text` through `parse_quoted()` alongside `expect_text`. Verified on
the merged tree: `expect_no_text "profiling the disk"` FAILS on a frame that
paints it, and reports the label it matched. Quoted multi-word arguments are
usable again; the "pass a bare single word" workaround below is no longer
required, and the comments repeating it in `tests/ui/*.e2e` are stale.

- **What happened.** Every negative assertion in every UI script passes,
  including this one, on a build where the tab plainly reads `new1`:

  ```
  expect_text    "new1"      # PASSES  — the text is on screen
  expect_no_text "new1"      # ALSO PASSES — on the same frame
  ```

  `parse_script` gives `parse_quoted()` to `expect_text`,
  `expect_selected_text`, `expect_input_text` and `assert_ui_text`, but
  `expect_no_text` is not in that chain and falls through to the generic
  tail:

  ```cpp
  } else {
      std::string arg;
      while (iss >> arg) cmd.args.push_back(arg);   // whitespace-split, quotes kept
  }
  ```

  So `args[0]` is the seven characters `"new1"` — with the quote marks — and
  `VisibleTextRegistry::contains()` is asked for a substring nothing will ever
  contain. The handler is correct; it is never given the string the script
  meant. A multi-word argument fails a second way: it is split on whitespace and
  only the first fragment is ever consulted.

  This is the quietest possible failure mode. `expect_no_text` is exactly the
  assertion you write when you want to be sure something is gone, and it has
  been answering "yes, gone" unconditionally.

- **The workaround.** Pass a bare single word — `expect_no_text Copy` — and
  choose a distinctive one, since multi-word phrases are unusable. Every script
  in `tests/ui/` now carries a comment saying why, because the natural thing to
  write is the quoted form and it looks right.



**POSTSCRIPT 2026-08-26 (source-reference audit).** Resolved upstream; old bare-word workaround comments remain in some scripts, but current source can use quoted expect_no_text.

**Hanabi reference.** Current code: `tests/ui/shortcuts_sheet.e2e` (`expect_no_text "Keyboard shortcuts"`) — current scripted tests use a quoted multi-word negative assertion. `docs/COMMIT_AUDIT.md` (`expect_no_text "quoted" can never fail.`) — audit records that this was true when written and fixed by the vendor bump. Tests: `tests/ui/shortcuts_sheet.e2e` (`Fixed in afterhours 2d6f23d.`) — test comment pins that quoted expect_no_text is the current, fixed behavior.

- **What the library could offer instead.** One line, next to its sibling:

  ```cpp
  } else if (cmd.name == "expect_text" || cmd.name == "expect_no_text") {
      cmd.args.push_back(parse_quoted());
      cmd.wait_seconds = 1 * frame;
  }
  ```

  Worth a wider look while in there: `parse_script`'s dispatch is a chain of
  string comparisons plus three `constexpr` name arrays, and a command that
  matches none of them silently gets whitespace-split args rather than an error.
  Any command whose argument can contain a space is one forgotten branch away
  from this same bug. Having the tables declare an arity *and* a quoting rule
  per command — rather than the branch order deciding — would make the class
  impossible.

  A test for the harness itself, along the lines of "`expect_no_text` fails when
  the text is present", is the other half. It is a two-line test and it would
  have caught this the day it was written.

- **Severity: blocks the feature.** Together with #39 and #40 this is three
  independent ways a UI script reports success it did not earn. All three bit us
  in the first hour of using the harness, and all three are small fixes.

### #48 — A codepoint the font lacks draws NOTHING, and there is no way to ask

- **What I was trying to build.** Ordinary UI furniture typed as characters: a
  disclosure chevron on a collapsible row, a return-arrow in the composer's
  "↵ send" hint, an up-arrow on the Send button, a block caret at the end of a
  streaming reply, a horizontal rule for markdown `---`.

- **What happened.** Several of them were never on screen and nobody noticed,
  because a codepoint the loaded font does not cover renders as **nothing at
  all** — no `.notdef` box, no warning, no log line. `fons__getGlyph` returns
  no glyph and the draw is silently skipped.

  The composer's hint had been reading a bare lowercase **"send"** floating next
  to the context meter, which looks like a stray label rather than a keyboard
  hint. The Send button was `"Send  ↑"`, so it had a trailing gap and no arrow.
  The sub-agent rollup and the tool pile had no disclosure triangle, so neither
  looked expandable. And a markdown `---` produced a blank line.

  Auditing this from screenshots does not work — you are looking for something
  that is not there. What worked was reading the font's `cmap` directly and
  cross-referencing every non-ASCII literal in the source:

  ```
  0x21b5 '↵'   composer hint            0x2500 '─'  markdown rule
  0x2191 '↑'   Send button              0x258b '▋'  streaming caret
  0x25b8 '▸'   collapsed disclosure     0x25be '▾'  expanded disclosure
  0x2605 '★'   0x2606 '☆'  0x2713 '✓'  0x2699 '⚙'  0x26d4 '⛔'  0x2302 '⌂'
  0x25a4 '▤'   0x2726 '✦'  0x1f389 '🎉' 0x1f50d '🔍'
  ```

  Every one of those is in Roboto's coverage hole — it has no Arrows block, no
  Geometric Shapes block, no Box Drawing block. That is not unusual: it is one
  of the most widely-used UI faces there is, and a toolkit's users will reach
  for these characters constantly.

- **The workaround.** Draw the shaped ones as vectors
  (`hanabi::glyph::chevron`, a filled triangle — hanabi already had one of these
  in the sidebar and now shares it), and for the rest pick characters Roboto
  does cover: em dashes for the rule, `|` for the caret, and plain words —
  "Enter to send" — for the hint. Plus a `tests/ui` script asserting the hint
  and the button label are on screen, since the failure is invisible.



**Hanabi reference.** `src/ui/icons.h` (`Small vector marks the UI font cannot draw`) — font-unsafe UI symbols are drawn as vectors instead of typed glyphs. `src/ui/icons.h` (`inline void arrow_up`) — send arrow is vector-drawn because Roboto lacks U+2191. Tests: `tests/unit/test_atlas_guard.cpp::test_zero_for_a_real_string_is_a_fault` — unit test covers the guard's zero-width fault detection. Measurement/gate: `scripts/atlas_gate.sh` (`the atlas overflowed AND the detector raised a fault`) — gate proves the detector fires under real atlas overflow and stays silent on clean UI.

- **What the library could offer instead.** Three things, cheapest first:

  1. **A coverage query.** fontstash already parses the `cmap`; expose it.

     ```cpp
     [[nodiscard]] bool FontManager::has_glyph(std::string_view font,
                                               uint32_t codepoint) const;
     ```

     An app can then pick a fallback at startup instead of shipping an invisible
     label.

  2. **Draw `.notdef`.** A visible box is the convention for a reason: it turns
     a silent bug into an obvious one. Behind a flag if it would upset anyone —
     `theme.show_missing_glyphs = true` in debug builds would be enough.

  3. **A `log_warn` on the first miss per codepoint.** Rate-limited to once, this
     costs nothing and would have put the whole list above in the terminal on
     the first run. This is the one I would do first.

  A fourth, bigger and genuinely valuable: **font fallback**. `FontManager`
  already holds several faces (hanabi has Roboto, JetBrains Mono and Atkinson
  Hyperlegible loaded at once, and JetBrains Mono covers `⏎` and `→`). A
  per-glyph fallback chain — try the requested face, then the others in
  registration order — would have made every one of these Just Work.

- **Severity: makes it ugly**, but it is the most *insidious* item on this list.
  Every other gap announced itself. This one produces a screenshot that looks
  fine until you know what should have been there.

### #49 — A scripted test cannot press Cmd (the Super modifier is parsed, never held)

- **What I was trying to build.** A test for the app's keyboard shortcuts. On
  macOS every one of them is a Cmd chord: Cmd+B folds the sidebar, Cmd+, opens
  settings, Cmd+W closes a tab, Cmd+/ opens the new shortcut reference.

- **What I tried.**

  ```
  key CMD+SLASH
  wait_frames 8
  expect_text "Keyboard shortcuts"
  ```

- **What happened.** Nothing opened. Two things in the way, and the second is
  the real one:

  1. `parse_key_combo` maps `CMD+` to **Control**, with the comment
     `// Mac convention: Cmd = Ctrl for shortcuts` (`core/key_codes.h:302`).
     That convention holds for cross-platform game bindings, but a native macOS
     app reads the actual Command key — `LEFT_SUPER`/`RIGHT_SUPER`, 343/347 —
     because that is what the OS delivers. So `CMD+X` presses the wrong key.
  2. `KeyCombo` has a `super` field and `parse_key_combo` sets it for `SUPER+`,
     `WIN+` and `META+` — but **`HandleKeyCommand` never reads it**:

     ```cpp
     if (combo.ctrl)  input_injector::set_key_held(keys::LEFT_CONTROL);
     if (combo.shift) input_injector::set_key_held(keys::LEFT_SHIFT);
     if (combo.alt)   input_injector::set_key_held(keys::LEFT_ALT);
     // combo.super — parsed, then dropped
     ```

     So `SUPER+SLASH` presses the slash key with no modifier held. There is no
     spelling of a Cmd chord that works, and the whole shortcut surface of a
     macOS app is unreachable from a script.

- **The workaround.** Open the sheet through a state knob instead of the key
  that really opens it (`# env: HANABI_TEST_OVERLAY=shortcuts`, a per-script
  environment line the harness now supports), and script only the Esc that
  closes it. The test covers the sheet's content and its dismissal; the binding
  itself — the part most likely to break — is asserted by nobody.



**POSTSCRIPT 2026-08-26 (source-reference audit).** The harness still cannot hold Super, but the app now ships Ctrl twins/cmd_or_ctrl_down so most Cmd behavior is testable; the original “binding asserted by nobody” claim is stale.

**Hanabi reference.** `src/input_mapping.h` (`WHY THE CTRL TWINS`) — shipping action bindings add Ctrl twins so script CMD+ reaches Cmd-only app actions. `src/keys.h` (`inline bool cmd_or_ctrl_down()`) — raw shortcut reads accept Ctrl as a test-only alias for Cmd. Tests: `tests/ui/composer_line_delete.e2e` (`key CMD+BACKSPACE`) — scripted CMD chords now exercise line-editing behavior through the Ctrl alias.

- **What the library could offer instead.** Three lines and a rename:

  ```cpp
  if (combo.super) input_injector::set_key_held(keys::LEFT_SUPER);
  ```

  plus releasing it alongside the others in `key_release_detail`, and mapping
  `CMD+` to `super` rather than `ctrl`. That last one is a behaviour change for
  existing scripts, so it may want to be `CMD+` → super on macOS and ctrl
  elsewhere, or a new `SUPERCMD+`; either is better than a modifier that parses
  and evaporates. A `hold <keyname>` / `release <keyname>` pair for raw keys
  would also cover it generically, and would let a script test chords the
  parser has never heard of.

- **Severity: blocks the feature** — every keyboard shortcut in a macOS app is
  untestable.

### #50 — Key reads through the graphics layer bypass the input injector

- **What happened.** Even with a Cmd chord expressible, the app's shortcuts
  would not have fired: hanabi read keys with
  `afterhours::graphics::is_key_pressed(...)`, and the e2e hooks are on the
  INPUT plugin. `input_system.h` wraps every key and mouse read in
  `#ifdef AFTER_HOURS_ENABLE_E2E_TESTING → testing::test_input::...`; the
  graphics-layer key API has no such branch. Both are public, both look like
  the way to ask "is this key down", and only one of them is testable.

  The failure is silent and confusing from the app side: synthetic keys type
  into text fields and move focus (those paths go through the input plugin), so
  injection is plainly working — but the app's own shortcut handlers never see a
  thing.

- **The workaround.** A four-line shim in the app (`src/keys.h`) that calls
  `testing::platform_input::` in the scripted-UI build and `graphics::` in the
  shipping build, and routing all six shortcut sites through it. Cheap, and
  worth having anyway — but every app on this library has to independently
  discover the need for it.



**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's described four-line testing-vs-graphics shim is stale; the current wrapper is simpler and always forwards to afterhours::input.

**Hanabi reference.** `src/keys.h` (`afterhours::input already IS that branch`) — current key helper forwards through afterhours::input rather than graphics. `src/keys.h` (`inline bool pressed(int key) { return afterhours::input::is_key_pressed(key); }`) — all app shortcut reads go through the testable input plugin. Tests: `tests/ui/composer_word_editing.e2e` (`key CMD+LEFT`) — scripted key chords reach app shortcut/editing code through the input path.

- **What the library could offer instead.** Give the graphics key API the same
  `#ifdef` branch the input plugin already has, so the two agree:

  ```cpp
  // graphics.h
  inline bool is_key_pressed(int key) {
  #ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    return testing::test_input::is_key_pressed(key, [](int k) {
        return PlatformAPI::is_key_pressed(k); });
  #else
    return PlatformAPI::is_key_pressed(key);
  #endif
  }
  ```

  Or, if the intended rule is "apps read input through the input plugin, never
  through graphics", say so in `PLUGIN_API.md` — the fix is then a doc line
  rather than code, and both are better than the current position where the
  choice is silent and one branch cannot be tested.

- **Severity: makes it ugly** (the workaround is small), but it cost an hour of
  believing the injector was broken.

### #51 — No way to ask where a piece of text landed on screen

- **What I was trying to build.** Find-in-conversation: type a word, and every
  occurrence in the open thread gets a highlight band behind it. The band is
  the whole feature — a count with no visible matches is a search that makes
  you hunt.

- **What I tried.** The app hands afterhours a label and a rect and afterhours
  lays the text out — wraps it, positions the block, draws it. So the question
  is: *for byte range [a,b) of the string I gave you, which rectangles did that
  end up occupying?* I went looking for that on `ComponentConfig`, on
  `UIComponent`, on the render info, anywhere:

  ```cpp
  // hoped for something like
  auto rects = ui::text_rects_for(entity, Range{start, end});
  ```

  There is nothing. The layout that answers it happens inside
  `draw_text_in_rect` and is discarded the moment the glyphs are drawn.

- **What happened.** The only way to answer is to redo the layout the renderer
  just did and hope the two agree. `src/ui/find_highlight.h`, ~70 lines:

  ```cpp
  const auto measure = [&](const std::string& s) {
      return measure_text(font, s.c_str(), fontPx, 1.0f).x;
  };
  const auto lines = ui::detail::wrap_text_to_width(text, rect.width - 10.f,
                                                    measure);
  const float lineH  = measure_text(font, "Ag", fontPx, 1.0f).y;
  const float blockH = lineH * lines.size();
  const float y0     = rect.y + std::max(0.f, (rect.height - blockH) * 0.5f);
  //  … then per line:  x = rect.x + 5 + measure(line.substr(0, off))
  ```

  It works — verified against a real capture: with the query at the start of a
  line the band's left edge lands 3px before the first ink, which is the glyph
  bearing, and the bands sit on the right lines. **Credit where due: it works
  only because the wrapping primitive is public.** Calling
  `ui::detail::wrap_text_to_width` with afterhours' own measure function means
  the break decisions are identical by construction rather than by imitation —
  had I re-implemented greedy wrapping, this would have drifted apart on the
  first word longer than a line.

  What is copied, though, is arithmetic, and every line of it is a private
  detail of `rendering.h` (~655-700):

  | copied | why |
  |---|---|
  | `rect.width - 10` | the soft-wrap inset |
  | `measure("Ag").y` | the wrapped-line pitch |
  | `rect.y + (rect.height - blockH)/2` | wrapped blocks are vertically centred |
  | `rect.x + 5` | the left text margin |

  Change any one of those upstream — a different inset, top-aligned wrapped
  text, a padding-aware margin — and hanabi's highlights silently slide off the
  words with nothing failing to build. It is the most fragile code in the app,
  and it cannot be made less fragile from this side.



**Hanabi reference.** `src/ui/find_highlight.h` (`where is byte N of this label on screen`) — hanabi redoes afterhours text layout with copied renderer constants for find bands. `src/ui/link_detect.h` (`calls text_select::detail::layout_of`) — link rects share the same app-side reconstructed text layout. Tests: `tests/ui/find_counts_only_what_it_could_paint.e2e` (`Nothing is counted that find could not paint.`) — script guards count-vs-paint consistency.

- **What the library could offer instead.** The renderer already computes this;
  it just throws it away. Two shapes, either would do:

  1. **Ask afterwards.** Cache the laid-out lines on the entity when it draws
     text (they are already computed), and expose:

     ```cpp
     // rects in screen space for a byte range of the element's label
     [[nodiscard]] std::vector<RectangleType>
     text_rects(const Entity&, size_t begin, size_t end);
     ```

     `text_selection.h` already has `selection_rects` doing exactly this
     arithmetic — it needs the lines and the origin, and the renderer is the
     only thing that knows them.

  2. **Let the caller draw into the layout.** A hook that runs with the
     resolved lines in hand:

     ```cpp
     .with_on_draw_text_background([](const TextLayout& layout) {
         for (auto r : layout.rects_for(begin, end)) draw_rect(r, band);
     })
     ```

     which also covers the other things apps want behind text — a spell-check
     underline, a diff tint, an inline code pill (gap #22's real fix).

  Either one turns 70 lines of copied constants into three, and makes
  **selection (#37) mostly free** — selection is this plus a drag, and the two
  should share one answer to "where did byte N land".

  Smaller, and worth doing regardless: those four constants could be **named
  and public** — `ui::text_metrics::kWrapInset`, `kTextMarginX`, and a
  `line_height(font, size, spacing)` — so a caller that must reproduce the
  layout at least breaks loudly when one changes, instead of silently.

- **Severity: makes it ugly, and fragile.** The feature shipped and looks
  right. It is one upstream layout tweak away from being wrong in a way no test
  I can write here would catch.

## #37 revisited — I built the selection, here is what it cost

Gap #37 above asks for text selection on read-only text. Rather than leave it
as a request, hanabi now has a working app-side implementation
(`src/ui/text_select.h`, ~230 lines), so the ask can be concrete about what a
library version would save. **It works**: drag across a message and the run
highlights, character-snapped, across soft-wrapped lines; Cmd+C copies exactly
the dragged bytes; a press elsewhere drops it. Verified against real captures —
a drag from x=343 to x=430 paints a band at x=340..427, snapped to the
character boundaries either side.

What it took, in the order the problems arrived:

1. **Reproducing the layout** (gap #51). Same as the find highlight: call
   `ui::detail::wrap_text_to_width` with afterhours' own measure function so
   the breaks agree, then copy the wrap inset, the line pitch, the vertical
   centring and the left margin out of `rendering.h`. ~40 lines, four constants
   that can silently change upstream.

2. **Point → byte offset.** `text_selection::byte_offset_at` exists and is
   exactly right, but it takes `std::vector<detail::TextRunLine>` and the
   wrapping I can reach returns `std::vector<std::string>` — so its 20 lines
   are re-written here against plain strings. Two functions that should be one.

3. **Wrapped offsets do not index the source.** `joined_text()` warns about
   this and it bites immediately: a soft wrap CONSUMES the space it broke at,
   so an offset into the joined lines is one byte short per break, and a
   selection dragged past a line end copies text shifted left by the number of
   lines crossed. The fix is to walk the source string finding each wrapped
   line in turn and skipping the whitespace between — ~20 lines whose only job
   is to undo an information loss the wrapper could have avoided by returning
   each line's source offset.

4. **Hit-testing had to be hand-rolled.** The context's hot element is resolved
   by a system that has not seen this frame's tree when the transcript is
   building it, so on the frame the button goes down every element reports last
   frame's answer — and a press that arrives with the pointer already in place
   (exactly what a synthetic drag does, and what a fast real one does too) is
   missed. So the element hit-tests the pointer against its own rect. That in
   turn needs the rect, which is last frame's, which is fine only because text
   does not move between frames.

**What a library version would look like**, given all of the above already
exists inside the renderer:

```cpp
.with_selectable_text()                       // opt in, default off

// after the frame resolves
[[nodiscard]] std::string  UIContext::selected_text() const;
[[nodiscard]] bool         UIContext::has_selection() const;
```

with the press/drag handled where hot is already known, the rects taken from
the layout the renderer just did (#51), and the offsets indexing the ORIGINAL
string. The app side then goes from 230 lines to one call, and — the part that
matters — stops depending on four private constants and one documented
information loss.

Two things I would add to the original ask, having built it:

- **Double-click a word, triple-click a line** — now built here too, and it
  needed one more thing worth naming: **the pointer state carries no click
  count.** `MousePointerState` has `pos`, `left_down`, `just_pressed`,
  `just_released`, `press_pos`, `press_moved` — everything except how many
  presses this one is. So the app keeps its own last-press time and position and
  re-derives the run (400ms, 4px), which is a rule the toolkit should own: every
  widget that wants a double-click will otherwise pick its own thresholds and
  they will not match each other. A `click_count` on the pointer state is a
  handful of lines where it is already tracking `press_pos`, and the e2e
  `double_click`/`triple_click` commands — which are simply two and three
  presses — would then mean the same thing to every app.
  The text-input spec in this file already wants both (§4), and one
  implementation serves both widgets.
- **`joined_text` should come with offsets.** Whatever the selection API ends
  up being, a wrapped line wants to carry where it started in the source. Every
  caller that maps a screen position back to real bytes needs it, and every one
  of them will otherwise write the same 20-line reconstruction — badly, because
  the failure is a quiet off-by-N that only shows up on wrapped text.

Still deliberately NOT built, and still the library's call: **selection across
elements**. Dragging from one message into the next needs a document order over
the widget tree, which is a real design question and a much bigger one than
this. Within an element covers grabbing a value, a path, or a sentence, which is
the thing people actually do.

### #52 — Selection across elements needs a document order (deliberately not built)

Filing this separately from #37 so it can be judged on its own, because it is
the one piece of selection I chose NOT to build and I do not think an app
should.

- **What is missing.** Selection in hanabi lives inside one rendered element:
  one line of an assistant turn, or one user bubble. Dragging from the middle
  of one message into the next selects only within the one the press landed
  on. A real transcript reader eventually wants to drag across three messages
  and take the lot.

- **Why the app cannot do it.** The pieces an app can reach are per-element:
  each text element knows its own rect and its own string. To select across
  them you need to know, for two arbitrary elements, **which one comes first in
  reading order** — and then, for every element between them, that it is fully
  covered. That is a traversal of the widget tree in paint order, with the
  scroll transform applied, skipping anything that is not text and anything
  clipped away. An app can approximate it by sorting rects top-to-bottom, and
  that approximation breaks the moment there are two columns (hanabi's split
  view is exactly that), or a floating overlay, or an element that wraps.

  It is also not composable with what exists: `text_selection.h` is written
  around one text block's lines, and `Selection`'s anchor/cursor are byte
  offsets into a single string.

- **What the library could offer.** The tree, the paint order and the clip
  rects are all already known to the renderer. A minimal shape:

  ```cpp
  // Elements that opted into selection, in document order.
  [[nodiscard]] std::vector<EntityID> ui::selectable_order();

  // The whole selection, across however many elements it spans.
  struct SelectionSpan { EntityID element; size_t begin, end; };
  [[nodiscard]] std::vector<SelectionSpan> UIContext::selection_spans() const;
  [[nodiscard]] std::string UIContext::selected_text() const;  // joined
  ```

  With `selectable_order()` alone an app could do the rest. The full version
  belongs upstream because the join rule (a newline between blocks? between
  wrapped lines?) should be one decision, not one per app.

- **Severity: makes it ugly.** Per-element selection covers the common case —
  grabbing a value, a path, a sentence. The cross-element version is a
  completeness thing, and it is worth doing properly once rather than
  approximately in every app.

**Hanabi reference.** None — no app-side workaround is implemented.



### #53 — A layout an app declares wrong is corrected silently, and warned about forever

- **How I hit it.** Twice in one afternoon, in a 34px-tall bar with five
  children. Both times the app looked *fine* and I only found out by reading
  the log while chasing something else:

  ```
  [WARN] Layout wrap: 'find_close' in parent 'find_bar' - NoWrap set but would
         overflow (child_size=26.0, offset=296.0, container=306.0)
  [WARN] Layout wrap: 'text_input_field' in parent 'find_input' - NoWrap set
         but would overflow (child_size=168.0, offset=0.0, container=158.0)
  ```

  The first: I sized a `NoWrap` row by eye and its children came to 16px more
  than it. The second is subtler and worth naming, because it is a trap rather
  than an arithmetic slip — **`text_input`'s inner element is sized to the
  element's OUTER width**, so giving the field `with_padding(...)` makes the
  child wider than the content box it sits in. Padding is the natural way to
  inset text in a field, it is what every other widget here wants, and on
  `text_input` it is always wrong.

- **What it actually costs — measured, because I assumed wrong first.** I
  claimed in a commit message that this is expensive: afterhours detects the
  violation and then pays `solve_violations` to fix it every frame. **That is
  not true and I should not have written it without measuring.** Same scene,
  same 240 timed frames, overflowing versus fixed:

  ```
  overflowing:  median 1.39 ms/frame
  fixed:        median 1.45 ms/frame
  ```

  No measurable cost — if anything the overflowing build is a hair faster, well
  inside noise. `fix_violating_children` distributes the error in one pass now
  and the loop exits immediately, so the correction is genuinely cheap. Good.

  What it does cost:
  - **570 log lines per 285 frames** (~98 KB in one short run), two a frame,
    forever, on stdout mixed in with ordinary logs. That is enough to drown any
    real diagnostic, which is precisely how both of these survived so long —
    the app had learned to ignore its own log.
  - **A layout that is not the one the author wrote.** The correction shrinks
    children to fit, so the rendered result is *plausible* — my two bugs moved
    the bar's contents by about 12px and nobody noticed for a week. Silence
    would be better than plausible-but-wrong; a visible break would be better
    still.



**Hanabi reference.** Negative result: `scripts/bounds_gate.sh` (`assert_no_overflow measures every element against the VIEWPORT`) — hanabi has its own parent-containment audit because the library assertion checks the wrong relationship. `scripts/bounds_gate.sh` (`fail on anything not in the baseline`) — current gate freezes known harmless overflows and fails on new parent escapes. Measurement/gate: `scripts/bounds_gate.sh` (`HANABI_BOUNDS_AUDIT=1`) — measurement mode reads resolved rects after layout rather than relying on screenshot/log spam.

- **What the library could offer instead.** The detection is already there and
  correct — it is the reporting that fails:

  1. **Warn once per (element, parent, reason), not once per frame.** A
     `log_warn_once` keyed on the debug names would turn 570 lines into two,
     and two lines an app author will actually read. This is the whole fix, and
     it is a few lines.
  2. **Make `text_input` reject or absorb padding.** Either honour it by
     insetting the inner element (what a caller means), or refuse it with a
     compile-time or startup error. Silently producing a child wider than its
     parent is the worst of the three.
  3. **A debug mode that renders the violation** — the classic red overflow
     stripe. `debug_wrap` already exists as a per-child flag; a global
     `theme.show_layout_violations` would make these findable by looking rather
     than by grepping.

- **Severity: makes it ugly.** No frame cost — I checked, and I was wrong to
  say otherwise. The real damage is that the log is unusable as a signal, so an
  app tripping this learns nothing until someone reads 98 KB of warnings by
  accident.

### #54 — `check_single_action_impl` takes an injected key reader, then ignores it for modifiers

- **How I hit it.** `tests/unit/test_input_pipeline.cpp` is deliberately
  backend-free: it hands `check_single_action_impl` its own `key_check` lambda
  so a key mapping can be asserted with no graphics backend at all. That works,
  the test passes — and it emits **72 lines of `[ERROR] @notimplemented
  is_key_down`** doing it.

- **Why.** The function accepts `KeyCheckFn key_check` and honours it for the
  chord key (`input_system.h:939`). But its very first statement is

  ```cpp
  uint8_t current_mods = get_current_modifiers();   // :926
  ```

  and `get_current_modifiers()` (`:862`) calls the **static backend**
  `is_key_down` eight times — both shifts, controls, alts and supers — with no
  way to redirect it. With no backend registered those land on the stub that
  logs an error. Nine assertions x eight modifier probes = exactly 72 lines.

- **Why it matters beyond the noise.** The injection point is advertised but
  incomplete: a caller can control what "is this key down" means for the chord
  and *cannot* control it for the modifiers. So a backend-free test can assert
  `Cmd+B` resolves — but never that it resolves **only while Cmd is held**,
  which is the half of a chord worth testing. It is also why the errors cannot
  be silenced app-side: there is no seam to silence.

- **Severity: makes it ugly, and caps what a test can prove.** Nothing is
  wrong on screen; the mapping resolves correctly in the real app because a
  real backend is registered.

- **Minimal upstream fix:** give `check_single_action_impl` the modifier source
  too — either take a `ModifierFn` defaulting to `get_current_modifiers`, or
  derive the modifiers from the `key_check` it was already handed, which needs
  no new parameter and makes the injected reader mean one consistent thing:

  ```cpp
  // instead of get_current_modifiers()
  uint8_t current_mods = modifiers_from(key_check);
  ```

  Then a chord test can hold Cmd, and the 72 lines go away as a side effect.

**Hanabi reference.** None — no app-side workaround is implemented.



### #55 — A scripted test can right-click a coordinate, never a named element

- **What I was trying to build.** The session-rename theme: right-click a
  sidebar row, pick "Rename…", type, press Return. The context menu is the
  entry point, so the test has to open it the way a person does.

- **What I tried.**

  ```
  right_click_ui chat_row
  ```

- **What happened.** `right_click_ui` is not a command. The harness has
  `click_ui`, `focus_ui`, `double_click_ui`, `triple_click_ui` and
  `assert_ui`, all resolving a `debug_name` to the element's live rect — but
  the secondary button has only the raw-coordinate form,
  `right_click x y` (`e2e_testing/command_handlers.h:156`). The UI side is
  complete: `UIContext::is_right_click(id)` exists and is exactly the API a
  context menu wants. Only the driver half is missing.

- **The workaround.** Hardcode the row's pixel position:

  ```
  mouse_move 120 511
  right_click 120 511
  ```

  which is what `tests/ui/session_rename.e2e` does. It costs the property the
  harness's own README sells `click_ui` for: a layout change now retargets the
  click onto whatever moved into 120,511 — a different row, or empty sidebar —
  and the test keeps passing while exercising the wrong thing (or fails for a
  reason that has nothing to do with rename). The menu ITEM is safe, because
  once the menu is open `click_ui row_menu_rename` finds it by name.



**Hanabi reference.** `tests/ui/session_rename.e2e` (`right_click 120 475`) — rename test still hardcodes the secondary-click coordinate. `tests/ui/session_archive.e2e` (`right_click 120 475`) — archive test carries the same coordinate workaround. Tests: `tests/ui/session_rename.e2e` (`click_ui row_menu_rename`) — menu item can be addressed by name after coordinate right-click opens the menu.

- **What the library could offer instead.** The same handler `click_ui`
  already has, dispatching `simulate_right_click` instead of
  `simulate_click` — the element lookup, the centre-of-rect maths and the
  failure message are all written. Roughly:

  ```cpp
  // ui_commands.h, beside HandleClickUiCommand
  if (cmd.is("right_click_ui")) { ... test_input::simulate_right_click(cx, cy); }
  ```

- **Severity: makes it fragile.** The feature is testable; the test just
  cannot be written the robust way, so every context menu in every app that
  vendors this gets a coordinate-keyed test.

### #56 — A newly built text_input cannot be focused programmatically

- **What I was trying to build.** A rename modal whose field opens focused
  with the current title selected, so Return renames and the first keystroke
  replaces the old name. That is the ordinary behaviour of every rename dialog,
  and `text_input` already implements the selecting half: focus gained by
  anything other than a mouse press selects the whole value
  (`text_input/component.h:245`).

- **What I tried.** Focus the widget on the frame the modal opens, using the
  entity the imm call hands back:

  ```cpp
  auto field = imm::text_input(ctx, mk(panel, 2), draft, cfg);
  if (justOpened) ctx.set_focus(field.ent().id);
  ```

- **What happened.** The field never took focus; typing went nowhere and
  `expect_focused rename_input` failed. Two separate reasons, and each on its
  own is enough:

  1. `text_input` returns its OUTER entity, and the `HasClickListener` lives on
     the inner FIELD child. `can_be_focused` requires a click or drag listener,
     so the outer is never added to `focused_ids` by `HandleTabbing` — and
     `EndUIContextManager` then drops any focus id that is not in that set. So
     focus set on the returned entity survives exactly until the end of the
     frame it was set in.
  2. Focusing the inner field instead is still not enough on the OPENING frame:
     `can_be_focused` also requires `was_rendered_to_screen`, which a widget
     created this frame does not have yet. The grab only becomes possible on
     the following frame.

- **The workaround.** Walk the outer entity's children for the one carrying
  `InFocusCluster` (the field), and re-assert focus for a few frames rather
  than once:

  ```cpp
  if (justOpened) focusFrames_ = 3;
  if (focusFrames_ > 0) { --focusFrames_; ctx.set_focus(focusable_field(field.ent())); }
  ```

  It works, and it is guesswork: the app is reaching past the returned handle
  into the widget's internal child structure and papering over a one-frame
  ordering rule with a counter.



**Hanabi reference.** `src/ecs/rename_modal_system.h` (`if (justOpened) focusFrames_ = 3`) — rename modal reasserts focus across several frames. `src/ecs/rename_modal_system.h::ctx.set_focus(focusable_field(field.ent()))` — workaround focuses the text_input's inner field rather than the returned wrapper. Tests: `tests/ui/session_rename.e2e` (`The field opens focused with the whole title selected`) — script relies on autofocus/select-all to type the replacement title.

- **What the library could offer instead.** Either make `set_focus` on the
  entity `text_input` returns mean "focus this input" (route it to the field,
  the way `state.is_focused` already accepts focus on EITHER entity), or give
  the widget a config knob — `with_autofocus()` / `with_focus_on_open(bool)` —
  which is what the caller actually wants to say. A widget that cannot be
  focused by the code that just created it can only be reached with a mouse.

- **Severity: makes it ugly.** The modal works, but four lines of the app now
  encode two of the library's internal invariants, and they will break silently
  if either changes.
### #57 — `text_input` blurs itself on Escape, and focus can only be given back to a child you were not handed

- **How I hit it.** The composer's slash-command menu (`/new`, `/model`, …)
  needs Escape to mean "put the menu away" while the caret stays in the field —
  the next keystroke is the rest of the message. Escape does close the menu,
  and the draft survives; the caret does not.

- **Why.** `text_input`'s own key handling ends with

  ```cpp
  // ui/text_input/component.h:718
  if (ctx.pressed(InputAction::MenuBack)) {
    state.clear_selection();
    ctx.set_focus(ctx.FAKE);
  }
  ```

  unconditionally, with no config flag and no listener hook to veto it. The
  widget has already run by the time an app system sees the same keystroke, so
  the app is always answering a blur that has happened.

- **What I tried, in order.**
  1. `ctx.set_focus(inputRes.ent().id)` right after the widget, in the same
     frame. No effect. The id `imm::text_input` returns is the WRAPPER; the
     focusable element is the inner field it creates as a child, and
     `EndUIContextManager` resets `focus_id` to `ROOT` at end of frame unless
     that exact id is in `focused_ids` — which `HandleTabbing` fills with the
     field, never the wrapper. So the assignment is silently discarded one
     system later.
  2. `ctx.try_to_grab(id)` before `set_focus` to get the wrapper into
     `focused_ids`. Focus then held for exactly as many frames as I kept
     re-asserting it and dropped the frame I stopped — the wrapper is not what
     the tab pass re-grabs, so nothing sustains it.
  3. Holding the re-focus for three frames with a countdown, because the
     scripted Escape stays down for two. Same ending, plus a frame counter in
     app state that exists only to fight the widget.
  4. **What shipped:** reach into the widget's internals from the app —

     ```cpp
     const auto& kids = inputRes.ent().get<afterhours::ui::UIComponent>().children;
     ctx.set_focus(kids[0]);   // the field, by position
     st.was_focused = true;    // else the widget reads it as a fresh focus
     ```

     `was_focused` is the second half: a field that gains focus without a mouse
     press selects all its text (`component.h:246`), so without it the next
     character typed replaces the whole draft instead of appending to it.

- **Cost of the workaround.** The app now depends on two things the widget
  never promised: that child 0 of a `text_input` is the focusable field, and
  that `HasTextInputState::was_focused` may be written from outside. Both are
  a refactor away from breaking silently — the failure mode is a caret that
  quietly stops taking keystrokes, which no assertion outside a scripted UI
  test would catch.

- **Severity: caps what an app can build.** Any composer-anchored affordance —
  a slash menu, an @-mention list, an inline autocomplete — needs Escape to
  dismiss ITS thing without dismissing the field, and needs to hand focus back
  after a click on one of its rows. Neither is expressible today.

- **Minimal upstream fix**, either half of which would have been enough:
  1. `ComponentConfig::with_escape_blurs(false)` (or a `HasTextInputListener`
     `on_escape` that can return "handled"), so the app decides what Escape
     means while its own overlay is up.
  2. Return the field's id alongside the wrapper's — `ElementResult` already
     carries an entity, so a `focus_target()` accessor (or making
     `ctx.set_focus(wrapper)` forward to the field) would make "put the caret
     back" a one-liner that cannot rot: today the only way to say it is to
     index into the widget's children.

**Hanabi reference.** `src/ecs/main_pane_system.h` (`const auto refocus_field = [&]`) — composer reaches into widget children to restore focus. `src/ecs/main_pane_system.h` (`was_focused = true`) — workaround preserves caret behavior after restoring focus. Tests: `tests/ui/slash_menu_escape_keeps_draft.e2e` (`Escape puts the slash menu away and keeps what you typed.`) — script guards Escape menu dismissal without losing draft/focus.



### #58 — No colour input of any kind, so a theme editor can only offer a menu

- **What I was trying to build.** The custom theme editor from
  `docs/breakdown/search-settings-shortcuts.md`: a row of colour swatches for
  the palette's named tokens, each one opening a picker so a user can choose an
  arbitrary colour, with a live preview of the result.

- **What I tried.** Looked for anything in `vendor/afterhours/src/plugins/ui/`
  that takes a colour from a user: an `imm::` colour widget, a hue/saturation
  surface, a slider group I could compose three of, a hex text field with a
  swatch preview. `imm_components.h` offers `div`, `button`, `checkbox`,
  `checkbox_group`, `slider`, `dropdown`, `navigation_bar`, `pagination`,
  `text_input`, `icon_row` — no colour widget, and no way to raise the OS
  picker either (the library has no native-dialog surface at all, by design).

- **The nearest composition, and why I did not ship it.** Three `slider`s (R,
  G, B) plus a `div` filled with the result is buildable today. It costs an
  editor that is three unlabelled sliders per token — the breakdown asks for
  eleven tokens, so thirty-three sliders — and it would hand the user a colour
  space with no guard rails, on an app whose light theme already shipped once
  as "muddy grey sidebar, no card contrast, not shippable". A slider can pick
  `#2b2b30` for `text_primary` on the dark palette, and the app is then a blank
  window with no way back except editing settings.json by hand.

- **The workaround, and its cost.** Named swatches instead of free colour: the
  editable set is the two DECORATIVE token families (accent, find highlight),
  each offered as four named choices that carry a dark colour AND a light one
  (`src/ui/theme.h`). The cost is real — nobody can dial in their own brand
  colour, and adding one means shipping a build. What it buys is that every
  reachable combination has been looked at on both palettes, and no choice can
  produce an unreadable pane.

- **Severity: caps what an app can build.** Any app with theming, a drawing
  tool, a chart editor or a tag-colour setting hits this the moment it wants a
  colour from a person rather than from a constant.



**Hanabi reference.** `src/ecs/settings_system.h` (`Two NAMED tokens are editable`) — settings uses named swatches rather than a free color picker. `src/ecs/settings_system.h` (`What this is not is the colour-picker`) — source documents why the richer picker is not shipped. Tests: `tests/ui/custom_theme_colours.e2e` (`The two editable colour tokens change`) — script covers swatch selection.

- **Minimal upstream fix.** An `imm::color_swatch(ctx, id, Color& value, cfg)`
  — a button that fills with the current colour and, when pressed, opens a
  library-drawn popover with a hue strip, a saturation/value square and a hex
  field, writing back through the reference the way `slider` already writes a
  float. It needs no platform code and no new backend surface: it is the
  existing popover, rect-fill and text-input machinery arranged into the one
  widget a themeable app cannot do without.
### #59 — `assert_ui` cannot assert a property whose value contains a space

- **How I hit it.** Drag-to-reorder is a claim about ORDER, and the only thing
  that can carry it is position: after the drag, the row that was sixth has to
  be the one at the top of the band. The obvious spelling is

  ```
  assert_ui row_title text="Draft release notes for 4.2"
  ```

  which addresses the first row by its debug name and asks what it says.

- **What happened.** `assert_ui` is not in any of the runner's argument
  categories (`runner.h` `coord_commands` / `single_arg_commands` / …), so it
  falls through to the catch-all parser, which splits the rest of the line on
  whitespace and never looks at quotes. The command therefore sees
  `text="Draft`, `release`, `notes`, … — it reports
  `text=""Draft" but got "Draft release notes for 4.2"`, and the two trailing
  words are separate malformed assertions. Every other text-bearing command
  (`expect_text`, `assert_ui_text`, `expect_input_text`) has an explicit branch
  that calls `parse_quoted()`; `assert_ui` is the one that does not.

- **The workaround.** Turn the assertion inside out and use `assert_ui_text`,
  which DOES parse a quoted first argument: find the element by its LABEL and
  assert its geometry.

  ```
  assert_ui_text "Draft release notes for 4.2" y=382
  ```

- **Cost of the workaround.** It only reads well when the label is unique and
  the property is a number — which happens to be exactly this case, so the test
  is arguably clearer for it. But the two commands are now addressed by
  different keys (one by debug name, one by label) for no reason a reader can
  see, and any assertion that genuinely needs BOTH — "the element named X says
  Y" for a label with a space in it — cannot be written at all. A label is not
  a stable handle the way a debug name is: it is the thing under test.

- **Severity: makes it ugly.** Nothing is unreachable here, and the failure is
  loud rather than silent, which is the saving grace: a malformed assertion
  fails the script instead of passing vacuously.



**Hanabi reference.** `src/ecs/main_pane_system.h` (`No spaces: assert_ui name text=<value> splits its arguments on whitespace`) — syntax audit captions avoid spaces because assert_ui cannot parse them. `tests/ui/pinned_threads_head_the_sidebar.e2e` (`assert_ui row_title text=kicker-tick`) — current direct text property assertions use single-token values. Tests: `tests/ui/syntax_highlighting.e2e` (`assert_ui code_block_audit_PYTHON text=kw2/ty0/str2/com1/num4`) — audit caption is encoded as one token to fit assert_ui.

- **Minimal upstream fix.** Add `assert_ui` (and `assert_no_overflow`'s
  siblings, if any grow arguments) to the runner's parse switch with the same
  shape `assert_ui_text` already has: read the name, then `parse_quoted()` each
  remaining `prop=value` so a quoted value survives. Two lines next to the
  `assert_ui_text` branch.
---

### #60 — sokol's drag-and-drop is built into the backend and cannot be turned on

- **What I was trying to build.** Dropping an image from Finder onto the
  composer. sokol_app already has this: `sapp_desc.enable_dragndrop`, then
  `SAPP_EVENT_TYPE_FILES_DROPPED` with `sapp_get_dropped_file_path(i)`. Every
  platform sokol supports implements it, so an app that could ask for it would
  get drops on macOS, Windows and Linux from one code path.

- **What I tried.** Setting it through the graphics `Config` hanabi already
  fills in (`width`, `height`, `title`, `target_fps`, `flags`) — the same place
  `FLAG_WINDOW_RESIZABLE` goes.

- **What happened.** There is nothing to set. `MetalPlatformAPI::run()`
  (`vendor/afterhours/src/backends/sokol/backend.h`) builds its `sapp_desc`
  privately and hard-codes the whole thing: callbacks, `high_dpi`,
  `sample_count`, `enable_clipboard`, `clipboard_size`. `enable_dragndrop` is
  never mentioned, `Config` carries no field for it, and the only flag the
  backend reads (`FLAG_WINDOW_RESIZABLE`) it then ignores as a no-op. The
  event enum afterhours translates sokol events into has no dropped-files
  member either, so even a flipped flag would have nowhere to arrive.

- **The workaround, and what it costs.** hanabi installs its own AppKit
  dragging destination in `src/native_extras.mm` (section 6): a category on
  `MTKView` adding the `NSDraggingDestination` methods, plus
  `registerForDraggedTypes:` on the window's content view, feeding a queue the
  frame loop drains. It works — the install logs the registration and the
  content view is sokol's `MTKView` — but the costs are real:

  1. **macOS only.** sokol's version would have been every platform at once;
     this one is AppKit, so a Linux or Windows hanabi has no drop at all.
  2. **It reaches around the backend to a view it does not own.** The
     category assumes the content view is an `MTKView` — true today because
     sokol makes it one, and checked at install time rather than assumed, but
     it is a fact about somebody else's implementation.
  3. **The other two ways in are worse.** Replacing the window's delegate
     takes sokol's own resize/close callbacks away from it; an overlay
     `NSView` registered for dragged types has to be hit-testable to be found
     as a drag destination, and a hit-testable view over the content view eats
     every mouse event the UI needs.
  4. **Untestable in the harness.** A drag is not in the widget tree, so the
     scripted test drives the queue directly (`native_simulate_file_drop`) and
     the AppKit delivery itself is verified by hand.



**Hanabi reference.** `src/native_extras.mm` (`sokol's own drag-and-drop is unreachable from here`) — AppKit category workaround is documented beside the native implementation. `src/native_extras.mm` (`void native_filedrop_install`) — window content view is registered as an NSDraggingDestination. Tests: `tests/ui/composer_image_attach.e2e` (`HANABI_DROP_TEST feeds the pending-drop queue the way AppKit does`) — script covers everything downstream of the actual AppKit drag.

- **Minimal upstream fix.** One bool on `Config` (`enable_file_drop`) copied
  into `desc.enable_dragndrop`, and the dropped-files event surfaced the way
  the other sokol events already are — a `files_dropped` callback on `Config`,
  or a `get_dropped_files()` the frame can poll. Everything underneath it is
  already written and shipping inside sokol_app.
### #61 — A scripted test can assert a rect and a string, never a colour

- **What I was trying to build.** Syntax colouring in fenced code blocks
  (`src/ui/syntax_highlighter.h`): keywords one hue, strings another, comments
  grey. Colour is the entire feature — the text on screen is byte-for-byte what
  it was before — so the test has to assert colour or it asserts nothing.

- **What I tried.**

  ```
  assert_ui code_block_line color=syntax_keyword
  assert_ui code_block_line spans=3
  ```

- **What happened.** `check_ui_property` (`e2e_testing/ui_commands.h:1013`)
  understands exactly five properties: `x`, `y`, `w`, `h`, `hidden`, and
  `text`. Anything else is `unknown property`. `text` compares against
  `HasLabel::label`, which for a styled label is the concatenated plain string
  — so the one property that can see a styled element cannot see the styling.
  `HasLabel::spans` sits right beside `label` in the same component, holding a
  `TextSpan{text, color, weight}` per run, and nothing in the harness reads it.

  A second, smaller edge of the same gap: `assert_ui` splits its arguments on
  whitespace (`cmd.args`), so `text=kw 2 ty 0` cannot be expressed at all. Any
  value a test wants to assert has to be a single token.

- **The workaround.** A test-only audit caption, the same shape as the find
  bar's band counter: `HANABI_SYNTAX_AUDIT=1` makes each code block render a
  count of the coloured runs it handed to the renderer, spelled without spaces
  (`kw2/ty0/str2/com1/num4`), and the test asserts that string. The counts are
  filled in by the span builder itself, so they cannot drift from the draw —
  but they are still a caption ABOUT the colouring rather than the colouring,
  and they only exist because the harness cannot read what it renders. The
  shipping build carries the branch and never the caption.

- **Severity: makes it untestable.** Every colour-carrying feature — syntax
  highlighting, a diff view's red and green, a status hue, the find band — is
  in the same position: assertable only through a hook the app adds for the
  purpose. Three of hanabi's features now carry one.



**Hanabi reference.** `src/ecs/main_pane_system.h` (`HANABI_SYNTAX_AUDIT caption`) — code block renders a test-only text summary of colored runs. `src/ecs/main_pane_system.h` (`No spaces: assert_ui name text=<value> splits its arguments on whitespace`) — audit summary is intentionally whitespace-free. Tests: `tests/ui/syntax_highlighting.e2e` (`HANABI_SYNTAX_AUDIT=1`) — script asserts run-count captions because colors are not directly assertable.

- **Minimal upstream fix.** Teach `check_ui_property` two more properties:

  ```cpp
  else if (prop == "spans") { actual_int = label.spans.size(); }
  else if (prop.starts_with("span")) {   // span0_color=#c695ea
      ...  compare TextSpan::color, hex or r,g,b
  }
  ```

  and let a quoted `prop="value with spaces"` survive argument splitting. The
  data is already in the component; only the reader is missing.

### #62 — Styled spans are placed run by run, so a monospace block loses its columns

- **What I was trying to build.** The same code blocks. Each line is one mono
  label, and the colours arrive as `with_styled_label(spans)`.

- **What happened.** The runs are measured and drawn one after another, each
  advancing x by its own measured width. In proportional text nobody sees the
  fractional pixel that rounds off at each boundary. In a MONOSPACE block the
  columns are the point: `min(base * 2 ** attempt, 30000)` drawn as one label
  and the same line drawn as seven coloured runs do not line up, and a block
  whose lines have different run counts wobbles column by column against
  itself. Screenshot evidence: colouring punctuation put a visible half-space
  before every comma and closing paren.

- **The workaround.** Colour fewer boundaries: punctuation is deliberately NOT
  coloured, which removes the most frequent split (the syntax palette carries a
  `punct` token that the scanner never emits — kept because the omission is a
  workaround, not a design). Keywords, strings, comments and numbers are almost
  always bounded by spaces, where a fraction of a pixel does not read as a
  broken column. The wobble is reduced, not gone.

- **Cost of the workaround.** Punctuation-heavy languages get less colour than
  they should, and the residual drift is still visible in a dense expression.
  The alternative — hand-drawing every code line at exact `advance * column`
  offsets through `on_draw_fg` — would cost the label its measurement, its
  overflow handling and its text selection, for a feature that is legibility
  polish.



**Hanabi reference.** `src/ui/syntax_highlighter.h` (`What it cannot do it leaves as plain text.`) — scanner deliberately leaves unsupported/plain runs uncolored. `src/ecs/main_pane_system.h` (`case hanabi::syntax::Tok::Punct: return theme::syntax_punct();`) — punctuation coloring is intentionally skipped to reduce monospace run-boundary wobble. Tests: `tests/ui/syntax_highlighting.e2e::kw2/ty3/str0/com2/num2` — script proves colored syntax runs are present, but not punctuation-level styling.

- **Minimal upstream fix.** Lay a styled label out on the ORIGINAL string's
  measured positions rather than by accumulating per-run widths: measure the
  full label once, then draw each run at the offset its byte range has in that
  measurement. That is the same trick `find_highlight.h` performs from outside
  to place a band, so the geometry is known to be available.

---

### #63 — A container cannot draw over its own children

**What I was building.** The minimap rail (`src/ui/minimap.h`): a thin strip of
per-item marks with a scrubber over them saying where the viewport currently
sits. The rail is one element; each mark is a child of it, so a click lands on
the item it is drawn over. The scrubber belongs to the rail — it is about the
whole strip, not about any one mark.

**What I tried.** Drew the scrubber from the rail's own `on_draw_fg`, which is
documented as "custom draw (on top): enqueued after all of the widget's own
primitives".

**What happened.** It disappeared under the marks. "On top" means on top of
that widget's OWN fill, label and image — the render collector enqueues a
parent's `fg` and only then descends into its children
(`plugins/ui/rendering.h`, `collect()`), so every child draws over the
foreground of the container that holds it. There is one draw order and no way
to ask for a pass after the subtree.

**The workaround, and its cost.** An extra absolutely-positioned child, added
LAST, the same size as the rail, whose only job is to run the draw the parent
wanted to run: `minimap_scrubber` in `main_pane_system.h`. It works, and it
costs an entity per overlay plus a comment explaining why the code does not say
what it means. The real cost is that the ordering is now implicit in the order
the children happen to be built — a child appended after it (a future mark
type, a hover label) silently paints over the scrubber again, and nothing
fails.

**Severity: a papercut with a reliable workaround.** Any widget that wants a
frame, a focus ring, an overlay or a selection band over a subtree it owns hits
this. Every one of them can add a trailing absolute child.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`Where the viewport is, drawn LAST`) — minimap scrubber is a trailing absolute child so it draws over mark children. `src/ecs/main_pane_system.h` (`.with_debug_name("minimap_scrubber")`) — extra overlay entity carries the over-subtree draw. Tests: `tests/ui/minimap_navigator.e2e` (`The rail on the right edge takes you to the message you point at.`) — minimap behavior is covered even though the scrubber itself is drawn.


**Minimal upstream fix.** An `on_draw_over` callback on `HasOnDraw` enqueued by
`collect()` AFTER the child loop, so a container can paint over the subtree it
owns without owning an extra entity to do it. The buffer already carries
per-entity custom draws; this is one more insertion point in the walk that is
already there.

---

### #64 — no window-level chrome: a rule that spans panels cannot belong to any of them

**What the design asks for.** Puffin parts its sidebar from its main pane with
ONE hairline: a single pixel at x=279, colour `#2A2A39`, running the entire
window height — over the tab strip above it and over the footer below it, and
over the selected row it crosses. Not three segments that agree; one rule.

**Why no border can do it.** `with_border_right` exists and is the obvious
answer, and it fails twice over.

1. **A border is clipped to its own panel's rect.** hanabi's frame is four
   absolutely-positioned panels — sidebar, tab strip, main, status bar — and
   they do not tile the window: the sidebar's height is `contentH = h - barH`,
   because the status bar spans the FULL width underneath it. A border on the
   sidebar therefore stops ~24px short of the bottom, and no panel exists whose
   rect is the thing the rule needs to run along.
2. **A parent's border is painted under its own children** — the same ordering
   #63 documents: `collect()` enqueues a parent's primitives and only then
   descends. Every sidebar row is full-width, so the selected row would erase
   the border wherever it crosses. (Puffin's rule draws OVER its selected row.)

**The workaround, and its cost.** A fifth floating entity that belongs to no
panel: a 1px-wide, window-height, absolutely-positioned div parented to the UI
root (`render_rail_divider`, `sidebar_system.h`). 27 lines including the
explanation. The expensive part is not the lines, it is the **render layer**:
the div has to sit above the status bar and the tab strip and below the row
menu, and the only way to learn those numbers is to grep four unrelated systems
for `with_render_layer` and pick an unused integer (7) by hand. Layers are bare
ints with no registry and no names, so this is a global ordering decision made
locally, and nothing detects a future collision.

**Severity: works, but every piece of window chrome pays it.** Anything that is
"of the frame" rather than "of a panel" — a rail divider, a drag-resize handle,
a window-wide focus ring, a drop-target outline — needs its own orphan entity
and its own hand-picked layer.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`void render_rail_divider`) — sidebar/main divider is an orphan root-level absolute entity. `src/ecs/sidebar_system.h::with_render_layer(7)` — divider uses a hand-picked render layer above status/tab chrome. Tests: `tests/ui/composer_reaches_the_window_floor.e2e` (`The composer runs to the window's floor`) — test verifies the frame/footer geometry around the window-level chrome.


**Minimal upstream fix.** Two independent things, either of which helps:
(a) named render layers, or at minimum a `Layer::Chrome`-style enum the app can
extend, so ordering is declared once instead of rediscovered per call site;
(b) a border mode that draws with the parent's foreground pass rather than its
background one, so `with_border_right` survives its own children (this is #63's
`on_draw_over` by another name — one fix would serve both).

### #65 — `text_input`'s inner padding is derived from field HEIGHT and cannot be overridden

- **What I was trying to build.** Puffin's composer input: a 45px-tall outlined
  box whose placeholder starts **10px** in from the left border.

- **What I tried.**

  ```cpp
  text_input(ctx, mk(wrap, 1), draft,
      ComponentConfig{}
          .with_size(ComponentSize{percent(1.f), pixels(45)})
          .with_font_size(theme::type::BODY)          // honoured — see below
          .with_padding(Padding{.left = pixels(10), .right = pixels(10)}));
  ```

- **What happened.** The text landed **17px** in, not 10. `with_padding` is not
  merely ignored — it is *overwritten*. `text_input/component.h` recomputes the
  field's padding from the field's own height every frame and calls
  `set_desired_padding`, unconditionally:

  ```cpp
  float pad_h = field_h * 0.125f;
  float pad_w = field_h * 0.35f;
  field_cmp.set_desired_padding(Padding{...pad_h..., ...pad_w...});
  ```

  At 45px that is a forced 15.75px horizontal inset and a 5.6px vertical one.
  There is no config flag beside it — unlike the font size, which the same block
  now DOES let you win (`config.font_size_explicitly_set ? config.font_size :
  pixels(field_h * 0.5f)`). Half of gap #17 has been fixed upstream; this half
  has not, and the fix makes the remaining half more visible, because a caller
  who has just successfully set a 13px font on a 45px field naturally expects
  the padding beside it to take too.

- **App-code workaround (used).** Work backwards from the constant: the inset I
  want is 10px, `pad_w` is `h * 0.35`, so the FIELD must be 29px tall — and the
  45px box comes from the WRAPPER, which owns the border and the corner radius
  and holds the shorter field inside it. Two coupled magic numbers with a
  comment explaining why one of them is not the number in the design:

  ```cpp
  constexpr float kInputH = 45.0f;   // the box the user sees
  constexpr float kFieldH = 29.0f;   // 10px inset / 0.35 — NOT a design number
  ```

  Cost: 2px still wrong (12 measured vs Puffin's 10, the rest is border AA), one
  derived constant that will silently drift if the 0.35 ever changes, and a
  wrapper div that exists only to hold a border the field could have drawn.

- **Minimal upstream help.** Give padding the treatment font size already got:
  honour an explicitly-set `with_padding` and fall back to the height-derived
  value only when unset. One `if`, symmetric with the line above it.

- **Severity: makes it ugly**, and it is a papercut with leverage — every field
  in the app inherits an inset it did not choose from a height it chose for a
  different reason.

**POSTSCRIPT 2026-08-26 (source-reference audit).** The text_input padding workaround described by the entry is stale for the composer because it moved to text_area; kFieldH survives for a different text_area sizing reason.

**Hanabi reference.** Current code: `src/ecs/main_pane_system.h` (`text_area derives neither -- its padding is a fixed 6/4`) — current composer no longer uses text_input's height-derived padding. `src/ecs/main_pane_system.h` (`const float kFieldH = composer_field_h(composerRows_)`) — one-row field height is now derived for text_area row/padding agreement. Tests: `tests/ui/composer_box_grows_with_the_draft.e2e` (`One row, and this is the number the whole composer band is measured against.`) — current composer field/box sizing is pinned by scripted geometry.



### #66 — A placeholder is a string, so a hint the font cannot draw cannot be drawn

- **What I was trying to build.** Puffin's composer placeholder, verbatim:
  `Message Agentcloud… (↵)`. The return arrow is the whole point of the hint —
  it says which key sends without spending a sentence on it.

- **What happened.** Roboto has no U+21B5 and a missing codepoint paints
  NOTHING (gap #48), so the placeholder renders as `Message Agentcloud… ()`.
  The established fix for that in this codebase is to draw the mark as vectors
  (`hanabi::glyph::chevron`, and now `arrow_up`) — but a placeholder cannot be
  drawn. `with_placeholder(std::string)` hands the widget a string; the widget
  owns the layout, the wrap, the ellipsis and the x-offset, and it never tells
  the caller where it put any of it. There is no per-run styling, no inline
  icon, and no "placeholder rect" to hang an `on_draw_fg` on. The only escape is
  the one gap #17 already documents for the sidebar search: abandon the
  widget's placeholder entirely and re-implement it as an absolutely-positioned
  overlay whose origin you derive by hand from the panel geometry — which means
  re-deriving the very inset that #65 above will not let you set.

- **Cost.** The hint is dropped. hanabi's placeholder is
  `Message hanabi…` and the key that sends is named in words
  ("Enter to send") in the meter row, which is where it already was. That is a
  worse design than Puffin's and it is the library's choice, not hanabi's.

- **Class: IMPOSSIBLE** as specified. Two things would each fix it: a glyph
  fallback chain across the loaded faces (JetBrains Mono covers ⏎ — this is
  gap #48's fourth ask and would make the plain string just work), or a
  placeholder that accepts styled runs the way the transcript's spans do.

**Hanabi reference.** `src/ecs/main_pane_system.h` (`Puffin's reads "Message Agentcloud… (↵)". The key hint is dropped`) — composer placeholder drops the return-arrow hint and explains why. `src/ecs/main_pane_system.h` (`with_on_draw_fg([placeholder]`) — current placeholder is an app-drawn overlay over text_area. Tests: `tests/ui/composer_hints_are_legible.e2e` (`The composer's keyboard hint must be TEXT`) — script covers the composer hint/status text.



### #67 — Multi-line is a different widget, not a mode, so the switch is a rewrite

- **What I was trying to build.** Puffin's composer is a multi-line input: the
  box is 45px for one line and grows as you type, and the first line sits at the
  TOP of the box rather than centred in it.

- **What happened.** afterhours ships exactly the widget for this —
  `text_area`, with `with_auto_grow()`, `with_max_lines()`, `with_word_wrap()`
  and even `with_submit_on_enter()`, which is the chat-composer Enter semantic
  spelled out by name. It is not reachable from a `text_input`: there is no
  `with_multiline(true)`. They are separate widgets over separate state
  components (`HasTextInputState` vs `HasTextAreaState`), so the switch is not a
  config change but a rewrite of every integration point that touches the
  field's state. In this composer that is: the Enter submit listener
  (`HasTextInputListener`, whose scar is documented in `render_composer`), the
  history walk (reads `st.storage` / `st.cursor_position`), `set_field`, the
  Escape-to-clear path, `refocus_field`'s reach into the widget's first child,
  and the `expect_input_text` test hook. Six call sites, four of them load-
  bearing for tests that exist because they each caught a real bug.

- **Cost.** Not attempted inside this task's scope. The composer stays a
  single-line `text_input` inside a 45px box that LOOKS like Puffin's — the
  visible cost is one line of typing where Puffin takes several, plus the
  vertical placement: a centred single-line field puts its text 25.5px into the
  box where Puffin's top-aligned first line sits at 14.5px, an 11px error that
  was fixed here by top-aligning the short field inside the tall wrapper rather
  than by being multi-line.

- **Minimal upstream help.** Either `text_input` gains a multiline mode that
  keeps `HasTextInputState` as its public state, or the two widgets share one
  state type so an app can move between them without rewriting its callers.
  The second is the honest one: the state IS the API here, and the library has
  two of them for one concept.

**POSTSCRIPT 2026-08-26 (source-reference audit).** Entry's 'not attempted; composer stays single-line' claim is stale: the rewrite is now in current source, with compatibility shims.

**Hanabi reference.** Current code: `src/ecs/main_pane_system.h` (`auto inputRes = afterhours::ui::imm::text_area`) — current composer did switch to text_area. `src/ecs/main_pane_system.h` (`A SHADOW HasTextInputState`) — source documents one compatibility shim needed because text_area uses different state. Tests: `tests/ui/composer_shift_enter.e2e` (`Shift+Enter puts a line break in the message; Enter still sends it.`) — script proves the multiline rewrite shipped.


### #68 — Nothing reports the height an element actually came out at, so every scrolling list keeps two copies of its own layout

**What I wanted.** A virtualized transcript whose spacers are the size of the
items they stand in for. The list needs an item's height BEFORE it builds the
item (to place the spacers above and below the window), so `bubble_height()`
computes it and the render walk then draws the same thing. The two are supposed
to agree.

**What I tried.** Making the drawn shape match a measured reference: a user
bubble that hugs its text, an assistant bubble with its own padding. Every one
of those numbers has to be added in two places — the measure and the draw —
and I wanted to check that I had actually put it in both.

**What happened.** There is no way to ask. Not from the app: an element's
resolved rect exists (`UIComponent::rect()`) but only for the frame that has
already been laid out, and nothing correlates it back to the item the measure
pass sized. Not from a test either: `assert_ui` can assert a height you already
know, which is the same guess the app made.

So I wrote a probe (`src/ui/measure_probe.h`, `HANABI_PROBE_MEASURE=1`) that
does the correlation by hand: the measure pass records `bubble_height(i)`
against a key, the turn element is named with its index while the probe is on,
and the next frame walks the UI collection and holds the resolved rect against
the promise. On the mock threads it makes ~45 comparisons per rendered frame.

Three things came out of it, and none were visible without it:

1. The pair the repo already worried about — `rich_body_h` vs
   `render_rich_body`'s walk — **agrees exactly**: 0 drifts.
2. The pre-existing user-bubble arithmetic did not: the measure added a single
   `kUserPadV = 14` where the draw padded `8 + 9 = 17`. **Every user message
   measured 3px shorter than it drew**, on every thread, and had done for as
   long as those two constants disagreed. Fixed by giving both passes the same
   two constants.
3. What is left is a **flat +1px per turn** — measured 79, drew 80; measured
   107, drew 108 — that is not in the app's arithmetic at all. The parts sum to
   59 and the engine resolves 60. Over a 100-message thread that is 100px of
   scroll error.

Then, to prove the probe was not vacuous, I broke the draw on purpose: `+1.0f`
on the drawn segment height in `render_rich_body`. The probe went from 90
drifts in 136 comparisons to 136 in 136, and the `richbody` key — silent until
then — fired 46 times with exactly `+1.00`. Worth noting what did NOT change:
the turn's resolved height stayed at 108. The line element carries an explicit
`pixels(segH)`, segH grew by 1, and the element the engine produced did not,
which says a wrapped label's height is decided by its text and the size you
hand it is a hint.

**The workaround, and its cost.** Two functions kept in step by hand, one
constant at a time, with a comment on each saying "mirrors the other". This
change added four such constants (`kBubblePadTop`, `kBubblePadBot`,
`kBubbleCfgPadX`, `kAsstInsetR`) and two shared helpers (`asst_text_w`,
`user_box`) whose only reason to exist is that both passes must not compute the
number twice. ~90 lines of probe to find a 3px bug that had been shipping, and
a 1px residual I cannot explain and did not paper over.

**Severity: quiet, permanent, and it gets worse with list length.** Nothing
fails. The transcript just scrolls slightly wrong, more so the longer the
thread, and the next person to change a padding re-introduces it.


**Hanabi reference.** `src/ui/measure_probe.h` (`TEMPORARY measure-vs-draw probe`) — probe records expected and observed heights to catch drift. `src/ecs/main_pane_system.h::probe_drawn_turns` — transcript walks rendered turn entities and compares their resolved heights. Tests: `tests/ui/user_turn_hugs_the_right_edge.e2e` (`A user turn is a bubble that hugs its own text`) — script pins geometry that depends on the duplicated measurement/draw math. Measurement/gate: `src/ui/measure_probe.h` (`[mprobe] DRIFT`) — probe emits measured-vs-drawn height mismatches when enabled.


**Minimal upstream fix.** Either half would do:
(a) a `measure_only` pass — hand the library a ComponentConfig subtree and get
back the height it WOULD resolve, so the app has one layout function instead of
two; or
(b) a resolved-height read-back keyed by the caller's own id
(`ui::last_resolved_height(id)`), which at least turns a silent drift into
something an assert can catch.

### #69 — A wrapped label cannot size itself to its text, and it insets that text by an amount only the renderer knows

**What I wanted.** The reference's user bubble hugs its message: the bubble is
as wide as the longest wrapped line plus 13px of padding either side, and
right-aligned. Two ordinary asks — shrink-to-fit, and a padding that means what
it says.

**What I tried.** `children()` on the bubble's width, with the label inside it.

**What happened.** The label takes the width it is given and wraps into it;
there is no content width to collapse onto, so `children()` gives the bubble
whatever the label was already told to be. To get a hugging bubble the app has
to do the text metrics itself: wrap at the maximum width with
`ui::wrap_text`, measure every resulting line with the active font
(`theme::text_px`), take the widest, and set the box from that — reimplementing
the measurement the wrapper had just done internally and thrown away.

The second half is worse because it is invisible. A label's text does not start
at its rect's left edge: the wrap width is the rect less ~10px and the first
glyph lands ~6px in. Neither number is documented or exposed. A tab padded 12px
draws its title 18px in — measured off a screenshot, which is the only way to
find out. So a measured 13px gap is coded as a 7px padding plus a named
`kLabelInsetX = 6.0f` fudge, and every content-sized box has to add
`2 * kLabelInsetX` back to the width it computed or the text re-wraps inside
the box that was just sized to fit it.

**The workaround, and its cost.** `user_box()` in `main_pane_system.h`: ~25
lines to answer "how wide is this text", plus one magic constant in the
geometry block. The constant is a guess to the pixel — right for this font at
this size, and silently wrong for the reader who picks Atkinson Hyperlegible in
settings.

**Severity: a chat app cannot draw a chat bubble without it.** Any bubble,
chip, tag or tooltip that hugs its content hits both halves.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`static constexpr float kLabelInsetX = 6.0f`) — hanabi names and compensates for the renderer's private label inset. `src/ecs/main_pane_system.h` (`UserBox user_box`) — hanabi computes wrapped text width and hands the bubble a pixel width. Tests: `tests/ui/user_turn_hugs_the_right_edge.e2e` (`SHRINK-TO-FIT, CAPPED`) — script pins the app-computed hugging bubble width.


**Minimal upstream fix.** `ComponentSize{ content(), ... }` for a label — the
wrapper already computes the line extents it needs — and expose the two inset
constants (or, better, stop applying them and let padding be padding).

### #70 — An entity created this frame cannot be found by id until the frame merges, so state has to be threaded through the creation call

**What I wanted.** Restore the pinned tabs at launch: open each persisted tab,
then mark the ones that were pinned.

**What I tried.** The obvious loop — `open_session_in_tab()` for each id, then
walk `strip.tabOrder`, resolve each id with
`EntityHelper::getEntityForID`, and set `tab.pinned`.

**What happened.** Nothing was pinned. No error, no invalid entity reported at
a level the caller sees — the tabs opened, drew, and were simply not pinned. An
entity created during a frame is not in the collection the id lookup searches
until the merge at the end of it, so the loop resolved nothing and the `if
(!o.valid()) continue;` swallowed all of it. It cost a full build cycle (~5
minutes here) to even see that the flag was not arriving, because the symptom
is a missing 10x12px glyph.

**The workaround, and its cost.** `pinned` became a parameter of
`model::open_session_in_tab()`. That works, and it means any future per-tab
state set at restore time has to become another parameter of the same call
rather than a line of ordinary code after it. The existing "focus the persisted
active tab" loop right below has exactly the same shape and is presumably
equally inert; it looks correct, so nobody has questioned it.

**Severity: silent, and it looks like working code.** Create-then-configure is
the natural shape and it fails by doing nothing.


**Hanabi reference.** `src/ecs/tab_model.h` (`pinned applies to a NEWLY created tab only`) — pinned state is passed into tab creation rather than applied by a same-frame lookup. `src/ecs/tab_bar_system.h` (`/*pinned=*/std::find(app.restorePinnedIds.begin()`) — restore path feeds pinned state directly into open_session_in_tab. Measurement/gate: `docs/visual-parity/FRICTION_LOG.md` (`pinned had to become a`) — visual-parity notes record the create-then-configure failure and the parameter workaround.


**Minimal upstream fix.** Have `getEntityForID` see the pending set too (it
already knows about it — the temp-warning flag on `EntityQuery` exists for
exactly this), or make a lookup that misses a just-created id say so.
### #71 — Grid snapping quantizes child POSITIONS, so a measured pitch is only honoured at some window sizes

**Grid snapping quantizes child POSITIONS to a unit derived from the window
height, so a measured pixel pitch is only honoured at some window sizes.**

**What the design asks for.** Puffin's sidebar rows sit on a 32px pitch — the
view rows and the session rows both, measured off the reference window. hanabi's
rows are `pixels(32)` tall, stacked in a `FlexDirection::Column` with
`FlexWrap::NoWrap` and no gap. There is exactly one number in the design and one
number in the code, and they are the same number.

**What actually renders.** 32px apart at a 760px-tall window. **30px apart at a
949px-tall one.** Same binary, same sidebar width, same rows.

`snap_to_8pt_grid` (autolayout.h) derives its unit from the window HEIGHT:

```
grid_unit = max(1, round(4.0 * screen_height / 720))
```

which is 4 at 760px (`round(4.22)`) and **5 at 949px** (`round(5.27)`). Child
positions are then snapped to it, and — crucially — the column's running offset
is snapped after every child:

```
float next_y = offy + cy + gap;
if (enable_grid_snapping) next_y = snap_to_8pt_grid(next_y, Axis::Y);
offy = next_y;
```

so the error does not cancel: 32 → 30, 62 → 60, 92 → 90. Every row is 2px
early, and by the 18th row the list is **36px** out of place.

**Why the obvious escapes do not work.**

1. **`skip_grid_snap` exists but is only consulted for SIZE.**
   `compute_relative_positions` guards the size snap with
   `if (enable_grid_snapping && !widget.skip_grid_snap)`, and then the position
   snap 300 lines later is guarded by `if (enable_grid_snapping)` alone. A
   widget cannot opt its own placement out.
2. **Pixel sizes are already exempt, which makes the behaviour surprising.** The
   size path documents "when a developer specifies pixels(150), they expect
   exactly 150px" — and then places that 150px box on a 5px grid. The stated
   principle is right; it just is not applied to the axis that matters for a
   list.
3. **Absolute positioning is not a general answer.** It works for one-off chrome
   (the footer here) but a scrolling list of 2000 rows cannot hand-place its
   children without reimplementing the column.
4. **Choosing a pitch that IS a multiple of the grid unit is not stable.** The
   unit changes with the window height, so the "correct" pitch would have to
   change as the user resizes — which is the same as having no fixed pitch.

**The workaround, and its cost.** There is only one lever inside the app:
`UIStylingDefaults::get().set_grid_snapping(false)` in `preload.cpp`, which is
global. It moves every panel in the window by up to 5px, so it is not a change a
single component can make while four other components are being matched in
parallel. This sidebar therefore ships with the drift: **correct at 760px tall
(the window the whole test suite uses), 2px-per-row short at 949px (the window
the reference was shot at).**

**Severity: any measured design, at most window sizes.** This is not a sidebar
problem. Grid snapping is on for the whole app, so every hardcoded pixel
geometry in hanabi — tab heights, composer insets, row pitches — is a
suggestion that the layout rounds to a unit the designer never chose and cannot
see. The taller the window, the coarser the rounding.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The original “ships with drift” paragraph is stale; snapping is disabled app-wide and the entry’s resolved postscript is the current behavior.

**Hanabi reference.** Current code: `src/preload.cpp` (`UIStylingDefaults::get().set_grid_snapping(false);`) — turns off the global grid snapping that quantized row positions. Measurement/gate: `docs/visual-parity/FRICTION_LOG.md` (`It is off now, and pitch is exactly 32`) — records the measured correction and no-regression result.


**Minimal upstream fix.** Honour `skip_grid_snap` in the position path too — one
condition, and a widget that says "place me exactly" gets placed exactly.
Better: snap the child's FINAL position from an unsnapped accumulator (the code
already distinguishes the two and snaps both), so a column of N children drifts
by at most one unit total rather than one unit per child.

**RESOLVED (feat/vis-list) — the global lever costs nothing, and the assumption
above was never measured.** The diagnosis stands in full; the "workaround and
its cost" paragraph does not. `set_grid_snapping(false)` was flipped in
`preload.cpp` and every region re-scored against ref/01_home.png:

| region | snap on | snap off |
|---|---|---|
| views | 8.89% | 8.85% |
| search | 8.02% | 7.59% |
| list | 19.21% | 16.50% |
| footer | 5.26% | 5.26% |
| tabbar | 25.30% | 25.30% |
| main | 4.75% | 4.74% |
| STRUCTURAL | 10.71% | 9.84% |

Nothing regressed. The up-to-5px moves are real, but they move elements off a
grid nobody designed to and onto the pixel numbers already written in the code,
so every region carrying a measured number got closer. Session-row pitch is now
exactly 32 at 949px tall, with all 18 row centres inside 1px of the reference
(previously 33.5px of accumulated drift by row 18). Snapping is off app-wide.

Elements really do move, even though no region got worse: at 1100x760 the
transcript body rose 12px, which broke the two coordinate-addressed transcript
tests (`select_word_and_line`, `tracker_links`). Both instruct in their own
comments to re-measure rather than nudge, and both were re-measured.

The upstream fix is still worth making: an app should not have to choose between
"positions I asked for" and "grid alignment I did not", and the four rows of
that table are the whole argument for honouring `skip_grid_snap` on the position
path.

---

### #72 — A focus ring is painted at rest, on whatever happens to be first

**A focus ring is painted at rest, on whatever happens to be first, with no
"focus-visible" notion — so deleting a button moves a blue box onto a design
element.**

**What the design asks for.** The reference sidebar has no focus ring in it.
Nothing has been clicked and nothing has been tabbed to, so nothing is ringed —
the desktop convention, and the same convention as `:focus-visible` on the web.

**What happens.** `visual_focus_id` is assigned unconditionally from `focus_id`
(systems.h), and `focus_ring_for` paints a ring whenever
`context.visual_focus_id == entity.id`. Focus lands on the first focusable
element at startup, before any input, so exactly one element in the window is
always ringed. Before this change that was the sidebar header's `+` button,
where it read as an intentional highlight. Deleting the header — a pure design
change with no focus intent in it — moved the ring onto the VIEWS header strip,
a 280px-wide blue box across the top of the sidebar.

**Why the obvious escapes do not work.**

- **`with_skip_tabbing(true)` relocates the ring, it does not remove it.** Taking
  the strip out of the tab order put the ring on the first view row instead —
  a full-width row of the design, now outlined in accent blue at rest.
- **A clickable div is a focus stop.** Any element that takes
  `HasClickListener` joins the focus order, so a list of 18 rows is 18 focus
  stops; there is no "mouse target, not a tab stop" (skip_tabbing is the closest
  and, per above, only shifts the problem one element along).
- **`theme.focus_ring_thickness = 0` removes the ring EVERYWHERE**, including
  after a real Tab press, which is an accessibility regression rather than a
  fix.

**The workaround, and its cost.** `with_skip_tabbing(true)` on the four sidebar
chrome controls, which pushes the ring onto a view row where it is at least
row-shaped. ~600 pixels that no geometry work can remove, and the ring will move
again the next time someone deletes or reorders a control.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The old skip_tabbing-only workaround is superseded by the current focus-visible policy.

**Hanabi reference.** Current code: `src/ui/focus_visible.h` (`The ring is off until a navigation key is pressed`) — current app implements a focus-visible heuristic instead of merely moving the startup ring. `src/ecs/focus_visible_system.h` (`ctx.theme.focus_ring_thickness = fv::ring_thickness();`) — system writes the renderer-visible focus-ring thickness each frame. Tests: `tests/ui/focus_ring_waits_for_the_keyboard.e2e` (`expect_text "ring off"`) — test asserts no focus ring at rest.


**Minimal upstream fix.** A focus-visible rule: track whether focus was last
moved by the KEYBOARD, and paint the ring only then (`focus_source` is already
carried on the context — `FocusSource::Pointer` vs `Explicit` — so the
information exists and is thrown away at the ring). Failing that, an app-level
"no ring until first keyboard focus" switch.

---

### #73 — `assert_ui_text` matches ANY element with that label, so a positional assertion can silently be about a different panel

**A label that appears twice on screen makes `assert_ui_text` a coin toss, and
there is no way to say WHICH one you meant.**

**What happened.** The scripted sidebar tests assert row order by position —
`assert_ui_text "<row title>" y=306` is how "this thread is in slot 0" is
written, because there is no way to name one row of a list (#55) and no
`dump_ui` registered. But hanabi paints every sidebar row title a second time,
as a Home digest card in the main pane. `assert_ui_text` resolves its argument
with

```cpp
ui_query()
    .whereHasComponent<ui::HasLabel>()
    .whereLambda([&](const Entity &e) {
      return e.get<ui::HasLabel>().label == text &&
             e.get<ui::UIComponent>().was_rendered_to_screen;
    })
    .gen_first();
```

`gen_first()` — the first entity the query happens to reach, which is entity
creation order, not screen order and not the order the systems ran in. So
`assert_ui_text "watchdog clean for 6h" y=…` reports the MAIN PANE card's y,
while `assert_ui_text "profiling the disk" y=…` reports the SIDEBAR row's, in
the same frame of the same app. Which one you get depends on which entity was
created first across the whole run: rows inside the list's initial
viewport-fill cap were created on frame 1 and win; rows created later, when
"Show N more…" is clicked, lose to cards that already existed.

The failure mode is not a wrong answer, it is a wrong SUBJECT. The assertion
still passes or fails honestly — about an element the script was not talking
about.

**Why the obvious escapes do not work.**

- **There is no ancestor/scope filter.** `assert_ui <name> …` scopes by debug
  name, but every row in a list shares one debug name, so it can only ever
  reach the first row.
- **Adding `x=` to pin the panel does not disambiguate, it just fails.** If the
  query returns the main-pane card, `x=21` fails on the card rather than going
  and finding the sidebar row.
- **`expect_text` has the same blind spot, and it is worse there**, because a
  duplicate satisfies it: a test that asserts "the sidebar rows are still
  lettered" passes on the Home cards alone, with every sidebar row blank.

**The workaround, and its cost.** Assert only on strings that ONE panel can
produce. The sidebar ellipsizes a title to its column width and the main pane
does not, so the sidebar-truncated form (`"oncall sweep finished — 3 rows ne…"`)
is unique to the sidebar and the bare title is not. That works, and it means
the coordinate tests can only be written against rows whose titles happen to be
longer than 36 bytes — the fixture, not the test, decides which rows are
assertable. Change a title's length and a passing test quietly starts measuring
a card.


**Hanabi reference.** `tests/ui/sidebar_row_drag.e2e` (`assert_ui_text takes whichever entity the query reaches first`) — the current sidebar-order test is written around the duplicate-label ambiguity and documents why only initial-viewport rows are safe. Tests: `tests/ui/sidebar_row_drag.e2e` (`assert_ui_text "SKU backfill — my name for it" y=306`) — the workaround pins row order by positional text assertions on rows whose sidebar entities win over main-pane cards.


**Minimal upstream fix.** A scope argument on both commands —
`assert_ui_text "<text>" under=sidebar y=306`, resolved by walking up the
UIComponent parent chain to a debug-named ancestor — plus an error rather than
a silent pick when the match is ambiguous: "3 elements match, name one".
### #74 — The resolved layout tree cannot be walked after layout: `UIComponent::children` is cleared every frame before app code runs

**What I wanted.** One number: how tall did the assistant turn's meta row come
out at. Removing that row made the measure and the draw disagree by 2px
(`bubble_height` charges `kAuthorH + kAuthorGap = 18`; the drawn turn lost 20),
and the way to settle a question like that is to read the resolved heights of
the turn and of each of its children and see which one does not add up.

**What I tried.** The existing probe (#68) already finds an element by debug
name and reads `UIComponent::rect()`, so I extended its walk one level down:
for each `asst_turn#<i>`, iterate `UIComponent::children` and print each child's
resolved `y`, `height` and `computed_margin`.

**What happened.** The loop printed nothing, on every frame, while the parent it
was iterating printed a real rect. `ClearUIComponentChildren`
(`plugins/ui/systems.h`) empties `cmp.children` at the top of every frame so the
immediate-mode pass can re-parent from scratch, and app code only ever runs
AFTER that. So the parent→child edges of the tree that was just laid out do not
exist by the time anything can look at them: `rect()` survives the frame
boundary, the structure does not. There is no `parent_id` to walk upward from
either, and `UIComponentDebug` carries a name but no relationship.

The practical consequence is that a resolved subtree is only addressable as a
flat set of debug names you thought to assign in advance. You cannot ask "what
is inside this element", you cannot sum a parent's children to find which one
disagrees with it, and a debug name you did not add before the build is a
rebuild away — on this app, 2 minutes of `main.o` per hypothesis.

**The workaround, and its cost.** I answered the question with a camera instead
of the library: rendered the same thread twice, once with the row forced on and
once with it suppressed, and measured the y of the first assistant bubble's fill
in both PNGs (189 vs 169). That is how the row's true 20px footprint was
established. Four full rebuilds (~2 min each) plus a pixel scan to read one
height the engine already knows. The 2px itself is still unexplained, because
"which child is wrong" is exactly the question that cannot be asked.


**Hanabi reference.** `src/ecs/main_pane_system.h::probe_drawn_turns` — the current measure probe can only walk the flat UI collection by debug name and compare resolved turn rect heights. `src/ui/measure_probe.h` (`turn#<i>`) — the probe API records named height expectations instead of traversing a resolved child tree. Measurement/gate: `docs/visual-parity/FRICTION_LOG.md` (`UIComponent::children is cleared every frame before app code runs`) — records the original pixel-diff investigation and why subtree traversal was unavailable.


**Minimal upstream fix.** Keep the previous frame's structure alongside the
previous frame's rects — either don't clear `children` until the new tree is
built (double-buffer it), or expose a read-only `resolved_children(id)` /
`resolved_parent(id)` on the laid-out snapshot. Either one turns "diff two
screenshots" back into a loop.
### #80 — Every box rasterizes one pixel bigger and one pixel up-left than you asked for

**What was wanted.** A tab whose outer edge lands on the reference's measured
geometry: 220x34 at (284, 32), with its bottom border on the row directly above
the strip hairline.

**What the library does.** A `w x h` box translated to `(x, y)` paints
`(w+1) x (h+1)` pixels anchored at `(x-1, y-1)`. Asking for 220x34 at (284, 32)
painted x=283..503 and y=31..65 — one row of stray fill above every tab and one
column left of it. The rounded-rect fill and `draw_rectangle_rounded_lines`
agree with each other, so the shape is self-consistent; it simply is not the
rect that was requested, and nothing in the config (`with_size`,
`with_translate`, border width) accounts for the difference.

**Cost as a number.** 416 diff pixels on the single row y=31 — 0.65 points of
the tab bar's region score, from one off-by-one. Every measured rectangle in
the app carries the same error; the tab bar is just where it was measured.

**Workaround.** Author the design rect, then hand the library
`(w-1) x (h-1)` at `(x+1, y+1)`. In hanabi that is `tab_colors::kRasterGrow`,
a named 1.0f whose only job is to undo the library. Hit-testing still uses the
true outer rect, so the two now disagree by a pixel on every edge.


**Hanabi reference.** `src/ecs/tab_colors.h::kRasterGrow` — names the 1px compensation for afterhours box raster growth. `src/ecs/tab_bar_system.h` (`tabW - tab_colors::kRasterGrow`) — applies the width/height shrink and x/y translation compensation to tab boxes. Tests: `tests/ui/selected_view_fill.e2e` (`boxes rasterize one pixel bigger and one up-left than asked for`) — the scripted selected-fill test documents the same raster convention while pinning full-width geometry.


**Minimal upstream fix.** Rasterize a `w x h` box as `w x h` pixels at
`(x, y)`. Failing that, say in the API which convention is meant (inclusive
end coordinates? half-open?) so an app can compensate once, centrally, instead
of per widget.

CLASS: WORKAROUND

---

### #81 — The per-corner rounding bits are named for the OPPOSITE corner

**What was wanted.** The reference's tab is a folder tab: top corners rounded,
bottom corners square, standing on the strip's hairline. `RoundedCorners`
advertises exactly this.

**What the library does.** `RoundedCorners`' enum is
`TOP_LEFT=0, TOP_RIGHT=1, BOTTOM_LEFT=2, BOTTOM_RIGHT=3`. The sokol backend
reads the same `std::bitset<4>` under its own documented layout,
`3=TL, 2=TR, 1=BL, 0=BR` — an exact mirror. Every corner you name is applied to
its diagonal opposite. On top of that, the two helpers whose names match the
intent are themselves wrong: `top_round()` sets TOP_LEFT, TOP_RIGHT **and
BOTTOM_RIGHT** to ROUND (only BOTTOM_LEFT sharp), and `bottom_round()` mirrors
the same bug. Compose the two faults and `top_round()` renders top-left,
bottom-left and bottom-right rounded with a square top-right — an asymmetric
bracket that looks like a rasterizer glitch rather than a wrong bitset, which
is exactly how it gets diagnosed.

**Cost as a number.** A previous round of this work tried top-only rounding,
saw the bracket, concluded "the outline/edge path glitches on sharp bottom
corners", and shipped fully-rounded pills instead — the tab shape stayed wrong
for an entire round, and the comment recording that false conclusion is still
in the file's history. Rediscovering it cost ~35 minutes of reading the backend.

**Workaround.** Name the bottom two corners to round the top two:
`RoundedCorners().all_sharp().bottom_left(ROUND).bottom_right(ROUND)`. Wrapped
in `tab_colors::tab_corners_top_round_bottom_square()` with a comment telling
the next reader not to "fix" it.


**Hanabi reference.** `src/ecs/tab_colors.h::tab_corners_top_round_bottom_square` — rounds the bottom-left and bottom-right bits to render top-only rounded tabs under the inverted backend mapping. `src/ecs/tab_bar_system.h::with_rounded_corners(tab_colors::tab_corners_top_round_bottom_square())` — uses the inverted-corner helper for active and inactive tab chips.


**Minimal upstream fix.** One of the two layouts has to move. Making the
backend read `0=TL, 1=TR, 2=BL, 3=BR` matches the public enum and costs four
lines. Whichever way it goes, `top_round()`/`bottom_round()` need to set the
two corners their names promise.

CLASS: FOOTGUN

---

### #75 — Text is inset by a hardcoded 5px margin that no caller can turn off

**What was wanted.** A tab title starting exactly 26px from the tab's left edge
(12px when unpinned) — the reference's measured inset.

**What the library does.** `draw_text_in_rect` builds
`Vector2Type margin_px{5.f, 5.f}` as a literal and passes it to
`position_text_ex`, which offsets the glyph run by it. Nothing on
`ComponentConfig` reaches that value. Padding the parent to 26 put the title at
+33; zeroing the label child's own padding changed nothing, because the child's
padding was never the offset.

**Cost as a number.** 7px of drift on every tab title (5px margin + ~2px first
glyph bearing), and ~25 minutes to find, most of it spent ruling out padding.
See also #69, which names the same renderer-only inset from the wrapped-label
side; this is the single-line path.

**Workaround.** Author every left pad as `design_inset - 5.0f`, with a named
`kTextMarginPx` constant so the subtraction is explained rather than magic. The
design number in the source is now not the design number.


**Hanabi reference.** `src/ecs/tab_bar_system.h` (`const float kTextMarginPx = 5.0f`) — subtracts the private afterhours text inset from authored tab title padding. `src/ecs/sidebar_footer_geometry.h` (`kAhTextInset = 5.0f`) — centralizes the 5px label inset for footer label positioning. Tests: `tests/ui/row_title_starts_where_puffin_starts.e2e` (`row_title x=23`) — pins the row-title element position that compensates for the private text inset. Measurement/gate: `docs/visual-parity/FRICTION_LOG.md` (`Text carries a hardcoded 5px margin`) — records the visual-parity measurement behind the app constants.


**Minimal upstream fix.** Honour the element's own padding for text instead of
adding a private margin on top of it — or at minimum expose the margin on
`ComponentConfig` so it can be set to zero.

CLASS: WORKAROUND

---

### #76 — An unpadded element is not unpadded: it silently gets a fraction of the SCREEN

**What was wanted.** A label child that fills its parent's content box exactly,
so the parent's padding is the only thing positioning the text.

**What the library does.** `component_init` treats "every padding side is
`Dim::None`" as "caller expressed no opinion" and substitutes `Spacing::sm`,
which is `screen_pct(0.02f)` — a fraction of the WINDOW, not of the element.
So a child you never padded is padded, and by an amount that changes when the
window is resized. `imm_components.h` knows this (the toggle widget zeroes
padding explicitly with a comment explaining why) but the trap is not in the
public docs.

**Cost as a number.** ~15 minutes chasing the wrong suspect for #75's 7px, and
a latent resize bug in every nested element in the app that never set padding.
Sibling of #71: both are cases where a measured layout only holds at the window
size it was measured at.

**Workaround.** `with_padding(Padding{.top = pixels(0), .left = pixels(0),
.bottom = pixels(0), .right = pixels(0)})` on any child that must not be
padded. All four sides are required — a partially-specified Padding still trips
the default.


**Hanabi reference.** `src/ecs/tab_bar_system.h` (`Zero it explicitly (gap #76)`) — explicitly sets all four padding sides to zero on tab_label to avoid the default Spacing::sm. `scripts/check_label_padding.py` (`literal zero is exempt (gap #76's deliberate no-op)`) — the label-padding audit recognizes zero padding as an intentional workaround, not a defect.


**Minimal upstream fix.** Default to zero, and make the theme's spacing an
opt-in (`with_padding(Spacing::sm)`) rather than an opt-out. If the default has
to stay, make it element-relative so it at least survives a resize.

CLASS: FOOTGUN

---

### #77 — Weight loading works; Hanabi had no safe source for the face bytes

**Correction, measured on the pinned submodule (428047e), feat/bold-face.**
The "fails silently" half of this entry is stale. `resolve_weighted` calls
`warn_once` on an unresolvable weight — added upstream in 90f8ae8, "Public text
measuring, and stop two text features failing silently", which the current pin
includes. Asking for SemiBold with no variant registered prints, once:

```
[WARN] No font registered for '__default@semibold', drawing '__default' at its
normal weight. Load the variant under that exact name to get it.
```

**The weighted-resolution path works end to end.** Registering a second face
under `"<base>@semibold"` / `"<base>@bold"` is all it takes: `load_font` accepts
the weighted name, `resolve_weighted` finds it, and every renderer path
(`render`, the styled-span path, the measuring paths) swaps the face in and back
out around the draw. Proven by pointing `HANABI_BOLD_FONT` at a bold TTF already
on the machine and re-shooting `01_home`. With the knob unset the capture is
byte-identical to the run before the `with_font_weight` calls were added, so the
fallback is genuinely inert. **The only missing thing is the asset.**

**Cost as a number — and it is not the number this entry implied.** With a real
semibold in place the diff does NOT improve. Measured against
`ref/01_home.png` at 1180x949:

| session-list titles | list % | structural % |
|---|---|---|
| Roboto Regular 16.5 (shipped) | 16.58 | 8.18 |
| + Roboto Medium @semibold, 16.5 | 17.07 | 8.28 |
| + Roboto Bold @semibold, 16.5 | 17.32 | 8.38 |
| + Roboto Bold @semibold, best size (16.0) | 16.45 | 8.13 |
| SF Text Regular 15.5 (reference's own face, no bold) | **15.83** | 8.04 |
| SF Text Regular + SF Text Semibold, 15.5 | 16.02 | **7.99** |

The active tab's title moves 3.90 -> 3.86, not the 1.68 points this entry
attributed to it: the tab-bar residue is the fixture's different title strings,
which weight cannot touch.

**Why the correct answer scores worse.** The metric counts every
non-overlapping pixel on BOTH sides, so a face that draws too little ink is
cheap to be wrong with. Title-ink measurements over the same 18 rows:

| | ink px | vs reference | overlap with ref ink |
|---|---|---|---|
| reference (SF semibold) | 9442 | — | — |
| Roboto Regular 16.5 | 7958 | −15.7% | 40.1% |
| Roboto Bold @semibold 16.0 | 10518 | +11.4% | **60.3%** |
| SF Semibold 15.5 | 9253 | −2.0% | **53.9%** |
| SF Regular 15.5 (the region-score "winner") | 6562 | **−30.5%** | 34.8% |

The semibold closes the 16% ink-density deficit this entry named and lifts
title-ink overlap from 40% to 60% — it is unambiguously the more faithful
render — while the region score's best result is the arm that draws a THIRD
less ink than the reference. Shipping a bold is right for the picture and
roughly neutral-to-negative for this metric; anyone quoting "6 points" off it
should stop.

**What actually holds the titles apart** is glyph position, not glyph weight:
Roboto's advances diverge from SF's over a string, so ink drifts out of register
after a few characters and denser glyphs simply miss harder. Compounding it,
`theme::type::LIST_ROW = 16.5f` was fitted with a Regular face in hand — it was
absorbing the missing weight. Swap in the reference's own face and the optimum
moves to 15.5px, which alone is worth more (16.58 -> 15.83) than any weight
change.

**Still true.** No bold TTF ships in `resources/fonts`, so every
`with_font_weight` call in the tree resolves back to Regular today. Shipping one
is `Roboto-Bold.ttf` (Apache-2.0) + `AtkinsonHyperlegible-Bold.ttf` (OFL 1.1),
which is a licensing decision, not a library one.

CLASS: WORKAROUND



**POSTSCRIPT 2026-08-28 (font-system audit).** The resource statement remains true and is no longer the product blocker. Hanabi now resolves installed Roboto, Atkinson Hyperlegible, SF Pro Text, and Optimistic weight files through CoreText, loads them under afterhours' existing weighted-name convention, and deterministically falls back to the bundled regular faces for headless tests or missing files. No font binary was added. Current call sites use `with_font_weight` for chrome headings and the active tab; the transcript remains regular.

**Hanabi reference.** `src/native_extras.mm::native_font_faces`, `src/ui/font_system.cpp::apply`, and `tests/ui/system_font_and_weight_picker.e2e`. Measurements and fallback limits: `docs/font-system-audit.md`.

---

### #78 — `draw_circle_v` truncates its centre to whole pixels

**A filled circle cannot be placed on a half pixel: `draw_circle_v` takes float
coordinates and casts them to `int` before drawing.**

**What the design asks for.** Puffin's resting session-row marker is a 7px
filled dot centred at x=15.5 in a 13px glyph slot — a half-pixel centre, which
is what any glyph slot of even width gives you.

**What happens.** The float centre is thrown away one call in:

```
inline void draw_circle_v(Vector2Type center, float radius, Color color) {
  draw_circle(static_cast<int>(center.x), static_cast<int>(center.y), radius,
              color);
}
```

`draw_circle` then fans 32 unantialiased segments from that integer centre. At
r=3.7 the result is a lumpy heptagon sitting a pixel left of where it was asked
for, against a clean circle in the reference. The error is invisible at r=20 and
dominant at r=4, which is the size UI dots actually are.

**Why the obvious escapes do not work.**

- **Pre-rounding the centre does not help** — the point is to land ON the half
  pixel, not to pick which side of it to fall off.
- **`draw_circle_lines` has the identical cast**, so an outlined dot is no
  escape either.
- **`draw_poly` takes a float centre** but draws a regular polygon with a
  vertex at angle 0, so at 4-6px it reads as a polygon, which is the symptom.

**The workaround, and its cost.** `draw_ring_segment(cx, cy, 0.0f, r, 0.0f,
360.0f, 28, c)` — a zero-inner-radius annular sector is the same filled disc,
and that path uses `centerX`/`centerY` as floats throughout with a
caller-chosen segment count. One line, and it works. The cost is that the
primitive with the obvious name is the broken one, so every future caller writes
`draw_circle_v` first and only discovers this by looking at a screenshot.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`draw_circle_v truncates its centre to int (gap #78)`) — the Dot status mark avoids draw_circle_v and uses draw_ring_segment for a float-centered filled disc.


**Minimal upstream fix.** Delete the cast: add a float overload of
`draw_circle` (or change `draw_circle_v` to build the fan directly, which is
what `draw_ring_segment` already does correctly six functions away) and make the
segment count radius-adaptive, as `draw_ring_segment` already does when it is
passed `segments < 4`.

---

### #79 — A label cannot be told to fit a width; the caller must ellipsize blind

**There is no "truncate this label to fit its box". A label that is too wide is
hard-clipped mid-glyph, so the caller must shorten the string first — and the
only handle the caller has is a character count.**

**What the design asks for.** Puffin's session rows ellipsize at the column
edge: `stickers broke — concluded, D113637…`. Whatever the title, the ellipsis
lands at the same x.

**What happens.** `with_label` renders and the widget clips at its pixel width,
mid-glyph, with no ellipsis. To get one, the app truncates before the call —
and afterhours exposes no text measurement in the config path, so hanabi's
sidebar carried an average-advance budget:

```
size_t titleChars = static_cast<size_t>((rowTitleW - kRowTitlePad) / 6.1f);
```

`6.1` is Roboto's mean lowercase advance at 12.5px. It is not wrong so much as
it is a constant that silently encodes a font AND a size. Changing the row title
to the Puffin-measured 16.5px clipped four titles a word early, with nothing to
say why — the budget shrank because the divisor did not grow.

**Why the obvious escapes do not work.**

- **Scaling the divisor by the size ratio is still wrong**, because mean advance
  is per-FACE, not per-size-times-a-constant: the same scaling that fits Roboto
  over-truncates a narrower face.
- **Letting it clip** is what afterhours does by default, and a half-rendered
  glyph at the column edge is worse than an ellipsis.
- **Sizing the label by `Dim::Children`** measures the full string and overflows
  the row, which then trips the layout-overflow warning and `solve_violations`
  every frame.

**The workaround, and its cost.** An 18-line `fit_to_width` in the sidebar that
calls `theme::text_px` (which wraps `measure_text_internal`, the same fontstash
bounds the renderer uses) and walks back on UTF-8 boundaries until the string
plus an ellipsis fits. It is correct at any size and any face. The cost is that
it is the third place in hanabi that re-derives text metrics the layout engine
already computes, and every one of them pays a measure-per-frame per row —
bounded here only because the list renders viewport-many rows.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The original upstream ask is closed; the remaining source uses are narrower app-side choices around measurement/inset differences.

**Hanabi reference.** Current code: `src/ecs/sidebar_system.h::fit_to_width(display_title_view(s.title)` — the sidebar still carries its measured-width app-side ellipsizer. `src/ecs/settings_system.h::with_text_overflow(TextOverflow::Ellipsis)` — current source also uses afterhours' landed TextOverflow::Ellipsis API. Tests: `tests/unit/test_ellipsize.cpp::test_it_returns_what_the_linear_scan_returned` — pins hanabi's app-side fit_to_width behavior while the stale workaround remains in use.


**Minimal upstream fix.** A `TextOverflow::Ellipsis` on `ComponentConfig`,
resolved inside the renderer where the font, the size and the final rect are all
already in hand. Failing that, expose the measurement the layout already does so
callers stop guessing at advances.

**POSTSCRIPT 2026-08-26 (gap index) — THE ASK HAS LANDED; the entry is stale.**
`TextOverflow::Ellipsis` exists on `ComponentConfig` in the pinned submodule
(428047e) and does exactly what this entry asks for, in exactly the place it
asks for it. `with_text_overflow(TextOverflow overflow)`
(`plugins/ui/component_config.h:428`), enum at `ui_core_components.h:445`, and
the implementation is inside the renderer at `rendering.h:737-782`: it measures
the whole string, returns it untouched if it fits, and otherwise binary-searches
the longest prefix that fits alongside `"..."`, using the active font, the
resolved font size and the final rect. hanabi already uses it —
`src/ecs/settings_system.h:825` and `src/ecs/main_pane_system.h:4173`. The
sidebar's 18-line `fit_to_width` is a workaround for a gap that is closed, and
deleting it is an app-side change, not an upstream one.

Two caveats before anyone deletes anything, because they are the reason the
sidebar may still want its own:

  * the renderer's ellipsis measures with `measure_text` — the INK BOX, not the
    advance (**#103**, **#137**) — so it disagrees with `theme::text_px` by the
    same ~2px, on the same strings;
  * it hardcodes `max_width = rect.width - 10.f` "to account for margins (5px
    each side)" (`rendering.h:743`), which is the same unqueryable literal as
    **#75** / **#100** / **#277**, so an element whose text inset is not 5px
    ellipsizes at the wrong width.

The class drops from WORKAROUND to a live but much smaller ask: make the
ellipsis path use the same measure and the same inset as everything else.
Verified by reading the vendored source, not by running it.

### #82 — App-side global advance measurement cannot name a weight; renderer measurement can

**What was wanted.** Ellipsize a semibold session-list title to its column.
`fit_to_width` (see #79) already does this for regular text by measuring the
candidate substring and walking back until it fits.

**What the library does.** The measuring function the app can reach,
`afterhours::measure_text_internal(text, size)`, takes no weight and no font.
It measures against the backend's *global* active font
(`graphics::metal_detail::g_active_font`), which is whatever the renderer last
set — the base face, never the weighted variant, because the renderer swaps the
variant in and straight back out around each draw. So the app measures Regular
and the renderer draws SemiBold. There IS a font-taking `measure_text(Font, ...)`
overload, but reaching a weighted `Font` from app code means pulling the
`FontManager` singleton out of the ECS and hand-composing
`fm.get_font(fm.resolve_weighted(base, weight))` at every measuring site —
re-implementing resolution the renderer already does internally.

**Cost as a number.** Measured on `01_home` at 1180x949, ink beyond the title
column's right edge (x=248..268, 18 rows):

| titles drawn at | stray ink px past the column |
|---|---|
| Roboto Regular (measured correctly) | 3 |
| Roboto Bold @semibold, 15.75 | 26 |
| Roboto Bold @semibold, 16.5 | 132 |
| SF Text Semibold, 15.5 | 116 |

The ellipsis is placed where Regular would have ended, then the wider SemiBold
glyphs run past it and the label widget hard-clips mid-glyph — the exact failure
`fit_to_width` was written to avoid. It is invisible until a weight variant is
registered, at which point every measured-and-ellipsized string in the app is
silently wrong at once. The three existing measure-per-frame sites in hanabi all
inherit it.

**Correction 2026-08-28.** Current immediate and batched renderers resolve the component or span weight before both measurement and drawing, so the claim about every weighted string is stale. The remaining gap is narrower: `measure_text_internal(text, size)` has no font/weight parameter and uses the backend global, so app-owned advance measurement needs its own explicit-face seam (#574).

**Hanabi reference.** `src/ui/font_system.cpp::measure_advance` resolves the requested weighted `Font` before measuring. `src/ui/theme.h::text_px` carries weight in both the resolver and bounded cache key.


**Minimal upstream fix.** Give `measure_text_internal` a
`colors::FontWeight w = Regular` parameter and resolve it the same way the
renderer does. One signature, one lookup, and the whole class of "measured
regular, drew bold" bugs goes away before anyone ships a bold face.

CLASS: FOOTGUN

---

### #83 — The focus ring paints at rest: there is no `:focus-visible`, and `FocusSource` cannot be used to build one

**What was wanted.** An app that opens looking like an app that has not been
touched. Puffin's sidebar at launch has a selection fill on the current view and
nothing else; hanabi's had a 1px accent-blue rectangle around the same row,
present in every screenshot the harness took, before any input at all.

**What happens.** `UIContext::try_to_grab` parks focus on the first focusable
widget of the frame — "whichever widget happens to be first", as its own comment
says — and `ComputeVisualFocusId` then paints the ring on whatever holds focus,
with no interest in how focus got there. Every desktop toolkit and every browser
distinguishes *focused* from *focus-visible*: the ring appears when the keyboard
is driving and stays away when the pointer is. afterhours has one state.

Measured cost on the parity capture: rows y=67..69 and y=98..99 across the whole
sidebar width, ~99% wrong on each, the single largest contiguous block in the
VIEWS region. Removing it took VIEWS from 8.85% to 7.66%.

**Why `FocusSource` is not the answer.** It looks like the missing signal — it
already separates `Grab` (the per-frame re-park) from `Pointer` and `Explicit` —
but it is reset to `Grab` at the top of every frame in `BeginUIContextManager`,
so it answers "who claimed focus THIS frame", not "how did the focused element
come to be focused". By render time, a focus moved by Tab three frames ago
reports `Grab`, exactly like one nobody ever asked for.

**Why the obvious escapes do not work.**

- **`with_skip_tabbing(true)` on the first row** moves the problem rather than
  solving it: focus lands on whatever is next, and wears the ring instead. The
  sidebar already does this to its VIEWS header strip, which is how the ring
  came to sit on the first *row*. Do it to everything and the app has no
  keyboard navigation at all, and hanabi's search field then swallows every
  keystroke (gap #72).
- **`theme.focus = <the row's own background>`** hides the ring by painting it
  invisible, which also hides it when the keyboard IS being used — the ring's
  entire job.
- **A per-widget "no ring" flag** does not exist; the ring is drawn from the
  theme in `rendering.h`, keyed only on `visual_focus_id`.

**The workaround, and its cost.** `src/ui/focus_visible.h` plus a
`FocusVisibleSystem` registered ahead of every UI system: hanabi tracks, itself,
whether a navigation key has been pressed since the last pointer press, and
writes `theme.focus_ring_thickness` to 1 or 0 each frame accordingly —
`focus_ring_thickness = 0` being the one knob afterhours documents for turning
the ring off. It is ~30 lines and it works. The cost is that a heuristic every
UI framework ships now lives in the app, so every other afterhours app either
reinvents it or ships the resting ring; and because the knob is global, an app
that wants the ring suppressed on ONE widget still cannot have that.


**Hanabi reference.** `src/ui/focus_visible.h` (`:focus-visible, by hand`) — implements the app-owned focus-visible heuristic and documents why FocusSource is insufficient. `src/ecs/focus_visible_system.h` (`ctx.theme.focus_ring_thickness = fv::ring_thickness()`) — applies the app-owned heuristic to afterhours' global focus-ring thickness each frame. Tests: `tests/ui/focus_ring_waits_for_the_keyboard.e2e` (`expect_text "ring off"`) — verifies the ring is absent at rest, appears after Tab, and disappears after pointer input.


**Minimal upstream fix.** Keep a sticky `focus_visible` bool on the context —
set when focus moves by keyboard, cleared on a pointer press — and gate the ring
in `ComputeVisualFocusId` on it. Ten lines where the focus already lives, and
`FocusSource` already carries the distinction the frame needs; it only needs to
survive the frame boundary.

CLASS: WORKAROUND

---

### #84 — Right-aligned label text can never sit flush to its box (hardcoded 5px inset)

**What was wanted.** A sub-agent count on a sidebar row, right-aligned against
the row's right edge, the way the reference client draws it.

**What the library does.** Every alignment insets the text by a constant that
callers cannot see or change. `rendering.h`:

```cpp
constexpr float kInset = 5.f;                       // line 890
float x = rect.x + kInset;                          // Left
...
else if (alignment == TextAlignment::Right)
  x = rect.x + rect.width - kInset - line_w;        // Right
```

`ComponentConfig` has no margin, inset or padding knob that reaches this, so
`TextAlignment::Right` means "5px shy of the right edge" and nothing else. The
same constant governs Left and Center, which is fine for those — a 5px gutter
on the left of a left-aligned label is invisible. On the right it is a
5px hole between the digits and the edge they are supposed to be flush with.

**This is not theoretical, and it is not new.** Every right-aligned count
already in hanabi's sidebar — the smart-view badges (Home/Blocked/Review), the
folder counts — lands its right edge at x=263 where the reference puts it at
x=271. The 8px has been visible in the parity captures the whole time and read
as "the counts sit a bit left", which is exactly what a hardcoded inset plus a
glyph's own side bearing looks like.

**Why the obvious escapes do not work.**

- **Shrinking the parent's right padding** moves every sibling in the row, not
  just the label — the star and mute slots go with it.
- **Making the slot 5px wider** does nothing: the inset is measured from the
  slot's own right edge, so the text moves left with it.
- **`with_translate`** takes an absolute position, not a delta, so using it
  here means re-deriving the row's laid-out x in the caller, which is the
  layout engine's job and wrong the moment anything beside it changes width.

**The workaround, and its cost.** Left-align in a slot sized to the measured
text **plus** `kInset`. The inset then lands on the left of the slot, where
there is nothing to be flush with, and the text's right edge falls exactly on
the slot's — which, for the row's last child, is the row's right edge. It is
exact and it costs one measurement the layout will immediately redo, but it
encodes the library's private constant (`kAhTextInset` in `sidebar_system.h`)
in app code, so a change to `kInset` upstream silently moves hanabi's counts.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`LEFT, not Right — see kAhTextInset`) — sub-agent counts are left-aligned in a text-plus-inset slot to make their right edge flush. `src/ecs/sidebar_footer_status.h` (`slot sized to the text PLUS afterhours' hardcoded 5px label inset`) — the footer count uses the same left-aligned slot workaround. Tests: `tests/unit/test_footer_geometry.cpp` (`label_box_x(fs::kFooterPadX) == 5.0f`) — pins the 5px label-box compensation used for flush footer text.


**Minimal upstream fix.** Honour a per-component inset/margin on
`ComponentConfig` and default it to the current 5px; or simply stop insetting
the Right case, where the inset has no reader-facing purpose.
---

### #87 — A box cannot be sized to its own text AND capped: `Dim::Text` measures unwrapped, and `max_width` clamps nothing but itself

**What was wanted.** Puffin's user turn, verbatim from
`Sources/Views/AgentcloudTranscriptView.swift`:

```swift
HStack(alignment: .top, spacing: 6) {
  Spacer(minLength: 60)
  BubbleAvatar(fill: fill)
  VStack(alignment: .leading, spacing: 4) { Text(row.text) ... }
    .padding(.horizontal, 12).padding(.vertical, 9)
    .background(Color(fill))
}
```

There is no width in it. The bubble is as wide as its text; the `Spacer` is what
stops it at 644 of the 736pt column; past that the `Text` wraps and the bubble
grows down instead of sideways. Two rules — *hug your content* and *never
exceed N* — and in SwiftUI they compose for free.

**What happens.** afterhours has both halves and they do not compose.

`Dim::Text` on the X axis resolves in `calculate_standalone`, through
`get_text_size_for_axis`. That function only measures a wrapped extent when

```cpp
const bool wraps = label.text_overflow == TextOverflow::Wrap &&
                   widget.font_size_explicitly_set &&
                   widget.computed[Axis::X] > 10.f;
```

— and on the X pass `computed[Axis::X]` is exactly the thing being computed, so
it is still `-1`. **The intrinsic width of a label is therefore always its
single-line width**, however long the string. A 200-character message measures
~1240px wide on a 736px column. (The Y pass then runs with X already set, so it
*does* measure wrapped: the box ends up two lines tall and twelve hundred wide.)

`with_max_width(pixels(N))` exists and works — but only on the element carrying
it. `apply_size_constraints` is called from `solve_violations`, which runs
*after* `calculate_those_with_children`, so nothing else in the tree is
re-solved around the clamp. Both directions leak, and both were measured on the
real app at 1180x949 rather than argued:

- **Parent hugs the pre-clamp child.** Bubble `children()`, text
  `Dim::Text` + `max_width(pixels(200))`. The text clamps; the bubble was
  already sized from the unclamped 800-odd and stays there. Measured bubble
  width: **818px**, off the right edge of a 1180px window. A cap of 200 changed
  nothing anyone can see.
- **Child stretches to the pre-clamp parent.** Bubble `pixels(500)` +
  `max_width(pixels(200))`, text `percent(1.0f)`. The bubble clamps correctly
  to **200**. The text was already sized against 500 and renders **226px** wide
  — 26px of it painted outside the bubble it is supposed to be inside.

The second failure is the more dangerous one, because the clamp *looks* like it
worked: the box is the right size and the content is simply wrong.

**Why the obvious escapes do not work.**

- **`children()` on the bubble alone** hugs the text, but the text still has no
  cap, so a long message runs off the pane. That is the 818px measurement.
- **`percent()` on the bubble** caps it, but every bubble is then the same
  width. That is precisely the shape Puffin does *not* have — the assistant
  side is full-width and the user side is not, and the difference is what makes
  a transcript read as a conversation.
- **`Dim::Text` plus `max_width`** is the API that ought to be the answer and is
  the measurement above.
- **`expand()` with `min_width(text())`**, the pattern
  `resolve_constraint`'s own comment advertises, is the opposite problem: it
  fills its share and refuses to shrink. There is no `max_width(text())`
  equivalent that would mean "hug".

**The workaround, and its cost.** `MainPaneSystem::user_box()` — hanabi measures
the text itself. It wraps the body at the cap with `wrapped_lines()`, measures
every resulting line with `theme::text_px()`, takes the widest, adds back the
6px the label inset takes off its own rect, clamps to `kBubbleCap`, and hands
the layout a literal `pixels(N)`. About 20 lines.

The cost is not the 20 lines, it is what they oblige. The number has to be
computed **twice per message per frame** — once in `bubble_height()` so the
transcript's virtualization spacers reserve the right extent, once in
`render_bubble()` for the draw — which is why `user_box()` exists as a function
at all rather than as two expressions that drift. And hanabi's `wrapped_lines()`
must agree with the renderer's own wrap to the character, forever: the library
wraps in `measure_wrapped`, the app wraps in `textscan.h`, and if either ever
splits on a different rule the bubble is the wrong width with no warning
anywhere. The app now owns a second implementation of the layout engine's most
subtle function, and cannot stop owning it.

Pinned by `tests/ui/user_turn_hugs_the_right_edge.e2e`, which asserts the
bubble's computed width is its text's (460) and not the cap (644) or the column
(736) — because a hand-computed literal is exactly the kind of number that rots
without a test.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`UserBox user_box`) — computes user bubble width from wrapped text and cap in app code. `src/ecs/transcript_render_cache.h` (`user_box() asks at the bubble's MAXIMUM text width`) — documents the two-width memoization shape caused by app-side hug measurement. Tests: `tests/ui/user_turn_hugs_the_right_edge.e2e` (`assert_ui user_bubble x=598 w=460 h=35`) — pins a right-aligned shrink-to-fit user bubble rather than a cap-width or column-width bubble.


**Minimal upstream fix.** Give `get_text_size_for_axis` the max constraint that
is already on the widget: when `max_size[Axis::X]` resolves to a real value,
wrap against *that* on the X pass instead of requiring `computed[Axis::X]`.
`Dim::Text` then means "as wide as my text, wrapped at my cap", which is the
only thing anyone ever wants it to mean, and `min(intrinsic, cap)` falls out.
The second half is one more line: re-run `calculate_those_with_children` for a
`children()`-sized parent whose child was clamped, or clamp before the parent
reads the child rather than after.


CLASS: WORKAROUND

---

### #85 — Padding on a label-only element is silently ignored

*(See also #91, which found the same wall from the composer and states it more
completely: padding is one of three ways a label refuses to be laid out.)*

**What was wanted.** A smart-view row's label to start 12px after its icon,
where the reference draws it. The row asked for exactly that:

```cpp
div(ctx, mk(row.ent(), 2),
    ComponentConfig{}
        .with_label(label)
        .with_size(ComponentSize{pixels(svLabelW), pixels(22)})
        .with_padding(Padding{.left = pixels(12)})   // does nothing
        ...
```

and its comment did the arithmetic out loud: *"Puffin puts the label ink at
x=37: kSbInset 9 + icon 16 + a 12px pad on the label = 37."*

**What happens.** The text lands at x=31 — the div's own left edge plus the
hardcoded 5px text inset (gap #75), with the padding contributing nothing.
Proven by changing the value: **`pixels(12)` and `pixels(40)` produce a
byte-identical frame.** Padding is applied when laying out an element's
CHILDREN; a label is not a child, it is drawn into the element's rect by
`position_text_ex`, which reads the rect and the alignment and never the
padding.

The cost is not the six pixels. It is that the code, its author and its comment
all believed the padding was applied, and nothing in the build or the run said
otherwise — the label sat 6px left of the reference for the entire parity
effort while a comment in the same file asserted it was correct. A silent no-op
on a value someone computed is worse than a compile error and worse than a
crash.

**Why the obvious escapes do not work.**

- **`with_margin`** is the same story one level out: it spaces the element from
  its siblings, not the text from the element.
- **Sizing the label div smaller and letting alignment place it** only works for
  Right and Center, and both of those are already 5px off for the same reason
  (gap #84).
- **Nesting the label in a padded parent** works, and costs an entity per label.

**The workaround, and its cost.** An empty div of the measured width before the
label — the same move this file already makes vertically (`spacer`, "afterhours
has no margin between column children"), now also horizontally (`spacer_x`).
Two entities per row instead of one, and every measured gap in the design
becomes a widget that exists only to be empty.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The mechanism is still true, but the entry's statement that with_margin is no escape is contradicted by current source and by #109.

**Hanabi reference.** `src/ecs/sidebar_system.h` (`spacer_x(ctx, row.ent(), 7, kViewLabelGap)`) — smart-view labels still use a spacer rather than label padding for the measured gap. `scripts/check_label_padding.py` (`with_padding() on an element that has a label and no child`) — current audit tooling detects new label-padding no-ops. Tests: `tests/ui/row_title_starts_where_puffin_starts.e2e` (`Margin moves the`) — guards the later margin-based correction to the same family of bug. Proof-patch decision: `vendor_patches/README.md` rejects #85/#277 because the 5px contract is duplicated across plain, wrapped, styled, ellipsis, immediate, batched, and text-input paths; honoring padding would move nine live labels.


**Minimal upstream fix.** Either honour `Padding` in `position_text_ex` — the
rect is right there — or, better, refuse it: make `with_padding` on an element
that has a label and no children a warn-once, the way `resolve_weighted` now
warns on an unresolvable font weight. Silence is what made this cost a day.
---

### #88 — A row cannot baseline-align its children, so every avatar-beside-text row carries a magic top offset

**What was wanted.** A 20px avatar beside a 35px bubble, sitting on the bubble's
first line of text rather than floating in the middle of it.

**What happens.** `AlignItems` is `FlexStart | FlexEnd | Center | Stretch`.
There is no `Baseline`. `SelfAlign` — the per-child override, which does exist
and does work — carries the same four.

`FlexStart` is close and is what hanabi uses, but "top of the avatar box" and
"top of the first line of text in the sibling" are not the same y: the sibling
has 9px of padding and the text has its own ascent above the cap height, and
the avatar has to come down by some of that to look aligned. Nothing in the
layout knows either number, so the offset is a constant in app code:

```cpp
static constexpr float kAvatarTop = 6.0f;  // BubbleAvatar .padding(.top, 6)
```

Puffin has exactly the same constant, `.padding(.top, 6)`, for exactly the same
reason — SwiftUI *has* `.firstTextBaseline` and Puffin did not reach for it,
because the avatar is a `Circle` with no baseline to align to. So this is not a
gap where afterhours is behind a peer; it is a gap where **both** toolkits push
the same fudge into the app, and afterhours could not offer the alternative even
if the design wanted it.

**Why the obvious escapes do not work.**

- **`AlignItems::Center`** puts the avatar at the vertical centre of the row.
  Measured: y=142 against the wanted 137 on a one-line bubble, and it gets
  worse with every line the message wraps to — a five-line message parks the
  silhouette next to line three.
- **`Stretch`** makes the avatar as tall as the bubble, and `draw_circle_v`
  then draws an ellipse.
- **Wrapping the avatar in a `children()`-sized spacer column** moves the magic
  number rather than removing it, and adds an entity per turn.

**The workaround, and its cost.** The 6px constant, and a comment saying where
it came from. The cost is small and entirely in the future: the number is
correct for BODY at 13px with 9px of bubble padding and silently wrong for any
other combination, and nothing will fail when someone changes one of them. It
is pinned by `tests/ui/user_turn_hugs_the_right_edge.e2e` (`user_avatar y=137`
against `user_bubble y=131`), which at least makes the rot loud.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`kAvatarTop = 6.0f`) — keeps the user avatar top offset as a measured app constant. `src/ecs/main_pane_system.h` (`with_margin(Margin{.top = pixels(kAvatarTop)`) — applies the offset to the user avatar beside the bubble. Tests: `tests/ui/user_turn_hugs_the_right_edge.e2e` (`assert_ui user_avatar x=620 y=141 w=20 h=20`) — pins the avatar's top alignment relative to the bubble.


**Minimal upstream fix.** `AlignItems::Baseline` / `SelfAlign::Baseline`, where
a child with a label contributes its own first-line ascent and a child without
one falls back to its top edge. The renderer already knows the ascent — it is
what `draw_text_in_rect` positions against.


CLASS: FOOTGUN

---

### #86 — A capture cannot say where anything landed: the screenshot path emits pixels and nothing else, so every geometric fact about a frame is bisected back out of the PNG

**What was wanted.** A list of rectangles, in the parity capture's own
coordinates, naming the surfaces where hanabi and Puffin draw structurally
different things — the transcript viewport, the status strip, the composer box.
Each one has to be drawn tightly around a real element: too small and it leaks
difference into the score, too large and it swallows signal that could have been
worked on. The engine knows all of it. `layout.composer` is a `Rect` computed in
`layout_system.h`, and every div in `status_bar_system.h` carries a
`with_debug_name` chosen for exactly this kind of question.

**What happens.** `run_headless_screenshot` writes a PNG. That is the entire
output of a capture. There is no companion file, no stdout table, no flag that
says "and also tell me the resolved rect of every named element you just drew".
So the way to find the composer's top edge is to load the PNG in Pillow, pick a
background colour, and walk rows counting non-background pixels until a run
appears — which is how the numbers in `scripts/compare.py`'s divergence table
were established: y=825 is hanabi's composer divider because row 825 has 720
non-background pixels and rows 790..824 have none. The same bisection had to be
repeated for the reference frame, where it is unavoidable (it is a PNG of
another app), and for hanabi, where it is not.

Three costs, all paid this session. Each rectangle took a scripted row-scan and
a judgement call about where to cut. The scan found the boundary in hanabi's
capture and NOT in the source, so the first version of the transcript rectangle
ran to y=855 and quietly ate Puffin's full-width composer rule at y=850 — caught
by re-reading the profile, not by anything the tool said. And because the
numbers are transcribed pixels rather than derived ones, they are stale the
moment a layout constant moves, with nothing to notice.

**Why the obvious escapes do not work.**

- **`assert_ui` in the scripted `.e2e` DSL** does resolve a `debug_name` to a
  live rect — but it lives in the UI-test runner, on a separate binary and a
  separate invocation from `--screenshot`, and it asserts rather than reports.
  Getting a table out of it means writing one assertion per element, guessing
  the number, and reading it off the failure text. It also cannot run in the
  same process as the capture, so nothing guarantees the two frames agree.
- **Walking the tree after layout** is gap #74: `ClearUIComponentChildren`
  empties `UIComponent::children` at the top of every frame, so a resolved
  subtree is only reachable as a flat set of names decided in advance. #74 asks
  for the parent/child edges back; this asks for something weaker and more
  useful at capture time — the flat name→rect table, which already survives the
  frame boundary and merely has no way out of the process.
- **Reading the layout struct directly** covers `layout_system.h`'s six rects
  and nothing else. The composer's inner divider, the chip row and the status
  bar's own clusters are all built inside their systems from local constants,
  which is where the interesting boundaries are.
- **Printing rects from app code** is a rebuild per hypothesis (~2 min of
  `main.o` on this box, the same toll #74 records) and leaves debug printf in
  systems other agents are editing on other branches.

**The workaround, and its cost.** Bisect the PNG. `scripts/compare.py` now
carries five hand-measured rectangles pinned to a `DIVERGENCE_FRAME` of
1180x949, with two guards standing in for the derivation that should not have
been necessary: the table refuses to apply at all if the reference is not that
exact size, and any rectangle that turns out to exclude zero differing pixels
prints `<-- STALE? excludes nothing`. Those guards are real value — they are
also thirty lines of scaffolding whose only job is to detect that a transcribed
number has gone out of date, which is a problem a derived number does not have.


**Hanabi reference.** `scripts/compare.py` (`DIVERGENCE_FRAME = (1180, 949)`) — declared divergence rectangles are pinned to the measured frame size. `scripts/compare.py` (`STALE? excludes nothing`) — the comparison script flags transcribed rectangles that no longer exclude any differences. Tests: `tests/unit/test_footer_geometry.cpp` (`assert_ui reads x/y/w/h/hidden/text (gap #86) and ROUNDS them`) — documents a case that had to be moved to an arithmetic unit test because screenshot/e2e geometry was insufficient. Measurement/gate: `scripts/compare.py` (`DECLARED DIVERGENCES`) — reports the measured cost of each hand-transcribed rectangle on the current pair.


**Minimal upstream fix.** One flag on the capture path that dumps the frame's
resolved geometry as it is written: for every entity with a `UIComponentDebug`
name, its name and its `rect()`, as JSON, next to the PNG. The data is already
in hand at that moment — `rect()` survives the frame boundary, which is the
half of the tree #74 says still works — and it needs no new bookkeeping, only
an exit. Any parity harness downstream then addresses surfaces by name instead
of by transcribed pixels, and a rectangle stops being a number somebody has to
remember to re-measure.

CLASS: TEDIOUS
### #91 — A label is not a layout participant: padding cannot inset it, `children()` cannot measure it, and the one field that moves it is not on `ComponentConfig`

*(#85 is the same family, found independently one theme over and kept because
it carries the proof — `pixels(12)` and `pixels(40)` render byte-identical
frames. This entry is the fuller statement of the same thing.)*



**Hanabi reference.** `scripts/check_label_padding.py` (`afterhours_gaps.md #85, #91 and #109`) — current audit tooling treats #91 as part of the active label-padding family. `src/ecs/sidebar_system.h` (`private 5px text margin and ignores the element's padding entirely`) — current row-title source documents the label-as-non-child mechanism. Tests: `tests/ui/row_title_starts_where_puffin_starts.e2e` (`afterhours_gaps.md #85, #91`) — pins the corrected title placement for the same label-layout family.

---

### #89 — NOT A GAP: right-aligning a child needs no spacer, and this is worth writing down

Filed deliberately as a negative result, because two of the three things this
theme set out to probe turned out to be things afterhours does correctly, and a
gaps file that only records failures overstates the case.

**What was wanted.** Puffin pushes the user turn to the right edge with
`Spacer(minLength: 60)` — a real sibling view that eats the slack. The question
was whether hanabi would have to build the same thing.

**What happens.** It does not. `JustifyContent::FlexEnd` on the row does it,
with no spacer sibling and no phantom child:

```cpp
.with_size(ComponentSize{percent(1.0f), children()})
.with_flex_direction(FlexDirection::Row)
.with_justify_content(JustifyContent::FlexEnd)
```

Measured at 1100x760: the row is x=322 w=736, the bubble is x=646 w=412, and
646+412 == 322+736 == 1058. Flush, to the pixel, with two children of unequal
width and no filler between them. Flipping it to `FlexStart` moves the bubble
to x=348, which is what
`tests/ui/user_turn_hugs_the_right_edge.e2e` exists to catch.

`SelfAlign` covers the per-child cross-axis case too, so "this one child aligns
differently from its siblings" is expressible without a wrapper.

**The one caveat.** `FlexEnd` justifies within the row's own width, so the row
must have one: `percent(1.0f)` here. A `children()`-sized row has no slack to
distribute and `FlexEnd` silently does nothing — which reads as the
justification being broken rather than as the row being the wrong size. That is
a footgun, not a missing feature, and one line of documentation on
`with_justify_content` would retire it.

CLASS: TEDIOUS


**Hanabi reference.** Negative result: `src/ecs/main_pane_system.h::with_justify_content(JustifyContent::FlexEnd)` — user rows use FlexEnd rather than a spacer sibling to right-align the bubble. Tests: `tests/ui/user_turn_hugs_the_right_edge.e2e` (`JustifyContent::FlexEnd buys — there is no spacer sibling`) — asserts the bubble is flush right and documents the no-spacer negative result.

---

### #90 — `ctx.theme` is one global struct read at RENDER time, so a per-widget colour is a frame-wide edit

**What was wanted.** The search field's placeholder in the reference's colour —
one label, in one system.

**What happens.** `text_input` ignores per-widget colours and reads the theme
(gap #17), and the field's placeholder specifically reads `theme.font_muted`.
So the only way to colour it is `ctx.theme.font_muted = <colour>` — and
`ctx.theme` is a single mutable struct on the UI context that the RENDERER
reads, long after the system that set it has returned. There is no push/pop, no
scope, and no per-subtree override.

The consequence is not theoretical. Setting `font_muted` in `SidebarSystem` to
colour one placeholder moved the **main pane's** score by 0.14 points on a
change that touched no main-pane code, because every muted label in the frame
picked it up. Restoring the value at the end of the sidebar's `for_each_with`
does not help either: by then nothing has been drawn yet.

This is why four systems in hanabi open with the same five lines
(`ctx.theme.secondary = …; ctx.theme.surface = …; ctx.theme.font = …`) — each
is defending itself against whatever the previous one left behind. Miss a field
and you inherit it silently; that is what happened here, and the only reason it
was caught is that a parity number moved.

**Why the obvious escapes do not work.**

- **Per-widget colour** is what `with_custom_text_color` is, and the widgets
  that need this are exactly the ones that ignore it (gap #17).
- **Save and restore around the call** cannot work: the write is consumed at
  render, not at build.
- **A second theme** is not addressable — `UIContext` has one `theme` member.

**The workaround, and its cost.** Every system re-asserts every theme field it
cares about, every frame, forever. hanabi now sets `font_muted` in
`MainPaneSystem` too — a line in a file that has no interest in the search
field, which exists solely because the sidebar had to shout its colour at the
whole frame to reach one placeholder.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`setting it here brightens every muted label in the frame unless the next system re-asserts its own`) — sidebar still writes global font_muted for its search placeholder. `src/ecs/main_pane_system.h` (`ctx.theme.font_muted = theme::text_faint()`) — main pane reasserts muted text color so it does not inherit sidebar state.


**Minimal upstream fix.** Either honour the per-widget colours in `text_input`
(gap #17's ask, which subsumes this), or give the context a scoped theme —
`ctx.push_theme(t) / pop_theme()` recorded into the render command stream, so a
theme edit lasts exactly as long as the subtree that made it.

CLASS: FOOTGUN


---

**What was wanted.** Puffin's composer strip, which is four text runs and a
pill: `Opus 5` then `(high)` butted onto it, a meter, `0%`, and a capsule
carrying a 9pt icon 3pt before its label inside 6pt of padding
(`AgentcloudChatView.StripMetrics`, `ToggleChip`). Three ordinary asks — size a
widget to the words in it, put a gap of N between two runs, and start a label N
pixels in from its own box.

**What happens.** None of the three is expressible.

- `with_padding` never reaches the text. `rendering.h` draws a label with
  `Vector2Type margin_px{5.f, 5.f}` — a literal, clamped only to 40% of the box
  — and that is the *only* inset a label gets. The padding sizes and positions
  the BOX; the words stay at +5 from its left edge whatever you passed. A pill
  given 19px of left padding to make room for an icon draws the icon straight
  through the first letter, which is exactly what the first attempt here did.
- `children()` sizing cannot see a label. `_sum_children_axis_for_child_exp`
  sums child *elements*, and a label is a component on the widget, not a child.
  A `button` with a label and no children asks for `children()` and gets a width
  that has nothing to do with its text: `Server default` at 11px measures 55px
  wide and came out in a 112px box, which put a 52px hole in the middle of a run
  of words that is supposed to read as one phrase.
- `HasLabel::text_x_offset` is precisely the missing knob — `label_rect.x +=
  hasLabel.text_x_offset` in both render paths — and `ComponentConfig` has no
  setter for it. Only `text_input`'s own internals ever write it.

**Why the obvious escapes do not work.**

- **Leading spaces in the label string** move the text, and are measured in
  whatever face is active at draw time, so the inset is a different number on
  every font and every scale — and it lands *inside* the string every
  `expect_text` assertion and every accessibility reader sees.
- **A wrapper div holding an icon div and a label button** lays out correctly
  and gives up the hit target: the capsule's border, its padding and the icon
  all stop being clickable, so the pill answers to a click on its words only.
  Puffin's whole capsule is the button.
- **Putting the icon at the RIGHT of the label** does work, because the free
  space in a label's box is all at the trailing end. It is also not the design;
  the icon leads in all three of Puffin's pills.
- **`with_alignment(Center)`** does not help — it centres inside the same
  hard-coded margin, so it moves both edges and buys no inset on either.

**The workaround, and its cost.** Every text run in the strip is measured with
hanabi's own `theme::text_px` (fontstash bounds through the active face) and
given an explicit `pixels(...)` width, and every gap between runs is written net
of the 5 the library will spend regardless — the composer names that as
`kLabelInset` so the arithmetic reads as arithmetic. The pill's icon is blitted
by `on_draw_fg` into space reserved by hand, and its label is pushed clear by
reaching past `ComponentConfig` and writing `HasLabel::text_x_offset` on the
entity after the widget exists.

The cost is three things. Every gap in that row is now `wanted - 5`, so the
numbers in the source are not the numbers in the design and only a comment
connects them. The offset write is a public field on a vendored component with
no setter and no test covering it, so an afterhours bump that renames or
re-purposes it fails at compile time if we are lucky and silently mis-lays the
pill if we are not. And the whole thing has to be repeated, by hand, at every
call site in every project that wants an icon in a button.

**Minimal upstream fix.** Two additions, neither of which changes an existing
render path. Expose the field that already exists —
`ComponentConfig::with_text_offset(float x, float y = 0)` writing
`HasLabel::text_x_offset`/`text_y_offset` — and make `Dim::Children` fall back
to the measured label when a widget has a `HasLabel` and no child elements,
which is what every caller already means by `children()` on a button. The
padding-versus-label question then answers itself: `children()` returns
`measure_text + padding`, and the label's inset is the padding, not a literal
in the renderer.

CLASS: WORKAROUND


### #92 — Primitive shapes are not antialiased (MSAA is hardcoded off), so a small drawn glyph can never match a vector one

**What was wanted.** A sidebar row's state mark — an 8px dot, a 9px bang, an
8px cross, a 4x8 chevron, an open arc — drawn to match a reference client that
renders the same shapes as vector glyphs. Small ink, at 1x, where every edge
pixel is a large fraction of the shape.

**What happens.** Every one of hanabi's marks comes out with hard, binary
edges: a pixel is either the full colour or the background, with nothing in
between. The reference's same shapes carry two or three levels of partial
coverage on every curve and diagonal. Sampled out of the two captures, one
column of the arc, `#` = full ink and `.`/`:`/`*` = partial:

```
 reference                      hanabi
   .*:                            #####
 .#####*                         #######
 ###*###*                       ##     ##
*##   .##                       ##     ##
```

The cause is one line, in both of the backend's setup paths:

```cpp
// backends/sokol/backend.h
desc.sample_count = 1;                       // line 756, windowed (sapp_desc)
desc.environment.defaults.sample_count = 1;  // line 796, headless
```

`sgl` has no per-primitive antialiasing of its own — `draw_line_ex` emits two
triangles, `draw_ring_segment` a quad strip — so with the sample count pinned
at 1 there is nothing anywhere in the pipeline to soften an edge. Text escapes
this because it is sampled from a font atlas whose glyphs are already
antialiased, which is why a screen of hanabi text reads as smooth while every
shape on it reads as pixel art. The backend even documents the opposite: line
197 says "the default sgl context here matches the swapchain (4x MSAA)", 550
lines above the two places that set it to 1.

**Why the obvious escapes do not work.**

- **Drawing the shape bigger and letting the downscale soften it** — there is
  no downscale. hanabi renders at 1x logical pixels; the retina framebuffer is
  handled inside the backend and the shape is rasterized after that decision.
- **Faking coverage with a second, dimmer pass** (draw the shape again at
  +0.5px in a 40%-alpha ink) needs alpha blending of a shape over a shape,
  which is gap #13's other half — the fill path cannot alpha-blend, and the
  second pass lands as a solid halo rather than a soft edge.
- **Using the icon atlas instead** works only for shapes the atlas has. It has
  no bang, no cross, no small chevron and no arc (gap #20 is the same wall from
  the other side), and adding them means a PNG per glyph per colour, since the
  atlas is sampled rather than tinted per-draw.
- **Turning MSAA on from the app** — `Config` exposes width, height, title,
  flags and display mode; the sample count is not a field, so there is no
  caller-side way to reach it. `vendor/` is read-only here.

**The workaround, and its cost.** Accept the aliasing and spend the effort on
placement instead: every mark in this change is positioned and sized from a
row-by-row read of the reference's own pixels rather than from round numbers,
so the shapes land on the right rows and the right columns even though their
edges are hard. The cost is a permanent floor under any glyph-level parity
work. Measured on the twenty reference rows: a mark that is the RIGHT shape in
the RIGHT place still differs from the reference over ~90 pixels, against
~106 for a mark that is the wrong shape entirely. In other words the aliasing
is worth more of the diff than the correctness is — fixing nine wrong glyphs
moved the list region 14.32% -> 14.20%, and the whole glyph column can only
ever be worth ~1.2 points of it.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Take the sample count from `Config` (default 4, which
is what the comment already claims) and thread it into both `sapp_desc` and the
headless `sg_desc`, plus the `sgl_context_desc_t` for offscreen targets so a
render texture keeps matching its pass. Two struct fields and one plumb; no
call site changes for anyone who does not set it.

CLASS: WORKAROUND

### #93 — An absolutely positioned child can only be placed from the LEADING edge, so a trailing-edge overlay has to re-derive its parent's layout by hand

**What was wanted.** A sidebar row's star: a hover affordance that floats over
the trailing end of the row's title, anchored to the row's right edge, taking
no width from the title. Puffin's row reserves nothing for it — its trailing
items are all conditional and it has no star at all — and hanabi's version was
costing every one of twenty titles a permanent 18px whether or not a star was
ever drawn.

**What happens.** `with_absolute_position()` does the hard half correctly:
`autolayout.h:1356` skips absolute children in the flow pass, in the size pass
and in `solve_violations`, so an overlaid child genuinely costs its siblings
nothing. But the position it takes is `computed_rel[X] = absolute_pos_x`, one
raw number measured from the parent's LEADING edge, and that is the only
anchor there is. There is no trailing, bottom or centre form, and no percent
that resolves against the parent (`with_absolute_position(Size, Size)` exists,
but a `Percent` under an absolute element is the case `autolayout.h:398`
special-cases away).

So "18px in from the row's right edge" has to be written as a leading-edge
number the caller computes itself:

```cpp
.with_absolute_position(pixels(panelW - kCountRightPad - countW - kStarW),
                        pixels(6.0f))
```

Every term there is a copy of something the row already knows. `panelW` is the
row's own width, `kCountRightPad` is the row's own right padding, `countW` is a
sibling's measured width, and the `6.0f` is the row's top padding. The row is
the one object that has all four, and it is the one object that cannot be
asked: an absolute child is positioned before its parent's box is anything the
caller can read back.

**Why the obvious escapes do not work.**

- **Leaving it in flow and letting the row overflow.** The row is a NoWrap
  fixed-px Row; a child past the content box makes afterhours log a wrap
  warning and run `solve_violations` up to ten iterations EVERY FRAME. The
  sidebar's collapse tween sweeps 280 -> 52 and hits every intermediate width,
  so this is a measurable per-frame cost, not a one-off.
- **Reserving the slot only when the star will draw.** That is what a
  conditional column means, and it reflows the row's trailing columns under the
  pointer — the count jumps 18px sideways on hover. This is what the reserved
  slot was there to prevent, and it was the right call given the choice.
- **Drawing it in the TITLE's `on_draw_fg` instead of as a child.** The draw
  lands fine — but `on_draw_fg` gets a rect and nothing else, so the widget has
  no click of its own. The star stops being a button and the row has to
  hand-hit-test a sub-rectangle of itself against `ctx.mouse.pos`, which is
  gap #55's problem (no way to name or address part of a widget) reached from a
  new direction.
- **A negative margin.** `with_absolute_position()` warns and ignores margins
  outright: "For absolute elements, margins are position offsets only."

**The workaround, and its cost.** Compute the leading-edge number in the caller
from the four constants above, and hand-paint 18px of the row's current
background inside the star's own `on_draw_fg` before the glyph — an overlaid
child inherits nothing from what it covers, so without the chip the star lands
on top of the title's last letters and the two are unreadable together. Twelve
lines, and the cost is that the star's position is now a formula that does not
recompute when the row's padding does: change `kCountRightPad` and the star
silently drifts off the count while every test still passes, because nothing
in the layout engine relates the two any more. The chip is a second, quieter
cost — the row now has a rectangle of fill that must be kept in step with the
row's hover state by hand, and it is wrong for exactly one frame after the
pointer leaves.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`with_absolute_position( pixels(panelW - kCountRightPad - countW - kStarW)`) — positions the row star from a caller-computed leading-edge formula for the trailing edge. `src/ecs/sidebar_system.h` (`It paints its own 18px of row fill before the glyph`) — repaints the row background under the overlaid star because it is not integrated with the row fill. Tests: `tests/ui/tab_close_shows_on_an_unpinned_tab.e2e` (`test_tab_colors.cpp instead`) — documents that geometry around overlaid tab marks is pinned indirectly through extracted constants.


**Minimal upstream fix.** An anchor on `ComponentConfig`:
`with_absolute_position(Anchor::TrailingTop, dx, dy)`, resolved against the
parent's computed content box in the same pass that already reads
`absolute_pos_x`. Four enumerators and one subtraction where line 1359 sits;
the leading-edge form stays exactly as it is.

CLASS: WORKAROUND

### #94 — A scroll view's bar is a bare on/off bool, so "overlay scrollers" has to be re-implemented per frame by the caller, and the bar paints over the panel's own trailing edge

**What was wanted.** macOS overlay scrollers, which is what the reference client
asks for by name (`.background(OverlayScrollers())` on its session list):
nothing at rest, a bar while the list is being scrolled or the pointer is over
it, then gone again.

**What happens.** `HasScrollView` offers `show_scrollbar` (bool),
`scrollbar_thickness` and `scrollbar_min_thumb`, and `scrollbar_geometry()`
draws whenever `show_scrollbar && needed`. There is no auto-hide, no fade, and
no notion of "recently scrolled" anywhere in the component — `dragging_scrollbar`
is the only activity state and it only covers a thumb drag, not a wheel.

Worse for a sidebar: the bar is laid inside the panel's own right edge rather
than beside it, so it paints over whatever is there. hanabi's sidebar draws a
1px column rule at x=279 and the bar covers it for the panel's whole height —
600px of a (23,23,35) rule replaced by an 8px (100,100,112) stripe. Measured
against the frozen reference the moment the list became long enough to scroll:
the list region went 14.24% -> 17.25% structural, 2.8 of those 3 points being
the stripe alone. Puffin's frame has no bar in it at all.

**Why the obvious escapes do not work.**

- **`show_scrollbar = false` and live without one.** A list of 2000 sessions
  with no scroll indicator does not say it is scrollable or how far down you
  are. It is also the wrong answer to compare against a reference that has an
  overlay scroller and simply is not showing it in this frame.
- **Making it thinner or dimmer.** `scrollbar_thickness` is reachable, colour
  is not: the bar is drawn from `ctx.theme` inside `rendering.h` with no
  per-view override, which is gap #90 again — a per-widget colour is a
  frame-wide edit.
- **Padding the panel so the bar sits clear of the rule.** The padding applies
  to the panel's children, and the bar is drawn from the panel's own rect, so
  the rows move and the bar does not.

**The workaround, and its cost.** Drive the bool from the app, once per frame,
off hanabi's own hover test:

```cpp
scroll.ent().get<HasScrollView>().show_scrollbar =
    ctx.mouse_in_subtree(scroll.ent().id) ||
    ctx.mouse_was_in_subtree(scroll.ent().id);
```

Three lines and it gets the resting state right, which is the state a
screenshot is taken in and the state the reader spends all their time in. What
it does NOT get right is the half of the real behaviour that is about activity
rather than the pointer: a two-finger flick that carries momentum after the
pointer has left the sidebar scrolls a list with no bar on it, and a keyboard
`Page Down` never shows one at all. Reproducing that needs a timestamp and a
fade the component has nowhere to keep, so hanabi would have to hold a
per-scroll-view clock of its own beside every panel.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`has no auto-hide mode, only a show_scrollbar bool`) — documents the overlay-scroller limitation at the current sidebar scroll panel. `src/ecs/sidebar_system.h` (`scroll.ent().get<afterhours::ui::HasScrollView>().show_scrollbar =`) — drives the afterhours scrollbar boolean from the app's current/previous subtree hover state. Tests: `tests/ui/sidebar_scroll_keeps_row_text.e2e` (`assert_ui_text "PSC daily post generator" y=706`) — pins row visibility after sidebar scrolling under the current scrollbar/list implementation.


**Minimal upstream fix.** Replace the bool with a three-state enum —
`Always | Never | WhileActive` — and one `float last_activity` on
`HasScrollView`, bumped where `scroll_offset` is already written; `Always` is
the current behaviour and stays the default. Separately, subtract the bar's
thickness from the content box rather than overlaying it, or expose an
`overlay_scrollbar` bool so a caller with chrome at its trailing edge can
choose.

CLASS: WORKAROUND


---


---

### #95 — `clipboard.h` declares none of the symbols it calls, so a header that copies one string becomes a graphics translation unit

**What was wanted.** To unit-test the tab strip's palette. `tab_colors` is
arithmetic — a token, an alpha, a composite over a known fill — with a right
answer measured off the frozen reference, and it lived in
`src/ecs/tab_bar_system.h`. A test that includes that header and asserts three
colours.

**What happens.** Nothing under `tests/` can include `tab_bar_system.h`,
because it includes `vendor/afterhours/src/plugins/clipboard.h` for the tab
context menu's "Copy Navi URL", and that plugin does not compile on its own.
It is `#pragma once`, `<string>`, `<string_view>`, and then a three-way
`#ifdef` whose backend arms call symbols it never declares or includes:

```cpp
#elif defined(AFTER_HOURS_USE_METAL)
inline void set_text(std::string_view text) {
  std::string str(text);
  sapp_set_clipboard_string(str.c_str());   // sokol_app.h is nowhere in scope
```

Compiling a two-line TU that defines `AFTER_HOURS_USE_METAL` and includes only
this header gives `use of undeclared identifier 'sapp_set_clipboard_string'`
three times over. So the header is not includable; it is *pasteable*, and only
after something else has pulled the whole sokol stack in. Anything that wants
to put a string on the clipboard therefore has to be a full graphics TU.

The other arm of the same `#ifdef` is worse, and it is the one a test would
hit. With no backend macro defined at all, the file falls through to:

```cpp
#else
// Fallback implementations when no backend is available
inline void set_text(std::string_view) {}
```

That compiles silently and copies nothing. So the header has exactly two
behaviours — refuse to compile, or work as a no-op — and it picks between them
off a macro the includer may never have heard of, with no `#error`, no
`static_assert`, and no diagnostic of any kind. A test binary that copies to
the clipboard and asserts the clipboard passes by doing nothing twice.

**Why the obvious escapes do not work.**

- **Include `sokol_app.h` before it in the test.** That is the whole windowing
  backend, in a headless unit test, to reach a colour constant. It also needs
  the Metal frameworks at link time.
- **Define `AFTER_HOURS_USE_METAL` off in the test TU only.** The fallback arm
  then compiles, but every other afterhours header the file needs keys off the
  same macro, so the TU no longer agrees with the app about what `Color`,
  `RectangleType` or the UI plugin are.
- **Include the plugin lazily at the call site instead of the header top.** The
  call site is a lambda inside a `ComponentConfig` chain in the middle of a
  function; there is no scope where a `#include` helps, and the header is
  needed for the declaration either way.
- **Forward-declare `sapp_set_clipboard_string` in hanabi.** It is a C symbol
  with a stable signature, so this does link — but it puts a private
  redeclaration of somebody else's ABI in the app, which is worse than a file
  split, and it does nothing about the silent-no-op arm.

**The workaround, and its cost.** The palette moved out to
`src/ecs/tab_colors.h`, which includes `theme.h` and afterhours'
`rounded_corners.h` and nothing else, and `tab_bar_system.h` includes that. The
test then reaches the shipped constants rather than a copy of them
(`tests/unit/test_tab_colors.cpp`). Cost: a new header and one more include, and
the split is arbitrary from the outside — the file boundary records a
limitation of a clipboard plugin, which is not a thing anyone reading
`tab_colors.h` would guess. Anybody who moves a colour back into
`tab_bar_system.h` for tidiness silently removes it from the test.


**Hanabi reference.** `src/ecs/tab_colors.h` (`Split out of tab_bar_system.h so a headless unit test can reach it`) — the palette was moved to a graphics-free header because tab_bar_system includes the clipboard plugin. `src/ecs/tab_bar_system.h` (`include afterhours after sokol_impl.mm has pulled in sokol_app.h`) — current tab_bar_system still isolates clipboard usage in the graphics TU. Tests: `tests/unit/test_tab_colors.cpp` (`#include "../../src/ecs/tab_colors.h"`) — unit tests reach the extracted palette header without including clipboard.


**Minimal upstream fix.** Include the backend header the arm actually uses —
`sokol_app.h` under the Metal arm, raylib under the other — the way every other
plugin in that directory does, and replace the silent `#else` with an
`#error "afterhours/clipboard: define AFTER_HOURS_USE_RAYLIB or
AFTER_HOURS_USE_METAL"`, or keep the no-op arm behind a macro the caller has to
ask for by name. Two lines, and the header becomes includable and honest.

CLASS: WORKAROUND

---

### #96 — NOT A GAP: a translucent SHAPE blends correctly inside `on_draw_fg`, and only the texture path needs its own pipeline

Written down because the evidence in front of you points the other way, and
acting on it costs every call site something.

Puffin draws a pinned tab's pushpin as `mutedText` at `.opacity(0.7)`
(`TabStrip.swift:506`), so hanabi's wants an alpha too. The nearest thing to
guidance in hanabi's own tree is `src/ui/icons.h`, where the atlas blit is
wrapped in a manual pipeline dance:

```cpp
// Push a blend-enabled pipeline so the atlas' transparent pixels
// don't blit as opaque black (sgl's default pipeline has blending
// off — see the AtlasTexture note / afterhours_gaps.md).
sgl_push_pipeline();
sgl_load_pipeline(AtlasTexture::get().blend_pipeline());
```

Read that and the conclusion is obvious and wrong: alpha does not survive an
`on_draw_fg`, so a translucent mark has to be resolved by hand — 
`theme::over(ink, backdrop)` at the call site, which means every call site has
to know what it is sitting on, and a mark drawn over a surface whose colour is
not in scope cannot be translucent at all.

Probed instead of assumed: the same pin drawn both ways, pre-composited with
`theme::over` and handed straight through with `a = 179`, then the two captures
diffed. **Identical pixels** — (107,107,119) on the inactive tab and
(113,117,134) on the active one, both ways. `Backend::begin_drawing` loads
`g_blend_pip` for the whole frame (`backends/sokol/backend.h:403`), so
`draw_rectangle`, `draw_line_ex` and the rest are already blending src-over.

What the comment in `icons.h` is actually about is narrower than it reads: the
TEXTURE path calls `sgl_texture` and the atlas keeps its own pipeline object,
so a blit specifically needs that push. Shapes do not, and the general claim
"sgl's default pipeline has blending off" is true of `sgl_defaults()` and not
of the state any hanabi draw callback ever runs in.

So: pass the alpha. It is one less thing that has to know its own backdrop, and
it is what makes a translucent mark work over a surface that is itself hovered
or themed. Filed as a numbered entry rather than a footnote because the wrong
version of this belief is cheap to hold and expensive to unpick — it was held
here for about an hour and shipped a `pinInk` local that existed for no reason.

CLASS: NOT A GAP




**Hanabi reference.** Negative result: `src/ecs/tab_colors.h::pin_ink` — pin ink remains a translucent color passed to shape drawing. `src/ecs/tab_bar_system.h` (`The alpha goes to the GPU rather than being resolved here`) — current source deliberately does not pre-compose the pushpin shape. Tests: `tests/unit/test_tab_colors.cpp::test_the_rule_survives_a_light_theme` — checks the translucent pin-ink rule survives theme changes.

---

### #97 — An absolute child cannot be sized against what it overlays: `percent()` is refused outright, so every overlay is handed a literal pixel width

**What was wanted.** Puffin's per-message Copy affordance, verbatim from
`Sources/Views/AgentcloudTranscriptView.swift`:

```swift
VStack(alignment: .leading, spacing: 4) { Text(row.text) ... }
  .padding(.horizontal, 12).padding(.vertical, 9)
  .background(Color(fill))
  .overlay(alignment: .topTrailing) { copyButton }
```

An overlay in the bubble's own coordinate space, aligned to its trailing edge,
taking no layout space. There is no width anywhere in it: "as wide as the thing
I am on" is the default, and it stays true when the bubble is re-measured —
which for a user bubble in this app is every frame, because the bubble is
shrink-to-fit and its width is computed from its text (gap #87).

**What happens.** afterhours has the overlay — `with_absolute_position()` sets
`computed_rel` from the parent and `solve_absolute_children` keeps the subtree
solved — and it cannot be sized relative to the parent. `percent()` on an
absolute widget is not merely wrong, it is rejected:

```cpp
// autolayout.h, compute_size_for_parent_expectation
if (widget.absolute && exp.dim == Dim::Percent) {
  VALIDATE(false, "Absolute widgets should not use Percent");
}
```

`children()` is no good either — it sizes to the overlay's OWN children, so the
bar shrinks to the Copy button and the right-alignment inside it means nothing.
So the only sayable width for an overlay is `pixels(N)`, and N has to be the
parent's width, which the parent knows and the child cannot ask for.

**Why the obvious escapes do not work.**

- **`expand()`** resolves in `distribute_expand_space`, which runs over a
  parent's flow children; an absolute child is skipped everywhere its size
  would feed into its parent's, so it never gets a share. It comes out 0.
- **Reading the parent's resolved rect** is a frame late by construction. The
  tree being built now has not been laid out — that is the same reason
  `mouse_was_in_subtree` exists — so an overlay sized from `rect().width` is
  one frame behind every resize, and on a shrink-to-fit bubble it is one frame
  behind every keystroke.
- **Putting the overlay outside the bubble** (a sibling, absolutely positioned
  over it) needs the bubble's width AND its origin, so it trades one unknown
  for two.
- **`with_translate` alone**, without a width, positions a box that is still
  sized by the rules above.

**The workaround, and its cost.** `MainPaneSystem::message_actions()` takes the
host's width as a parameter, and both call sites pass the same expression the
bubble itself was built from — `box.bubbleW` for the user turn,
`asst_bubble_w(paneWidth)` for the assistant. The overlay is
`pixels(hostW) x pixels(22)`, absolute at (0,0) of the bubble, right-justified.

The cost is a third caller of the width, on top of the two gap #87 already
forced (the measure pass and the draw). Nothing checks that the three agree:
if `user_box()` ever changes and one call site is missed, the Copy button
detaches from the bubble's corner by exactly the drift, on hover only, in a
state no screenshot test captures. The same fact — how wide is this bubble —
is now written in three places and owned by none of them.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`message_actions(UIContext<InputAction>& ctx, Entity& host,`) — message action overlays take the host width as an explicit parameter. `src/ecs/main_pane_system.h` (`with_size(ComponentSize{pixels(hostW), pixels(kMsgActionsH)})`) — sizes the absolute Copy bar with a literal pixel width, not percent(). Tests: `tests/ui/message_copy_on_hover.e2e::Copy` — covers the shipped hover Copy affordance that uses the literal-width overlay.


**Minimal upstream fix.** Let `Dim::Percent` on an absolute widget resolve
against the PARENT's computed size instead of failing the validate. The parent
is solved before `solve_absolute_children` runs, so the number is already
there; this is the one case where percent has an unambiguous meaning for an
absolute child, and it is the meaning every overlay wants. A `with_fill_parent()`
sugar over it would cover the whole alignment family (`topTrailing` and
friends) without any caller doing arithmetic.

CLASS: WORKAROUND


### #100 — The renderer's private 5px text margin is in DEVICE pixels, so every label slides toward its leading edge as `ui_scale` rises

**What was wanted.** To render hanabi's UI at 2x and reduce it to 1x, so that
its glyphs and Puffin's are sampled the same way and the parity metric's
8–12% text floor becomes measurable. `theme.ui_scale = 2.0` in Adaptive mode is
the supported way to ask for that: it multiplies every `pixels()` value in the
tree, explicit font sizes included, so a 2360x1898 render at scale 2 is the
same UI at twice the size.

**What happens.** Every label lands 2.5px to the left of where the 1x build put
it, and at scale 3 it lands 3.3px left. Gap #75 already named the cause —
`draw_text_in_rect` builds `Vector2Type margin_px{5.f, 5.f}` as a literal and
hands it to `position_text_ex`, which offsets the glyph run by it, and nothing
on `ComponentConfig` reaches the value. What #75 did not say, because at the
time there was only one scale, is that **the literal is never multiplied by
`ui_scale`**. It is 5 DEVICE pixels at every zoom level, which is
`5 / ui_scale` logical pixels — so it is not an inset at all, it is a
scale-dependent one, and it moves the text relative to its own box every time
the app zooms.

Four render scales of the same build, measuring the "Settings" nav label's ink
start in device pixels and dividing by the scale to get logical pixels:

| ui_scale | label starts at | `5/s` | residual |
|---|---|---|---|
| 1.0 | 37.000 | 5.000 | 32.00 |
| 1.5 | 34.667 | 3.333 | 31.33 |
| 2.0 | 34.000 | 2.500 | 31.50 |
| 3.0 | 33.333 | 1.667 | 31.67 |

The residual is flat to within 0.7px across a 3x range — that is the first
glyph bearing — so the drift is exactly `5/s` and nothing else. The same
measurement across the eighteen visible session-row titles: the left inset goes
from 5.5px at 1x to 2.8px at 2x, against the reference's 6.6px.

The same literal also sets `max_text_size.x = container.width - 2 * margin_px.x`,
so an auto-fitted label gets 10 device pixels more room to grow into at scale 2
than the same label had at scale 1 — the fitted size is scale-dependent too.
`rendering.h:681` and the sibling `{5.f, 5.f}` at `:640`, `:846` and `:2186`
are all the same constant.

**Why the obvious escapes do not work.**

- **Author every left pad as `design_inset - 5.0f`** — #75's workaround, and it
  is what hanabi does. It is only correct at one scale. At scale 2 the
  compensation over-corrects by 2.5px in the opposite direction, so a build
  tuned for 1x is wrong at 2x and vice versa; there is no pad that is right at
  both.
- **Compensate by scale in app code** — the correction is
  `5 * (1 - 1/ui_scale)` per label, which means every one of the app's ~200
  label call sites grows a term that depends on a global, for a margin the
  caller never asked for.
- **Set the margin to zero** — there is no way to; that is #75.
- **Use `text_x_offset`** — `HasLabel::text_x_offset` is applied before the
  margin, not instead of it (`rendering.h:1616`), it is not on
  `ComponentConfig` (#91), and it too is a raw float nobody scales.
- **Avoid the zoom** — then the app has no zoom, and the capture cannot be
  supersampled. Which is the point.

**The workaround, and its cost.** For the capture: none — the drift is accepted
and reported. Measured against `ref/01_home.png`, the misregistration is worth
**+1.49 points on the VIEWS region and roughly +1.3 on the row titles**, which
is more than everything a 2x capture gains elsewhere. Removing it with a
sub-pixel shift sweep (so the comparison is at each capture's best offset)
recovers all of the VIEWS regression — 6.02% back to 3.85%, against 1x's 3.66%
— which is how we know the rest of the 2x result is not a registration
artefact. For the app: hanabi's zoom is usable but every label is up to 2.5px
off its designed inset, in a direction that depends on the zoom level.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Resolve the margin through the scaling cascade the
same way the font size beside it already is. `position_text_ex`'s callers
already have `cmp.resolved_scaling_mode` and `theme.ui_scale` in hand two lines
earlier (`rendering.h:2177-2180`); passing `Vector2Type{px(5), px(5)}` instead
of `{5.f, 5.f}` is a one-line change per call site and a no-op at scale 1. The
better version is still #75's: honour the element's own padding and drop the
private margin entirely.

CLASS: WORKAROUND

---

### #101 — There is no supersampled capture: `ui_scale` is a LAYOUT zoom, and a zoom is not a supersample

**What was wanted.** hanabi's parity references are Puffin captured at 2x on a
retina panel and reduced to 1x. To compare like for like, hanabi's own headless
capture needs to render the same 1x layout into a 2x buffer and be reduced the
same way — supersampling, in the ordinary sense: the geometry is unchanged and
only the sampling rate goes up.

**What happens.** afterhours has no way to express that. `graphics::Config`
carries `display`, `width`, `height`, `target_fps`, `flags` and `title`; there
is no render scale, no framebuffer scale, and no sample count (the sample count
is pinned at 1 in both setup paths — that is #92). `Config.hidpi` exists but is
honoured only by the raylib backend, and hanabi is on Metal/sokol.

The only lever that does anything is `theme.ui_scale`, and it is a different
operation. It multiplies `pixels()` values **before layout**, so the UI is
re-laid-out at the larger size and every string is re-measured, re-fitted and
re-advanced at the larger font size. That is a browser zoom. A supersample
would leave the layout alone and scale the transform.

The difference is not academic; it is the whole result of this branch. Rendered
at 2360x1898 with `ui_scale 2.0` and reduced to 1180x949 with LANCZOS, measured
over the eighteen visible session-row titles against `ref/01_home.png`:

| | reference | hanabi 1x | hanabi 2x-reduced |
|---|---|---|---|
| mean ink px per title | 685 | 640 | 662 |
| mean string width | 148.5 | 148.3 | **150.6** |

The ink weight improves — half the deficit against CoreText's stem-darkened
render closes, which is the visible win. But the strings come out **2.3px wider
on average**, because fontstash measured and advanced them at 33px and the
result was halved, which is not the same as advancing at 16.5px. A true
supersample would have reproduced the 1x advances exactly and only softened the
edges. Scored at each capture's own best sub-pixel offset, so registration is
not in it, the row-title column goes **14.57% → 15.86%**: the zoom costs more
in placement than it gains in weight.

Nothing in the frame that is not text improves or regresses this way — the
hand-drawn row marks go **6.92% → 5.67%**, a 1.25-point win, because a shape
has no advances to get wrong and the reduction is the antialiasing #92 says
does not exist. So the two operations are separable in principle and the
library conflates them.

**Why the obvious escapes do not work.**

- **Render into a 2x texture without `ui_scale`** — the original `main.cpp`
  comment's version, and it is right: the layout just uses the extra room (a
  thin sidebar in a big canvas). Confirmed; not retried here.
- **`ui_scale` plus a 2x framebuffer** — what this branch built. It works, it
  is a correct zoom, and it is measured above: not a supersample.
- **Scale the sgl transform by 2 around the whole frame** — `sgl` matrix
  helpers are not exposed through the graphics facade, the UI plugin issues its
  own draw calls, and the text path bakes the font atlas at a size chosen from
  the resolved font size, so a transform scale would blit 1x glyph bitmaps at
  2x and look worse than 1x.
- **Turn on MSAA instead** — not reachable either, and it is #92; MSAA also
  does nothing for text, which is atlas-sampled.
- **Patch the backend** — `vendor/afterhours` is read-only here: ~20 projects
  vendor it.

**The workaround, and its cost.** `HANABI_SHOOT_2X=1` on both shoot scripts
does the zoom-and-reduce, and it is **off by default because it is worse
overall** (shared-surface structural 9.12% → 9.68% on `01_home`, 5.84% → 6.00%
on `02_thread`). It is kept because it is the only way to see the row-mark win,
and because having measured it is what stops the fourth investigation from
concluding "the rest is the rasterizer" and reaching for it again. Cost: the
parity harness has no like-for-like capture and, per the measurement above,
cannot get one from this library.


**Hanabi reference.** Proof patch or spike, not shipped: `scripts/shoot_hanabi.sh` (`2x CAPTURE (HANABI_SHOOT_2X=1)`) — retains the opt-in zoom-and-downsample experiment while leaving it off by default. `src/main.cpp` (`HANABI_SHOOT_2X=1 scripts/shoot_hanabi.sh`) — documents that HANABI_UI_SCALE is a zoomed capture path, not a true render-scale path. Tests: `tests/ui/ui_scale_is_a_zoom_not_a_bigger_canvas.e2e` (`ui_scale is a ZOOM, not a bigger canvas`) — guards the app's layout handling under ui_scale without claiming it is supersampling. Measurement/gate: `docs/visual-parity/PUFFIN_SPEC.md` (`HANABI_SHOOT_2X=1`) — records that zoom-and-reduce is available but scores worse overall and is not a supersample.


**Minimal upstream fix.** A `render_scale` on `graphics::Config` (default 1.0)
that multiplies the offscreen target's dimensions and the view transform,
leaving the layout's logical size alone — the offscreen path already builds its
own `sg_desc` and `sgl_context_desc_t`, so it is where the two dimensions are
chosen and the only place that needs to know. Pair it with #92's sample count,
which is the other half of the same struct.

CLASS: IMPOSSIBLE

---

### #102 — `on_draw_fg` is handed a SCALED rect and no scale, so every shape the library cannot draw silently stays 1x when the app zooms

**What was wanted.** hanabi draws a lot of its own chrome, because the library
cannot: five row-state marks (#92 — no antialiased primitives), a mute ring, an
attention triangle, a disclosure chevron, a send arrow, a pushpin and a radio
(#48 — the font has no arrows, no geometric shapes, no box-drawing), plus every
icon-atlas blit (#13/#15 — the default pipeline has blending off). All of them
go through `.with_on_draw_fg(cb)` or an immediate-mode helper. At `ui_scale
2.0` they should be twice the size, like everything else.

**What happens.** They stay exactly 1x, inside a frame where everything around
them has doubled. An 8px dot is an 8px dot in a 32px row.

The callback's signature is `std::function<void(RectangleType)>`. The rect it
receives has been through the whole scaling cascade and is correct. Every
number **inside** the callback — a radius, a stroke width, a sprite's blit
size, an optical y-bias, a hand-positioned sub-rect — is a literal in app code
that the library never sees and therefore never scales. There is no scale
argument, no second parameter, no context object, and `HasUIModifiers::scale`
is applied to the widget's own transform rather than exposed to the callback.

It is not visually obvious either. Everything is in the right PLACE, because
the rect is right; the marks are just small. On a first 2x capture of this app
it read as "the icons are a bit light" for several minutes.

**Why the obvious escapes do not work.**

- **Read the global theme inside the callback** — `imm::ThemeDefaults::get()
  .theme.ui_scale`, which is what hanabi now does (`viewport::px`). It works
  and it is a no-op at scale 1, but it is wrong for any widget that overrides
  its own scaling mode: `with_scaling_mode(ScalingMode::Proportional)` means
  that widget's `pixels()` are NOT scaled, and the global read cannot know
  that. The correct value is `resolved_scaling_mode == Adaptive ? ui_scale :
  1.0f`, which lives on the widget's `UIComponent` — reachable from a system,
  not from inside the callback, which has no entity.
- **Capture the scale when the callback is built** — the callbacks are built
  every frame in immediate mode, so this is the same read one line earlier, and
  it has the same defect plus a stale-by-one-frame window on a zoom change.
- **Derive it from the rect** — the rect is scaled, but so is the slot it came
  from; there is no unscaled reference to divide by.
- **Size everything from the rect instead of from literals** — works for a mark
  that fills its slot and not for one measured off a reference, which is all of
  hanabi's: the arc is 4.8px at a 13px slot because the reference's arc is
  4.8px, not because it is 0.37 of a slot.

**The workaround, and its cost.** `hanabi::viewport::px(v)` — `v * ui_scale`,
read from the global theme — wrapped around 25 literals in `src/ui/icons.h` and
`src/ecs/sidebar_system.h`. Two costs. Every future `on_draw_fg` has to
remember, and forgetting is invisible at the only scale anyone tests at; and
the app can no longer use per-widget `Proportional` overrides anywhere near a
drawn glyph without the two disagreeing. Worth it: the marks are the one part
of the frame a supersampled capture measurably improves, 6.92% → 5.67% against
`ref/01_home.png`.


**Hanabi reference.** `src/ui/viewport.h` (`inline float px(float v)`) — provides the app-side logical-to-device scale wrapper for draw callbacks. `src/ecs/sidebar_system.h::hanabi::viewport::px(kDotR)` — row status marks wrap radii/strokes in viewport::px. Tests: `tests/ui/ui_scale_is_a_zoom_not_a_bigger_canvas.e2e` (`sidebar's live footer cluster scales with everything else`) — covers scaled footer/sidebar geometry under HANABI_UI_SCALE=2.


**Minimal upstream fix.** Pass the resolved scale to the callback —
`std::function<void(RectangleType, float)>`, or a small `DrawContext{rect,
scale}` if the signature is worth keeping stable. The renderer has
`cmp.resolved_scaling_mode` and `ctx.theme.ui_scale` in hand at the call site,
so it is the one place that can answer correctly, and a callback that ignores
the second argument behaves exactly as it does today.

CLASS: WORKAROUND


---

### #103 — `measure_text` returns the ink BOX, not the advance, so every hug-to-text box is short by its own side bearings

**What was wanted.** A code line's chip sized to exactly the surface the
reference draws behind that line. hanabi computes it the only way #87 leaves
open — measure the string, add the padding, set `pixels()`:

```cpp
const float chipW = std::ceil(theme::text_px(shown.c_str(), theme::type::MD))
                    + 2.0f * kCodeChipPadX;
```

**What happens.** The chip comes out short of the text it is hugging, by an
amount that depends on which characters the string starts and ends with, and by
a whole space if it ends with one. `theme::text_px` bottoms out in the Metal
backend's `measure_text`, which is:

```cpp
fonsTextBounds(ctx, 0, 0, text, nullptr, bounds);
float w = (bounds[2] - bounds[0]) / dpi;      // <- the INK box
```

`bounds` is fontstash's glyph-quad union: `minx`/`maxx` are updated from
`q.x0`/`q.x1` per glyph, so they describe where the ink is, not where the pen
ends up. The advance is the function's own RETURN value — `advance = x - startx`
— and it is discarded. So the measurement drops the first glyph's left side
bearing, the last glyph's right side bearing, and the full advance of any
trailing space (a space contributes a zero-width quad at the pen, so it moves
`maxx` no further than the glyph before it did).

Measured on `ref/02_thread.png`'s fence, mono at 12px, whose advance is 5.0px
flat:

| | |
|---|---|
| `"exit 65"`, 7 glyphs | advance width **35px** |
| `theme::text_px` says | **30px** |
| chip drawn (measure + 2x6 padding) | 42px |
| chip that the advance would have drawn | 47px |
| the reference's chip | 62px |

So a quarter of that chip's shortfall against Puffin is this, and the rest is
the face. The same 5px is missing from every other box in the app that hugs a
string, and nobody would find it by looking: a box that is 5px tighter than its
text still contains its text, because the missing width is bearing and
whitespace by construction.

The same measurement drives `draw_runs_in_rect`'s pen: it advances
`x += weighted_width(run.text, run.weight)` between styled runs, so a
syntax-highlighted line's coloured runs each start slightly left of where the
same string would put them unstyled. On this fence that is 1px over the eight
character cells between `matched` and `'fbmacos…'` — small because mono
bearings are small, and it scales with the run count and with the face.

**Why the obvious escapes do not work.**

- **Add a fudge to the measurement.** The error is per-string, not constant:
  it is the two side bearings of the particular first and last glyphs plus any
  trailing space. `"exit 65"` loses 5px and `"WM"` loses under one.
- **Measure `text + "x"` and subtract `"x"`.** Recovers the trailing side
  bearing and not the leading one, and gets the trailing-space case wrong in
  the other direction, because `"exit 65x"`'s box now ends at the `x`'s ink.
- **Use the other measurement helper.** `measure_text_line` in
  `text_measure.h` and the single-argument overload in `font_helper.h:60` DO
  use `fonsTextBounds`'s return value, i.e. the advance. They are not reachable
  from `ComponentSize` sizing or from `draw_runs_in_rect`, both of which call
  the `Vector2Type` overload for the height as well as the width. **The two
  overloads in the same file disagree about what "the width of this string"
  means**, which is the part worth fixing whatever else happens.
- **`Dim::Text`.** #87: it measures unwrapped and cannot be capped, and it goes
  through the same `measure_text`.

**The workaround, and its cost.** None taken. The honest correction is to add
the advance back, and hanabi cannot compute it: it has no reachable API that
reports it for a string it is about to draw. hanabi's fence now sidesteps the
question for every line but the last — those are `percent(1.0f)`, which the
reference wants anyway — so the defect is confined to one chip per block and
costs 5px there. Every other hug-to-text box in the app still carries it
un-measured.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The broad renderer/API issue remains, but the claim that hanabi has no reachable advance-style helper is stale for current user_box code.

**Hanabi reference.** Current code: `src/ecs/main_pane_system.h::afterhours::ui::measure_text_line` — current user_box span measurement uses the advance-returning helper for wrapped line widths. `tests/ui/code_fence_only_its_last_line_hugs.e2e` (`assert_ui_text "exit 65" w=42`) — the code-fence test still documents the last-line chip width affected by text measurement choices. Tests: `tests/ui/code_fence_only_its_last_line_hugs.e2e` (`Every line before it does not`) — pins the current code-fence behavior after the earlier hug-to-text assumptions were corrected. Measurement/gate: `docs/visual-parity/FRICTION_LOG.md` (`measure_text returns the ink BOX, not the advance`) — records the original measurement and side-bearing diagnosis.


**Minimal upstream fix.** In `backends/sokol/font_helper.h`, take the advance:

```cpp
float adv = fonsTextBounds(ctx, 0, 0, text, nullptr, bounds);
float w = adv / dpi;                       // was (bounds[2] - bounds[0]) / dpi
```

That is the value the single-argument overload twenty lines above already
returns, so the change makes the file self-consistent rather than introducing a
new convention. Anything that genuinely wants the ink box — a focus ring drawn
tight to the glyphs, say — wants a separate `measure_text_ink()` and does not
have one today either.

CLASS: WORKAROUND

---

### #104 — A scripted test cannot assert that an element is ABSENT, or that a border was painted, so removing chrome is unverifiable

**What was wanted.** A test that goes red if hanabi's unlabelled code fence
grows its language bar back.

The bar was being emitted at zero height for a fence with no language, and a
zero-height div still paints its border: `with_border_bottom(code_bg(), 1)`
drew a 1px rule of the fence's own dark colour clean across the assistant
bubble — 656 pixels of surface `ref/02_thread.png` has nothing at. It survived
a previous change that set out to remove exactly this strip, because that
change removed the bar's CHILDREN and left the bar.

**What happens.** Neither the defect nor the fix can be expressed.

- `assert_ui <name> x/y/w/h/hidden/text` is the whole vocabulary (#61 has the
  colour half of this). A border is none of those six: it is not the rect, it
  does not change the rect, and it has no text.
- There is no negative assertion. `ui_commands.h` has `assert_ui`,
  `assert_ui_text`, `expect_text`, `expect_focused`, `expect_checkbox`,
  `expect_slider`, `expect_input_text`, `expect_input_selection`,
  `expect_selected_text` — every one of them names an element and asserts
  something about it, and every one FAILS when the element is not there. So
  "this element should not exist" is exactly the shape the harness cannot say,
  and it is the shape every piece of removed chrome has.
- The two states are geometrically identical anyway. A bar at `h=0` and no bar
  at all produce the same layout for everything around them, so even a
  y-coordinate test on the block's first line passes in both worlds.

**Why the obvious escapes do not work.**

- **Assert the bar with `h=0` and invert the expectation by hand.** There is no
  inversion; a passing assertion cannot be spelled as a failure.
- **Assert on `dump_ui`.** It prints; the runner has no way to match against
  what it printed.
- **Give the bar a debug name per state and assert the other one.** Renaming
  the element hides the problem rather than testing it, and the failure mode
  being guarded against is precisely "an element nobody meant to emit is being
  emitted".
- **Screenshot the frame and diff it.** That is what `docs/visual-parity` does
  and it is a different harness, run by hand, outside `make test`.

**The workaround, and its cost.** The removal is held by the parity captures
and by a paragraph in the test file saying it is not held by the test. Cost:
the one class of change this project makes constantly — deleting chrome the
reference does not draw — is the one class it cannot regression-test, and this
particular strip has now been removed twice.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Two small commands, both of which the runner already
has the machinery for:

```
assert_ui_absent <debug_name>          # fails if the element exists this frame
assert_ui <name> border=<n>            # the sixth field, beside w/h/hidden
```

`assert_ui_absent` is the general one and worth more: every "we stopped drawing
that" change in every app that uses this harness is currently untestable.

CLASS: TEDIOUS

### #105 — A text field's PLACEHOLDER colour is not a property of the field: it is whatever `ctx.theme.font_muted` holds when the field is built

**What was wanted.** hanabi's composer hint in Puffin's colour. Puffin draws
"Message Agentcloud… (↵)" in `mutedText` (140,140,166); hanabi drew "Message
hanabi…" at (94,94,106) measured off the two frames, the largest single colour
gap left in the composer band.

**What happens.** `text_input` hardcodes the hint's ink to one global field:

```cpp
// vendor/afterhours/src/plugins/ui/text_input/component.h:188
const bool show_placeholder =
    display_text.empty() && !config.placeholder.empty();
field_label.label = show_placeholder ? config.placeholder : display_text;
if (show_placeholder)
  field_label.explicit_text_color = ctx.theme.font_muted;
else if (config.custom_text_color.has_value())
  field_label.explicit_text_color = config.custom_text_color;
```

`ComponentConfig` has `with_placeholder(std::string)` and no
`with_placeholder_color`. So a field's TYPED text is configurable per widget
(`custom_text_color`, the `else if`) and its HINT is not — the two branches of
one `if`, one of which reads the caller and one of which reads a global.

That global is shared by every muted thing in the frame: section captions,
timestamps, disabled labels, the sidebar's own faint text. Any app whose hint
should not be the same colour as its dimmest body text has to move all of them
together or accept the wrong hint.

**Why the obvious escapes do not work.**

- **`with_custom_text_color`.** Reached only by the `else if`, i.e. only once
  the field has real text in it. It sets the colour of what the user types,
  which is a different string in a different state.
- **Draw the hint yourself as an absolutely-positioned `on_draw_fg` child over
  an empty field.** This is what hanabi did before `with_placeholder` existed
  and it is worse: the overlay does not know the field's h-scroll or its caret,
  it has to be removed on the first keystroke by the caller, and it cannot be
  clipped to the field without re-deriving the field's inner rect (#93, #97).
- **Set `ctx.theme.font_muted` for the whole pane.** Available, and it is a
  frame-wide edit for one label — the pane's captions and timestamps move with
  it. hanabi's main pane already re-asserts `font_muted` every frame precisely
  because it is global (#90).

**The workaround, and its cost.** Save `ctx.theme.font_muted`, set it, build
the one `text_input`, restore it:

```cpp
const auto savedMuted = ctx.theme.font_muted;
ctx.theme.font_muted = theme::text_secondary();
auto inputRes = afterhours::ui::imm::text_input(ctx, mk(...), draft, cfg);
ctx.theme.font_muted = savedMuted;
```

**This works, and #90 says it should not, so the distinction matters.** #90's
claim is that `ctx.theme` is one global struct read at RENDER time, which makes
a set/restore around a build call useless. That is true of `Theme::Usage::*`
values, which are resolved late. It is NOT true of this line: it *copies a
concrete colour into the entity* during the imm build, so the value that
matters is the one live at the call, and the window is exactly one call wide.
Verified by measurement, not by reading — the hint's ink moved and nothing else
in the pane did.

Cost: four lines and a paragraph of comment at every call site that wants a hint
in its own colour, and a save/restore that is silently load-bearing — delete the
restore and the rest of the pane's muted text changes colour, in a way no test
in the harness can see (#61: `assert_ui` has no colour predicate).


**Hanabi reference.** `src/ecs/main_pane_system.h` (`const auto savedMuted = ctx.theme.font_muted`) — composer placeholder color is scoped with save/set/build/restore around the text_area call. `src/ecs/main_pane_system.h` (`text_area( ctx, mk(inputWrap.ent(), 1), replyDraft`) — the current widget is text_area, but it still relies on the global font_muted placeholder color path. Tests: `tests/ui/composer_hints_are_legible.e2e` (`expect_text "Enter to send"`) — covers the composer hint visibility/legibility behavior that depends on the placeholder-color workaround. Measurement/gate: `src/ecs/main_pane_system.h` (`Worth 68 of the hint row's 962 differing pixels`) — records the measured visual effect of the placeholder color change.


**Minimal upstream fix.** One field and one line, symmetrical with the branch
directly below it:

```cpp
// component_config.h
std::optional<Color> placeholder_color;
ComponentConfig &with_placeholder_color(Color c) {
  placeholder_color = c; return *this;
}

// text_input/component.h:194
if (show_placeholder)
  field_label.explicit_text_color =
      config.placeholder_color.value_or(ctx.theme.font_muted);
```

Backwards compatible: unset keeps today's behaviour exactly.

CLASS: TEDIOUS

---

### #106 — A primitive cannot be antialiased, and the ONE escape that works needs a flat, known background: `bg + c*(fg-bg)` is a colour, not a blend

**What was wanted.** A sidebar row's bang — the mark on six of the eighteen
visible rows — drawn to the reference's own measurement: a vertical stroke
**1.95px wide**, which the reference lays down as 0.44 / 0.97 / 0.50 coverage
across three columns.

**What happens.** `draw_line_ex` at 1.95px emits two triangles into a pipeline
with `sample_count = 1` (gap #92), so the stroke rasterizes to whole columns at
full strength. There are exactly two answers available and neither is the
shape:

| thickness asked | columns lit | ink laid down | reference |
|---|---|---|---|
| 1.4 | 2 hard | 2.00 | 1.95 in three columns |
| 2.3 | 3 hard | 3.00 | 1.95 in three columns |

hanabi shipped the second. Measured against `ref/02_thread.png`, its bang
carried **twice the reference's ink** — 30.0 coverage against 14.8 — on the
most common mark in the list.

**Why the obvious escapes do not work — and one that gap #92 said does not, and
does.**

- **A thinner stroke** does not get lighter, it drops to a hairline: without
  antialiasing, coverage is quantized to whole pixels, so 1.4px and 1.0px paint
  the same two columns. This is `hanabi::glyph::chevron`'s comment, arrived at
  from the other side.
- **A second, dimmer pass at +0.5px** — gap #92 lists this and rules it out,
  correctly, because the fill path cannot alpha-blend a shape over a shape
  (gap #13) and the second pass lands as a solid halo.
- **The escape that works, and why #92 missed it: it needs no blending at
  all.** A partly covered pixel is not a translucent pixel. Over a background
  whose colour is KNOWN, the composited result of coverage `c` is the opaque
  colour `bg + c*(fg - bg)`, which the caller can compute and lay down as a
  solid rectangle. #92 reached for alpha because that is how a renderer does
  it; the caller does not have to, because the caller can do the compositing
  arithmetic itself. Coverage of an axis-aligned rectangle is separable —
  `cov(x,y) = fx(x)*fy(y)` — so a fractional rect is at most **nine solid
  rectangles**: three column bands by three row bands.

  Measured, per column, on the reference's bang and hanabi's:

  | | x14 | x15 | x16 | total |
  |---|---|---|---|---|
  | reference | 0.44 | 0.97 | 0.50 | 1.91 |
  | hanabi, hard 2.3px | 1.00 | 1.00 | 1.00 | 3.00 |
  | hanabi, `rect_aa` | 0.47 | 1.00 | 0.47 | 1.94 |

  Every row of the stroke and every row of the tittle now agrees with the
  reference's to within 0.05 coverage.

**The workaround, and its cost.** `hanabi::glyph::rect_aa(x0,y0,x1,y1,fg,bg)`
in `src/ui/icons.h`, used by the row bang and by the search row's filter rules.
Three costs, and the third is the one that bites:

1. **Axis-aligned only.** The separability is what makes it nine rectangles. An
   arc, a diagonal or a disc needs per-pixel coverage, which is a rectangle per
   run per row — hundreds of draw calls for one 9px glyph. The arc, the cross
   and the chevron in the same vocabulary therefore stay hard-edged, so the
   sidebar now draws two marks with soft edges beside three without.
2. **It bakes the background in.** The composited colour is only right over the
   colour it was computed against, so every caller has to KNOW what is behind
   it. `draw_mark` had to grow a `bg` parameter and the session row had to hoist
   its own hover-fill decision above the glyph slot to pass it — a row that
   forgets gets a visible halo under the pointer and nowhere else, which is a
   state no reference captures and no screenshot test shoots.
3. **It cannot survive anything non-flat.** A gradient, an image, another
   widget's fill, or a translucent surface behind the glyph all break it
   silently. It works in the sidebar because the sidebar is one flat colour.


**Hanabi reference.** `src/ui/icons.h` (`inline void rect_aa`) — implements the flat-background coverage precompositing helper. `src/ecs/sidebar_system.h::hanabi::glyph::rect_aa` — uses rect_aa for the bang and dot parts of sidebar marks, with caller-provided row background. Measurement/gate: `src/ecs/sidebar_system.h` (`0.44 / 0.97 / 0.50`) — the source comment records the measured reference coverage that rect_aa approximates.


**Minimal upstream fix.** #92's — take the sample count from `Config`. With
MSAA on, none of the above is needed and the arc gets soft edges too, which
`rect_aa` can never give it. Failing that, an `afterhours::draw_rectangle_aa`
that does this arithmetic in the library would at least put the background
parameter in one place instead of at every call site.

CLASS: WORKAROUND

### #107 — A selected row's fill IS the row's own background box, so its inset, height and corner radius cannot be set independently of the row's padding and pitch

**What was wanted.** The smart-view sidebar's selected fill, matching
`ref/02_thread.png`: full-bleed x0..278, **27.7px tall inside a 32px pitch**,
corner radius **~8**, with the row's label, icon and count badge staying
exactly where they already are — which is exactly right, measured.

**What happens.** hanabi's fill is the row div's `with_custom_background`, so
the fill's rectangle and the row's content box are the same box. Its height is
`kSbViewRowH - kSbViewFillInset`, its position is the row's `Margin`, its
radius is `with_roundness` — and its content's position is that same box's
`Padding`. Change any of the first three and the label moves with it.

Measured, the fill is 1.3px too tall and its radius is 3.5 where the
reference's is 8. Every attempt to fix either one, scored against the
reference (views region, 4.41% before each):

| change | intent | views |
|---|---|---|
| radius 5 → 8 | match the measured corner | 4.44% |
| radius 5 → 11 | " | 4.48% |
| margin top 1 → 2, padding top 6 → 5 | drop the fill 1px, hold the content | **5.36%** |
| inset 3 → 4, top 1 → 2, pad 6 → 5 | shrink and drop it | 4.81% |
| inset 4.3, top 1.7, pad 5.3 | the measured 27.7px height exactly | 5.13% |

The radius alone gets worse because the fill is also a pixel high, and the
corner is where the two errors meet: a bigger radius on a misplaced corner
disagrees over more pixels, not fewer (this is the workstream's trap #1 — a
correct change costs points while its other half is missing). And the other
half cannot be applied: the fractional margins that would give the fill its
real height are also the rows' pitch, so **six rows drift** and the region
loses half a point.

Puffin has the separation as a first-class thing.
`SessionRowView`/`SmartViewSidebar` end with
`.hoverHighlight(inset: 4, vertical: 0, cornerRadius: 5, ...)` — a background
modifier with its OWN inset, independent of the `EdgeInsets` above it. Two
rectangles, one for the fill and one for the content, and the row states both.

**Why the obvious escapes do not work.**

- **A child div behind the content** — afterhours has no z-order within a row
  and no way to make a sibling render under its neighbours; an absolutely
  positioned one is placed from the leading edge only (gap #93) and cannot be
  sized against what it overlays (gap #97).
- **Fractional margins** — they work, and they move the pitch. See the table.
- **Padding the row and letting the fill shrink** — padding insets the CONTENT,
  not the box; the fill is the box.

**The workaround, and its cost.** None taken. `with_on_draw_bg` exists and
would let the fill be painted at an arbitrary rect under the content, which is
the real answer — but it means re-implementing the selected and hover fills by
hand for six rows plus the folded rail, and the whole prize is 0.37 points on
one region. Left measured and unspent, which is the cost: the fill is visibly
squarer than the reference's at every corner and nothing in the app can say so.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's 'none taken' workaround claim is stale: current source implemented the measured fill-height/radius correction and tests it.

**Hanabi reference.** Current code: `src/ecs/sidebar_system.h` (`kSbViewRowH - kSbViewFillInset`) — current smart-view rows reduce fill height and give the missing pixels back as margin. `src/ecs/sidebar_system.h` (`kSbViewFillRadius /`) — current smart-view rows set measured rounded fill corners instead of leaving the squarer old fill. Tests: `tests/ui/selected_view_fill.e2e` (`assert_ui smart_item h=29`) — pins the corrected selected-row fill height. Measurement/gate: `tests/ui/selected_view_fill.e2e` (`corner radius of about 5`) — records the reference measurement used for the corrected source constants.


**Minimal upstream fix.** A `with_background_inset(Padding)` on
`ComponentConfig`, applied to the background rect only, before roundness is
resolved. It is Puffin's `inset:` parameter, it changes nothing for anyone who
does not set it, and it makes the corner radius mean a radius on the shape the
designer drew rather than on the box the layout happened to produce.

CLASS: MISSING

### #108 — An icon's stroke weight is baked into the atlas at generation time, so a sprite cannot be drawn lighter without changing it for every consumer

**What was wanted.** The six smart-view icons matching the reference's, which
draws them as SF Symbols at `Font.message`. Same drawings — Puffin names them
in `SmartViewSidebar.systemImage` and hanabi now blits the Lucide icons of the
same shape — and the bounding boxes agree to a pixel.

**What happens.** Every one of them carries more ink than the reference's,
measured over the icon's own box against `ref/02_thread.png`:

| icon | reference | hanabi | ratio |
|---|---|---|---|
| house | 49.0 | 63.2 | 1.29 |
| gearshape | 31.3 | 41.3 | 1.32 |
| hand.raised | 23.9 | 37.5 | **1.57** |
| checkmark.circle | 25.6 | 31.9 | 1.24 |
| pin | 22.5 | 28.2 | 1.25 |
| archivebox | 28.8 | 40.9 | **1.42** |

The cause is not the size and not the colour. Lucide draws on a 24-unit grid at
`stroke-width="2"`; SF Symbols at this weight is nearer 1.5. The extra is
inside a box that already matches, so it is stroke, and a sprite's stroke is
pixels — it was decided when `scripts/gen_icons.py` rasterized the SVG.

**Why the obvious escapes do not work.**

- **Blit it smaller** — that thins the stroke and the drawing together. Swept
  against the reference: 14px scores 4.48%, 15px 4.45%, **16px 4.41%**, 17px
  4.54%. The shipping size is already the optimum; the bbox is right and only
  the stroke is heavy.
- **Blit it dimmer** — this is real and worth doing for the part of the error
  that IS colour (see the friction log: a blit reaches its colour and 9pt text
  never does, so the icon needed its own token). But the score keeps improving
  monotonically as the ink darkens past the measured value — 4.42% at the true
  `Chrome.mutedText`, 4.31% at 125 — which is the metric paying for less ink,
  not for a truer colour. Stopping at the source's own constant is the only
  defensible place to stop, and it leaves the surplus.
- **Regenerate the atlas at `stroke-width="1.5"`** — the atlas is ONE sheet
  shared by the tab strip, the composer, the sidebar footer and the main pane,
  and two of those regions are already AT FLOOR. Thinning them all to fix six
  sidebar rows is a change no one region can justify and no one region can
  veto.
- **Ship thin variants alongside** — six more cells, a bigger PNG, two names
  for one drawing, and a rule about which to use that nobody will remember.

**The workaround, and its cost.** None. The icons keep Lucide's weight and the
sidebar's views region keeps roughly a third of a point it cannot spend. The
cost worth recording is that the residual is now ISOLATED: the size is swept,
the colour is measured off the source's own token, the bboxes match, so
anything still there is the atlas and nobody needs to re-derive that.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Not afterhours': it is `gen_icons.py`, and the fix is
a `stroke-width` override per icon in the `ICONS` table so a caller can ask for
a lighter cut of one sprite without re-cutting the sheet. What afterhours could
offer instead is a tinted blit that takes a coverage gamma — one uniform — so a
sprite's weight becomes a draw-time parameter rather than an asset decision.

CLASS: MISSING

---

### #109 — #85's escape list rules out `with_margin`, and `with_margin` is the fix; the ignored padding it warns about was still live 1,100 lines down the same file and cost a whole region

**This is not a new wall.** #85 (*"Padding on a label-only element is silently
ignored"*) and #91 describe the mechanism exactly and correctly. This entry is
the second instance, what it cost, and one correction to #85 that is the reason
the second instance was fixable in one line and was not fixed for six rounds.

**What was wanted.** A session-row title indented from its status-glyph slot.
`sidebar_system.h:2655`:

```cpp
.with_size(ComponentSize{pixels(rowTitleW), pixels(20)})
.with_padding(Padding{.left = pixels(kRowTitlePad)})   // does nothing
```

with, six lines up, the arithmetic out loud: *"Puffin puts the glyph's centre at
x=15.5 and the title's first ink at x=28, so the slot is 13 wide from kSbInset
and the title carries a 6px left pad."* 9 + 13 + 6 = 28 and 28 is right.

**What happens.** Same as #85, and confirmed the same way: built at
`kRowTitlePad` 6, 7 and **20**, the three captures are byte-identical except
where the wider budget re-ellipsized three rows. The title has always drawn at
`kRowLeftInset + kGlyphW + 5 = 27`. `rendering.h:2161` is the whole
explanation — the rect handed to `position_text_ex` is the element's own drawn
rect, inset only by a nine-slice border, and the layout system's `Padding` is
never consulted on that path:

```cpp
RectangleType text_rect = draw_rect;
if (entity.has<HasNineSliceBorder>()) { text_rect.x += ns.left; ... }
position_text_ex(fm, hasLabel.label.c_str(), text_rect, hasLabel.alignment,
                 Vector2Type{5.f, 5.f}, ...);
```

**What the recurrence cost, which is the reason to file it again.** One pixel,
uniform across all nineteen visible rows. That pixel was **100% of the session
list's remaining parity headroom**: list 13.56% → 11.82% structural against a
floor of 8.41–11.83, i.e. from +1.73 to AT FLOOR, on one line. Six independent
rounds of visual-parity work read that comment, checked the arithmetic, measured
hanabi's ink at 77–86% of the reference's, and concluded "the rest is the
rasterizer" — because a rigid 1px shift on every string is indistinguishable
from a uniform ink deficit in every aggregate anybody had. #85's closing line
is *"silence is what made this cost a day."* Filed, it cost six rounds.

**The correction, and it is the useful half.** #85's escape list opens with:

> **`with_margin`** is the same story one level out: it spaces the element from
> its siblings, not the text from the element.

That sentence is literally true and practically backwards. Margin does not move
text *within* an element — but it moves the ELEMENT, and the element's rect is
precisely what the text is drawn from, so the text goes with it. It is the
cheapest correct fix for the thing #85 is about, it costs no entity, and #85
rules it out in its first bullet. `render_snippet`, forty lines above the
defect in the same file, already says so in as many words — *"The indent is a
margin, which moves the element and its text together"* — so one function in
this codebase knew and the gap doc said not to.

The consequence is visible in the workarounds: #85 shipped `sb_spacer_x`, an
empty div per row whose only job is to be 10px wide, because margin had been
ruled out.

**Why the other obvious escapes still do not work.**

- **Widen the preceding sibling** (`kGlyphW` 13 → 14). It moves the title and
  it also moves the status glyph, which centres in that slot. Measured: the
  glyph column is AT FLOOR at 1.95% and half a pixel of drift takes it to
  10.13%. A layout constant two elements read is not a place to put one
  element's offset.
- **`TextAlignment::Center` and size the box.** Centring divides the slack at
  both ends, so a title's left edge becomes a function of its own length —
  nineteen different indents.
- **A padded wrapper** (#85's "works, and costs an entity per label"). Nineteen
  more entities in the sidebar, and the wrapper needs the row's width
  arithmetic, so the constant that was wrong once is now wrong in two places.

**The workaround, and its cost.** A left `Margin` of 1 with the element's width
reduced by the same 1, so the row's column arithmetic and its ellipsis budget
are untouched:

```cpp
static constexpr float kRowTitleLead = 1.0f;
    .with_size(ComponentSize{pixels(rowTitleW - kRowTitleLead), pixels(20)})
    .with_margin(Margin{.left = pixels(kRowTitleLead)})
```

Two costs. The indent is now split across two constants that only make sense
read together — `kRowTitlePad` is a WIDTH budget for `fit_to_width`, and
`kRowTitleLead` is the position — and neither name says which without its
comment. And the defect is still live elsewhere: **twenty label-bearing
elements in hanabi set horizontal padding**, of which nine are cases where the
element's own label is the thing meant to move — `tab_label`, `md_table_cell`,
`msg_time`, `dc_tag`, `welcome_chip`, `sb_show_more`, `sb_no_results`,
`shortcuts_keys`, `xsearch_note`. None sits in a region above its floor today
(the tab bar, where `tab_label` lives, is AT FLOOR), so none is touched here.
They are listed so the tenth round starts from the right hypothesis.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`static constexpr float kRowTitleLead = 1.0f`) — current source encodes the one-pixel title lead that fixes the row-title position. `src/ecs/sidebar_system.h` (`with_margin(Margin{.left = pixels(kRowTitleLead)})`) — applies the margin-based correction that #85's escape list had ruled out. Tests: `tests/ui/row_title_starts_where_puffin_starts.e2e` (`assert_ui row_title x=23`) — pins the source margin correction and row glyph column separation. Measurement/gate: `tests/ui/row_title_starts_where_puffin_starts.e2e` (`list 13.56% -> 11.82% structural`) — records the measured impact of the one-pixel row-title correction.


**Minimal upstream fix.** #85 offers two and prefers the warning; after a
second instance the warning is clearly the right one, because the honouring
fix would silently move nine live labels in this app alone. A label element
carrying a non-zero `Padding` and no `HasNineSliceBorder` is asking for
something the renderer will not do — one `log_warn` at build time, once per
debug name. That single line would have turned six rounds of a parity
workstream into a startup message.

CLASS: FOOTGUN

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): the audit above counted the
CURE as the disease, and the real number is ten.** "Twenty label-bearing
elements set horizontal padding, of which nine are this defect" was produced by
a regex over the whole config chain --

    H_PAD = re.compile(r"\.(?:left|right)\s*=\s*pixels\(\s*([0-9.]+)f?\s*\)")

-- which is equally the shape of a `Margin`, and `with_margin` is the fix this
entry exists to prescribe. `50237d3` tightened it to look only inside a
`.with_padding(Padding{...})` block and to accept a constant rather than a
numeral (this entry's own defect, `pixels(kRowTitlePad)`, the old pattern could
not see at all). `scripts/label_padding_baseline.txt` is **ten** lines now:
`effort_row_ fold_row_ md_table_cell model_row_ msg_time sb_no_results
sb_show_more shortcuts_keys slash_item_ tab_menu_item`.

Four of the nine named above are not the defect, each for a different reason,
and that is the useful half: `tab_label` zeroes all four sides (the deliberate
gap #76 no-op), `dc_tag` is `TextAlignment::Center` so horizontal padding cannot
matter, and `welcome_chip` and `xsearch_note` were already using `with_margin`.
The five that stand -- `md_table_cell`, `msg_time`, `sb_show_more`,
`sb_no_results`, `shortcuts_keys` -- are all in the baseline.

---

### #110 — Nothing rounds a widget's ORIGIN, so a shape on a half pixel silently loses a row (or a column, depending on the caller's arithmetic)

**What was wanted.** A 6px round activity light in the sidebar footer, centred
in the band beside the session count.

**What happens.** It came out **six wide and five tall**. The band's content
runs y922..948, so the centring arithmetic is `922 + (27 - 6) / 2 = 932.5`, and
a 6px box at y=932.5 rasterizes to rows 933..937 — 4/6/6/6/4 pixels. At an
integer y the same box draws six rows, 4/6/6/6/6/4, and is round. Half a pixel
of position costs a sixth of the shape.

Nothing in the stack rounds it. `AutoLayout::compute_relative_positions` has
grid snapping, hanabi turns it off (`preload.cpp`), and it only ever snapped
`computed[Axis]` — the SIZE — never `computed_rel`, so even switched on it
would not have touched this. The float origin reaches
`draw_rectangle_rounded`, which emits float triangles to sgl with MSAA
hardcoded off (#92), and the rasterizer resolves a shape whose extremes land
exactly on pixel boundaries however the vertex arithmetic happens to fall — the
asymmetry between the lost top and bottom row is not a rule anyone can predict,
it is float precision at the fan's extreme vertex.

**Two things make this worse than an off-by-one.**

1. **It is one axis at a time, so a circle becomes an ellipse rather than
   moving.** A shape that MOVED half a pixel would be invisible. This one
   changes proportion, and it changes it on whichever axis the caller's
   arithmetic happened to make fractional. Here x was whole and y was not.
2. **The fractional part can be data-dependent.** This light's x is
   `text_px("<N> sessions") ` subtracted from a right edge, so it changes with
   the catalog: the same build draws a round light on one machine's session
   count and a 5-wide one on another's. That is a bug that does not reproduce.

**Why the obvious escapes do not work.**

- **`with_skip_grid_snap` / `set_grid_snapping(true)`** — the wrong axis of the
  problem entirely: both are about the computed SIZE of non-pixel dimensions,
  and this is a pixel-dimensioned widget at a fractional POSITION.
- **Make the band an even height so the halves come out whole** — the band's
  height is the footer's 28px minus its 1px rule, which is Puffin's own
  `FooterMetrics.height`. Changing it to make one child's centring divide
  evenly is letting a 6px dot set the height of a piece of chrome.
- **Draw it in `on_draw_fg` instead** — `CustomDrawFn` is handed the widget's
  own `RectangleType`, so it inherits exactly the same fractional origin, and
  #102 says it is handed it already scaled.
- **`rect_aa` (the escape #106 documents)** — that computes a partly covered
  pixel's opaque colour over a known background, and it is axis-aligned only.
  It would soften the light's edge, which is not the complaint: the complaint
  is that one of its two axes is a pixel short.
- **Round the size instead of the position** — a 6px box at 932.5 and a 7px box
  at 932.5 are both wrong; it is the origin that is off the grid.

**The workaround, and its cost.** `std::floor` on both coordinates, at the
call site, in `src/ecs/sidebar_footer_geometry.h`. Two lines. The costs are
that (a) every caller who draws a small shape has to know this, and there is
nothing in the API to suggest it; (b) `floor` versus `round` is now a design
decision the caller has to justify — this footer floors, because the band's
centre is a half pixel below the count's ink centre and flooring is what lands
the light on the text; and (c) **no harness in the repo can regression-test
it.** `assert_ui` reads x/y/w/h and rounds them (#86), so it reports 933 for
an unsnapped 932.5 and 932 for an unsnapped 932.4 — it cannot tell a snapped
origin from an unsnapped one. The property had to be pulled into a header with
no graphics in it and tested as arithmetic
(`tests/unit/test_footer_geometry.cpp`), which is a file that exists only
because of this gap.


**Hanabi reference.** `src/ecs/sidebar_footer_geometry.h` (`inline float dot_y`) — floors the activity-light y origin to whole pixels. `src/ecs/sidebar_footer_geometry.h` (`inline float dot_x`) — floors the activity-light x origin because it is derived from measured session-count text. Tests: `tests/unit/test_footer_geometry.cpp` (`the activity light's origin is on a whole pixel`) — unit-tests the snapping property that e2e geometry cannot observe. Measurement/gate: `tests/unit/test_footer_geometry.cpp` (`a 6x6 light at y=932.5 rendered rows 933..937`) — records the measured five-row failure before the workaround.


**Minimal upstream fix.** Snap absolute translates to whole DEVICE pixels in
`compute_relative_positions` — device, not logical, so it stays correct at
`ui_scale 2` where #100 already shows the difference matters — with
`with_skip_grid_snap` (or a sibling opt-out) as the escape for a caller who is
deliberately animating sub-pixel. Failing that, a `snap_px()` in the layout
header so the arithmetic at least has a name and shows up in a search.

CLASS: SURPRISING

---

### #111 — A widget's hover highlight IS its hit rectangle, so a big target with a small chip needs the whole hover fill re-implemented

**What was wanted.** Puffin's sidebar footer buttons. Each is an
`Image(systemName:).frame(width: 24, height: 28).contentShape(Rectangle())` —
a hit target the full height of the footer band — carrying
`.hoverHighlight(inset: 2, vertical: 2, cornerRadius: 4)`, which draws its
chip 2px inside that on both axes. So the target is 24x28 and the chip is
20x24, and Puffin's source says in as many words why the two differ:
*"Icon-sized, not a row: `hoverHighlight`'s own `maxWidth: .infinity` is built
for a list row, and applied here it would stretch this button to fill the rest
of the footer."*

**What happens.** `with_custom_hover_bg` paints the widget's own rect. There is
one rectangle and it is both things, so a caller picks: a hit target the size
of the chip, or a chip the size of the target. hanabi's footer buttons are
22x22 in a 27px band — the chip is right and the target is 5px short of the
band, which is 18% of the button's height and all of it at the edges, where a
pointer travelling down the sidebar arrives.

**Why the obvious escapes do not work.**

- **A bigger transparent parent with the button inside it** — the parent is
  what the pointer is over, so the hover state now belongs to the parent and
  `with_custom_hover_bg` on the CHILD never fires. Moving the fill to the
  parent puts it back at the target's size, which is where it started.
- **`with_ignore_pointer_events` on a chip drawn over a bigger button** — this
  works for the pointer and not for the paint: the chip is a second widget, so
  it needs the button's hover state at BUILD time to know whether to exist, and
  see below.
- **`with_on_draw_bg`** — this is the escape that works, and it is a bigger
  bill than it looks. `RenderPrimitive::CustomDrawFn` is
  `std::function<void(RectangleType)>`: it gets the rect and nothing else, no
  hover state, no theme, no interaction phase. So the caller reads
  `ctx.was_hot(id)` itself while building, closes over the answer, and hand-
  draws the fill — which means re-implementing the hover fill (colour, corner
  radius, and whatever afterhours' own hover animation does) for every button
  that wants an inset chip, off a hover state that is a frame stale by
  construction. `feat/vis-sb3` reached the same conclusion from the other
  direction for the selected-view fill (#107).

**The workaround, and its cost.** None taken. hanabi's footer buttons keep the
22x22 that makes the CHIP right and leaves the target 5px short at the top and
bottom of the band. The trade was made deliberately: the chip is in every
frame anyone looks at and the missing 5px is in none, and no reference capture
in this workstream shows a hover state at all, so the half that was chosen is
the half that can be verified.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** An inset on the highlight —
`with_hover_bg_inset(float h, float v)` — applied to the rect
`RenderPrimitive` builds for the hover fill and to nothing else. It changes no
hit testing, no layout, and no existing caller, and it is the one field
SwiftUI's own `hoverHighlight` needed to be reusable between a list row and a
20pt glyph.

CLASS: MISSING

---

### #112 — There is no tooltip and no accessible name, so an icon-only button is unlabelled in every sense

**What was wanted.** Puffin's four footer elements, each of which carries a
`.help()`: the version label ("About Puffin"), `info.circle` (the same),
`ant` (`BugReport.buttonHint`) and `gearshape` ("Settings"). Two of them also
carry an explicit `.accessibilityLabel`.

**What happens.** There is no tooltip anywhere in afterhours — no
`with_tooltip`, no `help`, no title, no hover-delay timer, nothing under any
spelling; the string `tooltip` does not appear in `vendor/afterhours/src`. Nor
is there an accessible name: hanabi's footer buttons are built
`.with_label(" ")` — a literal space, because the widget wants a label and the
drawing is a sprite in `on_draw_fg` — so the only text those three controls
have anywhere in the tree is a space. A reader who does not recognise a
magnifier has no way to find out what it does, and nothing else in the frame
says.

This is not a parity nicety. Three unlabelled glyphs are the entire right half
of this band, and hanabi's two are `plus` and `search` against Puffin's
`info.circle` and `ant` — the pair that is a deliberate product divergence
(REFERENCE.md). The divergence is defensible precisely because hanabi's two do
something a reader wants; a control nobody can name does not.

**Why the obvious escapes do not work.**

- **Put the word in the button** — a 24px slot in a 280px column holding three
  of them. That is the layout Puffin's own comment records this footer moving
  AWAY from.
- **Build one out of a hover-triggered absolute div** — possible, and it is a
  hover-delay timer, a z-order above every sibling (#93's trailing-edge
  problem, one layer up), a measured text box (#103), a screen-edge flip, and
  a rule for dismissal, per surface, in app code. That is a component, not a
  workaround, and every consumer of afterhours would write it again.
- **The OS's own tooltip** — there is no per-widget rect the platform knows
  about; afterhours draws into one surface and the window is one control.

**The workaround, and its cost.** None. hanabi's footer buttons are
unlabelled, and so is every other icon-only control in the app — the tab
strip's `+` and pin, the search row's filter, the collapse toggle. The cost is
recorded here rather than paid: it is the same missing feature every time, and
it wants building once, upstream, not five times in hanabi.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`Sprite-icon button: a consistent 28x28 chrome button`) — sidebar icon buttons still use blank labels and drawn sprites. `src/ecs/main_pane_system.h` (`afterhours has no tooltip and no accessible name`) — composer send/steer source documents the missing accessible-name primitive. Tests: `tests/ui/steer_is_an_icon_with_a_name.e2e` (`expect_text "Enter to steer"`) — guards the product-level workaround that an icon-only steer button has a visible neighboring name.


**Minimal upstream fix.** `with_tooltip(std::string)` on `ComponentConfig`,
rendered by the existing overlay layer on a hover dwell, plus an
`accessible_name` that defaults to the label and can be set when the label is
a placeholder — which is exactly the pair SwiftUI's `.help()` sets in one call.

CLASS: MISSING

### #113 — Every scripted failure but one is told which element it was about; the timeout, which is the one that means "it is not there", is not

**What was wanted.** To watch a new scripted test go red against a build
without its fix, read the failure, and know from the failure alone what was
missing.

**What happens.** `assert_ui` calls `cmd.retry()` when no element carries the
name, so a missing element is not a failure — it is a retry, thirty times, and
then a timeout. The timeout's message is built in
`command_handlers.h:812-824`, which special-cases exactly one command:

```cpp
if (cmd.name == "expect_text" && !cmd.args.empty()) {
    error_msg = std::format("Text not found: '{}'. Visible: {:.200}", ...);
} else {
    error_msg = std::format("Command '{}' timed out after {} frames",
                            cmd.name, PendingE2ECommand::MAX_FRAMES);
}
```

So `expect_text` says what it was looking for and what was on screen, and
`assert_ui`, `assert_ui_text`, `click_ui`, `focus_ui`, `expect_focused` and
`expect_input_text` all say nothing at all. Four consecutive assertions about
one element produce four identical lines:

```
[WARN] [TIMEOUT] assert_ui (line 47): Command 'assert_ui' timed out after 30 frames
[WARN] [TIMEOUT] assert_ui (line 48): Command 'assert_ui' timed out after 30 frames
[WARN] [TIMEOUT] assert_ui (line 49): Command 'assert_ui' timed out after 30 frames
[WARN] [TIMEOUT] assert_ui (line 50): Command 'assert_ui' timed out after 30 frames
```

`cmd.args[0]` — the name — is in scope on the line above.

This is #104's other half and it bites in the same place. #104 is that you
cannot ASSERT an element is absent; this is that when an element IS absent, the
harness declines to say which one. Between them, "did this element render?" is
the question the harness is worst at, and it is the question every piece of
chrome asks.

**Why the obvious escapes do not work.**

- **Read the line number** — works, and it is what I did. It requires the
  script open beside the log, it does not survive the script being edited, and
  it is no use at all in the suite runner's summary, which prints
  `sed -n '/E2E ERROR\|TIMEOUT\|FAIL/p' | head -8` and so prints eight lines
  that differ only in a number.
- **Name the assertion in a comment** — comments are not in the log.
- **Split one element's assertions across separate tests** so the test NAME
  carries the element — one file per property, and the suite is 85 files.

**The workaround, and its cost.** None available in app code; the message is
built inside the plugin. The cost is that a red scripted test in this repo
tells you a line number and not a fact, and the reflex it trains — open the
script, count lines — is the reflex that made #104's missing assertion hard to
notice in the first place.


**RESOLVED UPSTREAM 2026-08-29.** Afterhours `2caf525` includes the timed-out command subject in every retry-timeout diagnostic. Hanabi now consumes that behavior directly from the `fc4d625` pin; the former proof patch and probe were removed.

**Hanabi reference.** `tests/ui/composer_model_picker.e2e` and the rest of the scripted suite now receive subject-bearing timeout diagnostics from the vendored runner without a local patch.


**Minimal upstream fix.** When a command times out and has args, append them. Three lines, and it makes every
name-taking command self-describing:

```cpp
error_msg = std::format("Command '{}'{} timed out after {} frames", cmd.name,
                        cmd.args.empty() ? "" : std::format(" ('{}')", cmd.args[0]),
                        PendingE2ECommand::MAX_FRAMES);
```

CLASS: FOOTGUN

### #114 — A sprite's rendered INK extent is not derivable from its atlas rect and its `draw_px`, so every icon is sized by build-measure-repeat

**What was wanted.** To draw a close mark whose ink lands on the reference's
own 8x8, in one build.

**What happens.** `icons::draw_fg(name, fallback, colour, draw_px)` blits the
atlas sub-rect into a `draw_px` x `draw_px` destination centred in the widget.
`draw_px` is therefore the size of the sprite's BOX, and what a caller has to
match is the size of its INK — and nothing relates the two. `src_rect(name)`
returns the atlas rect, which is the box again. The inked fraction of that box
is a property of the icon set's own normalisation: Lucide normalises to a
24-unit box and SF Symbols normalises optically, which is already written up in
REFERENCE.md ("hanabi's one nominal 13px drew `plus` and `search` at 12x12 and
`gear` at 10x12") as the reason the footer's gear needed 14 to look like its
neighbours' 13.

So sizing one mark to a measured target is: build, shoot, measure the ink,
scale, build again. The close mark took two builds — 11px gave x482..491 y43..53
against the reference's x483..490 y46..53, three pixels wide and three tall,
and 8px with a +1 y bias gave the reference's extent row for row.

**Why the obvious escapes do not work.**

- **Scale by the ratio of the atlas rect to the target** — that is the ratio of
  two BOXES, and the discrepancy is between a box and its ink. It is the
  arithmetic that produced 11 in the first place.
- **Read the alpha out of the atlas at startup** — the atlas is a GPU texture
  loaded lazily on first draw (`AtlasTexture::ensure`); there is no CPU-side
  image to scan, and `scripts/gen_icons.py`, which has the PNG in hand, emits
  only `{name, x, y, w, h}`.
- **Trim the sprites at generation time** so the box IS the ink — it changes
  every existing call site's effective size at once, and #108 already records
  that the atlas is a shared artefact whose properties cannot be varied per
  consumer.

**The workaround, and its cost.** Two builds and a screenshot per icon, and a
comment at the constant recording what the first attempt measured so the next
person does not repeat it. Multiplied by every glyph in the app: this is the
same afternoon the footer's gear cost (13 -> 14, `37 diff pixels -> 22`) and
the same one the filter rules cost, each rediscovered from scratch.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Have the atlas generator emit the inked bounding box
beside the sprite rect — it already opens every PNG — and expose
`ink_extent(name, draw_px) -> Vector2` so a caller can size to ink instead of
to box. Two fields in `icons_atlas.h` and one function.

**Whose gap this is.** The atlas and its generator are hanabi's, so the fix
above is hanabi's to make; it is filed here rather than in a TODO for the same
reason #108 is, which is that the shape of the problem is
`draw_texture_pro(src_rect, dest_rect)` — afterhours' blit takes two boxes and
has no concept of what is inked inside either — and any consumer that builds a
sprite sheet against it arrives at the same afternoon. The library half is that
there is nothing to ask.

CLASS: TEDIOUS


### #135 — `wrap_text` measures one growing PREFIX per word and materialises every line, so "how many lines is this" costs O(words) measures and O(words) string builds

**What was wanted.** The line count of a message body at a given width, to
place the messages after it. Nothing more: not the lines, not their text, not
their widths. A number.

**What happens.** The only way to ask is
`ui::wrap_text(text, max_width, font, size)`, which returns
`std::vector<std::string>` — every wrapped line, materialised — and hanabi
throws all of it away:

```cpp
static int count_lines(const std::string& text, float widthPx, float fontPx) {
    return static_cast<int>(wrapped_lines(text, widthPx, fontPx).size());
}
```

Underneath, `detail::wrap_runs_to_width` is a greedy word-wrap that measures
the CANDIDATE LINE at every word boundary, and the candidate is the whole
accumulated prefix:

```cpp
const std::string candidate = current_text + pending_ws.text + chunk.text;
if (!current_text.empty() &&
    measure_candidate(current, pending_ws.parts, chunk.parts) > max_width) {
```

and `measure_candidate` concatenates the parts into a fresh `std::string seg`
before handing it to `measure`. So an N-word line costs N measure calls whose
arguments are N distinct strings of average length N/2 — quadratic in bytes —
plus N string constructions for the candidate and N more for the measured
segment, before the vector of results is built and dropped.

Measured, on hanabi's 120-message perf fixture at 1180x949: 22 KB of text
wrapped per frame producing 3,456 `TextMeasureCache` lookups per frame, to
answer a question whose whole output is one integer per message.

The cache saves the fontstash call but not the rest: at a 100% hit rate those
3,456 lookups are still 3,456 FNV hashes over the candidate strings plus 3,456
list splices, and the candidate strings still had to be built to be hashed.

**Why the obvious escapes do not work.**

- **Cache the line count in the app.** hanabi does, and it is the right answer
  for a body whose text has not changed — but it is a memo over the library's
  answer, so the first ask still pays, every distinct width pays again, and
  anything genuinely dynamic (a streaming message, a live filter) pays every
  frame. It moves the cost, it does not remove it.
- **Use `measure_text_wrapped`.** Same primitive underneath
  (`detail::measure_wrapped` calls `wrap_text_to_width` and then measures each
  resulting line AGAIN), so it is strictly more work for the same answer.
- **Estimate from `length / chars_per_line`.** hanabi did exactly this and the
  comment recording its removal is still in the file: the wrapper honours hard
  newlines and the estimate did not, so a three-line message measured as one
  and the bubble clipped. An estimate that disagrees with the wrapper is a
  rendering bug, not an optimisation.
- **Reserve the vector / move the strings out.** The allocation is not the
  return value, it is the N candidate strings built inside the loop; the
  returned vector is the cheap part.

**The workaround, and its cost.** A per-message memo of (display body, line
count, measured height) keyed by (message id, wrap width) —
`src/ecs/transcript_render_cache.h`, 120 lines including the reason it holds
TWO widths per key (see #136). It works: it took the transcript's measure pass
from 2.01 ms/frame to 0.08 ms at 120 messages and from 5.79 to 0.27 at 480.
The cost is that a cache is now load-bearing for correctness of the frame
budget, with the failure mode caches have — it silently had a 34% hit rate and
the only symptom was that long threads were slow.


**Hanabi reference.** `src/util/wrap_count.h::wrapped_line_count_fast` — implements line counting without materialising every wrapped line. `src/ecs/main_pane_system.h` (`This used to be wrapped_lines(...).size()`) — transcript line count now calls the counter instead of building a vector just to count it. Tests: `tests/unit/test_wrap_count.cpp` (`wrapped line count vs afterhours' own wrapper`) — differential test checks the counter against the vendored wrapper. Measurement/gate: `docs/perf/TEXT.md` (`Fix — src/util/wrap_count.h`) — perf notes record the shipped counter and its safety test.


**Minimal upstream fix.** A counting overload that does not materialise:
`detail::wrapped_line_count(text, max_width, measure) -> int`, sharing the same
break logic so it cannot disagree with `wrap_text`. Better still, have
`measure_candidate` measure the DELTA (the pending whitespace plus the new
chunk) and accumulate the width, instead of re-measuring the whole prefix —
the comment above it already explains that pieces are summed per same-weight
stretch rather than per word to avoid losing inter-character spacing at joins,
which is the same trade at a different granularity, and it would turn the
quadratic byte count linear for the plain single-run case that `wrap_text`
always is.

CLASS: PERFORMANCE

### #136 — Nothing sizes a box to its own text, so a hug costs a wrap plus a measure per line — and forces every memo of that measurement to hold two widths, not one

**What was wanted.** A user's chat bubble that is as wide as its longest
wrapped line and no wider, capped at a maximum. The universal chat layout.

**What happens.** Already filed as #79 / #87 / #103 as a *tedium* gap: there is
no `size_to_content`, so the app measures. This entry is about what that costs
per frame, which turned out to be a different and larger problem than the
typing.

Hugging is inherently a TWO-PASS measurement, and both passes are the app's:

```cpp
const float maxTextW = maxW - 2.0f * kBubbleCfgPadX;
const auto& mr = measured(m, maxTextW, ...);           // pass A, at 630 px
float widest = 0.0f;
for (const auto& ln : wrapped_lines(mr.body, maxTextW))
    widest = std::max(widest, theme::text_px(ln, theme::type::BODY));
box.textW = std::min(maxTextW, widest + 2.0f * kLabelInsetX);  // 458 px
```

and then the height pass asks for the same message again at `box.textW`. So a
memo keyed by (message, width) — the obvious and correct key — is asked for one
key at two widths, alternating, forever. hanabi's held one entry per key and
therefore had a **negative** hit rate on every user message: put at 630, ask at
458, miss, recompute, put at 458; next frame ask at 630, miss, recompute, put
at 630. Measured on the 120-message fixture: 72.5 misses per frame against
37.4 hits, of which 99.8% were this ping-pong and 0.2% were genuinely cold.

The library shape causes this. If the box could size itself there is one width
and one entry; because the app must derive the width from a measurement of the
content, there are necessarily two, and any cache in front of it has to know
that.

**Why the obvious escapes do not work.**

- **Measure once at the hugged width.** Circular — the hugged width is the
  output of the measurement.
- **`children()` sizing.** It sizes a parent to its laid-out children, and the
  child here is a text label whose own width is the thing in question; the
  label fills the width it is given rather than reporting the width it wants.
- **Key the memo by width.** Bounded at a fixed pane width, unbounded across a
  resize DRAG: an entry per message per intermediate width, which is a worse
  version of the bug.
- **Round the two widths together.** They are 172 px apart. They are different
  questions.

**The workaround, and its cost.** Two width slots per key, newest-first,
insert evicting the older — enough for exactly the working set a hug creates,
bounded by construction. Plus a second memo for the hug result itself, so the
wrap and the per-line measures do not run for every user message in the thread
on every frame. Together: render-cache hit rate 34% -> 99.9%, wrap calls per
frame 242.9 -> 61.8, allocations per frame 30,015 -> 9,565 at 120 messages.

The cost is that both memos, and the reason the pair is two and not one, are
now permanent load-bearing complexity in a chat app for want of a layout mode.


**Hanabi reference.** `src/ecs/transcript_render_cache.h` (`TWO WIDTHS PER KEY`) — cache explicitly holds the max-width and hugged-width working set. `src/ecs/main_pane_system.h` (`if (const float* w = render_cache().hug(hugKey, maxTextW))`) — user-bubble hug width is memoized before recomputing wraps/measures. Tests: `tests/unit/test_text_cache.cpp::a_resize_drag_is_bounded` — generic text cache test covers bounded width-key behavior. Measurement/gate: `docs/perf/TRANSCRIPT.md` (`hug memo (new)`) — transcript perf notes record the hit rate and miss reduction for the hug memo.


**Minimal upstream fix.** `ComponentSize{fit_content(max), ...}` resolved
inside AutoLayout, which already has the text, the font and the width and
already wraps — it is measuring all of this anyway to lay the label out. One
sizing mode removes both memos and the entire class of bug.

CLASS: PERFORMANCE

### #137 — The measure that IS cached and the measure the app can call answer different questions, so routing app measurement through `TextMeasureCache` moves pixels

**What was wanted.** hanabi measures text constantly (#79 / #87 / #103 / #136).
afterhours ships `TextMeasureCache`, an LRU with a 4096 default, wired up as a
singleton by `ui::utilities`. The obvious move is to send hanabi's measuring
through it.

**What happens.** There are two measure functions and they do not agree.

`theme::text_px`, hanabi's app-facing measure, calls
`afterhours::measure_text_internal`, which is the backend's own single-line
measure and knows nothing about the cache:

```cpp
// backends/sokol/font_helper.h
inline float measure_text_internal(const char *text, const float size) {
  ...
  return fonsTextBounds(ctx, 0, 0, text, nullptr, nullptr) / dpi;   // ADVANCE
}
```

The cached path is `ui::measure_text_line`, which goes
`TextMeasureCache::measure` -> the measure function registered in
`ui::utilities` -> `measure_text(font, ...)`:

```cpp
// backends/sokol/font_helper.h
fonsTextBounds(ctx, 0, 0, text, nullptr, bounds);
float w = (bounds[2] - bounds[0]) / dpi;                            // INK BOX
```

One returns the pen ADVANCE, the other the INK BOUNDING BOX. Measured in
hanabi with both called on the same string, same font, same size, in the same
frame:

```
[probe] uncached=440.0000 cached=442.0000  delta=+2.0000  "Follow-up question #0: can you dig into th"
[probe] uncached=435.0000 cached=437.0000  delta=+2.0000  "Follow-up question #1: can you dig into th"
[probe] uncached=440.0000 cached=442.0000  delta=+2.0000  "Follow-up question #2: can you dig into th"
```

A consistent 2 px. So the cache is not a drop-in: switching hanabi's hug
measurement to it widens every user bubble by 2 px, which in a project with a
frozen pixel reference and a floor-scored parity harness is a regression, not
an optimisation. The result is that hanabi's own measuring goes AROUND the
cache — 70.6 uncached fontstash calls per frame — while the library's internal
layout goes through it, and the two are measuring the same strings.

**Why the obvious escapes do not work.**

- **Absorb the 2 px into the padding constant.** The delta is a property of the
  STRING (ink bearing on the first and last glyph), not a constant; it is 2 px
  for these and something else for a line ending in a comma.
- **Call `measure_text` directly and cache it in hanabi.** That is the ink box,
  so it moves the same 2 px. The advance is the number the current layout was
  tuned against.
- **Register `measure_text_internal` as the cache's measure function.** It is
  registered by `ui::utilities::register_...`, inside the library's own setup,
  and the library's layout pass reads the same cache — changing it changes what
  AutoLayout believes about every widget in the app.

**The workaround, and its cost.** Leave `theme::text_px` uncached and avoid
CALLING it: hanabi now memoizes the hug so the measure happens once per message
instead of once per message per frame (70.6/frame is what is left, all of it
outside the transcript). The cost is that the one shared cache the library
provides for exactly this is unusable by the app that needs it most, and every
app-side measurement needs its own memo instead.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The old “leave theme::text_px uncached” workaround is stale: current source memoizes the app’s advance semantics with TextKeyCache.

**Hanabi reference.** Current code: `src/ui/theme.h` (`MEASURED, MEMOIZED, AND NOT THE LIBRARY'S CACHE`) — theme::text_px now has an app-owned memo over advance semantics. `src/ui/theme.h::afterhours::measure_text_internal` — the measurement still uses the advance-returning path, not TextMeasureCache's ink-box path. Tests: `tests/unit/test_text_cache.cpp::a_font_swap_drops_the_lot` — app-owned text cache invalidates when the face behind the font name changes. Measurement/gate: `docs/perf/TEXT.md` (`returns the pen ADVANCE and TextMeasureCache caches the INK BOX`) — perf notes record why the shared cache is not a drop-in. Proof-patch decision: `vendor_patches/README.md` rejects the one-line swap as pixel-unsafe because the measured semantics differ by 2px on current strings.


**Minimal upstream fix.** Make the two agree — pick advance or ink box, use it
in both `measure_text_internal` and `measure_text`, and document which. Failing
that, expose the cache over `measure_text_internal`'s semantics as a second
entry point (`ui::measure_advance_cached(text, size)`), so an app can cache the
number it is already using without adopting a different one.

CLASS: FOOTGUN

### #138 — Rebuilding one widget costs ~4.6 heap allocations per frame, so any list that must draw a mark per item is linear in allocations however well the CONTENT is virtualized

**What was wanted.** A minimap rail beside the transcript: one small mark per
message, the whole thread at a glance. Every mark is on screen by definition —
that is what a minimap is — so there is nothing to virtualize away.

**What happens.** Each mark is a widget, and an immediate-mode widget is
rebuilt every frame:

```cpp
auto slot = button(ctx, mk(rail.ent(), static_cast<int>(i) + 1),
    ComponentConfig{}
        .with_size(...)
        .with_custom_hover_bg(...)
        .with_on_draw_fg([mark, hot](RectangleType r) { ... })
        .with_debug_name("minimap_mark_" + std::to_string(i)));
```

Counted with a global `operator new` counter over 300 frames, the minimap's
own scope allocates 72 / 572 / 2,218 times per frame for 12 / 120 / 480
messages — **~4.6 heap allocations per widget per frame**, flat per item. At
480 messages and 60 fps that is 133,000 allocations per second for a strip of
dots.

`ComponentConfig` is the shape of it: it is a by-value builder holding
`std::string debug_name`, `std::string font_name`, `std::vector<TextSpan>
styled_label` and two `std::function` draw callbacks, constructed and destroyed
per widget per frame, and the entity's components are added and resolved
alongside.

For the transcript proper this does not bite, because virtualization means only
the visible turns are built and the count is flat in thread length — measured
5,456 allocations per frame at 120 messages and 5,534 at 480, correctly
independent. It bites precisely where the widget count is the item count by
design.

**Why the obvious escapes do not work.**

- **Virtualize the minimap.** It is a map OF the whole thread; culling it to
  the viewport is deleting the feature.
- **Coalesce adjacent marks into one widget.** `minimap::draw_mark` clamps each
  dot to `kMinDotH` and centres it in its slot, so merging two slots does not
  draw what two slots drew — it moves pixels, in the most pixel-tested pane in
  the app.
- **Draw all the marks from the rail's own `on_draw_fg`** — one widget, N dots.
  This does work for the drawing, and loses per-mark hover (`with_custom_hover_bg`
  is per-widget) and per-mark hit testing. Hover would have to be
  re-implemented against a hand-rolled hit test, which is #111 again.
- **Skip invisible marks.** Already done (`if (h <= 0.0f) continue;`), and it
  does not help: at 480 messages every slot is a positive fraction of a pixel,
  so nothing is skipped.

**The workaround, and its cost.** None applied. It is documented and left: at
120 messages the minimap is 0.13 ms and 572 allocations per frame, which is
real but is not what was making long threads slow, and every way of removing it
either changes what is drawn or re-implements hover by hand. hanabi's slope
gate (`scripts/perf_transcript_slope.sh`) sets its allocation limit at 12 per
message specifically to leave room for this, and says so.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The original “none applied / one mark per message by design” claim is stale; current source groups dense rails.

**Hanabi reference.** Current code: `src/ui/minimap_marks.h` (`inline std::vector<Slot> group_marks`) — current minimap groups dense marks instead of requiring one widget per item. `src/ecs/main_pane_system.h` (`hanabi::minimap::group_marks(markH, markKind, top, totalH, railH)`) — transcript minimap uses grouped slots for mark widgets. Tests: `tests/unit/test_minimap_marks.cpp` (`minimap mark grouping`) — unit test covers mark grouping arithmetic. Measurement/gate: `docs/perf/EVENTS.md` (`The minimap rail was 2,263 widgets and a solid stripe — FIXED`) — perf notes record the fix and before/after mark count reduction.


**Minimal upstream fix.** Let a `ComponentConfig` be built once and reused
across frames, or give the immediate-mode API a batched primitive for "N
uniform children of this parent" that resolves rects without a full config per
child. The narrower version: make `debug_name` a `std::string_view` or drop it
in release builds, and give `on_draw_fg` a non-owning callable overload — those
three fields are the per-widget allocation.

CLASS: PERFORMANCE

---

### #125 — `load_texture` takes a path and nothing else, so a UI that draws thumbnails has to hold full-resolution pixels

**What was wanted.** To show a pasted screenshot as a 64px composer chip, and
the same image inline in a transcript at most 420px tall, without keeping
3024x1964 pixels resident to do it.

**What happens.** The whole texture API is `load_texture(const char* path)`. It
stbi_loads the file at its natural size, uploads that, and returns a
`TextureType` whose `width`/`height` are the file's. There is no
max-dimension, no scale factor, no "decode to fit this box" — and no
`load_texture_from_pixels` in the public surface either (it exists, but inside
`metal_texture_detail::`, which is a backend implementation namespace, and it
is absent from the raylib backend, so a consumer that used it would compile on
one backend only).

`draw_texture_pro` then scales the full-size texture down at draw time, which
is correct and free on the GPU. The cost is entirely residency: a Retina screen
grab is 23.7 MB of RGBA to draw a 64x64 chip, a factor of about 5,800.

Measured in hanabi (`docs/perf/MEMORY.md`, entry 4): sixty 640x480 PNGs shown
once each cost 114 MB of RSS, 1.9 MB apiece, all of it full-resolution pixels
for images drawn no larger than a chip.

**Why the obvious escapes do not work.**

- **Downscale the file first and load that** — it means writing a resized copy
  to disk for every image the user pastes, which is a cache of files to manage,
  invalidate and clean up, in exchange for not managing a cache of textures.
- **stbi_load it ourselves, box-filter, and call the pixel upload** — the
  upload entry point is backend-private (above), and `stb_image` is vendored
  under afterhours' own vendor dir with `STB_IMAGE_IMPLEMENTATION` already
  defined in an afterhours translation unit, so a consumer linking its own copy
  is one ODR violation away from a very confusing bug.
- **Just cache fewer of them** — this is the workaround below, and it bounds
  the total without touching the per-image cost. The budget has to be at least
  as large as one frame's working set, so "five screenshots visible at once"
  still means 119 MB with or without a cache.
- **Draw from a smaller source rect** — `draw_texture_pro`'s source rect
  crops, it does not resample the resident copy.

**The workaround, and its cost.** hanabi's `src/ui/inline_image.h` now runs a
32 MB LRU over decoded pixels, calling `unload_texture` on eviction and
refusing to evict anything touched in the last sixteen accesses (so a frame
never evicts what it is about to draw and re-decode). It holds the total flat —
180 images cost the same as 60 — and it does not reduce the per-image cost at
all. About 70 lines, and the 28x saving that a decode-to-fit would give is
still on the table.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The LRU workaround still exists, but the original “does not reduce per-image cost” claim is stale: current source also decodes oversized images to fit.

**Hanabi reference.** `src/ui/inline_image.h` (`It now decodes to the size it draws at`) — inline image cache no longer keeps full-resolution pixels for oversized screenshots. `src/ui/decode_to_fit.h` (`Decode an image to the size it will be DRAWN at`) — Metal-only decode-to-fit workaround uses backend-private upload. Tests: `tests/unit/test_downscale.cpp::test_halving_a_retina_grab_is_a_four_fold_saving` — unit test pins the decode-to-fit reduction. Measurement/gate: `docs/perf/MEMORY.md` (`A Retina screen grab is 3024x1964: 31.6 MB resident`) — memory notes record the full-resolution cost and decode-to-fit result.


**Minimal upstream fix.** One optional argument:
`load_texture(path, max_dimension = 0)`, resampling during decode when the
image exceeds it. stb_image_resize is already a header in the same vendor tree.
The caller knows its draw box; the library is the only one holding the pixels.

CLASS: WORKAROUND

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): the ratio above is 1,450, not
5,800.** 23.7 MB of RGBA over a 64x64 chip is 23,756,544 B / 16,384 B =
**1,450**, and the same figure in pixels: 5,939,136 / 4,096 = 1,450. 5,800 is
exactly four times it -- bytes on one side of the ratio and pixels on the other.
The shape of the ask is unchanged and so is every measured number under it; the
upstream payoff this entry advertises is 1,450x, which is still the largest
single multiple in this file.

### #126 — A GPU texture is not a malloc block, and nothing in afterhours will say how many bytes it is holding

**What was wanted.** To find a memory leak with the same instrument that found
the last one.

**What happens.** hanabi's soak probe (`src/util/soak.h`) reads the malloc
zones' own `blocks_in_use` and `size_in_use`. That instrument is excellent: it
moves the instant something is not freed, and the mean block size says what
kind of thing leaked. It is how the Metal autorelease leak was found in one
run.

It is also completely blind to GPU memory. Over the rung that leaked 114 MB of
textures, live malloc moved **+427 KB and +677 blocks** — nothing. Every byte
was inside `sg_make_image`, which is an IOGPU resource, not a heap allocation.
The only signal is process RSS, which is a single number for the whole process
and attributes nothing.

And afterhours cannot be asked. `TextureType` carries `width`, `height` and
three sokol ids; there is no `gpu_bytes_in_use()`, no live-image count, no
allocation callback. sokol_gfx itself keeps exactly this bookkeeping —
`sg_frame_stats`, and the pool sizes in `sg_desc` — and afterhours' backend
wrapper surfaces none of it.

**Why the obvious escapes do not work.**

- **Compute it in the consumer from `width * height * 4`** — this is what
  hanabi does, and it is a guess: it does not know the pixel format the backend
  chose, whether a mip chain was built (`load_texture`'s own comment says it
  uploads a full chain), or what the driver padded the row stride to. It is
  right to within a factor, which is enough for a budget and not enough for a
  measurement.
- **Read RSS before and after** — RSS is page-granular, lags by seconds, is
  moved by everything else in the process, and does not fall on free until
  something asks the allocator to release (and never for GPU pages). The ladder
  in `src/util/mem_ladder.h` calls `malloc_zone_pressure_relief` for exactly
  this reason and it does not help with textures.
- **Use Instruments' Allocations / VM Tracker** — it can see it, and it cannot
  be a `make test` gate, which is the whole point of having a number.

**The workaround, and its cost.** hanabi tracks its own `w * h * 4` estimate
and prints it on the ladder, and treats process RSS as the ground truth for
anything GPU-shaped. Two instruments, one of which is trustworthy and blind and
one of which is honest and vague, and a whole class of leak that only shows up
as "the number is bigger and I do not know why".


**POSTSCRIPT 2026-08-26 (source-reference audit).** The original w*h*4-plus-RSS workaround is stale; current source budgets mip-chain bytes, tracks a ledger, and can read Metal device bytes.

**Hanabi reference.** `src/util/gpu_mem.h` (`device_bytes() GROUND TRUTH`) — current source can read Metal device allocation when built with GPU accounting. `src/util/gpu_mem.h` (`ledger() ATTRIBUTION`) — current source keeps a Hanabi texture ledger beside the device total. Tests: `tests/unit/test_gpu_mem.cpp::test_the_ledger_tracks_live_bytes_and_churn` — unit test covers ledger bytes/live/churn behavior. Measurement/gate: `docs/perf/GATES.md` (`A leak that is not on the heap`) — gate notes record GPU memory accounting as the fix for the heap blind spot.


**Minimal upstream fix.** Expose what sokol already counts:
`graphics::gpu_bytes_in_use()` and `graphics::gpu_image_count()`, or simply
forward `sg_query_frame_stats()`. Ten lines in the backend wrapper, and the
malloc-shaped instrument every consumer already has stops having a hole in it.

CLASS: IMPOSSIBLE


---

### #145 — `begin_frame`/`end_frame` are two free calls, not a frame SCOPE, so the Metal objects the library autoreleases have nowhere to be drained and nothing says so

**What was wanted.** A render loop that does not grow.

**What happens.** `graphics::begin_frame()` reaches
`backends/sokol/backend.h:391`, which calls `sg_begin_pass`. On Metal that
returns a command buffer, a render-pass descriptor and its colour, depth and
stencil attachment descriptors — six autoreleased Objective-C objects per
frame, created **inside the library**, owned by nobody. In a Cocoa app the run
loop's own pool drains every iteration. A render loop is not a run loop, so
they stay live for the life of the process: measured over a 10,248-frame idle
run, exactly one of each per frame, ~2.5 KB a frame, ~9 MB a minute, never
returned. That is the whole of a shipped "it gets slower and slower every
second until it freezes".

The fix is four lines in the consumer (`src/util/autorelease.h`,
`hanabi::AutoreleaseFrame`, opened at the top of every frame loop). The gap is
that the library gives the consumer no place to put them and no reason to know
they are needed:

- `graphics_common.h:294` and `:309` are two independent free functions. There
  is no `graphics::Frame` RAII object, no `on_frame_begin` / `on_frame_end`
  hook, and no scope the library owns that a caller could hang a pool off. A
  caller writes `begin_frame(); …; end_frame();` by hand, in as many loops as
  it has — hanabi has eleven — and each one is a separate opportunity to
  forget.
- Nothing in the backend's own code, comments or headers mentions
  autorelease. The Metal path was added without one and the library's own
  examples do not have one either, so every consumer that renders on Apple
  inherits the leak silently. Twenty projects vendor this.
- The consumer cannot fix it where it belongs, because `vendor/afterhours` is
  read-only.

**Why the obvious escapes do not work.**

- **Drain inside `end_frame`** — that is the right place and it is the library's
  to write, not ours. A patch here is a patch to a submodule twenty projects
  share.
- **Wrap `sm.run()` instead of the whole frame** — the objects are created by
  `begin_frame`, before `sm.run()` is reached, so a pool around the systems
  drains nothing that matters.
- **One pool for the whole loop** — a pool that is popped when the app exits is
  the leak with extra steps.

**The workaround, and its cost.** An RAII class in the consumer, opened as the
first line of every frame loop, plus a source check
(`scripts/check_autorelease.py`) whose entire job is to notice when someone
deletes one — because a four-line RAII object with no callers and no return
value reads as dead code in a diff. Writing the check found **two more loops
that had never had a pool at all** (the frame-timing diagnostic and the
scripted-UI loop, the latter running 85 test scripts). Eleven call sites, one
runtime gate and one source gate, for something a scoped type in the library
would have made unforgettable.


**Hanabi reference.** `src/util/autorelease.h` (`class AutoreleaseFrame`) — RAII pool that brackets frame/texture work on Apple platforms. `src/main.cpp` (`const hanabi::AutoreleaseFrame framePool;`) — main frame loops instantiate the pool around graphics work. Tests: `scripts/check_autorelease.py` (`check_autorelease: every graphics::begin_frame()`) — scripted source check is the regression gate for the workaround. Measurement/gate: `docs/perf/GATES.md` (`one const hanabi::AutoreleaseFrame framePool; line`) — gate notes show the check failing when one pool is deleted.


**Minimal upstream fix.** Either (a) push and pop an autorelease pool inside
`begin_frame`/`end_frame` on Apple platforms — two `objc_autoreleasePool*`
calls, already in libobjc, no Objective-C++ translation unit needed — or (b)
ship a `graphics::FrameScope` RAII type that brackets the pair, so the frame
has a scope and the pool has an owner. (a) is four lines and fixes every
consumer that already exists.

CLASS: FOOTGUN

---

### #146 — Nothing reports the size of the tree the library just built, so "did this stay O(1) in the data?" has to be answered by walking an internal collection, and the answer counts survivors rather than work

**What was wanted.** A gate that fails when the per-frame cost starts scaling
with the size of the session list — the assertion that a virtualization fix,
once made, stays made.

**What happens.** The honest measurement is "how many widgets did this frame
build", because frame time on a shared machine is not portable (the same binary
read 8.27 ms on a quiet minute and 16.07 ms on a busy one, so a millisecond
threshold is a coin flip; a ratio between two data sizes is not). The library
exposes no such number. What hanabi does instead, in `src/main.cpp`, is:

```cpp
for (const auto& e :
     afterhours::ui::UICollectionHolder::get().collection.get_entities())
  if (e && e->has<afterhours::ui::UIComponent>()) ++widgets;
```

Three problems with that as an instrument:

- It reaches through `UICollectionHolder::get().collection`, which is the
  library's own storage. A consumer counting a library's internal container is
  a consumer that breaks when the container changes.
- It counts what the tree **is** at the end of the frame, not what was
  **built** during it. A widget created and discarded inside the frame costs
  real layout and real allocation and is invisible to this count.
- It is a whole-tree number with no breakdown. 2985 widgets at a 2000-session
  catalog against 348 at 20 says the frame scales with the data; it does not
  say which subtree does, which is the first thing anyone wants next.

**Why the obvious escapes do not work.**

- **Time it instead** — that is the thing that is not portable, which is why
  the count was wanted.
- **Count by debug name** — `UIComponentDebug::name_value` exists and is how
  the soak driver finds the scroll view (#147), but naming is per-call-site and
  optional, so a count over names measures how diligently the app named things.
- **Count ECS entities** — that is every entity in the world, UI or not; it
  moved from 268 to 2993 across the same two catalog sizes and cannot separate
  a widget from a data component.

**The workaround, and its cost.** `scripts/scaling_gate.sh` gates the ratio of
that survivor count between two catalog sizes (8.58x at 100x the data today, on
five runs, with zero spread — it is at least perfectly repeatable). The cost is
a gate that is one indirection away from the library's internals and blind to
transient widgets.


**Hanabi reference.** `src/main.cpp` (`widgets={} min={:.2f}ms median={:.2f}ms`) — frame-timing output reports a UI widget survivor count. `scripts/scaling_gate.sh` (`WIDGET ratio — how many UI widgets the frame builds`) — catalog-scaling gate consumes that count as its deterministic ratio. Measurement/gate: `docs/perf/GATES.md` (`UICollectionHolder's collection after the frame`) — perf gate docs record that the count is a post-frame collection count.


**Minimal upstream fix.** A per-frame counter on the UI context — widgets
created, widgets laid out, widgets drawn — readable after `end_frame`. It is
three increments in code that already runs, and it turns "is this O(1) in the
data?" from an archaeology exercise into an assertion.

CLASS: WORKAROUND

---

### #147 — A scroll view can only be driven from outside by finding its entity by DEBUG NAME, so the one input the bug report was about is reachable only through a string

**What was wanted.** A soak arm that scrolls the sidebar, because the report was
"open it, scroll the sidebar up and down until it breaks".

**What happens.** There is no handle on a scroll view. The widget is rebuilt
every frame and its state lives in a `HasScrollView` component on an entity
whose id the caller never sees; `imm::` returns a wrapper valid for that frame
only. So the driver (`src/util/soak.h`, `scroll_sidebar`) walks every entity in
the world, looks for a `UIComponentDebug` whose `name_value` is
`"sidebar_scroll"`, and writes `scroll_target` on it:

```cpp
for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod()) {
  …
  if (e.get<afterhours::ui::UIComponentDebug>().name_value != "sidebar_scroll")
    continue;
```

The consequences are all the ones a string lookup has. Renaming a debug name —
which is documentation, and reads as safe — silently turns the scroll arm into
a second idle arm that passes forever. Compiling without debug names would do
the same. The lookup is O(entities) and is done per frame. And it can only be
written for a widget somebody remembered to name.

**Why the obvious escapes do not work.**

- **Inject a wheel event** — the injector is behind
  `AFTER_HOURS_ENABLE_E2E_TESTING`, so it is absent from the binary a person
  actually runs, which is the binary the bug report is about.
- **Keep the entity id from the frame that built it** — the tree is cleared and
  rebuilt every frame (#27); the id is stable only because `mk()` derives it,
  and `mk()`'s inputs are private to the building code.
- **Hold the `HasScrollView` reference** — same lifetime problem, one level
  down.

**The workaround, and its cost.** The name lookup above, plus a comment
explaining why it is a name and not a handle, plus the standing risk that a
rename disables a gate without failing anything. It is the reason the scroll
soak arm is the one arm in `make soak` that cannot prove it is still driving
anything.


**Hanabi reference.** `src/util/soak.h` (`scroll_named(const char* debugName, float dy)`) — The soak driver walks UI entities by debug name, counts ambiguous matches, then mutates HasScrollView scroll_target/scroll_offset. Measurement/gate: `docs/perf/TRANSCRIPT.md` (`HANABI_STRESS=scroll drives sidebar_scroll, by debug`) — The perf docs record that the scroll soak is driven through the named scroll-view path.


**Minimal upstream fix.** Have `imm::scroll_view` (and the other stateful
widgets) return a stable, addressable handle the caller can keep — an id plus
an accessor that resolves it against the current frame's collection — so an
out-of-tree driver addresses a widget rather than searching for one.

---

### #115 — A widget that stops being built is never retired, so every system walks the union of every screen the app has ever shown

**What was wanted.** For a screen the user has navigated away from to stop
costing anything. hanabi's Home pane builds a card per attention-worthy
session; open a thread and Home is gone. It should be gone from the frame
budget too.

**What happens.** It is never gone. `imm::mk()` keeps a permanent
`std::map<UI_UUID, EntityID> existing_ui_elements` and hands back the same
entity for the same call site forever
(`src/plugins/ui/entity_management.h`). Nothing marks an entity as "not built
this frame", nothing sweeps one, and the library's own
`clear_existing_ui_elements()` is called from nowhere in the library and would
orphan the entities rather than destroy them. Meanwhile
`run_systems_on_ui_entities` (`src/plugins/ui/utilities.h`) iterates
`ui_coll.get_entities_for_mod()` -- the WHOLE collection -- once per system per
frame, twice for a render system (mutable pass then const pass). So the set
every pass walks is the union of every widget any screen has ever built, and it
only grows.

Gap #27 records this half of the design approvingly -- "`mk()` retains ENTITIES
by UUID (good — no per-frame alloc churn)" -- and for a screen you keep coming
back to that is right. What it misses is that retention with no retirement is
not a cache, it is a leak with a bounded key space.

Measured in hanabi, at a 2020-session catalog, idle, chat tab open:

  * `render_home` runs **twice** in the first ~1000 frames and never again
    (instrumented count; `app.view` is Chat from frame ~3 onward).
  * Those two frames build **696 digest cards, 2784 entities**, which are
    still in the collection at frame 800 -- the soak's entity census reports
    them.
  * Frame time with them present: **4.59 ms**. With the sections capped so
    only 80 cards are ever built: **1.44 ms**.

**3.15 ms a frame, 69% of the frame, for a screen that was drawn twice.**
Not drawn 800 times -- twice. Every one of those 800 frames paid to lay out,
hit-test and consider drawing widgets belonging to a pane that was not on
screen. The user's report was "it gets slower and slower until it freezes";
this is the shape of the half of that which was not the Metal leak.

**Why the obvious escapes do not work.**

- **Stop building the widget when the screen is not shown.** Already true --
  that is what made this measurable. It does not help, because the entity from
  the two frames it WAS shown persists. There is no "and destroy what I built
  last time" to pair with it.
- **Call `clear_existing_ui_elements()` on a screen change.** It clears the
  hash->id map, not the entities. The next `mk()` from the same call site
  allocates a NEW entity and the old one stays in the collection, unreferenced
  and still iterated. It converts a bounded set into an unbounded one.
- **Delete the entities from the app side.** An app can reach
  `EntityHelper::get_entities_for_mod()`, but the ids are inside `mk()`'s
  private map; there is no way to ask "which entities belong to this subtree"
  or "which were not built this frame". Guessing by `UIComponentDebug::name` is
  matching on a debug string.
- **Wait for `ClearVisibity` to make them free.** It clears visibility and
  children, which is what makes a stale entity cheapER than a live one -- but
  the per-entity, per-system, per-frame iteration is unconditional, and at
  ~1.1 us per entity per frame (3.15 ms / 2784) that iteration is the cost.

**The workaround, and its cost.** Cap what is ever built, everywhere, so the
high-water mark is small: hanabi's Home now renders at most 20 cards per
section (commit "Home's Recent list was capped..."). This is the same
workaround gap #23 already forces for scroll views, arrived at from the other
direction -- there it is "do not build what is off screen", here it is "do not
build what may leave the screen", and both are the app hand-rolling the
lifetime the framework does not offer. The cost is that every list in the app
now needs a cap whether or not it has a scroll view, the cap has to be chosen
by hand against a viewport, and a section header has to carry the true count
because the rows no longer do. And it is a high-water mark, not a fix: a user
who visits one big screen still pays for it for the rest of the session.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's original claim that app-side deletion was impossible is stale; the postscript and current source show the public map plus mk shadowing workaround shipped.

**RESOLVED UPSTREAM 2026-08-29.** Afterhours `2393fe3` and `c682382` preserve survivor order, stamp every immediate widget, remove stale map entries, and retire unbuilt widgets after a configurable grace window. Hanabi now uses that lifecycle directly; its wrapper remains only to avoid the separate hot-path string construction in gap #180.

**Hanabi reference.** `src/ui/mk.h` refreshes the upstream `UIElementRecord`; `src/ui/widget_epoch.h` is now a measurement/configuration adapter rather than a second retirement implementation. Tests: `tests/unit/test_widget_retire.cpp`, `tests/ui/widgets_of_a_screen_you_left_are_retired.e2e`, and `scripts/retire_gate.sh`.


**Minimal upstream fix.** A frame stamp and a sweep. `mk()` already touches
every live element -- write the current frame number onto the entity when it
does. At end of frame, retire entities whose stamp is older than N frames (N a
small grace so a screen toggled every other frame does not thrash): remove the
id from `existing_ui_elements` and mark the entity for cleanup. Two fields and
one pass, and it is the only place with both halves of the information. A
weaker version that would still have caught this: expose the collection size
and a per-frame "built" count so an app can see the two diverge -- hanabi had
to add its own entity census to find that out.

CLASS: RESOLVED UPSTREAM

**POSTSCRIPT, 2026-08-25: fixed from the app side, and this entry was wrong
about why it could not be.** The escape list above says:

> **Delete the entities from the app side.** An app can reach
> `EntityHelper::get_entities_for_mod()`, but the ids are inside `mk()`'s
> private map [...]

`existing_ui_elements` is not private. It is an `inline std::map` at namespace
scope in `afterhours::ui::imm` (`entity_management.h`), so any app that
includes the header can read it, iterate it and erase from it. That one
mistaken word is the difference between "the library must fix this" and "the
app can", and it stood in this file for a month.

The second half is that `mk` can be SHADOWED. hanabi imports it through
exactly one using-declaration and ADL cannot reach
`afterhours::ui::imm::mk` on its own, so replacing that line puts hanabi's own
`mk` in front of the library's at all 335 call sites at once -- forwarding to
`imm::mk` (same call-site hash, same entity, same reuse) and recording the
frame that built it. `src/ui/widget_epoch.h`.

With both, the sweep this entry asks the library for is fifty lines of app
code: stamp on build, and once every 15 frames walk the map and, for any entry
whose entity has not been built for 90 frames, erase the hash AND mark the
entity for cleanup (afterhours' own post-update bridge destroys it before the
frame renders). Erasing the hash is the half that matters -- afterhours
recycles EntityIDs, so an entry left pointing at a destroyed entity hands the
next `mk()` at that call site a different widget.

    views arm, 2000 sessions, 3600 frames, sampled every 360 (one whole
    navigation cycle, so every sample is on the same screen):

                    sweep off     sweep on
      entities           2844          213     13.4x fewer
      ms/frame           4.57         3.13     -31%
      live heap        48.8 MB      43.5 MB    -5.3 MB

**The upstream fix is still worth doing**, for four reasons the app-side
version cannot cover:

  * Every app that vendors afterhours has to write this, and each one has to
    discover that the map is public and that `mk` is shadowable.
  * It cannot retire what it did not create. Nine entities per run in hanabi
    are the library's own (the UI root, scrollbars, the drag spacer) and no
    app-side sweep can ever see them -- #162.
  * It depends on `mk` being the ONLY way an entity enters
    `existing_ui_elements`, which is true today and is not a documented
    contract.
  * It costs 0.030 ms/frame of stamping that the library would get for free:
    `mk()` already has the entity in hand at the moment it decides to hand it
    back. From the app side that write is a second cache line -- #160.

The class stays WORKAROUND. It is a good workaround, it is gated
(`scripts/retire_gate.sh`) and tested, and it is still an app re-implementing
widget lifetime because the framework does not have one.

### #116 — There is no way to ask how much of a string fits in a width, so every ellipsized label is O(n) whole-string measurements

**What was wanted.** A sidebar row title cut to its column with an ellipsis --
the single most common operation in a list UI, done once per visible row per
frame.

**What happens.** The only measuring primitive is
`measure_text(font, cstr, size, spacing)`, which measures a WHOLE string and
returns its extent. There is no prefix-width query, no per-glyph advance
array, no "how many code points fit in W", and no single-line truncation
helper. `ui::detail::wrap_text_to_width` exists but is for WRAPPING, takes a
whole-string `measure` callable, and is itself built out of repeated
whole-string measurements.

So the only expressible algorithm is: guess a cut point, build that prefix,
measure the whole prefix, adjust. hanabi's was the natural one -- walk the cut
point back one code point at a time -- and since measuring a prefix is itself
linear in the prefix (fontstash walks every glyph and
`stbtt_GetGlyphKernAdvance` binary-searches the kern table per pair), one title
cost O(len^2) glyph work plus a `substr` allocation per probe. `sample` put it
at **34% of the main thread** (2025 of ~5900 samples over 8 s) at a
2000-session catalog, on 38 visible rows.

**Why the obvious escapes do not work.**

- **Estimate from an average advance.** That is what the code did before it
  measured, and the comment above it records why it stopped: the estimate is
  calibrated to one font at one size, so it clips early or overflows the column
  the moment either changes.
- **Binary search the cut point.** Works, and hanabi now does it (O(log n)
  measurements) -- but only because prefix width is monotonic in prefix length,
  which kerning does not guarantee. The library knows the kern values and could
  answer exactly; a consumer binary-searching from outside can only assume, and
  has to document that its answer may be one glyph short on a font that
  violates the assumption. hanabi's `src/util/ellipsize.h` carries that
  paragraph because there is nowhere better to put it.
- **Cache the answer.** hanabi does that too, and it is the bigger win for an
  idle frame -- but it only moves the cost to the frames that matter, which are
  the ones where the text or the width is new: a resize, a scroll into unseen
  rows, a live search. Those are exactly the frames a user is watching.
- **Use `TextMeasureCache`.** It memoizes whole-string measurements, so it
  turns O(n) DISTINCT measurements into O(n) cache lookups on a string that
  changes length every probe. It does not make the algorithm sublinear, and
  gap #42 already records that the draw path does not consult it anyway.

**The workaround, and its cost.** `src/util/ellipsize.h`: 100 lines,
binary search plus a scratch buffer, wrapped in a memo at the call site, plus
`tests/unit/test_ellipsize.cpp` -- 190 lines whose entire job is to prove the
new answer equals the old one, including a synthetic backwards-kern metric
that demonstrates the case the binary search provably cannot get right. Roughly
300 lines and a documented correctness caveat, to do what one library call
should do exactly and in one pass.


**Hanabi reference.** `src/util/ellipsize.h` (`fit_to_width(const std::string& text, float maxW`) — Hanabi implements the app-side O(log n) ellipsis helper with the documented kerning caveat. Tests: `tests/unit/test_ellipsize.cpp` (`reference_fit(const std::string& text, float maxW`) — Differential tests compare the fast helper with the old linear reference over synthetic rulers. Measurement/gate: `docs/perf/TEXT.md` (`measure calls per ellipsized title (cold)`) — The text perf report records before/after per-operation counts for ellipsized titles.


**Minimal upstream fix.** One function beside `measure_text`:

    // Bytes of `text` that fit in `max_width`, on a code point boundary.
    size_t measure_fit(Font, const char* text, float size, float spacing,
                       float max_width);

fontstash already walks the glyphs accumulating advances; this is that walk
with an early exit, so it is ONE pass and exact under kerning -- strictly
better than anything a consumer can write, and cheaper than what it replaces.
A `truncate_to_width(..., ellipsis)` on top would remove the ellipsis-budget
arithmetic every consumer currently repeats.


CLASS: WORKAROUND


---

### #155 — The first few draws cost 5-8x a warm draw and there is no way to pre-warm, so every launch pays pipeline compilation inside its first measured frame

**What was wanted.** A cold-launch number that measures the app, and a way to
move unavoidable GPU warm-up off the critical path — ideally to overlap it with
the work the app does before it needs to draw.

**What happens.** The first draws after `graphics::init` are several times more
expensive than steady-state ones, and nothing in the graphics API acknowledges
it. Measured on the headless one-shot path (`HANABI_STARTUP_PROF=1`, mock
backend, five consecutive runs), separating the three settle frames from the
first capture frame:

    phase                          span         per frame
    3 settle frames (+3ms sleep)   22-35 ms     ~6.3-10.7 ms
    capture frame 0 (warm)          1-2 ms      ~1-2 ms

So the first three draws cost **5-8x** what the same scene costs once the
pipeline is warm. This is Metal compiling pipelines/shaders on first use, which
is a driver cost rather than an afterhours one — but afterhours is what owns
the surface, and it offers nothing to manage it:

- no `warm_up()` / `precompile()` / `prewarm_pipelines()` on the graphics API
- no way to submit a throwaway frame that is explicitly not presented
- no signal that the pipeline cache is cold, so an app cannot even *report*
  honestly that this launch will be slower than the next one

The practical consequence is that **the cost lands inside whichever frame you
happen to be measuring**. `FirstFrame` is logged at the first frame of the
capture loop, so the warm-up either sits inside it (making the gate number
worse) or sits in whatever ran before it (making the gate number better while
the app is no faster). That is a metric that moves when you reorder code that
does the same work — the worst property a gate can have.

**Why the obvious escapes do not work.**

- **Warm up during init** — `graphics::init` returns before anything is drawn,
  and there is no draw call available that does not go through
  `begin_frame`/`end_frame` and present. Drawing a warm-up frame IS a frame.
- **Pump state without drawing** — this one DOES work, and is worth recording
  as a non-gap: `SystemManager` exposes `tick_all()` and `render_all()`
  separately, so an app can advance async state without a render. It does not
  help here, because the cost being moved is the render.
- **Measure from a later frame** — that is just choosing a flattering number.
- **Warm the OS cache instead** — `~/…/C/com.apple.metal` persists between
  runs, which is exactly why a first launch after a build is 38 ms of App init
  and the next is 1 ms (gap #8). It is not something the app can do anything
  about at launch time.

**The workaround, and its cost.** None available; the cost is paid and then
attributed by hand. What this branch did instead was make the *instrumentation*
honest: `[hprof]` marks (src/main.cpp, gated on `HANABI_STARTUP_PROF`) now
attribute every span between process start and `FirstFrame`, so the warm-up is
visible as its own line rather than hiding inside whichever phase happens to
contain it. That is documentation, not a fix — the 22-35 ms is still spent.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The gap remains, but the original 5-8x/6.3-10.7 ms framing is stale: STARTUP.md says the per-frame number included sleep and is about 3x too high on CPU time.

**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** A `graphics::prewarm()` that renders and discards one
frame against the real pipeline state, callable right after `init` and before
the app's own timer starts. Failing that, a `graphics::pipeline_cache_cold()`
predicate, so an app can at least say which of its launch numbers are
comparable to each other.

CLASS: PERFORMANCE


---

### #117 — A scripted test pins screen coordinates, so it goes stale silently and its failure names nothing

**What was wanted.** To know whether `make test` failing is my fault.

**What happens.** `select_word_and_line.e2e` fails on this branch, and on
unmodified master, and has presumably been failing for a while. Its whole
content is two coordinates:

    double_click 415 225
    ...
    triple_click 415 225

The script carries a careful comment about them -- "the three body lines sit at
209 / 225 / 241", and a history of the two previous times they moved (from
184/200/216, then from 226/242/258) -- which is a maintenance log for a
constant that cannot check itself. Instrumenting hanabi's own hit test says the
lines are now at **y = 228 / 244 / 260**, so the click at 225 lands in the 3px
gap above the first one and `text_select::update` is never entered with `hot`.
The failure it reports is `Text not found: '6 selected'`, which names the
composer's readout: two hops from the click that missed, and no clue that a
coordinate is stale.

This is gap #86 ("a capture cannot say where anything landed") and gap #51
seen from the maintenance end rather than the authoring end. The reason the
script holds a number instead of a query is that there is nothing to query;
the reason it goes stale is that a number cannot be re-derived; and the reason
the failure is unhelpful is that the assertion is on a downstream label rather
than on the gesture. Filed separately because the COST is different and
compounding: #86 is "writing this test took a bisect", this is "the test now
lies, on master, and every future run of the suite starts by re-litigating
whose change broke it".

**Why the obvious escapes do not work.**

- **Re-measure and move the number.** What the script tells you to do, and the
  numbers here are measured, not guessed. It does not fix the class: this is
  the fourth set of coordinates and there is no reason to think it is the last.
  (Moving the click to 236 -- the middle of the re-measured first line -- gets
  it into the line and still does not produce the expected selection, so the
  word under x=415 has moved too. Two coordinates, both stale, and the script
  can only assert on the consequence of both being right.)
- **Assert on the gesture instead of on a downstream label.** There is nothing
  to assert on. `assert_ui` reads x/y/w/h/hidden/text (#61); a selection is a
  band drawn behind text and the range itself is not on any component.
- **Query the text's position and click that.** The whole of #86: the
  screenshot path emits pixels and nothing else, and no runtime API reports
  where a laid-out run landed.

**The workaround, and its cost.** hanabi's is the comment: a coordinate, the
three previous coordinates, and an instruction to re-measure by rendering the
frame and scanning the PNG. It costs a stale test nobody can attribute -- this
one cost forty minutes of bisecting onto master, and an entry in this file that
had to be retracted and rewritten because the first diagnosis (a wall-clock
multi-click window racing a frame-counted harness) was plausible, wrong, and
would have sent the next person into the vendor.


**POSTSCRIPT 2026-08-26 (source-reference audit).** Several quoted coordinates and the claim that click_text was only a proposed fix are stale. Current source has click_link for tracker links, while double/triple-clicking a text run still remains coordinate-based.

**Hanabi reference.** `src/ecs/e2e_commands.h` (`click_link <id>`) — Hanabi added a custom command that clicks the renderer-derived rect for tracker links instead of a stale coordinate. `src/ui/link_detect.h` (`painted_rects()[id] = r`) — The renderer records link rects for the e2e-only click_link command. Tests: `tests/ui/tracker_links.e2e` (`click_link D948120`) — The tracker-link script no longer pins a coordinate.


**Minimal upstream fix.** The one in #86 -- have the capture/e2e layer report
laid-out rects for text runs -- plus a `click_text "ledger"` command that
resolves a rect by content and clicks its centre. Then the script says what it
means, cannot go stale, and fails with the name of the thing it could not find.

**POSTSCRIPT 2026-08-26 (gap index) — the evidence is stale for the third time,
and half the fix already exists.** Two corrections, neither of which kills the
entry:

  * **The coordinates quoted above are not the ones in the tree.**
    `tests/ui/select_word_and_line.e2e` at `2fd9e84` reads `double_click 415
    252` and `triple_click 415 252`, not `415 225`. Master moved them in
    `7f15b253444b` while this entry was being written on a branch that never
    picked the fix up (`docs/COMMIT_AUDIT.md` M12 has the chain). So the
    entry's "it fails on unmodified master" is a claim about a coordinate the
    entry no longer has. The class-level point — that a pinned coordinate rots
    and cannot check itself — is made *better* by this, not worse: the number
    in the write-up went stale before the write-up was a week old.
  * **`click_text` is already a runner command.** The fix this entry proposes
    ("a `click_text \"ledger\"` command that resolves a rect by content and
    clicks its centre") is in the pinned submodule:
    `HandleClickTextCommand` (`plugins/e2e_testing/ui_commands.h`) resolves via
    `find_component_with_text` and calls `test_input::simulate_click` at the
    centre. Three hanabi scripts already use it
    (`session_archive.e2e`, `session_archive_persists.e2e`,
    `tool_diff_is_coloured.e2e`).

What is actually missing is narrower and worth stating as the real ask: there
is no `double_click_text` / `triple_click_text`, and `click_text` resolves a
whole ELEMENT, so it cannot address a word inside a wrapped label — which is
precisely what a word-selection test needs. The general request stands
(**#51** / **#86**: report where a text RUN landed); the specific one shrinks to
two more entries in the parse switch beside `double_click_ui`. Verified by
reading the vendored source, not by running it.

CLASS: TEDIOUS

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): the coordinates above are the
fourth set and the script is on its fifth, which is the entry restating
itself.** `select_word_and_line.e2e` now clicks `415 226`, not `415 225`, and
its comment records the three body lines at **218 / 234 / 250** as element tops,
not 209 / 225 / 241. The "y = 228 / 244 / 260" measured here is superseded too:
the script gained `HANABI_SELECT_AUDIT=1`, which prints the landing element and
its rect for every press --

    [sel] press=(415.0,226.0) run=1 off=17 rect=(329.0,218.0 656.0x16.0)
          len=61 text="  acct 8842 - ledger $128.60, computed $116.20 ..."

-- so the number is now mechanically re-derivable in a normal run instead of by
rendering the frame and scanning the PNG. That is a real improvement to the
WORKAROUND and it is not the fix: the script still holds a literal, it still
cannot check itself, and a fifth move is a fifth hand edit. CLASS is unchanged.
The quotations above are left as written, because they are the state this entry
was filed against.


---


**POSTSCRIPT (2026-08-26).** Twice more, and the second time it cost four
investigations. The three body lines moved again — 218/234/250, then
244/260/276 when six feature branches landed on one day — and
`select_word_and_line.e2e` and `a_click_on_a_line_seam_is_one_click.e2e`
failed on master both times, exactly as this gap says they would. Once
re-measured they passed ALONE and in the suite, which is the detail worth
recording: a stale coordinate looks like an order-dependent flake, because
the test that was green yesterday is red today and nothing about the app
changed. `tracker_links.e2e` was the same failure, missed by that re-measure,
and spent three sessions being called a flake; one agent ran it 148 times
green inside a suite trying to reproduce it.

The maintenance log this gap complains about is now four entries long in one
script. hanabi stopped writing it: `click_link <id>` (src/ecs/e2e_commands.h)
presses the centre of the rect the RENDERER derived for that id this frame,
so the click and the hit test read the same number and a layout move cannot
put them out of step. That is the shape of the fix for any coordinate a test
pins — ask the thing that drew it — and it needs a per-feature command each
time, because the library has nothing to ask.

### #200 — Every headless resize creates five Metal render pipelines that destroying the render texture does not release, so a resize loop leaks 4.8 MB per 1000 frames — larger than the bug that started this project

**What was wanted.** A stress scenario that drags the window narrower and wider
while a soak probe watches memory. `docs/perf/GATES.md` names this as one of
the four things it could not gate: *"no gate here presses a key, opens a menu,
or resizes a window"*. The reason to want it is one layer below hanabi: the
headless backend honours a resize by destroying and recreating the offscreen
render target, and its own comment says that is fine because *"called on resize
events only (not per-frame)"*. A drag makes them sixty a second.

**What happened.** The arm goes red immediately, and it is not hanabi's.
2000 frames of resizing, one step a frame, against the mock catalog:

```
  metric        slope /1000f    per minute @60fps   budget   rising
  RSS             +5209.6 KB         +18754.6 KB       512     1.00   FAIL 10.2x
  heap bytes      +4863.9 KB         +17510.0 KB       256     1.00   FAIL 19.0x
  heap blocks    +66365.6           +238916.2         1000     1.00   FAIL 66.4x
```

18.7 MB a minute. The Metal autorelease leak that started this whole project
(`#145`) was 9 MB a minute.

**Where it is.** `MallocStackLogging=1` plus `malloc_history -allBySize`, live
allocations after ~8100 resizes, top four by count:

```
 162040 calls   7777920 bytes  _sg_init_pipeline -> MTLVertexAttributeDescriptor
  40511 calls  12963520 bytes  _sg_init_pipeline -> MTLVertexDescriptor
  40511 calls  12963520 bytes  _sg_init_pipeline -> MTLVertexBufferLayoutDescriptor
   8103 calls    388944 bytes  _sg_init_image     (exactly one per resize)
```

8103 images is one per resize, and 40510 pipelines is **five per resize**, none
of them released. The path is
`window_manager::set_window_size` → (headless branch) `unload_render_texture` +
`load_render_texture` → `sgl_make_context` → `sg_make_buffer` /
`_sg_init_pipeline`.

`unload_render_texture` looks correct — it calls `sgl_destroy_context` first and
then destroys every view, image and sampler it made. So this is not a missing
call in afterhours' own teardown: **`sgl_destroy_context` does not release the
render pipelines the context created**, and nothing above it can, because
sokol_gl's pipeline pool is not reachable from the afterhours API.

**Scope, stated honestly, because it changes who should care.** This is the
HEADLESS path. `set_window_size`'s other branch calls `metal_set_window_size`,
a Cocoa `NSWindow` resize, and never touches the offscreen target — so a person
dragging the real window does not hit this. It is a harness leak, not a user
one. It still matters: it makes the one scenario that could have gated a
user-facing resize leak unable to gate anything, because 4.8 MB a thousand
frames of upstream noise swamps whatever hanabi might be doing.

**The workaround, and its cost.** `HANABI_STRESS=resize` resizes the LAYOUT
only: it writes `ProvidesCurrentResolution` (with `should_refetch = false`, or
`CollectCurrentResolution` puts the old size straight back) and does not call
the backend. Every widget is laid out against that singleton and
`viewport::width()` feeds from the same place, so the wrap, the clip and every
width-keyed cache in hanabi see the new size — which is hanabi's own resize
cost, and it measures flat. `HANABI_STRESS_RESIZE_BACKEND=1` puts the backend
call back and reproduces the table above in one run.

The cost is that the arm draws every frame into a render target of the wrong
size. Nothing in a soak asserts on pixels so it does not matter here, but it
means this arm can never be extended into a screenshot test, and it means the
render-target half of a resize — a real cost on a real drag — stays unmeasured.


**Hanabi reference.** `src/util/stress.h::HANABI_STRESS_RESIZE_BACKEND` — The resize stress arm defaults to layout-only and can opt back into the backend resize path to reproduce the upstream leak. Measurement/gate: `docs/perf/STRESS.md` (`layout only (default) | +0.0 KB | PASS`) — The perf report records the current workaround and the backend-resize failure slope.


**Minimal upstream fix.** Either have `sgl_destroy_context` release the
context's pipelines, or — better, and entirely within afterhours — stop making
a context per render texture: `load_render_texture` creates one because it
needs a pipeline matching the target's pixel format, and a small cache keyed on
`(color_format, depth_format, sample_count)` would make that one context for
the life of the process instead of one per resize. Then a resize is an image
swap and the arm can gate the thing it was written for.

**Postscript, 2026-08-25 (perf/gpu): headless-only CONFIRMED, both ways, and
it is not GPU memory at all.**

The scope paragraph above reasons from the code. Two measurements now back it.

*A windowed resize leaks nothing.* `HANABI_GPU_WATCH=20
HANABI_GPU_WATCH_RESIZE=1` drags a REAL window through
`metal_set_window_size` and prints `-[MTLDevice currentAllocatedSize]` beside
the window size. 73 resizes, 33 distinct sizes each visited twice about forty
resizes apart: the GPU total is **identical to the kilobyte on 32 of the 33**,
and the exception reads 14 MB LOWER the second time because its first sample
predated settling. (osascript cannot do this — it needs assistive access, which
is not a permission to grant on somebody's daily machine to settle a
measurement — so the drag is driven from inside the process through the same
NSWindow call `set_window_size`'s windowed branch makes.)

*And the call graph is exhaustive rather than argued.* `load_render_texture`
has exactly two call sites in the whole program, `backend.h:353` and
`backend.h:813`, and both are inside `if (metal_detail::g_headless)` — the
windowed `graphics::init` returns false two lines later with
`@notimplemented`. `sgl_make_context` has exactly one afterhours call site,
inside `load_render_texture`. A windowed app has ONE sokol-gl context, made
once at `sgl_setup`.

*It is a HEAP leak, not a GPU one.* The resize arm with the backend on reads
`+4,928 KB` RSS and `+65,966` live blocks per 1000 frames — and the GPU column
on the same run reads **-8,192 KB**. It goes DOWN, because the arm sweeps the
render target smaller and the leaked objects are Objective-C descriptors
(`MTLVertexDescriptor`, `MTLVertexAttributeDescriptor`) that
`currentAllocatedSize` does not count. So a GPU-bytes gate can never catch this
one, exactly as a malloc gate can never catch a texture. That is not a defect
in either gate; it is why hanabi's soak now runs both.

CLASS: BLOCKING (for the scenario; the workaround measures the other half)
### #170 — `Overflow::Scroll` clips what you built; there is no way to build less, so every consumer with a long list re-implements windowing against state the library writes AFTER the build

**What was wanted.** A sidebar whose per-frame cost is its viewport, not its
catalog. The reported bug is "open the program and scroll the sidebar up and
down until it broke", and the shape of it was that a 2000-row list built 2000
rows to show nineteen: 6645 entities and 17.2 ms of CPU per frame, against 461
and 1.55 for the same list capped at two viewports.

**What happens.** A scroll view is a clip. `HasScrollView` holds an offset, a
target, a content size and a viewport size; `RunAutoLayout` positions every
child, `MeasureScrollViews` sums every child's height into `content_size`, and
the renderer scissors the ones that fall outside. Every one of those steps is
per CHILD, and the children are all of them. There is no hook that says "these
are the indices you need this frame", no spacer primitive, and no way to tell
the layout that a child is a run of N identical boxes.

So the consumer writes it. hanabi's version is ~120 lines: first =
`scroll_offset.y / rowHeight`, span = `viewport_size.y / rowHeight` plus
overscan, two `div`s of the exact height of the rows that were skipped so
`content_size` still comes out right, and an id scheme (#171) that keeps the
window's entities from accumulating.

Two things make that harder than it sounds, and both are properties of the
library rather than of the problem:

- **The numbers it needs are a frame stale, by construction.** The build runs
  inside the UI system pass. `MeasureScrollViews` runs after `RunAutoLayout`,
  which runs after the build; `ease_scroll` moves `scroll_offset` toward
  `scroll_target` later still. So the offset a builder reads is the one the
  PREVIOUS frame settled on, and the viewport size is last frame's too. A
  window sized exactly to what it reads shows a strip of empty list for one
  frame every time the view moves. hanabi covers it by overscanning by
  `|scroll_target - scroll_offset|`, which is exactly the distance the easing
  is about to travel -- correct, but it is a compensation every consumer has to
  independently discover, and the failure when you do not is one frame of
  blank at 60 Hz, which is precisely the kind of thing that is never seen in
  development and is reported as "flickers when I scroll fast".
- **The first frame has no viewport at all.** `viewport_size` is zero until
  something has been measured, so the window has to have a "build everything"
  fallback for frame one, which is the frame with the least budget for it.

**Why the obvious escapes do not work.**

- **Use `Overflow::Auto`.** Same clip, same children, same layout. Auto decides
  whether to clip, not what to build.
- **Build one child that draws N rows in its `on_draw_fg`.** This is #138's
  escape and it fails the same way: per-row hover (`with_custom_hover_bg`) and
  per-row hit testing are per-WIDGET, so a list drawn as one widget has to
  re-implement both by hand against a coordinate.
- **Keep the scroll view's state from last frame and window on that.** That IS
  what this does; the staleness is the whole complaint.
- **Read the rects after layout and skip the draw.** Too late: the cost being
  removed is the build, the `ComponentConfig`, the component add, and the
  layout pass -- ~4.6 heap allocations per widget per frame (#138) -- and all
  of it has happened by the time a rect exists.

**The workaround, and its cost.** Applied, and it is the largest single win on
this branch: 17.217 -> 1.533 ms, 6645 -> 496 entities, 46,508 -> 3,703
allocations a frame. The cost is that it is hanabi's, in hanabi's sidebar, and
the next list in this app -- or in any of the twenty projects vendoring this --
starts from nothing. It is also only correct because every sidebar row is the
same height: the arithmetic that makes it cheap is `offset / rowHeight`, and
hanabi has to switch the whole thing off when a search snippet makes the rows
uneven.


**Hanabi reference.** `src/ecs/sidebar_system.h` (`RowWindow row_window(Entity& parent, int limit, bool uniformHeight) const`) — The sidebar implements its own scroll-window calculation from stale viewport/offset and overscan. `src/ecs/sidebar_system.h` (`render_row_spacer(UIContext<InputAction>& ctx, Entity& parent`) — Skipped rows are represented by spacer widgets to preserve content height. Tests: `scripts/scroll_gate.sh` (`level row_window() returns the whole list`) — The scroll gate has an intentional-failure arm for disabling row windowing. Measurement/gate: `scripts/scroll_gate.sh` (`20 sessions: 364 364 364`) — The gate records entity-count scaling for virtualized vs unwindowed lists.


**Minimal upstream fix.** A `HasVirtualList` component beside `HasScrollView`:
the consumer sets `item_count` and `item_height`, the library resolves
`[first, last)` from the offset it already owns AND the offset it is about to
ease to, hands them to a build callback, and adds the leading and trailing
extents into `content_size` itself instead of making the consumer fake them
with two invisible divs. Uniform height covers the case that actually recurs; a
`std::function<float(size_t)>` covers the rest without changing the shape.

CLASS: MISSING

---

### #171 — A widget's identity is its call-site id and nothing retires one, so a virtualized list must key rows on the SLOT, which silently re-points every per-widget state at a different row

**What was wanted.** To render rows 291 through 320 of a list this frame and
rows 288 through 317 the next, without the library accumulating an entity for
every row ever scrolled past.

**What happens.** `imm::mk()` keeps a permanent `std::map<UI_UUID, EntityID>`
and hands back the same entity for the same call site forever, and nothing
sweeps one (#115). So the id a virtualized list chooses IS its memory policy:

- **id from the row index** -- the natural spelling, and it reads as the safe
  one because the widget then "is" the row -- mints an entity per row ever
  reached. Measured on a 1600-frame sweep of a 2000-row list: **+180 live
  malloc blocks per 1000 frames**, still climbing at the end of the run,
  against -18 for the same list keyed on the slot. The virtualization is
  perfect and the leak is exactly the one it was written to remove.
- **id from the window slot** -- 0..29, re-used as the list moves under them --
  is flat, and is what hanabi ships.

The slot is right and it is not free. Everything the library keys on entity id
now belongs to a POSITION rather than to a row: `hot`/`active`, the click
listener's `down`, `with_custom_hover_bg`, drag state, and the debug name a
test or an out-of-tree driver looks the widget up by (#147). Most of the time
that is what you want -- the cursor is over a place, and the row under that
place is the row to highlight. It is not what you want when the list moves
under a held button: the press began on row 291's entity and ends on the same
entity now showing row 294, and there is nothing in the library that can tell
those apart, because as far as it is concerned nothing happened.

**Why the obvious escapes do not work.**

- **Call `clear_existing_ui_elements()` when the window moves.** It is called
  from nowhere in the library and orphans entities rather than destroying them
  (#115), so it converts a bounded map into an unbounded entity collection.
- **Hash the session id into the `mk()` id.** That is the row-index case with
  extra steps: distinct rows, distinct ids, one entity each, forever.
- **Keep the slot and re-assert the state by hand.** There is nothing to
  re-assert it against: `UIContext::hot`/`active` take an `EntityID`, and the
  row identity the app cares about is not one.

**The workaround, and its cost.** Slot-keyed ids, and a comment at the loop
saying why the drag path uses the absolute index while the widget uses the
slot. The cost is that "which row is this widget" has two answers in the same
five lines, and the compiler will never tell you when a third caller picks the
wrong one.

**POSTSCRIPT, 2026-08-25 (`perf/retire`): the sweep bounds this, and does not
remove it.** #115 is now worked around app-side -- widgets nothing has built
for 90 frames are retired -- so the first bullet's "one entity per row ever
reached, forever" is no longer true. It is worth knowing exactly what replaces
it, because "the leak is fixed" would be the wrong lesson.

Measured by putting the defect back (`rowId = base + 1 + idx`, the row index)
and running `scripts/scroll_gate.sh`:

    row-index ids, sweep off   the gate fails as documented, blocks climbing
    row-index ids, sweep on    blocks +148.8 /1000f (budget 150) -- inside,
                               but the LEVEL arm fails at 3.55x (budget 1.60):
                               1360 entities at 2000 sessions against 383 at 20
    slot ids, sweep on         1.33x, blocks -340 to +30 over four runs

The sweep turns an unbounded leak into a bounded working set, and the bound is
`grace x scroll rate` -- every row scrolled past in the last 90 frames is still
alive, which on a fast sweep of a 2000-row list is 3.5x the window. Bounded is
much better than unbounded and it is not the same as free.

**So the slot keying stays**, and the reason is now sharper than "otherwise it
leaks": an id scheme that mints an entity per row is asking the sweep to
destroy and recreate a row's worth of entities on every scroll frame, which is
allocation churn where the slot scheme has none. Everything above about the
slot being right and not free is unchanged.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The unbounded 'forever' claim is superseded by the #115 retirement sweep; slot keys still ship because row-index keys cause churn and a larger bounded working set.

**Hanabi reference.** `src/ecs/main_pane_system.h` (`Keyed on the window SLOT, never the card index.`) — Digest-card windowing deliberately keys built cards by slot rather than by absolute card index. `src/ecs/sidebar_system.h` (`The row ids run base+1 .. base+window`) — Sidebar virtualization bounds the mk id space by the visible window slots. Tests: `scripts/scroll_gate.sh` (`row ids keyed on the row INDEX`) — The scroll gate rehearses the row-index-id defect. Measurement/gate: `scripts/scroll_gate.sh` (`blocks row ids keyed on the row INDEX`) — The script records the allocation/leak signal when the slot-key workaround is removed.


**Minimal upstream fix.** Make recycling a thing the library knows about:
`mk()` taking an optional stable KEY distinct from its slot, so the library can
re-use the slot's entity while telling the consumer the key changed -- one
`bool key_changed` on the returned wrapper is enough to let a press be
cancelled and a hover re-evaluated. Retiring unbuilt entities (#115) would
solve the memory half on its own and leave this half exactly where it is.

---

### #172 — Input can only be INJECTED in a build with the e2e plugin compiled in, so the one gesture the bug report names cannot be driven in the binary a person runs

**What was wanted.** To measure the app while it scrolls, in the build that
ships. The report is a gesture: "scroll the sidebar up and down until it
broke."

**What happens.** There are two ways in, and neither is the one that is
wanted. `test_input::simulate_click` / the `scroll_wheel` e2e command live
behind `AFTER_HOURS_ENABLE_E2E_TESTING`, so they are absent from
`output/hanabi.exe` and present only in `output/hanabi_uitest.exe`. And the
uitest binary is a different build with a different loop: it runs scripts and
asserts on text, and nothing in it samples RSS, live malloc blocks or frame
CPU.

So the soak driver writes `HasScrollView::scroll_offset` and `scroll_target`
directly. That exercises the clamp, the ease, the layout and the clip -- most
of what matters -- and it skips `HandleScrollInput` entirely, along with
`HandleScrollbarDrag`, the natural-scrolling inversion, the sync-group
propagation, and whatever the OS event path does before any of them. A leak or
a per-event allocation in the wheel handler is not reachable from any
instrument this project has, and the same is true of every other input: no gate
here presses a key, opens a menu or resizes a window while anything is
watching memory.

**Why the obvious escapes do not work.**

- **Build the shipping binary with the e2e flag on.** Then it is not the
  shipping binary, which is the whole point -- and the flag pulls in the
  command runner, the pending-command component and the systems that drain it.
- **Put the soak's instrumentation into the uitest binary instead.** Two
  measured binaries then diverge silently: the thing you gate is not the thing
  you ship, which is the failure mode this file exists to avoid.
- **Post an OS event.** On macOS that is a `CGEventPost` into a headless
  process with no window server session, and it lands nowhere.

**The workaround, and its cost.** `src/util/soak.h`'s `scroll_named`, and a
paragraph in `docs/perf/GATES.md` under "What could NOT be gated" saying which
half of the scroll path is measured and which half is not. The cost is a
permanent blind spot with a known shape, which is the best available outcome
and is still a blind spot.


**Hanabi reference.** `src/util/soak.h` (`scroll_named(const char* debugName, float dy)`) — The soak driver mutates scroll state directly rather than injecting an input event. `docs/perf/STRESS.md` (`Nothing here presses a key or opens a menu`) — Perf docs explicitly keep the input-path blind spot visible. Measurement/gate: `docs/perf/STRESS.md` (`half hanabi owns`) — The stress report distinguishes measured app-owned work from unavailable OS/input/backend paths.


**Minimal upstream fix.** Split the injector from the harness. A tiny
always-compiled seam -- `input::post_synthetic(Event)`, guarded at RUNTIME by a
flag the app sets rather than at compile time by a macro -- costs an untaken
branch in the input collector and makes every gesture drivable in the shipping
binary. The scripting, the assertions and the command runner can stay behind
the existing macro; it is only the one-line "pretend a wheel turned" that needs
to be in the build a person runs.

CLASS: MISSING

---

### #190 — `TextMeasureCache` is keyed by a font's NAME, so swapping the FACE behind that name serves stale measurements with no way to know

**What was wanted.** hanabi lets the reader pick a typeface — Settings →
Standard / Hyperlegible. Applying it is the one line the library offers:

```cpp
fontMgr.load_font(afterhours::ui::UIComponent::DEFAULT_FONT, path.c_str());
```

**What happens.** Every glyph in the app changes and *nothing that any cache
is keyed by* changes with it. `TextMeasureCache::compute_hash` mixes the text,
the font NAME, the size and the spacing; the name is `DEFAULT_FONT` before and
after, the sizes are the type scale, the strings are the same strings. So the
cache goes on answering with measurements of a face that is no longer on
screen, and it is not degraded — it is confidently wrong, at a 100% hit rate.

It self-heals eventually and by accident: `end_frame` prunes entries older
than `DEFAULT_MAX_AGE` (120 frames) every `DEFAULT_PRUNE_INTERVAL` (60), so a
string that stops being asked for drops out in about two seconds. A string
asked for every frame — every visible label — is refreshed on every access and
`last_used_generation` never ages, so it never prunes. The entries that are
wrong for longest are exactly the ones on screen.

The library offers no invalidation finer than `clear()`, and no signal that
the font manager's contents moved. There is no `FontManager` callback, no
generation counter on the loaded font, and `load_font` returns void.

**Why the obvious escapes do not work.**

- **Use a different font NAME per face.** Then every widget in the app has to
  be told which name to use, and `UIStylingDefaults::set_default_font` plus
  every `with_font_name` call site becomes a switch on the reader's
  preference. The whole point of loading into `DEFAULT_FONT` is that nothing
  downstream has to know.
- **Let the age-based prune handle it.** It is a two-second window for the
  text nobody is looking at and an unbounded one for the text they are.
- **Call `clear()` from the app.** This is what hanabi does now, and it is the
  right call — but it only works because hanabi happens to know where the
  singleton lives and that the swap happened. Any app that loads a font from
  somewhere the settings screen does not own (a hot reload, a fallback face
  for a missing glyph, a DPI change that reloads at another size) has the same
  bug with nothing to hang the `clear()` on.
- **Trust that nobody swaps a font at runtime.** An accessibility typeface
  toggle is not exotic; it is the reason the feature exists.

**The workaround, and its cost.** `src/util/text_epoch.h` — a generation
counter hanabi bumps in `apply_pending_font`, plus a `tmc->clear()` for the
library's own cache. Every hanabi memo of a measurement reads the counter and
drops itself when it moves, and the check lives inside the shared cache TYPE
(`src/util/text_cache.h`) rather than at the call sites, because a call site
can forget. Four hanabi caches were stale before this: the transcript's
per-message render memo, its hug memo, the sidebar's ellipsis memo and the
line-count memo.

The honest cost is small and worth stating: on hanabi's mock fixtures a live
swap and a cold start in the other face produce frames 0.02% apart, and every
one of those pixels is a relative-time label ticking between the two captures
rather than a measurement. The bug is real, the current symptom is not
visible, and it gets worse in exactly the direction every app moves — more
measurement memoized, for longer.


**Hanabi reference.** `src/util/text_epoch.h::bump_font_epoch()` — Hanabi maintains its own font generation counter when a font name's face changes. `src/ui/font_system.cpp::apply` bumps the app epoch and clears afterhours' TextMeasureCache for family and emphasis changes. Measurement/gate: `docs/font-system-audit.md` records the selected-face advance/ink checks and fallback matrix.


**Minimal upstream fix.** A generation counter on `FontManager`, bumped by
`load_font`, and a `TextMeasureCache` that mixes it into the key or checks it
on lookup. That is two integers and turns a silent wrong answer into a cold
miss. Failing that, an `on_font_replaced` callback on `FontManager`, so an app
can invalidate the caches it owns without having to notice the swap itself.

CLASS: FOOTGUN

---

### #191 — `wrap_text` will tell you the LINES or nothing: no offsets, no count, so any consumer that needs less must reimplement the break rule

**What was wanted.** Two questions a chat transcript asks constantly, neither
of which needs the lines themselves:

1. *How many lines is this paragraph at this width?* — to place the paragraph
   after it. Filed as **#135**.
2. *How wide is the widest of them?* — to hug a bubble to its text. Filed as
   **#136**.

**What happens.** `ui::wrap_text` returns `std::vector<std::string>`, and
`detail::wrap_text_to_width` builds it by joining the runs of every line into
a fresh string. There is no overload that returns a count, no overload that
returns offsets into the input, and no way to hand in a sink. For (1) the
caller takes `.size()` and drops the vector; for (2) it iterates the vector,
measures each string, keeps a float, and drops the vector.

Per word, the wrapper builds the candidate line TWICE — once as `candidate`
for the accepted branch and once as `seg` inside `measure_candidate` — so a
paragraph of N words costs 2N string constructions of average length N/2
before the result vector exists at all. On hanabi's 120-message fixture,
standing still, that was 61.8 wraps and ~3.2 KB of text per frame, and the
whole output was one integer and one float per paragraph.

This is #135 seen from one step further out. #135 asks for a counting
overload; that alone would not have helped the hug, which needs the extent of
each line. What is missing is any form of the answer smaller than "the lines".

**Why the obvious escapes do not work.**

- **`measure_text_wrapped`.** It calls `wrap_text_to_width` and then measures
  each resulting line AGAIN (`detail::measure_wrapped`), so it is strictly
  more work than doing it yourself, and its `WrappedTextMetrics` gives the
  overall extent rather than the widest LINE — which for a hug is not the same
  number when the last line is short.
- **Reserve the vector, or move the strings out.** The allocation is not the
  return value; it is the 2N candidates built inside the loop.
- **Wrap once and cache the lines.** That is holding every wrapped line of
  every message in memory to avoid rebuilding them, which trades the gap for
  #136's memory problem.
- **Reimplement it.** Which is what hanabi did — and it is the escape that
  works, so the gap is about what it costs. `src/util/wrap_count.h` restates
  the break rule over byte offsets: hard-newline split, space/non-space
  chunking, greedy accept with the first word of a line taken unmeasured,
  pending whitespace kept when the word fits and eaten by a break, trailing
  whitespace kept on the last line of a source line. Every one of those is a
  detail that can drift from upstream silently and only shows up as a message
  clipped by one line.

**The workaround, and its cost.** ~230 lines of counter plus a 200-line
differential test that compares BOTH forms against
`ui::detail::wrap_text_to_width` itself — 9,200 (string, width) pairs per
metric for the count, 3,082 wraps compared line for line for the spans. The
test is the whole safety story: the two implementations share no code, so
nothing but a differential check can notice upstream changing a break rule.
It has already earned it, catching two whitespace details during
development (`span 0 is "a b" but the line is "a b "`).

It bought: 474 → 5.6 text measurements per idle frame, 9,508 → 5,942
allocations per frame, and 134 fewer allocations per bubble hug. Restating a
vendored algorithm to get a cheaper form of its answer is a bad trade that
was worth making.


**Hanabi reference.** `src/util/wrap_count.h::wrapped_line_count_linear` — Hanabi restates the wrap algorithm over offsets/counts instead of materializing every line string. Tests: `tests/unit/test_wrap_count.cpp` (`vendor_lines(const std::string& s, float w`) — Differential coverage compares the counters against afterhours' wrap_text_to_width. Measurement/gate: `docs/perf/TEXT.md` (`text measurements / frame | 492.6 | **5.3**`) — Text perf docs record the before/after call and allocation reductions from the text work.


**Minimal upstream fix.** One more overload beside `wrap_text`, sharing the
same loop so it cannot disagree:

```cpp
// Byte ranges of each wrapped line, into `text`. No allocation per line.
void wrap_text_spans(const std::string& text, float max_width,
                     const std::string& font, float size,
                     std::vector<std::pair<size_t, size_t>>& out);
```

A count is then `out.size()` and #135 is closed too. Separately, and worth
almost as much on its own: build the candidate once instead of twice inside
`wrap_runs_to_width`.

CLASS: PERFORMANCE

---

### #192 — `dump_ui` is fully implemented, is not registered, and reports itself as a typo

**What was wanted.** To find which widget's width changed when the font
changed — the geometry, not the pixels. `ui_commands.h` has exactly the right
thing: `HandleDumpUICommand`, ~100 lines, walks the tree and emits XML with
every element's debug name, rect and text, with optional subtree scoping.

**What happens.** `register_ui_commands` does not register it. Sixteen other
handlers in the same function are registered; this one is defined and never
mentioned again. Its argument shape is also absent from the runner's
`single_arg_commands` / `two_arg_commands` tables, so even if it were
registered the parser would not hand it the name it requires.

So a script containing `dump_ui` fails with:

```
[E2E ERROR] dump_ui (line 3): Unknown command: 'dump_ui'. Either a typo, or
its handler was registered after register_unknown_handler()/
register_all_handlers() -- custom handlers must come before those.
```

which is a message about the CONSUMER's registration order, for a command the
consumer never had the chance to register. Everything in it is true and all of
it points the wrong way: the reader checks their own `register_*` ordering,
finds it correct, and concludes they mistyped a command that does not exist —
when it does exist, in the file they are looking at.

This is the same shape as #86 and #117 (a harness that cannot report where
anything landed) with an extra turn of the knife: the capability is written,
it works, and it is unreachable.

**Why the obvious escapes do not work.**

- **Register it from the app.** `HandleDumpUICommand` is public, so this is
  possible — but the parser still will not give it an argument, so it fails on
  `has_args(1)`. Both halves have to be worked around and one of them is
  inside the runner's parse tables.
- **`assert_ui <name> w=<wrong value>` and read the error.** This is what
  hanabi does. It reports the real width in the failure message, so it is a
  one-widget-at-a-time `dump_ui` that requires knowing the debug name in
  advance and makes the script fail on purpose. Finding which of ~200 widgets
  moved this way is not a search, it is a guess.
- **Screenshot and diff.** Gives pixels, not names, and is exactly the tool
  #86 already records as insufficient.

**The workaround, and its cost.** None applied; the investigation was done
with `assert_ui` probes against guessed names, and the question ("which
widget's geometry depends on the face?") went unanswered. It is a detour of
maybe forty minutes for anyone who reads `ui_commands.h`, sees the command,
and believes it is available.


**RESOLVED UPSTREAM 2026-08-29.** Afterhours `2caf525` registers `dump_ui`; Hanabi consumes it directly from the `fc4d625` pin and no longer carries the local diagnostics patch.

**Hanabi reference.** `tests/ui/composer_model_picker.e2e` and targeted debugging scripts can invoke `dump_ui` through the ordinary upstream command registry.


**Minimal upstream fix.** Register `HandleDumpUICommand` in
`register_ui_commands`. Separately, the unknown-command message should not assert a cause it
cannot know — "no handler consumed 'dump_ui'" is both shorter and true.

CLASS: TEDIOUS

---

### #160 — A component is the only per-entity storage, and it costs two cache misses to write four bytes

**What was wanted.** To write one 32-bit frame number onto a widget, once per
widget per frame, at the moment the library hands it back. This is the
mechanical heart of retiring widgets (#115): the stamp IS the fix, so its cost
is the fix's cost.

**What happens.** The ECS's only per-entity storage is a component, and
`Entity` holds `std::array<std::unique_ptr<BaseComponent>, 128>` INLINE --
a kilobyte of pointers in every entity, whatever it actually carries. So
`entity.addComponentIfMissing<BuiltAt>().epoch = n` is:

  * a bitset test in the entity header,
  * a load from `componentArray[id]`, which for a late-registered component id
    is ~500 bytes past the header and therefore a different cache line,
  * a dereference of that `unique_ptr` into a separately allocated 16-byte
    object -- a second miss,
  * and, on both the `has` and the `get`, a function-local-static guard check
    inside `components::get_type_id<T>()`.

Measured on hanabi, idle, 2000-session catalog, `scripts/perf_ab.sh`
interleaved, median of 5 runs of 800 frames, against the same binary without
the stamp:

    component (addComponentIfMissing)   1.310 -> 1.418   +0.108 ms/frame
    dense vector, EntityID -> unsigned  1.309 -> 1.339   +0.030 ms/frame

**3.6x, for the same four bytes.** At ~435 widgets a frame that is 250 ns per
widget for a component and 70 ns for the vector; the difference is the cache
lines, and it is why hanabi's stamp lives in a `std::vector<unsigned>` indexed
by EntityID rather than in the obvious ECS-shaped place.

**Why the obvious escapes do not work.**

- **Register the component early so its id is small.** Component ids come from
  a counter in order of first use; an app does not control the order, and
  "small id" only buys the first cache line -- the `unique_ptr` indirection is
  still there.
- **Use `Entity::entity_type`.** There IS a spare `int` in the entity header,
  four bytes from `id`, and nothing in afterhours reads it (only
  `snapshot.h` copies it). Writing an app's frame stamp into it would be free.
  It is also a public field with a name that claims a meaning, so an app that
  hijacks it is one library release from a silent collision.
- **Use tags.** `TagBitset tags` sits AFTER the kilobyte array, so it is the
  same miss, and a bitset cannot hold a frame number anyway.
- **Keep the side table but key it by pointer.** Worse: a hash lookup per
  widget per frame instead of an indexed load.

**The workaround, and its cost.** `src/ui/widget_epoch.h` keeps
`std::vector<unsigned> g_stamps` indexed by EntityID -- 4 bytes per live id,
contiguous, L1-resident at hanabi's sizes. The cost is that it is NOT tied to
the entity's lifetime the way a component is: a stamp has to be cleared by hand
when its entity is retired, and if an entity ever died by some path other than
the app's own sweep, its stale stamp would be inherited by whatever EntityID
recycling handed the id to next. hanabi closes that by sweeping the `mk` map
rather than the entity collection -- the map only contains ids the app itself
owns right now -- but that is a second piece of reasoning bought with the
performance.


**Hanabi reference.** Hanabi-owned performance finding: `src/ui/widget_epoch.h` (`inline std::vector<unsigned> g_stamps`) — Hanabi uses a dense EntityID-indexed vector instead of a component for the hot frame stamp. Tests: `tests/unit/test_widget_retire.cpp` (`src/ui/widget_epoch.h puts hanabi's mk in front of afterhours`) — Unit coverage exercises the stamped mk wrapper and retirement state. Measurement/gate: `docs/perf/RETIRE.md` (`g_stamps[entity.id] = frame;`) — Retirement perf docs record the side-table stamp cost that motivated the design.


**Minimal upstream fix.** A documented per-entity user word: `uint64_t
Entity::user_data` (or a small `std::array<uint32_t, 2>`) in the header, next
to `id` and `entity_type`, reserved for the app and never read by the library.
Free to write, dies with the entity, no component id burned. Failing that: a
`components::reserve_type_id<T>()` so an app can place a hot component in a low
slot, plus storing small trivially-copyable components inline instead of behind
a `unique_ptr`.

CLASS: TEDIOUS

### #161 — When a scripted assertion fails, the harness truncates the evidence to 200 characters and the command that would show the rest is not registered

**What was wanted.** To find out why `expect_text "RECENT"` failed: what WAS on
screen at that moment.

**What happens.** The timeout message prints the visible-text registry through
`{:.200}`, so on any real screen you get the first dozen labels and nothing
else. Every failure in this session printed the same truncated sidebar --
identical bytes for four completely different failures -- so the message tells
you a test failed and nothing at all about why:

    [TIMEOUT] expect_text (line 56): Text not found: 'RECENT'. Visible:
      | VIEWS |   |   | Home | 9 |   | Settings |   | Blocked | 6 | ...

`dump_ui` looks like the answer -- the command exists in `ui_commands.h`, and
`dump_ui_node` builds a full tree with names, rects and labels. Putting it in a
script gets:

    [E2E ERROR] dump_ui (line 5): Unknown command: 'dump_ui'. Either a typo,
    or its handler was registered after register_unknown_handler() /
    register_all_handlers()

so the diagnostic exists and cannot be reached from a script.

Three wrong guesses cost a rebuild-and-rerun cycle each, and every one of them
would have been answered instantly by a list of what rendered: a label that
does not exist (`"Send"`), a click on the text inside a row rather than the row
that carries the listener (`sv_label` vs `smart_item`), and a section header
that is uppercased at the call site (`"Recent"` is drawn as `"RECENT"`).

**Why the obvious escapes do not work.**

- **Read the log.** It is the truncated string; that IS the log.
- **Take a screenshot and look.** `screenshot` works, and a PNG answers "what
  is on screen" for a human eye, not "what text does the registry hold" -- and
  the registry is what the assertion reads. Drawn text (`on_draw_fg`) never
  reaches it at all, which is exactly the kind of thing you are trying to find
  out.
- **Print it from the app.** Means building a debug label per question, which
  is the (real, useful) pattern the row / snippet / widget audits already use
  -- but those are for a permanent claim, not for finding out why today's
  script does not match.

**The workaround, and its cost.** Guess, rebuild, rerun. Three cycles here at
about a minute each, for three facts that were all sitting in a structure the
harness already builds.


**RESOLVED UPSTREAM 2026-08-29.** Afterhours `2caf525` removes the 200-character cap and registers the full tree dump. Hanabi consumes both behaviors directly from the `fc4d625` pin.

**Hanabi reference.** `scripts/verify_vendor_patches.py` now verifies only the four gaps that remain unlanded; scripted UI failures and `dump_ui` use upstream diagnostics.


**Minimal upstream fix.** Drop the limit and let the reader scroll. Better still, print the registry on failure the way
`dump_ui` prints the tree -- one label per line, so a diff of expected against
actual is readable.

CLASS: TEDIOUS

### #162 — An app can own the lifetime of the widgets IT built, and there is no way to see the ones the library built for itself

**What was wanted.** After #115 was worked around app-side, a complete answer
to "what is in the UI collection and who owns it".

**What happens.** hanabi's sweep can only retire entities that came through its
own `mk`, because that is the only set it can enumerate (`existing_ui_elements`
maps call-site hash to EntityID, and nothing else in the collection is
addressable except by walking it and guessing from `UIComponentDebug::name`).
Everything the library creates for itself is invisible to that: the UI root,
`HandleDragGroupsPreLayout`'s spacer, the drag overlay, anything a future
version adds. hanabi's soak census reports them as a separate column:

    [soak] widgets: 205 live, 196 built this frame, 0 stale, 9 unstamped

Nine is not a problem. The fact that the number is unbounded-by-contract is:
an app that has taken responsibility for widget lifetime has taken it for a
subset it cannot name, and the only way it knows the subset is small is by
counting.

**Why the obvious escapes do not work.**

- **Walk the collection and treat anything unstamped as the library's.** That
  is exactly what the census does, and it is a definition by exclusion -- it
  cannot tell a library entity from an app entity created outside `mk`, and it
  cannot tell either from an entity a test spawned.
- **Match on `UIComponentDebug::name`.** Matching on a debug string, which
  #115 already rejected for the same reason.
- **Retire them too.** They are load-bearing: the UI root is permanent, the
  drag spacer is owned by a system that also destroys it. An app cannot know
  which is which.

**The workaround, and its cost.** Count them and report the count next to
everything else, so the blind spot is visible rather than assumed
(`src/ui/widget_epoch.h`, `Tally::unstamped`). The cost is that "we hold what
we draw" is true of hanabi's widgets and silently approximate overall.


**Hanabi reference.** `src/ui/widget_epoch.h` (`size_t unstamped = 0;`) — The tally distinguishes widgets made through hanabi mk from unstamped library-owned entities. `src/util/soak.h` (`%zu unstamped (library's own)`) — The soak census reports unstamped library-owned entities next to live/built/stale counts. Measurement/gate: `docs/perf/RETIRE.md` (`grep '\[soak\] widgets:'`) — The retire docs show how to reproduce the widget census that exposes the unstamped column.


**Minimal upstream fix.** Tag them. A `LibraryOwned` marker component (or a
reserved tag) on every entity the UI plugin creates for its own use, plus
`ui::library_owned_entities()`. Then an app can say "everything that is neither
mine nor the library's is a bug" instead of "nine, last time I looked".

CLASS: TEDIOUS

### #163 — A scroll view's offset is clamped against a content size measured from children that are not there, so leaving a screen resets it to the top

**What was wanted.** To leave a screen scrolled halfway down, look at something
else, come back, and still be halfway down. Every list app does this.

**What happens.** `MeasureScrollViews` runs once per frame on EVERY entity
carrying `HasScrollView`, computes `content_size` by summing `cmp.children`,
and calls `clamp_scroll()`. `ClearUIComponentChildren` empties every widget's
children list at the top of every frame, and only the screen being BUILT
refills it. So for a pane that is not being built, the scroll view is measured
against zero children:

    content_size.y = 0
    max_scroll_y   = max(0, 0 - viewport) = 0
    scroll_offset.y = clamp(offset, 0, 0) = 0

One frame off-screen and the reader's position is gone -- not lost on the way
back, destroyed immediately, by the measuring pass.

Measured in hanabi (scripted, 60-session catalog): Home scrolled until its
first section header sat at **y=-354**, then a trip to a thread and back, and
the header is at **y=123** -- the top of the list.

This was found while fixing #115, on the assumption that RETIRING a scroll view
would be what lost the position. It is not. The same script reads y=-354 then
y=123 with retirement on and with `HANABI_RETIRE=0`, byte for byte: the
position was already gone before anything was destroyed. A carve-out that kept
scroll views alive through the sweep was written, measured, and deleted --
keeping the entity keeps the field, and the field is overwritten with 0 by the
next frame's measure.

**Why the obvious escapes do not work.**

- **Keep the entity alive.** Done, measured, no effect. The offset is a live
  field being recomputed, not a value being lost with the entity.
- **Restore the offset when the screen comes back.** The app would have to
  notice the return, which means tracking per-pane "was I built last frame" --
  the same bookkeeping #115 already forces -- and then write the offset back
  before `MeasureScrollViews` runs but after the children exist, which is
  inside the library's post-update bridge. There is no hook there.
- **Stop the pane from being measured while it is away.** Nothing marks a
  subtree as "not participating this frame"; `should_hide` is a render flag and
  the measure system does not consult it.
- **Keep building the screen off-screen so it keeps its children.** That is
  exactly the cost #115 is about.

**The workaround, and its cost.** None in hanabi -- the behaviour predates the
retirement work and is out of its scope. It is filed because the mechanism is
now known exactly, and because anyone who tries to fix "coming back to a screen
loses your place" will otherwise look at the widget lifetime, which is the
wrong place: the culprit is a measurement of an empty tree.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Do not clamp against a content size measured from
zero children. Either skip the measure entirely when `cmp.children.empty()` (a
scroll view with no children this frame is not a scroll view whose content
shrank, it is one that was not built), or keep `scroll_offset` and clamp only
the RENDERED offset, so the stored intent survives a frame in which the tree
was not built.

CLASS: WORKAROUND

---

### #210 — Every GPU object comes out of a fixed pool the consumer cannot size, and the one that runs out FIRST fails silently

**What was wanted.** To cache decoded images without a bound the app cannot
see, and to be told when it hit one.

**What happens.** sokol allocates every GPU object from fixed-size pools, set
once in `sg_desc` at `sg_setup`. afterhours' backend calls `sg_setup` with a
default-constructed `sg_desc` (`backends/sokol/backend.h:194` and `:791`) and
offers no hook — not a field on `graphics::Config`, not a callback, not an
overload — so every consumer gets sokol's defaults and cannot raise them:

    images 128 | samplers 64 | views 256 | pipelines 64 | shaders 32

`load_texture` makes one image, one view **and one sampler** per texture, so
the **sampler pool is the binding constraint at 64** — half the image pool,
and the number nothing in the API mentions.

Measured in a process doing exactly what hanabi's launch does (`graphics::init`
at 1180x949 plus four font faces): **the sampler pool ran out after 61 loads
and the image pool after 124.** Three sampler slots are gone before the app
draws anything.

**The part that makes this a footgun rather than a limit.**
`metal_texture_detail::load_texture_from_pixels` checks `sg_make_image` and
`sg_make_view` — carefully, with a comment about not leaking the view when the
image fails — and does **not** check `make_sampler_for_filter`. So between the
61st texture and the 124th it returns:

    TextureType{ width = <the file's>, height = <the file's>,
                 img_id = valid, view_id = valid, sampler_id = 0 }

Every "did this load?" test a consumer can write reads that as success —
hanabi's `inline_image::available` does, its composer chip does, its
`bubble_height` image term does — and the texture cannot be sampled. Sixty
textures of silent wrongness sit between the first pool running out and the
second one reporting itself honestly. Past 124, `sg_make_image` fails, the
existing check fires, and the failure is finally visible.

Note also what a leak looks like from outside: a texture leaked every frame
does **not** grow without bound. hanabi's soak probe measured it growing to
264,048 KB and then sitting flat to the kilobyte for 800 frames, because the
image pool was full and every further allocation failed. A slope-based leak
detector sees a texture leak for about two seconds and then goes green.

**Why the obvious escapes do not work.**

- **Call `sg_setup` yourself with bigger pools** — `graphics::init` calls it,
  the consumer's first line of graphics is `graphics::init`, and calling
  `sg_setup` twice is undefined. There is no "configure then init" split.
- **Query the pool and stop before it** — `sg_query_desc()` would answer, and
  it answers about the *configuration*, not the *occupancy*. There is no
  `sg_query_samplers_in_use()`, and the consumer cannot count the ones
  afterhours, fontstash and sgl took for themselves.
- **Check `sampler_id != 0` in the consumer** — this is the workaround below.
  It is one line and it is correct, and it requires knowing that a texture can
  come back valid-looking and unsamplable, which is exactly what the API's
  shape says cannot happen.
- **Share one sampler across every texture** — right, and not available:
  `TextureType` owns its sampler id, `load_texture` makes a new one per call,
  and there is no entry point that takes an existing sampler.

**The workaround, and its cost.** hanabi caps its own texture cache at 32
entries — sokol's 64 samplers, less 16 reserved for the app's own atlases and
render targets, halved again for headroom (`src/util/texture_budget.h`, with a
`static_assert` tying the two numbers together) — and treats
`sampler_id == 0` as a failed load at the single seam every texture in the app
comes through, destroying the orphaned image and view and counting the event
(`src/ui/decode_to_fit.h`). The cap was **512** before this was understood,
which is eight times what the GPU can represent, and the byte budget in front
of it could not help: bounding BYTES does not bound OBJECTS, and the way to
hold 512 textures is for them to be small. Measured, 80 96x96 avatars through
the real app: **80 entries held, 20 of them unsamplable, and every "is this
loaded?" test said yes.**

The cost of the workaround is a texture cache four times smaller than the
byte budget would allow, sized by a constant from another library's internals
that nothing will tell us if it changes.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's 'single seam every texture' claim was stale; current source also checks the icon atlas and invalid blend pipeline path.

**Hanabi reference.** `src/util/texture_budget.h` (`kDefaultMaxEntries = 32`) — Hanabi caps cached textures below the measured sampler-pool ceiling. `src/ui/decode_to_fit.h` (`reject_if_unsamplable(TextureType& tex)`) — Texture loads are rejected if afterhours returns an image/view with sampler_id 0. Tests: `tests/unit/test_texture_budget.cpp::test_the_budget_bounds_what_the_device_holds` — The cache budget policy is covered independently of GPU resources. Proof patch: `vendor_patches/210-reject-unsamplable-textures.patch`; `tests/vendor_probes/source_contract_probe.cpp` verifies sampler validation plus sampler/view/image cleanup before return, and `tests/vendor_probes/sokol_backend_smoke.mm` compiles the patched backend through `make verify-vendor-patches`. Pool sizing is deliberately deferred in `vendor_patches/README.md`.


**Minimal upstream fix.** Two things, neither large. (a) Check the sampler in
`load_texture_from_pixels` the way the image and the view are already checked,
and return an empty `TextureType` — three lines, and it converts sixty
textures of silent wrongness into an honest failure every consumer already
handles. (b) Put the pool sizes on `graphics::Config`, defaulted to sokol's,
so an app that wants a hundred thumbnails can ask for them.

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): the workaround covered the
texture cache and missed two other consumers in the same app.** The entry says
hanabi "treats `sampler_id == 0` as a failed load at the single seam every
texture in the app comes through". That was true of every texture the CACHE
loads and not true of the app. `src/ui/icons.h` does not go through that seam:
it calls `afterhours::load_texture` directly for `icons.png` and accepted the
result on `tex.width > 0.0f && tex.height > 0.0f` — which is verbatim the test
this entry says cannot be trusted. The same file also called
`sgl_make_pipeline` for its alpha-blend pipeline and set `ready = true`
whatever came back, and the pipeline pool is 64 under the same defaults.

Neither could plausibly fire: both happen once per process, at pre-warm, when
three sampler slots are gone and nothing else has run. "Could not plausibly" is
precisely the assumption this gap is about — the texture cap was 512 for months
on reasoning of the same shape — so both now check. The icon atlas routes
through `decode_to_fit::detail::reject_if_unsamplable`, and the pipeline is
accepted only on `id != SG_INVALID_ID`, falling back to sgl's default (icons
blit unblended, which is visibly wrong rather than a per-draw validation
storm).

Audited at the same time and clear: `src/util/prewarm.h` creates no GPU object
of its own — it forces those two to happen earlier, which is its whole point —
and no other file under `src/` reaches `sg_make_*`, `sgl_make_pipeline` or
`load_texture`.

---

### #230 — `UIContext::mouse.pos` is NaN until the first mouse event, and every hit test is run against it

**What was wanted.** A hit test over a byte range inside a wrapped label
(hanabi's tracker-id links; the same shape as gap #51). It runs from the UI
build, every frame, for every label that carries a link, because that is also
where the pointer cursor comes from.

**What happens.** Before any mouse event has been delivered — the whole of the
launch, the whole of a scripted run's settle, and every frame of a headless
render — `ctx.mouse.pos` is `(nan, nan)`. Instrumented over twenty scripted
runs of `tests/ui/tracker_links.e2e`:

```
866  [link] miss id=D948120  point=(nan,nan)  rect=(432.0,140.5 46.0x13.0)
520  [link] HIT  id=D948120  point=(455.0,147.0)  rect=(432.0,140.5 46.0x13.0)
```

Two thirds of the hit tests in a scripted run are against NaN.

**Why it is not merely ugly.** NaN compares false against everything, so
`px >= r.x && px <= r.x + r.width` is false and the miss is silent and
correct-looking. Any hit test written the other way round — an exclusion test,
`if (outside) return;`, or anything that treats "not inside" as "inside
something else" — inverts, and it inverts only on the frames before the user
has moved the mouse, which is the set of frames nobody tests. It also means a
profile of the hit path counts work that can never produce an answer.

**The workaround.** None needed in hanabi, because the comparison happens to
be the safe way round. The cost was diagnostic: `HANABI_LINK_AUDIT=1`
(src/ui/link_detect.h) prints the point with the rect, and the NaN is only
visible because that diagnostic prints misses as well as hits.


**Hanabi reference.** Negative result: `src/ui/link_detect.h` (`const bool in = px >= r.x && px <= r.x + r.width`) — Hanabi's link hit test uses comparisons for which NaN safely evaluates as not inside. `src/ui/link_detect.h::HANABI_LINK_AUDIT` — The diagnostic logs hit/miss points and rects, including NaN points. Tests: `tests/ui/tracker_links.e2e` (`HANABI_LINK_AUDIT=1`) — The tracker-link script keeps the link audit enabled for remeasurement.


**Minimal upstream fix.** Initialise `mouse.pos` to something a comparison can
be reasoned about — off-screen (`{-1, -1}`) is the conventional answer — or
give `UIContext` a `mouse.valid` flag so a caller can skip a hit test that
cannot have an answer. Either is one line and removes a class of bug that only
appears before the first mouse move.

CLASS: FOOTGUN

---

### #211 — The glyph atlas is a fixed 2048², nothing registers fontstash's overflow callback, and the symptom is `measure_text` returning a wrong number

**What was wanted.** To know whether the font atlas is bounded, and to be told
if the app ever fills it.

**What happens.** It is bounded, admirably: `backends/sokol/backend.h:104`
creates one `sfons` atlas of 2048x2048 R8 — 4 MB, once, at init — and it never
grows. Measured with the device counter: rasterising 94 glyphs at fifty sizes
from 8 to 400 pt moves `-[MTLDevice currentAllocatedSize]` by **0 KB**. A whole
category of leak simply does not exist here, and that is worth writing down.

The problem is what happens at the ceiling. fontstash raises `FONS_ATLAS_FULL`
through `stash->handleError` (`vendor/fontstash/fontstash.h:1131`), afterhours
never calls `fonsSetErrorCallback`, and the default handler is null. So the
glyph is dropped, and — this is the part that matters — **the measurement is
dropped with it**. Measuring the 94-character printable-ASCII string against
one face:

    size 144 pt   width 5933.0
    size 192 pt   width  230.0
    size 288 pt   width    0.0

No error, no log, no exception, no return code. `measure_text` is what every
wrap, every hug-to-text and every ellipsize in a consumer is computed from
(#103, #116, #135, #136 are all about it), so a string that measures zero is
laid out as absent. This is not a rendering artefact that a user squints at; it
is silent, arbitrary layout corruption whose only visible cause is that
somebody used a big font.

hanabi is not close to the ceiling — four faces x fourteen sizes x the ASCII
set, and again at 2x, 3x, 4x and 6x, all fit with a reference measurement
unchanged to the pixel. A consumer with CJK, or with a document viewer that
offers a font-size slider, is a different story, and neither would find out
from the library.

**Why the obvious escapes do not work.**

- **Register the callback yourself** — `fonsSetErrorCallback` needs the
  `FONScontext`, which lives in `metal_detail::g_fons_ctx`, a backend-private
  static with no accessor.
- **Ask how full the atlas is** — nothing reports it. There is no
  `atlas_usage()`, no glyph count, and the atlas image id is not exposed
  either, so a consumer cannot even read its dimensions back.
- **Detect it from the outside by sanity-checking widths** — this is the only
  thing available and it is guesswork: a consumer would have to measure a
  canary string every frame and compare it against a value it recorded earlier,
  which costs a measure per frame to detect a condition that should be one
  branch inside the library.
- **Ask for a bigger atlas** — 2048x2048 is hard-coded two lines above the
  `sfons_create` call, and #210 is the same complaint about sokol's pools:
  there is no `graphics::Config` field for any of it.

**The workaround, and its cost.** None taken. hanabi measured its own headroom
(above) and recorded the result rather than defending against a condition it
cannot detect. The cost is that if hanabi ever ships a font-size setting or a
non-Latin script, the first symptom will be labels laying out at zero width and
nothing in the app or the library will say why.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The original 'none taken' workaround claim is stale. Current source ships atlas_guard and atlas_gate; the residual partial-drop caveat remains.

**Hanabi reference.** `src/util/atlas_guard.h` (`probe() asks the question directly`) — Hanabi detects a full glyph atlas by probing an uncached glyph/size and checking suspicious measurements. `src/util/soak.h::text-faults` — Soak output includes atlas fault counts. Tests: `tests/unit/test_atlas_guard.cpp::test_atlas_zero_for_a_real_string_is_a_fault` — Unit tests cover zero and non-finite measurement faults. Measurement/gate: `scripts/atlas_gate.sh` (`first bad measurement`) — The gate reports the first bad measurement and detector status.


**Minimal upstream fix.** Three lines and a field. Call
`fonsSetErrorCallback` in the backend's init with a handler that `log_error`s
`FONS_ATLAS_FULL` once — that alone turns silent corruption into a message with
a name. Then put the atlas dimensions on `graphics::Config` beside the pool
sizes #210 asks for.

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): the workaround is no longer
"none", and the condition has now been REACHED and watched rather than
reasoned about.** Two corrections to the entry above, both in the direction of
it being worse than it says.

*The escape it rules out is the one that works, in a narrower form.* The entry
says detecting this from outside "is guesswork: a consumer would have to
measure a canary string every frame and compare it against a value it recorded
earlier". A canary cannot work at all, and for a reason the entry does not
give: fontstash caches a glyph on `(codepoint, isize, blur)`, so a canary's
glyphs are already resident and stay CORRECT for the whole life of a full
atlas. The canary never moves. What does work is the opposite question — ask
for a glyph the atlas has never held, at a size it has never held, and see
whether it comes back with an advance. That is one measurement, it is exact
rather than heuristic, and it answers one step BEFORE the app's own text is
affected. `hanabi::atlas::probe()` in `src/util/atlas_guard.h`.

*Measured on this machine, driving hanabi's own `theme::text_px` over the
94-glyph printable-ASCII set at climbing sizes (`hanabi.exe --atlas-stress`):*

```
  pt          width   detector
  64         2644.0   ok
  120        4836.0   FULL   <- the probe says the atlas can take no new rect
  124         622.0   fault  <- the app's own measurement is now WRONG
  136         231.0   fault
  168          40.0   fault
  172+          0.0   fault  <- terminal: every glyph dropped
  13pt reference: 538.0 -> 538.0, unchanged throughout
```

The 622.0 at 124 pt is the shape that matters, and it is worse than a zero: it
is a plausible number. Nothing about it says "wrong", it is about an eighth of
the truth, and a wrap computed from it puts eight lines' worth of words on one
line. The entry's three-row table (5933 / 230 / 0.0) is reproduced exactly in
character — a decay, not a cliff — and the reference measurement holding at
538.0 the whole way confirms why: already-resident glyphs are fine, so the
corruption arrives only for text the app has not drawn before, which is the
text a user just typed.

*What hanabi does now, and what it still cannot do.* Every measurement seam in
the app (`theme::text_px`, and the four `measure_text` callers in
`text_select.h` / `link_detect.h` / `find_highlight.h`) runs its result through
`hanabi::atlas::check`, which faults on a non-blank string measuring zero or
non-finite; a soak column probes the atlas once a bucket;
`HANABI_ATLAS_STRICT=1` aborts on the first fault; and `scripts/atlas_gate.sh`
fills the atlas on purpose inside `make test`, because a detector for a
condition nobody has ever reached is a detector nobody has ever seen work. What
it cannot do is recognise an individual PARTIAL drop inside an ordinary
measurement — 622.0 is knowably wrong only to somebody who already knows the
answer is ~5000. The probe bounds how long the condition goes unnoticed; it
does not make each poisoned number identifiable. That residue is why #350 is
filed rather than this being closed.

---

### #231 — the e2e runner's `wait_frames N` is stored as SECONDS, so it is a frame count only while the host ticks at exactly 1/60

**What was wanted.** `wait_frames 4` between a `mouse_move` and a `click`, on
a machine whose load average has been over 100 — a wait that means four frames
of app work whatever the box is doing, which is the whole reason a scripted UI
test counts frames instead of sleeping.

**What happens.** `runner.h` parses it as
`cmd.wait_seconds = frames * (1.0f / 60.0f)` and `tick(dt)` decrements
`wait_time_ -= dt`. The command is a *duration*, and it is a frame count only
because hanabi's e2e loop happens to pass a fixed `kDt = 1.0f / 60.0f`. A host
that passes a real frame delta — which is what every non-test host does, and
what `tick()`'s own doc comment calls the preferred form — turns every
`wait_frames` in every script into "wait 66 ms", and then a busy machine runs
two frames where the script asked for four.

**Why it matters here.** hanabi's scripted suite is 89 scripts of
`wait_frames`, and the fixed dt that makes them mean what they say is one
constant in `src/main.cpp` with nothing pinning it. Change it to a measured
delta to make the harness more realistic and every timing-sensitive script in
the suite becomes load-dependent at once, with no error message that says so.

**The workaround.** Keep `kDt` fixed and say why. Done, in main.cpp.


**Hanabi reference.** `src/main.cpp` (`constexpr float kDt = 1.0f / 60.0f`) — Hanabi's e2e host passes a fixed timestep, making wait_frames deterministic. Tests: `tests/ui/composer_shift_enter.e2e` (`wait_frames 8`) — Current scripts depend on wait_frames semantics under the fixed-step host.


**Minimal upstream fix.** Give `PendingCommand` a frame COUNT alongside
`wait_seconds` and have `tick` decrement whichever the command set. A frame
count is what the command is named after and it is exact under any dt.

CLASS: FOOTGUN

---

### #212 — Destroying a GPU object does not free it until the next frame, and doing enough of that without one trips an assert inside sokol

**What was wanted.** To measure the cost of a texture load: create one, destroy
it, repeat, and watch the counters.

**What happens.** The process dies:

    Assertion failed: (_sg.mtl.idpool.free_queue_top > 0),
      function _sg_mtl_alloc_pool_slot, file sokol_gfx.h, line 14937

sokol's Metal backend does not release a destroyed object immediately — it
defers to a frame boundary, so the GPU cannot still be reading it. A loop that
creates and destroys textures **with no `begin_frame` between them** therefore
never returns a slot, and sokol asserts rather than reporting anything a caller
can act on. `unload_texture` returns void and `sg_destroy_image` returns void,
so there is no signal at any layer that the resource is still held.

Nothing in afterhours says this. `unload_texture`'s neighbours in
`drawing_helpers.h` are careful about not leaking a view when an image fails,
which reads as a file that has thought about resource lifetime — and the one
lifetime fact a consumer needs is not there.

The consequences reach past the crash:

- **A cache eviction does not reclaim anything until the next frame is drawn.**
  A consumer bounding a texture cache by bytes (as hanabi does) evicts and
  immediately loads the replacement, so for the rest of that frame BOTH are
  resident and the real peak is over the budget by one entry. Nothing in the
  API lets the caller wait for, or force, the drain.
- **Any warm-up, migration or bulk reload outside the frame loop is a landmine**
  — which is a real shape: a pre-warm, a theme switch that reloads an atlas, a
  hi-DPI change that recreates every texture.
- **A probe cannot be written the obvious way.** The measurement above had to
  be restructured to draw an empty frame per iteration, which is not what it
  was trying to measure.

**Why the obvious escapes do not work.**

- **Call `sg_destroy_image` yourself and wait** — the deferred queue is
  `_sg.mtl.idpool`, private, and there is no drain entry point.
- **Draw a frame after every destroy** — this is the workaround, and it means
  the cost of freeing a texture is a whole frame.
- **Just do not destroy many at once** — "many" is `SG_NUM_INFLIGHT_FRAMES`
  worth of pool, a number the consumer cannot see (#210).

**The workaround, and its cost.** hanabi's eviction happens inside the frame
loop, where the drain follows naturally, and its one out-of-frame texture load
(the pre-warm) creates without destroying. Both are true by accident of where
the code sits rather than by design, and nothing would catch either changing.
The cost of the probe was an hour and a crash whose message names a static in
another library.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** A sentence in `unload_texture`'s comment saying the
release is deferred to the next frame, which is the whole of what a consumer
needs to not walk into this. Better, a `graphics::flush_deleted_resources()`
that drains the queue, so a bulk reload has something to call.

CLASS: SURPRISING

---

### #180 — `imm::mk()` builds a stringstream and a 200-character source-location string to hash, for every widget, every frame

**What was wanted.** To find out why hanabi allocates ~3,700 times per frame
sitting still on the Home view, and ~10,000 with a thread open.

**What happens.** `mk()` is what gives an immediate-mode widget a stable entity
across frames — it is called once per widget per frame, by construction. Its
key is derived like this (`entity_management.h:29`):

```cpp
std::stringstream pre_hash;
pre_hash << parent.id << otherID << "file: " << location.file_name() << '('
         << location.line() << ':' << location.column() << ") `"
         << location.function_name() << "`: " << '\n';
UI_UUID hash = std::hash<std::string>{}(pre_hash.str());
```

An absolute source path plus a fully expanded C++ function signature. In this
app that is routinely 200 to 400 characters — a template-heavy render function
prints its whole instantiation — pushed through a `stringbuf` a character at a
time, hashed, and destroyed.

Measured with a global `operator new` counter and a call-site table
(`HANABI_PROF_SITES=1`, `src/util/prof.h`), 2000-session catalog, Home view:
**2,589 allocations and 448 KB per frame, 47% of every allocation the process
makes.** The cost is the string, not the hash: it is `basic_stringbuf::overflow`
→ `string::push_back` → `string::__grow_by` all the way down.

**Why the obvious escapes do not work.**

- **Pass a shorter `source_location`.** The caller does not choose what
  `std::source_location::current()` contains, and the default argument is what
  makes the API pleasant.
- **Cache the hash at the call site.** A `static` per call site would be wrong
  the moment the same line runs with a different `parent` or `otherID`, which
  is the normal case for every list.
- **Skip `mk()` and reuse an entity id directly.** The map it maintains
  (`existing_ui_elements`) is what `clear_existing_ui_elements()` and the whole
  reuse contract are built on; bypassing it means reimplementing the identity
  layer.

**The workaround, and its cost.** hanabi ships its own `mk()`
(`src/ui/mk.h`, ~40 lines) that hashes the same five facts — parent id, index,
file, function, line:column — with no string at all, hashing the file and
function BY POINTER (`source_location`'s strings are static, so the pointer is
stable for the process, which is the only property a key that is never
persisted needs). It stores into afterhours' own `existing_ui_elements` and
resolves through afterhours' own collection holder, so a widget made by it is
indistinguishable from one made upstream, and the swap is one `using`
declaration reaching all 335 call sites.

Cost: 3,731 → 1,426 allocations per frame at 2000 sessions (−62%), 2,721 → 946
at 20 sessions (−65%), entity counts unchanged. And a permanent fork of a
function whose semantics every other consumer relies on — if upstream changes
what goes into the key, hanabi silently keeps the old one.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The initial 2,589/47% number is stale; current source and alloc_gate use 2,164 allocations on Home 2000 and 3,063 on thread480.

**Hanabi reference.** Hanabi-owned performance finding: `src/ui/mk.h` (`widget_key(parent.id, otherID, loc)`) — Hanabi replaces afterhours mk key construction with a field hash that avoids constructing a string. `src/ui/widget_epoch.h` (`hanabi::ui::mk(parent, otherID, location)`) — The epoch wrapper routes app widgets through the fast mk implementation. Tests: `scripts/alloc_gate.sh` (`point the app's mk wrapper back at the library's`) — The allocation gate rehearses the one-line regression back to the vendored mk. Measurement/gate: `scripts/alloc_gate.sh` (`home2000 3361.0 1450`) — Gate comments record the measured allocation delta when the wrapper uses the library's mk.


**Minimal upstream fix.** Hash the fields instead of a rendering of them:
`hash_combine(parent.id, otherID, (uintptr_t)location.file_name(),
location.line(), location.column())`. No allocation, no `<sstream>`, and the
same uniqueness — `source_location`'s pointers are static per translation
unit. Roughly the same number of lines as the version that is there.

CLASS: PERFORMANCE

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): "2,589 allocations, 47% of
every allocation" is not any arm's figure; the measured cost is 2,164 on the arm
this entry names.** `scripts/alloc_gate.sh` now rehearses exactly this by
pointing `src/ui/widget_epoch.h`'s wrapper back at `afterhours::ui::imm::mk` and
changing nothing else, which makes the delta the hash's own cost:

    arm         with hanabi's mk   with the library's   the hash costs
    home20                 827.0               2466.0            1,639
    home2000              1197.0               3361.0            2,164
    thread480             2740.0               5803.0            3,063

64% of the 2000-session Home frame, 53% of the 480-message thread. 47% is not
reachable from any pair in the tree, and 2,589 cannot be a Home-view figure at
all: the paragraph above reports 3,731 -> 1,426 on that arm, a saving of 2,305,
which cannot contain a 2,589-allocation component. The conclusion is untouched
and if anything understated.

---

### #181 — A `ComponentConfig` is copied three times on the way into a widget, so every string it carries is malloc'd three times per widget per frame

**What was wanted.** After #180, to find what the remaining per-frame
allocations on an idle Home view are.

**What happens.** They are the labels, and they are allocated once each for the
config the app builds and then again for each copy the library makes of it:

```
div(ctx, ep, ComponentConfig config = ComponentConfig())     # by value
  init_component(ctx, ep, config, ...)                       # by reference
    config = detail::overwrite_defaults(ctx, config, ...)    # BY VALUE -> copy
      config = styling_defaults.merge_with_defaults(...)     #   ComponentConfig result = config -> copy
        defaults.value().apply_overrides(result)             #   returns by value -> copy
```

Measured per widget on a Home digest card whose title is 40 characters: 4.28
allocations per frame for that one label. `ComponentConfig` carries
`std::string label`, `std::string debug_name`, `std::string font_name`,
`std::vector<TextSpan> styled_label` and two `std::function` draw callbacks —
every one of them re-allocated per copy. At 2000 sessions, Home:
**472 allocations per frame are label copies alone**, 38% of what the app has
left after #180.

This is #138 seen from the other end. #138 counted 4.6 allocations per widget
and named `ComponentConfig` as the shape of it; this is where they go.

**Why the obvious escapes do not work.**

- **Pass a shorter label.** The label is the content. A sidebar row title, a
  card title and a transcript line are all past libc++'s 22-character
  small-string buffer because they are sentences.
- **Set `is_internal`.** `overwrite_defaults` skips the merge for an internal
  config, which removes one or two of the copies — and with it the default font
  name, the default font size, and any per-component-type defaults. On a UI
  whose pixels are diffed against reference screenshots, that is not a
  performance change, it is a rendering change.
- **Build the config once and keep it.** #138's suggestion, and it does not
  help: the copies are made *inside* `init_component`, from whatever it is
  handed.
- **Memoize the label string.** hanabi does (`src/util/text_memo.h`), and it
  removes the DERIVATION, not the copies. A cached `const std::string&` handed
  to `with_label` is still copied four times.

**The workaround, and its cost.** None available in app code. hanabi removed
everything around it instead — the derivation of the strings, the identity
hash (#180), the wrap measurement — and left this. It is now the single
largest remaining allocation source on the Home view and there is no app-side
lever on it.


**POSTSCRIPT 2026-09-02 (`fix/div-move`).** One of the copies WAS app-side, and
this entry missed it. The first line of the trace above — `div(ctx, ep,
ComponentConfig config)` — takes the config by value so a caller can move into
it, but the fluent builder returns `ComponentConfig&`, so every inline
`ComponentConfig{}.with_label(...)` chain in hanabi was an lvalue and that
parameter was copy-constructed. `src/ui/div.h` moves instead and
`src/ecs/ui_imports.h` binds the ECS call sites to it, worth 89 allocations per
frame on Home at 20 sessions and 147 at 2000 (`scripts/alloc_gate.sh`,
`docs/perf/ALLOCATIONS.md`). The remaining three copies are made inside
`init_component` from whatever it is handed, and those are still upstream's:
the gap stands, at three per widget rather than four.


**Hanabi reference.** Hanabi-owned performance finding: `scripts/alloc_gate.sh` (`home20 2550.0 827.0`) — The allocation gate records the remaining per-frame allocation ceiling after surrounding reductions.


**Minimal upstream fix.** Take the config by reference through the whole init
path, or move it: `overwrite_defaults(ctx, ComponentConfig&& config, ...)` and
`merge_with_defaults` mutating `result` in place rather than copy-constructing
it. Two signatures. Alternatively `std::string_view label` with the owning copy
made once, where it is actually stored on the entity.

CLASS: PERFORMANCE

---

### #183 — The focusable set is a `std::set` cleared and refilled every frame, so every focusable widget costs a tree node malloc per frame

**What was wanted.** To account for 634 allocations per frame that
`HANABI_PROF_SITES=1` attributed to `HandleTabbing::for_each_with`
(`systems.h:943`) with a thread open, and 167 on an idle Home view.

**What happens.** `UIContext::focused_ids` is a `std::set<EntityID>`. It is
cleared once a frame and then `try_to_grab(entity.id)` inserts into it for
every widget that `can_be_focused` — which is every widget carrying a
`HasClickListener` or `HasDragListener` and rendered to screen. A `std::set`
insert is a red-black-tree node allocation, and clearing frees every node, so
the whole set is rebuilt from the allocator every frame. The container never
keeps its storage because a node-based container has none to keep.

It is ~30 bytes each and it is exactly proportional to how interactive the
screen is, which is the wrong direction for it to scale in.

**Why the obvious escapes do not work.**

- **Have fewer focusable widgets.** Partly available and worth doing on its own
  merits (below), but the ones that remain are the buttons and rows, and they
  are the point of the app.
- **Reserve it.** There is nothing to reserve on a `std::set`.
- **Replace the container from app code.** `focused_ids` is a public member of
  a component, but the system that fills it is `HandleTabbing`, registered by
  `afterhours::ui::add_singleton_components` / the plugin's system bundle, and
  the ordering contract around `try_to_grab` and `process_tabbing` is internal.

**The workaround, and its cost.** hanabi marks the widgets that were never
meant to be tab stops with `with_skip_tabbing(true)` /
`SkipWhenTabbing` — `can_be_focused` tests it before the insert, so a skipped
widget costs nothing. Two places: every transcript line (whose click listener
is an empty lambda that exists only to get hover and press plumbing for
drag-select) and every minimap mark (one per turn, all on screen at once by
definition). Worth 392 allocations per frame on the 480-message fixture,
2,795 down from 3,187, and it makes Tab reach the composer instead of walking
several hundred dots — so the workaround is an improvement in its own right,
which is the only reason it is a comfortable one.

What it does not touch is the ~430 widgets that legitimately are focusable.
Those still cost a malloc each per frame and nothing in app code can change
that.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The residue estimate is stale: the postscript corrects ~430 remaining focusable nodes to about 242. The skip-tabbing workaround remains current.

**Hanabi reference.** `src/ecs/main_pane_system.h::with_skip_tabbing(true)` — Minimap marks are intentionally removed from tabbing to avoid per-mark focusable-set allocations. `src/ecs/sidebar_system.h::with_skip_tabbing(true)` — Sidebar/transcript paths use skip-tabbing for widgets that should not be focus stops. Tests: `scripts/alloc_gate.sh` (`thread480 6687.0 2740.0`) — The allocation gate bounds the post-workaround allocation level on a thread-heavy screen. Measurement/gate: `docs/perf/ALLOCATIONS.md` (`with_skip_tabbing(true) takes them out`) — Allocation docs record the skipped-tabbing reduction.


**Minimal upstream fix.** A sorted `std::vector<EntityID>` with `clear()`
(which keeps capacity) and `push_back`, or any flat set. The access pattern is
insert-many-then-query, which is the pattern a flat container is best at. The
public shape of `focused_ids` would change; nothing else would.

CLASS: PERFORMANCE

**POSTSCRIPT, 2026-08-26 (`fix/audit-closeout`): the residue is ~242 widgets,
not ~430.** Both operands are in this entry: 634 allocations a frame from the
focusable set with a thread open, and a workaround "worth 392 allocations per
frame on the 480-message fixture, 2,795 down from 3,187". 634 - 392 = 242. The direction is unchanged --
what is left is the buttons and the rows, and they still cost a tree node each
per frame -- but the residue is a third of the frame's original tabbing cost,
not two thirds of it, which is what makes the upstream flat-set ask worth less
than the sentence above implies.

---

### #220 — A scroll view's viewport is zero until the frame after it exists, so a virtualizing consumer must build the WHOLE list once — and under #115 once is forever

**What I wanted.** To window `render_digest`'s card list the way `perf/scroll`
windowed the sidebar's rows: read `HasScrollView::viewport_size.y`, build the
cards inside it, spacer out the rest.

**What happened.** On the first frame a scroll view has been created but never
measured: `viewport_size` is `{0, 0}`, because `MeasureScrollViews` runs after
the build. There is no way to ask the library what the viewport is ABOUT to be
— the size is in the `ComponentSize` the consumer just passed in, but the
library keeps no "pending" or "requested" figure a consumer can read back, and
`percent(1.0f)` (which is what a pane naturally passes) is not resolvable
without running the layout.

So the honest fallback is "if the viewport is zero, build everything, and by
frame two there is a number to read". This is what `row_window` does in
`sidebar_system.h`, and it is what I wrote first here. It is wrong, and it is
wrong in a way that no frame time can show you.

**Because of #115, one uncapped frame is a permanent plateau.** Nothing
retires a widget, so the 2276 entities that one unmeasured frame minted are
still there at frame ten thousand. Measured at a 2000-session catalog, the
same binary, the ONLY difference being the fallback:

```
  fallback = "build everything"    widgets 2473   frame 2.91 ms
  fallback = the pane's own listH  widgets  501   frame 1.05 ms
```

The frame time was already right with the fallback in — 2.91 against the 5.58
it started at — and the widget count had not moved by one. A branch whose
whole subject is entity counts nearly shipped with the entity count unchanged,
and the thing that caught it was reading the census rather than the clock.

**The workaround, and its cost.** hanabi passes its own `listH` — the pixel
height it just asked the scroll view to be — as the viewport when the measured
one is zero. It is right to within the scroll panel's own 12 px of padding, on
one frame, which is one card. The cost is that the pane now states the
viewport height in two places (the `ComponentSize` and the window call) and
nothing checks they agree; the day someone gives the scroll a `percent()`
height instead of `pixels(listH)`, the fallback silently becomes a guess.


**RESOLVED UPSTREAM 2026-08-29.** Afterhours `2ccc38e` makes `viewport_size` optional, so an unmeasured first frame is distinct from a measured zero-sized viewport. Hanabi consumes that contract through `viewport_or_zero()` and keeps its requested-height fallback only for the explicitly unmeasured case.

**Hanabi reference.** `src/ecs/main_pane_system.h` (`viewport_or_zero().y`) and `src/ecs/sidebar_system.h` use the new optional viewport contract. `tests/ui/digest_is_windowed.e2e` and `sidebar_show_all_is_still_virtualized.e2e` cover first-frame and scrolling behavior.


**Minimal upstream fix.** Either would do it:
- `HasScrollView` keeps the size it was configured with, resolved or not, so a
  consumer can read `requested_size` on frame one instead of zero; or
- the build gets to run after a measure pass — which is #170's ask from the
  other side, and would close both.

Failing those: `viewport_size` could be `std::optional`, so "not measured yet"
is distinguishable from "measured as zero" and a consumer cannot silently
treat one as the other. That is a one-line type change and it turns this whole
entry into a compile error.

CLASS: SHARP EDGE

### #221 — `with_label` takes `const std::string&`, so every label is a heap allocation per widget per frame even when the text already exists

**What I wanted.** To hand a label text I already have — a `string_view` into
a session's preview, a string literal, a `char*` — without copying it.

**What happened.** `ComponentConfig::with_label(const std::string &lbl)` is
the only spelling, so every one of those becomes a `std::string` temporary at
the call site. In an immediate-mode UI that is once per label per widget per
frame, forever.

Measured on hanabi's digest cards with a global `operator new` counter
(`HANABI_PROF=1`): `digest.build` is **611 allocations a frame for 79 cards,
7.7 each**, on cards that carry three labels. On the whole static Blocked
screen at a 2000-session catalog the app allocates 1,826 times a frame, and
before this branch windowed the list it was 20,827.

The shape is worth naming: this is a per-WIDGET cost in a library whose whole
model is rebuilding every widget every frame, so it scales with exactly the
thing the library is optimised around.

**The workaround, and its cost.** None available in app code — the parameter
type is the API. hanabi's own `sub_line` returns a `string_view` precisely so
the pitch pass (which needs the length, not the text) can avoid this, and then
pays it anyway at the one call site that renders. That split is the workaround:
**the measurement path is allocation-free and the render path is not**, which
is fine here only because the window made the render path small.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Add `with_label(std::string_view)` and
`with_label(std::string&&)`. The config's storage is a `std::string`, so the
view overload still copies once — but into the destination rather than through
a temporary, and the rvalue overload moves. A `const char*` overload avoids a
strlen-and-copy for the many literal labels.

CLASS: TEDIOUS

### #222 — An absolutely-positioned child is still counted in its parent's flex flow, so an overlay inside a sized column reports an overflow it does not cause

**What I wanted.** A diagnostic label pinned to a corner of the main pane —
the same thing `sidebar_system.h` does for `HANABI_ROW_AUDIT` — without it
taking part in the column's layout.

**What happened.** `with_absolute_position()` plus `with_translate(x, y)` on a
child of a flex column still contributes its height to the parent's flow. The
parent (a `percent(1.0f)` column already full to the pixel) then warns, once
per frame, twice:

```
[WARN] Layout wrap: 'digest_audit' in parent 'main_content' - NoWrap set but
       would overflow (child_size=14.0, offset=595.0, container=595.0)
[WARN] Layout overflow: 'digest_audit' extends outside parent 'main_content'
       bounds (child_rel=[0.0,595.0], ..., parent_size=[820.0,595.0])
```

Two frames of that is 120 lines of log, which is enough to bury whatever the
diagnostic was for.

The sidebar's equivalent does not warn, and the difference is instructive:
`sb_row_audit`'s parent is itself an absolutely-positioned uiRoot child, and
the docs' "absolute+translate is SCREEN-space" rule is only stated for uiRoot
children. What an absolute child of a *flowed* parent does is not documented
and, from this, is not what the name says.

**The workaround, and its cost.** Give the label real height and let it flow:
the pane subtracts 16 px from the list when the audit is on. It works, and it
means the diagnostic MOVES the thing it is diagnosing — the viewport shrinks
by a row, so the card count the label reports is the count for a slightly
shorter pane. Fine for a UI test that pins its own window size; wrong for a
gate, which is why hanabi's digest gate reads a log line instead and the label
is only for the scripted suite.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`digest_audit(UIContext<InputAction>& ctx, Entity& parent)`) — The digest audit label is rendered as a normal sized row rather than as an absolute overlay. `src/util/bounds_audit.h` (`if (c.absolute)`) — The bounds audit deliberately skips absolute children when checking parent containment. Tests: `scripts/bounds_gate.sh` (`HANABI_BOUNDS_AUDIT=1`) — The containment gate runs the bounds audit across reachable states.


**Minimal upstream fix.** Make `with_absolute_position()` remove the child
from the parent's flow accounting — which is what "absolute" means in every
other layout system, and is presumably the intent, since the position is
already taken from the translate rather than the flow.

CLASS: SHARP EDGE

### #223 — The e2e runner's retry budget is named in seconds and fed by the host's `dt`, so whether the suite is reproducible is the HOST's decision and nothing says so

**What I wanted.** To tell whether a UI-suite failure was mine.

**What happened, and the first answer was wrong.**
`tests/ui/select_word_and_line.e2e` failed on my branch. The box's load average
was 123 at the time — another agent had leaked several dozen runaway processes
— so I looked at the runner, found that every assertion's retry budget is a
field called `wait_seconds`, and concluded the suite was failing correct
scripts under load. That is written up in the first version of this entry and
it is **wrong**, and the way it was caught is the only reason it is worth an
entry at all: I built the merge-base in a separate worktree and ran the suite
on BOTH, on a quiet box (load 6.6).

```
  base   (main @ ef29c1a)   86 passed, 1 failed — select_word_and_line
  branch                    88 passed, 1 failed — select_word_and_line
```

Identical, quiet, reproducible. `select_word_and_line` is simply broken on
main, the way `tracker_links.e2e` already is. Not mine, and not the load.

**The gap that is actually there.** `PendingE2ECommand::wait_seconds` is
decremented by whatever `dt` the host hands `runner.tick(dt)`:

```cpp
wait_time_ -= dt;          // runner.h
elapsed_time_ += dt;
```

hanabi's host passes `constexpr float kDt = 1.0f / 60.0f` — a FIXED step — so
in this app the budgets are frames wearing a seconds-shaped name, and the suite
is reproducible on any machine. That is correct, and it is correct by a choice
made in `src/main.cpp` that the library neither requires nor mentions. A host
that passes real elapsed time — the obvious thing to write, and exactly what a
field called `wait_seconds` invites — gets a suite whose assertions expire in
wall clock while `wait_frames` between them counts frames, and correct scripts
then fail under load with `Text not found`, which is indistinguishable from a
genuine regression.

`stress.h` in this same repository argues the general case in its header,
quoted from Puffin: *"Every scenario terminates on a fixed COUNT, never a
duration. A count is the same amount of work on a fast machine and a slow
one."* The e2e runner leaves that decision to the host and does not say it is
being made.

**The workaround, and its cost.** hanabi already passes a fixed dt, so nothing
to work around — but the shared-box cost is real anyway: `run_ui_tests.sh`
kills a script after **60 seconds of WALL clock** (`TIMEOUT=60`, rc 124), which
is genuinely load-sensitive and is what took out an unrelated script during the
load spike. And establishing that a failure is not yours costs a worktree, a
submodule checkout and a full compile, every time.


**Hanabi reference.** `src/main.cpp::runner.tick(kDt)` — The e2e host passes the runner a fixed timestep, making retry budgets deterministic in hanabi. `scripts/retire_gate.sh` (`No milliseconds anywhere`) — Gate scripts avoid wall-clock timing where possible because the shared machine is noisy. Measurement/gate: `scripts/scroll_gate.sh` (`Both scenarios terminate on a fixed frame COUNT`) — Scroll-gate comments record the fixed-count strategy used to avoid load-sensitive verdicts.


**Minimal upstream fix.** Rename the field to `wait_frames_` and count ticks,
or document at `tick(dt)` that the runner's determinism is the caller's
responsibility and that a fixed step is what the harness expects. The rename is
better: it makes the right thing the only thing.

CLASS: SHARP EDGE

### #224 — Nothing can tell you how tall a child WOULD be, so windowing a variable-height list means re-implementing the box model in app code

**What I wanted.** To window a list of cards that are 34 px tall or 52
depending on their content. The sidebar's window (#170's workaround) is a
division, because its rows are a fixed height; a card list needs a prefix sum
over the real heights, and to get those it has to know a card's height without
building the card.

**What happened.** There is no such query. `UIComponent` carries the computed
rect, but only after the layout has run on a widget that exists; nothing takes
a `ComponentConfig` and returns the height it would resolve to. So the app has
to restate the box model:

```cpp
inline float card_pitch(std::string_view sub) {
    return kCardMarginTop + card_body_height(sub) + kCardMarginBot;
}
```

That is margin-top plus body plus margin-bottom, which is a claim about how
afterhours sums a column — that adjacent margins do NOT collapse, in
particular, which is true here and is the opposite of CSS. If the library ever
collapses them, or adds a gap, or rounds, every spacer in every windowed list
in the app is silently the wrong size and every card below the fold lands at
the wrong y. Nothing would fail to compile and nothing would look wrong on a
screen scrolled to the top.

**The workaround, and its cost.** One function, called by both the window and
the built card, so the app's two copies cannot disagree with each other — and
a unit test (`tests/unit/test_digest_layout.cpp`) that pins the pitch to 42
and 60 for the two card shapes. Neither can catch the library changing its
mind; they can only make the app's own two answers the same answer.


**Hanabi reference.** `src/ecs/digest_layout.h` (`inline float card_pitch(std::string_view sub)`) — Hanabi centralizes the hand-rolled card pitch used by both windowing and rendering. `src/ecs/main_pane_system.h::pitches_.push_back(digest::card_pitch` — The digest window computes card heights up front rather than asking the library to measure unbuilt children. Tests: `tests/unit/test_digest_layout.cpp` (`CHECK(dg::card_pitch("3h") == 42.0f)`) — Unit coverage pins the pitch for sparse and rich card shapes.


**Minimal upstream fix.** A `measure_config(const ComponentConfig&, float
available_w)` that runs the sizing rules without minting an entity. That is
the same primitive #191 asks for on text and #116 asks for on fits, one level
up, and it is what turns every hand-rolled window in this file into
arithmetic the library owns.

---

### #232 — a coordinate-addressed test cannot state its own precondition

**What was wanted.** `tests/ui/tracker_links.e2e` clicks a point inside a byte
range because there is no way to address the range (#51). Fine. What it also
wants is to say *the point I am about to click is inside the thing I mean*, so
that a layout change fails as "the layout moved" rather than as "the feature
is broken".

**What happens.** There is no such assertion. `expect_text`, `assert_ui_text`
and friends assert on TEXT and on named ELEMENTS; a byte range is neither.
So the only signal a coordinate test can produce is the downstream one — here
`expect_text "Opened D948120"` — which is the same failure a genuinely broken
tracker feature produces, and the reader cannot tell them apart without
rendering the thread and scanning the PNG for text rows (FRICTION_LOG section
12 prices that at ~20 minutes for three tests).

Worse, it cannot detect the *silent* form. `tests/ui/tracker_links_need_a_host`
clicked (456,492), 348 px below the only tracker id in the thread, and passed
for as long as it existed — a negative test on empty space passes whatever the
app does. Verified: with the host check removed from `link_hotspot`, so that
clicking an id raises the toast with no tracker configured, the old coordinate
still passed and the corrected one failed.

**The workaround.** An app-side diagnostic, `HANABI_LINK_AUDIT=1`, that prints
every rect the hit test considered next to the point it tested:

```
[link] HIT  id=D948120  point=(455.0,147.0)  rect=(432.0,140.5 46.0x13.0)
```

That is the precondition, after the fact, in the log of a normal run. It is
also the mechanical re-measure that replaces the screenshot ruler.


**Hanabi reference.** `src/ecs/e2e_commands.h::HandleRequireThreadCommand` — Hanabi added a command that waits for a named thread to be open with content and fails with the actual state. Tests: `tests/harness/precondition_not_met.e2e` (`require_thread t1`) — A harness fixture intentionally fails to prove the precondition failure text.


**Minimal upstream fix.** `expect_point_in <element> <x> <y>`, asserting that
the point is inside a named element's rect, would cover the general case
without any notion of a text run. For the text-run case specifically, #51's
"where did byte N land" would make the coordinate unnecessary in the first
place.


**POSTSCRIPT (2026-08-26).** The precondition is now stated IN the script,
which is what this gap asked for, and it took a custom command rather than a
library feature: `require_thread <id>` (src/ecs/e2e_commands.h) waits for the
named thread to be open with messages on screen and fails naming what was
open instead —

    precondition not met: thread 't1' is not open with content
    (open=nothing, messages=0). The assertions below this line would have
    been checked against a screen that does not have the thread on it.

Before it, a script whose thread never arrived asserted against the sidebar
and failed with `Text not found: 'Built the config diff'` plus a dump of the
VIEWS rail — a failure that reads as a broken transcript and was read that
way three times. tests/harness/precondition_not_met.e2e exists to produce
that failure on purpose, and scripts/harness_gate.sh fails if it stops
naming the precondition.

Note what the custom command needed that the library could not give: a
timeout message of its own (#380).

CLASS: TEDIOUS

---

### #285 — Every element-addressed input command is a CLICK, so a gesture on a named widget can only be scripted in absolute window coordinates

**What was wanted.** A scripted test for dragging along the transcript's
minimap rail. The rail's marks are named elements and the existing test clicks
them by name — `click_ui minimap_mark_60` — precisely so that a layout change
moves the test's target with it. The drag wants the same thing: press on the
rail, sweep, release.

**What happens.** The runner's commands split cleanly in two, and the split is
along the wrong axis (`e2e_testing/runner.h:47-69`):

```cpp
constexpr std::array<std::string_view, 5> coord_commands = {
    "click", "double_click", "triple_click", "mouse_move", "mouse_down"
};
constexpr std::array<std::string_view, 13> single_arg_commands = {
    "key", "select_all", "screenshot", "arrow",
    "action", "hold", "release", "click_ui",
    "click_text", "click_button", "focus_ui",
    "toggle_checkbox", "expect_focused"
};
```

Everything that addresses an ELEMENT is a click: `click_ui`, `click_text`,
`click_button`, `double_click_ui`, `triple_click_ui`, `focus_ui`. Everything
that can compose a gesture — `mouse_down`, `mouse_move`, `mouse_up`, `drag`,
`drag_to` — takes raw x/y. There is no `mouse_down_ui`, no `mouse_move_ui`, no
`drag_ui`.

The primitive is already there. `double_click_ui` takes an optional `dx dy`
offset from the element's top-left (`runner.h:172-182`), so "resolve a name to
a point, then aim the pointer at it" is machinery the runner has and does not
share with the down/move/up trio.

**Why the obvious escapes do not work.**

- **`drag_to x1 y1 x2 y2`** — the right shape (press / move / release over
  three frames, `command_handlers.h:398-453`) and still four raw coordinates.
  It also cannot be asserted THROUGH: it consumes itself in one command, so
  there is no point at which the script can ask "and is the transcript
  following right now, with the button still down", which is the whole claim
  a drag test exists to make. `mouse_down` / `mouse_move` / `mouse_up` can be
  asserted between, which is why the test uses them, and they are the
  coordinate-only ones.
- **`assert_ui <name> x=…` first, then use the number** — a script has no
  variables. The number has to be read by a human from a deliberately failing
  assertion and pasted back in as a literal, which is how this test's `1089`
  and `73` were obtained.
- **Give the rail a debug name and click it** — a click is not a drag. That is
  the gap.

**The workaround, and its cost.** `tests/ui/minimap_drag.e2e` is written in
absolute window coordinates, with a comment recording where they came from and
the window size they are true at. Cost: the test is exactly gap #232's
complaint — a coordinate test cannot state its own precondition, so a layout
change that moves the rail 20px does not fail as "the rail moved", it fails as
"dragging does not scrub". It also pins the window size for a reason that has
nothing to do with what is under test, and every other pane's geometry is now
load-bearing for a test about the minimap.


**Hanabi reference.** `tests/ui/minimap_drag.e2e` (`mouse_down 1089 650`) — The minimap drag test still uses absolute coordinates because no element-addressed press/move exists. `src/ecs/main_pane_system.h` (`minimap_rail(UIContext<InputAction>& ctx`) — The app implements the minimap rail as an addressable UI element, but the script cannot drag it by name. Tests: `tests/ui/minimap_drag.e2e` (`expect_text "Follow-up question #0:"`) — The script asserts the drag moves the transcript while the button is held.


**Minimal upstream fix.** `mouse_down_ui <name> [dx dy]` and `mouse_move_ui
<name> [dx dy]`, parsed like `double_click_ui` and resolved through the same
element lookup. Two entries in the parse switch and two handlers that call the
existing `test_input::set_mouse_position` with a resolved point instead of a
literal one. `mouse_up` already needs no argument.

CLASS: TEDIOUS

---

### #286 — A widget cannot know its own position on the frame it is built: the coordinates it was CONFIGURED with are in the parent's space, and the only absolute answer is the one last frame's layout left behind

**What was wanted.** To hit-test the pointer against the minimap rail on the
frame the rail is built, so that a press landing on it starts a drag.

**What happens.** The rail is built with an absolute position and a translate:

```cpp
const float railX = paneW - kRailW - kRailInset;   // 804
const float railY = railTopY + 6.0f;               //   6
auto rail = div(ctx, mk(parent, 7400),
    ComponentConfig{}.with_absolute_position().with_translate(railX, railY) …);
```

`with_absolute_position` means absolute within the PARENT, and the pointer
(`ctx.mouse.pos`) is in window space with letterbox and resolution scaling
already applied. The rail's real rect at this window size is `x=1084, y=73`.
The two numbers the caller has are off by the pane's origin — 280px and 67px —
and nothing says so:

```
[mmdbg] pos=1089,600  rail=804,6 10x583  down=1 jp=1 on=0 armed=0 live=0
```

`on=0`, forever. Nothing warns, nothing asserts, nothing fails to compile. The
gesture simply never starts, and the code that fails to start it is the code
anyone would write. It cost an hour, and the only reason it was found at all
is that a printf was cheaper than the next hypothesis.

The recovery is to read the rect the entity already carries —
`rail.ent().get<UIComponent>().rect()` — which is correct, absolute, and
written by LAST frame's layout pass, because this frame's has not run yet.

**Why the obvious escapes do not work.**

- **Add the parent's origin to the config's translate.** That is
  re-implementing the layout pass's coordinate composition in app code, one
  ancestor at a time, and it is wrong the moment anything between the widget
  and the root has padding, a border, or a scroll offset. It is #224's
  complaint about SIZE, at the other axis.
- **Do the hit test from a draw callback**, which does get an absolute
  `RectangleType`. It runs in the render pass, after the frame's input
  decisions have been made, and `RenderPrimitive::CustomDrawFn` takes the rect
  and nothing else (#111) — so the callback would have to write the answer to
  a variable the next frame reads, which is the one-frame-stale rect again with
  more machinery around it.
- **Ask afterhours whether the pointer is over the subtree** —
  `ctx.mouse_was_in_subtree(id)` exists and is the right question, but it
  resolves through `prev_hot_id`, so on the frame of a press that also moved
  the pointer (which is every synthetic press, and a real trackpad tap) it
  answers about where the cursor USED to be. Fine for a hover highlight,
  which is what it is for; not fine for deciding whether a press was yours.

**The workaround, and its cost.** Build the widget first, read
`UIComponent::rect()` off the returned entity, hit-test against that. It is one
line and it is correct — for a widget that has not moved since last frame,
which the rail has not. The costs are that the gesture is dead on the first
frame a widget exists (the rect is still 0x0, so a press on a rail that has
never been laid out is ignored), that the ordering is now load-bearing and
undocumented — build, THEN decide, when every other read in the function is the
other way round — and that the trap is entirely silent for the next person, who
will also have the parent-relative numbers in hand and no reason to distrust
them.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`const auto railRect = rail.ent().get<afterhours::ui::UIComponent>().rect()`) — The minimap rail reads the laid-out retained rect rather than hit-testing against configured parent-relative translate values. Tests: `tests/ui/minimap_drag.e2e` (`The rail is at x=1084..1094, y=73..656`) — The UI test records the measured absolute rail rect used by coordinate gestures.


**Minimal upstream fix.** Either resolve `with_translate` against the parent's
absolute origin at build time and hand it back on the `ElementResult`, or —
smaller and more useful — make `mouse_was_in_subtree`'s geometry available as
`ctx.point_in(id, pos)` answering against the retained rect rather than against
`prev_hot_id`. The narrowest version costs nothing: make `with_absolute_position`
say in its doc comment that "absolute" means the parent's frame and not the
window's, so that the mistake is at least documented where it is made.

---

# Session 2026-08-26 — text editing in the composer

Gabe, testing the build: no Shift+Enter for a newline, Alt+Backspace does
nothing ("i thought we added these already?"), no Cmd+A. The first and third
were true. The second had never landed, and #255 is why nobody noticed.

### #255 — a text-editing feature is opted into by ENUMERATOR NAME, and opting out is silent

**What was wanted.** Word motion and word delete in a text field. The library
has both, and has had them the whole time: `move_cursor_word_left`,
`delete_word_before_cursor` and friends in `text_input/utils.h`, driven from
`text_input/component.h`.

**What happens.** Each one is wrapped in a compile-time test on the consuming
app's own enum:

```cpp
if constexpr (magic_enum::enum_contains<InputAction>("TextDeleteWordBack")) {
  if (ctx.pressed_or_repeat(InputAction::TextDeleteWordBack)) { ... }
}
```

The app's `InputAction` enum is the feature switch, and the switch is a
STRING that has to match an enumerator. hanabi's enum stopped at
`TextSelectAll`, so every word path was deleted from the binary at compile
time. There is no error. There is no warning. There is no runtime log. There
is no symbol to grep for, because the code that would have named the missing
feature is the code that got compiled out — `git log -S TextDeleteWordBack`
over the whole history returns nothing, which reads identically to "we never
wrote it" and to "the library never had it".

The failure mode this produces is the expensive one: the app looks finished.
A field that takes text, moves a caret, deletes a character and selects with
Shift is obviously a working text field, and the four things it cannot do
have no visible edge. hanabi shipped like that from its first commit
(91d26bf); until this session `src/input_mapping.h` had exactly one commit in
its history, that one. The bug
was found by a person pressing Alt+Backspace, which is the only way it could
have been found.

There is a second, quieter half. An enumerator with no BINDING is equally
silent: `TextSelectAll` was in hanabi's enum from the start and had no row in
the key table, so `ctx.pressed(TextSelectAll)` could never fire and Cmd+A did
nothing. Same symptom, opposite cause, and neither one is observable from the
app's side.

**The workaround.** Add the names, add the bindings, and defend both with
tests, because nothing else can: `tests/unit/test_input_pipeline.cpp` asserts
that the enumerators the library gates on are PRESENT
(`magic_enum::enum_contains<InputAction>(name)` for each), and that each one
has a chord of the right shape in the shipping table. The first of those
assertions is the one that matters — it is the only thing standing between a
tidy-up of the enum and the silent removal of the feature.


**Hanabi reference.** `src/input_mapping.h::TextDeleteWordBack` — Hanabi's InputAction enum now declares the word-editing enumerators the library checks by name. `src/input_mapping.h::bind(InputAction::TextDeleteWordBack` — The shipping keymap binds the word-editing actions. Tests: `tests/unit/test_input_pipeline.cpp::test_word_chords_are_bound_to_option` — Unit tests assert word-editing chords are present and use Option. Proof patch: `vendor_patches/255-word-editing-capability.patch`; `tests/vendor_probes/word_editing_capability_probe.cpp` is a compile failure before and verifies complete/incomplete enums after through `make verify-vendor-patches`.


**Minimal upstream fix.** Any of three, cheapest first:

1. Name the required enumerators in ONE place a reader will find — the
   `text_input` doc comment lists the features but not the names that turn
   them on, and `default_keymap()` lists the names but reads as a convenience
   rather than as the contract.
2. A `constexpr` audit the app can assert on:
   `text_input::missing_actions<InputAction>()` returning the names it looked
   for and did not find, so an app can `static_assert` it is empty or log it
   once at startup.
3. Log it once, at runtime, the first time a field is built: "text_input:
   TextWordLeft not in InputAction; word motion disabled". One line, and the
   whole class of bug becomes a grep.

CLASS: FOOTGUN

---

### #287 — There IS a drag primitive, and it is not reachable from `ComponentConfig`

**What was wanted.** A press-drag-release on a plain element — the minimap
rail. Not a slider, not a splitter: a strip of the app's own drawing that the
reader can grab and sweep.

**What happens.** afterhours has exactly the right primitive, and no builder
for it. `HasDragListener` (`ui/components.h:102`) carries a `down` flag that
`HandleDrags` (`ui/systems.h:1060`) maintains, with the semantics a drag needs
spelled out in its own comment:

```cpp
// ResolveHitTarget grants active on the press. Deliberately not gated on
// the hit target below: a drag has to keep firing once the cursor leaves
// the handle, which is exactly what active persisting across frames buys.
```

That is the hard half of a drag — the press keeps belonging to the thing it
started on — solved, in the library, with `down` designed to be polled from
the next build pass rather than read inside a callback.

`ComponentConfig` cannot ask for it. Every listener the builder exposes is a
click or a value change; the drag-adjacent entry, `with_draggable_children`
(`component_config.h:623`), is drag-and-drop REORDERING of a container's
children (`entity.enableTag(DragTag::Group)`), a different feature that shares
a word. The only components that get a `HasDragListener` are the two the
library builds itself — `imm::drag_handle` (`imm_components.h:358`) and
`imm::slider` (`:1434`) — and both do it by reaching past the builder:

```cpp
entity.addComponentIfMissing<HasDragListener>([](Entity &) {});
const float moved = entity.get<HasDragListener>().down ? ctx.mouse.delta.y : 0.f;
```

**Why the obvious escapes do not work.**

- **Use `imm::drag_handle`.** It is a splitter: it sizes itself to a fixed
  thickness across the cross-axis, sets a resize cursor, and reports a per-frame
  DELTA. A scrubber needs its own geometry, its own cursor, its own drawing,
  and an ANCHOR rather than a delta — accumulated deltas detach from the cursor
  the first time the scroll clamps at an end, which is the classic scrollbar
  bug and is unfixable from the delta alone.
- **Do what `drag_handle` does — `addComponentIfMissing` on the returned
  entity.** It compiles and it would work. It also makes the rail hit-testable
  (`systems.h:688`), which puts a hit target over the per-mark buttons that are
  the rail's whole point, and it means the app is adding a library component to
  an entity the immediate-mode layer owns and rebuilds. The two call sites that
  do it are inside the library.
- **`ctx.mouse.delta`** — the same delta problem, without even the active
  tracking.

**The workaround, and its cost.** The gesture is hand-rolled in
`src/ecs/main_pane_system.h` (`minimap_rail`) out of `ctx.mouse.just_pressed`,
`left_down`, `press_moved` and `pos`, with four fields of state on the pane.
It is about twenty lines and it works.

What it costs is that hanabi now has a second, private definition of "a drag is
in progress" that the library cannot see. `is_active` says the mark that was
pressed is active, which is true and not the same fact. The cursor is whatever
the element under the pointer says, so sweeping off the rail reverts it to an
arrow mid-scrub — a real scrubber holds its cursor for the duration, and there
is nowhere to say so. Focus is untouched. And `MousePointerState::
press_drag_threshold_px` is a hard 6px with no per-widget override, which on a
583px rail standing for 12,000px of transcript is a 124px dead zone at the
start of every drag; a scrubber wants a smaller threshold than a list row does
and cannot ask for one.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`if (ctx.mouse.just_pressed) drag.armed = onRail;`) — The minimap rail hand-rolls drag state from raw mouse fields. `src/ecs/main_pane_system.h::drag.live` — The app maintains its own drag-in-progress state and scrolls while live. Tests: `tests/ui/minimap_drag.e2e` (`Releasing OUTSIDE the rail ends the drag`) — The UI test covers held drag behavior, off-rail release, and click-without-drag.


**Minimal upstream fix.** `ComponentConfig::with_drag_listener()` (or a bare
`with_draggable()`, since the useful half is `down` and not the callback),
adding the component the two built-in widgets already add by hand, and
returning `down` on the `ElementResult` the way `drag_handle` returns `moved`.
The threshold override is a second, smaller field on the same config.

---

### #275 — Nothing in the stack asks whether a widget is inside its PARENT: the one warning is main-axis only and goes to a log, and the one assertion measures the viewport

**What was wanted.** To answer "which widgets are drawing outside their own
box", after a one-sentence report — *"many buttons are going outside the
bounds"* — that named no widget and no screen.

**What happens.** There are three things in the stack that look like they
answer it and none of them does.

1. **`assert_no_overflow`** (`e2e_testing/command_handlers.h:653`) is the only
   containment ASSERTION, and its first check is
   ```cpp
   bool rect_out = (rect.x < -TOLERANCE) || (rect.y < -TOLERANCE) ||
                   (rect.x + rect.width > vw + TOLERANCE) ||
                   (rect.y + rect.height > vh + TOLERANCE);
   ```
   `vw`/`vh` are `e2e_screen_size()`. So a child can escape its parent by any
   amount at all and pass, as long as it lands inside the WINDOW. The parent's
   rect is never fetched. Its other two checks are about text fitting its own
   element, which is a different question again.

2. **The layout warning** does compare a child to its parent —
   ```
   [WARN] Layout overflow: 'composer_send' extends outside parent
   'composer_row' bounds (child_rel=[725.0,7.0], child_size=[78.0,32.0],
   child_end=[803.0,39.0], parent_size=[744.0,46.0], ...)
   ```
   — and it is exactly right when it fires. It fires when a `NoWrap` flow would
   have had to wrap on the MAIN axis. Measured against the app's real
   population: of **55** elements outside their parent's content box across
   twelve reachable states, this warning named **one**. The other 54 were
   cross-axis (a 32px button in a 30px row), or margin-driven (below, #276),
   and produced nothing.

3. And it is a `log_error`/`log_warn` line, so even the one it catches reaches
   nobody: it goes to a per-script log the scripted runner discards on a pass.
   It had been firing every frame, in a suite that was green.

**Why the obvious escapes do not work.**

- **Read `assert_no_overflow`'s output and tighten the tolerance** — it is not
  a tolerance problem. The rectangle it compares against is the window.
- **Assert geometry per widget with `assert_ui <name> x= y= w= h=`** — this is
  what the app does now, and it is per-widget and per-window-size: it pins the
  three or four you already know about. The report was about the ones nobody
  knew about, and a list of 55 is not something anyone writes by hand.
- **Diff a screenshot** — a picture cannot distinguish a button 59px past its
  row from a button that belongs there, and it only covers the screens someone
  thought to capture. Containment is not a fact about pixels; it is a
  relationship between two rects that both exist, exactly, after autolayout.
- **Walk the tree in app code** — this is the workaround, and it is ~120 lines
  before it is trustworthy, because the skip list is where all the difficulty
  is (below).

**The workaround, and its cost.** `src/util/bounds_audit.h`
(`HANABI_BOUNDS_AUDIT=1`) walks every `UIComponent` after the last frame and
reports each one whose `rect()` escapes its parent's rect inset by the parent's
`computed_padd`. `scripts/bounds_gate.sh` runs it over twelve states and fails
on anything outside a frozen baseline. It found 55; three were real horizontal
escapes and 44 were one rule written twice, and the app is at 3 known
1–1.5px vertical survivors.

**The cost is the skip list, and it is the part worth reading**, because every
line of it is a thing the library knows and an app has to re-derive:

- `absolute` — out of flow by construction; flagging them buries the flow
  overflows in noise.
- a child of a `HasScrollView` — content taller than the viewport IS a scroll
  view.
- `computed[axis] < 0` — never laid out.
- **`was_rendered_to_screen`** — and this one cost the audit its first
  finding. A widget built on an earlier frame and never rebuilt keeps its
  parent id, its computed rect and its last position (#115), so it looks
  exactly like a live overflow. `composer_status` was in the baseline at
  `right=50.0` and is not a defect: probed, its parent's shrink-to-fit width
  was correct to the pixel over a child list that did not contain it, and it
  reported `parent=85 rendered=0`. `assert_no_overflow` tests that flag first
  and I did not copy it.
- a floor — 0.5px, because nothing rounds a widget's origin (#110) and a
  fractional overflow fires on almost every `percent()`-sized element.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The entry's 55/3-survivor figures are stale against current bounds_gate comments, which describe eleven known 1.0-1.5px vertical survivors; the app-side audit remains current.

**Hanabi reference.** `src/util/bounds_audit.h` (`Every flow-positioned element whose rect escapes its parent's content box`) — Hanabi walks the UI tree and compares each flow child against its parent's content box. Tests: `scripts/bounds_gate.sh` (`fail on anything not in the baseline`) — The bounds gate runs the audit over reachable states and compares against a frozen baseline. Measurement/gate: `scripts/bounds_gate.sh` (`The survivors are eleven`) — The gate documents the current known baseline and why new parent overflows fail.


**Minimal upstream fix.** Two small ones, and the first is nearly free:
`assert_no_overflow` already walks every laid-out element with its parent one
hop away, so adding the parent-content-box comparison beside the viewport one
is a few lines and turns the only containment assertion into one that answers
the question people mean. Second, extend the existing layout warning past the
main axis — it already has the child and parent rects in hand at the point it
decides not to warn.

---

### #256 — correction to #49: `CMD+` in a script means Ctrl, and `SUPER+` is dropped

**What #49 says.** That `HandleKeyCommand` parses a Super modifier and never
holds it, so "there is no spelling of a Cmd chord that works, and the whole
shortcut surface of a macOS app is unreachable from a script". The first half
is right. The conclusion is too strong, and acting on it costs test coverage —
`tests/ui/composer_slash_commands.e2e` and `src/settings.h` both cite #49 as a
reason an assertion cannot be written, and one of those two is now wrong.

**What actually happens.** `parse_key_combo` (core/key_codes.h) has SIX
modifier prefixes and they do not do what their names suggest:

```cpp
{"CTRL+", &KeyCombo::ctrl},
{"CMD+",  &KeyCombo::ctrl},   // "Mac convention: Cmd = Ctrl for shortcuts"
{"ALT+",  &KeyCombo::alt},  {"OPTION+", &KeyCombo::alt},
{"SUPER+", &KeyCombo::super}, {"WIN+", ...}, {"META+", ...},
```

So `CMD+A` sets `ctrl`, and `HandleKeyCommand` holds `LEFT_CONTROL` for it.
`SUPER+A` sets `super`, which is the flag nothing ever reads. `ALT+` is held
properly.

The consequence is therefore conditional, not absolute:

- A chord read straight off the key state — `is_key_down(LEFT_SUPER)`, which
  is what `hanabi::keys::cmd_down()` does and what every app-level shortcut in
  hanabi uses — is unreachable from a script. #49 stands for these.
- A chord routed through the ACTION MAPPING is reachable, if the app binds the
  Ctrl twin alongside the Super one, exactly as `default_keymap()`'s own
  `bind_chord` does. The script writes `CMD+A`, the harness holds Ctrl, the
  Ctrl chord matches, and the same action fires that a real Cmd+A fires.

hanabi now tests Cmd+A, Cmd+Left, Cmd+Right and Cmd+Backspace from `.e2e`
files on the strength of that (`tests/ui/composer_word_editing.e2e`,
`composer_line_delete.e2e`). Cmd+Backspace is the interesting one: it is read
off the key state, so it is reachable only because `hanabi::keys::ctrl_down()`
was added as an explicit alias for the harness's benefit.

**The workaround.** Bind the Ctrl twin for anything that must be tested, and
for anything read off the key state, read Ctrl as well as Super. Both are in
`src/input_mapping.h` and `src/keys.h` with the reason written down.


**Hanabi reference.** Current code: `src/input_mapping.h` (`WHY THE CTRL TWINS`) — The keymap binds Ctrl twins because the script CMD+ prefix maps to Ctrl. `src/keys.h::cmd_or_ctrl_down()` — Raw Cmd-only app chords read Ctrl as a test-harness alias. Tests: `tests/unit/test_input_pipeline.cpp::test_line_chords_are_bound_to_command` — Unit tests assert Cmd and Ctrl twins for line-motion chords.


**Minimal upstream fix.** Hold the modifier that was parsed. Three lines next
to the three that are already there:

```cpp
if (combo.super) input_injector::set_key_held(keys::LEFT_SUPER);
```

plus the matching release in `HandleKeyReleaseSystem`. Then `SUPER+A` means
Cmd on a Mac, `CMD+` can keep its Ctrl-flavoured meaning for cross-platform
scripts, and no app has to bind a modifier it does not want in order to be
testable.

CLASS: FOOTGUN

---

### #257 — no action for delete-to-line-start, and an outside system cannot erase text

**What was wanted.** Cmd+Backspace: delete from the caret back to the start of
the line. Standard on macOS, in every text field on the platform.

**What happens.** There is no `InputAction` for it. The enum covers
`TextBackspace`, `TextDelete`, `TextDeleteWordBack`, `TextDeleteWordForward`,
`TextHome`, `TextEnd` — every other deletion granularity, and not this one. So
the key table cannot express it and the app has to drive it.

Driving it is where the second half of the gap is. The obvious implementation
is the one the library uses internally — erase from the field's state:

```cpp
st.storage.erase(line_start, caret - line_start);
```

That does not survive the frame. `text_input` is immediate-mode and re-seeds
itself from the `std::string` the caller binds, every call:

```cpp
if (s.text() != text) { s.storage.clear(); s.storage.insert(0, text); 
                        s.cursor_position = text.size(); }
```

An outside system that erases from the state and not from the bound string
leaves the two disagreeing, and the next build puts the deleted text straight
back — with the caret thrown to the END of the field for good measure, which
is the part that makes the bug look like something else. Only code that holds
the bound string can safely edit the state, which means "one text field at a
time, by name" rather than "the focused field, whichever it is".

**The workaround.** Set the SELECTION instead of erasing, and let the field's
own Backspace do the deleting (`src/ecs/text_edit_chords_system.h`). A
selection is not text, so the re-seed never fires on it; the widget sees a
selection under `TextBackspace`, deletes it, pushes the undo snapshot, and
writes back to the bound string itself. It costs an ordering constraint — the
system has to run before the UI is built, since the widget consumes the same
keypress later in the same frame — and it buys a chord that works in EVERY
field in the app rather than only the composer.

One further constraint, worth stating because it looks like a free choice:
Cmd+Backspace must NOT be bound as an explicit chord in the key table.
`suppress_permissive_duplicates` would then claim the BACKSPACE key and
suppress the plain `TextBackspace` binding — the very one the workaround
relies on to perform the delete.


**Hanabi reference.** `src/ecs/text_edit_chords_system.h` (`st->selection_anchor = line_start`) — Hanabi implements Cmd+Backspace by selecting to line start and letting TextBackspace delete. `src/ecs/keyboard_focus.h::focused_text_field()` — The app locates the focused text_input/text_area state before applying its chord workaround. Tests: `tests/ui/composer_line_delete.e2e` (`expect_input_text composer_reply_input "three"`) — UI coverage proves Cmd+Backspace deletes to line start and survives the next frame.


**Minimal upstream fix.** `TextDeleteToLineStart` (and its mirror
`TextDeleteToLineEnd`, which is Ctrl+K on macOS), guarded the same
`if constexpr` way as the word actions, deleting `[line_start, caret)`. Ten
lines beside `delete_word_before_cursor`, which already does the harder
version of the same thing.

Separately, and more valuable: a supported way for an app to edit a field's
state from outside it. `set_text(entity, str)` that writes BOTH the state and
the bound string would remove the re-seed trap for every consumer, not just
this chord — hanabi's escape-to-clear has to clear both by hand for the same
reason, and its comment says so.

CLASS: MISSING

---

### #265 — A focus ring is three outlines, not one, and the two you did not ask for take their colour from the RING rather than from what it is drawn on

**What was wanted.** The ring hanabi's own `preload.cpp` believes it is asking
for, in a comment: "ONE hairline, flush with the element." `theme.focus_ring_thickness
= 1.0f`, `focus_ring_offset = 0.0f`.

**What happens.** Measured off a Tab capture at 1180x949, the ring on the
sidebar's first row is three pixels of white-blue-white:

```
  y=67  (187,187,190)     <- outer contrast, white @180 over (23,23,35)
  y=68  ( 90,128,255)     <- the ring
  y=69  (194,197,206)     <- inner contrast, white @180 over the row fill
```

`rendering.h`'s `focus_ring_for` emits `outer_contrast()` at `expanded(thickness)`
and `inner_contrast()` at `expanded(-1)` alongside the coloured ring, and the
draw sites emit all three unconditionally. Only the coloured one is gated on
`focus_ring_thickness` — the early return exists precisely because, as the
comment there says, "the contrast edges are not gated on it". So `thickness`
selects between *no ring at all* and *at least three lines*. There is no value
of any theme field that produces one line.

**And the edges' colour is not a theme value.** It is derived from the ring:

```c++
ring.contrast = luminance(ring.color) > 0.5f ? Color{0,0,0,180}
                                             : Color{255,255,255,180};
```

That is the right rule for the case the comment names — a ring on a widget
whose fill is the ring's own colour — and the wrong one for a ring on a dark
app, which is the ordinary case. hanabi's dark accent {90,128,255} has a WCAG
luminance of 0.248, so both edges came out white at 70% opacity: 9.27:1 against
the {23,23,35} backdrop, where the blue they exist to protect manages 5.04:1.
The thing drawn to make the ring legible was the brightest thing on screen and
the ring was a detail inside it.

The two rules are the same rule with opposite answers. "Contrast with the
ring" and "disappear into the backdrop" agree only when the ring and the
backdrop are on the same side of the threshold, and nothing in the library
knows what the backdrop is.

**Why the obvious escapes do not work.**

- **Turn the edges off.** There is no flag. `theme_io.h` exposes
  `focus_ring_thickness` and `focus_ring_offset` and nothing else about the
  ring; the edges are not in the theme at all.
- **Raise the thickness so the edges are a smaller fraction.** The loop draws
  `thickness` rings OUTWARD from `ring.rect`; `inner_contrast` is at
  `expanded(-1)` and is never covered by any of them. The inner white line
  survives every thickness.
- **Give the ring an alpha.** `ring.color` alpha is only touched by
  `compute_effective_opacity`, which scales the contrast alpha by the same
  factor. The edges fade exactly as fast as the ring.
- **Pick the real macOS accent.** #0A84FF reads 0.238 and lands on the wrong
  side. So does every saturated blue: the threshold cuts straight through the
  range an accent would plausibly come from — {140,190,255} is 0.496 and
  {150,195,255} is 0.527, so six units on one channel flips both edges from
  black to white.

**The workaround, and its cost.** `ui/focus_visible.h`'s `ring_color()` walks
the palette's ring colour toward white (dark backdrop) or black (light) until
`afterhours::colors::luminance` — the same function that will make the
decision — puts it on the backdrop's side, then hands that to `theme.focus`.
The edges are still drawn; they are simply drawn in the backdrop's own
direction and vanish into it. Dark: the ring becomes {173,192,255} and the
edges composite to {7,7,10} against {23,23,35}, 1.13:1. Light was already
correct at {46,90,236} and is returned untouched.

The cost is that the app can no longer choose its ring's colour. The colour is
now an output of a constraint the library imposes, so hanabi's accent blue
cannot be its focus ring, and a custom accent swatch is walked wherever the
threshold requires rather than to where the designer put it. An app that wants
the system accent, on a dark theme, cannot have it.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The 'nothing turns them off' framing is stale: focus_ring_thickness=0 now disables all outlines. The specific lack of separate contrast-edge control remains and the color workaround ships.

**Hanabi reference.** `src/ui/focus_visible.h` (`ring_color(afterhours::Color base`) — Hanabi shifts the ring color to make the forced contrast edges sink into the backdrop. `src/ecs/focus_visible_system.h` (`ctx.theme.focus = fv::ring_color`) — A frame-level focus-visible system applies the adjusted ring color. Tests: `tests/unit/test_focus_ring.cpp::check_the_edge_does_not_outshout_the_ring` — Unit tests assert the contrast edge is less visually dominant than the ring. Proof patch: `vendor_patches/265-focus-ring-contrast-toggle.patch`; `tests/vendor_probes/focus_ring_contrast_probe.cpp` proves both renderers keep the three-outline default and emit only the requested ring when contrast is disabled through `make verify-vendor-patches`.


**Minimal upstream fix.** Gate the two contrast edges the way the coloured ring
is already gated — a `focus_ring_contrast` field, or simply `bool` — so an app
that has decided its ring is legible can say so. Better, derive the edge from
the backdrop rather than the ring: `focus_ring_for` already has the entity, so
`HasColor` is one lookup away, and "contrast with what is behind me" is the
question the edge was written to answer.

**POSTSCRIPT 2026-08-26 (gap index) — PARTLY OVERTAKEN: there is now a total
off switch, but not the one this entry asks for.** `focus_ring_for`
(`plugins/ui/rendering.h:212-226`) at the pinned submodule (428047e) opens with

```cpp
  FocusRing ring;
  ring.thickness = context.theme.focus_ring_thickness;
  if (ring.thickness <= 0.f)
    return {};
```

and the comment above it names exactly this entry's finding as the reason:
"thickness 0 means 'no ring'. It has to be checked before anything is emitted:
the contrast edges are not gated on it, so an app that set 0 to opt out still
got lines painted into its widgets." So the statement "nothing in the theme
turns them off" is no longer accurate — `focus_ring_thickness = 0` now turns off
all three outlines together, and that is what makes hanabi's own
`src/ui/focus_visible.h` workaround for **#83** possible at all.

What is still not possible is the thing this entry actually wants: keep the
coloured ring and drop the two contrast edges. They remain ungated
individually, and their colour is still derived from the ring's own luminance
(`rendering.h:243-245`, `lum > 0.5f ? black@180 : white@180`) rather than from
the backdrop. The measured three-pixel white-blue-white band on a dark theme is
unchanged. The ask stands as written; only the "no escape at all" framing is
out of date. Verified by reading the vendored source, not by running it.

---

### #258 — `expect_input_text` is the only field assertion that cannot see a multiline field

**What was wanted.** Move the composer to `text_area` for Shift+Enter, and
keep the scripted tests that assert what is in it.

**What happens.** `HasTextAreaState` DERIVES from `HasTextInputState`, and the
ECS keys components by exact type id:

```cpp
template <typename T> bool has() const {
  return componentSet[components::get_type_id<T>()];
}
```

so a query for the base never matches the derived one. The harness knows this.
Two of its three field assertions test for either component, and
`expect_input_selection` carries a comment saying exactly why:

```cpp
// HasTextAreaState is a distinct component, not a HasTextInputState, so a
// query for the latter alone never matches a multiline field -- which made
// this assert unusable on exactly the widget most likely to need it.
```

`expect_input_text` was left behind. It still queries
`whereHasComponent<text_input::HasTextInputState>()` alone — and it is the
only assertion in the harness that reads a field's TEXT. Its two siblings read
the SELECTION.

Measured: with the composer switched to `text_area` and nothing else changed,
six composer scripts failed, and every single failure in all six was the same
line —

```
[E2E ERROR] expect_input_text: Text input not found: composer_reply_input
```

Nothing else broke. Not the send key, not the slash menu, not the history
walk, not focus, not the layout. One missing `||` in one query is the entire
cost of a multiline field to a test suite.

**The workaround.** A shadow `HasTextInputState` on the field entity, carrying
a copy of the text, whose only purpose is to be visible to the harness
(`src/ecs/main_pane_system.h` on the spike branch). Its `is_focused` is
deliberately left alone so it cannot answer "is a field focused" with a stale
yes, and hanabi's own lookups ask for the area state first.


**POSTSCRIPT 2026-08-26 (source-reference audit).** The workaround is no longer merely 'on the spike branch'; it is present in current source. The underlying vendored expect_input_text limitation remains.

**Hanabi reference.** `src/ecs/main_pane_system.h` (`A SHADOW HasTextInputState, for the scripted-UI harness only.`) — The multiline composer adds a shadow HasTextInputState so expect_input_text can read text_area content. Tests: `tests/ui/composer_shift_enter.e2e` (`expect_input_text composer_reply_input ""`) — The multiline composer suite still uses expect_input_text for the empty-field case.


**Minimal upstream fix.** The `||` its two siblings already have.

```cpp
.whereLambda([&](const Entity &e) {
  return e.get<ui::UIComponentDebug>().name() == name &&
         (e.has<text_input::HasTextInputState>() ||
          e.has<text_input::HasTextAreaState>());
})
```

CLASS: WORKAROUND

---

### #266 — The ring's rect is the widget's LAYOUT box and its offset is one number for the whole app, so a UI with both full-bleed rows and inset chips cannot have a correct ring on either

**What was wanted.** The macOS convention the rest of this app is matched
against: a focus ring sits a couple of points OUTSIDE the control, clear of it.
`focus_ring_offset` looks like exactly that knob — positive insets, negative
outsets.

**What happens.** It is one number, read from the theme at draw time for every
widget in the frame, and the rect it insets is `UIComponent::focus_rect`, which
is built from `rect()` — the layout box — with no reference to what the widget
actually paints.

Both halves bite, in opposite directions, in one app:

- A sidebar row is full-bleed: its layout box runs from x=-1 to the sidebar's
  right edge, so at offset 0 the ring's left edge and its outer contrast are
  already off-screen and only the inner edge is visible. Outsetting it moves
  the ring further off-screen on the left and into the 3px gap above the next
  row on the other side.
- The composer's effort chip is an 18px transparent button whose layout box is
  wider than the text it draws, and which sits butted against the model chip.
  At offset 0 the ring already overlaps the neighbouring label; measured at
  -2 it cuts a further two pixels into it. The macOS-correct direction makes
  this widget worse.

So the correct offset is +something for one widget, -something for another and
0 for a third, and there is one field. Nor is there an opt-out: no per-widget
"no ring" flag exists — #83 noted that already — and none of `with_*` on
`ComponentConfig` reaches the ring at all.

**Why the obvious escapes do not work.**

- **Write the theme field per widget as the frame builds.** `theme` is one
  global struct read at RENDER time, not captured per widget (the same shape
  as gap #90), so the last writer of the frame decides the offset for
  everything in it.
- **Size the layout box to the ink.** That is #136 and #114: nothing sizes a
  box to its own content, and a sprite's ink extent is not derivable, so
  "make the rect the visible shape" is not a change the app can make either.
- **`with_skip_tabbing`** removes the widget from focus entirely rather than
  from the ring, which costs the keyboard user the control (#83).

**The workaround.** None that is correct. hanabi keeps `focus_ring_offset = 0`
because flush is the least-wrong single answer for its mix of widgets, and
accepts a ring that overlaps a label on the chips.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Let a widget carry its own offset — the same
override `HasRoundedCorners` already gives the ring for corner radius, which
`focus_ring_for` reads two lines above where it reads the global offset. A
`HasFocusRing{offset, thickness, enabled}` component would cover the offset,
the per-widget opt-out #83 asked for, and #265's edge control in one place.

---

### #259 — the script parser is line-based, so a multi-line expectation cannot be written

**What was wanted.** `expect_input_text composer_reply_input "first line\nsecond line"`
— assert that Shift+Enter actually put a break in the draft.

**What happens.** `parse_script` reads the file with `std::getline` and parses
one command per line; `parse_quoted` takes the rest of the line and strips a
leading and trailing quote. There is no escape processing anywhere in it, so
`\n` in a script is a backslash and an n, and a literal line break ends the
command. There is no spelling of a newline in any argument to any command —
which also means `type` cannot type one.

So the content of a multiline field cannot be stated in this DSL at all. The
one exception is the empty string, which is why `expect_input_text <name> ""`
still works and is the only text assertion a `text_area` can carry.

**The workaround.** Assert the LENGTH instead of the text: select all, and
assert the range with `expect_input_selection`, which is a three-argument
command taking integers and so unaffected. "first line" + "second line" is 21
bytes joined and 22 with a break between them, so `0 22` is the whole claim,
and `0` would mean it sent instead. `tests/ui/composer_shift_enter.e2e` on the
spike branch is written this way throughout. It is exact, and it reads
terribly — every assertion is a number the reader has to recompute by hand.


**Hanabi reference.** `tests/ui/composer_shift_enter.e2e` (`WHY THE ASSERTIONS ARE BYTE COUNTS`) — multiline assertions use selection byte ranges. Tests: `tests/ui/composer_shift_enter.e2e` (`expect_input_selection composer_reply_input 0 22`) — Shift+Enter is proven by range.


**Minimal upstream fix.** Process `\n` (and `\\`) in `parse_quoted`. Four
lines, and it makes `type` able to enter a newline as a bonus, which no
command can currently do either.

CLASS: TEDIOUS

---

### #260 — `text_area`'s word motion does not collapse a selection; `text_input`'s does

**What happens.** With text selected, an UNSHIFTED motion should collapse the
selection to its near edge and stop there — that is what macOS does, and what
`text_input` does:

```cpp
navigate([&] {
  if (!shift_held && state.has_selection())
    state.cursor_position = state.selection_end();
  else
    move_cursor_word_right(state);
});
```

`text_area`'s word motion skips that branch and always steps a word:

```cpp
navigate([&] { move_cursor_word_right(state); reset_preferred_column(state); });
```

The inconsistency is not only between the two widgets. `text_area`'s own
plain Left/Right DOES collapse, under a comment that says so ("Unshifted with
a selection, they collapse to its near edge rather than stepping one character
from the caret") — so within a single widget, arrow collapses and Option+arrow
does not.

Observed: select "alpha" with Shift+Option+Left, press Option+Right, type. In
`text_input` the marker lands at the end of "alpha"; in `text_area` it lands
past the following word.

**The workaround.** None available. An app cannot intervene: clearing the
selection before the widget runs does not stop the extra word of movement, and
the movement happens inside the same call that reads the key.
`tests/ui/composer_shift_selection.e2e` asserts the behaviour AS IT IS with
this gap named beside it, so that a library fix shows up as a failing test
rather than as nothing.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** The four lines `text_input` already has, in the two
word-motion branches of `text_area`.

CLASS: SHARP EDGE

---

### #261 — `text_area` has no placeholder

**What happens.** The word "placeholder" does not occur in `text_area.h`.
`text_input` has had one since #29 was closed (`component.h`: `show_placeholder
= display_text.empty() && !config.placeholder.empty()`), and
`ComponentConfig::with_placeholder` is shared by both, so the call compiles,
does nothing, and the hint silently disappears.

That is a regression for any app moving a field from single-line to multiline,
and the direction of travel is always that way — the field that needs a
placeholder most is the chat composer, and the chat composer is the field that
needs to be multiline.

**The workaround.** hanabi already had one, in `git show 982376a`: an
absolutely-positioned child over the empty field with an `on_draw_fg` that
draws the hint. It was DELETED when `text_input` grew a real placeholder, and
the spike branch brings it back for the widget that still needs it. Measured
against the single-line field it replaces, the overlay lands on the same pixel
the typed text does (x=329, y=706), so the two agree — but that is a
hand-alignment that holds only until either side's padding changes.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`with_debug_name("composer_placeholder")`) — composer draws overlay placeholder. `src/ecs/main_pane_system.h` (`text_area does not render one at all`) — source explains overlay.


**Minimal upstream fix.** The four lines from `component.h`, in `text_area`'s
line loop when the text is empty.

CLASS: MISSING

---

### #267 — The ring is drawn from focus STATE with no reference to whether focus can move, so an input map missing one binding paints a ring that is a lie

**What was wanted.** Tab moves focus; the ring follows it. The ordinary
contract.

**What happens.** hanabi bound `WidgetLeft`, `WidgetRight`, `WidgetPress`,
`MenuBack` and the four text actions, and never `WidgetNext`. Four Tab presses
against the sidebar then produced four byte-identical frames (md5
`e7ac8de4d5be67dbdbcd36b2ffbd26a9` for captures 2, 3 and 4): `process_tabbing`
is the only thing that moves `focus_id`, it moves it only on `WidgetNext`, and
`WidgetNext` could not be pressed. The ring still painted — on whatever
`UIContext::try_to_grab` parked focus on at startup — and stayed there for the
life of the process.

That is worse than no ring. A ring says "you are here, and the keyboard can
take you elsewhere", and the second half was false.

Nothing reports it. `process_tabbing` guards on
`magic_enum::enum_contains<InputAction>("WidgetNext")`, which is satisfied by
an app that DECLARES the enumerator, so declaring it and never binding it
compiles to a live branch whose condition is permanently false. The ring is
computed from `visual_focus_id` in `focus_ring_for`, which never asks whether
anything in the frame could change it. And `default_keymap` — which ships
exactly the bindings that were missing — is opt-in, so an app that hand-rolls
a mapping (the natural thing to do when it has actions of its own) silently
opts out of the UI plugin's own requirements.

**Why the obvious escapes do not work.** There is nothing to escape from once
the binding exists; the gap is that it took a screenshot diff to find. No
warning, no assert, no debug overlay names it: `dump_ui` is not registered
(#192), and the scripted harness cannot see a ring at all (#61), so a suite of
92 passing UI tests said nothing.

**The workaround.** Bind it — `WidgetNext = TAB`, `WidgetMod = SHIFT` — and
prove it with a test that names the widget wearing the ring. hanabi extends its
own `HANABI_FOCUS_AUDIT` to print the focused widget's first labelled
descendant next to the version string, because the focusable thing is the row
CONTAINER and it holds no text of its own; that string is what a scripted test
asserts, and it is the only way from outside to say WHICH element the ring is
on.


**Hanabi reference.** `src/input_mapping.h` (`bind(InputAction::WidgetNext, {keys::TAB});`) — Tab focus action is bound. `src/ecs/sidebar_system.h` (`HANABI_FOCUS_AUDIT=1`) — footer audit names focus-ring target. Tests: `tests/ui/tab_walks_the_focus_ring.e2e` (`expect_text "ring on Settings"`) — e2e proves ring walks.


**Minimal upstream fix.** Two lines in `focus_ring_for`: if no binding can
reach `WidgetNext`, the frame cannot move focus by keyboard, so log once and
skip the ring. The mapping is already a singleton the context can read. A
cheaper version is a startup check — `init_ui_plugin` warning for every action
the UI plugin consumes that the app declared and left unbound — which would
also catch `WidgetPress`, `WidgetMod` and the text actions, and costs nothing
at runtime.

---

### #276 — `Dim::Percent` is the one sizing mode that ignores the child's own margin, so `percent(1) + margin` is a child that does not fit — and it moves the WRAP, not just the child

**What was wanted.** A row of chips, full width of its parent, indented 16px.

```cpp
.with_size(ComponentSize{percent(1.0f), children()})
.with_flex_wrap(FlexWrap::Wrap)
.with_margin(Margin{.top = pixels(6), .left = pixels(16)})
```

**What happens.** The row comes out **exactly as wide as its parent's content
box and 16px to the right of it**. `autolayout.h`'s `Dim::Percent` case says so
itself:

```cpp
case Dim::Percent: {
  // Percent of parent's content area (computed minus padding only;
  // margins are external spacing and not included in computed).
  float parent_size = parent.computed[axis] - parent.computed_padd[axis];
  return constraint.value * parent_size;
}
```

and the margin is then applied as a pure translation in
`compute_relative_positions`. Measured: `subrollup_chips` 736 wide at x=378
inside a parent whose content is 736 wide at x=362.

**This is a specific inconsistency and not "margins are ignored", which is why
it is worth an entry.** The other two sizing modes do account for a child's
margin, in opposite directions:

- `_sum_children_size` adds it — *"Include child margins so `Dim::Children`
  parents are sized to fit children including their external spacing."*
- cross-axis `Dim::Expand` subtracts it —
  `child->computed[cross] = fmaxf(0.f, content - child->computed_margin[cross])`.

So a consumer who has learned that margin participates in sizing learns it
correctly, twice, and then meets the third mode.

**And the damage is not the 16px.** The row is `FlexWrap::Wrap`, and wrapping
is decided against the row's own width. The row's width was a lie, so **the
last chip on every line wrapped 16px late** — 16px outside the parent, on every
line, at every window size. A pure position bug would have been visible at the
edge; this one is visible in the middle of the list, as chips that do not line
up with anything.

**Why the obvious escapes do not work.**

- **`percent(0.98)`** — the margin is pixels and the percent is a fraction, so
  the arithmetic is only right at one parent width.
- **Compute the width in pixels from the parent's own** — the parent's content
  width is not a number the child has; that is what `percent` was for.
- **Give the PARENT the padding instead** — right when the indent applies to
  every child, wrong here: the rollup's header sits flush and only the chip row
  is indented.

**The workaround, and its cost.** `Padding{.left = 16}` on the row instead of a
margin. Padding insets the CHILDREN and leaves the element the size its parent
gave it, so it is the same indent and the right box. The cost is that this is
the OPPOSITE of the advice #109 gives — margin over padding — and both are
right: #109 is about a LABEL, which is drawn from the element's own rect and
never sees padding, and this is about child DIVS, which are all padding
reaches. A consumer has to know which of the two shapes they are holding before
they can pick, and nothing in the API distinguishes them.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`.with_padding(Padding{.left = pixels(16)})`) — chip row indent uses padding rather than margin. Tests: `tests/ui/subagent_chips_stay_inside_the_rollup.e2e` (`The sub-agent chip row is indented by PADDING, not by a left margin`) — e2e pins containment.


**Minimal upstream fix.** Subtract the child's `computed_margin[axis]` in the
`Dim::Percent` resolution, exactly as the cross-axis `Dim::Expand` path already
does. `percent(1.0f)` with a margin then means "as wide as will fit next to my
margin", which is what every caller writing it believes it means. Failing that,
the same `log_error` that already fires for "parents sized with mode 'children'
cannot have children sized with mode 'percent'" would fire for a percent child
with a non-zero margin, and this would have been a build-time complaint instead
of a wrap that is wrong on every line.

CLASS: FOOTGUN

---

### #277 — The 5px a label is drawn at is hard-coded, unexposed and unqueryable, so a text child and a drawn child of one parent are on different columns and the app carries two different constants for the one number

**What was wanted.** A pulsing dot in an assistant bubble whose leftmost lit
pixel is the column the bubble's prose starts on — so that when the first token
arrives and the dot is replaced by text, nothing moves sideways.

**What happens.** They cannot be on the same column without the app knowing a
number the library never says. `rendering.h` draws a label at a literal inset
from the element's own rect:

```cpp
position_text_ex(fm, hasLabel.label.c_str(), text_rect, hasLabel.alignment,
                 Vector2Type{5.f, 5.f}, ...);
```

— origin at `container.x + margin_px.x`, wrap width at
`container.width - 2 * margin_px.x`. The literal appears three times in that
file. A CUSTOM DRAW (`with_on_draw_fg`) gets the element's rect untouched, and
so does a child widget. So two children of one parent, given the same box, put
their ink 5px apart, and the number is in neither the config nor the component
nor any query.

**What it costs a consumer, which is more than 5px.** The app's transcript
bubble wants its first glyph 13px inside the bubble, so it sets its padding to
`13 - 6 = 7` and lets the library add the rest back. That is correct for text
and silently wrong for everything else in the same bubble: the thinking
indicator's dot drew its ink at x=372..383 where every line of prose in the
same bubble began at x=374. The code block one screen away pays the same 6 back
by hand at its right edge, with a comment saying why. Each site is a separate
discovery.

**And the number is not agreed on, in one file.** `main_pane_system.h` carries
BOTH:

```cpp
static constexpr float kLabelInset  = 5.0f;   // the composer
static constexpr float kLabelInsetX = 6.0f;   // the transcript
```

Both are named for the same library behaviour and both comments describe it
correctly. 5 is right: it is the literal, read out of `rendering.h`. 6 is what
you get when you MEASURE it off a capture, because what a screenshot shows is
the first lit pixel and that is the 5 plus the leading side bearing of whatever
glyph you measured. Neither reader was careless; one had the source and one had
a PNG, and the library gave them no way to agree. A consumer measuring a
capital letter and a consumer measuring a lowercase one would have got two
different sixes.

**Why the obvious escapes do not work.**

- **`with_padding` on the label** — inert (#85, #91, #109): the words are drawn
  from the element's own rect and padding never reaches them.
- **Give the drawn child a 5px margin so it matches** — this is the workaround,
  and it is per-child forever, in app code, against a literal in a vendored
  file that no test anywhere pins. If afterhours changes the 5 the app silently
  goes back to being 5px out on every hand-drawn mark.
- **Measure it once at startup** — there is nothing to measure it against
  without rendering a glyph and scanning pixels, which is the loop that
  produced the 6.

**The workaround, and its cost.** The indicator's row takes
`Padding{.left = kLabelInsetX}` so its children start on the text column, and
its dot's gap to the label is written `kDotLabelGap - kLabelInsetX` — "the gap
a reader sees is dot-ink to first glyph, and the label's own rect already
spends some of it". That idiom now appears four times in the file, with two
different values for the one constant.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`constexpr float kLabelInset = 5.0f;`) — composer path names label inset. `src/ecs/main_pane_system.h` (`static constexpr float kLabelInsetX = 6.0f;`) — transcript path carries measured inset. Tests: `tests/ui/thinking_indicator_sits_on_the_text_column.e2e` (`kLabelInsetX (6), because afterhours adds that 6 back`) — e2e pins alignment. Proof-patch decision: `vendor_patches/README.md` rejects #85/#277 because a coherent setting must reach every plain/wrapped/styled and immediate/batched 5px/10px calculation; changing only the visible literal would create divergent pixels.


**Minimal upstream fix.** Name it and expose it: a
`ui::kLabelTextInset` (or a field on the theme, since it is a styling decision)
that `position_text_ex`'s callers read instead of the literal, plus a
`text_origin_for(const Entity&)` returning where a label's text will actually
be drawn. The first stops it being a magic number; the second is what a
consumer aligning anything to a label actually needs, and it is the same
primitive #191 asks for on measurement.

CLASS: FOOTGUN

---

---

### #240 — NOT A GAP: coloured runs are first-class (`with_styled_label`), so a two-colour row is ONE widget and needs no hand-sized column

Filed as a negative result, and filed because this session got it wrong first
and the wrong version very nearly shipped.

**What was wanted.** The transcript grew rows that are not speech — a skill
loading, a node attaching, a delivery, a status report. Each wants a class word
in its own colour followed by the fact in the body colour:

```
node     attached  od-4471.quota
skill    presto-query
```

**What the obvious reading of the API says.** `with_label` takes one
`std::string` and `with_custom_text_color` takes one `Color`. One label, one
colour. So the shape that suggests itself is a Row div with two children — a
chip and a line — and the chip needs a WIDTH, which nothing hugs (#136), so it
becomes a constant:

```cpp
static constexpr float kEventChipW = 62.0f;   // guessed, drifts with the font
```

Three entities per row instead of one (#138 prices a widget at ~4.6 heap
allocations per frame), a magic number that is wrong at any other font size,
and a second child sized `colW - kEventChipW` that has to be kept in step by
hand.

**What actually exists.** `ComponentConfig::with_styled_label(std::vector<TextSpan>)`
— `component_config.h:658`. A `TextSpan` is `{text, color, weight}`
(`ui_core_components.h:457`), and the setter concatenates the runs into `label`
so "layout, measurement, and overflow behave exactly like a normal label". The
renderer's `draw_runs_in_rect` (`rendering.h:830`) wraps runs across lines,
swaps the font face per run for weight, and mirrors `position_text_ex`'s
alignment maths so a styled label lands where a plain one would.

So the row is one widget:

```cpp
.with_styled_label({{"node   ", theme::status_active()},
                    {"attached  od-4471.quota", theme::text_secondary()}})
```

No column width, no second entity, and the ellipsize/overflow path is the one
every other label uses. hanabi already had two callers
(`code_spans`, inline-code runs in `main_pane_system.h`) and this theme did not
find them before building the three-entity version.

**Why it is worth an entry anyway.** The discoverability is the defect, not the
capability. `with_label` and `with_custom_text_color` are what a consumer meets
first, they are scalar, and nothing on either points at the run-based form —
so the natural conclusion is "one label is one colour", which is false, and the
workaround it leads to is worse in entity count, in allocations, and in
correctness across font sizes. One `@see with_styled_label` on
`with_custom_text_color` retires it.

The neighbouring gaps are real and unaffected: #90 (`ctx.theme` is frame-wide)
and #17 (some widgets ignore `custom_text_color`) are about which colour a
widget gets, not about how many a label may have.

CLASS: NOT A GAP



**Hanabi reference.** Negative result: `src/ecs/main_pane_system.h::with_styled_label(event_spans(m))` — event rows use one styled-label widget. `src/ecs/main_pane_system.h` (`with_styled_label(code_spans(shown, runs, &audit))`) — inline code uses same capability.

---

### #241 — NOT A GAP: `imm::mk` hashes the SOURCE LOCATION, so two row kinds cannot collide, and the hand-allocated id bases in hanabi's transcript protect against nothing

The second negative result of this theme, and the one that was one edit away
from being filed as a gap with a confident wrong mechanism — the #115 failure
mode exactly.

**What was wanted.** The transcript is a heterogeneous virtualized list: a
bubble, a tool pile, a thinking fold, a spawn card, and now an event row and a
delivery fold, all children of the same column entity. Adding two row kinds
meant choosing widget ids for them, and hanabi's existing renderer reads as if
the id space were a hand-allocated flat namespace per parent:

```
render_meta_line     mk(parent, 200 + index * 10)
render_spawn_card    mk(parent, 240 + index * 10)
render_thinking_block mk(parent, 3400 + index * 10)
```

The `* 10` stride and the disjoint bases say, plainly, "these must not
collide". The gap that suggests itself is: *the id space has no allocator, and
a collision between two row kinds silently reuses one entity — including its
`HasClickListener` — so a new row kind means auditing every base and stride in
an 8400-line file.*

**That is false, and the source says so in eleven lines.**
`imm::mk` (`entity_management.h:26`) hashes

```cpp
pre_hash << parent.id << otherID << "file: " << location.file_name()
         << '(' << location.line() << ':' << location.column() << ") `"
         << location.function_name() << "`: ";
```

The `std::source_location` defaults to the CALL SITE. So the identity of a
widget is (parent, integer, file, line, column, function): two different
functions cannot collide whatever integers they pass, and two calls on
different lines of the SAME function cannot either. The integer disambiguates
one call site invoked more than once — a loop — and nothing else.

Which means the bases are inert. `render_event_row` is one `mk` call reached
once per row; `mk(parent, index)` is sufficient and `mk(parent, 3600 + index * 10)`
buys nothing. The convention costs a decision on every new row kind and defends
against a collision the hash makes unrepresentable.

**The one real edge, and it is handled loudly.** Calling `mk` twice from the
same line with the same integer DOES return the same hash, and afterhours logs
it by name before rethrowing:

```
Entity ID conflict detected! This usually happens when mk() is called multiple
times from the same source location without proper index management.
Location: {}:{}:{}, Function: {} …
```

Loud, attributed, with the fix in the message. That is the opposite of silent.

**What this does NOT retire.** #171 stands untouched and is the gap that
actually bites a virtualized list: identity is keyed on the SLOT, so when the
row at index 40 changes kind between frames the entity is reused and any
per-widget state points at the wrong row. That is why the delivery fold's
open/closed state is keyed by MESSAGE ID in app state
(`app.expandedThinking`, `"d" + m.id`) rather than by index — and #171 is why
it has to live in app state at all.

CLASS: NOT A GAP



**Hanabi reference.** Negative result: `src/ui/mk.h` (`widget_key(parent.id, otherID, loc)`) — identity includes parent/id/source-location. `src/ecs/main_pane_system.h` (`delivery_key(const api::Message& m, int index)`) — real fold state is message-keyed.

---

### #262 — `text_area` hardcodes its field background and ignores `with_transparent_bg`

**What happens.** The field div is built with a fixed background:

```cpp
div(ctx, mk(entity, 0),
    ComponentConfig::inherit_from(config, "text_area_field")
        .with_size(config.size)
        .with_background(Theme::Usage::Secondary)   // not from config
        ...
```

so a caller's `with_transparent_bg()` is overwritten. `text_input` honours it.

Measured on hanabi's composer: the field interior went from the window colour
(23,23,35) to a (57,57,68) fill inside an outlined wrap whose own comment
spends a paragraph on getting Puffin's look right — "Puffin's input interior is
the window colour and only the 1px border says where the field is". The change
is not subtle and it is not configurable.

**The workaround.** None used. In principle the app can save `ctx.theme`'s
secondary colour, set it to the surrounding panel colour for the duration of
the call and restore it — the same save/restore this composer already does for
`font_muted` two lines above (#90) — but that is defeating a hardcode with a
global, and it would recolour anything else in the call that reads Secondary.
This is one of the two reasons hanabi's multiline composer is a spike branch
rather than a shipped change.


**POSTSCRIPT 2026-08-26 (source-reference audit).** original 'none used' and grey-interior measurement are stale; postscript/current code correct them.

**Hanabi reference.** `src/ui/field_chrome.h` (`inline void clear_forced_fill`) — forced fill is cleared after text_area builds. `src/ecs/main_pane_system.h` (`hanabi::ui::field_chrome::clear_forced_fill(composerFieldId);`) — composer applies workaround. Tests: `scripts/composer_chrome_gate.sh` (`THE INTERIOR IS THE WINDOW COLOUR`) — pixel gate catches fill regression. Measurement/gate: `src/ui/field_chrome.h` (`42 pixels, measured`) — outline-overpaint measurement.


**Minimal upstream fix.** Read the background from the config the way the rest
of the widget reads everything else, defaulting to Secondary when unset.

**POSTSCRIPT (shipped on `feat/composer-multiline`). The measurement above is
WRONG, the gap is real, and it costs something else.**

"None used" is no longer true — hanabi ships a workaround — and the
description of the damage was wrong in a way worth correcting rather than
quietly fixing, because it sent the fix in the wrong direction.

*What is wrong.* The interior does NOT go to (57,57,68) on this composer, and
it never did. `Theme::Usage::Secondary` is resolved at BUILD time, not at
render time — `component_init.h:381-385` writes a concrete `Color` into
`HasColor` from `ctx.theme.from_usage(...)` during the imm build — and this
composer has pointed `ctx.theme.secondary` at the strip colour since gap #17,
two lines above the call. So the forced fill lands on (23,23,35), which is
exactly the colour the interior is supposed to be. Measured after the switch:
every sampled interior pixel is (23,23,35), unchanged.

That also retires the entry's own proposed workaround, and the objection to
it. Save/restore of `ctx.theme.secondary` is not "defeating a hardcode with a
global" here: the window is the build call, exactly as gap #105 records for
`font_muted`, and the stated cost — "it would recolour anything else in the
call that reads Secondary" — is empty. `text_area.h` reads the theme in
four places (`:150` rounded corners, `:157` this background, `:268` and
`:352` `custom_text_color.value_or(ctx.theme.font)`) and exactly one of them
is Secondary. Nothing else in the call reads it.

*What it actually costs.* The OUTLINE. The field is `percent(1.0f)` of a wrap
whose own 1px border draws ON the box edge, so an OPAQUE field — of any
colour, including the right one — paints over that border wherever the two
overlap. Measured, 1100x760, resting composer, before against after:

    42 pixels, all (41,41,52) -> (23,23,35)
    both rounded top corners' 7px arcs, and the entire right-hand border
    column x=1033 down the field's 29 rows

The composer stopped being a rounded outlined box and became a box missing a
corner and an edge. A same-coloured fill is invisible on a flat surface and
destructive on top of a line, and that distinction is the difference between
"the right colour" and "nothing" — which is what `with_transparent_bg()` asks
for and what the widget refuses to give.

*The workaround shipped.* Not the theme save/restore, because the right colour
is not the ask. `src/ui/field_chrome.h::clear_forced_fill` sets the field
entity's `HasColor` to `colors::transparent()` after the call — the exact
value `with_transparent_bg()` would have put there
(`component_config.h:344`). Reachable because the widget builds the field as a
child div and then mutates that entity, so it is addressable by the time the
call returns.

*Cost of the workaround:* none measurable. The composer at rest and focused is
byte-identical to the single-line one it replaced, and `make chrome-gate` now
fails if the outline is ever painted over again ("the wrap's outline is
painted over on 29 of its 45 interior rows").

*The upstream fix above is still right*, and this makes it more urgent, not
less: an app cannot even work around it by pointing the theme at the correct
colour, because the correct colour is the bug.

CLASS: MISSING

---

### #263 — `text_area` draws no focus ring

**What happens.** `text_input` puts a 2px accent border on the field while it
is focused:

```cpp
if (state.is_focused) {
  auto focus_color = ctx.theme.accent;
  field_config.border = Border::all(focus_color, pixels(2.0f));
}
```

`text_area` has no equivalent. It tracks `state.is_focused` (it needs it for
the caret and for input) and never draws anything with it, so a focused
multiline field is indistinguishable from an unfocused one except for a
blinking caret.

Measured on hanabi's composer: the blue (0,122,204) edge at y=724 is simply
gone after the switch.

This one is worse than it sounds for an app with a focus policy. hanabi has a
whole system for deciding when a focus ring is EARNED (`ui/focus_visible.h`,
"nothing renders a ring the keyboard has not earned") and its composer is
where that decision is most visible.

**The workaround.** None used. The app can put the ring on its own wrapper div
from `state.is_focused`, at the cost of one frame of lag (the wrapper is built
before the field it contains) and a ring around the 45px box rather than the
29px field. Not obviously wrong, but it is a visual change to the most
measured widget in the app, so it is on the spike branch and not shipped.


**POSTSCRIPT 2026-08-26 (source-reference audit).** original 'none used' wrapper workaround is stale; current code fixes field entity directly.

**Hanabi reference.** `src/ui/field_chrome.h` (`inline void apply_focus_edge`) — field entity receives focused border. `src/ecs/main_pane_system.h::hanabi::ui::field_chrome::apply_focus_edge` — composer applies workaround. Tests: `scripts/composer_chrome_gate.sh` (`A FOCUSED FIELD HAS A COLOURED EDGE`) — pixel gate verifies focused edge.


**Minimal upstream fix.** The four lines from `component.h`.

**POSTSCRIPT (shipped on `feat/composer-multiline`). "None used" is no longer
true, the proposed workaround was not the one taken, and its two stated costs
were both avoidable.**

*The workaround shipped.* `src/ui/field_chrome.h::apply_focus_edge` does the
four lines from `component.h:257-262` from outside the widget, on the same
entity, after `text_area()` returns:

```cpp
field.addComponentIfMissing<HasBorder>();
field.get<HasBorder>().border = Border::all(ctx.theme.accent, pixels(2.f));
```

*Why not the wrapper div this entry proposed.* Both costs it names are real
for the wrapper and neither applies here.

- "one frame of lag (the wrapper is built before the field)" — true of the
  wrapper, and the reason it does not bite is the mechanism this entry did
  not spot: `text_input` does not put the border in its field's CONFIG, it
  mutates the field ENTITY after building the div (`component.h:257`,
  `addComponentIfMissing<HasBorder>` on `field_entity`). An app can do the
  same thing to the same entity in the same frame. Zero lag.
- "a ring around the 45px box rather than the 29px field" — same cause. The
  field child is findable: `text_area.h:276` gives it `InFocusCluster`,
  which is what hanabi's `ecs::focusable_field()` already looks for and what
  two other call sites in the app already use to hand focus to a field.

*Self-clearing, which is the part that surprises.* There is no "else" branch
and there must not be one. `component_init.h:272-276`'s `apply_border`
REMOVES `HasBorder` from any entity whose config carries no border, and the
field div is rebuilt every frame, so an unfocused frame has already stripped
the edge before the app's code runs. `text_input` relies on exactly this.

*Cost of the workaround, measured.* None. 1100x760, three-tab dark fixture,
composer focused: the accent rows land at y=696 and y=724 — the field's top
and bottom, 29px apart — in (0,122,204), which is byte-identical to what
`text_input` drew, and the resting frame is byte-identical too. `make
chrome-gate` asserts both, plus that the edge is inside the wrap's border
rows rather than on them, and that focusing repaints nothing outside the
composer band.

*On the focus POLICY,* since this entry raises it. hanabi's
`ui/focus_visible.h` governs the app's :focus-visible RING — armed by Tab,
disarmed by a pointer press, applied through `theme.focus_ring_thickness`. It
is a different indicator from this one and that file says so itself, in the
note about "the focused border the composer draws itself". Reproducing
`text_input`'s edge is obeying that policy rather than bypassing it: the ring
stays gated exactly as it was, and the colour is taken from
`ctx.theme.accent` — deliberately NOT `ctx.theme.focus`, which
`focus_visible_system.h` rewrites every frame with the ring's contrast
correction, and taking it would have quietly put a policy value on a
non-policy indicator.

CLASS: MISSING

---

### #264 — `default_keymap()` is not macOS-correct

**What was wanted.** Take the library's conventional bindings and add the
app's own on top, which is exactly what the function's doc comment offers:

```cpp
auto m = ui::default_keymap<MyAction>();
m[(int)MyAction::Jump] = {keys::SPACE};   // add your own on top
```

**What happens.** Every editing chord is bound to Cmd and Ctrl identically:

```cpp
auto bind_chord = [&bind](std::string_view name, int key) {
    bind(name, {KeyChord{key, CMD}, KeyChord{key, CTRL}});
};
bind_chord("TextDeleteWordBack", keys::BACKSPACE);
bind_chord("TextWordLeft", keys::LEFT);
```

and `MOD_ALT` appears nowhere in the function. On macOS that is wrong twice
over, in the two places a user notices first:

| chord | `default_keymap` | macOS |
|---|---|---|
| Cmd+Left | previous word | start of line |
| Cmd+Backspace | delete word back | delete to line start |
| Option+Left | *unbound* | previous word |
| Option+Backspace | *unbound* | delete word back |

Option is the WORD modifier on this platform and Command is the LINE
modifier; the table collapses them into one. So the keymap a macOS app is
invited to start from binds the word chords to the line keys and leaves the
word keys dead — and because the bindings that ARE there work, the app looks
configured rather than mis-configured.

**The workaround.** Write the table out by hand (`src/input_mapping.h`), which
also means re-deriving which enumerator names exist (#255). A unit test
asserts the split explicitly — that Cmd is NOT on the word actions — because
the failure mode of drifting back toward the library's default is a chord that
still does something.


**Hanabi reference.** `src/input_mapping.h` (`WHICH MODIFIER MEANS WHAT. On macOS Option is the WORD modifier and Command`) — shipping key map is macOS-first. `src/input_mapping.h` (`bind(InputAction::TextWordLeft, {KeyChord{keys::LEFT, ALT}});`) — word motion is Option/Alt only. Tests: `tests/ui/composer_word_editing.e2e` (`Cmd+Left / Cmd+Right are the LINE ends on macOS`) — e2e verifies behavior.


**Minimal upstream fix.** Split the modifier by platform in `bind_chord`, or
offer `default_keymap_macos<InputAction>()` beside it. The values are already
platform-independent (`afterhours::keys` are GLFW codes); it is only the
modifier choice that is not.

CLASS: FOOTGUN

---

# The rail, the transcript, and three gaps under both (2026-08-26, perf/post-merge)

Found while bounding the transcript minimap's cost after `feat/event-model`
landed. All three are the same shape from different sides: **the library's unit
of "a thing on screen" is an Entity, and there is no cheaper unit** — so an app
drawing a 2px dot pays what an app drawing a button pays, and the only defence
is to draw fewer of them. Measured on `gabeochoa-mac-GRQ7Y259H4`, 1180x949,
against a 3,672-message transcript.

## #325 `with_debug_name` takes a `std::string`, so an INDEXED element costs a heap allocation per frame

```cpp
ComponentConfig &with_debug_name(const std::string &name) {   // component_config.h:478
```

Every element in a list needs a name of its own to be addressable — by the
scripted runner (`click_ui`, `assert_ui`), by the entity census, by anything
that has to tell row 6 from row 7. The only spelling the API allows is

```cpp
.with_debug_name("minimap_mark_" + std::to_string(i))
```

which is a `std::string` constructed, copied into the config, and destroyed,
**once per element per frame**. On the rail before this branch that was 2,263
of them a frame; it is 241 now, and the count went down only because the app
stopped drawing the other 2,022 elements. The names themselves are unchanged
work.

There is no `string_view` overload, no `(const char* prefix, int index)`
overload, no interning, and no way to compile the names out (this app cannot
anyway — its census and its whole scripted suite resolve them).

**What the library does about it for itself is the tell.** `imm::vlist`
(`imm_components.h:207`) names every row `"vlist_row"` — one static string,
no allocation, and therefore no way to address an individual row. The library
avoided the cost by giving up the property; an app cannot.

**The workaround.** Bound the number of named elements
(`hanabi::minimap::group_marks`). That is the right fix for the rail on its own
merits and it does nothing for the general case.

**Minimal upstream fix.** `with_debug_name(std::string_view)` storing into an
interned table, or an `(const char*, int)` overload that formats into a small
fixed buffer. Either removes the allocation without removing the property.

CLASS: PERF

## #326 `imm::vlist` virtualizes UNIFORM-height rows only, which is not what a real list is

`imm::vlist` takes one `row_height` and derives the window from it
(`imm_components.h:180-219`). Every list in this app has rows whose heights are
MEASURED and different: a transcript's items are bubbles, tool piles, thinking
folds, event rows and dividers; a digest's cards wrap their titles to two lines
or one; the minimap's slots are proportional to the items they stand for, so no
two are alike by construction.

So hanabi has hand-rolled the same window three times — the sidebar
(`docs/perf/SCROLL.md`), the digest cards (`docs/perf/DIGEST.md`), the
transcript (`main_pane_system.h` pass 1) — and each one re-derives the same
prefix-sum-of-measured-heights that the library would need in order to offer
this. Three copies of one algorithm is where the fourth one gets it wrong.

**Minimal upstream fix.** A second entry point taking a
`std::function<float(size_t)>` height accessor (or a span of precomputed
heights) and doing a binary search over the prefix sum, instead of a division.
The uniform case stays exactly as it is and remains the fast path.

**POSTSCRIPT 2026-08-26 (gap index) — the symbol is `imm::virtual_list`, not
`imm::vlist`.** Nothing in the pinned submodule (428047e) is named `vlist`;
`grep -rn vlist src/` returns three debug-name string literals
(`"vlist_top_spacer"`, `"vlist_row"`, `"vlist_bottom_spacer"`) and nothing else.
The function is
`afterhours::ui::imm::virtual_list(ctx, ep, count, row_height, render_row,
config)` at `plugins/ui/imm_components.h:159`. Recording it because the entry as
written is not greppable, and this is the entry a reader lands on when they go
looking for the primitive. Everything else in the entry checks out: the window
IS `offset / row_height` with a 4-row overscan (`:181-190`), so it is a division
and mixed heights cannot use it. Two details worth having beside the ask:

  * on frame one, when `viewport_size` has not been measured yet, it renders a
    fixed initial window of 61 rows (`:189-191`) rather than the whole list —
    so **#220**'s "build the WHOLE list once" is not true of `virtual_list`
    itself, only of a consumer windowing by hand;
  * the rows are keyed `mk(entity, i + 1)` — the SLOT, not the item — which is
    **#171** exactly, shipped. A per-row `text_input` in a `virtual_list` today
    silently re-points its state when the list scrolls.

Verified by reading the vendored source, not by running it.

CLASS: MISSING

## #327 There is no draw-only element, so a decorative mark costs a full Entity

A minimap mark is a rounded rectangle in a colour. It has no children, no
label, no text to measure, no layout of its own — and the only way to put one
on screen is `div()`/`button()`, which mints an Entity, a `UIComponent`, a
`UIComponentDebug`, a `ComponentConfig` and (for `button`) a click listener and
a `std::function` for `on_draw_fg`, then lays it out, every frame.

Measured, on the rail alone at 3,672 messages: **2,263 entities and 1.33 ms of
a 8.14 ms frame** to draw 2,263 dots that between them carry two facts each, a
rect and a colour. `afterhours_gaps.md` #138 records the per-widget allocation
cost that makes up most of it (~4.6 heap allocations per widget per frame);
this is the sentence above it — that the app had no choice but to pay it.

The parent already exposes `on_draw_fg`, so the *drawing* is reachable without
a child; what is not reachable is drawing N things at N positions the parent
does not know, and hit-testing a click to which of them. (Doing it inside one
parent's `on_draw_fg` means hand-rolling the hit test against the parent's rect
— which is exactly what the drag half of this rail already does, and
`afterhours_gaps.md` #286 is the coordinate-space trap that cost.)

**The workaround.** Draw fewer: group the marks until the count is bounded by
the rail rather than by the thread. Real win (2,263 → 241 entities, 8.14 →
3.52 ms), and it is a workaround — the remaining 241 still cost a full entity
each.

**Minimal upstream fix.** A "strip" primitive: one entity, a vector of
(offset, extent, payload), one `on_draw_fg` per item and a hit test the library
does against the same offsets. A scrollbar, a minimap, a sparkline, a timeline
and a tab underline are all this shape.

---

### #335 — Two independent view trees in one window is not a notion the library has: everything a "view" would scope is scoped to the process instead

The headline finding of building split view, and the one every other entry in
this block is a consequence of.

**What was wanted.** Two transcripts side by side, each with its own scroll
position, its own keyboard focus, its own find bar, and its own handle for a
test driver. In a toolkit with a notion of a view (an `NSView`, a Flutter
`Navigator`, an ImGui window) that is a container type: you make two, and
everything scoped to a view is scoped twice by construction.

**What happens.** There is no such container. A "pane" in afterhours is a
`div`, and every piece of state that ought to be per-view is a process-wide
singleton:

| what | where | how many can exist |
|---|---|---|
| the entity collection every UI system walks | `UICollectionHolder::get()`, a Meyers singleton (`ui_collection.h:20-54`) | one |
| focus, hot, active, `visual_focus_id`, `last_processed` | `UIContext<InputAction>` fields (`context.h:166-176`), one singleton component | one |
| the widget-identity map | `imm::existing_ui_elements` (`entity_management.h`) | one |
| pointer state | `UIContext::mouse` (`context.h:99-141`) | one |
| the drag-reorder gesture | `DragGroupState` (`components.h:698-723`), one singleton | one |

The macro `AFTER_HOURS_UI_SINGLE_COLLECTION` sounds like it is about this and
is not: it chooses whether UI entities share the *game's* collection, not
whether there can be two UI trees. `UICollectionHolder` has no second
instance, no constructor a caller can reach, and `get()` returns a reference to
a function-local static.

The one thing that IS naturally per-view is the only thing that is a component:
`HasScrollView` lives on the scroll entity, so two scroll views genuinely have
two offsets. That is what makes this survivable.

**What the app has to do instead.** All of it by hand, at the app layer:

- Per-pane VALUE state: `struct Pane` in `src/ecs/components.h` holds the
  session, load state, in-flight fetch, scroll latches and find state, and the
  renderer takes the pane it is rendering. Before this, split view rendered its
  second pane by MOVING the first pane's session out to a local, moving the
  second's in, rendering, and moving both back.
- Per-pane FOCUS: an `int focusedPane` on the app, a hit test against the
  pane's screen rect on mouse-press, and every keyboard site reading
  `app.pane()`. The library's `focus_id` is untouched by any of it, because
  what it means -- which WIDGET has the caret -- is a different question from
  which PANE has the keyboard, and the library has no vocabulary for the
  second.
- Per-pane MEMORY keys: everything hanabi keys by thread id had to become
  keyed by pane and thread, because the default split shows one thread in both
  panes. `ecs::model::pane_key`.
- Per-pane NAMES: see #337.


**Hanabi reference.** `src/ecs/components.h` (`struct Pane`) — per-pane app state exists. `src/ecs/main_pane_system.h` (`render_split(UIContext<InputAction>& ctx`) — split view renders two pane columns. Tests: `tests/ui/pane_split.e2e` (`Two panes, side by side, and only one of them has the keyboard`) — e2e covers split/focus.


**Minimal upstream fix.** Not "make the singletons plural" -- that is a rewrite
and most of them are correctly one per window. The smallest thing that would
have helped: let a subtree be declared a FOCUS SCOPE, and let `UIContext` hold
focus per scope rather than one id, with the app naming the active scope. That
is the piece the app cannot build on top (see #336), and every other item above
is state the app can own if it must.
### #305 — `text_area` re-wraps its whole text every frame, and measures through the raw backend instead of the shared cache

**What happens.** `text_area.h:228` (pinned 428047e):

```cpp
state.layout_cache.rebuild(display_text, wrap_width, line_height,
                           line_width);
```

Unconditional. Every frame, for every text area on screen, whether or not
anything about the wrap changed.

`TextLayoutCache::rebuild` (`text_layout.h:43`) is not a cheap thing to
repeat. It copies the text (`const std::string source(text)`), calls
`ui::detail::wrap_text_to_width`, and that (`text_selection.h:156-200`) cuts
the text into one `std::string` per word-or-space run, accumulates a
`current_text + pending_ws.text + chunk.text` CANDIDATE string at every word
boundary, and measures each candidate. Then `rebuild` walks the returned
lines calling `source.find(line, pos)` per line. An N-word line is O(N)
strings, O(N) measures and O(N²) bytes measured.

Two things make it fixable rather than inherent, and both are already in the
tree:

1. **`HasTextAreaState::needs_layout_rebuild(uint64_t)` exists and NOTHING
   CALLS IT.** `text_area_state.h:53-56`, with the comment "Check if layout
   needs rebuilding", next to a `last_layout_version` that `mark_dirty`
   maintains. `grep -rn needs_layout_rebuild vendor/afterhours` returns the
   definition and no call site. The guard was written and never wired up.
2. **The measure bypasses the cache.** `text_area.h:216-224` builds its
   `line_width` out of the raw backend call:

   ```cpp
   return measure_text(font_manager->get_font(font_name),
                       std::string(s).c_str(), resolved_font_size, 1.f).x;
   ```

   `measure_text_line` (`text_measure.h:22-31`) is the same measurement
   through `TextMeasureCache` — its own comment reads "Cache first,
   FontManager second" — and it is what the layout pass fills. So a text area
   pays full price for every probe AND builds a `std::string` per probe to
   reach a `const char *`, next to a cache that already holds the answer.

**Measured**, hanabi's composer standing still, `operator new` calls per frame,
headless 1180x949, `scripts/alloc_gate.sh` arithmetic:

| composer draft | allocs/frame |
| --- | --- |
| empty | 811 |
| one line, 130 chars (does not even wrap) | 1007 |
| six short lines | 1025 |

+196 allocations per frame, at 60Hz, for a draft nobody is touching. The
one-line figure is the one to look at: the text FITS, no wrapping is
performed, and the cost is entirely the machinery deciding that.

For scale against the widget it replaced: the same six-line string in a
single-line `text_input` costs 978/frame, so `text_input` has a smaller
version of the same problem (it re-measures its label every frame) and
`text_area` is worse.

**The workaround.** NONE IS POSSIBLE from app code, and the reason is worth
stating because it is the shape of several entries here: the expensive call is
made *inside* the widget function, on state the widget owns, with inputs the
caller has already handed over. There is no config flag that reaches it —
`with_word_wrap(false)` does NOT, because `text_layout.h:51` turns a zero wrap
width into `1e9f` rather than into the `max_width <= 0` early-out that
`wrap_text_to_width` itself provides. Every lever the caller has is on the
wrong side of the call.

What hanabi does instead is *bound* it: `scripts/alloc_gate.sh` grew a
`draft6` arm so the cost cannot grow further unnoticed, and the ceiling
carries a note saying it should come down when this lands.


**Hanabi reference.** Proof patch or spike, not shipped: `vendor_patches/305-text-area-wraps-every-frame.patch` (`Subject: [PATCH] text_area: wrap once per change, and measure through the`) — proof patch implements cached rebuild/measurement. `scripts/alloc_gate.sh::CEIL_DRAFT6` — gate bounds current allocation cost. Measurement/gate: `vendor_patches/305-text-area-wraps-every-frame.patch` (`one line, 130 chars 1007.0 824.0`) — before/after allocation counts.


**Minimal upstream fix.** Both halves are small, and both are PROVEN — the
patch is `vendor_patches/305-text-area-wraps-every-frame.patch`, applies
cleanly to 428047e, and takes the table above to 810 / 824 / 847. Hold the
inputs `rebuild` reads (text, wrap width, line height, font size, font name)
beside the cache and skip the call when none moved; and measure through
`measure_text_line`.

CLASS: PERFORMANCE

---

### #306 — `text_area` knows how many rows it wraps to and will not tell the caller, so an app that draws the BOX around the field has to read the widget's private-by-convention state

**What happens.** `with_auto_grow()` makes the FIELD's height follow its
content (`text_area.h:135-143`):

```cpp
const size_t rows = std::max<size_t>(1, state.layout_cache.line_count());
const size_t capped = config.text_area_max_lines > 0
                          ? std::min(rows, config.text_area_max_lines) : rows;
config.size.y_axis = pixels(capped * line_height + kVerticalPadding);
```

That is the right behaviour and it is exactly half of what a chat composer
needs. The other half is that a real composer is a field inside something the
APP draws — hanabi's is an outlined box on the window plane, with the border,
the corner radius and the send button beside it all owned by the app — and
none of that can size itself, because `rows` is a local and `ElementResult`
(`element_result.h`) carries `changed` and the entity and nothing else.

The result is a field that grows inside a box that does not: hanabi's
three-line draft drew lines two and three outside the outline, over the
transcript. It is not a layout bug in either place; the two sides simply have
no way to agree.

**The workaround.** Read `state.layout_cache.line_count()` off
`HasTextAreaState` after the call and use it to size the box on the NEXT
frame. It works and it is exact rather than approximate — which matters,
because the obvious alternative (count the rows app-side with the app's own
memoized wrapper) has to re-derive the widget's wrap width from its padding
rules and can disagree by a line, and a line of disagreement is a clipped
draft or a gap.

It is exact for a reason worth writing down: reading the count one frame late
puts the app on the SAME number `with_auto_grow` used, because auto-grow reads
that same cache at the top of the call, before the frame's `rebuild` writes
it. The lag is not a compromise, it is what makes the two agree.

But it is reaching into the widget's state component for a number the widget
computed and discarded, and every app that puts a text area inside its own
chrome will do the same reach.


**Hanabi reference.** `src/ecs/main_pane_system.h::layout_cache.line_count()` — composer reads widget row count. `src/ecs/main_pane_system.h` (`static float composer_field_h(size_t rows)`) — composer chrome sizes from row count. Tests: `tests/ui/composer_box_grows_with_the_draft.e2e` (`The composer box GROWS with the draft`) — e2e covers growth.


**Minimal upstream fix.** Return it. Either a field on `ElementResult` (it is
already the widget's channel back to the caller) or an accessor pair on
`HasTextAreaState` that says `rows()` and `capped_rows()` in so many words —
the value exists, it is one `size_t`, and the caller needs it in the same
frame the widget used it.

---

### #366 — `wrap_text_to_width` returns the lines it made and not where they came from, so highlighting a byte range means reconstructing the mapping

**What I was trying to build.** Find-in-conversation paints a band behind every
occurrence of the query. #51 already covers the big version of this ("nothing
will tell you where a byte range landed on screen"), and the workaround it
describes — call afterhours' own wrapper and redo the arithmetic — works. This
is the smaller, sharper thing that workaround runs into next.

**The mechanism.** `detail::wrap_text_to_width`
(`vendor/afterhours/src/plugins/ui/text_selection.h:219-241`) returns
`std::vector<std::string>`: the joined text of each wrapped line, and nothing
about where each line began in the input. It cannot be inferred by
concatenation, because a break CONSUMES the whitespace at it —
`wrap_runs_to_width` holds a `pending_ws` chunk and discards it when the next
word does not fit (`:172-200`, the `if (!current_text.empty() &&
measure_candidate(...) > max_width)` arm pushes the word without the pending
whitespace). So `join(lines) != text` in general, and the difference is a
variable number of bytes at a variable number of places.

That matters for exactly one case, and it is the case a user notices: a
multi-word query that STRADDLES a break. Search each wrapped line separately
and `"6 failures"` split across two of them is in neither, so it is counted
(the counting side scans the logical line) and never painted. In hanabi that
was docs/SEARCH.md S12, and the phrase was unpaintable at every scroll
position — not off-screen, just absent.

**The workaround, and why it is exact rather than a guess.** Match over the
whole logical line, then locate each wrapped line inside it with `find()` from
the end of the previous one (`src/ui/find_highlight.h`). That is sound because
of three properties I read out of `text_selection.h` before relying on them:

1. the wrapper only ever breaks between whitespace-separated chunks — the
   chunking loop at `:150-168` splits on runs of `' '` and never inside one;
2. a word wider than the line gets a line to itself rather than being cut
   (`:215-217`, and the greedy arm never splits `chunk.text`);
3. it never rewrites a byte — the trailing-whitespace branch at `:203-206`
   exists so that "hard-broken text round-trips byte for byte through
   `joined_text()`".

So every returned line is a contiguous substring of the input, in order.
The reconstruction is O(text) per label per frame on top of the wrap.


**Hanabi reference.** `src/ui/find_highlight.h` (`Match over the WHOLE line, then place the hit on the wrapped ones`) — highlight reconstructs wrapped-line offsets. `src/ecs/main_pane_system.h` (`find_highlight::paint_bands matches over the`) — find tally and paint rule match. Tests: `tests/ui/find_paints_a_match_that_wraps.e2e` (`A phrase broken across a soft wrap is counted AND painted`) — e2e covers straddling match.


**Minimal upstream fix.** Return the ranges alongside the lines, or as a
sibling entry point:

```cpp
struct WrappedLine { std::string text; std::size_t begin, end; };
std::vector<WrappedLine> wrap_text_to_width_spans(const std::string&, float,
                                                  MeasureFn&&);
```

The information exists inside `wrap_runs_to_width` already — the chunk loop
knows exactly how many bytes of whitespace it dropped — and it is thrown away
one line before it is returned. This is strictly cheaper for the library than
#51's `text_rects_for`, and it is most of what #51's consumers actually need.

CLASS: MISSING

---

### #350 — There is no way to ask the font atlas anything: not how full it is, not whether a measurement was complete, not whether a glyph was dropped

**What was wanted.** After #211 established that a full atlas makes
`measure_text` return a wrong number silently, a way for the consumer to tell a
correct measurement from a corrupt one — at the moment of the measurement, not
by inference.

**What happens.** Nothing is exposed. Not the atlas dimensions, not its
occupancy, not the glyph count, not the atlas image id, and — the one that
would cost the library a single `bool` — not whether the call that just
returned dropped a glyph. `fonsTextBounds` already knows: it walks
`fons__getGlyph` per codepoint and gets back NULL for the ones the atlas
refused, and then adds nothing to the advance for them and returns as if
nothing happened. The information exists inside the function and is discarded
on the way out.

The consequence is that the only detectable case is the terminal one. A width
of 0.0 for a non-blank string is unambiguous and can be caught. A width of
622.0 where the truth is ~5000 — measured, `--atlas-stress`, 124 pt — is
indistinguishable from a correct measurement of a shorter string, and it is the
case that actually corrupts a layout while looking normal.

**Why the obvious escapes do not work.**

- **Sanity-check the width against a per-glyph estimate.** The estimate would
  have to be looser than the widest real font variation and tighter than a
  partial drop, and those ranges overlap: dropping two glyphs from a
  forty-glyph string is a 5% error, and 5% is inside the honest variation
  between a lowercase run and an uppercase one.
- **Compare against a recorded measurement of the same string.** Only if the
  same string was measured before the atlas filled, which is exactly the string
  that is still correct — see the #211 postscript on why a canary cannot fire.
- **Count the glyphs and multiply.** `fonsVertMetrics` gives line height; there
  is no per-glyph advance accessor, so a consumer cannot sum one.
- **Ask before measuring.** No occupancy query, so a consumer cannot even
  refuse to measure into a full atlas.

**The workaround, and its cost.** `src/util/atlas_guard.h`: catch the terminal
case exactly (a non-blank string measuring zero, or non-finite) at every
measurement seam, and detect the CONDITION separately by asking for a glyph the
atlas has never held at a size it has never held — a fresh `(codepoint, size)`
pair, whose zero advance means the atlas can take no more rects. That probe is
run once per soak bucket and by `scripts/atlas_gate.sh`; it cannot be run per
frame, because each probe consumes a rect of the very resource it is measuring.

The cost is that the partial-drop window is bounded rather than closed: between
two probes, a measurement can be short and nothing will say so. That window is
the residue of a one-`bool` return value the library already has and does not
give back.


**Hanabi reference.** `src/util/atlas_guard.h` (`inline float check(std::string_view text, float px, float w)`) — guard catches impossible widths. `src/util/atlas_guard.h` (`inline bool probe(float px_hint, Measure&& measure)`) — probe detects atlas-full condition. Tests: `tests/unit/test_atlas_guard.cpp::test_zero_for_a_real_string_is_a_fault` — unit test covers zero-width fault. Measurement/gate: `src/main.cpp` (`--atlas-stress: FILL the glyph atlas on purpose`) — stress mode exercises overflow.


**Minimal upstream fix.** Either of two, both small. (a) An out-parameter or a
sibling entry point: `measure_text_checked(..., bool* complete)`, set false
when any `fons__getGlyph` returned NULL. Three lines inside the existing loop.
(b) `atlas_usage()` returning used/total, so a consumer can watch a slope and
act before the ceiling instead of after.

---

### #336 — Tab order cannot be scoped to a subtree, so Tab in a split pane walks out of it

**What was wanted.** Tab cycles the focusable widgets of the pane that has the
keyboard, and stops at its edge. That is what every split-pane editor does.

**What happens.** `UIContext::process_tabbing` (`context.h:416-451`) moves
focus by remembering `last_processed`, and `last_processed` is assigned for
EVERY entity the UI system visits (`systems.h:944`, in the per-entity system
body). So the tab order is the whole collection's walk order -- sidebar,
tab strip, both panes, the composer -- and the only per-widget control is
`SkipWhenTabbing` (`components.h:144`), which removes a widget from the order
entirely rather than assigning it to a group.

**The near miss, and it is worth naming because it looks like the answer.**
`FocusClusterRoot` / `InFocusCluster` (`components.h:147-148`) read exactly
like a scoping mechanism. They are not: their only use is in
`UpdateVisualFocus` (`systems.h:608-621`), which climbs from the focused entity
through `InFocusCluster` parents to find a `FocusClusterRoot` and draws the
RING on that ancestor. Navigation never consults either component. A cluster
changes where the ring is painted and nothing about where Tab goes.

**The workaround, and its cost.** None, and this is the one thing in the split
that is simply absent rather than re-implemented. hanabi's panes are focused by
CLICK and by chord, never by Tab, and Tab keeps its existing whole-window
meaning. That is a real hole for keyboard-only use and it is not one an app can
close: the order is derived inside the library from a walk the app does not
drive, so the app cannot renumber it, filter it, or bound it.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Honour the clusters that already exist -- when the
focused entity is inside a `FocusClusterRoot`, confine `process_tabbing`'s
`last_processed` to entities under that root, and wrap at its ends. The
components, the parent chain and the climb are all already there; only the
navigation half is missing.

CLASS: MISSING

---

### #337 — #147's consequence: with two panes, a debug name stops naming one widget, and the driver silently gets whichever one the entity walk reaches first

#147 says a scroll view is reachable from outside only by `UIComponentDebug::
name_value`. This is what that costs once an app has two of anything, which is
a different fact and one #147 could not have predicted.

**What happens.** `src/util/soak.h`'s `scroll_named` walks
`EntityHelper::get_entities_for_mod()` and takes the FIRST entity whose
`name_value` matches. With one transcript that was a lookup. With two, the two
scroll views are built by the same `mk` call on the same line of the same
function -- so they carry the same debug name, and which one the driver drives
depends on iteration order over the collection, which depends on entity id
allocation, which depends on which ids the retire sweep (`widget_epoch.h`) freed
last frame. It is not stable and nothing reports that it is not.

The failure is silent in the worst direction: the soak arm that scrolls the
transcript keeps passing, because scrolling the WRONG scroll view still returns
`true`.

**The workaround.** Two halves, and neither is satisfying:

1. The panes give their scroll views different names -- pane 0 keeps the name
   every existing driver and gate uses, pane 1 gets `transcript_scroll_2`. This
   means the app now maintains a name-per-pane table (`MainPaneSystem::
   scroll_name`) whose only purpose is to defeat an identity collision the
   library created.
2. `scroll_named` COUNTS its matches and prints to stderr when there is more
   than one, so the next widget that gets duplicated fails loudly instead of
   passing forever. That is a smoke alarm, not a fix.


**Hanabi reference.** `src/ecs/main_pane_system.h` (`static const std::string& scroll_name(int paneIndex)`) — pane scroll views get distinct names. `src/util/soak.h` (`AMBIGUOUS scroll target`) — driver warns on duplicates. Tests: `tests/ui/pane_split.e2e` (`assert_ui transcript_scroll_2 w=407`) — e2e verifies second scroll name.


**Minimal upstream fix.** #147's -- a stable addressable handle from
`imm::scroll_view` -- and this entry exists to raise its priority rather than
to ask for something else. An id the caller keeps is unique by construction; a
name the caller invents is unique only until the app renders the same code
twice, which is what any split, any grid and any repeated card does.

CLASS: FOOTGUN

---

### #351 — `fonsSetErrorCallback` exists, is exactly the hook a consumer needs, and is unreachable

**What was wanted.** To register a handler for `FONS_ATLAS_FULL` — the error
fontstash already raises, at the exact moment the atlas refuses a glyph.

**What happens.** The API is there and the call site is there:

```c
// fontstash.h:1131
added = fons__atlasAddRect(stash->atlas, gw, gh, &gx, &gy);
if (added == 0 && stash->handleError != NULL) {
    stash->handleError(stash->errorUptr, FONS_ATLAS_FULL, 0);
    added = fons__atlasAddRect(stash->atlas, gw, gh, &gx, &gy);
}
```

`stash->handleError` is null because nothing calls `fonsSetErrorCallback`, and
a consumer cannot call it either: it needs the `FONScontext*`, which lives in
`graphics::metal_detail::g_fons_ctx` — a backend-private static with no
accessor, no getter and no forwarding call. So the notification a consumer
wants is generated, checked against a null pointer, and thrown away, twice per
dropped glyph.

This is a strictly smaller ask than #350 and it is worth filing separately
because it needs no design at all: the mechanism is written, tested upstream,
and used by every other fontstash consumer. What is missing is one line in the
backend's init, or one accessor.

Note that the callback is also the only route to the OTHER half of what
fontstash offers here. The retry after `handleError` exists so a handler can
`fonsExpandAtlas` or `fonsResetAtlas` and have the glyph succeed on the second
attempt — the library is structured to let the consumer GROW the atlas at the
moment it fills, and afterhours' null handler forecloses that too. A consumer
that could register a handler would not merely be told; it could fix it.

**Why the obvious escapes do not work.**

- **Include fontstash and reach the context.** `g_fons_ctx` is in an anonymous
  translation-unit-local sense private to the backend header, and hanabi does
  not patch vendored code.
- **Create a second FONScontext.** It would not be the one `measure_text` and
  `draw_text` use, so its atlas is not the atlas that fills.

**The workaround.** `src/util/atlas_guard.h` — outside-in detection, with the
partial-drop hole #350 describes. `fonsSetErrorCallback` would close that hole
completely and cost one line.


**Hanabi reference.** `src/util/atlas_guard.h` (`fonsSetErrorCallback is out of reach`) — callback cannot be registered. `scripts/atlas_gate.sh` (`src/util/atlas_guard.h -- and that guard has the failure mode`) — gate proves fallback detector. Tests: `tests/unit/test_atlas_guard.cpp::first_fault()` — unit test covers reporting state. Measurement/gate: `src/main.cpp` (`detector fired:`) — atlas stress reports detector status. Proof patch: `vendor_patches/351-report-font-atlas-exhaustion.patch`; `tests/vendor_probes/source_contract_probe.cpp` verifies callback registration and log-once state, and `tests/vendor_probes/sokol_backend_smoke.mm` compiles the patched backend through `make verify-vendor-patches`.


**Minimal upstream fix.**

```cpp
// backends/sokol/backend.h, beside the sfons_create call
fonsSetErrorCallback(g_fons_ctx, [](void*, int err, int) {
    if (err == FONS_ATLAS_FULL) log_error("font atlas full: glyphs and their "
                                          "advances are being dropped");
}, nullptr);
```

Better still, expose `graphics::Config::font_error_callback` so the consumer
can decide between logging it, growing the atlas and asserting.

CLASS: BLOCKER

---

### #352 — The atlas is 2048×2048 and there is no configuration for it, so the ceiling is a build-time constant of the library

**What was wanted.** A larger atlas, on a consumer that knows it will need one
— a font-size slider, a CJK script, a document view.

**What happens.** `backends/sokol/backend.h:104` creates one `sfons` atlas of
2048×2048 R8 and the two dimensions are literals two lines above the
`sfons_create` call. `graphics::Config` has no field for them, `graphics::init`
takes no other route, and `sfons_create` is called before the consumer's first
line of code runs. It is the same shape as #210's complaint about sokol's
object pools and it has the same answer: the one place the number could be
chosen is inside the library.

4 MB is a good default and this entry is not asking for a bigger one. It is
asking for the number to be reachable. The measured ceiling on this machine is
about 26 glyphs at 400 pt, or the printable-ASCII set somewhere past 120 pt
(`--atlas-stress`); a consumer offering a font-size control has no way to know
that, and no way to move it.

**Why the obvious escapes do not work.**

- **`fonsExpandAtlas` / `fonsResetAtlas`.** Both need the `FONScontext` (#351).
- **Load fewer faces.** hanabi loads four and could load three; that buys 25%
  of a limit whose consumption is quadratic in point size.
- **Cap the font size in the app.** Which is a product decision made by a
  library constant, and the app cannot even discover what cap would be safe.

**The workaround.** None, and none needed yet: hanabi's four faces at fourteen
sizes, again at 2×/3×/4×/6×, fit with the reference measurement unchanged. The
cost is that this remains true only by luck, and the app now needs a gate
(`scripts/atlas_gate.sh`) to notice when it stops being true.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** `graphics::Config::font_atlas_width/height`,
defaulted to 2048, passed to `sfons_create`. Beside the pool sizes #210 asks
for, since they are the same request about the same struct.

---

### #338 — NOT A GAP: two subtrees built from the same call sites get disjoint widget identities, and the text measure cache is width-independent

Two walls that were expected while building split view and that the library
handles. Written down so the next person does not spend the afternoon proving
it again.

**Widget identity.** Rendering the same `render_transcript` twice in one frame
looks like it must alias every widget in it: same file, same line, same
column, same function, same loop indices. It does not.
`imm::mk` hashes `parent.id` FIRST (`entity_management.h:29`,
`pre_hash << parent.id << otherID << "file: " ...`), and hanabi's own
`widget_key` (`src/ui/mk.h`) mixes the same five facts. Each pane's subtree
hangs off its own column div, so every descendant's parent chain differs at the
root and every hash differs all the way down. Two panes need no id offsets, no
namespacing, and no per-pane integer bases. (#241 established the source-
location half of this for two row KINDS; this is the parent half, for two
copies of one kind.)

**Text measurement.** `TextMeasureCache` (`core/text_cache.h:29`) keys on
(text, font, size, spacing) and NOT on a wrap width -- it measures unwrapped
extents. So two panes at two different widths ask it the same questions and
share every entry. Measured on a 480-message thread at 1180x949: one pane
95.8% hit / 263 misses, two panes 97.6% hit / 272 misses. The second pane added
NINE misses.

The width-keyed thing that DID thrash was hanabi's own transcript render cache,
which memoizes wrapped heights and holds two widths per message -- two panes at
two widths is four. That is an app bug and was fixed app-side
(`ecs/transcript_render_cache.h`), not a library one.

**Hanabi reference.** Negative result: `src/ui/mk.h` (`h = mix(h, static_cast<std::uint32_t>(parentID));`) — parent id disambiguates subtrees. `src/ecs/transcript_render_cache.h` (`A SLOT IS A PANE AND A THREAD`) — cache is per pane/thread. Tests: `tests/unit/test_pane_memory.cpp::test_two_panes_at_two_widths_do_not_thrash` — unit test covers two-pane width behavior.


### #307 — NOT A GAP: `HasTextAreaState::line_index` does not move the caret, so an outside write that leaves it stale is unobservable

**The trap this entry exists to close.** `HasTextAreaState` carries a
`LineIndex` — the byte offset of every `'\n'` — that is NOT derived on demand.
`rebuild_line_index()` is called at every edit the widget makes
(`text_area.h:122, 513, 548, 559, 575, 587`) and never automatically. So an
app that writes a field's `storage` from outside — a history recall, a
slash-command expansion, a clear-after-send: hanabi does all three — leaves
the index describing the string that was there BEFORE.

That reads as an obvious latent bug, and the fix is one line. I wrote the one
line, and then wrote a scripted test for it, and the test passed WITHOUT the
fix. Twice, with the fixture rebuilt so the recalled text had a different line
structure from the text it replaced — which is the case where a stale index
must be wrong.

**Why it cannot be observed.** Nothing in `text_area` reads it.

The `LineIndex` consumers are all in `text_input/utils.h` —
`move_cursor_up`/`move_cursor_down` (`:334`, `:355`) and
`move_to_line_start`/`move_to_line_end` (`:403`, `:410`, `:411`) — and
**`grep -n` for any of them in `text_area.h` returns nothing.** The widget
binds Home and End to `move_to_visual_line_start`/`_end`
(`text_area.h:597-600`) with a comment saying why ("on a wrapped paragraph the
source version jumps to the far end of the paragraph rather than the end of
the row you can see"), and its vertical moves are visual for the same reason.
Every one of those reads `layout_cache`, which `rebuild()` refreshes from the
text on every frame (#305 — the same unconditional rebuild that costs so
much is what makes this harmless).

The remaining readers are `cursor_position_rc()` and `line_count()` on the
state itself, and neither has a call site inside `text_area.h` either.

**So: no workaround needed, and hanabi ships none.** `set_field` carries a
comment pointing here instead of a call, because the next reader will notice
the same asymmetry and reach for the same one-liner.

**Worth doing upstream anyway, and it is not the call.** The index is dead
weight in this widget: it is maintained at six sites, hashed and rebuilt by
`systems.h:38-40` for the `HasLineIndex` component as well, and read by
nothing the widget offers. Either drop it from `HasTextAreaState` and let
`text_input` keep it, or — better, if it is meant to stay — say in the type
that it is the SOURCE-line index and that the widget navigates by VISUAL rows,
so the next person does not spend an afternoon proving a negative.

**Related, and genuinely useful:** #257 records the other half of this — that
`init_state`'s callback runs every frame, not only at creation
(`component_init.h:700-708`), so the field re-seeds itself from the
`std::string&` the caller binds. #257 frames it as a trap (erasing from the
state alone does not survive the frame). The positive corollary is worth
saying out loud and is not written down anywhere: **assigning the bound string
IS a supported way to set a field's contents**, and unlike a hand-written
`storage` edit it goes through the widget's own path — cursor to the end,
`rebuild_line_index()` included. Verified empirically: hanabi's
`HANABI_REPLY_DEMO` hook does nothing but `replyDraft = d` and the text
renders on the next frame.

CLASS: NOT A GAP



**Hanabi reference.** Negative result: `src/ecs/main_pane_system.h` (`See afterhours_gaps.md #307 -- a stale index`) — set_field documents negative result. `src/ecs/main_pane_system.h` (`replyDraft = text;`) — outside writes go through bound string. Tests: `tests/ui/composer_history.e2e` (`expect_input_text composer_reply_input "msg1"`) — history recall works.

---

### #339 — NOT A GAP: `imm::divider` and `hsplit` already exist, and the hand-rolled version had exactly the bug the library's doc comment warns about

Filed as a negative result because the mistake is instructive and I made it.

A draggable pane divider was hand-rolled first: a 1px line, a 9px
absolutely-positioned grab strip over it, a `bool splitDragging` on the app,
and a ratio computed from the absolute mouse x against the pane origin. It
worked, and it had a defect -- grabbing the bar 3px off centre snapped the bar
under the cursor, because a position is not a delta.

`imm::divider(ctx, mk(parent), Axis::X, config)`
(`imm_components.h`, near `hsplit`/`vsplit`) is the library's own separator. It
sizes itself thin across the drag axis, sets `CursorType::ResizeH`, attaches a
`HasDragListener`, and returns the frame's MOVEMENT -- and its doc comment says
why: *"Delta, not position, so grabbing the bar off centre does not jump it."*
The hand-rolled one reproduced the exact bug the library had already written a
sentence about.

There is also `hsplit(ctx, mk(...), {pixels(200), expand(1)})`, which would
have replaced the pane row's manual width arithmetic. hanabi does not use it
here only because the pane widths are a persisted RATIO the app clamps itself,
and `expand()` gives the leftover rather than a share -- a small enough
difference that it is a preference, not a gap.

The lesson, and the reason this is an entry rather than a commit message: the
library's widget vocabulary is bigger than the parts an app happens to have
needed so far, and `imm_components.h` is worth reading end to end before
building any container-shaped thing.

CLASS: NOT A GAP



**Hanabi reference.** Negative result: `src/ecs/main_pane_system.h` (`imm::divider is the LIBRARY's separator, not a hand-rolled one`) — current split uses library divider. `src/ecs/main_pane_system.h` (`app.splitRatio = hanabi::clamp_split_ratio`) — divider delta updates ratio. Tests: `tests/ui/pane_split.e2e` (`The movement is a DELTA, not a position`) — e2e pins non-jumping drag.

---

### #340 — PERF: every styled text element re-wraps and re-allocates on the RENDER path, once per frame, and it is the single biggest allocation site in the app

**What happens.** `RenderImm::render_me` calls `draw_runs_in_rect`
(`rendering.h:1622`) for any label with spans, and that calls
`detail::wrap_runs_to_width` (`rendering.h:874` -> `text_selection.h:66`). That
function, per call:

```cpp
std::vector<TextRunLine> source_lines{TextRunLine{}};   // vector of vectors
...
source_lines.back().push_back(TextSpan{
    run.text.substr(start, end - start), run.color, run.weight});  // a string per span
```

A fresh `std::vector<std::vector<TextSpan>>` and a fresh `std::string` per
span, built from scratch, on the DRAW pass, for text that has not changed and
at a width that has not changed. There is no cache, no caller-owned scratch
buffer, and no way for the app to hand it one -- `draw_runs_in_rect` is a free
function with no state parameter.

**The number.** Measured with `HANABI_PROF_SITES=1` on a 480-message
transcript at 1180x949, 300 frames, rolled up by innermost frame:

| | one pane | two panes |
|---|---|---|
| `operator new` per frame, whole app | 3,836.9 | 5,399.1 |
| bytes per frame | 462,059 | 601,807 |
| `vector<TextSpan>::__init` under `render_me` | 367.0 /f | 811.0 /f (two passes) |

That one library call site is ~10% of every allocation the app makes at one
pane and grows linearly with panes. It is the top entry in the table below the
two `run_headless_screenshot` roll-ups, which are the render and layout passes
themselves.

**Why the app cannot fix it.** The wrap is on the draw side of a widget the app
builds declaratively; hanabi already memoizes its OWN wrapped heights
(`ecs/transcript_render_cache.h`) and that cache is on the measure path, which
this does not use. `use_batched` takes a different path (`rendering.h:2283`)
that calls the same function.


**Hanabi reference.** None — no app-side workaround is implemented.


**Minimal upstream fix.** Cache the wrap result on `HasLabel` beside the spans,
keyed by (rect width, font size, spacing) -- the same shape as the existing
`TextMeasureCache`, and invalidated by the same edits that already rewrite
`spans`. Failing that, hoist the two vectors to a thread-local scratch that is
cleared rather than freed; that alone removes the per-span string.

CLASS: MISSING

---

### #353 — A dropped glyph is not drawn either, so an unmeasurable string is also invisible, and the two failures are reported the same way: not at all

**What was wanted.** Having found #211's measurement corruption, to know what
the user actually SEES when the atlas is full — the assumption being that the
text draws and only the layout is wrong.

**What happens.** It is the other way round. `fonsDrawText` walks the same
`fons__getGlyph` and skips the quad entirely when it returns NULL
(`fontstash.h:1346-1360`), so a glyph the atlas refused is not rendered at all.
A string measured at 0.0 is also drawn as nothing. The two symptoms arrive
together and neither is reported, which is worth writing down because it
changes what the failure looks like from the outside: not "text laid out
wrongly" but "text absent, and the space where it should be laid out wrongly
too". A blank label reads as a data problem — an empty string, a failed fetch —
and nobody looks at the font stack.

It also removes the one diagnostic a consumer might have hoped for. If the
glyphs still drew, a capture would show text overlapping its neighbours and
somebody would file a rendering bug. They do not draw, so the capture shows a
clean, plausible, empty region.

**Why the obvious escapes do not work.**

- **Draw a fallback box for a missing glyph.** That is a decision inside
  `fonsDrawText`, and the consumer never learns a glyph was missing (#350).
- **Diff the capture against a reference.** hanabi does exactly this
  (`scripts/compare.py`) and it would catch this — but only for a screen that
  already has a frozen reference, and the condition arrives on user-authored
  text, which no reference covers.

**The workaround.** Detection only: `src/util/atlas_guard.h` faults on the
measurement, which at least names the cause on stderr before the labels start
disappearing. Nothing hanabi can do makes the glyph draw.


**Hanabi reference.** `src/util/atlas_guard.h` (`a string that measures short is LAID OUT short and a string that`) — source documents visible impact. `src/util/atlas_guard.h` (`raise(Fault::ZeroWidth, text, px);`) — zero-width text measurements are loud. Tests: `scripts/atlas_gate.sh` (`a normal render raised a glyph-atlas fault`) — gate catches ordinary-run faults. Measurement/gate: `src/main.cpp` (`detector fired:`) — stress reports detector status.


**Minimal upstream fix.** Whatever #350 or #351 provides covers this too — one
notification serves both halves. Failing that, rendering the substitute glyph
(codepoint 0, which fontstash already caches and which most fonts draw as a
box) instead of skipping the quad would turn an invisible failure into the
oldest visible one in typography.

CLASS: FOOTGUN

---

### #341 — PERF (ours, not the library's): what a second pane costs, and the two things I did not do about it

Filed here because the brief for this file now includes our own measured-but-
unfixed costs. Everything below is hanabi's code, not afterhours'.

**What a second pane costs.** 480-message thread in BOTH panes (the worst case
the default produces, since splitting opens the same thread twice), 1400x900,
900 frames, `-O2`:

| | one pane | two panes | ratio |
|---|---|---|---|
| frame CPU | 2.755 ms | 3.925 ms | **1.42x** |
| `operator new` /frame | 2,524 | 4,507 | 1.79x |
| entities | 609 | 728 | 1.20x |
| RSS | 41,952 KB | 43,328 KB | 1.03x |
| `transcript.pass1_measure` | 0.265 ms | 0.296 ms | 1.12x |
| `transcript.pass2_build` | 0.096 ms | 0.190 ms | 1.97x |
| `transcript.minimap` | 0.175 ms | 0.193 ms | 1.10x |

The shape is the one the windowing and memoization were built for: the MEASURE
pass is +12% for 2x the calls, because the second pane's measurements come out
of the render cache; the BUILD pass is the honest 2x, because those are real
widgets that have to exist. 1.42x for a second full transcript is the number
to hold onto, and it is a long way from 2x.

**UNFIXED (1): allocations are 1.79x, and that is the worst column.** Roughly
1,980 more `operator new` per frame. About 440/f of it is #340, which is the
library's. The rest is hanabi's second pass through `render_rich_body`
(`main_pane_system.h:7253`, 306 -> 430 calls/f) and `md_to_spans` (46 -> 93
calls/f) -- both of which rebuild span vectors per visible message per frame
from text that has not changed. THE FIX: memoize spans per (message id,
variant) in the transcript render cache, which already holds the wrapped
heights for the same keys and is already keyed per pane. Half a day, most of it
in getting invalidation right for a STREAMING message whose text changes every
frame. Not attempted on this branch because it touches the hottest path in the
app and the branch's job was the pane, not the painter.

**UNFIXED (2): the minimap is built per pane and is not windowed.**
`transcript.minimap` is 0.175 ms/f at one pane and it walks every message in the
thread, not every visible one -- it is a map OF the whole thread, so that is
inherent to what it draws, but it recomputes the whole map every frame from
data that changes only when the message list does. Two panes on one thread
compute the identical map twice. THE FIX: memoize the rail's geometry per
(thread, height) and invalidate on message count; two panes on one thread at
the same height would then share it outright. An hour or two. Left alone
because 0.19 ms is under 5% of the frame and I had no measurement showing
anyone feels it.

**CONSIDERED AND REJECTED: sharing the render cache between panes on the same
thread.** The obvious saving, since the default split shows one thread twice.
It does not work, and the reason is the interesting part: the cache is keyed by
WIDTH and holds two widths per message (a user bubble is measured at its
maximum text width and again at the hugged width that falls out of it). Two
panes are two widths the moment the divider moves off centre, which is four
through two slots -- so a shared cache does not save a lookup, it thrashes:
measured 40 misses over 10 frames against 4 cold ones
(`tests/unit/test_pane_memory.cpp`, `test_two_panes_at_two_widths_do_not_thrash`).
Keyed per pane it is 4 cold misses and nothing after. The saving was negative.

**CONSIDERED AND REJECTED: skipping the unfocused pane's build on frames where
nothing in it changed.** This is the retained-mode idea (#27) applied to one
subtree, and it cannot be done from the app: `imm::mk` is what keeps a widget
alive, and a frame that does not call it is a frame the widget goes unbuilt --
which after 90 such frames is a frame the retire sweep destroys it. "Do not
rebuild but stay alive" is not expressible.



**Hanabi reference.** Hanabi-owned performance finding: `src/ecs/transcript_render_cache.h` (`two panes have different widths the moment the divider is dragged off`) — explains rejected shared-cache idea. `src/ui/minimap_marks.h` (`inline std::vector<Slot> group_marks`) — minimap marks are grouped. Tests: `tests/unit/test_pane_memory.cpp::test_two_panes_at_two_widths_do_not_thrash` — unit test covers rejected cache sharing. Measurement/gate: `docs/perf/EVENTS.md` (`transcript.minimap @ 3,672`) — minimap before/after.

---

## #380 A custom command that retries cannot say why it timed out — the runner overwrites its reason, unless it is literally named `expect_text`

**What was wanted.** Two commands the generic vocabulary cannot express
(`require_thread <id>`, `click_link <id>` — see #232 and #51). Registering
them is the easy half: `PendingE2ECommand` carries a name and string args,
`register_unknown_handler` documents the ordering, and a hanabi system that
matches on the name works first try. The hard half is FAILING WELL, which is
the entire point of writing them.

**What happens.** A command that waits for a condition calls `cmd.retry()`,
and the runner's `E2ECommandCleanupSystem` gives up for it after
`MAX_FRAMES`. That give-up path composes the failure message itself:

```cpp
if (cmd.name == "expect_text" && !cmd.args.empty()) {
    error_msg = std::format("Text not found: '{}'. Visible: {:.200}", ...);
} else {
    error_msg = std::format("Command '{}' timed out after {} frames", ...);
}
```

One command name is special-cased into a good message and every other
command — including every custom one — gets "Command 'require_thread' timed
out after 30 frames", which names nothing. `cmd.error_message` set by the
handler before retrying is discarded: `fail()` overwrites it.

That matters more for a custom command than for a built-in, because a custom
command exists precisely to answer a question the generic ones cannot, and
the answer it has (which thread WAS open; which link ids WERE painted) is
the whole reason it was written.

**The workaround.** Do not let the runner's timeout arrive. Each handler
counts its own frames off `cmd.frames_alive` and calls `cmd.fail()` with its
own diagnosis at frame 24, six frames before the runner would:

```cpp
if (cmd.frames_alive < kGiveUpFrame) { cmd.retry(); return; }
cmd.fail(std::format("precondition not met: thread '{}' is not open with "
                     "content (open={}, messages={})", want, open, msgs));
```

It works, and every custom command in every app has to reinvent it, with a
magic number that has to stay under a constant it does not own.

**Minimal upstream fix.** Let a handler own its timeout message: either keep
`cmd.error_message` if the handler already set one (the cleanup system would
use it instead of composing), or a `cmd.fail_on_timeout("...")` that records
the message to use when the frames run out. Both are a couple of lines and
neither needs the library to know what a thread is.

CLASS: TEDIOUS

---

## #381 `load_scripts_from_directory` runs a whole suite in ONE process with no reset between scripts, so the only hermetic way to run N scripts is N processes from a shell script

**What was wanted.** Run `tests/ui/` and get one verdict per script, where
each verdict depends on that script and nothing else.

**What happens.** The runner offers exactly that entry point —
`load_scripts_from_directory(path)` — and it loads every script into ONE
process and one ECS world. Everything a script touches that is not an entity
is shared with the next script: `double_click_detail`/`triple_click_detail`'s
run counters, `key_release_detail`, the `VisibleTextRegistry`, the app's own
singletons, and whatever the app under test has persisted to disk by the time
script 2 starts. Nothing runs between two scripts. `reset_test_state` exists
and does most of what would be needed (`test_input::reset_all`, the click
detail resets, clearing the registry) but it is a SCRIPT command: the only
way to get it between two scripts is to write it at the top of all 105 of
them and hope nobody forgets, and it still cannot reset the application.

So hanabi does not use the directory mode. `scripts/run_ui_tests.sh` runs one
process per script, and now one home directory per script as well, because
"one process" is only half of it — the other half is the app's own state on
disk, which the library has no view of and cannot be expected to reset.

The cost of the library's version being the non-hermetic one is not
theoretical. Three separate investigations in one session, and 148 repeat
runs of one test looking for a race, went into failures that were understood
as order dependence. (They were not, in the end: the tests were failing on
stale pinned coordinates — #117, #232 — alone AND in suite. But "some earlier
script left state behind" was the reasonable first hypothesis every time,
precisely because the harness could not rule it out, and ruling it out took
running all 105 scripts one per process and diffing the verdicts.)

**The workaround.** A shell loop: one process per script, one HOME/cache/
token directory per script, and a `HANABI_UI_SEED` that shuffles the order so
an accidental dependence fails the suite rather than hiding in it
(`scripts/run_ui_tests.sh`, `make uitest-shuffle`, `make uitest-alone`). ~120s
for 105 scripts, and the per-script directories cost nothing measurable.

**Minimal upstream fix.** Have the directory mode run `reset_test_state`'s
body between scripts, and expose a `set_between_scripts(std::function<void()>)`
so the host can reset ITS state too. That makes the library's own entry point
mean what its name suggests. A `--shuffle`/seed on the runner would be the
other half.
### #308 — The e2e harness can assert a widget's geometry and its text and nothing about its appearance, so no scripted test can see a colour regression

**What happens.** `assert_ui <name> <prop>=<value>` is the scripted-UI
harness's general assertion, and `check_ui_property`
(`e2e_testing/ui_commands.h`) understands exactly six properties: `x`, `y`,
`w`, `h`, `hidden`, `text`. There is no `color`, no `bg`, no `border`, no
`focused-border`, nothing about `HasColor`, `HasBorder`, `HasRoundedCorners`
or the render layer — all components the harness's own queries could reach on
the entity it has already found.

That is the whole reason two visible regressions in hanabi's composer
(#262, #263) could be introduced by a change with 106 scripted UI tests
passing over it. Both are pure colour: a field painting a fill it should not,
and a focused field drawing no accent edge. Every geometric assertion in the
suite stayed green through both, because nothing moved.

**The workaround.** Shoot a PNG and read the pixels
(`scripts/composer_chrome_gate.sh`). It works and it is the right check for a
`--screenshot` app, but it is a whole capture, a Python dependency and ~4
seconds per assertion, to answer a question the harness is one match arm away
from answering in-process. And it can only be done for states a headless
capture can reach, so a colour that only appears after a click or a keystroke
— which is precisely what a focus colour is — needs a test-only env hook to
force it.


**Hanabi reference.** `scripts/composer_chrome_gate.sh` (`scripts/composer_chrome_gate.sh -- the composer's INTERIOR and its FOCUS`) — pixel gate is appearance workaround. `src/ui/field_chrome.h` (`The two bits of chrome text_area gets wrong`) — gate targets #262/#263. Tests: `scripts/composer_chrome_gate.sh` (`THE INTERIOR IS THE WINDOW COLOUR`) — checks background color. Measurement/gate: `scripts/composer_chrome_gate.sh` (`COLS = (600, 700, 800, 900, 1000)`) — pixel sample columns.


**Minimal upstream fix.** Add the colour components to
`check_ui_property`, comparing against a parsed `r,g,b,a`:
`bg=23,23,35`, `border=0,122,204`, and a `border-thickness`. The entity is
already in hand at that point in the function; the components are public; the
assertion is a comparison. It turns a four-second screenshot into a line of
script, and it is the difference between a UI suite that tests behaviour and
one that tests appearance too.

CLASS: MISSING

---

### #370 — NOT A GAP: there IS a cached measure path (`ui::measure_text_line` / `TextMeasureCache`), and hanabi's highlight code bypasses it

Filed so the next person does not re-derive this as a library complaint. It is
ours.

**What it looked like.** `find_highlight.h` measures text a lot: a wrap
(a measure per candidate line) plus two measures per painted band, for every
label the find bar highlights, every frame. The obvious suspicion is that
afterhours makes you pay the font engine every time.

**What is actually there.** `vendor/afterhours/src/plugins/ui/text_measure.h`
exposes `ui::measure_text_line(text, font_name, size, spacing)`, which goes
`TextMeasureCache` first and `FontManager` second — the same path
`AutoLayout::get_text_size_for_axis` uses, "so the two answers cannot drift".
`ui::wrap_text` (`:37`) is `wrap_text_to_width` already wired to it.
`TextMeasureCache` is registered as a singleton by
`ui/utilities.h:122-138`, so it is live in every app that brings up the UI
plugin.

hanabi calls the RAW `afterhours::measure_text(font, str, px, spacing)`
instead, with a font handle from `fm->get_active_font()`. Verified in the
profile: with the find bar open on the 480-message fixture the cache reports
`5565 hits / 263 misses` against `4803 / 263` with it closed — 1.3 extra
lookups a frame, while `find.paint` alone runs 6 times a frame and each of
those does a wrap plus two measures per band. Find's measuring is not in the
cache's numbers because it never asks the cache.

**What to do about it.** Route `find_highlight.h` and `snippet_highlight.h`
through `ui::measure_text_line` / `ui::wrap_text`. The blocker is small and
real: those take a font NAME and hanabi holds a `Font` handle, so it needs the
active font's name plumbed to the call site. Measured upside is bounded —
`find.paint` is 0.34 ms/frame of a 5.45 ms frame (see #365), and the wrap
inside it is most of that — so this is worth doing when someone is next in
these files, not on its own.

CLASS: NOT A GAP (app-side)



**Hanabi reference.** Negative result: `src/ui/find_highlight.h` (`afterhours::measure_text(font, s.c_str(), fontPx, kSpacing).x`) — find highlight still uses raw measure. `src/ecs/main_pane_system.h::afterhours::ui::measure_text_line` — other paths use cached helper. Measurement/gate: `docs/perf/TEXT.md` (`hanabi's own measuring still goes around the library's cache`) — app-side nature is documented.

---

### #365 — PERF (ours, FIXED): find-in-conversation re-normalized every loaded message every frame, and was 45% of the frame

**Measured.** `HANABI_PROF=1 HANABI_SOAK=600`, 1180x949, the 480-message
`rbig` fixture (40 messages loaded — `LoaderSystem::kMessagesWindow`), CPU
time, gabeochoa-mac. Two `hanabi::prof::Scope`s were added for this and they
ship (`find.collect`, `find.paint`) — the find path was invisible to the
profiler, which is part of why a doubling of the frame went unnoticed:

```
                        find bar CLOSED     find bar OPEN
  FRAME (cpu)              2.807 ms/f         5.452 ms/f     +94%
  find.collect                   --           2.430 ms/f     44.6% of frame
  find.paint                     --           0.341 ms/f
  allocs                    2660 /f          14788 /f        +5.6x
```

**The mechanism.** `MainPaneSystem::collect_matches` runs every frame the bar
is open, over every loaded message, and for each one calls `paintable_lines`,
which is `strip_inline_md(redact_secrets(m.text))` — two whole-string
allocations — plus `md_to_spans(line).visible` per line. Nothing is cached.
`paint_query_for` then re-parses the query per row, and `message_has_match`
normalizes the same message again on the render side.

It is a LEVEL, not a slope, so every gate in `make test` reads it as perfectly
flat and green: `soak-gate` measures trend, `scaling-gate` measures the ratio
between two catalog sizes with no find bar open, and `alloc-gate` has three
fixtures and none of them opens find. This is the same blind spot
`soak_gate.sh`'s own header describes for leaks, in the other axis.

**How it scales.** 2.43 ms for 40 loaded messages is 0.061 ms per message per
frame. The window is 40 today, but "load older" appends and the transcript
keeps everything it has fetched, so a reader who scrolls back through a long
thread walks that number up linearly — at 480 loaded it extrapolates to ~29 ms
a frame, which is 34 fps with the find bar open and nothing else happening.
(Extrapolation, not a measurement: the headless path does not run the
load-older prefetch far enough to hold 480.)

**Fixed on `perf/find-memo`.** `src/search/find_memo.h` keeps normalized
paintable lines and ordered `(message, logical-line, byte-offset)` matches in a
per-pane, 16,384-message-bounded memo. The key covers the transcript content
version, exact message content signature, parsed operator AST, ASCII fold
policy, pane width, long-message fold policy, reasoning visibility, fold-state
revision, role and event kind. Append/prepend sync reuses unchanged message
entries; streaming and replacement paths advance `Pane::transcriptVersion`.

At 480 / 3,672 / 14,688 messages, `find.collect` fell from 3.3937 / 25.6338 /
109.6700 ms/f to 0.0152 / 0.0971 / 0.4332 ms/f. The stable evidence is the
operation and allocation counts: repeated unchanged frames visit zero messages,
whole-result hit rate is 99.33%, and open/closed allocation ratios fell from
5.368× / 28.616× / 75.137× to 1.451× / 1.526× / 1.527×. Disabling the
whole-result hit path makes `scripts/find_gate.sh` fail at 797.6 and 6,099.8
rows/frame for its 480/3,672-message arms.

**Hanabi reference.** `hanabi::find_memo::Memo::collect` in
`src/search/find_memo.h`; `MainPaneSystem::collect_matches` and
`Pane::note_transcript_change`; `tests/unit/test_find_memo.cpp` and
`scripts/find_gate.sh`. The unit test covers query/content/operator/fold/width/
event-kind changes, append, prepend, two panes and the bound. The gate is in
`make test`.

**Rejected.** Caching only the count loses ordered stepping and cannot drive
paint. Keying only by message id serves stale text after streaming or refetch.
A global thread cache lets two panes with different queries and widths evict or
reuse one another's answer. A fixed result cap breaks the whole-loaded-thread
count law.



**Hanabi reference.** Hanabi-owned performance finding: `src/ecs/main_pane_system.h` (`static std::vector<std::string> paintable_lines`) — find collection builds paintable strings. `src/ecs/main_pane_system.h` (`hanabi::prof::Scope _pfind("find.collect")`) — find collection is profiled. Measurement/gate: `docs/SEARCH.md` (`find.collect | — | 2.430 ms/f`) — docs record find.collect cost.

---

### #367 — PERF (ours, FIXED HERE): opening Cmd+Shift+F parsed the entire disk cache on the UI thread — 370 ms at a 2000-thread cache

**Measured.** `tools/bench_search_index.cpp` (new), 2000 threads x 40
messages = 19 MB of cache, `CLOCK_THREAD_CPUTIME_ID`, gabeochoa-mac:

```
                          before                     after
  open the panel          370.5 ms, 2000 reads       0.2 ms, 0 reads
  one frame after         --                         1.1 ms, 8 reads
  to full coverage        370.5 ms in one frame      324.3 ms over 250 frames
```

**The mechanism.** `SessionSearchSystem::build_index` called
`api::disk_cache::load_transcript` — a full nlohmann parse — once per session
not already in the in-memory LRU, synchronously, on the frame the panel opened.
No cap, no budget, no thread. It grows with the user's history forever, and
"once per opening" was undercut by `close()` and the chord both resetting the
flag, so it was once per open, every open.

**The fix.** `src/search/session_corpus.h`. The panel opens having done no
disk I/O at all, and each frame it is open reads `kDeepenPerFrame = 8` more
transcripts, newest thread first. The total work is unchanged; no single frame
pays it. `coverage_note` was already the sentence that says how much of your
history was read, so the partial state needed no new UI — it just carries an
ellipsis until it is done.

**Gated on a COUNT, not a clock** (`test_opening_the_panel_reads_nothing_from_
disk`): opening a 500-thread corpus performs zero loader calls, one frame
performs exactly eight whatever the catalog size, no thread is read twice, and
it converges. A time budget would read a different number of files on a loaded
box than on a quiet one, which is a UI whose behaviour depends on what else the
machine is doing — and it could not be gated on a shared box at all.

**Rejected, with reasons.**
- **A cap on how many transcripts are indexed.** Cheapest fix, and it makes
  Cmd+Shift+F permanently blind to old threads. `session_index.h` opens by
  naming that exact failure: "a search that quietly misses half your history
  and reports '3 results' is the failure mode this file exists to avoid".
- **Build it off-thread.** `api::disk_cache` has no ownership story for a
  reader racing a save: `save_transcript` bumps a generation the reader would
  have to re-check per file, and the whole corpus can be invalidated under one.
  A worker would need that contract designed first, and spreading the reads
  gets the responsiveness without it.
- **Keep the corpus across opens** instead of rebuilding. Tempting — reopening
  the panel re-reads everything. The disk cache has a generation counter
  (`invalidate_content_index`) that would cover the file half, but the LRU has
  nothing equivalent, so a thread read since the last open would be stale with
  no way to notice. Worth doing after the LRU grows a generation; not before.



**Hanabi reference.** Hanabi-owned performance finding: `src/search/session_corpus.h` (`inline constexpr std::size_t kDeepenPerFrame = 8;`) — search deepens corpus in slices. `src/ecs/session_search_system.h::corpus_.deepen(hanabi::search::kDeepenPerFrame` — UI uses bounded deepening. Tests: `tests/unit/test_session_index.cpp::test_opening_the_panel_reads_nothing_from_disk` — unit test pins zero reads on open. Measurement/gate: `docs/SEARCH.md` (`opening the panel | **370.5 ms**, 2000 disk reads | **0.2 ms**, 0 disk reads`) — before/after measurement.

---

### #368 — PERF (ours, NOT FIXED): the sidebar's deep filter reads a file per thread on the first frame of every new query — 165 ms at a 2000-thread cache

**Measured.** Same bench, same fixture, `CLOCK_THREAD_CPUTIME_ID`, three
alternating runs each:

```
  sidebar filter, first frame of a query    ~165 ms   (0.082 ms/thread)
  the same query again (memoized)             0.05 ms
```

**The mechanism.** `api::disk_cache::content_matches` is called for every
session whose TITLE did not match, on every frame a sidebar query is live
(`sidebar_system.h`, `render_folder`). It is memoized on `(id, query)` and the
memo is correct, so the steady state is 0.05 ms — but the memo is keyed on the
WHOLE query, so the first frame of each new query pays the full walk. The
narrowing shortcut only helps when the new query CONTAINS the old one as a
substring; the first keystroke of any query, and every backspace past a
previously-typed prefix, is a cold walk of the catalog.

So the shape a user feels is: type `r`, hitch; type `re`, cheap; backspace,
hitch. At 2000 cached threads the hitch is ~165 ms — ten frames.

**Not a regression from this branch.** The same walk with the old
lowercase-the-whole-file scan measures ~184 ms against the new
`json_field_contains` scan's ~164 ms (three alternating runs of each), so
searching the values rather than the document is about 11% cheaper as well as
correct. The cost was always there.

**The fix, and its size.** An inverted index would be the real answer and is
out of proportion. The cheap 80%: keep a per-thread lowercased BODY in memory
(the memo already holds a bool per thread; holding the text instead costs the
cache's size in RAM, ~12 MB for the 19 MB fixture) so the walk is a substring
scan over memory rather than a file read per thread, and the first frame of a
new query costs what the memoized one does. A day, and it wants the
`Depth::Windowed` bookkeeping from docs/SEARCH.md S2 so it does not quietly
hold half a thread and call it the thread. Alternatively: pay it off the frame
thread — same ownership problem as #367's rejected arm.



**Hanabi reference.** Hanabi-owned performance finding: `src/ecs/sidebar_system.h` (`api::disk_cache::content_matches(s.id, q)`) — sidebar still calls content search. `src/api/disk_cache.cpp` (`If the new query CONTAINS the old one`) — memo optimizes narrowing only. Tests: `tests/unit/test_data.cpp::test_content_search_memo_is_not_stale` — unit test covers invalidation. Measurement/gate: `docs/SEARCH.md` (`its deep filter costs 165 ms on the first`) — docs record open perf issue.

---

### #369 — PERF (ours, NOT FIXED): a sidebar search plus "Show N more" un-virtualizes the list, and the two tests that would catch it never overlap

**The mechanism, in our code.** `SidebarSystem::row_window` returns the whole
range whenever the rows are not uniform in height, and a search result carries
a snippet under its title, so a live query means no windowing at all:

```cpp
return render_group(..., row_window(parent, limit, q.empty()));
```

The argument for that is written next to it — `visible_limit` already caps a
searched list at roughly two viewports, so there is "nothing here to win". It
holds until the user clicks the expander: `visible_limit` returns the whole
total once `__more_<key>__` is in `collapsedFolders`. Compose the two — search,
scroll, click `sb_show_more` — and every matched row is built every frame,
which is the exact defect `sidebar_show_all_is_still_virtualized.e2e` exists to
prevent. That test never types a query;
`search_does_not_draw_the_whole_catalog.e2e` never clicks the expander.

**The number, from this repo rather than from me.** The unvirtualized sidebar
at a 2000-session catalog is 6645 entities and 17.2 ms a frame against 461 and
1.55 ms for the same list windowed — measured when row virtualization was
added, recorded in `sidebar_system.h`'s own comment above `row_window`. This
path walks straight back into it. I did not re-measure it; the entry is a code
path plus the repo's own figure for that code path.

**Why it is not a one-liner, and where afterhours comes in.** The reason
`row_window` bails is that the arithmetic is `offset / kRowHeight`, and a
search row is a different height from a plain one. Windowing a
variable-height list means knowing how tall a child WOULD be before building
it, which is **#224** — nothing in the library will answer that, and #326's
`imm::vlist` only virtualizes uniform rows, which is why this app hand-rolled
the same window three times (sidebar, transcript, digest). The app-side fix
that does not need any of that is narrow: a searched list has exactly TWO row
heights, so window it with the taller pitch while a query is live, or refuse to
uncap while one is. Half a day with a test that does both halves —
type a query AND click the expander — which is the test neither existing script
is.



**Hanabi reference.** Hanabi-owned performance finding: `src/ecs/sidebar_system.h` (`if (!uniformHeight) return w;`) — search rows disable row-window arithmetic. `src/ecs/sidebar_system.h` (`Presence = expanded.`) — Show N more uses expanded sentinel. `src/ecs/sidebar_system.h` (`app.collapsedFolders.insert(more_key(key, moreKeyScratch_));`) — click expands list. Tests: `tests/ui/sidebar_show_all_is_still_virtualized.e2e` (`"Show N more…" must not cost N rows.`) — test covers Show N more without query. Measurement/gate: `docs/perf/GATES.md` (`one they clicked "Show N more…" on, and that list cost 17.2 ms of CPU a frame`) — row-window regression cost.

---

### #372 — PERF/HONESTY (ours, NOT FIXED): the sidebar truncates its search results and there is no header to say so

**The mechanism.** `visible_limit`'s justification for capping a searched list
is written in the code: "the count in the header is still the true number of
matches, so the search still ANSWERS with all of them; it just does not draw
all of them". The catch-all group — which is where every unfoldered thread
lands, and therefore where nearly every search result lands — is rendered
headerless (`render_folder(..., headerless=true)`). There is no header, so
there is no count. `Show N more…` does exist and says the number, and it sits
at the bottom of two viewports of rows by construction, so it is never on
screen without scrolling.

The net effect: type a query that matches 300 threads, see 40, and nothing
anywhere says 300.

**Why it is not the one-line fix it looks like.** The count is known inside
`render_folder`, which runs AFTER the search row has already been built that
frame — the search box is at the top of the panel and the groups are below it.
Putting "40 of 300" next to the box means either (a) reading last frame's
number, which the sidebar already does for `HANABI_ROW_AUDIT` (`rowsMatched_`,
`rowsRendered_`) and which is a real precedent but currently gets overwritten
per GROUP rather than accumulated across them, or (b) hoisting the member
collection above the search row's build, which reorders a function that also
owns sorting, pinning and the drag pass. (a) is right; it needs
`rowsMatched_`/`rowsRendered_` turned into accumulators reset once per frame
rather than once per group, and a label that is not a test-only audit. Two
hours, and the risk is entirely in the accumulator's reset point.



**Hanabi reference.** Hanabi-owned performance finding: `src/ecs/sidebar_system.h` (`/*headerless=*/true`) — catch-all group is headerless. `src/ecs/sidebar_system.h` (`The count in the header is still the true number`) — visible_limit rationale depends on header count. Measurement/gate: `docs/SEARCH.md` (`The sidebar truncates silently`) — docs record issue as open.

---

### #371 — SMALL (ours, NOT FIXED): four leftovers in the search subsystem

Each is real, each is minor, and none is worth a commit of its own. Recorded so
the next person in these files can take them on the way past. From
docs/SEARCH.md S13.

1. **Two snippet cutters with the same bug.** `session_index.h::snippet_around`
   (32 bytes of context) and `snippet_text::extract` (22) share a
   "don't start mid-word" trick and share its flaw: the trim only fires if
   whitespace exists between the window start and the match, so a hit inside a
   long token still yields the `…imization` the comment says it prevents. Both
   also cut at raw byte offsets with no UTF-8 boundary check, so a snippet can
   begin or end mid-codepoint and render as a replacement glyph. One function
   with a context parameter, plus a boundary walk. An hour.

2. **The sidebar reads the disk cache on a backend where caching is off.**
   `content_matches` is called unconditionally; cache WRITES are gated on
   `backend_label != "mock"`. The mock's cache dir is the same flat directory
   an http backend with an empty base URL writes to, so the mock's sidebar can
   match files another backend left behind. Harmless in the suite (the mock
   never writes any), confusing on a dev machine that has run both. The fix is
   to gate the READ on the same predicate as the write — but that predicate
   lives in `loader_system.h` and `disk_cache` does not know about backends, so
   it wants a small "is this cache live" flag set at startup rather than a
   condition copied to a second place.

3. **No Unicode anywhere.** All four matchers fold `A-Z` only and nothing
   normalizes, so `Café` does not match `café`. This is a decision, not an
   oversight — the alternative is a normalization table in a repo with no ICU —
   but it is undocumented outside the fold functions themselves. (The
   locale-dependent `std::tolower` that used to be in `disk_cache.cpp`'s scan
   is gone; the remaining three were already ASCII.)

4. **`find_nav::advance(i, n, Step::None)` returns 0, not `i`.** Asserted as
   intended in `test_find_nav.cpp:62`, harmless because the only caller
   short-circuits on `Step::None` before reaching it, and a footgun for the
   second caller. Ten minutes, and the test says the wrong thing rather than
   the code doing it.



**Hanabi reference.** Hanabi-owned performance finding: `src/search/session_index.h` (`inline std::string snippet_around`) — first snippet cutter exists. `src/ui/snippet_text.h` (`inline std::string extract`) — second snippet cutter exists. Tests: `tests/unit/test_find_nav.cpp` (`CHECK(advance(1, 3, Step::None) == 0);`) — test asserts Step::None behavior.

---

### #373 — Ideas considered for the search subsystem and rejected, with reasons

Recorded so nobody spends the afternoon I would have.

- **Make find's tally scroll-dependent** (count only what the window built), so
  "tally equals bands" becomes true. Rejected: "3 of 47" means 47 in the
  document in every editor there is, and a number that changes when you drag a
  scrollbar is a worse lie than the one being fixed. The claim was wrong, not
  the count. docs/SEARCH.md S1.

- **Count find's matches over the WRAPPED lines**, which is what "count what
  you paint" literally says. Rejected: it makes the tally a function of the
  window width — resize and "of 47" changes — and it needs the layout in the
  counting path, which has neither a rect nor a font. The mapping goes the
  other way instead (S12, #366).

- **Parse the disk cache off-thread** for Cmd+Shift+F. Rejected for now: no
  ownership story for a reader racing a save. See #367.

- **A true result total for Cmd+Shift+F** ("showing 6 of 213"). Rejected: it
  means scanning every body to its end, every frame, over a corpus that reaches
  tens of megabytes — for a number whose only use is to tell you to keep
  typing. It asks for seven and says "more" instead.

- **Give the sidebar's deep filter a real inverted index.** Out of proportion
  to a list filter; the cheap 80% is an in-memory lowercased body per thread.
  See #368.

- **Search tool OUTPUT.** The sidebar's comment claimed it did. It cannot:
  `to_json(const Message&)` never writes `tool_result`, so it is not on disk to
  search. Persisting it would grow the cache by roughly the size of the
  transcripts again and wants a decision about the cache cap first, not a
  search change.



**Hanabi reference.** Negative result: `docs/SEARCH.md` (`Fixed the other way round from the entry's suggestion.`) — docs preserve rejection of wrapped-line totals. `docs/SEARCH.md` (`Rejected: a **cap**`) — docs preserve rejection of capped corpus. Tests: `tests/unit/test_data.cpp` (`CHECK(!api::disk_cache::content_matches("quota-42", "xylophone"));`) — tool output not searchable.

---

### #405 — A trackpad and a mouse wheel arrive as the same float, so an app can match the platform's per-notch distance or track a finger 1:1, never both

**What was wanted.** Wheel scrolling that feels like a Mac. Two separate things
hide inside that sentence: a mouse detent should move about three lines, and a
two-finger drag on a trackpad should move the content exactly as far as the
fingers moved. Both are conventions, both are checkable against any native
scroll view, and neither is reachable from here.

**What happens.** macOS distinguishes the two at the event
(`NSEvent.hasPreciseScrollingDeltas`), and the vendored sokol reads that flag
and then throws it away, keeping only a fixed scale on one of the branches
(`vendor/sokol/sokol_app.h:6078`):

```objc
float dy = (float) event.scrollingDeltaY;
if (event.hasPreciseScrollingDeltas) {
    dx *= 0.1;
    dy *= 0.1;
}
```

Downstream there is one number and one multiplier
(`src/plugins/ui/components.h:519`, `src/plugins/ui/systems.h:2097`):

```cpp
float scroll_speed = 20.0f;         // Pixels per scroll wheel notch
...
scroll_state.scroll_target.y += direction * wheel_v.y * scroll_state.scroll_speed;
```

So work the arithmetic through, in pixels the reader sees:

| input | what AppKit sends | after sokol | x speed 20 | what it should be |
|---|---|---|---|---|
| trackpad / Magic Mouse, 10 pt of finger | 10.0 (precise) | 1.0 | 20 px | 10 px (1:1) |
| third-party wheel, one detent | ~1.0 (lines) | 1.0 | 20 px | ~57 px (3 lines) |

VERIFIED and NOT VERIFIED, separately, because the difference matters. The
precise-delta row is verified: the 0.1 is in sokol's source above, the 20 is in
`components.h:519`, and the multiply is `systems.h:2097`. The wheel row's
`~1.0` is NOT verified here — there is no wheel mouse on this machine and #172
means the injector cannot stand in for one, since it writes the post-sokol float
directly and never sees `hasPreciseScrollingDeltas` at all. It is AppKit's
documented behaviour for non-precise events (`scrollingDeltaY` in lines) and it
is what the 3-line convention is measured against; if a detent turns out to
deliver 3.0 rather than 1.0 on real hardware, the wheel is already right and
only the trackpad's 2x remains. Either way the two rows want different
multipliers, which is the gap.

The same 1.0 has to become 10 and 57. A trackpad therefore runs at twice the
finger and a wheel at a third of the platform's step, and moving `scroll_speed`
to fix either makes the other worse by the same factor. On Apple's own hardware
— trackpad and Magic Mouse both send precise deltas — the 2x is the one a reader
actually gets.

There is a second cost on the same seam. hanabi eases the rendered offset toward
the wheel's target so a detent glides instead of jumping. macOS momentum is
ALREADY smooth, so on a trackpad that easing is a second smoothing on top of the
OS's, and it shows up as the content lagging the fingers by a few frames rather
than sitting under them. The fix is to ease line deltas and pass precise ones
straight through — which needs the flag that was discarded.

**Why the obvious escapes do not work.**

- **Pick a better single number.** There is no single number: the two wants are
  10 and 57 through the same multiplier. 20 is not a compromise anyone chose, it
  is afterhours' default sitting between them.
- **Infer the kind from the delta.** Line deltas are whole numbers and precise
  deltas usually are not, so "integer means wheel" almost works — and a trackpad
  flick that happens to deliver exactly 10.0 points then scrolls three times too
  far, at the fastest moment of the gesture, which is the worst place for it.
  A heuristic that is wrong only when moving fast is worse than a constant.
- **Read the flag from the app.** `NSApp.currentEvent` is stale by the time the
  frame callback runs, so the app would have to install its own
  `NSEvent` local monitor and shadow-track every scroll event alongside sokol's
  handler — a second input path, in ObjC++, that no harness here can drive
  (#172), to recover a boolean the first path already had.

**The workaround, and its cost.** `HANABI_SCROLL_SPEED` in
`src/util/scroll_prefs.h`, so the number is a per-machine setting rather than a
rebuild, with the default left at 20 so nothing moves for anyone who does not
set it (`tests/ui/wheel_notch_distance_is_settable.e2e`). The cost is that the
app ships feeling wrong on one input or the other, and which one depends on the
mouse the reader happens to own.


**Hanabi reference.** `src/util/scroll_prefs.h::HANABI_SCROLL_SPEED` — per-machine escape hatch for the one-multiplier wheel/trackpad problem. `src/util/scroll_prefs.h` (`sv.scroll_speed = speed;`) — applies the override to HasScrollView. Tests: `tests/ui/wheel_notch_distance_is_settable.e2e` (`HANABI_SCROLL_SPEED=10`) — script proves the speed knob reaches the transcript scroll view. Measurement/gate: `tests/ui/wheel_notch_distance_is_settable.e2e` (`706 = 646 + 3 x 10 x 2`) — test comment records the expected distance under the override.


**Minimal upstream fix.** Carry the bit. sokol already has it: one more field on
`sapp_event` (`bool scroll_precise`) set beside `scroll_x`/`scroll_y`, plumbed
through `input::get_mouse_wheel_move_v` as a second return or a sibling
accessor, and `HasScrollView` grows `scroll_speed_precise` next to
`scroll_speed`. Four lines of plumbing for a flag the OS already computed.

CLASS: MISSING

---

### #406 — `HandleScrollInput` hit-tests the wheel against a rect its own sibling system corrects and it does not, so a scroll view inside a scroll view is wheel-tested at the wrong place

**What was wanted.** Certainty about where a wheel event lands, while chasing a
transcript that would not scroll. (It was not this — the cause was on hanabi's
side, and this is filed as the thing that was ruled out, with the asymmetry that
made ruling it out take longer than it should have.)

**What happens.** Two systems in the same file hit-test the pointer against the
same widget's rect, sixty lines apart, and only one of them corrects for the
ancestor scroll offset.

`HandleScrollbarDrag` (`src/plugins/ui/systems.h:1893-1896`):

```cpp
RectangleType view = cmp.rect();
const Vector2Type outer = detail::accumulated_scroll_offset(entity);
view.x -= outer.x;
view.y -= outer.y;
```

with the comment "Same rect the bar is drawn against, so grabbing it where you
see it works even inside another scroll view."

`HandleScrollInput` (`src/plugins/ui/systems.h:2081-2085`, no correction):

```cpp
RectangleType rect = cmp.rect();
if (rect.width <= 0.0f || rect.height <= 0.0f)
    return;
if (!is_mouse_inside(context->mouse.pos, rect))
    return;
```

`accumulated_scroll_offset` (`systems.h:35`) walks ANCESTORS, so it is zero for
a top-level scroll view and both spellings agree — which is why this is latent
and not a live bug in this app. Put one scroll view inside another, though, and
the thumb is grabbable where you see it while the wheel is hit-tested against
where the inner view would be if the outer one had never scrolled: the further
the outer view is scrolled, the further the wheel's target is from the pointer.
The same rect is also the one `apply_scroll_offset_for_e2e`
(`e2e_testing/ui_commands.h:35`) corrects for clicks, so the scripted suite
agrees with the drag and disagrees with the wheel.

**Verified, and verified as NOT the reported bug.** hanabi's transcript scroll
view has no scroll-view ancestor, so its accumulated offset is zero and the
wheel arrived exactly where it looked like it should: instrumented, one notch
over the transcript moved `scroll_target` by the full notch every time. The
transcript's own follow-latch then erased it. A negative result, filed because
the asymmetry is real and the next person reading these two systems will ask the
same question.

**Why the obvious escapes do not work.** Nothing to work around yet — an app
that nests scroll views cannot correct this from outside, because the wheel is
consumed inside the library before the app sees a frame.


**Hanabi reference.** Negative result: `tests/ui/wheel_scrolls_the_pane_under_the_pointer.e2e` (`The wheel drives the pane the pointer is over, and only that one.`) — current app coverage shows the reported split-pane wheel symptom is not this nested-scroll hit-test issue. `tests/ui/wheel_scrolls_the_pane_under_the_pointer.e2e` (`mouse_move 900 400`) — script separately targets the right pane before wheeling it. Tests: `tests/ui/wheel_scrolls_the_pane_under_the_pointer.e2e` (`expect_no_text #39:`) — asserts the pane under the pointer actually scrolled.


**Minimal upstream fix.** Three lines: the same `accumulated_scroll_offset`
subtraction `HandleScrollbarDrag` already does, moved into a shared helper both
call. Or a comment on `HandleScrollInput` saying nesting is unsupported, which
is at least honest.

CLASS: TEDIOUS

---

### #407 — An injected wheel event is delivered on TWO frames, so a script cannot spell one notch

**What was wanted.** A scripted test for the gesture in a bug report: put the
pointer over the transcript, turn the wheel one notch, assert the transcript
moved by one notch.

**What happens.** The e2e injector deliberately lets a wheel delta survive one
`reset_frame`, and the comment says exactly why
(`src/plugins/e2e_testing/input_injector.h:36-46`):

> Survives one extra reset, because the reader may run before the command that
> sets it -- HandleScrollInput does exactly that, which is why injected wheel
> events used to do nothing at all.

That is a correct fix for a real ordering problem, and it has a consequence
nothing states: `HandleScrollInput` runs on the frame the command lands AND on
the frame after, reads the same non-zero delta both times, and adds it to
`scroll_target` twice. Measured in hanabi, at the default `scroll_speed` of 20:
`scroll_wheel 0 3` moves the transcript 120 px, not 60.

So every scripted wheel distance in this repo is twice what it spells, and a
test that pins a pixel position is pinning a harness artifact alongside the
app's arithmetic. Worse, it is silently *self-consistent*: the numbers were read
back out of the harness in the first place, so nothing ever disagrees and the
factor is invisible until someone works out the arithmetic by hand.

**Why the obvious escapes do not work.**

- **Halve the delta in the script.** `scroll_wheel 0 1.5` for one notch is a lie
  in the other direction, and the factor is a property of the injector's
  lifetime rule rather than of the wheel, so it belongs nowhere in a script.
- **Drain on read, like pinch does.** That is exactly the bug the survival rule
  fixed: the reader can run before the setter, and a drained wheel then does
  nothing at all.

**The workaround, and its cost.** Every wheel `.e2e` here states the doubling
and its arithmetic in a comment
(`tests/ui/wheel_scrolls_the_transcript.e2e`, `wheel_notch_distance_is_settable.e2e`).
The cost is that a change to the injector's lifetime rule silently moves every
one of those pixel assertions, and they will fail as "the wheel broke".


**Hanabi reference.** `tests/ui/wheel_scrolls_the_transcript.e2e` (`one scroll_wheel command is read on two consecutive frames`) — wheel tests document the injector double-delivery factor. `tests/ui/wheel_notch_distance_is_settable.e2e` (`3 x 10 x 2`) — speed-knob test bakes in the same two-frame injector math. Tests: `tests/ui/wheel_scrolls_the_transcript.e2e` (`assert_ui transcript_bottom_pad y=766`) — script asserts the doubled default wheel distance through a proxy element.


**Minimal upstream fix.** Make the survival one-shot rather than
one-frame-long: mark the delta consumed the first time
`get_mouse_wheel_move_v` actually reads it, and clear it on the following
`reset_frame` whether it was read or not. The setter-before-reader problem stays
fixed, the delta is delivered once, and a script's `scroll_wheel 0 3` means
three.

CLASS: MISSING

---

### #408 — `assert_ui` cannot see a scroll offset, though the dump command next to it prints one

**What was wanted.** To assert, in a scripted test, that a wheel moved a scroll
view — the most direct possible spelling: `assert_ui transcript_scroll
scroll_y=120`.

**What happens.** `check_ui_property` (`e2e_testing/ui_commands.h:1014-1046`)
knows five properties: `x`, `y`, `w`, `h`, `hidden`, plus `text`. Anything else
is `unknown property`. Twenty lines away, `dump_ui_node`
(`ui_commands.h:948`, the scroll line at `:974`) formats the very thing:

```cpp
if (entity.has<ui::HasScrollView>()) {
    auto &sv = entity.get<ui::HasScrollView>();
    out += std::format(" scroll_x=\"{}\" scroll_y=\"{}\"", ...);
}
```

— but `dump_ui` writes an XML tree to a file through an app-supplied callback,
so it is a debugging aid, not an assertion, and a script cannot branch on it.

So a scroll assertion has to go through a proxy: find a named element that sits
at a known place in the content, and assert its SCREEN y, which moves as the
view scrolls. That works (`assert_ui transcript_bottom_pad y=766`) and it costs
the test its own subject: a layout change that moves the proxy fails as "the
wheel broke", the number is only meaningful at one window size, and the reader
of the failure has to know that the pad is a proxy for an offset at all.

**Why the obvious escapes do not work.**

- **Use `dump_ui` and grep the file.** The runner has no variables and cannot
  branch (#285's complaint); the grep would live outside the script, in the shell
  that runs it, and no other assertion in this suite works that way.
- **Give the scroll view a debug name and assert its own y.** The viewport does
  not move when its content scrolls. That is the whole point of a viewport.

**The workaround, and its cost.** A proxy element and a comment recording where
its number came from and the window size it is true at, in every wheel test
here. Cost: exactly gap #232's complaint, one more time.


**Hanabi reference.** `tests/ui/wheel_scrolls_the_transcript.e2e` (`assert_ui transcript_bottom_pad y=646`) — scroll offset is asserted through a named proxy element at the pinned position. `tests/ui/wheel_scrolls_the_transcript.e2e` (`assert_ui transcript_bottom_pad y=766`) — same proxy element verifies the scrolled position because assert_ui has no scroll_y property. Tests: `tests/ui/wheel_notch_distance_is_settable.e2e` (`assert_ui transcript_bottom_pad y=706`) — speed override test uses the same proxy-element workaround.


**Minimal upstream fix.** Two lines in `check_ui_property`:

```cpp
else if (prop == "scroll_y" && entity.has<ui::HasScrollView>())
    actual_int = std::lroundf(entity.get<ui::HasScrollView>().scroll_offset.y);
```

and the same for `scroll_x`. The component is already in hand.

CLASS: TEDIOUS

---

### #409 — PERF, not fixing: reading an OS preference inside the widget build, 333 ns a scroll panel a frame, invisible to every gate here

**What was measured.** hanabi's `apply_scroll_prefs`
(`src/util/scroll_prefs.h`) runs once per scroll panel per frame — that is how
immediate mode works, the panel is rebuilt every frame and its preferences are
re-applied — and it used to answer "is macOS natural scrolling on?" by reading
`NSUserDefaults` every time:

```objc
@autoreleasepool {
    return [[NSUserDefaults standardUserDefaults]
               boolForKey:@"com.apple.swipescrolldirection"];
}
```

Measured on this machine, `-O2`, 100,000 calls after a warm-up: **333 ns a
call**. Three or four scroll panels are on screen at once in split view
(sidebar, two transcripts, and the digest when Home is up), so about a
microsecond a frame — a tenth of a percent of a 1.2 ms frame.

**Why it is worth writing down anyway.** It is invisible to every instrument
this project has. The allocation gate counts `operator new`; this allocates
through CoreFoundation and the gate reads 810.0 / 1162.0 / 2743.0 allocations a
frame with it and without it, identical to the tenth. The soak gate watches
slopes and this is flat. The scroll gate watches a ratio and this is in both
arms. A cost that no gate can see does not get smaller on its own, and the shape
— an OS call inside the per-frame widget build — is the shape that is cheap at
one panel and is not at ten.

FIXED here (resolved once into a function-local static, along with the two
`getenv` reads beside it), which costs live tracking: flipping the OS
natural-scroll preference now takes effect on the next launch. That matches how
this repo already treats the sibling preference — `macos_is_dark_mode` in
`sokol_impl.mm` is documented "Read on demand (theme apply), cheap".

**The library's part in it.** afterhours offers no seam for "apply this to the
scroll view once, when it is created, and not every frame". `component_init`
creates `HasScrollView` for `Overflow::Auto`/`Scroll` during config application,
and the app has no callback on that event — so `apply_scroll_prefs` is called
after every `div()` because there is nowhere else to call it from, and any work
it does is per-frame work by construction. Every immediate-mode app that wants
per-widget configuration that is expensive to compute hits this.

CLASS: MISSING



**POSTSCRIPT 2026-08-26 (source-reference audit).** The title’s “not fixing” wording is stale: current source fixes the OS/default/env reads by caching them in function-local statics.

**Hanabi reference.** Hanabi-owned performance finding: `src/util/scroll_prefs.h` (`static const bool resolved = []`) — natural-scroll preference is resolved once instead of per scroll panel per frame. `src/util/scroll_prefs.h` (`static const float smooth = []`) — sibling scroll preference getenv is also cached once. Measurement/gate: `src/util/scroll_prefs.h` (`measured at 333 ns a`) — source comment records the measured per-call cost and why it was cached.

---

### #410 — PERF, not fixing: the soak driver's only handle on a widget is a linear walk of every entity, per call, per frame

**What was measured.** `hanabi::soak::scroll_named` (`src/util/soak.h:106`) is how
every perf arm drives a scroll view, and it finds the view by walking
`EntityHelper::get_entities_for_mod()` and string-comparing a debug name:

```cpp
for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod()) {
    if (!ptr) continue;
    if (!e.has<UIComponentDebug>()) continue;
    if (e.get<UIComponentDebug>().name_value != debugName) continue;
```

At the scroll gate's 2000-session arm that is a walk of ~450 live entities and
up to 450 string compares, once per driven view per frame, inside the thing
whose frame cost is being measured. The gate's own numbers are a RATIO between
two catalog sizes and the walk is in both, so it cancels — which is the only
reason it does not matter.

**Why it is worth writing down.** It is measurement apparatus inside the
measurement. The entity count it walks is the same quantity the gate reports
(`entities, list expanded 327 -> 453`), so the driver gets more expensive
exactly as the thing under test does, and a future arm that compares an absolute
number across catalog sizes rather than a ratio would be reading the driver's
cost as the app's.

**Not fixing** because the fix belongs upstream and the workaround is worse than
the problem: caching the entity pointer between frames is wrong (immediate mode
destroys and rebuilds it), and caching the ID needs a name-to-ID index the
library does not offer. afterhours has `UIEntityMappingCache` for its own
lookups and no public way to ask it "which entity has this debug name".


**Hanabi reference.** Hanabi-owned performance finding: `src/util/soak.h` (`inline bool scroll_named(const char* debugName, float dy)`) — soak driver still resolves a scroll view by debug-name walk. `src/util/soak.h` (`for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod())`) — implementation is a linear entity walk per call. Measurement/gate: `docs/perf/TRANSCRIPT.md` (`scroll_named returns false when the view is not on screen`) — perf notes record the driver behavior and missing-target handling.


**Minimal upstream fix.** Expose the debug-name lookup the e2e plugin already
performs (`ui_commands.h`, the `whereLambda` name match in every `*_ui` handler)
as a plain `ui::find_by_debug_name(name)`, backed by the mapping cache rather
than a walk. Both the e2e handlers and every out-of-tree driver would stop
walking.

CLASS: MISSING

---

### #465 — PLATFORM, FIXED: a process path inside `Foo.app` is not an application identity; `NSBundle.bundleIdentifier` comes from the bundle manifest

**What was needed.** Notifications, CoreSpotlight, and the `hanabi://` handler need one stable application identity shared across rebuilds.

**Exact mechanism.** Foundation exposes the process bundle through `NSBundle.mainBundle` and its nullable `bundleIdentifier` (`Foundation.framework/Headers/NSBundle.h:23,110`). The old bare `output/hanabi.exe` returned `nil`; copying that same Mach-O into a directory named `Hanabi.app` without a real manifest does not invent an identifier.

**Measured result.** Before this change the in-process diagnostic printed `bundle=none` for the developer executable. The packaged executable now prints `bundle=io.github.gabeochoa.hanabi`; `mdls` reports the same `kMDItemCFBundleIdentifier`, and the LaunchServices record's `identifier` and `codeInfoID` both match it.

**Rejected approaches.** Deriving identity from `argv[0]` changes with the path. Using `com.hanabi.app` is globally generic rather than owned by this repository. Treating any non-nil bundle id as Hanabi allows a test host or repackager to write under another app's identity.

**Workaround.** Fixed in the app: a checked-in Info.plist owns `io.github.gabeochoa.hanabi`, and native integrations require that exact value.

**Hanabi reference.** `resources/macos/Info.plist::CFBundleIdentifier` — the checked-in bundle manifest owns the stable application identity. `src/native_extras.mm::hanabi_is_bundled` — native integrations require that exact identifier. Measurement/gate: `scripts/verify_macos_app.sh` (`runtime_status`) — the bundle verifier checks the plist, signature identity, and runtime identity agree.

**Minimal upstream change.** None. This is application packaging, not an afterhours defect.

CLASS: FIXED

---

### #466 — PLATFORM/PERF, FIXED: a TLS executable copied into an app kept two absolute Homebrew load commands and was not self-contained

**What was needed.** `make app` must run on a Mac without the build machine's `/opt/homebrew/opt/openssl@3` tree.

**Exact mechanism.** Mach-O records linked libraries in `LC_LOAD_DYLIB` and search roots in `LC_RPATH` (`usr/include/mach-o/loader.h:293,317,702-705,1227-1228`). The old bundle copied only the executable, whose two load commands named `/opt/homebrew/opt/openssl@3/lib/libssl.3.dylib` and `libcrypto.3.dylib`.

**Measured result.** The old app was 7,256 KB and failed the self-contained test with two machine-local dependencies. The packaged app is 12,884 KB: `Contents/Frameworks` costs 5,604 KB and the OpenSSL licence 16 KB. Its executable now names only `@rpath/libssl.3.dylib` and `@rpath/libcrypto.3.dylib`; libssl's own crypto dependency is also `@rpath`. The self-containment costs **5,628 KB**.

**Rejected approaches.** Requiring Homebrew on the destination is not self-contained. Static linking changes the build and licence surface. A shell launcher that sets `DYLD_LIBRARY_PATH` makes `CFBundleExecutable` a script and moves the failure into process startup.

**Workaround.** Fixed in packaging: copy both dylibs, rewrite ids and load commands, add `@executable_path/../Frameworks`, then sign the rewritten bytes.

**Hanabi reference.** `scripts/package_macos_app.sh` (`install_name_tool -add_rpath`) — packaging copies OpenSSL, rewrites its install names, and adds the bundle-relative runtime path before signing. Measurement/gate: `scripts/verify_macos_app.sh` (`bundle has a machine-local runtime dependency`) — verification rejects Homebrew, local, and user-home load paths. `makefile::verify-app` — the make target runs the bundle gate.

**Minimal upstream change.** None. afterhours does not own Hanabi's TLS dependencies.

CLASS: FIXED

---

### #467 — PLATFORM, FIXED: the compiler's ad-hoc signature covered the Mach-O, not the assembled app or its resources

**What was needed.** A locally verifiable bundle seal and an identifier bound to the complete app, without claiming Developer ID distribution or notarization.

**Exact mechanism.** Mach-O carries an `LC_CODE_SIGNATURE` command (`usr/include/mach-o/loader.h:318,1248`), but the linker signs only the executable it emits. Before packaging, `codesign -dv` said `Identifier=hanabi.exe`, `Info.plist=not bound`, `Sealed Resources=none`; `codesign --verify --deep --strict Hanabi.app` failed: `code has no resources but signature indicates they must be present`.

**Measured result.** The rebuilt bundle reports `Identifier=io.github.gabeochoa.hanabi`, `Signature=adhoc`, `TeamIdentifier=not set`, and `Sealed Resources version=2 ... files=11`. Strict deep verification takes **0.24 s** on Aspen. This proves local integrity only.

**Rejected approaches.** Preserving the linker's signature fails after `install_name_tool`. `codesign --deep` alone before rewriting libraries seals bytes that are then changed. Describing ad-hoc signing as trusted, Developer ID, or notarized would be false.

**Workaround.** Fixed in packaging: sign each relocated dylib, then the executable, then the final bundle; verify after all mutations.

**Hanabi reference.** `scripts/package_macos_app.sh` (`codesign --force --sign -`) — packaging signs relocated libraries, then the executable, then the assembled bundle. Measurement/gate: `scripts/verify_macos_app.sh` (`signing scope: local ad-hoc only`) — verification checks the sealed bundle and states the signature's limited scope.

**Minimal upstream change.** None. A future release process may replace the ad-hoc identity with a real Developer ID and notarization, but this task cannot claim those credentials.

CLASS: FIXED

---

### #468 — PLATFORM, FIXED: declaring a URL scheme in Info.plist does not make a build artifact a LaunchServices handler until it is registered

**What was needed.** A Spotlight result's `hanabi://thread/<id>` URL must resolve back to Hanabi, and install/update/remove must not hide system-state changes inside `make app`.

**Exact mechanism.** LaunchServices registers an application URL through `LSRegisterURL` (`LaunchServices.framework/Headers/LSInfo.h:248-261`). The bundle's `CFBundleURLTypes` is data LaunchServices reads; writing the plist alone does not insert the handler into its database.

**Measured result.** Explicit registration took **0.05 s**. The database then contained the exact app path, identifier, `claimed schemes: hanabi:`, and claim id `io.github.gabeochoa.hanabi.thread`. Install was exercised twice at the same destination, then uninstall removed it; no stale process remained.

**Rejected approaches.** Registering as a side effect of `make app` makes a build mutate user state. Depending on Finder/open's incidental registration makes tests order-dependent. Blind `rm -rf ~/Applications/Hanabi.app` can remove an unrelated app with the same filename.

**Workaround.** Fixed with explicit `register-app`/`unregister-app` and `install-app`/`uninstall-app` targets. Replacement and removal require the exact bundle id.

**Hanabi reference.** `scripts/manage_macos_app.sh::require_hanabi` — registration, replacement, and removal refuse any bundle with the wrong identity. `makefile::register-app` — the reversible registration targets expose the state change explicitly. `docs/macos-bundle.md` (`register-app`) — the bundle guide documents each side effect.

**Minimal upstream change.** None. Registration belongs to the app installer.

CLASS: FIXED

---

### #469 — PLATFORM-GATED: modern notifications have a durable authorization state, and this machine has denied Hanabi

**What was needed.** Replace deprecated `NSUserNotificationCenter` with `UNUserNotificationCenter` without prompting from tests or pretending delivery succeeded when permission is denied.

**Exact mechanism.** The API is asynchronous: request authorization at `UNUserNotificationCenter.h:53`, read settings at `:60`, and distinguish not-determined, denied, authorized, and provisional at `UNNotificationSettings.h:12-26,60`. Ephemeral authorization is explicitly unavailable on macOS at line 26.

**Measured result.** A real windowed launch from the registered bundle reached the platform in the first frame and reported `notification authorization status=denied` **7 ms after the URL handler installed**. No delivery error was fabricated and no banner was claimed. The same call from the bare unit executable reports `non-bundled` and performs no platform request.

**Rejected approaches.** Falling back to deprecated notifications bypasses the chosen modern contract. Re-requesting after denial cannot override the user's system choice. Changing Notification Center preferences programmatically would be an unauthorized system mutation.

**Workaround.** None for denied permission. The app reports the state and continues normally; the user can change it in System Settings. This work is permission-gated, not broken.

**Hanabi reference.** `src/native_extras.mm::native_notifications_start` — the app maps durable macOS authorization states and never retries denied permission as if it were transient. `src/main.cpp::app_frame` — only the windowed frame starts native notification integration. Tests: `tests/unit/test_native_extras.mm` (`native_notifications_start`) — the bare executable path proves side-effect-free behavior.

**Minimal upstream change.** None. Authorization is deliberately controlled by macOS and the user.

CLASS: PLATFORM-GATED

---

### #470 — PLATFORM, FIXED: notification authorization and delivery are both asynchronous, so the first real event can race the first-run answer

**What was needed.** If the first blocked/finished transition arrives while the first-run permission sheet is unresolved, it must not be silently lost.

**Exact mechanism.** Both `requestAuthorizationWithOptions` and `addNotificationRequest` complete later (`UNUserNotificationCenter.h:53,63`). Calling add immediately while status is still `NotDetermined` can return before the authorization answer and reject the request.

**Measured result.** On the verified run, installation, settings lookup, and Spotlight donation all completed inside the first 24 ms of the frame; authorization returned independently. The race window is therefore not theoretical even on a warm machine. Hanabi holds exactly **one** pending payload, so the bound is title + body + thread id rather than an unbounded notification queue.

**Rejected approaches.** Sleeping blocks the UI and still guesses at consent timing. Dropping the first event violates the notification contract. Queueing every transition can replay a burst after a person spends minutes deciding.

**Workaround.** Fixed: keep the newest one pending while status is unresolved, flush it only on authorized/provisional, and drop it on denied/error.

**Hanabi reference.** `src/native_extras.mm::NotificationPayload` — the bounded pending slot stores exactly one newest notification while authorization is unresolved. `src/native_extras.mm::deliver_pending_notification` — authorization success flushes that slot and denial/error drops it. `src/native_extras.mm::native_notify` — delivery and queue policy meet at the app seam.

**Minimal upstream change.** None. The asynchronous API is intentional; queue policy belongs to the app.

CLASS: FIXED

---

### #471 — PLATFORM, FIXED: foreground presentation and request sound are separate switches in UserNotifications

**What was needed.** Preserve audible notifications when enabled, silence them when disabled, and still show a banner while Hanabi is frontmost.

**Exact mechanism.** Request content owns its nullable `sound` and `threadIdentifier` (`UNNotificationContent.h:115-122`), while the delegate separately chooses foreground presentation options (`UNUserNotificationCenter.h:83-86,97`). Setting one does not imply the other.

**Measured result.** The old `native_notify` always attached the default sound even though Settings exposed Off/Ping. The new seam passes one boolean per event: Off creates no `UNNotificationSound`; Ping does. The delegate returns Banner + List and adds Sound only when the request carries one. Mute, quiet-hours, and the **30 s** debounce remain before the native call.

**Rejected approaches.** Authorizing sound but always attaching it ignores Settings. Omitting the delegate makes foreground delivery depend on system defaults and can hide the banner. Using one notification identifier per thread would replace prior delivered requests, contrary to separate transition events.

**Workaround.** Fixed in the app.

**Hanabi reference.** `src/native_extras.mm::deliver_notification` — request content independently sets sound, thread identity, and payload. `src/native_extras.mm::HanabiNotifDelegate` — foreground presentation mirrors the request's sound choice while always allowing banner and list. Tests: `tests/unit/test_notify_events.cpp` — transition, mute, and debounce-adjacent selection logic remains covered.

**Minimal upstream change.** None.

CLASS: FIXED

---

### #472 — PLATFORM/PERF, FIXED: CoreSpotlight updates by identifier but does not return the previous catalog, and batching is unavailable on the default index

**What was needed.** Titles/previews must update, deleted sessions must disappear, a failed refresh must not erase the last good index, and index memory/work must stay bounded.

**Exact mechanism.** CoreSpotlight exposes index and delete-by-identifier at `CSSearchableIndex.h:48,53`; its durable client-state batching is explicitly unsupported for `defaultSearchableIndex` at `:71-73`. `CSSearchableItem` supplies stable unique/domain identifiers at `CSSearchableItem.h:48-65`, but no API enumerates the app's old identifiers.

**Measured result.** Catalog planning over **2,500** synthetic sessions, truncating to the newest **2,000**, including UTF-8 truncation, URL encoding, dedupe, sorting, and signature, averaged **541.5 µs** over 1,000 runs at `-O2`. One real donation completed **24 ms** after the first native install log; the next sync reported `1 indexed, 1 removed`. The persisted manifest held one `thread:` id, then an explicit empty sync removed it and persisted `()`.

**Rejected approaches.** `deleteAllSearchableItems` can erase unrelated future Hanabi domains. Domain-delete then re-add creates a visible empty window every refresh. Keeping prior ids only in memory leaves stale results after relaunch. Syncing mock fixtures pollutes a real user's Spotlight.

**Workaround.** Fixed: newest-first 2,000-item catalog, 500-item API chunks, stable `thread:` identifiers, a bundle-scoped id manifest, delete-only-the-difference, and no sync on Loading/Error/mock.

**Hanabi reference.** `src/util/spotlight_catalog.h::make_catalog` — the catalog owns bounds, ordering, deduplication, preview truncation, URLs, and update signatures. `src/native_extras.mm::native_spotlight_sync` — native code reconciles update/delete sets with the bundle-scoped manifest. Tests: `tests/unit/test_spotlight_catalog.cpp` — catalog bounds, dedupe, UTF-8, previews, URLs, and signatures are covered.

**Minimal upstream change.** A public `fetchSearchableItemIdentifiers(domain:)` or durable client state on the default index would remove the app-owned manifest. Until then the manifest is the smallest correct reconciliation state.

CLASS: FIXED

---

### #473 — PLATFORM-GATED: CoreSpotlight accepted the item, but all three `mdquery`/`mdfind` predicates returned zero rows

**What was needed.** Programmatic verification that the donated title/preview/URL is discoverable through the same metadata query tool used for file Spotlight content.

**Exact mechanism.** `indexSearchableItems` exposes only an error-or-success completion (`CSSearchableIndex.h:48`). The item's searchable title, description, and URL fields exist (`CSSearchableItemAttributeSet_General.h:14,26,58`; `_Documents.h:23`), but the command-line metadata query does not promise to enumerate CoreSpotlight's private app-domain records.

**Measured result.** The real completion handler reported `Spotlight catalog synced (1 indexed, 1 removed)`. Immediately afterward, exact-title, preview-text, and bundle-id `mdfind` predicates each returned **0** rows. LaunchServices and the persisted manifest independently showed the correct bundle and item id.

**Rejected approaches.** Treating zero `mdfind` rows as indexing failure contradicts the API's success callback. Scraping private CoreSpotlight databases is OS-version-specific and privacy-sensitive. Creating a fake file solely so `mdfind` returns a path would test file metadata, not the donated item.

**Workaround.** None for command-line visibility. Keep the successful completion as API proof and classify end-user discoverability as Spotlight-UI/index-state gated.

**Hanabi reference.** `src/main.cpp` (`HANABI_SPOTLIGHT_TEST`) — a local-only diagnostic donates or clears one record without a backend send. `src/native_extras.mm::native_spotlight_sync` — the completion log is the supported API proof. Measurement/gate: `docs/macos-bundle.md` (`HANABI_SPOTLIGHT_TEST=clear`) — the guide records the command-line visibility limit and cleanup path.

**Minimal upstream change.** A supported command-line/CoreSpotlight diagnostic that queries an app's own domain by bundle id and unique identifier.

CLASS: PLATFORM-GATED

---

### #474 — PLATFORM, FIXED: a headless executable run from inside the app still has the real bundle id, so bundle gating alone does not prevent side effects

**What was needed.** Headless screenshots, scripted UI, unit tests, and performance gates must never request notification permission, install a global hotkey, register a drop target, or write CoreSpotlight.

**Exact mechanism.** `NSBundle.mainBundle.bundleIdentifier` describes where the process came from, not whether it has a window (`NSBundle.h:23,110`). Therefore `Hanabi.app/Contents/MacOS/hanabi --screenshot ...` reports the same stable id as a windowed launch and would pass every `hanabi_is_bundled()` guard.

**Measured result.** A bundled headless run with both `HANABI_NOTIFY_TEST=must-not-send` and `HANABI_SPOTLIGHT_TEST=must-not-index` produced a **102,916-byte** screenshot, emitted **0** native side-effect logs, and left a seeded Spotlight manifest exactly `thread:sentinel`. The bare native unit also remained non-bundled. The complete unit/e2e run passed **37/37**.

**Rejected approaches.** Checking only the bundle id is insufficient. Checking for CI or a terminal is heuristic. Making native functions inspect `--screenshot` couples platform code to the app's argument parser and can drift as new headless modes appear.

**Workaround.** Fixed at the ownership boundary: notification startup, hotkey, URL handler, file drop, and test donations are called only from `app_frame`; every headless path owns a separate loop and never reaches it. Native functions still require the exact bundle id as defense in depth.

**Hanabi reference.** `src/main.cpp::app_frame` — the windowed loop is the only owner of native startup and test donations. `src/main.cpp::run_headless_screenshot` and `src/main.cpp::run_e2e` — both headless loops remain outside that ownership boundary. Tests: `tests/unit/test_native_extras.mm` — bare native calls are no-ops. Measurement/gate: `docs/macos-bundle.md` (`sentinel`) — the bundled headless sentinel probe documents that no side effect escapes.

**Minimal upstream change.** None. afterhours correctly separates its windowed and headless loops; the application must keep side effects on the windowed branch.

CLASS: FIXED

---

### #475 — NOT A GAP: a real archive icon is an app-owned atlas entry, not a missing afterhours primitive

**Afterhours mechanism.** `imm::sprite` accepts an arbitrary texture and source
rectangle (`vendor/afterhours/src/plugins/ui/imm_components.h:654-686`), and the
Sokol backend draws that sub-rectangle with tint through
`draw_texture_pro` (`vendor/afterhours/src/backends/sokol/drawing_helpers.h:1222-1257`).
The library deliberately supplies a sprite mechanism, not an icon catalogue.

**Measured result.** Removing the U+25A4 fallback from Archived changed **0
pixels** in the 1100x760 archived-view capture while the atlas was present. The
real Lucide archive sprite was already live; only the failure path was stale.

**Workaround.** Generate and ship the app's Lucide atlas, then call the existing
source-rectangle draw path with no text fallback.

**Minimal upstream change.** None. An afterhours-owned icon vocabulary would be
the wrong layer.

**Rejected ideas.** Keeping the box fallback was rejected because it is a
different symbol, and using a system-font glyph was rejected because the
bundled UI font does not guarantee that codepoint.

**Remaining slow Hanabi work.** None for this item.

**Hanabi reference.** `scripts/gen_icons.py::ICONS`,
`src/ui/icons_atlas.h::kIcons`, `src/ui/icons.h::draw_at`, and
`src/ecs/sidebar_system.h::render_smart_views` demonstrate the generated sprite
and no-fallback call. `docs/screenshots/baselines/11_view_archived_dark.png` is
the targeted capture.

CLASS: NOT A GAP (app-side)

### #476 — A scroll-to-end action must know all three fields in the smoothing state machine

**Afterhours mechanism.** `HasScrollView` exposes `scroll_offset`,
`scroll_target`, and `last_eased_offset` independently
(`vendor/afterhours/src/plugins/ui/components.h:511-580`). `clamp_scroll()` clamps
both positions and then records only the offset as the last eased value. There
is no `scroll_to_end()` operation.

**Measured failure.** A jump control needs the sequence `offset = 1e9`, `target =
1e9`, `clamp_scroll()`, plus an application follow-latch update. Writing only
the offset is not stable: after `clamp_scroll()` makes `last_eased_offset` equal
to it, the next `ease_scroll()` sees no caller edit and eases toward the old
target.

**Workaround.** Hanabi writes both fields, clamps, then calls
`note_follow_pinned` for the pane-local latch.

**Minimal upstream change.** Add `HasScrollView::scroll_to_end(Axis)` and
`scroll_to_start(Axis)` methods that update offset, target, and
`last_eased_offset` atomically.

**Rejected ideas.** Offset-only snaps back; target-only animates rather than
jumps; retaining `1e9f` without a named operation keeps the state-machine
coupling at every call site.

**Remaining slow Hanabi work.** Input-device normalization remains gap #405;
this helper would not distinguish wheel from trackpad.

**Hanabi reference.** `src/ecs/main_pane_system.h::jump_to_bottom_button` and
`src/ecs/follow_latch.h::note_follow_pinned` show the workaround.
`tests/ui/jump_to_bottom_tracks_pane.e2e` and
`tests/unit/test_follow_latch.cpp` cover the click and latch behavior.

CLASS: MISSING

### #477 — NOT A GAP: a clicked child cannot choose an application-defined pane focus policy

**Afterhours mechanism.** `HandleClicks` focuses the clicked entity before
invoking its listener (`vendor/afterhours/src/plugins/ui/systems.h:750-760`).
`UIContext::set_focus` and `focus_in_subtree` operate on UI entities
(`vendor/afterhours/src/plugins/ui/context.h:244-299`); they cannot know that a
host also has a separate `AppComponent::focusedPane` selection.

**Measured failure.** The new split-pane regression initially left the focus
edge at **x=280** after clicking the right pane's jump button; the required
right-pane edge was **x=693**. Scrolling moved the right transcript correctly,
but application pane focus stayed left.

**Workaround.** The jump handler explicitly assigns the pane index after the
button fires. The generic click system continues to focus the button itself.

**Minimal upstream change.** None. Pane ownership is application state, and
silently focusing arbitrary ancestors would break controls whose parent is not a
pane.

**Rejected ideas.** Changing `HandleClicks` to focus a parent or cluster was
rejected because the library has no semantic pane boundary. Inferring pane from
the debug name was rejected as test-only naming leaking into product behavior.

**Remaining slow Hanabi work.** None; the assignment is one integer store on a
click, not per-frame work.

**Hanabi reference.** `src/ecs/main_pane_system.h::pane_column` owns pane focus
and `jump_to_bottom_button` now applies the same policy.
`tests/ui/jump_to_bottom_tracks_pane.e2e` reproduces the x=280 failure and holds
the x=693 result.

CLASS: NOT A GAP (app policy)

### #478 — NOT A GAP: smart-view row consistency is one application renderer with one boolean removed

**Afterhours mechanism.** The generic `button` accepts one
`ComponentConfig` and returns the same `ElementResult` regardless of the data
model (`vendor/afterhours/src/plugins/ui/imm_components.h:726-786`). Flex,
labels, colors, and sizes are all caller-supplied; afterhours does not define a
"smart view" or card variant.

**Measured failure.** Blocked and Review passed grouped mode, selecting the
**34px** sparse card, while Pinned and Archived selected the **52px** rich card
(`src/ecs/digest_layout.h::kCardSparseH/kCardRichH`). The scripted contract now
observes a 52px card with title, status pill, and metadata in all four populated
views.

**Workaround.** Route every smart view through `digest_card` with the same
non-grouped card shape. Home remains grouped because its section header carries
the grouping context.

**Minimal upstream change.** None.

**Rejected ideas.** Four separate row components were rejected because they
would recreate the inconsistency. Forcing Home's grouped rows into the same
shape was rejected because it would repeat one section status on every card.

**Remaining slow Hanabi work.** Digest virtualization still builds 13 of
506/303/200/200 matched cards in the stress fixture; this change leaves that
window and its 0.99x widget ratio intact.

**Hanabi reference.** `src/ecs/main_pane_system.h::render_digest` and
`digest_card`, plus `src/ecs/digest_layout.h::sub_line`, are the shared path.
`tests/ui/smart_view_rows_are_consistent.e2e` and the populated
`10b_view_pinned_row_dark` / `11b_view_archived_row_dark` baselines cover it.

CLASS: NOT A GAP (app policy)

### #479 — NOT A GAP: status meaning belongs in the app; custom foreground drawing already supports it

**Afterhours mechanism.** `ComponentConfig::with_on_draw_fg` stores a custom
draw callback (`vendor/afterhours/src/plugins/ui/component_config.h:486-492`).
The library does not impose a status enum or require one entity per glyph.

**Measured result.** Hanabi reduced five shapes and three tones to four meanings
— running, blocked, done, idle — inside the existing row glyph callback.
Steady-state allocations stayed exactly **811 / 1163 / 2750 / 1025 per frame**
for home20 / home2000 / thread480 / draft6, and scaling stayed **322 / 428
widgets** at 20 / 2000 sessions.

**Workaround.** A small app-side classifier selects one of four draw branches in
the existing entity.

**Minimal upstream change.** None.

**Rejected ideas.** Separate status widgets or sprite children were rejected:
they add entities to every visible row and make a policy enum part of the UI
library. Keeping failure, attention, subagent, and idle as separate shapes was
rejected as the vocabulary the task was removing.

**Remaining slow Hanabi work.** The unchanged home2000 floor is 1163
allocations/frame and 428 widgets; this change neither adds to nor fixes it.

**Hanabi reference.** `src/ecs/sidebar_system.h::SidebarGlyph`,
`sidebar_glyph`, and `draw_mark` are the app policy. `scripts/alloc_gate.sh` and
`scripts/scaling_gate.sh` produced the unchanged counts; the 01/02/03 screenshot
baselines hold the pixels.

CLASS: NOT A GAP (app policy, performance proof)

### #480 — NOT A GAP: a section glyph and its label can carry independent colors

**Afterhours mechanism.** A component can set its text color through
`with_custom_text_color` and draw different foreground ink through
`with_on_draw_fg` (`vendor/afterhours/src/plugins/ui/component_config.h:360-367,
486-492`). No extra wrapper or styled-label split is required.

**Measured result.** Changing Home's label ink to the neutral secondary token
while leaving state color on the chevron changed **1,364 pixels (0.163%)** in
dark and **1,366 pixels (0.163%)** in light, bounded to
**x=323..451, y=129..528**. Shelf folding still passes.

**Workaround.** Keep the disclosure glyph in the custom foreground callback and
give the adjacent label the neutral token.

**Minimal upstream change.** None. This is also consistent with the existing
negative result #240: multi-color rows are supported.

**Rejected ideas.** Coloring the full label was the defect. Removing state color
entirely loses the fast scan. Rebuilding the row as a styled label is unnecessary
because the glyph is geometry, not text.

**Remaining slow Hanabi work.** None; this changes constants read by two existing
draw paths and creates no entity.

**Hanabi reference.** `src/ecs/main_pane_system.h::section_label` demonstrates
the two independent colors. `tests/ui/home_shelf_folds.e2e` holds interaction;
`01_home_dark` and `02_home_light` hold rendering.

CLASS: NOT A GAP

### #481 — Status-pill alpha still has to be pre-composited by the application

**Afterhours mechanism.** `with_custom_background` preserves an RGBA token
(`vendor/afterhours/src/plugins/ui/component_config.h:334-345`), but the ordinary
UI fill path uses a non-blended pipeline, the mechanism already filed as #15.
A low-alpha pill otherwise paints as an opaque slab.

**Measured result.** Hanabi's current pre-composited pill pairs measure
**4.97 / 6.21 / 4.92:1** for Blocked / Ready / Done in dark and
**6.01 / 5.24 / 4.71:1** in light. All six clear the 4.5:1 small-text threshold.
The original raw-alpha path made same-hue text effectively disappear, as #15
records.

**Workaround.** `theme::over(tag_*_bg(), panel_bg_2())` computes the opaque
surface before `with_custom_background` sees it.

**Minimal upstream change.** The #15 fix: enable source-over blending for UI
fills, or expose a fill primitive whose documented contract composites alpha.

**Rejected ideas.** Passing the low-alpha token directly repeats #15. Using one
opaque pill color in both themes fails contrast in one palette. Making every
pill plain grey throws away status meaning instead of making it readable.

**Remaining slow Hanabi work.** None for contrast. The workaround is fixed
arithmetic per pill and is below the allocation gate's resolution.

**Hanabi reference.** `src/ui/theme.h::over`, `tag_*_fg`, and `tag_*_bg`, plus
`src/ecs/main_pane_system.h::tag_bg`, demonstrate the workaround. The Blocked
light/dark and populated Pinned/Archived baselines cover all pill families.

CLASS: DUPLICATE → #15 (live)

### #482 — NOT A GAP: bottom-anchoring an empty state is ordinary column justification

**Afterhours mechanism.** AutoLayout computes remaining main-axis space and
implements `JustifyContent::FlexEnd` by assigning it to `start_offset`
(`vendor/afterhours/src/plugins/autolayout.h:1273-1317`). A fixed-height column
therefore anchors its children without absolute coordinates.

**Measured result.** In the 1100x760 empty-transcript fixture the title lands at
**y=572**, directly above the composer rather than near the top of a blank pane.
The scripted behavior test completes in **0.13s**.

**Workaround.** None in the library sense; Hanabi uses a normal full-height
column with `FlexEnd` and a 28px bottom inset.

**Minimal upstream change.** None.

**Rejected ideas.** Absolute y translation was rejected because it would stale
on resize and split width. Vertical centering was the void being removed. A lone
"Task:" label was rejected because it names a field rather than inviting the
next action.

**Remaining slow Hanabi work.** None; the state builds three entities only when
the transcript is empty.

**Hanabi reference.** `src/ecs/main_pane_system.h::render_transcript` contains
`transcript_empty_anchor`; `tests/ui/empty_transcript_is_anchored.e2e` pins the
y-coordinate and copy; `13b_empty_transcript_dark` is the targeted baseline.

CLASS: NOT A GAP

### #483 — A scripted UI test still cannot assert the color that defines a status pill

**Afterhours mechanism.** `check_ui_property` accepts exactly `x`, `y`, `w`,
`h`, `hidden`, and `text`
(`vendor/afterhours/src/plugins/e2e_testing/ui_commands.h:1012-1050`). It already
has the entity and UI component but exposes no foreground, background, border,
or rendered-pixel assertion. This is the current concrete instance of #308.

**Measured cost.** The empty-state scripted assertion ran in **0.13s**. One
single-state screenshot capture took **1.58s** on the same Mac, about **12x** as
long, before image comparison. Pill legibility is color-only, so the slower path
is mandatory.

**Workaround.** Commit exact screenshot baselines for dark and light palettes;
use a deterministic capture clock and isolated settings.

**Minimal upstream change.** The #308 fix: add parsed RGBA properties for text,
background, and border to `check_ui_property`.

**Rejected ideas.** Asserting the label text proves the pill exists but not that
it is legible. Reading token constants proves intent but not the color that the
renderer put on screen.

**Remaining slow Hanabi work.** Every new color state adds another process and
PNG to the screenshot suite. This batch adds three targeted states because the
scripted harness cannot replace them.

**Hanabi reference.** `scripts/screens.sh`,
`docs/screenshots/baselines/manifest.json`, and the Blocked light/dark plus
populated Pinned/Archived baselines are the workaround. `src/ui/theme.h` carries
the measured token pairs.

CLASS: DUPLICATE → #308 (live)

### #484 — PERF PROOF: changing a glyph inside one draw callback costs no entities or allocations

**Afterhours mechanism.** `with_on_draw_fg` stores one `std::function` on the
existing component (`vendor/afterhours/src/plugins/ui/component_config.h:486-492`),
and `button`/`div` entity creation is independent of which branch that callback
draws (`vendor/afterhours/src/plugins/ui/imm_components.h:726-786`).

**Measured result.** Before and after the four-state sidebar change:

```
                         before   after
home20 allocations/f       811     811
home2000 allocations/f    1163    1163
thread480 allocations/f   2750    2750
draft6 allocations/f      1025    1025
widgets at 20 sessions      322     322
widgets at 2000 sessions    428     428
```

The 2000/20 widget ratio stayed **1.33x**. The minimum frame ratio moved
**1.27x → 1.25x**, normal timing noise; the count-based invariants did not move.

**Workaround.** Keep all four shapes inside the existing row glyph callback and
classify with a branch over existing session fields.

**Minimal upstream change.** None.

**Rejected ideas.** Four child widgets or four sprite entities were rejected
because they would turn a vocabulary cleanup into per-row entity/allocation
cost. A new generic status component was rejected because afterhours cannot own
Hanabi's product states.

**Remaining slow Hanabi work.** The unchanged levels — 1163 allocations/frame
and 428 widgets at 2000 sessions — remain worth profiling, but the glyph change
adds zero to either.

**Hanabi reference.** `src/ecs/sidebar_system.h::draw_mark` keeps one callback
and one glyph slot. `scripts/alloc_gate.sh` and `scripts/scaling_gate.sh` are the
measurement harnesses; 01/02/03 baselines cover the visible result.

CLASS: PERF PROOF (app-side)

---

### #435 — PERF: `draw_text_in_rect` reconstructs plain wrapped lines on every draw and exposes no reusable layout

**What was wanted.** Repaint an unchanged wrapped label and place a find band on
known byte offsets without rebuilding the same line layout twice.

**Verified mechanism.** In pinned afterhours `428047e`,
`plugins/ui/rendering.h:629-653` tests for hard/soft wrapping and then calls
`detail::wrap_text_to_width` into a new `std::vector<std::string>` on every
`draw_text_in_rect`. The wrapper in `plugins/ui/text_selection.h:65-212` first
builds `source_lines`, then `chunks`, then output lines; strings are copied at
`:77`, `:157`, `:184` and `:233`. No result is retained on `HasLabel`, and the
public `measure_text_wrapped` returns dimensions only, not the wrapped lines.

**Measured cost.** With Cmd+F open on the 480-message fixture after the collector
fix, the allocation profile attributes 87.0 vector allocations/frame and 86.3
string-growth allocations/frame to `draw_text_in_rect`'s wrap at
`rendering.h:653`. The whole remaining `find.paint` path is 0.53 ms/frame.

**Hanabi reference.** `MainPaneSystem::render_bubble` in
`src/ecs/main_pane_system.h` builds the `user_text` label; that label's normal
draw reaches this mechanism. `find_paints_a_match_that_wraps.e2e` proves its
soft-wrap behavior, and `scripts/find_gate.sh` reports the resulting allocation
level.

**Workaround.** Hanabi caches normalized text and match offsets, but cannot feed
wrapped lines into the renderer. The renderer performs this allocation even
when every input is unchanged.

**Rejected.** Turning wrapping off changes layout. Supplying one label per line
would duplicate the library's break algorithm in app code and move selection,
alignment and hard-break semantics out of the renderer.

**Minimal upstream change.** Store the wrapped-line result on `HasLabel`, keyed
by text, width, font name/size/spacing, overflow mode and font generation, and
let `draw_text_in_rect` consume it until one input changes.

CLASS: PERFORMANCE · duplicate family #42/#340

---

### #436 — PERF: styled-label drawing rebuilds a nested run layout every frame, independently of the plain-label wrap

**What was wanted.** Repaint unchanged markdown spans without reconstructing a
`vector<vector<TextSpan>>` and its concatenated strings every frame.

**Verified mechanism.** `plugins/ui/rendering.h:866-874` calls
`detail::wrap_runs_to_width` unconditionally in `draw_runs_in_rect`.
`plugins/ui/text_selection.h:70-208` materializes source-line vectors, token
chunks, run fragments and output lines. This is a separate path from #435:
plain labels use `draw_text_in_rect`; styled labels use `draw_runs_in_rect`,
then call the plain routine once per final run at `rendering.h:915-929`.

**Measured cost.** On the same open-find profile, the renderer's styled-label
site at `rendering.h:1622` accounts for 320.0 allocations/frame. The calling
`MainPaneSystem::render_rich_body` accounts for 758.4 allocations/frame in the
roll-up. This work is viewport-sized, not transcript-sized; it remains after
the loaded-thread collector is memoized.

**Hanabi reference.** `MainPaneSystem::render_rich_body` and `md_to_spans` in
`src/ecs/main_pane_system.h` populate `LineDrawState` and `with_styled_label`.
`find_sees_through_markdown.e2e` and `find_paints_a_match_that_wraps.e2e` hold
the styled and wrapped output; `scripts/alloc_sites.sh` produces the allocation
attribution above.

**Workaround.** None without replacing the renderer. Hanabi's message memo keeps
the normalized line and offsets, but `HasLabel` owns no wrapped-layout cache and
`draw_runs_in_rect` accepts no precomputed layout.

**Rejected.** Rendering the line as plain text removes inline colors and weights.
Caching only the `TextSpan` input does not help because the library copies and
wraps that input after the call.

**Minimal upstream change.** Cache wrapped run lines on `HasLabel` with the same
key and invalidation inputs as #435, and expose the retained layout to draw-time
decorations.

CLASS: PERFORMANCE · duplicate family #340

---

### #437 — PERF: find highlight must run a second wrap because the text renderer exposes no byte-to-rectangle map

**What was wanted.** Paint a rectangle behind each cached match offset using the
layout the renderer already computed.

**Verified mechanism.** `draw_text_in_rect` keeps its wrapped `lines` local in
`plugins/ui/rendering.h:652-675`; `draw_runs_in_rect` keeps its wrapped run lines
local at `:873-935`. Neither returns line starts, glyph advances or rectangles.
The only public geometry helpers in
`plugins/ui/text_selection.h:337-428` operate on caller-supplied wrapped lines,
so a caller must first repeat the wrap.

**Measured cost.** After cached collection and cached occurrence offsets,
`hanabi::find_highlight::paint_bands` still accounts for 686.5 allocations/frame
and `find.paint` for 0.53 ms/frame on the 480-message fixture. The total
open/closed allocation ratio is 1.451× there and 1.527× at 14,688 messages,
showing that the residual is bounded by painted content rather than loaded
content.

**Hanabi reference.** `hanabi::find_highlight::paint_bands` in
`src/ui/find_highlight.h` calls afterhours' wrapper, reconstructs each wrapped
line's source offset, and maps one logical hit onto one or more rectangles.
`hanabi::find_memo::Memo::line_hits` supplies the cached logical offsets.
`find_paints_a_match_that_wraps.e2e` proves a phrase crossing a soft wrap is
counted once and painted; the other find E2Es hold the count/band law.

**Workaround.** Hanabi re-runs the library wrapper for visible matching lines,
then reconstructs source offsets with ordered substring searches. It caches the
query hits but not absolute rectangles, because scroll and layout move them.

**Rejected.** Searching each wrapped line drops a phrase whose whitespace was
consumed by the break. Caching screen-space rectangles goes stale on scroll,
resize and font change. Counting one rectangle per wrapped fragment breaks the
one-match stepping order.

**Minimal upstream change.** Return a reusable layout object from the label
renderer containing wrapped runs plus source byte ranges, and expose a
`rects_for_range` query in label-local coordinates.

CLASS: MISSING · refines #51

---

### #438 — PERF (ours, not fixing here): visible rich text still reparses markdown and rebuilds widget configuration every frame

**What was wanted.** Account for the cost left after the whole-thread find scan
was removed instead of folding it into the memo's result.

**Verified mechanism.** This is app-owned. `MainPaneSystem::render_rich_body`
recreates `InlineParse`, link runs, `ComponentConfig` and styled spans for every
visible ordinary line on every frame. The library multiplies those values via
its by-value `ComponentConfig` path (`plugins/ui/component_config.h:199-211`,
`plugins/ui/component_init.h:58,88,693-696`), the already-filed #181 mechanism.

**Measured cost.** The open-find 480-message allocation profile attributes
758.4 allocations/frame to `render_rich_body`, another 204.5 to its config
construction site, and 94.0 each to three `md_to_spans` frames. Total open-find
allocations are 6,660.9/frame against 4,590.3 closed. The ratio remains nearly
flat at 1.451× / 1.526× / 1.527× over 480 / 3,672 / 14,688 messages, so this is
a visible-window level rather than the fixed loaded-thread slope.

**Hanabi reference.** `MainPaneSystem::render_rich_body`, `md_to_spans`, and
`ecs::LineDrawState` in `src/ecs/main_pane_system.h` and
`src/ecs/line_draw_state.h`; `scripts/alloc_sites.sh` names the call sites and
`scripts/find_gate.sh` bounds the aggregate allocation ratio.

**Workaround.** `LineDrawState` already prevents callback captures from cloning
two strings and a `shared_ptr` through each config copy. The new find memo also
reuses normalized message lines and query offsets. The remaining per-frame
styled parsing and component copies are outside the collector fixed here.

**Rejected.** Caching the whole rendered subtree in hanabi cannot work:
afterhours clears children and visibility before every immediate-mode build.
Caching only `InlineParse` would reduce app parsing but leave the larger
library-side wrapped-run and config-copy allocations, so it is a separate
measured change rather than scope hidden inside #365.

**Minimal upstream change.** The retained wrapped-label layout in #435/#436,
plus pass `ComponentConfig` through initialization without the three deep
copies already specified by #181.

CLASS: PERFORMANCE · app + duplicate family #181/#340

---

### #455 — `virtual_list` cannot window variable-height transcript rows, so every frame scans every item to find the visible window

**Exact afterhours mechanism.** `vendor/afterhours/src/plugins/ui/imm_components.h:159-219` accepts one `row_height`, derives `first` and `last` with `offset / row_height` at lines 185-188, and sizes both spacers as `count * row_height` at lines 194-217. A transcript mixes wrapped bubbles, tool piles, output panels, thinking folds, deliveries, and dividers; one height cannot represent it.

**Measured cost.** `scripts/events_gate.sh` on this checkout, 1180×949 and the real-event mix, measured **2345 allocations/frame at 15 turns and 2381 at 240 turns**, a flat **0.16 allocations/turn** and below the 2900/2.0 gates. Allocation traffic is not the remaining slope. The existing CPU ladder in `docs/perf/EVENTS.md` measures `transcript.pass1_measure` at **1.50 ms for 3,672 messages and 6.12 ms for 14,688 messages**, **85% of the frame** at the long arm. That is the item-list scan this fixed-height primitive cannot replace.

**Workaround.** Hanabi maintains measured heights, prefix positions, overscan, and top/bottom spacers itself. The workaround is correct and allocation-flat, but it still walks the full item list each frame.

**Minimal upstream change.** Add a `virtual_list` overload that accepts a stable per-row height table or prefix-sum index and binary-searches the visible range, plus a way to invalidate one changed row.

**Rejected approaches.** An average row height makes the viewport wrong after the first expanded tool output. Building every row once to ask its height spends the entities and layout work virtualization exists to avoid. Replacing all rows with one fixed height destroys readable message and tool content.

**Hanabi reference.** `src/ecs/main_pane_system.h`, `MainPaneSystem::render_transcript` and `tool_pile_height`, demonstrate the variable-height measure/render mirror and hand-windowed item list. `src/ecs/transcript_render_cache.h` carries the width-keyed measurements. `scripts/events_gate.sh` and `scripts/perf_transcript_slope.sh` are the level/slope gates; `docs/perf/EVENTS.md` records the CPU ladder. Measured workaround; no complete downstream fix remains.

CLASS: PERFORMANCE

---

### #456 — The e2e harness can write clipboard text but has no assertion for the clipboard it already exposes

**Exact afterhours mechanism.** The Metal clipboard plugin exposes both `set_text` and `get_text` at `vendor/afterhours/src/plugins/clipboard.h:34-54`. The built-in e2e registration at `vendor/afterhours/src/plugins/e2e_testing/e2e_testing.h:54-82` registers text, pointer, key, wait, resize, and overflow commands, but no clipboard assertion.

**Measured cost.** The first exact-copy script clicked the real button and then read `clipboard::get_text()` in the headless Metal runner; it failed with expected 82 source bytes and actual 0 bytes. The product write is real, but the headless backend cannot prove it. Hanabi now carries one test-only mirror and a custom command solely to observe the value passed to the plugin.

**Workaround.** `HandleExpectClipboardCommand` compares against `test_hooks::recorded_clipboard_text`, recorded at the same call site that invokes `afterhours::clipboard::set_text`. Shipping builds compile the recorder to a no-op.

**Minimal upstream change.** Add an e2e clipboard backend and built-in `expect_clipboard "text"` command whose `set_text`/`get_text` round trip is deterministic headlessly.

**Rejected approaches.** `pbpaste` reads the host clipboard, not necessarily the headless app backend, and makes tests depend on machine state. Asserting the visible word `Copied` proves feedback, not bytes. Skipping the exact assertion leaves markdown, whitespace, and truncation regressions invisible.

**Hanabi reference.** `src/ecs/e2e_commands.h`, `HandleExpectClipboardCommand`; `src/test_hooks.h`, `record_clipboard_text`; `src/ecs/main_pane_system.h`, `MainPaneSystem::message_actions`; and `tests/ui/message_actions_copy_retry.e2e`. The script proves exact source bytes through pointer activation. Downstream workaround is test-only.

CLASS: MISSING

---

### #457 — Custom e2e commands do not receive the quoted arguments the built-in parser promises

**Exact afterhours mechanism.** `vendor/afterhours/src/plugins/e2e_testing/runner.h:162-170` calls `parse_quoted()` only for a hardcoded set of built-in names. Known one/two/three-argument commands have separate tables at lines 221-237. Every custom command falls through to `while (iss >> arg)` at lines 238-243, splitting quoted prose on spaces and retaining the quote characters.

**Measured cost.** The first `expect_clipboard "Reconcile this cycle's …"` run reached the handler as expected=`"Reconcile`; `expect_outbox "t2" "Reconcile …"` looked for session=`"t2"`. Both failed before the product assertions ran. Hanabi adds a shared downstream join-and-unquote helper for two commands.

**Workaround.** `joined_args` in `src/ecs/e2e_commands.h` rejoins the whitespace-split tail and strips one matching quote pair. It cannot represent multiple independently quoted custom arguments without each handler knowing which prefix is structural.

**Minimal upstream change.** Tokenize every command with the same quote-aware lexer, then let handlers consume the resulting argument vector; remove command-name-specific parsing branches.

**Rejected approaches.** Banning spaces makes clipboard and prompt assertions useless. Encoding spaces produces a test language different from the product bytes. Adding each downstream command name to the vendor parser edits vendor and does not scale.

**Hanabi reference.** `src/ecs/e2e_commands.h`, `joined_args`, `HandleExpectClipboardCommand`, and `HandleExpectOutboxCommand`; `tests/ui/message_actions_copy_retry.e2e` supplies a multi-word prompt to both. The focused UI suite fails before `joined_args` and passes with it. Proven downstream workaround.

CLASS: FOOTGUN

---

### #458 — Icon-only controls have no semantic name or role; a visible text child is the only accessible label

**Exact afterhours mechanism.** `ComponentConfig` exposes visual label, debug name, button variant, and icon texture fields at `vendor/afterhours/src/plugins/ui/component_config.h:122-191`, with setters such as `with_debug_name` at lines 478-480. There is no accessibility name, description, role, or platform accessibility adapter. This is the message/tool-specific proof of existing gap #112, not a second independent upstream ask.

**Measured cost.** `focus_ui` and `expect_focused` can reach the new action containers by debug name, proving keyboard activation inside the toolkit. They cannot query a macOS accessibility tree. To avoid shipping unnamed icons, each visible action is a container plus an icon child plus a text child: a hovered user row builds seven action entities including the bar; the hidden path builds zero. The long busy-event arm remains under gate at **2381 allocations/frame** and **507 widgets**, versus **2369 / 501** before the richer action/tool presentation.

**Workaround.** Pair every icon with persistent visible text (`Copy`, `Retry`, `Copied`, `Queued`) and give the actionable container a stable debug name. This is readable and keyboard-focusable but does not create native assistive semantics.

**Minimal upstream change.** Add semantic role/name/value fields to `ComponentConfig`, propagate them to the macOS accessibility tree, and teach the e2e harness to assert them.

**Rejected approaches.** A tooltip is neither persistent nor keyboard/screen-reader metadata. A debug name is test instrumentation, not user-facing semantics. Icon shape and colour alone fail both discoverability and non-visual access.

**Hanabi reference.** `src/ecs/main_pane_system.h`, `MainPaneSystem::message_actions`, `tool_count_badge`, `tool_status_cluster`, and `tool_sub_row`; `tests/ui/message_actions_copy_retry.e2e` and `message_actions_split_focus.e2e` prove pointer and keyboard focus; `scripts/events_gate.sh` supplies the allocation/widget numbers. Workaround only; upstream accessibility remains missing. DUPLICATE/NARROWED PROOF OF #112.

CLASS: MISSING

---

### #459 — NOT A GAP: conditional immediate-mode construction is enough to make hidden hover actions cost zero entities

**Exact afterhours mechanism.** `imm::div` immediately dereferences the entity pair and calls `init_component` at `vendor/afterhours/src/plugins/ui/imm_components.h:126-138`. There is no hidden-widget optimization to wait for: if application code does not call `div`, no entity is created. Hover is available from `UIContext::mouse_was_in_subtree` at `vendor/afterhours/src/plugins/ui/context.h:266-295` once the host has a hit-test listener.

**Measured cost.** Before this branch, `message_actions` built the absolute `msg_actions` bar and only then returned when not hovered, paying one hidden entity per visible conversational row. It now returns before the first `div`; static inspection proves **zero hidden action entities**. The post-change long busy-event gate is **2345 allocations/frame at 15 turns and 2381 at 240 turns**, slope **0.16/turn**. The +12 allocation and +4/+6 widget levels versus the pre-change 2333/2369 and 325/501 are the visible real-status/tool-detail UI, not a hidden-action slope.

**Workaround.** Plain application-side conditional construction. None needed upstream.

**Minimal upstream change.** None. A lazy subtree helper could improve ergonomics, but it would wrap the same `if` and would not change cost or capability.

**Rejected approaches.** Keeping invisible buttons with alpha zero preserves hit targets and tab stops that are not on screen. Reserving a row under every message caused layout jump and 24px of permanent vertical cost per turn. A widget-level `hidden` flag would still create the entity and misses the performance requirement.

**Hanabi reference.** `src/ecs/main_pane_system.h`, `MainPaneSystem::message_actions`, returns before `msg_actions` construction; `tests/ui/message_copy_on_hover.e2e` proves absent-at-rest and visible-on-hover; `tests/ui/message_actions_split_focus.e2e` proves focused state is pane-local; `scripts/events_gate.sh` records the allocation level and slope. Proof-only negative result.

CLASS: NOT A GAP

---

### #420 — `virtual_list` cannot index variable-height rows, so a transcript must rebuild or own its geometry tree

**What was wanted.** A list of mixed transcript rows whose layout cost is paid
when message geometry changes, not once per loaded message per frame. Rows are
bubbles, tool piles, date and unread dividers, thinking blocks, deliveries and
run outcomes; their heights depend on content, pane width, fold state and
settings.

**What happens.** afterhours' only list virtualizer is
`ui::imm::virtual_list` in
`vendor/afterhours/src/plugins/ui/imm_components.h:141-219`. Its contract says
every row is exactly one `row_height` (`:147-160`), derives the visible range by
dividing scroll offset by that constant (`:174-191`), and sizes both spacers as
`row count × row_height` (`:194-217`). It has no per-item height accessor,
prefix-height index, stable item key, revision, or invalidation range.

Hanabi therefore built every transcript `Item` and summed every height before it
could virtualize the draw. The operation counter measured 3,672.3 / 7,344.3 /
14,688.3 source-message visits per frame at 3,672 / 7,344 / 14,688 messages.
`transcript.pass1_measure` was 1.896 / 3.857 / 9.329 ms per frame in matched
300-frame runs and was the dominant cost.

**Rejected ideas.** Hashing every message every frame is the same O(messages)
walk with cheaper work inside it. Sharing one item list between split panes is
wrong once the panes have different widths. A map keyed by every observed width
is unbounded during a resize drag. Calling `virtual_list` after building a full
prefix-height vector saves widget construction but not the measured walk; that
is already the old path. Rebuilding only visible rows cannot place them because
the scroll offset is in cumulative-height space.

**Hanabi workaround.** A four-slot LRU retains one item vector and total height
per `(pane, thread)`. It rebuilds only the affected suffix for appends, content
updates, and fold toggles; width and global-setting changes replace one slot.

**Hanabi reference.** `src/ecs/transcript_item_index.h` —
`TranscriptItemIndex::update` implements the bounded index and explicit
invalidation. `src/ecs/components.h` — `Pane::note_transcript_mutation` carries
the source revision. `src/ecs/main_pane_system.h` — `render_transcript` builds a
changed suffix and consumes the retained list; the old full-pass path is gone.
Tests: `tests/unit/test_transcript_item_index.cpp` runs a 3,000-change
randomized differential against a from-scratch reference and covers the bound
and different-width panes; `tests/unit/test_pane_memory.cpp` covers same-ID
content and unread changes. Measurement/gate:
`scripts/perf_transcript_slope.sh` gates the visited-message ratio at 0.02; the
old path is 1.0 and the new 480-message run is 0.0026.

**Result and remaining cost.** The matched CPU ratios are 0.064x / 0.063x /
0.078x at 3,672 / 7,344 / 14,688 messages. Allocation calls rose 0.4–1.0%
while bytes allocated per frame fell 15–30%; the retained index is a deliberate
small-allocation trade. Pass 2 and minimap preparation still iterate the item
list. They are not fixed here: pass 2 is the virtualized widget build, and the
minimap represents the whole transcript. Their remaining costs are already
tracked by #138 and #341.

**Minimal upstream change.** Add a variable-height `virtual_list` overload with
a stable item key, caller-supplied height measurement, a retained prefix-sum
index, and `invalidate(first, count)` plus reset semantics. Its storage must be
bounded per list instance and width changes must replace one geometry version,
not accumulate a width-keyed history. Keep the current constant-height overload
as the zero-index fast path.

CLASS: MISSING / PERFORMANCE (extends #326)

---

### #445 — Middle-click bypasses the UI input model, hit resolution, and scripted injector

**What was wanted.** Browser-style middle-click-to-close on a tab, with the same
hit target and scripted coverage as left and right clicks.

**The afterhours mechanism.** `MousePointerState` carries only left and right
button state (`vendor/afterhours/src/plugins/ui/context.h:99-121`), and
`BeginUIContextManager` polls only buttons 0 and 1
(`vendor/afterhours/src/plugins/ui/systems.h:269-279`). In test mode,
`is_mouse_button_pressed` and `is_mouse_button_released` explicitly return false
for every button except 0; button 2 can never be injected
(`vendor/afterhours/src/plugins/e2e_testing/test_input.h:233-265`). The UI cannot
resolve a middle click to `hot_id`, and the harness cannot produce one.

**Correctness and cost.** A real button-2 press is visible only through the raw
graphics facade; under `--e2e`, the same call is always false. The Hanabi path
therefore has zero gesture-level scripted coverage. Its workaround scans the
open tab rectangles only on the button-2 edge: O(open tabs) per middle click,
zero scans on ordinary frames.

**Workaround.** Hanabi polls button 2, applies the tab strip's scroll and clip
math again, then calls the shared close model. This is proof-only at the gesture
boundary: model close semantics are tested, but the injector cannot exercise
the button that reaches them.

**Hanabi reference.** `src/ecs/tab_bar_system.h::TabBarSystem::for_each_with`
contains the raw `is_mouse_button_pressed(2)` poll and bounded visible-tab scan;
`tests/e2e/test_e2e.cpp::test_tab_close_fallback` and
`test_tab_close_reconciles_both_panes` guard the resulting state transition.
Gate: `make unit-e2e`; the missing gesture is why no `.e2e` claim is made.

**Rejected.** Mapping middle click to right click would make one physical button
open a menu and close a tab. Adding an invisible child button would still not
receive button 2 because the state and injector discard it first.

**Minimal upstream change.** Track middle down/pressed/released beside right,
resolve it through the same hit-test path, expose `is_middle_click(id)`, and let
the injector set button 2.

CLASS: MISSING

---

### #446 — A horizontal-only scroll view ignores vertical wheel and Shift+wheel intent

**What was wanted.** Chrome-like tab overflow: a native horizontal trackpad
moves X directly, while a mouse wheel over the strip and Shift+wheel also move
X, with macOS natural direction preserved.

**The afterhours mechanism.** The facade already returns both axes
(`vendor/afterhours/src/plugins/input_system.h:619-627`), but
`HandleScrollInput` maps Y only to a vertically enabled view and X only to a
horizontally enabled one (`vendor/afterhours/src/plugins/ui/systems.h:2088-2105`).
There is no axis fallback and no modifier policy. On a horizontal-only view an
ordinary wheel event with `(x=0,y=1)` moves exactly **0 px**; Shift changes
nothing because the system never reads it.

**Workaround.** Hanabi reads the vector once, prefers native X when present, and
otherwise maps Y to X while the pointer is over the tab strip or Shift is held.
The sign is passed through the same cached natural-scroll preference as every
other panel. This is O(1) per frame and does not rebuild a menu or tab model.

**Hanabi reference.** `src/ecs/tab_bar_system.h::TabBarSystem::for_each_with`
contains the tab-strip axis policy and calls `model::scroll_to_show` for active
visibility. `tests/e2e/test_e2e.cpp::test_tab_wheel_semantics` and
`test_tab_scroll_to_show_active` guard the policy; gates: `make uitest-shuffle
SEED=1234` and `make unit-e2e`.

**Rejected.** Raising `scroll_speed` cannot amplify a zero X delta. Using only Y
fixes a mouse wheel but discards native horizontal trackpad motion. Enabling Y
scroll on the strip creates a second offset the renderer never reads.

**Minimal upstream change.** Add a horizontal fallback policy to
`HasScrollView` (`None`, `WhenHorizontalOnly`, `WithShift`) and apply it before
the existing axis updates. Keep the vector input and direction flag unchanged.

CLASS: MISSING

---

### #447 — Drag groups start on press, with no click threshold or nested-control exclusion

**What was wanted.** A tab should remain a click until the pointer moves, and a
press on its close affordance must close rather than begin reordering.

**The afterhours mechanism.** `MousePointerState` already computes a 6 px
`press_moved` threshold (`vendor/afterhours/src/plugins/ui/context.h:118-121`,
`vendor/afterhours/src/plugins/ui/systems.h:281-295`). Drag groups do not use it:
`HandleDragGroupsPostLayout` starts dragging on `just_pressed` as soon as the
pointer is inside the direct child rect
(`vendor/afterhours/src/plugins/ui/systems.h:1714-1755`). It has no candidate
phase and no way to exclude a nested control.

**Correctness failure.** A stationary click is a drag for at least one frame.
A close button inside a draggable tab is inside the same child rect, so the drag
owner and close owner both qualify. This is a gesture arbitration failure, not
a styling difference.

**Workaround.** Hanabi carries four scalar drag fields, promotes the candidate
only after 4 px, and tests the close rectangle before accepting the candidate.
The scan is O(open tabs) only on press/release; movement is O(1).

**Hanabi reference.** `src/ecs/components.h::TabStripComponent::{has_drag_candidate,
clear_drag}` and `src/ecs/tab_bar_system.h::TabBarSystem::for_each_with`
demonstrate the candidate threshold and close exclusion.
`tests/e2e/test_e2e.cpp::test_tab_reorder_moves_and_preserves_active` plus the
scripted close-hit coverage guard the behavior; gates: `make uitest-shuffle
SEED=1234` and `make unit-e2e`.

**Rejected.** Using the vendor drag group and suppressing tab clicks would make
reordering work by deleting click-to-focus. Letting both handlers run makes a
close or focus race the drag release.

**Minimal upstream change.** Give drag groups a candidate phase keyed to
`mouse.press_moved`, and accept a predicate or descendant tag that excludes
nested interactive controls from drag start.

CLASS: FOOTGUN

---

### #448 — Drag-group hit-testing ignores ancestor scroll offsets and viewport clipping

**What was wanted.** Reorder tabs after the strip has overflowed, including a
tab partially clipped at either edge.

**The afterhours mechanism.** The library already has adjusted hit rectangles
for ordinary UI input (`vendor/afterhours/src/plugins/ui/systems.h:151-169`),
but drag start tests `child_cmp.rect()` directly
(`vendor/afterhours/src/plugins/ui/systems.h:1732-1736`) and drop targeting tests
`group_cmp.rect()` directly (`vendor/afterhours/src/plugins/ui/systems.h:1804-1809`).
Neither subtracts ancestor scroll nor intersects the clip viewport.

**Correctness failure.** After an ancestor scrolls 120 px, the drag source is
still tested 120 px from where it is drawn. A child wholly behind the clip can
still win because its raw rect remains inside the un-clipped group.

**Workaround.** Hanabi derives every slot from `baseX = runLeft - scrollX`,
intersects hit rectangles with `[runLeft, stripRight]`, and clamps the floating
tab to the same viewport. Cost is one bounded O(open tabs) pass per press and
release; off-screen tabs are skipped.

**Hanabi reference.** `src/ecs/tab_model.h::compute_drop_index` is the
scroll-aware drop decision used by `TabBarSystem::for_each_with`.
`tests/e2e/test_e2e.cpp::test_tab_reorder_drop_index` and
`test_tab_drag_across_scrolled_overflow` guard the model and overflow gesture.
Gates: `make unit-e2e` and `make uitest-shuffle SEED=1234`.

**Rejected.** Feeding the unadjusted vendor rects a fake mouse coordinate fixes
one ancestor but breaks top-level groups and nested offsets. Disabling drag once
there is overflow removes the feature exactly when the strip needs it.

**Minimal upstream change.** Route drag source and target tests through the same
ancestor-offset and clip-intersection helper used by ordinary hit testing.

CLASS: FOOTGUN

---

### #449 — The drag overlay copies flat label/color only, so a composite tab loses its affordances

**What was wanted.** A dragged tab should remain the same tab: title, pin,
close affordance, active accent, and hover/focus visuals.

**The afterhours mechanism.** `create_or_update_drag_overlay` constructs a new
entity and copies only `HasLabel`, font, and `HasColor`
(`vendor/afterhours/src/plugins/ui/systems.h:1533-1583`). Its own TODO says
children, nested divs, and icons do not render and proposes deep cloning or
reparenting at lines 1570-1573.

**Correctness and cost.** A composite tab becomes a flat rectangle while it is
being dragged; pin and close children disappear. Hanabi's workaround moves the
original composite and reflows siblings. Before this pass it also allocated an
N-float `renderX` vector every frame, even when no drag was active; that
steady-state allocation is rejected and removed here. The 20-tab arm measured
**774 allocations/frame on HEAD and 762 after**, while the existing arms held
at 811 / 1163 / 1025 allocations/frame and the thread arm moved 2750 -> 2748.

**Workaround.** Reposition the original tab entity at a raised layer during the
gesture and compute sibling slots directly from `(from,to,index)`. No clone, no
per-frame menu model, O(open tabs) draw work that the strip already pays.

**Hanabi reference.** `src/ecs/tab_bar_system.h::TabBarSystem::for_each_with`
uses the grep-stable `auto render_x =` path to reposition the original composite.
`tests/ui/tab_overflow_menu_and_pins.e2e` guards the in-flight drag position and
final order; `make alloc-gate` guards steady-state churn. The captured
`/tmp/hanabi_tab_drag_inflight.png` is **proof-only**, not a committed baseline:
no automated pixel assertion yet distinguishes a complete composite tab from a
flat overlay. The many-tabs baseline guards the non-dragged overflow state.

**Rejected.** Deep-cloning copies transient entity identity and callbacks with
no ownership contract. Reparenting the live subtree into an overlay perturbs
the immediate-mode tree and focus order. A flat text overlay is visibly wrong.

**Minimal upstream change.** Let a drag group render its original subtree at an
overlay transform without changing parentage, or accept an app-provided overlay
builder. Do not clone callbacks or state implicitly.

CLASS: MISSING

---

### #450 — `tab_container` asks for ellipsis, then makes its text the minimum width

**What was wanted.** Tabs should use available sibling slack, shrink to a
readable floor, then scroll; long titles should ellipsize rather than forcing
the whole strip wider.

**The afterhours mechanism.** `tab_container` sets
`TextOverflow::Ellipsis`, then sets `min_width` to `Dim::Text`
(`vendor/afterhours/src/plugins/ui/imm_components.h:1977-1995`). The vendored
test makes that policy explicit: a long tab must grow beyond its equal slice and
never shrink below content (`vendor/afterhours/tests/tab_container_test.cpp:7-10`,
`:19-37`). Ellipsis is unreachable at the point where a crowded strip needs it.

**Correctness failure.** One long title can consume sibling slack without a
floor or overflow viewport. Equal 1/N slices were previously rejected because
they truncated a long label despite unused space in short siblings; the current
opposite extreme prevents truncation at any pressure. Neither is Chrome's
three-stage policy.

**Workaround.** Hanabi does not use `tab_container`: it computes one uniform
width capped at 220 px, shrinks to 40 px, then scrolls while keeping the active
tab visible. The arithmetic is O(open tabs) and the draw loop skips wholly
off-screen tabs.

**Hanabi reference.** `src/ecs/tab_model.h::{kTabMinWidth,kTabMaxWidth,
compute_tab_width,compute_max_scroll,scroll_to_show}` and
`src/ecs/tab_bar_system.h::TabBarSystem::for_each_with` implement the
max→shrink→scroll ladder. `tests/e2e/test_e2e.cpp::test_tab_overflow_width` pins
the shipped 220 px cap; `test_tab_scroll_to_show_active` pins active visibility.
The many-tabs and split screenshots guard the rendered state. Gates: `make
unit-e2e`, `make validate-screenshots`, and `make alloc-gate`.

**Rejected.** Equal 1/N width wastes slack and truncates early. `Dim::Text` as a
minimum never truncates. Measuring every title every frame would add exactly the
per-frame string work this app's allocation gate exists to reject.

**Minimal upstream change.** Give `tab_container` caller-settable
`max_width`, `min_width`, and overflow policy; distribute slack above the floor,
then put the row in a horizontal scroll view when the sum reaches the floor.

CLASS: FOOTGUN

---

**Range note.** #451-#454 are intentionally unassigned. The candidate claims
were already #112 (tooltip/accessibility), #147 (scroll-view addressing), #171
(slot identity), #79/#136 (ellipsis/measurement), or capabilities that exist
(context menus and clipping). Padding the range would make the index worse.

**Remaining measured Hanabi paths, not new afterhours gaps.** These stay under
their existing numbers: #365 find-open is **5.452 ms/frame and 14,788
allocations/frame**; #368 a cold 2,000-thread sidebar query is **~165 ms**; #369
search plus Show More returns to **6,645 entities / 17.2 ms/frame**; #340 styled
text wrapping is about **10% of allocations** and doubles with two panes. The
rejected approaches and minimal app fixes remain in those entries. None is
renumbered into this range.

---

### #525 — A closed popover still resolves and retains a UI entity

**Exact afterhours mechanism.** `popover` calls `deref(ep_pair)` and adds
`HasMenuState` before it checks `open` in
`vendor/afterhours/src/plugins/ui/menu.h:263-271`. The closed path avoids the
panel, but it is not construction-free and requires a closed call to reset
`was_open_last_frame`.

**Measured result.** Hanabi's steady closed-overlay allocation gate remains
**811 / 1163 / 2735 / 1025 allocations per frame** for home20 / home2000 /
thread480 / draft6 after the guard. The values do not grow from the picker
refactor.

**Workaround.** Hanabi does not call `popover` while a picker has stayed closed.
It allows one transition call after close to reset vendor state, then returns
before `mk` on subsequent closed frames.

**Minimal upstream change.** Accept caller-owned open history, or move the
closed-state reset outside entity lookup so `open == false` can return before
`deref`.

**Rejected approaches.** Skipping every closed call without resetting state
breaks the next open: stale `was_open_last_frame` makes focus-within dismiss the
new panel immediately. Calling unconditionally preserves behavior but retains
work for an invisible surface.

**Hanabi reference.** `src/ecs/main_pane_system.h::render_model_popover` and
`render_effort_popover` carry the transition guard. `scripts/alloc_gate.sh`
records the unchanged closed-state levels above.

CLASS: PERFORMANCE / WORKAROUND

---

### #526 — Synthetic right-click does not reach direct mouse-button polling

**Exact afterhours mechanism.** `simulate_right_click` sets the injector's
`right_down` flag in `vendor/afterhours/src/plugins/e2e_testing/test_input.h`,
but `platform_test_input::is_mouse_button_pressed` only consults the injector
for button 0 at `platform_test_input.h:98-103`. Other buttons fall through to
the real graphics backend.

**Measured failure.** The same scripted secondary click opens Hanabi's row menu
through `UIContext::is_right_click`, but cannot open the tab menu, which polls
`graphics::is_mouse_button_pressed(1)` directly. The click instead leaves no
`TAB ACTIONS` node for the harness to inspect.

**Workaround.** Row menus use `UIContext::is_right_click`. The screenshot and
Escape suites seed the tab menu through a render-only test hook, so its state is
deterministic without pretending the synthetic secondary button reached the
platform poll.

**Minimal upstream change.** Make the test-aware platform wrapper map the
secondary-button id to `right_down`, and route application polling through that
wrapper.

**Rejected approaches.** Treating a synthetic right click as button 0 would
make ordinary click handlers fire too. Rewriting product input semantics solely
for the harness would test a branch users never take.

**Hanabi reference.** `src/ecs/tab_bar_system.h::render_tab_menu` is the direct
polling path. `src/ecs/sidebar_system.h::render_row_menu` uses the working
UIContext path. `src/main.cpp` exposes the render-only `tab-menu` state, and
`tests/ui/context_menus_escape.e2e` plus `tab_context_menu_escape.e2e` verify
single-owner Escape behavior.

CLASS: MISSING

---

### #527 — Secondary controls still have no native accessibility semantics

**Exact afterhours mechanism.** `ComponentConfig` exposes visual labels, debug
names, focus flags, and button variants, but no accessibility name,
description, role, or platform adapter in
`vendor/afterhours/src/plugins/ui/component_config.h:44-191`. This is another
concrete surface of #112/#458 rather than a separate API family.

**Measured result.** Keyboard focus and activation are scriptable for visible
controls, but the tests can only address debug names and rendered text. They
cannot inspect a macOS accessibility tree or prove a control's semantic role.

**Workaround.** Every icon-only close control is paired with an explicit title
or nearby visible action text, and every actionable element has a stable debug
name. This improves discoverability and testability but does not supply native
assistive metadata.

**Minimal upstream change.** Add semantic role/name/value fields to
`ComponentConfig`, bridge them to the host accessibility tree, and expose them
to `assert_ui`.

**Rejected approaches.** Debug names are test instrumentation, not user-facing
labels. Tooltips do not provide keyboard or screen-reader semantics.

**Hanabi reference.** `src/ui/secondary_surface.h` centralizes visible control
roles; `tests/ui/new_task_focus_escape.e2e`,
`palette_focus_escape.e2e`, and `search_focus_escape.e2e` prove keyboard focus
and dismissal. Native accessibility remains unresolved.

CLASS: DUPLICATE → #112 / #458

---

### #560 — Native application menus are outside afterhours' host contract

**Exact mechanism.** The macOS backend creates and feeds an `NSApplication` and an `NSWindow`, but afterhours exposes no application-menu model. Its public input boundary starts at `graphics::is_key_pressed` and `input::InputSystem`; no `NSMenu`, responder-chain, role, or selector concept crosses that boundary.

**Result.** A bundled afterhours app cannot declare File, Edit, View, Window, and Help menus through the framework. This is not a rendering-widget omission: those menus belong to the host application and must exist before the immediate-mode tree is involved.

**Workaround.** Hanabi installs the native menu hierarchy in its Obj-C++ host seam only for a real bundled, windowed launch. The headless frame path never calls the installer.

**Hanabi reference.** `src/menubar.mm::install_main_menu`, `src/main.cpp::app_frame`, and `src/menubar.h::menubar_diagnostics`; gates: `make app` and `make verify-app`.

**Minimal upstream change.** None required. A small documented host hook after window creation would improve discoverability, but menu ownership remains application policy.

CLASS: NOT A GAP / HOST RESPONSIBILITY

---

### #561 — AppKit consumes a menu key equivalent before afterhours sees the key

**Exact mechanism.** AppKit resolves an `NSMenuItem.keyEquivalent` in `NSApplication` event dispatch. A matching event invokes the menu item's action instead of reaching the Sokol view's key handler, so `graphics::is_key_pressed` and every afterhours input wrapper correctly report no press.

**Correctness failure.** Adding Cmd+F, Cmd+K, or Cmd+W to a native menu while leaving an unrelated app-side key handler creates two paths with different ownership; the bundled app runs the menu action and the developer executable runs the afterhours action. A menu chord with no routed action silently disables the existing shortcut.

**Workaround.** Hanabi's menu items enqueue the same central command identifier that the keyboard matcher and command palette dispatch. There is one command definition and one request handler; the menu changes delivery, not meaning.

**Hanabi reference.** `src/shortcuts.h::kDefinitions`, `src/ecs/command_system.h::dispatch`, `src/menubar.mm::HanabiMenuTarget::onCommand`, and `src/ecs/palette_system.h::run`; `tests/unit/test_shortcuts.cpp` guards mapping uniqueness and conflict behavior.

**Minimal upstream change.** None inside afterhours can make AppKit deliver a consumed key. Document the host boundary and provide an optional command-dispatch hook for native shells.

CLASS: PLATFORM / APP WORKAROUND

---

### #562 — The E2E parser records SUPER, but the key command never holds it

**Exact mechanism.** `parse_key_combo` sets `KeyCombo::super` for `SUPER+`, `WIN+`, and `META+` in `vendor/afterhours/src/core/key_codes.h:285-332`. `HandleKeyCommand` in `vendor/afterhours/src/plugins/e2e_testing/command_handlers.h:52-136` holds and later releases only Ctrl, Shift, and Alt. It never calls `set_key_held(keys::LEFT_SUPER)` and has no pending-super release slot.

**Measured failure.** `key SUPER+K` presses K with no modifier. Any product path that requires `MOD_SUPER` is unreachable from the scripted harness even though the parser accepts the spelling.

**Workaround.** Hanabi tests command mapping and conflicts as pure data. Its E2E-only modifier read interprets the harness's Ctrl alias as Command, while shipping builds require the real Super state.

**Hanabi reference.** `src/keys.h::shortcut_modifiers`, `tests/unit/test_shortcuts.cpp`, and `tests/ui/shortcut_recorder.e2e`.

**Minimal upstream change.** Hold `LEFT_SUPER` when `combo.super` is true, add `pending_super`, and release it with the other modifiers.

CLASS: MISSING / TESTING

---

### #563 — `CMD+` means Ctrl in scripts, not Command on macOS

**Exact mechanism.** `parse_key_combo` maps the `CMD+` prefix to `KeyCombo::ctrl` at `vendor/afterhours/src/core/key_codes.h:299-303`. `HandleKeyCommand` then holds `keys::LEFT_CONTROL`. This is deliberate cross-platform aliasing, but it does not simulate the modifier named by the script on macOS.

**Correctness failure.** A script that says `key CMD+P` proves a Ctrl chord unless the application adds a test-only alias. It cannot prove AppKit menu-key-equivalent routing or the real Super modifier.

**Workaround.** Hanabi confines the alias to `AFTER_HOURS_ENABLE_E2E_TESTING`; production `shortcut_modifiers()` never treats Ctrl as Command. Native menu diagnostics remain a separate bundled-app check.

**Hanabi reference.** `src/keys.h::shortcut_modifiers`, `src/menubar.mm::menubar_diagnostics`, and `tests/ui/shortcut_recorder.e2e`.

**Minimal upstream change.** Make `CMD+` hold Super on macOS and reserve `CTRL+` for Control. Keep an explicit portable alias under a different spelling.

CLASS: FOOTGUN / DUPLICATE → #256

---

### #564 — Synthetic input is intentionally absent from shipping builds

**Exact mechanism.** The injector paths in `input_system.h`, `platform_test_input.h`, and the E2E runner are compiled under `AFTER_HOURS_ENABLE_E2E_TESTING`. A normal bundled binary has no supported API to push a synthetic key press into the input collector.

**Result.** A native host bridge cannot call the test injector to make a clicked Edit menu item look like Cmd+C in production. Enabling the E2E plugin in a shipping app would expose test-only global state and is the wrong fix.

**Workaround.** Hanabi's native responder bridge replays a local `NSEvent` directly to its own window. That enters the real Sokol/AppKit path and is used only after the standard responder selector has no native widget to handle it.

**Hanabi reference.** `src/menubar.mm::HanabiEditBridge` and `replay_edit_key`; the production and E2E object directories remain separate in `makefile`.

**Minimal upstream change.** None. Keeping synthetic injection test-only is correct.

CLASS: NOT A GAP / SECURITY BOUNDARY

---

### #565 — Text editing has no imperative command surface for a native responder

**Exact mechanism.** `handle_clipboard_and_undo` in `vendor/afterhours/src/plugins/ui/text_input/utils.h:514-594` exposes editing only as `ctx.pressed(InputAction::TextUndo/TextRedo/TextCopy/TextCut/TextPaste/TextSelectAll)`. The mutation helpers operate on `HasTextInputState`, but an outside system that mutates that state before the immediate widget runs is overwritten when the widget re-seeds from its bound `std::string`.

**Correctness failure.** A native Edit menu cannot safely call `undo`, `paste`, or `select all` against the focused afterhours field. Mutating only the state appears to work for part of a frame and then restores the old bound string.

**Workaround.** Hanabi first uses standard AppKit selectors through the responder chain. Its bridge is installed after the content view, so a real native text responder wins; only the unhandled afterhours case replays the corresponding local key event into the window, letting the widget update its state and bound string together.

**Hanabi reference.** `src/menubar.mm::HanabiEditBridge`, `src/ecs/keyboard_focus.h::focused_text_field`, and `src/ecs/text_edit_chords_system.h`.

**Minimal upstream change.** Expose imperative edit operations that take the state and its bound string together, or expose a focused-text command sink whose implementation runs inside the widget's synchronization point.

CLASS: MISSING

---

### #566 — Native Edit capabilities still depend on magic enum names

**Exact mechanism.** `handle_clipboard_and_undo` checks `magic_enum::enum_contains<InputAction>("TextUndo")` and the equivalent names for redo, copy, cut, paste, and select-all before compiling each branch. A responder bridge can replay the right key and still get no behavior if the app enum omitted the matching name.

**Correctness failure.** The native menu looks complete and the key event reaches the widget, but one Edit item can remain inert with no compiler error or runtime diagnostic.

**Workaround.** Hanabi keeps its text-action enum and mapping under direct unit coverage; standard AppKit responders remain independent of that enum.

**Hanabi reference.** `src/input_mapping.h::InputAction`, `tests/unit/test_input_pipeline.cpp::test_enum_carries_the_names_afterhours_gates_on`, and the Edit menu in `src/menubar.mm::install_main_menu`.

**Minimal upstream change.** Replace string-reflected opt-in with a declared text-action trait or static assertion listing unsupported actions.

CLASS: FOOTGUN / DUPLICATE → #255

---

### #567 — Headless UI assertions cannot observe an AppKit menu hierarchy

**Exact mechanism.** `assert_ui` reads afterhours' immediate-mode visible-text registry. An `NSMenu` is owned and rendered by AppKit, has no afterhours entity, and requires a real `NSApplication` plus window. Hanabi's headless screenshot path intentionally never calls the menu installer.

**Result.** A headless screenshot or `.e2e` script cannot prove that File/Edit/View/Window/Help exist, inspect a native key equivalent, or invoke a standard selector. Attempting to install menus headlessly would add platform state to every test and violate the existing no-side-effect contract.

**Workaround.** Hanabi tests the command/key/conflict model purely, exercises the recorder in the headless widget tree, and exposes a bundled-only one-line native diagnostic. The packaged app is the manual/permission-gated arm.

**Hanabi reference.** `src/menubar.h::menubar_diagnostics`, `src/main.cpp` under `HANABI_NATIVE_MENU_DIAGNOSTIC`, `tests/unit/test_shortcuts.cpp`, and `tests/ui/shortcut_recorder.e2e`.

**Minimal upstream change.** None in the renderer. A host-test adapter could serialize native menu metadata without installing it, but it cannot make `assert_ui` see AppKit objects.

CLASS: PLATFORM-GATED / DUPLICATE → #308

---

### #568 — Modifier release state has no Super slot

**Exact mechanism.** `key_release_detail` in `command_handlers.h:54-68` stores `pending_ctrl`, `pending_shift`, and `pending_alt`; `HandleKeyReleaseSystem` releases those three plus the primary key at lines 70-99. There is no `pending_super`, matching the missing hold path in #562.

**Failure mode.** Adding only a Super hold to `HandleKeyCommand` would leave Command stuck for the rest of the script. The parser and injector already know the key; the frame-lifetime bookkeeping is the missing half.

**Workaround.** Hanabi does not patch the vendor. Its E2E alias reuses the complete Ctrl hold/release path, and its pure tests carry the real production modifier bit.

**Hanabi reference.** `src/keys.h::shortcut_modifiers` and `tests/unit/test_shortcuts.cpp::test_serialization_round_trips`.

**Minimal upstream change.** Add `pending_super` to the release state, reset it, set it from `combo.super`, and release `keys::LEFT_SUPER` at the same countdown.

CLASS: MISSING / TESTING

---

### #569 — The non-layered input map has no remapping method

**Exact mechanism.** `ProvidesLayeredInputMapping::set_binding` and `clear_binding` are explicit runtime APIs at `vendor/afterhours/src/plugins/input_system.h:1253-1264`. The ordinary `ProvidesInputMapping` used by Hanabi exposes a public `std::map` field but no equivalent method, validation, conflict result, persistence hook, or change notification.

**Result.** Directly mutating the map can change an action chord, but it cannot explain a conflict, update native menu metadata, or keep a persisted setting and palette hint in sync. It is storage, not a shortcut configuration contract.

**Workaround.** Hanabi keeps customizable application commands in one pure definition table, validates and persists overrides in `Settings`, matches them in one command system, and refreshes AppKit menu equivalents from a revision counter. Low-level text editing remains in the stable afterhours action map.

**Hanabi reference.** `src/shortcuts.h`, `src/settings.h`, `src/settings.cpp`, `src/ecs/command_system.h`, `src/ecs/shortcuts_system.h`, and `tests/unit/test_shortcuts.cpp`.

**Minimal upstream change.** Add validated `set_binding`/`clear_binding` parity to `ProvidesInputMapping` plus an optional conflict query. Persistence and menu presentation remain application policy.

CLASS: TEDIOUS / APP WORKAROUND

---

### #570 — A fontstash size is not a native point size

At 13, fontstash reports a 13.0 logical-pixel line for every face. CoreText reports 15.234 for Roboto, 16.120 for Atkinson Hyperlegible, 15.514 for SF Pro Text, and 16.750 for Optimistic. The role values were right; the face-specific point conversion was missing.

**Hanabi reference.** `src/ui/theme.h::type::set_point_scale` converts the point tokens without scaling layout geometry. `src/ui/font_system.cpp::apply` supplies the selected face's ratio. Measurements: `docs/font-system-audit.md`.

CLASS: MISSING / WORKAROUND

---

### #571 — afterhours has no installed-font catalog

`FontManager` loads a path but cannot list installed families, resolve a PostScript face, or discover weight files. A desktop app otherwise has to bundle every selectable face.

**Hanabi reference.** `src/native_extras.mm::native_font_faces` resolves a bounded allowlist through CoreText behind the app's ObjC++ seam. No font binary was added.

CLASS: MISSING

---

### #572 — Path-only loading cannot select a face inside a collection

The macOS system descriptor maps regular through bold to one `SFNS.ttf` path. `load_font_from_file` accepts no collection face index or PostScript name, so those requests would select the same face.

**Hanabi reference.** `src/native_extras.mm` offers System only when separate `SFProText-*.otf` paths resolve; otherwise the app falls back to bundled Roboto.

CLASS: MISSING / FOOTGUN

---

### #573 — `FontManager::load_font` stores failure as a valid-looking key

The backend logs a failed `fonsAddFont`, but `load_font` returns the manager and stores `FONS_INVALID`. A later map lookup succeeds and the failure appears only at render time.

**Hanabi reference.** `src/ui/font_system.cpp::load_face` checks `is_font_loaded` after registration and falls back deterministically.

CLASS: FOOTGUN

---

### #574 — `measure_text_internal` ignores `FontManager::active_font`

It always sets the backend-global `g_active_font`, which is initialized by the first successful load and is not changed by `FontManager::set_active` or by replacing `FontManager["__default"]`. A live face switch can draw one face and measure another.

**Hanabi reference.** `src/ui/font_system.cpp::measure_advance` resolves the selected default and weight, sets that explicit font ID, and returns pen advance. `src/ui/theme.h::text_px` uses it.

CLASS: FOOTGUN / CRITICAL

---

### #575 — Advance and ink bounds are different answers under similar APIs

At point-correct 13-point samples, fontstash's ink width was 1-2 logical pixels wider than pen advance. `measure_text_internal` returns advance; `measure_text` discards it and computes `bounds[2]-bounds[0]`. Layout needs advance and density analysis needs ink.

**Hanabi reference.** `src/ui/font_system.cpp::measure_advance` keeps advance semantics. `docs/font-system-audit.md` reports both numbers. This narrows #137.

CLASS: DUPLICATE → #137

---

### #576 — Weighted renderer measurement already works

The pinned vendor resolves `<base>@<weight>` in `FontManager::resolve_weighted`, and both renderers select that face before measuring and drawing. The broad #82 claim is stale; only app code calling `measure_text_internal` directly has that failure.

**Hanabi reference.** `src/ecs/tab_bar_system.h` and `src/ecs/settings_system.h` use `with_font_weight` after `src/ui/font_system.cpp` registers the emphasis alias.

CLASS: NOT A GAP; CORRECTS #82

---

### #577 — Loaded font IDs cannot be unloaded and cap at sixteen

The sokol backend appends successful loads to `g_font_ids[MAX_FONTS]`; `FontManager` has no unload or replacement-aware registration API. Re-loading on every picker change grows renderer state.

**Hanabi reference.** `src/ui/font_system.cpp` loads each family/weight under one stable name, aliases existing `Font` values on switches, and statically bounds the reachable set at 14.

CLASS: PERFORMANCE / FOOTGUN

---

### #578 — Headless 2x layout zoom is not Retina rasterization

Windowed text multiplies fontstash size by `sapp_dpi_scale()` and scales the draw matrix back. Headless hardcodes `dpi_scale()` to 1; `HANABI_UI_SCALE=2` doubles adaptive layout before downsampling. On the matched System Bold frame, 1x and 2x differed in 10.64% of pixels and the 2x sidebar score worsened from 12.73% to 14.75%.

**Hanabi reference.** `src/test_hooks.h::ui_scale`, `scripts/shoot_hanabi.sh`, and `docs/font-system-audit.md`. This is the font-specific measurement of #101.

CLASS: DUPLICATE → #101

---

### #579 — A font replacement has no generation in the measurement cache key

`TextMeasureCache` keys text, font name, size, and spacing. Replacing a face under the same name moves none of them, and visible strings never age out because hits refresh their generation.

**Hanabi reference.** `src/util/text_epoch.h`, `src/util/text_cache.h`, and `src/ui/font_system.cpp::apply` bump the app epoch and clear afterhours' cache after every effective family or emphasis change. This completes Hanabi's workaround for #190.

CLASS: DUPLICATE → #190
### #580 — The system manager has no cancellable background-job primitive

**Exact afterhours mechanism.** `SystemManager` owns three vectors of synchronous
`SystemBase` instances and runs each one inline in `tick`, `fixed_tick`, and
`render` (`vendor/afterhours/src/core/system.h:432-581`). It has no executor,
result mailbox, cancellation token, or replace-by-key operation.

**Hanabi reference.** `src/ecs/components.h::Pane` owns transcript, disk-read,
and load-older futures plus three superseded-future vectors.
`src/ecs/loader_system.h::service_pane` polls them because a switch cannot wait
for disk or network.

**Rejected.** Destroying the old `std::future` on each switch can join the
`std::async` worker and put the beachball back on the UI thread. Detaching one
thread per click loses shutdown ownership and can outlive the client.

**Remaining cost.** Superseded work still runs to completion and its future stays
resident until a later frame reaps it. The cost is bounded by switch rate times
the backend timeout, not by the transcript LRU.

**Minimal upstream change.** A keyed background job queue with cancellation,
latest-generation delivery, and a non-blocking completion drain in the update
phase.

CLASS: MISSING

---

### #581 — A system has no deactivate or hidden-transition hook

**Exact afterhours mechanism.** `SystemBase` exposes frame callbacks
(`should_run`, `once`, iteration hooks, `after`) but no transition callback when
a system stops being applicable (`vendor/afterhours/src/core/system.h:91-116`).

**Hanabi reference.** `src/ecs/session_search_system.h::release_corpus` is called
when the search panel becomes closed so its full-text corpus is released. The
app has to remember `wasOpen_` solely to detect that transition.

**Rejected.** Keeping the corpus until the panel is opened again makes a closed
surface retain every indexed transcript body twice. Rebuilding every frame
releases nothing and repeats disk work.

**Remaining cost.** Every conditional system that owns non-entity state needs
its own previous-active bit and teardown branch.

**Minimal upstream change.** Add `on_activate` and `on_deactivate` hooks around
`should_run` transitions.

CLASS: MISSING

---

### #582 — NOT A GAP: transcript payload ownership belongs above the ECS

**Exact afterhours mechanism.** Components are arbitrary application values;
`Entity` and `SystemManager` do not prescribe persistence or payload caching.
Nothing in afterhours creates or retains `api::Session` or `api::Message`.

**Hanabi reference.** `src/ecs/transcript_cache.h::TranscriptCache` is the one
RAM owner for inactive transcript payloads: five threads, twenty messages each.
`src/api/disk_cache.h` and `src/api/disk_cache.cpp` are the durable tier, and
`src/ecs/loader_system.h` reloads misses asynchronously.

**Rejected.** Adding a second framework LRU would duplicate recency, eviction,
and stale-result policy. Keeping one `Session` per tab would make memory linear
in every tab ever left open.

**Remaining cost.** Two visible panes hold their current newest-message windows
in addition to the five-entry hot set. That is intentional and bounded by two.

CLASS: NOT A GAP

---

### #583 — NOT A GAP: the entity pool exposes its retained high-water count

**Exact afterhours mechanism.** Cleanup moves entities into `entity_pool_` only
while it is below `max_pool_size_`, and `pool_size()` exposes the retained count
(`vendor/afterhours/src/core/entity_collection.h:53-80,347-400`).

**Hanabi reference.** `src/util/mem_ladder.h` reads live malloc blocks and bytes;
`src/main.cpp::hold_note` reports live entities separately. The churn audit uses
both rather than calling a pooled entity a live leak.

**Rejected.** Clearing the pool after every tab close trades a stable high-water
mark for allocator churn. Treating `get_entities().size()` as total retained
memory misses the pool but is an instrumentation error, not missing ownership.

**Remaining cost.** The pool keeps its configured high-water allocation until
process exit; callers that reserve too high still pay that level.

CLASS: NOT A GAP

---

### #584 — NOT A GAP: the shared text-measure cache is bounded and observable

**Exact afterhours mechanism.** `TextMeasureCache` has a 4,096-entry default,
LRU eviction, age pruning, `clear`, `size`, hit/miss counters and a configurable
cap (`vendor/afterhours/src/core/text_cache.h:29-140`).

**Hanabi reference.** `src/ecs/main_pane_system.h::render_cache` owns the
separate transcript render cache because it stores app-specific display bodies,
heights and hugged widths rather than font measurements.

**Rejected.** Moving transcript `MsgRender` values into `TextMeasureCache` would
mix incompatible keys and still leave item geometry and markdown state with no
owner.

**Remaining cost.** Styled text still rebuilds its own wrapped run layout every
frame; that is the existing #340 family, not a new cache-lifetime gap.

CLASS: DUPLICATE → #340

---

### #585 — The ECS has no retained-byte attribution by system or component

**Exact afterhours mechanism.** `EntityCollection` exposes vectors and the pool
count, and `Arena` exposes its own used/capacity/peak counters, but no API walks
component payloads or attributes heap bytes to a system
(`vendor/afterhours/src/core/entity_collection.h:33-80`,
`vendor/afterhours/src/memory/arena.h:50-169`).

**Hanabi reference.** `src/util/mem_ladder.h` combines process RSS with a malloc
heap walk, while `src/main.cpp::hold_note` manually prints every container whose
entry count matters.

**Rejected.** RSS alone cannot say which owner retained bytes. `sizeof(Component)`
misses strings, vectors, futures and shared ownership, so it would print a precise
wrong answer.

**Remaining cost.** Every new cache must add its own count to Hanabi's hold note
or it is invisible until a heap stack walk names it.

**Minimal upstream change.** Optional per-component retained-size callbacks and
a SystemManager snapshot that reports live, pooled and externally-owned bytes
separately.

CLASS: MISSING

---

### #586 — The framework has no memory-pressure or cache-purge event

**Exact afterhours mechanism.** The system lifecycle is frame-only and the
platform layer exposes no memory-pressure callback. The UI and texture caches
can be cleared explicitly, but nothing coordinates that with OS pressure.

**Hanabi reference.** `src/ecs/transcript_cache.h`,
`src/ecs/transcript_render_cache.h`, `src/ecs/transcript_item_index.h`, and
`src/search/find_memo.h` each own an independent bounded cache with `clear`.

**Rejected.** Polling RSS every frame is expensive, lagging, and cannot tell
reclaimable cache from live state. Clearing every cache on every tab switch
recreates the split-pane thrash measured in `docs/perf/MEMORY.md`.

**Remaining cost.** Bounds prevent runaway growth, but a pressured machine
cannot ask all reclaimable caches to shrink before the OS compresses or kills
the process.

**Minimal upstream change.** A platform memory-pressure event distributed once
through `SystemManager`, with app-owned handlers deciding what can be discarded.

CLASS: MISSING

---

### #587 — Background completions have no frame-safe mailbox

**Exact afterhours mechanism.** Systems run serially over the live entity
collection; there is no queue for worker-produced values to be committed at a
known point in the frame (`vendor/afterhours/src/core/system.h:464-581`).

**Hanabi reference.** `src/ecs/loader_system.h` manually polls every future with
`wait_for(0)` and applies results only after checking pane id and cache epoch.
`src/ecs/components.h::Pane::accepts_disk_read` is the stale-result fence.

**Rejected.** Letting worker callbacks mutate ECS state races rendering. Applying
by session id alone fails on A→B→A because the old A result has the right id and
the wrong generation.

**Remaining cost.** Each async feature repeats pending flags, target ids,
futures, superseded storage and poll code.

**Minimal upstream change.** A typed main-thread mailbox whose envelopes carry a
replace key and generation.

CLASS: MISSING

---

### #588 — NOT A GAP: skeleton and stale metadata are ordinary app UI states

**Exact afterhours mechanism.** Immediate-mode elements can draw any loading
state the application describes; the framework does not own a data model or
know whether content is stale.

**Hanabi reference.** `src/ecs/components.h::Pane` carries `LoadState`, selected
and loading ids, while `src/ecs/main_pane_system.h::render_transcript` paints the
skeleton only when no matching stale transcript is available.

**Rejected.** A generic framework skeleton would still need application-defined
geometry and freshness semantics, while hiding whether the stale transcript is
safe for the selected pane.

**Remaining cost.** Hanabi owns the spinner and metadata copy. The UI-thread
switch path remains a small model mutation plus async dispatch.

CLASS: NOT A GAP

---

### #589 — SystemManager has no per-system CPU accounting seam

**Exact afterhours mechanism.** `tick`, `fixed_tick`, and `render` invoke systems
directly with no before/after timing callback or stable system name
(`vendor/afterhours/src/core/system.h:464-581`).

**Hanabi reference.** `src/util/prof.h` supplies app-owned
`CLOCK_THREAD_CPUTIME_ID` scopes, and `tests/e2e/test_perf.cpp` measures cached,
uncached and async-dispatch switch CPU on that clock.

**Rejected.** Wall-clock frame thresholds on a shared Mac reverse conclusions
under contention. Timing the whole frame cannot identify which system moved.

**Remaining cost.** Every hot path needs a manual scope, and uninstrumented
systems remain one aggregate remainder.

**Minimal upstream change.** Optional named before/after callbacks around each
system invocation, with no timing dependency when disabled.

CLASS: MISSING

---

### #540 — NOT A GAP: a host can retain the last Metal frame by returning before the UI systems run

**What was wanted.** Stop rebuilding and relaying out an unchanged immediate-mode tree without editing `vendor/afterhours`.

**Measured result.** On the same ten-second Home fixture, the legacy path ran 1,200 full frames, 136.432 thread-CPU ms/s, and 71,044.3 allocations/s. Returning before `SystemManager::run` ran 20 full frames, 2.761 thread-CPU ms/s, and 1,774.1 allocations/s. A real windowed run fell from 105.576 to 20.896 CPU ms/s.

**Workaround.** Hanabi owns `app_frame`, so it leaves the already-presented Metal layer intact on an idle callback. The old #27 claim considered skipping only the app's emit systems after `ClearUIComponentChildren`; skipping the entire framework frame is the missing seam.

**Rejected ideas.** Skipping only user systems empties the tree. Rebuilding but skipping draw keeps most structural cost. Editing the submodule violates the vendor boundary.

**Minimal upstream change.** None required for an app with its own host loop. A documented `should_render` callback would make the seam discoverable.

**Hanabi reference.** `src/main.cpp::app_frame`, `src/frame_activity.h::FrameActivityPolicy`, `scripts/idle_gate.sh`, and `docs/perf/IDLE.md`. `make idle-gate` is verified red with `HANABI_IDLE_DISABLE=1`.

CLASS: NOT A GAP / CORRECTION TO #27

---

### #541 — Metal ignores `RunConfig::target_fps`

**What was wanted.** Prototype a lower idle frame rate through the public run configuration.

**Measured result.** Hanabi sets `target_fps = 120`, but the Metal backend copies no such field into `sapp_desc`; callback rate follows `NSScreen.maximumFramesPerSecond / swap_interval`. The fixed-10-fps prototype therefore had to gate callbacks in app code. It produced 93 full frames in ten logical seconds and 11.401 CPU ms/s.

**Workaround.** Hanabi treats the display callback as a cheap clock and schedules full frames itself.

**Rejected ideas.** `graphics::set_target_fps(10)` is a documented no-op on this backend. A permanent 10 fps app-level gate saved CPU but added a measured 108 ms worst-case input wait.

**Minimal upstream change.** Map `RunConfig::target_fps` to `MTKView.preferredFramesPerSecond`, and allow changing it at runtime.

**Hanabi reference.** `src/main.cpp` sets the windowed `RunConfig`; `HANABI_IDLE_FIXED_10FPS` is the measured prototype; `tests/unit/test_frame_activity.cpp::fixed_ten_fps_prototype_adds_visible_input_latency` holds the rejection.

CLASS: MISSING / PERFORMANCE

---

### #542 — The Metal host has no event-triggered request-frame primitive

**What was wanted.** Run at a very low idle cadence while drawing immediately when input or background data arrives.

**Measured result.** A fixed 10 fps policy reduced deterministic CPU from 136.432 to 11.401 ms/s but delayed an event arriving after a tick past 100 ms. Event-driven retention kept the same event to one display callback while lowering CPU further to 2.761 ms/s.

**Workaround.** The display link keeps firing; Hanabi checks cheap wake signals every callback and returns before the full frame unless one is set.

**Rejected ideas.** Lowering `preferredFramesPerSecond` alone loses one-frame input latency. Sleeping in `app_frame` blocks AppKit and makes the latency worse.

**Minimal upstream change.** Expose `request_frame()` plus an idle/paused display-link mode whose event callback, worker completion, and window invalidation paths all request one frame.

**Hanabi reference.** `src/frame_activity.h` separates immediate wake reasons from cadence reasons. `src/main.cpp::frame_input_activity` and `metal_take_window_activity` feed the policy.

CLASS: MISSING / PERFORMANCE

---

### #543 — UI clear, emit, layout, input resolution, and draw are one indivisible frame

**What was wanted.** Run cheap polling or input work without clearing and rebuilding the UI tree.

**Measured result.** Current main's idle Home frame was 1.4855 ms of thread CPU and 811 allocations. `UIPluginPreUpdateBridge` clears every child list before app systems, and `UIPluginPostUpdateBridge` always rebuilds mapping, solves layout, resolves input, and cleans the collection.

**Workaround.** Hanabi either runs the whole frame or none of it. Background readiness and input are inspected outside `SystemManager::run`; a positive result admits one complete frame.

**Rejected ideas.** Running pre-update plus input without app emit leaves no tree. Running app emit without post-layout leaves stale rectangles. Partial registration duplicates the framework's required ordering.

**Minimal upstream change.** Split polling/input collection from tree mutation, then expose a retained-tree pass that can resolve events against the last layout without clearing children.

**Hanabi reference.** `src/frame_activity_collect.h` performs the outside-frame checks; `src/main.cpp::app_frame` gates the indivisible `systemManager->run(dt)` call. The pinned `vendor/afterhours/src/plugins/ui/utilities.h::UIPluginPreUpdateBridge` remains unchanged.

CLASS: MISSING / ARCHITECTURE

---

### #544 — Input has no public, non-consuming “anything happened” snapshot

**What was wanted.** Decide whether to run the expensive frame without consuming the key, character, pointer, button, or wheel event the UI frame must later process.

**Measured result.** The backend stores all edges in `metal_detail::InputState`, but public reads are per-key/per-button and character reads consume the queue. Scanning the private state lets Hanabi wake on the same callback; the transition test holds pointer and key latency to one callback.

**Workaround.** Hanabi's host translation unit reads the header-visible Metal state and reduces it to pointer/key activity bits before the framework clears edge state.

**Rejected ideas.** Polling every possible public getter cannot see the character queue without consuming it. Remembering only mouse position misses clicks, keys, repeats, wheel, and paste.

**Minimal upstream change.** Expose `input_activity()` with non-consuming pointer/key/text/window bit flags, or an event generation counter sampled by the host.

**Hanabi reference.** `src/main.cpp::frame_input_activity` and `tests/unit/test_frame_activity.cpp::{pointer_input_wakes_on_the_next_callback,key_input_wakes_on_the_next_callback}`.

CLASS: MISSING / FOOTGUN

---

### #545 — Window exposure and backing changes are hidden behind the Sokol event callback

**What was wanted.** Preserve the retained frame across idle time without leaving an exposed, restored, resized, or Retina-changed window stale.

**Measured result.** The backend's `sokol_event_cb` handles input only and is hardwired into `sapp_desc`; the app receives no event callback. Hanabi now turns AppKit resize, expose, key-window, miniaturize, deminiaturize, backing-property, and app-activation notifications into one-frame wakes.

**Workaround.** An app-owned Objective-C observer in `src/sokol_impl.mm` publishes resize/exposure bits consumed by the next frame callback.

**Rejected ideas.** Comparing width and height catches resize but not exposure, activation, restore, or backing-scale changes. Redrawing at 60 fps forever hides the bug by refusing to idle.

**Minimal upstream change.** Forward platform lifecycle events to the host, or mark the frame dirty internally and expose that bit before `frame_cb`.

**Hanabi reference.** `src/sokol_impl.mm::HanabiWindowActivityObserver`, `metal_take_window_activity`, and the `window_resize` / `window_exposure` transition tests in `test_frame_activity`.

CLASS: MISSING

---

### #546 — Futures and SSE completions have no frame-wake contract

**What was wanted.** Let network, disk, auth, settings, send, stream, and live-event work finish while the UI is idle, then paint the result on the next callback.

**Measured result.** Loader futures are polled inside the full UI frame today. Without an outside-frame readiness check, a 2 fps idle policy adds up to 500 ms to a completed future. Hanabi checks every app and pane future plus each live-subscription dirty bit before deciding; `async_ready` and a changed SSE stamp wake immediately.

**Workaround.** `collect_app_frame_signals` performs zero-time future polls and reads SSE atomics outside `SystemManager::run`.

**Rejected ideas.** Keeping 60 fps whenever any future exists burns the full tree through slow network waits. A blind periodic poll violates one-frame completion latency.

**Minimal upstream change.** Supply a thread-safe invalidation token or wake handle that futures and event sinks can signal, integrated with `request_frame()`.

**Hanabi reference.** `src/frame_activity_collect.h::{frame_future_ready,collect_app_frame_signals}` covers both panes, superseded futures, list/send/stream/auth/settings work, and every live subscription.

CLASS: MISSING / PERFORMANCE

---

### #547 — Timers cannot publish their next visual deadline

**What was wanted.** Sleep fully between static frames while preserving caret blink, toast expiry, auth polling, theme rotation, settings debounce, outbox retry, and transient feedback.

**Measured result.** Those deadlines live in unrelated systems or private fields. Hanabi conservatively gives timed work 10 full frames/s; deterministic idle stays at 2 frames/s. That is 93 frames/10 s for the timer prototype versus 20/10 s fully idle.

**Workaround.** `FrameActivity::Timer`, `Caret`, and `PendingFuture` share a 100 ms periodic cadence. Continuous visual motion remains in the 60 fps class.

**Rejected ideas.** A two-frame-per-second universal idle cadence makes a 500 ms blink visibly uneven and can delay a 1.5 s debounce. Keeping every timer at 60 fps gives back most of the power win.

**Minimal upstream change.** Let systems return `next_frame_at` and merge deadlines in the host; animations request continuous cadence only until their terminal frame.

**Hanabi reference.** `src/frame_activity.h::FrameCadence`, `collect_app_frame_signals`, and `tests/unit/test_frame_activity.cpp::caret_thinking_scroll_and_stream_keep_their_cadence`.

CLASS: MISSING / PERFORMANCE

---

### #548 — Frame `dt` measures display callbacks, not time between frames the app actually ran

**What was wanted.** Skip callbacks while idle without slowing simulated-time timers and easing by the same factor.

**Measured result.** `graphics::get_frame_time()` remains one display interval even after ten callbacks were skipped. Feeding it unchanged makes a ten-second toast last roughly a minute at 10 fps. Hanabi instead passes elapsed time since the last full frame, capped at 100 ms; 60 fps motion receives its normal step and periodic timers keep wall cadence.

**Workaround.** The host tracks the last admitted frame and derives `dt` from `CLOCK_MONOTONIC` time.

**Rejected ideas.** Reusing callback `dt` slows timers. Passing an unbounded resume delta can complete a new animation in one frame after a long occlusion.

**Minimal upstream change.** Distinguish callback delta from rendered-frame delta, or let the host provide simulation time explicitly when it admits a frame.

**Hanabi reference.** `src/main.cpp::app_frame` derives and caps the admitted-frame `dt`; `run_idle_timing` uses the same rule in the deterministic probe.

CLASS: SHARP EDGE

---

### #549 — The production frame callback has no headless cadence harness

**What was wanted.** Gate idle work per second and transition cadence without opening a window or relying on a busy machine's wall clock.

**Measured result.** Existing headless paths call `SystemManager::run` directly and bypass `app_frame`; existing perf gates therefore measure every requested frame, not whether the host should have requested it. The new 1,200-callback probe reports 20 full frames, 1.880 CPU ms/s, and 1,774.1 allocations/s, and fails at 1,200 / 128.412 / 71,044.1 with retention disabled.

**Workaround.** Hanabi's headless probe runs the real system tree behind the same pure `FrameActivityPolicy` and measures thread CPU plus operator-new counts per logical second.

**Rejected ideas.** Multiplying per-frame cost by an assumed FPS cannot test transitions or prove the policy ran. A windowed gate is nondeterministic and steals focus.

**Minimal upstream change.** Provide a host-loop test driver with synthetic callback time, event injection, admitted-frame count, and retained-frame semantics.

**Hanabi reference.** `src/main.cpp::run_idle_timing`, `scripts/idle_gate.sh`, `make idle-gate`, and `docs/perf/IDLE.md`.

CLASS: TESTING / MISSING

---

**Range note.** #550-#559 are intentionally unassigned after the session-lifecycle audit. The work needed no new afterhours primitive: the sidebar panel uses the existing scroll, button, clipping, and draw callbacks; the tab menu uses the existing secondary-surface pattern; and fork plus notification behavior lives below or above the UI framework. The only framework constraints encountered are already recorded as #112/#458 (no native accessibility semantics for the icon-only sidebar toggle) and #326/#420 (fixed-height application windowing for a bounded variable catalog).

**Hanabi references.** `src/ecs/sidebar_system.h::render_subagent_sidebar` is the bounded, open-only panel; `src/ecs/tab_model.h::close_all` owns pane-safe tab teardown; `src/util/notify_events.h::native_event` owns muted native-notification suppression; and `tests/ui/sidebar_subagents.e2e`, `tests/ui/tab_close_all.e2e`, and `tests/ui/session_btw_fork.e2e` are the live UI evidence. `vendor/afterhours` remains unchanged.

**Remaining measured cost.** Closed panel: zero child-session requests and zero child-row builds; the persistent toggle itself adds six steady-state allocations per frame (`home20` 811 → 817). Open panel: one catalog request capped at 2,000 child summaries, one O(n) status/filter pass, and 29 of 410 matching child rows built in the 2,000-session stress fixture. Close-all is O(open tabs); fork is one capability read plus one control-lane request for `/btw`, or one control-lane request for a bare fork.
# Afterhours gap #590 — button variants drop per-widget text inset

## Observation

`ComponentConfig::with_text_inset()` is present but misses two paths used by Hanabi's immediate buttons. `ComponentConfig::apply_overrides()` does not copy `text_inset` when `button()` applies a visual variant, and `draw_text_in_rect()`'s immediate single-line branch still constructs `Vector2Type margin_px{5.f, 5.f}` instead of using the inset it received. A button configured with `.with_text_inset(28, 0)` therefore renders at five pixels.

This is visible in Hanabi's model and effort pickers: the selection radio occupies x+9 through x+21 while the label starts at x+5 and paints through the mark.

## Hanabi workaround

`src/ecs/main_pane_system.h::render_model_popover` and `render_effort_popover` compensate the immediate renderer with `HasLabel::text_x_offset` after `button()` has applied its variant. `tests/ui/composer_model_picker.e2e` and `composer_effort_picker.e2e` exercise the rows; screenshot baselines `18j` through `18m` preserve the result.

## Minimal upstream fix

Copy a non-empty `overrides.text_inset` in `ComponentConfig::apply_overrides()`, pass the supplied inset into the immediate single-line `position_text_ex()` call, and add a button-variant test proving the configured inset survives the merge and changes the rendered origin. The existing plain/wrapped text-inset tests do not cover both paths.

CLASS: FOOTGUN
