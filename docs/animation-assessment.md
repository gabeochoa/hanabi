# afterhours animation capability assessment (V2 / post-MVP)

**Question:** the V2 direction wants "delightful animations" and empty-state
illustrations. Does the vendored `afterhours` animation library support the kinds
of motion we'll want, and if not, what do we build on top in app code?

This is a **read-only** assessment of `vendor/afterhours/` (off-limits to edit).
Every gap it surfaces is appended to `afterhours_gaps.md` as V2/post-MVP,
non-blocking. Nothing here changes app behavior.

---

## 1. What afterhours actually ships

There are **two** animation systems in the vendor tree, and they're different:

### A. Declarative per-widget animation (the `Anim` builder)
Files: `vendor/afterhours/src/plugins/ui/animation_config.h`,
`.../component_config.h`, applied by `.../component_init.h`.

You attach an animation to any immediate-mode widget with the config builder:

```cpp
// component_config.h:461-465
ComponentConfig &with_animation(const Anim &anim) {
  animations.push_back(anim.build());   // stored in std::vector<AnimationDef> animations;
}
```

**Triggers** (`animation_config.h:14-20`, `AnimTrigger`):
`OnAppear`, `OnClick`, `OnHover`, `OnFocus`, `Loop`. All five are real and wired
up in `component_init.h:443-506`:
- `OnAppear` starts once, first time the entity is seen (`has_appeared` latch, line 444).
- `OnClick` / `OnHover` / `OnFocus` are **edge-triggered off interaction state**
  (`was_active` / `was_hot` / `has_focus`, lines 481-493) and, importantly,
  **auto-reverse on the falling edge** (line 499-503): press animates to
  `to_value`, release animates back to `from_value`. That's exactly press-and-
  spring-back for free.
- `Loop` ping-pongs between `from`/`to` forever (lines 465-478).

**Animatable properties** (`animation_config.h:25-31`, `AnimProperty`):
`Scale`, `TranslateX`, `TranslateY`, `Rotation`, `Opacity`. How each is applied
(`component_init.h:509-529`):
- `Scale` — **multiplicative** into `HasUIModifiers.scale` (visual scale after
  layout; does not reflow siblings).
- `TranslateX` / `TranslateY` — **additive** pixel offset.
- `Rotation` — **additive**, in degrees.
- `Opacity` — multiplies `HasOpacity.value`.

**Curves** (`animation_config.h:36-42`, `AnimCurve`): `Spring`, `EaseOut`,
`EaseIn`, `EaseInOut`, `Linear`. Builder sugar in `animation_config.h:142-167`:
`.spring(freq=12, decay=8)`, `.ease_out(dur)`, `.ease_in(dur)`,
`.ease_in_out(dur)`, `.linear(dur)`. Spring is a real damped-spring integrator
(`animation_config.h:261-279`), the eases are the standard quad/quad-in-out
closed forms (`apply_easing`, lines 282-297).

README confirms the public surface (`vendor/afterhours/src/plugins/ui/README.md:235-246`):
```cpp
.with_animation(Anim::on_click().scale(0.9f, 1.0f).spring())
.with_animation(Anim::on_appear().opacity(0.0f, 1.0f).ease_out(0.3f))
.with_animation(Anim::on_hover().translate_y(0.0f, -4.0f).ease_out(0.15f))
.with_animation(Anim::loop().scale(0.95f, 1.05f).ease_in_out(1.0f))
```

### B. Key-based tween manager (the general animation plugin)
File: `vendor/afterhours/src/plugins/animation.h`.

A separate, lower-level system keyed by an arbitrary enum/`CompositeKey`, not
tied to a widget. It supports:
- `.from(v).to(value, duration, EasingType)` (lines 161-188), **segment queues /
  `.sequence(...)`** (lines 189-206) so you can chain multi-step timelines,
  `.hold(dur)` (207-213), `.loop_sequence(...)` (231-237).
- Lifecycle hooks: **`.on_complete(cb)`** (214-218) and **`.on_change` /
  `.on_step(step, cb)`** watchers (219-230) — the value crosses a quantized
  threshold and fires a callback. This is the primitive for a **count roll /
  ticker** (animate a float 0->N, `on_step(1, ...)` updates the displayed int).
- `one_shot(key, fn)` (278-296) to fire an appear-style tween exactly once.

Its `EasingType` here is a **narrower set**: only `Linear`, `EaseOutQuad`, `Hold`
(`animation.h:18`). No spring, no ease-in / ease-in-out in the key-based path
(those live only in the declarative `AnimCurve`).

