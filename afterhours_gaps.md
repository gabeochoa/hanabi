# afterhours gaps (from building hanabi)

Running log of capabilities the vendored `afterhours` immediate-mode UI does
not (readily) provide, discovered while building hanabi's app code. These are
NOT implemented in `vendor/afterhours/` (that submodule is owned by external
maintainers and off-limits). Each entry is a TODO: what was missing, why it was
wanted, the app-code workaround used, and the minimal upstream change that
would help.

Nothing here BLOCKED Phase 2 — every item has a working app-code workaround.

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

---

## Animation gaps (V2 / post-MVP — surfaced by docs/animation-assessment.md)

The `Anim` declarative builder (`plugins/ui/animation_config.h`) and the
key-based manager (`plugins/animation.h`) cover the core delight set (hover,
press-spring-back, appear fade, loop pulse, idle float, count tick). The items
below are the residual gaps for the "delightful V2" direction. All are
NON-BLOCKING and have an app-code workaround (the manual per-frame lerp/spring
pattern already proven in `src/ecs/layout_system.h`). Do NOT patch vendor.

### #8 — No per-item stagger / delay on declarative animations
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

### #9 — No exit / "leaving" animation (OnExit) in immediate mode
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

### #10 — No one-shot "value/state changed" trigger on a widget
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

### #11 — No shimmer-sweep / gradient-mask primitive
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

### #12 — No drag gesture + spring-to-slot path
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
