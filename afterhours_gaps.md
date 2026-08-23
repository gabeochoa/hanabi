# afterhours gaps (from building hanabi)

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
- #27 immediate-mode clears + rebuilds the whole tree every frame (no retained/dirty layer — the idle-frame floor)

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

**Resolved / corrected**
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

### #7 — (watch) memory knobs for RAM budget < 250 MB
hanabi targets peak RSS < 250 MB. Not a gap yet (baseline ~70 MB windowed), but
IF we later need to cap/evict GPU-side memory (font atlas is fixed 2048x2048 in
setup_sokol_gl_and_fonts; per-texture image cache), afterhours may not expose an
API to size/evict those. If we hit the ceiling: do NOT patch vendor — record the
exact knob needed here (e.g. configurable font-atlas dimensions, a texture-cache
eviction hook) and work around in app code (e.g. our own image-cache eviction for
textures we own). Placeholder until measured.

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

### #28 — a 2nd column child of a custom-background div doesn't render; on_draw_fg on a bg div doesn't fire (hanabi-observed)
- **Gap:** In the transcript, the user bubble is a `div` with `.with_custom_background(...)` + FlexDirection::Column. Adding a SECOND child after the first (a text label) — verified with a bright test label — does NOT render the 2nd child. Separately, attaching `.with_on_draw_fg(...)` to the bubble div itself (which has a custom background) never fires the callback (verified with a bright filled-rect probe — nothing drew). Tool-row checks DO draw via on_draw_fg, but they're transparent-bg children inside a Row, not children of a custom-bg Column.
- **Impact:** Blocked the "WhatsApp-style sync check as a nested badge under the bubble" approach. Worked around by appending a font-safe text suffix to the bubble's single body label ("· sent" etc.). A proper trailing badge/glyph row inside a filled bubble needs either (a) on_draw_fg to fire on bg divs, or (b) multi-child layout to render for custom-bg columns.
- **Repro:** add a 2nd `div(mk(bub.ent(), 9), ...label...)` after the user_text label; it doesn't appear. Add on_draw_fg to `bub`; it doesn't fire.
- **Also:** unicode U+2713 (✓) renders BLANK in the bundled font (JetBrainsMono/Roboto) at MICRO size — a check glyph must come from the icon atlas (draw_at "star"-style) or a drawn shape, not a text label.

### #29 — text_input has no placeholder support
- **Gap:** `afterhours::ui::imm::text_input` renders an empty field with no placeholder/ghost-text affordance when the bound string is empty. The hanabi composer + sidebar search both show a blank box with no "Reply…" / "Search…" hint. A `with_placeholder("…")` (rendered as faint text, cleared on first keystroke, not part of the value) would fix it cleanly.
- **Impact:** minor UX — the composer gives no cue what to type. Worked around by adjacent labels elsewhere; a real placeholder needs input support.
- **Related:** gap #17 (text_input forces its own Secondary bg + derives font size from height, ignoring per-widget colors) — same widget, same "input is hard to style" family.

### #30 — scroll is a raw wheel-delta add (no smoothing/momentum) → feels janky vs native macOS
- **Gap:** `HandleScrollInput` does `scroll_offset += direction * wheel * speed` — the rendered offset jumps by the raw wheel delta each event. There's no eased/target-based scrolling, so on macOS (where native scroll has momentum + sub-pixel smoothing) hanabi's scroll feels stepped/janky even at 100+fps. This is the "scroll perf" complaint — it's smoothness, not framerate (frame cost on a 120-msg transcript is only ~5.8ms).
- **Fix (PROVEN, vendor_patches/30-smooth-eased-scrolling.patch):** add `scroll_target` (wheel writes here) + `scroll_smoothing` factor; `scroll_offset` eases toward `scroll_target` once per frame (before the mouse-inside early return so an in-flight glide keeps animating). `scroll_smoothing >= 1` = instant (byte-identical legacy default); 0.28 = smooth glide that settles exactly (0.5px snap). clamp_scroll clamps both. Ease math unit-verified (first step 28%, settles ~21 frames ≈ 0.2-0.35s).
- **hanabi-side (committed, SFINAE-guarded):** `apply_scroll_prefs` sets `scroll_smoothing=0.28` (env `HANABI_SCROLL_SMOOTH` overrides); programmatic offset writes (jump-to-bottom, scrollbar drag/page) sync `scroll_target`. All guarded via `hanabi::has_smooth_scroll<>` detection so hanabi compiles against BOTH pinned edfe234 (no-op) and the patched afterhours (active) — verified both directions build 0 + test 8/8. Activates automatically when Gabe lands the patch + bumps the pointer.

---
## Reusable app-scaffolding gaps (survey 2026-08-03) — what a NATIVE DESKTOP app needs that afterhours doesn't provide

afterhours is a game/UI framework; hanabi is the first *native macOS desktop app* on it, so it had to hand-roll all the OS-integration + packaging scaffolding below. These are candidates to upstream so the next desktop app doesn't re-implement them. Grouped by whether afterhours is MISSING it or HAS-but-unusable.

