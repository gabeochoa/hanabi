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
  #13/#15) — a raw low-alpha color would render as a harsh opaque block. v1 is an
  **INDICATOR ONLY**: it accurately reflects position + content ratio and updates
  every frame as the wheel scrolls, but the thumb is not draggable.
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
