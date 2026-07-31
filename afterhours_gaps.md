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