### C. Custom easing / bezier?
**No.** Both systems expose a **fixed enum** of curves. There's no
`std::function<float(float)>` custom-easing hook, no cubic-bezier control points,
no per-keyframe easing beyond the enum. If we want a specific bezier feel, we
approximate with the nearest enum + a spring, or hand-roll it in app code with a
manual per-frame lerp (the pattern we already use — see §3).

### D. How the app already animates (evidence it's reachable)
`src/ecs/layout_system.h:11-42` — the collapsible sidebar's **width** is tweened
entirely by hand: store `sidebarAnimFrom/Target/T` on the layout component,
advance `animT += dt/0.18f` each frame, apply a local `smoothstep(t)`
(lines 15-18), write the eased width. This is the established afterhours pattern
for animating a **layout dimension** (which the declarative system deliberately
does NOT touch — it only does post-layout visual scale/translate/opacity).

---

## 2. Desired "delightful" set — Supported / Partial / Missing

| # | Desired animation | Verdict | Rationale |
|---|---|---|---|
| 1 | Hover lift/scale on rows & cards | **Supported** | `Anim::on_hover().translate_y(0,-4).ease_out(.15)` and/or `.scale(1.0,1.03).spring()`; auto-reverses on mouse-out. README:240. |
| 2 | Press/tap feedback (scale-down + spring back) | **Supported** | `Anim::on_click().scale(0.9,1.0).spring()`; the falling-edge auto-reverse (`component_init.h:499-503`) IS the spring-back. Textbook use. |
| 3 | Enter transition for list rows (fade+slide in) | **Supported** (per-row) | `Anim::on_appear().opacity(0,1).ease_out()` + `.translate_y(8,0)`. Two `with_animation` calls on the row. |
| 3b| **Staggered** enter (rows cascade as threads load) | **Partial** | No per-item delay/stagger param on `AnimationDef`. Every `OnAppear` starts the frame the widget first renders — same phase, no offset. Workaround: key-based manager with `one_shot(RowFadeKey, i)` + a `.hold(i*0.03f)` lead segment, or gate each row's *first render* by index. Doable in app code; not declarative. |
| 3c| Exit transition for rows (fade/slide out) | **Missing** | Immediate-mode: when a row's data is gone the widget simply isn't emitted next frame — there's no "leaving" state, no exit trigger, nothing keeps a departing widget alive to animate out. Only `OnAppear`, no `OnExit`. |
| 4 | Sidebar collapse width tween | **Partial** (works, but not via `Anim`) | Already done manually (`layout_system.h`). The declarative system animates *visual scale/translate*, **not layout width**, so it can't reflow the main pane. Manual smoothstep is the right tool; the `Anim` system can't do it declaratively. |
| 5 | Tab open/close (slide/scale in, fade out) | **Partial** | Open: `on_appear().scale(0.9,1).spring()` + opacity — fine. Close/out: same exit-animation gap as #3c (no OnExit); the tab just stops being emitted. Open is Supported, close-out is Missing. |
| 6 | Status-change pulse (working-ring pulse; row flash on flip to needs-you) | **Supported / Partial** | Steady pulse = `Anim::loop().opacity(0.45,1.0).ease_in_out(0.8)` — matches the mock's `ring-pulse` keyframe exactly (mock/index.html:315-316). A **one-shot flash on state change** (row flips to blocked) is **Partial**: no "value changed" trigger on a widget; drive it from the key-based manager's `on_change`/`on_complete`, or app-side detect the transition and fire a `one_shot` fade. |
| 7 | Smart-view / screen cross-fade or slide transition | **Partial** | Individual incoming widgets can `on_appear().opacity(0,1)`. But a true **cross-fade between two screens** needs the *outgoing* screen to fade out while the incoming fades in — that's the exit gap (#3c) plus there's no screen-level transition orchestrator. Workaround: app-side hold a `transitionT` (like the sidebar tween) and drive both panes' `with_opacity`. |
| 8 | Skeleton / shimmer loading placeholders | **Partial** | Structure is easy (draw placeholder rects). A **moving shimmer sweep** (a highlight band translating across) is a moving-gradient effect; afterhours can `loop().translate_x(...)` a highlight widget, but there's no gradient-mask primitive, so it's a coarser approximation than CSS `background-position` shimmer. Pulsing-opacity skeleton = Supported; sweeping shimmer = Partial. |
| 9 | Empty-state illustration entrance (fade+float) + idle loop | **Supported** | Entrance: `on_appear().opacity(0,1).ease_out(0.4)` + `translate_y(10,0)`. Idle float: `loop().translate_y(0,-6).ease_in_out(2.0)`. Both are core triggers/properties. The illustration itself draws via `with_on_draw_fg` custom shapes (already proven, `afterhours_gaps.md` #4). |
| 10| Number / badge count roll or tick | **Supported** (key-based path) | The key-based manager's `on_step(1.0f, cb)` / `on_change(quantize, cb)` (`animation.h:219-230`) is purpose-built: tween a float 0->N, get a callback each integer crossing to update the label. Not on the declarative widget path, but fully supported by the plugin. |
| 11| Spring-based drag (tab reorder) with settle | **Missing** | No drag gesture model and no "animate toward a moving/dropped target" affordance in either system. `OnClick`/`OnHover` are boolean edge triggers, not pointer-delta drags. Reorder + spring-to-slot must be hand-rolled: app-side track drag delta, and on drop run a manual spring (reuse `anim::spring` from `animation_config.h:261` on an app-owned `AnimTrack`, or the key-based `.to(...)`). |

**Score:** 6 Supported, 4 Partial, 2 Missing (counting 3/3b/3c and 5 splits by their worst sub-part). The everyday micro-interactions (hover, press, appear, pulse, idle float, count tick) are all first-class. The gaps cluster around **exit/transition orchestration** and **gesture-driven** motion.

---

## 3. Gaps → logged to `afterhours_gaps.md`

Appended as entries #8-#12 (V2/post-MVP, non-blocking, app-code workaround noted, vendor untouched):
- **#8 Staggered list-enter** (no per-item delay on `AnimationDef`).
- **#9 Exit / "leaving" animations** (immediate-mode has no OnExit; covers row exit, tab close-out, screen cross-fade out).
- **#10 One-shot state-change trigger on a widget** (row flash when status flips; no "value changed" declarative trigger).
- **#11 Shimmer sweep primitive** (no gradient-mask; pulsing skeleton fine, sweeping shimmer coarse).
- **#12 Drag gesture + spring-to-slot** (no pointer-delta drag model for tab reorder).

The universal workaround is the **manual per-frame lerp/spring** already proven
in `layout_system.h` (smoothstep width tween) — app owns an `AnimTrack`/float,
advances it by `dt`, applies it via `with_scale`/`with_opacity`/`with_translate`.
For count/threshold work, the key-based manager's `on_step`/`on_complete` is the
right primitive. None of this requires touching vendor.

---

## 4. Verdict

**Good enough? YES — for the delight that matters most.** The high-frequency
micro-interactions users actually feel — hover lift, press-and-spring-back,
row/card fade-in, the working-ring pulse, empty-state fade+float+idle, and count
ticks — are all directly supported by the `Anim` builder or the key-based manager
with a one-line call. hanabi can ship a genuinely delightful V2 on the vendored
library as-is.

**What to build on top in app code (small, additive, no vendor changes):**
1. A thin **stagger helper**: index-keyed `one_shot` + `hold(i*step)` so list
   rows cascade in (fixes #8).
2. A tiny **transition orchestrator** (one app-owned `transitionT` float, same
   shape as the sidebar tween) to cross-fade smart-views / screens and fake exit
   animations by fading the outgoing pane before swapping (fixes #7, softens #3c/#5).
3. A **state-change watcher** in the sidebar/row systems that detects a status
   flip and fires a `one_shot` flash via the key-based manager (fixes #6 flash, #10).
4. A **drag+spring** path in the tab-bar system: app-tracked drag delta on the
   existing manual hit-test (afterhours_gaps #3), spring-to-slot on drop reusing
   `anim::spring` (fixes #11 drag).

**What (if anything) to request upstream — all low priority, none blocking:**
- A per-item **stagger/delay** field on `AnimationDef` (`delay`, `stagger_index`).
- An **OnExit / leaving** lifecycle so departing widgets can animate out (the
  single biggest structural gap; hard in pure immediate-mode, but a "keep-alive
  for N ms after last emit" affordance would unlock exit transitions cleanly).
- A **custom-easing hook** (`std::function<float(float)>` or cubic-bezier control
  points) for designers who want a specific curve beyond the 5 enums.
- Unify the two systems' easing sets (the key-based path lacks Spring/EaseIn/
  EaseInOut that the declarative path has).

Bottom line: the library clears the bar for "delightful." The delta to a truly
polished V2 is a handful of small **app-side** helpers — exit/transition
orchestration and drag-spring — built on the same manual-lerp pattern we already
use, not a dependency on any upstream change.