### #31 — MISSING: macOS `.app` bundle packaging (Info.plist, Resources, URL schemes)
- afterhours has zero bundling support. hanabi's `makefile` hand-writes the whole `.app`: `Contents/MacOS/<exe>`, `rsync` of `Contents/Resources/`, and a heredoc'd `Info.plist` (CFBundle* keys, LSMinimumSystemVersion, NSHighResolutionCapable, LSApplicationCategoryType, **CFBundleURLTypes** for the `hanabi://` scheme).
- Upstream shape: a reusable `bundle.mk` include or a `tools/mk_bundle.sh <exe> <name> <id> <plist-extras>` that any afterhours desktop app can call. Also a Linux `.desktop` + Windows resource equivalent for cross-platform.

### #32 — HAS-BUT-UNUSABLE: `files::get_resource_path` resolves relative to CWD, not the executable/bundle
- `ProvidesResourcePaths` sets `resource_folder_path = fs::current_path() / root_folder` (files.cpp ~54). A launched `.app` has CWD `/`, so bundled resources under `Contents/Resources/` are never found — the app can only find resources when run from its build dir. This makes the resource API unusable for the exact case (a shipped bundle) it's most needed for.
- hanabi workaround (src/preload.cpp `get_exe_dir()` + `resolve_resource_root()`): platform-specific exe-path lookup (`_NSGetExecutablePath` / `/proc/self/exe` / `GetModuleFileNameA`) then probe `<exe>/resources`, then `<exe>/../Resources` (.app), then CWD fallback — and pass THAT to `files::init`.
- Upstream fix: `files::init` should resolve the resource root from the executable path (with a CWD/dev fallback), not raw CWD. The exe-dir helper is trivially generic and belongs in the files plugin.

### #33 — MISSING: native menu-bar extra (NSStatusItem), notifications, global hotkey, Spotlight
- afterhours has no menu-bar / tray, no native notifications, no global-hotkey registration, no Spotlight/indexing. hanabi implemented all of these in `src/native_extras.mm` (NSStatusItem status item + menu; UNUserNotification/NSUserNotification click-to-open; Carbon RegisterEventHotKey Cmd+Shift+N; CoreSpotlight donation). These are generic desktop-app needs — a thin `afterhours/desktop` (mac) shim (menubar item, post-notification, register-hotkey, on-activate callback) would be broadly reusable.

### #34 — MISSING: URL-scheme / deep-link handling (Apple-event kInternetEventClass/kAEGetURL)
- A non-App-Store bundle receives `myapp://...` opens via the classic Apple-event route (`kInternetEventClass`/`kAEGetURL` on the shared NSAppleEventManager). hanabi hand-registers a handler (`native_extras.mm handleGetURLEvent`) and drains it into the app via a pending-open queue. Generic: afterhours could expose `desktop::on_open_url(cb)` + auto-register the handler when the bundle declares CFBundleURLTypes.

### #35 — MISSING: runtime font swap is possible but there's no "list installed system fonts" / font-picker primitive
- FontManager.load_font(name, path) DOES allow swapping a named font at runtime (good — hanabi uses it for a font-choice pref). But there's no way to ENUMERATE available system fonts (macOS CTFontManagerCopyAvailableFontFamilyNames / fontconfig on Linux), so an app can't offer "pick any system font" without its own platform code. A `fonts::list_system_families()` would enable a real font picker. (hanabi ships a bundled-font CHOICE instead — Roboto default + Atkinson Hyperlegible, both OFL/Apache, no enumeration needed.)

### #36 — MISSING: config/save path is fine, but no "app data/cache dir" distinct from config
- `files::get_config_path()` (per-app config dir) works and hanabi uses it. But there's no separate get_cache_path() (XDG cache / ~/Library/Caches) — hanabi puts its transcript disk-cache under the config dir. Minor; a cache-vs-config split is the platform-correct convention.

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

### #33 — text_input has NO Shift+Enter newline (single-line only; Enter is the only submit)
- **Symptom (Gabe):** "shift+enter should make a new line." The single-line `text_input`
  treats plain Enter as submit (WidgetPress) and has no Shift+Enter → insert '\n' path.
  `text_area` (multiline) exists but the composer uses `text_input`.
- **Fix direction (vendor):** in `text_input`'s key handling, when WidgetPress fires AND
  Shift is held, insert '\n' instead of invoking on_submit (and let the field grow / wrap).
  Requires the field to render multi-line (see #34). Alternatively hanabi swaps the
  composer to `text_area` — but that widget needs the same wrap fix and a submit binding.

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

- **Minimal upstream fix.** One bool on `Config` (`enable_file_drop`) copied
  into `desc.enable_dragndrop`, and the dropped-files event surfaced the way
  the other sokol events already are — a `files_dropped` callback on `Config`,
  or a `get_dropped_files()` the frame can poll. Everything underneath it is
  already written and shipping inside sokol_app.
